# Unix Domain Socket IPC

## Overview

This project implements bidirectional inter-process communication (IPC) between a server and a client on Linux using Unix domain sockets with `AF_UNIX` and `SOCK_SEQPACKET`.

The intended use case is communication between a Qt application plugin acting as the server and a separate application acting as the client.

Both sides can send and receive arbitrary binary messages over the same connected socket. Sending is asynchronous, thread-safe, and backed by a bounded outgoing queue.

The design prioritizes:

- Nonblocking socket operations on application threads.
- Thread-safe sending from multiple producer threads.
- Packet boundaries preserved by the transport.
- Simple connection management.
- Safe integration with Qt's thread-affinity model.
- Predictable behavior when queues fill or connections are lost.

## Architecture

text

```
                  SERVER PROCESS
        ┌──────────────────────────────┐
        │ Qt main thread               │
        │                              │
        │ Server API: Send()           │
        │       │                      │
        │       ▼                      │
        │ Thread-safe outgoing queue   │
        │       │                      │
        │       ▼                      │
        │ I/O thread                   │
        │  - accept connections        │
        │  - authenticate clients      │
        │  - receive packets           │
        │  - send queued packets       │
        │       │                      │
        │       ▼                      │
        │ Packet handler               │
        │       │                      │
        │       ▼                      │
        │ Qt::QueuedConnection         │
        │       │                      │
        │       ▼                      │
        │ Qt object methods            │
        └──────────────┬───────────────┘
                       │
                       │ AF_UNIX
                       │ SOCK_SEQPACKET
                       │ Full duplex
                       │
        ┌──────────────┴───────────────┐
        │ CLIENT PROCESS               │
        │                              │
        │ Application threads          │
        │       │                      │
        │       ▼                      │
        │ Thread-safe outgoing queue   │
        │       │                      │
        │       ▼                      │
        │ I/O thread                   │
        │  - receive packets           │
        │  - send queued packets       │
        │       │                      │
        │       ▼                      │
        │ Packet handler               │
        │       │                      │
        │       ▼                      │
        │ Shared application data      │
        └──────────────────────────────┘
```

Each side uses one I/O thread to handle both sending and receiving. A separate sending thread is unnecessary because the socket is full-duplex and `poll()` can monitor multiple file descriptors.

## Transport

### Unix domain sockets

The server creates a filesystem socket, for example:

`/tmp/my.socket`

The client connects to that path.

Unix domain sockets are local to the machine; they do not communicate over a network interface.

### SOCK\_SEQPACKET

`SOCK_SEQPACKET` provides connection-oriented, ordered, reliable packet transport.

Each successful packet send is received as a separate message, preserving message boundaries. Unlike a TCP byte stream, the receiver does not need to reconstruct messages from arbitrary chunks.

The transport supports simultaneous communication in both directions over the same connection.

The protocol still needs to define what each packet means, how messages are identified, and how malformed data is handled.

### Binary messages

Packets may contain arbitrary binary data, including embedded zero bytes.

The application represents a packet using:

cpp

```
using Packet = std::vector<std::byte>;
```

A packet owns its contents and exposes its length through `.size()`.

Packets must not be treated as null-terminated strings unless the application protocol explicitly defines them as strings.

## Server behavior

The server runs inside the Qt plugin.

Its responsibilities are:

1. Create and bind the Unix domain socket.
2. Set filesystem permissions.
3. Listen for incoming connections.
4. Authenticate connecting processes.
5. Maintain at most one active client.
6. Receive packets from the active client.
7. Send queued packets to the active client.
8. Dispatch incoming messages to application logic.
9. Shut down cleanly.

### Connection policy

Only one client connection is allowed at a time.

When a connection arrives:

- If another client is connected, reject the new connection.
- Otherwise, retrieve the peer credentials using `SO_PEERCRED`.
- Reject the connection if the peer's UID is not `0` (root).
- Accept the connection if the credential check succeeds.

The listener remains available while a client is connected. Additional connections are accepted from the pending connection queue only to be rejected; they do not replace the existing client.

When the active client disconnects, the server clears its outgoing queue and becomes available for another client.

### Socket permissions

The server sets the socket path permissions to `0600`.

This restricts normal filesystem access to the socket owner. The effective owner must be configured appropriately for the intended deployment.

The peer credential check is an additional access-control measure. Filesystem permissions and peer authentication serve different purposes and should both be considered when deploying the service.

### Stale socket files

The server removes an existing socket path before binding.

This helps recover from stale filesystem entries after an unclean shutdown, but it must be used carefully: blindly unlinking a path can interfere with another live server using that path.

