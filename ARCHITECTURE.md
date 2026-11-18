# SmartKeyBinds Architecture

## 1\. Overview

SmartKeyBinds is a Linux keyboard interception and remapping system designed around three independent responsibilities:

1. Input management and macro execution.
2. Desktop state acquisition and command execution.
3. User-defined keyboard behavior.

The initial desktop integration is implemented as a KWin plugin.

The engine does not directly depend on KWin APIs. Communication between the engine and desktop integration is handled through IPC.

The architecture should allow additional context providers to be implemented without fundamentally changing the engine or macro interface.

## 2\. High-level architecture

text

```text
                    KDE Plasma / KWin
                           |
                    +--------------+
                    | KWin Plugin  |
                    |              |
                    | State export |
                    | Commands     |
                    +--------------+
                           |
                           |
                          IPC
                           |
                           |
+--------------------------v---------------------------+
|                       Engine                         |
|                                                      |
|  +----------------+       +-----------------------+  |
|  | Context Manager|<----->| IPC Client            |  |
|  +----------------+       +-----------------------+  |
|           |                                          |
|           v                                          |
|  +----------------+       +-----------------------+  |
|  | Macro Runtime  |<----->| Input Event Dispatcher|  |
|  +----------------+       +-----------------------+  |
|           |                          ^               |
|           v                          |               |
|  +----------------+       +-----------------------+  |
|  | Macro API      |       | Input Device Manager  |  |
|  +----------------+       +-----------------------+  |
|           |                          |               |
|           v                          |               |
|  +----------------+       +-----------------------+  |
|  | Virtual Output |       | Physical Input        |  |
|  | (uinput)       |       | (evdev)               |  |
|  +----------------+       +-----------------------+  |
|                                                      |
+------------------------------------------------------+
                           |
                           v
                    Linux Input System
```

## 3\. Components

### 3.1 Engine

The engine is the central component of SmartKeyBinds.

It owns the input devices, dispatches events, executes macros, manages context information, and produces virtual input events.

Responsibilities:

- Physical device discovery.
- Physical device lifecycle management.
- Exclusive device acquisition.
- Input event processing.
- Macro dispatch.
- Macro API implementation.
- Virtual device management.
- Context management.
- IPC communication.

The engine should not contain KWin-specific logic.

### 3.2 Input Device Manager

The Input Device Manager handles physical and virtual input devices.

#### Physical devices

Physical devices are discovered through Linux evdev.

The engine attempts to acquire exclusive access to each managed device.

On Linux, this can be implemented using `EVIOCGRAB`.

The purpose is to prevent other input handlers from receiving the original events while SmartKeyBinds is managing the device.

#### Virtual devices

The engine creates virtual devices through `uinput`.

All events that SmartKeyBinds intends to deliver to the rest of the system are emitted through these devices.

The virtual device should advertise the capabilities required to reproduce the managed physical device's supported events.

#### Device lifecycle

The device manager must account for:

- Devices appearing after engine startup.
- Devices being disconnected.
- Device reconnection.
- Failed exclusive grabs.
- Failed virtual device creation.
- Engine shutdown.
- Recovery after partial initialization.

A physical device must not be left permanently inaccessible if the engine fails.

### 3.3 Input Event Dispatcher

The dispatcher receives physical events and determines how they should be handled.

Its responsibilities include:

- Identifying the source device.
- Maintaining key state.
- Identifying relevant macro handlers.
- Providing the current context to macro execution.
- Executing the selected behavior.
- Forwarding events that are not overridden.

The dispatcher should preserve input event ordering and maintain consistent key press/release behavior.

### 3.4 Macro Runtime

Macros are implemented as native C++ functions.

They are compiled as part of the engine build and statically linked into the resulting executable.

There is no interpreted scripting environment.

The macro runtime is responsible for dispatching input events to the appropriate user-defined functions.

The runtime should provide a defined lifecycle for macro initialization, event handling, and shutdown.

### 3.5 Macro API

The Macro API hides Linux input implementation details from macro authors.

Potential API functionality:

- `press_key(key)`
- `release_key(key)`
- `tap_key(key)`
- `pass_through()`
- `consume_event()`
- `get_context()`
- `send_text(text)`
- `run_command(command)`

These names are illustrative and will be finalized separately.

The API must manage virtual input state correctly, particularly when multiple keys are pressed simultaneously.

### 3.6 Context Manager

The Context Manager stores the latest state received from external providers.

Macros can inspect this state to determine how an input event should behave.

