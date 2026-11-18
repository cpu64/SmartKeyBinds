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

    static constexpr std::size_t MaxQueuedPackets = 256;

    explicit SeqpacketServer(std::string_view socketName, logging::Logger &logger, PacketHandler handler = {});

    ~SeqpacketServer();

    SeqpacketServer(const SeqpacketServer &) = delete;
    SeqpacketServer &operator=(const SeqpacketServer &) = delete;

    bool Send(const void *data, std::size_t size);

    bool Send(const Packet &packet)
    {
        return Send(packet.data(), packet.size());
    }

    bool IsRunning() const
    {
        return m_running.load();
    }

    bool IsConnected() const
    {
        return m_connected.load();
    }

  private:
    void Wake();

    void DrainWakeFd();

    bool HasQueuedPackets();

    void SetConnected(bool connected);

    void DropConnection(int &fd, std::string_view reason);

    void AcceptPendingConnections(int &clientFd);

    bool ReceivePackets(int fd);

    bool SendQueuedPackets(int fd);

    void IoThread();

    void CleanupSetup();

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
