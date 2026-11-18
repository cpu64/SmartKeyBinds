#include "./server.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

SeqpacketServer::SeqpacketServer(std::string_view socketName, logging::Logger &logger, PacketHandler handler)
    : m_socketName(socketName), m_logger(logger), m_handler(std::move(handler))
{
    if (m_socketName.empty() || m_socketName.size() >= sizeof(sockaddr_un::sun_path))
    {
        m_logger.error("Invalid socket path");
        return;
    }

    // Remove a stale socket entry.
    if (::unlink(m_socketName.c_str()) < 0 && errno != ENOENT)
    {
        m_logger.Errno("unlink() failed");
        return;
    }

    m_listenFd = ::socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);

    if (m_listenFd < 0)
    {
        m_logger.Errno("socket() failed");
        return;
    }

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::memcpy(addr.sun_path, m_socketName.data(), m_socketName.size());
    addr.sun_path[m_socketName.size()] = '\0';

    const socklen_t len = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + m_socketName.size() + 1);

    if (::bind(m_listenFd, reinterpret_cast<sockaddr *>(&addr), len) < 0)
    {
        m_logger.Errno("bind() failed");
        CleanupSetup();
        return;
    }

    m_bound = true;

    if (::chmod(m_socketName.c_str(), 0600) < 0)
    {
        m_logger.Errno("chmod() failed");
        CleanupSetup();
        return;
    }

    if (::listen(m_listenFd, 4) < 0)
    {
        m_logger.Errno("listen() failed");
        CleanupSetup();
        return;
    }

    m_wakeFd = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);

    if (m_wakeFd < 0)
    {
        m_logger.Errno("eventfd() failed");
        CleanupSetup();
        return;
    }

    try
    {
        m_running.store(true);
        m_thread = std::thread(&SeqpacketServer::IoThread, this);
    }
    catch (const std::exception &e)
    {
        m_logger.error(std::string("Failed to start I/O thread: ") + e.what());
        CleanupSetup();
    }
}

SeqpacketServer::~SeqpacketServer()
{
    m_stopping.store(true);
    Wake();

    if (m_thread.joinable())
        m_thread.join();

    if (m_wakeFd >= 0)
        ::close(m_wakeFd);

    if (m_listenFd >= 0)
        ::close(m_listenFd);

    if (m_bound)
        ::unlink(m_socketName.c_str());
}

// Nonblocking with respect to socket I/O.
// Copies the packet before returning.
// Returns false if disconnected, invalid, or queue is full.
bool SeqpacketServer::Send(const void *data, std::size_t size)
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

void SeqpacketServer::Wake()
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

void SeqpacketServer::DrainWakeFd()
{
    std::uint64_t value;
    while (::read(m_wakeFd, &value, sizeof(value)) > 0)
    {
    }
}

bool SeqpacketServer::HasQueuedPackets()
{
    std::lock_guard<std::mutex> lock(m_queueMutex);
    return !m_queue.empty();
}

void SeqpacketServer::SetConnected(bool connected)
{
    std::lock_guard<std::mutex> lock(m_queueMutex);

    m_connected.store(connected);

    if (!connected)
        m_queue.clear();
}

void SeqpacketServer::DropConnection(int &fd, std::string_view reason)
{
    if (fd >= 0)
    {
        ::close(fd);
        fd = -1;
    }

    SetConnected(false);
    m_logger.warning(std::string(reason));
}

void SeqpacketServer::AcceptPendingConnections(int &clientFd)
{
    for (;;)
    {
        const int fd = ::accept4(m_listenFd, nullptr, nullptr, SOCK_CLOEXEC | SOCK_NONBLOCK);

        if (fd < 0)
        {
            if (errno == EINTR)
                continue;

            if (errno != EAGAIN && errno != EWOULDBLOCK)
                m_logger.Errno("accept4() failed");

            return;
        }

        // Reject immediately if a client is already connected.
        if (clientFd >= 0)
        {
            m_logger.warning("Rejecting client: another client is connected");
            ::close(fd);
            continue;
        }

        ucred cred{};
        socklen_t credLen = sizeof(cred);

        if (::getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cred, &credLen) < 0)
        {
            m_logger.Errno("SO_PEERCRED failed");
            ::close(fd);
            continue;
        }

        if (cred.uid != 0)
        {
            m_logger.warning("Rejecting non-root client");
            ::close(fd);
            continue;
        }

        clientFd = fd;
        SetConnected(true);
        m_logger.info("Root client connected");
    }
}

bool SeqpacketServer::ReceivePackets(int fd)
{
    Packet packet(65536);

    for (;;)
    {
        const ssize_t bytes = ::recv(fd, packet.data(), packet.size(), MSG_DONTWAIT);

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
            m_logger.info("Client disconnected");
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

bool SeqpacketServer::SendQueuedPackets(int fd)
{
    std::lock_guard<std::mutex> lock(m_queueMutex);

    while (!m_queue.empty())
    {
        const Packet &packet = m_queue.front();

        const ssize_t bytes = ::send(fd, packet.data(), packet.size(), MSG_DONTWAIT | MSG_NOSIGNAL);

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

void SeqpacketServer::IoThread()
{
    int clientFd = -1;

    while (!m_stopping.load())
    {
        pollfd fds[3]{};

        fds[0].fd = m_listenFd;
        fds[0].events = POLLIN;

        fds[1].fd = m_wakeFd;
        fds[1].events = POLLIN;

        nfds_t count = 2;

        if (clientFd >= 0)
        {
            fds[2].fd = clientFd;
            fds[2].events = POLLIN;

            if (HasQueuedPackets())
                fds[2].events |= POLLOUT;

            count = 3;
        }

        const int rc = ::poll(fds, count, -1);

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

        // Process the existing connection before accepting new clients.
        if (clientFd >= 0)
        {
            const short events = fds[2].revents;

            if (events & POLLIN)
            {
                if (!ReceivePackets(clientFd))
                {
                    DropConnection(clientFd, "Client connection closed");
                    continue;
                }
            }

            if (events & POLLOUT)
            {
                if (!SendQueuedPackets(clientFd))
                {
                    DropConnection(clientFd, "Client send failed");
                    continue;
                }
            }

            if (events & (POLLERR | POLLHUP | POLLNVAL))
            {
                DropConnection(clientFd, "Client connection lost");
                continue;
            }
        }

        if (fds[0].revents & POLLIN)
            AcceptPendingConnections(clientFd);

        if (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL))
        {
            m_logger.error("Listening socket failed");
            break;
        }
    }

    if (clientFd >= 0)
        ::close(clientFd);

    SetConnected(false);
    m_running.store(false);
}

void SeqpacketServer::CleanupSetup()
{
    if (m_wakeFd >= 0)
    {
        ::close(m_wakeFd);
        m_wakeFd = -1;
    }

    if (m_listenFd >= 0)
    {
        ::close(m_listenFd);
        m_listenFd = -1;
    }

    if (m_bound)
    {
        ::unlink(m_socketName.c_str());
        m_bound = false;
    }
}