The engine should not require macros to know which plugin supplied a particular property.

Instead, providers expose a common context interface.

For example:

cpp

```cpp
struct Context
{
    ActiveWindow active_window;
};
```

The actual context structure will depend on the properties exposed by the registered providers.

### 3.7 KWin Plugin

The KWin plugin is the first context provider.

It exposes KWin state and receives commands from the engine.

Initial functionality:

- Export active window information.
- Notify the engine when the active window changes.
- Receive commands from the engine.
- Execute supported KWin operations.
- Report command results where applicable.

The plugin should be responsible for translating between KWin internals and the shared IPC protocol.

The engine should only interact with the public protocol.

### 3.8 IPC

The IPC implementation is shared between the engine and plugins.

Current implementation:

text

```text
common/ipc/
├── protocol/
│   ├── CMakeLists.txt
│   ├── messages.hpp
│   ├── message_types.hpp
│   ├── protocol.cpp
│   └── protocol.hpp
├── client.cpp
├── server.cpp
└── README.md
```

Responsibilities:

- Message serialization.
- Message deserialization.
- Message type definitions.
- Connection management.
- State updates.
- Command requests.
- Command responses.
- Connection failure handling.

IPC should be independent of the specific provider implementation.

## 4\. Input processing model

The intended input flow is:

text

```text
Physical keyboard
       |
       v
Physical evdev device
       |
       v
Exclusive device grab
       |
       v
Input Event Dispatcher
       |
       v
Macro handler lookup
       |
       +-------------------+
       |                   |
       v                   v
  No override          Override
       |                   |
       v                   v
  Forward event        Execute macro
       |                   |
       +---------+---------+
                 |
                 v
          Virtual device
                 |
                 v
         Linux input system
                 |
                 v
             Applications
```

The key principle is that the original physical event is never independently delivered to applications when its device is successfully grabbed.

Instead, SmartKeyBinds decides which events to reproduce through virtual devices.

## 5\. Context provider architecture

KWin is the initial provider, but the architecture should not be permanently tied to it.

A provider exposes state and optionally supports commands.

Conceptually:

cpp

```cpp
class ContextProvider
{
public:
    virtual ~ContextProvider() = default;

    virtual void connect() = 0;
    virtual void disconnect() = 0;

    virtual ContextState get_state() const = 0;
};
```

This is an illustrative interface, not a finalized API.

Future providers could expose:

- Desktop environment state.
- Window manager state.
- Application-specific state.
- Other system-level context.

The engine should aggregate provider state into a unified context accessible to macros.

A provider must have a defined identity and ownership for its context properties to avoid collisions.

## 6\. Macro compilation and linking

Macros are native C++ code.

The intended build process is:

1. The user writes or modifies macro source files.
2. The build system compiles the macro sources.
3. The resulting object files are linked into the engine executable.
4. The engine starts with the compiled macro implementation.

There is no runtime interpretation or dynamic loading requirement.

This approach provides:

- Native C++ performance.
- Compile-time type checking.
- Access to project-provided APIs.
- Straightforward debugging with native development tools.

Changing macros requires rebuilding the engine and restarting it.

## 7\. Failure handling

Input interception introduces an important operational requirement: failure must not leave the user's keyboard unusable.

The engine should implement:

- Cleanup of grabbed physical devices.
- Cleanup of virtual devices.
- Recovery from disconnected devices.
- Handling of provider disconnections.
- Detection of IPC failures.
- Defined behavior when context becomes unavailable.
- Graceful shutdown on termination signals.

If a provider disconnects, the engine should continue operating with the remaining available context, with missing properties represented explicitly.

If the engine crashes, operating-system cleanup should release its device grabs and destroy its virtual devices.

The design should also include a recovery procedure for cases where cleanup does not happen normally.

## 8\. Security and permissions

Access to `/dev/input` and `/dev/uinput` requires appropriate permissions.

SmartKeyBinds should document the required permissions and avoid unnecessarily running the entire engine with elevated privileges.

Macro code is native C++ linked into the engine. It must therefore be treated as trusted code.

Macros can potentially access the same resources and capabilities as the engine process.

## 9\. Architectural principles

1. The engine owns input processing.
2. Plugins provide context and optional external operations.
3. Macros define behavior.
4. IPC separates the engine from external providers.
5. The macro API hides Linux input implementation details.
6. All delivered input is generated through virtual devices.
7. The architecture must support device lifecycle and failure recovery.
8. KWin is the first integration, not a permanent architectural dependency.