In production, ensure that the path is owned by the intended service and that only one server instance is allowed to manage it.

## Client behavior

The client is owned by the application.

Its responsibilities are:

1. Connect to the server.
2. Queue outgoing packets.
3. Send queued packets asynchronously.
4. Receive incoming packets continuously.
5. Invoke the registered packet handler.
6. Detect connection failures.
7. Clear pending messages and clean up resources.

Connection establishment is synchronous in the current design. It may fail or block briefly while the operating system establishes the connection. If connection setup must never block the caller, it should be moved to a worker thread or implemented as an asynchronous state machine.

After a connection is established, socket I/O is performed by the I/O thread.

## Threading model

### Server

The server uses two execution contexts:

- **Qt main thread:** Owns and controls the server. It can submit packets without waiting for socket I/O.
- **I/O thread:** Monitors the listener, active connection, and wake-up descriptor. It accepts connections, receives packets, and sends queued packets.

The server's public sending method does not wait for the socket to become writable. It copies and queues the message.

The packet handler runs on the I/O thread. It should perform only short operations, such as parsing the message and dispatching work to Qt.

### Client

The client also uses one I/O thread.

Any application thread may call `Send()` concurrently. A mutex protects the outgoing queue, and the I/O thread performs the actual socket writes.

The packet handler runs on the I/O thread. It can parse messages and update shared application data, provided that shared data is properly synchronized.

### Avoid blocking the I/O thread

Both packet handlers execute on the I/O thread. Slow work inside either handler delays further packet processing and sending.

Handlers should not perform expensive computation, wait on locks held by other long-running operations, or execute blocking I/O.

If work is expensive, hand it off to another thread or event loop.

## Qt integration

Qt objects generally have thread affinity: queued events and slots execute on the thread associated with the receiving object.

The server packet handler can use `QMetaObject::invokeMethod()` with `Qt::QueuedConnection` to schedule work on a Qt object.

Example:

cpp

```
SeqpacketServer server(
    "/tmp/my.socket",
    [this](const SeqpacketServer::Packet& packet) {
        auto copy = packet;

        QMetaObject::invokeMethod(
            this,
            [this, packet = std::move(copy)] {
                HandlePacket(packet);
            },
            Qt::QueuedConnection
        );
    }
);
```

The queued invocation returns without waiting for `HandlePacket()` to execute. The Qt event loop processes the call on the target object's thread.

Important considerations:

- The target object must remain alive while the handler can access it.
- The server should be stopped and its I/O thread joined before destroying objects that the handler accesses through raw pointers.
- Queued calls are processed only when the target thread's event loop is running.
- If the Qt event loop is overloaded, queued calls may accumulate.
- The packet is copied before queuing so its contents remain valid until the queued operation executes.
- Do not use `Qt::BlockingQueuedConnection` for this purpose; it makes the caller wait.

For high message rates, consider batching messages or limiting the amount of work scheduled into the Qt event queue.

## Sending API

Both sides expose a similar asynchronous interface:

cpp

```
bool Send(const void* data, std::size_t size);
bool Send(const Packet& packet);
```

The exact names may be adapted to the final class declarations.

### Send semantics

A call to `Send()`:

1. Validates the input.
2. Copies the data into an owned packet.
3. Acquires the outgoing queue mutex.
4. Checks the connection state and queue limit.
5. Enqueues the packet if permitted.
6. Wakes the I/O thread.
7. Returns.

The caller may safely modify or destroy the original buffer after `Send()` returns.

### Return value

A return value of `true` means the packet was accepted into the local outgoing queue.

A return value of `false` means the packet was not queued, for example because:

- There is no active connection.
- The queue is full.
- The object is shutting down.
- The supplied buffer or size is invalid.

A successful return does not mean the packet has been transmitted or processed by the peer.

### Thread safety

The outgoing queue is protected by a mutex, allowing concurrent calls from multiple application threads.

The mutex protects queue state and packet insertion/removal. Socket operations themselves are performed by the I/O thread.

Thread safety does not automatically extend to application data accessed by the packet handler or to concurrent lifecycle operations such as `Connect()` and `Disconnect()`. Unless explicitly implemented, callers must serialize lifecycle operations.

## Outgoing queue and overflow policy

The outgoing queue is bounded by message count.

The initial implementation uses a limit of 256 queued packets:

cpp

```
static constexpr std::size_t MaxQueuedPackets = 256;
```

This value can be adjusted based on expected traffic.

When the queue is full, new messages are rejected. Previously queued messages are retained.

This policy is intentional: normal operation should not fill the queue. A full queue indicates that messages are being produced faster than they can be transmitted or that the peer is not keeping up.

