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

SeqpacketServer::SeqpacketServer(std::string_view socket_name, logging::Logger &logger, PacketHandler handler)
    : m_socketName(socket_name), m_logger(logger), m_handler(std::move(handler))
{
    if (m_socketName.empty() || m_socketName.size() >= sizeof(sockaddr_un::sun_path))
    {
        m_logger.error("Invalid socket path");
        return;
    }

    // Remove a stale socket entry.
    if (::unlink(m_socketName.c_str()) < 0 && errno != ENOENT)
    {
        m_logger.error_errno("unlink() failed");
        return;
    }

    m_listenFd = ::socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);

    if (m_listenFd < 0)
    {
        m_logger.error_errno("socket() failed");
        return;
    }

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::memcpy(addr.sun_path, m_socketName.data(), m_socketName.size());
    addr.sun_path[m_socketName.size()] = '\0';

    const socklen_t Len = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + m_socketName.size() + 1);

    if (::bind(m_listenFd, reinterpret_cast<sockaddr *>(&addr), Len) < 0)
    {
        m_logger.error_errno("bind() failed");
        cleanup_setup();
        return;
    }

    m_bound = true;

    if (::chmod(m_socketName.c_str(), 0600) < 0)
    {
        m_logger.error_errno("chmod() failed");
        cleanup_setup();
        return;
    }

    if (::listen(m_listenFd, 4) < 0)
    {
        m_logger.error_errno("listen() failed");
        cleanup_setup();
        return;
    }

    m_wakeFd = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);

    if (m_wakeFd < 0)
    {
        m_logger.error_errno("eventfd() failed");
        cleanup_setup();
        return;
    }

    try
    {
        m_running.store(true);
        m_thread = std::thread(&SeqpacketServer::io_thread, this);
    }
    catch (const std::exception &e)
    {
        m_logger.error(std::string("Failed to start I/O thread: ") + e.what());
        cleanup_setup();
    }
}

