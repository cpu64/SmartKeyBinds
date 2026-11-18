#pragma once

#include "logger/logger.hpp"

#include <atomic>
#include <cstddef>
#include <cstring>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

class SeqpacketServer
{
  public:
    using Packet = std::vector<std::byte>;
    using PacketHandler = std::function<void(const Packet &)>;

    static constexpr std::size_t MAX_QUEUED_PACKETS = 256;

    explicit SeqpacketServer(std::string_view socket_name, logging::Logger &logger, PacketHandler handler = {});

    ~SeqpacketServer();

    SeqpacketServer(const SeqpacketServer &) = delete;
    SeqpacketServer &operator=(const SeqpacketServer &) = delete;

    bool send(const void *data, std::size_t size);

    bool send(const Packet &packet)
    {
        return send(packet.data(), packet.size());
    }

    bool is_running() const
    {
        return m_running.load();
    }

    bool is_connected() const
    {
        return m_connected.load();
    }

  private:
    void wake();

    void drain_wake_fd();

    bool has_queued_packets();

    void set_connected(bool connected);

    void drop_connection(int &fd, std::string_view reason);

    void accept_pending_connections(int &client_fd);

    bool receive_packets(int fd);

    bool send_queued_packets(int fd);

    void io_thread();

    void cleanup_setup();

    std::string m_socketName;
    PacketHandler m_handler;

    int m_listenFd{-1};
    int m_wakeFd{-1};
    bool m_bound{false};

    std::atomic<bool> m_running{false};
    std::atomic<bool> m_connected{false};
    std::atomic<bool> m_stopping{false};

    std::mutex m_queueMutex;
    std::deque<Packet> m_queue;

    std::thread m_thread;

    logging::Logger &m_logger;
};