The application can log the failure, count dropped messages, or initiate recovery if appropriate.

### Why only message count?

The application knows that message sizes are small and bounded. A message-count limit is therefore sufficient for the intended workload, provided that maximum packet size is enforced separately.

If message sizes grow significantly in the future, add a total queued-byte limit as well.

## Disconnect behavior

When a connection is lost, the outgoing queue is cleared.

Messages queued for one connection must not be delivered to a later connection because they may be stale or intended for a different session.

The expected lifecycle is:

1. Detect connection loss.
2. Mark the connection as disconnected.
3. Clear pending outgoing packets.
4. Release the connected socket.
5. Allow the server to accept a new client, or allow the client to reconnect.
6. Start with an empty queue after reconnection.

The current design does not automatically reconnect the client. Reconnection policy belongs to the application or can be added as a separate feature.

## Sending and receiving details

### Nonblocking socket I/O

Sockets are configured or used for nonblocking I/O.

The I/O thread uses `poll()` to wait for relevant events:

- Listening socket readability: a connection is pending.
- Connected socket readability: an incoming packet is available.
- Connected socket writability: queued outgoing packets may be sent.
- Wake-up descriptor readability: new work or shutdown needs attention.

The thread sleeps when there is no work instead of continuously polling in a busy loop.

### Wake-up descriptor

An `eventfd` wakes the I/O thread when the outgoing queue changes or shutdown is requested.

This avoids polling the queue periodically and lets `poll()` wait indefinitely until an event occurs.

### Packet size

The example implementation uses a 65,536-byte receive buffer. That is an implementation limit, not a universal maximum for all Unix domain `SOCK_SEQPACKET` configurations.

The application protocol should define a maximum packet size. The receiver must also detect truncation rather than assuming that a successful `recv()` always returned the complete message.

If messages can exceed the receive buffer, use `recvmsg()` and inspect its flags, including `MSG_TRUNC`, or otherwise establish a suitable size contract.

### Empty packets

The example sending API rejects zero-length messages. This is an application-level choice, not a limitation of `SOCK_SEQPACKET`.

If empty packets are meaningful, explicitly support them and handle zero-length receives correctly. Do not automatically interpret a zero-length receive as connection closure without confirming the semantics for the chosen socket type and API.

## Delivery guarantees

The application does not require delivery acknowledgments.

The socket provides reliable, ordered transport while the connection remains functional, but the sending API does not guarantee that the peer's application processed a message.

There are several distinct outcomes:

- **Queued:** The local application accepted the message.
- **Sent:** The local kernel accepted the packet for transmission.
- **Received:** The peer's kernel delivered the packet to its receiving process.
- **Processed:** The peer's application completed its handling of the message.

The current design confirms only the first outcome through `Send()`'s return value.

If the connection breaks, queued packets may be discarded. A packet already accepted by the local kernel may or may not have been processed by the peer. Since acknowledgments are unnecessary, the application should tolerate lost in-flight messages where appropriate.

## Error handling and recovery

The implementation should distinguish the following conditions:

| Condition | Expected behavior |
| --- | --- |
| No active connection | Reject new outgoing messages |
| Queue full | Drop the new message and report failure |
| Socket temporarily not writable | Keep the packet queued and retry when writable |
| Connection reset or send failure | Mark disconnected, clear queue, close connection |
| Peer disconnects | Clear queue and update connection state |
| Non-root client attempts to connect | Reject connection |
| Additional client attempts to connect | Reject connection |
| Invalid packet contents | Reject or ignore according to application protocol |
| I/O thread encounters unrecoverable error | Stop the I/O loop and report failure |
| Shutdown requested | Wake the I/O thread, join it, and release resources |

Do not log every expected temporary condition at a high severity. A full queue, for example, should be observable but should not flood logs under sustained load.

## Shutdown and resource lifetime

Destruction must stop the I/O thread before closing file descriptors that it uses.

The intended shutdown sequence is:

1. Prevent new outgoing messages.
2. Signal the wake-up descriptor.
3. Allow the I/O thread to exit.
4. Join the I/O thread.
5. Close the connected socket and other file descriptors.
6. Remove the server's filesystem socket entry if appropriate.

Avoid closing a file descriptor from another thread while the I/O thread is still using it. File descriptor reuse and concurrent system calls can create difficult race conditions.

Do not destroy the server or client while another thread is still calling its methods. If lifecycle operations can run concurrently, introduce explicit synchronization for them.

## Security considerations

The server uses a local filesystem socket and checks peer credentials, but these measures do not replace application-level validation.

Recommendations:

