#include "client.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

// Connect() may block while establishing the connection.
// Once connected, packet I/O runs asynchronously.
bool SeqpacketClient::Connect(PacketHandler handler, DisconnectHandler disconnectHandler)
{
    Disconnect();

    if (m_socketName.size() >= sizeof(sockaddr_un::sun_path))
    {
        m_logger.error("Socket path too long");
        return false;
    }

    m_handler = std::move(handler);
    m_disconnectHandler = std::move(disconnectHandler);
    m_stopping.store(false);

    m_socketFd = ::socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);

    if (m_socketFd < 0)
    {
        m_logger.Errno("socket() failed");
        return false;
    }

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;

    std::memcpy(addr.sun_path, m_socketName.data(), m_socketName.size());
    addr.sun_path[m_socketName.size()] = '\0';

    const socklen_t len = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + m_socketName.size() + 1);

    if (::connect(m_socketFd, reinterpret_cast<sockaddr *>(&addr), len) < 0)
    {
        m_logger.Errno("connect() failed");
        Cleanup();
        return false;
    }

    // Use nonblocking socket I/O in the I/O thread.
    const int flags = ::fcntl(m_socketFd, F_GETFL, 0);

    if (flags < 0 || ::fcntl(m_socketFd, F_SETFL, flags | O_NONBLOCK) < 0)
    {
        m_logger.Errno("fcntl(O_NONBLOCK) failed");
        Cleanup();
        return false;
    }

    m_wakeFd = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);

    if (m_wakeFd < 0)
    {
        m_logger.Errno("eventfd() failed");
        Cleanup();
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_connected.store(true);
    }

    try
    {
        m_thread = std::thread(&SeqpacketClient::IoThread, this);
    }
    catch (const std::exception &e)
    {
        m_logger.error(std::string("Failed to start I/O thread: ") + e.what());

        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            m_connected.store(false);
            m_queue.clear();
        }

        Cleanup();
        return false;
    }

    m_logger.info("Connected to server");
    return true;
}

// Safe to call concurrently from multiple application threads.
// Copies the packet and returns without waiting for socket I/O.
bool SeqpacketClient::Send(const void *data, std::size_t size)
{
    if (data == nullptr || size == 0)
        return false;

    Packet packet(size);
    std::memcpy(packet.data(), data, size);

    {
        std::lock_guard<std::mutex> lock(m_queueMutex);

        if (!m_connected.load() || m_stopping.load() || m_queue.size() >= MaxQueuedPackets)
        {
            return false;
        }

        m_queue.push_back(std::move(packet));
        Wake();
    }

    return true;
}

void SeqpacketClient::Disconnect()
{
    m_stopping.store(true);
    m_connected.store(false);

    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_queue.clear();
        Wake();
    }

    if (m_thread.joinable())
        m_thread.join();

    Cleanup();
    m_handler = {};
    m_disconnectHandler = {};
}

void SeqpacketClient::Wake()
{
    if (m_wakeFd < 0)
        return;

    const std::uint64_t value = 1;

    for (;;)
    {
        const ssize_t rc = ::write(m_wakeFd, &value, sizeof(value));

        if (rc == sizeof(value))
            return;

        if (rc < 0 && errno == EINTR)
            continue;

        if (rc < 0 && errno == EAGAIN)
            return;

        m_logger.Errno("eventfd write failed");
        return;
    }
}

void SeqpacketClient::DrainWakeFd()
{
    std::uint64_t value;

    for (;;)
    {
        const ssize_t rc = ::read(m_wakeFd, &value, sizeof(value));

        if (rc == sizeof(value))
            continue;

        if (rc < 0 && errno == EINTR)
            continue;

        break;
    }
}

bool SeqpacketClient::HasQueuedPackets()
{
    std::lock_guard<std::mutex> lock(m_queueMutex);
    return !m_queue.empty();
}

bool SeqpacketClient::ReceivePackets()
{
    Packet packet(65536);

    for (;;)
    {
        const ssize_t bytes = ::recv(m_socketFd, packet.data(), packet.size(), MSG_DONTWAIT);

        if (bytes < 0)
        {
            if (errno == EINTR)
                continue;

            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return true;

            m_logger.Errno("recv() failed");
            return false;
        }

        if (bytes == 0)
        {
            m_logger.info("Server disconnected");
            return false;
        }

        packet.resize(static_cast<std::size_t>(bytes));

        if (m_handler)
        {
            try
            {
                m_handler(packet);
            }
            catch (const std::exception &e)
            {
                m_logger.error(std::string("Packet handler exception: ") + e.what());
            }
            catch (...)
            {
                m_logger.error("Unknown packet handler exception");
            }
        }

        packet.resize(65536);
    }
}

bool SeqpacketClient::SendQueuedPackets()
{
    std::lock_guard<std::mutex> lock(m_queueMutex);

    while (!m_queue.empty())
    {
        const Packet &packet = m_queue.front();

        const ssize_t bytes = ::send(m_socketFd, packet.data(), packet.size(), MSG_DONTWAIT | MSG_NOSIGNAL);

        if (bytes < 0)
        {
            if (errno == EINTR)
                continue;

            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return true;

            m_logger.Errno("send() failed");
            return false;
        }

        if (static_cast<std::size_t>(bytes) != packet.size())
        {
            m_logger.error("Unexpected partial SOCK_SEQPACKET send");
            return false;
        }

        m_queue.pop_front();
    }

    return true;
}

void SeqpacketClient::IoThread()
{
    while (!m_stopping.load())
    {
        pollfd fds[2]{};

        fds[0].fd = m_socketFd;
        fds[0].events = POLLIN;

        if (HasQueuedPackets())
            fds[0].events |= POLLOUT;

        fds[1].fd = m_wakeFd;
        fds[1].events = POLLIN;

        const int rc = ::poll(fds, 2, -1);

        if (rc < 0)
        {
            if (errno == EINTR)
                continue;

            m_logger.Errno("poll() failed");
            break;
        }

        if (fds[1].revents & POLLIN)
            DrainWakeFd();

        if (m_stopping.load())
            break;

        if (fds[0].revents & POLLIN)
        {
            if (!ReceivePackets())
                break;
        }

        if (fds[0].revents & POLLOUT)
        {
            if (!SendQueuedPackets())
                break;
        }

        if (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL))
        {
            m_logger.warning("Server connection lost");
            break;
        }
    }

    {
        std::lock_guard<std::mutex> lock(m_queueMutex);

        m_connected.store(false);
        m_stopping.store(true);
        m_queue.clear();
    }

    if (m_disconnectHandler)
    {
        try
        {
            m_disconnectHandler();
        }
        catch (...)
        {
            m_logger.error("Disconnect handler exception");
        }
    }
}

void SeqpacketClient::Cleanup()
{
    if (m_socketFd >= 0)
    {
        ::close(m_socketFd);
        m_socketFd = -1;
    }

    if (m_wakeFd >= 0)
    {
        ::close(m_wakeFd);
        m_wakeFd = -1;
    }
}