SeqpacketServer::~SeqpacketServer()
{
    m_stopping.store(true);
    wake();

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
bool SeqpacketServer::send(const void *data, std::size_t size)
{
    if (data == nullptr || size == 0)
        return false;

    Packet packet(size);
    std::memcpy(packet.data(), data, size);

    {
        std::lock_guard<std::mutex> lock(m_queueMutex);

        if (!m_connected.load() || m_stopping.load() || m_queue.size() >= MAX_QUEUED_PACKETS)
        {
            return false;
        }

        m_queue.push_back(std::move(packet));
        wake();
    }

    return true;
}

void SeqpacketServer::wake()
{
    if (m_wakeFd < 0)
        return;

    const std::uint64_t Value = 1;

    for (;;)
    {
        const ssize_t Rc = ::write(m_wakeFd, &Value, sizeof(Value));

        if (Rc == sizeof(Value))
            return;

        if (Rc < 0 && errno == EINTR)
            continue;

        if (Rc < 0 && errno == EAGAIN)
            return;

        m_logger.error_errno("eventfd write failed");
        return;
    }
}

void SeqpacketServer::drain_wake_fd()
{
    std::uint64_t value;
    while (::read(m_wakeFd, &value, sizeof(value)) > 0)
    {
    }
}

bool SeqpacketServer::has_queued_packets()
{
    std::lock_guard<std::mutex> lock(m_queueMutex);
    return !m_queue.empty();
}

void SeqpacketServer::set_connected(bool connected)
{
    std::lock_guard<std::mutex> lock(m_queueMutex);

    m_connected.store(connected);

    if (!connected)
        m_queue.clear();
}

void SeqpacketServer::drop_connection(int &fd, std::string_view reason)
{
    if (fd >= 0)
    {
        ::close(fd);
        fd = -1;
    }

    set_connected(false);
    m_logger.warning(std::string(reason));
}

void SeqpacketServer::accept_pending_connections(int &client_fd)
{
    for (;;)
    {
        const int Fd = ::accept4(m_listenFd, nullptr, nullptr, SOCK_CLOEXEC | SOCK_NONBLOCK);

        if (Fd < 0)
        {
            if (errno == EINTR)
                continue;

            if (errno != EAGAIN && errno != EWOULDBLOCK)
                m_logger.error_errno("accept4() failed");

            return;
        }

        // Reject immediately if a client is already connected.
        if (client_fd >= 0)
        {
            m_logger.warning("Rejecting client: another client is connected");
            ::close(Fd);
            continue;
        }

        ucred cred{};
        socklen_t cred_len = sizeof(cred);

        if (::getsockopt(Fd, SOL_SOCKET, SO_PEERCRED, &cred, &cred_len) < 0)
        {
            m_logger.error_errno("SO_PEERCRED failed");
            ::close(Fd);
            continue;
        }

        if (cred.uid != 0)
        {
            m_logger.warning("Rejecting non-root client");
            ::close(Fd);
            continue;
        }

        client_fd = Fd;
        set_connected(true);
        m_logger.info("Root client connected");
    }
}

bool SeqpacketServer::receive_packets(int fd)
{
    Packet packet(65536);

    for (;;)
    {
        const ssize_t Bytes = ::recv(fd, packet.data(), packet.size(), MSG_DONTWAIT);

        if (Bytes < 0)
        {
            if (errno == EINTR)
                continue;

            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return true;

            m_logger.error_errno("recv() failed");
            return false;
        }

        if (Bytes == 0)
        {
            m_logger.info("Client disconnected");
            return false;
        }

        packet.resize(static_cast<std::size_t>(Bytes));

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

bool SeqpacketServer::send_queued_packets(int fd)
{
    std::lock_guard<std::mutex> lock(m_queueMutex);

    while (!m_queue.empty())
    {
        const Packet &packet = m_queue.front();

        const ssize_t Bytes = ::send(fd, packet.data(), packet.size(), MSG_DONTWAIT | MSG_NOSIGNAL);

        if (Bytes < 0)
        {
            if (errno == EINTR)
                continue;

            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return true;

            m_logger.error_errno("send() failed");
            return false;
        }

        if (static_cast<std::size_t>(Bytes) != packet.size())
        {
            m_logger.error("Unexpected partial SOCK_SEQPACKET send");
            return false;
        }

        m_queue.pop_front();
    }

    return true;
}

void SeqpacketServer::io_thread()
{
    int client_fd = -1;

    while (!m_stopping.load())
    {
        pollfd fds[3]{};

        fds[0].fd = m_listenFd;
        fds[0].events = POLLIN;

        fds[1].fd = m_wakeFd;
        fds[1].events = POLLIN;

        nfds_t count = 2;

        if (client_fd >= 0)
        {
            fds[2].fd = client_fd;
            fds[2].events = POLLIN;

            if (has_queued_packets())
                fds[2].events |= POLLOUT;

            count = 3;
        }

        const int Rc = ::poll(fds, count, -1);

        if (Rc < 0)
        {
            if (errno == EINTR)
                continue;

            m_logger.error_errno("poll() failed");
            break;
        }

        if (fds[1].revents & POLLIN)
            drain_wake_fd();

        if (m_stopping.load())
            break;

        // Process the existing connection before accepting new clients.
        if (client_fd >= 0)
        {
            const short Events = fds[2].revents;

            if (Events & POLLIN)
            {
                if (!receive_packets(client_fd))
                {
                    drop_connection(client_fd, "Client connection closed");
                    continue;
                }
            }

            if (Events & POLLOUT)
            {
                if (!send_queued_packets(client_fd))
                {
                    drop_connection(client_fd, "Client send failed");
                    continue;
                }
            }

            if (Events & (POLLERR | POLLHUP | POLLNVAL))
            {
                drop_connection(client_fd, "Client connection lost");
                continue;
            }
        }

        if (fds[0].revents & POLLIN)
            accept_pending_connections(client_fd);

        if (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL))
        {
            m_logger.error("Listening socket failed");
            break;
        }
    }

    if (client_fd >= 0)
        ::close(client_fd);

    set_connected(false);
    m_running.store(false);
}

void SeqpacketServer::cleanup_setup()
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