- Ensure the socket path and its containing directory are controlled by the intended service.
- Avoid blindly deleting a socket path that might belong to another live server.
- Verify peer credentials before trusting messages.
- Validate all packet lengths, message types, and payload fields.
- Treat received packets as untrusted input even when the peer UID is root.
- Avoid parsing raw packet data into C++ structures without validating size, layout, alignment, and byte order.
- Do not assume that a UID check proves the connecting process is the intended application; it proves the peer's kernel-reported user identity.
- Consider additional checks if multiple root processes may exist on the system.

## Known implementation caveats

The first implementation is a foundation and should be reviewed and tested before production use.

In particular:

1. **Connection lifecycle synchronization:** Concurrent `Connect()`, `Disconnect()`, destruction, and `Send()` calls need a clearly defined synchronization contract. The simple queue mutex alone does not make every lifecycle operation thread-safe.
2. **Queue clearing:** Clearing the queue when disconnecting must be coordinated with enqueueing so messages cannot accidentally survive a connection transition.
3. **Socket ownership:** The I/O thread should be the only thread performing I/O on the connected socket. Other threads should communicate through queues and wake-up events.
4. **Receive truncation:** The receiver must detect oversized packets and reject them safely.
5. **Empty packet semantics:** Zero-length packets must not be confused with orderly connection closure.
6. **Eventfd errors:** Wake-up writes and reads should handle `EINTR`, `EAGAIN`, and unrecoverable errors appropriately.
7. **Shutdown ordering:** All file descriptors and referenced objects must remain valid until the I/O thread exits.
8. **Handler lifetime:** The handler must not access destroyed objects. The server should be stopped before dependent Qt objects are destroyed.
9. **Handler exceptions:** Exceptions must not escape the I/O thread and cause `std::terminate()`.
10. **Thread blocking:** A slow handler delays further I/O. Qt actions should be queued, not executed synchronously on the I/O thread.
11. **Queue overload:** A bounded message count limits the number of entries, but not the memory occupied by an individual packet. Enforce a maximum packet size.
12. **Socket path management:** Unlinking a stale path should not interfere with a live server instance.
13. **Connection-state visibility:** Atomic state flags provide basic visibility, but they do not by themselves synchronize all related resources or guarantee that state cannot change immediately after a query.
14. **Client connection setup:** The current synchronous `Connect()` may block. Use a worker thread if it must not block the caller.
15. **Unexpected I/O thread termination:** Expose or log a failure state so the application can decide whether to restart the server or reconnect the client.

## Testing checklist

Before production deployment, test the following scenarios.

### Basic communication

- Server starts and creates the socket.
- Client connects successfully.
- Server sends packets to the client.
- Client sends packets to the server.
- Both directions operate simultaneously.
- Packet boundaries and binary payloads are preserved.

### Concurrency

- Multiple client threads call `Send()` simultaneously.
- Multiple server-side producers call `Send()` simultaneously.
- Packet ordering matches the order in which packets are accepted into each queue.
- Shared application data is accessed without data races.
- The Qt main thread remains responsive during heavy traffic.

### Connection management

- Non-root clients are rejected.
- A second client is rejected while one is connected.
- The server accepts a new client after the existing client disconnects.
- The outgoing queue is cleared on disconnect.
- No old queued packet is sent to a new connection.

### Failure handling

- Queue overflow drops new messages.
- A disconnected peer is detected.
- Oversized and malformed packets are handled safely.
- The client handles server shutdown.
- The server handles client shutdown.
- Repeated startup and shutdown do not leak file descriptors.
- Shutdown during active traffic does not deadlock.
- Thread creation and socket setup failures are handled.

### Qt integration

- Packet handlers execute on the intended thread.
- Queued invocations do not block the I/O thread.
- Queued work does not access destroyed objects.
- A stopped or overloaded Qt event loop is handled appropriately.

## Summary

The intended system is a small, asynchronous, bidirectional IPC layer built on Linux Unix domain sockets.

Each side uses a single I/O thread, a bounded thread-safe outgoing queue, and a packet handler. The server rejects additional clients while connected, authenticates peers, and integrates with Qt through queued method invocation. The client allows concurrent sending from application threads and processes incoming packets in its handler.

The most important operational rules are:

- Keep socket I/O on the I/O thread.
- Copy outgoing packet data before returning from `Send()`.
- Drop new packets when the bounded queue is full.
- Clear queued packets on disconnect.
- Dispatch Qt work with `Qt::QueuedConnection`.
- Validate every received packet.
- Coordinate shutdown and object lifetimes carefully.

These rules provide a straightforward foundation for reliable local communication without blocking the Qt main thread.
