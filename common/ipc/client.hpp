#pragma once

#include "logger/logger.hpp"
#include "logger/sinks.hpp"

#include <atomic>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

class SeqpacketClient
{
  public:
    using Packet = std::vector<std::byte>;
    using PacketHandler = std::function<void(const Packet &)>;
    using DisconnectHandler = std::function<void()>;

    static constexpr std::size_t MAX_QUEUED_PACKETS = 256;

    explicit SeqpacketClient(std::string_view socket_name) : m_socketName(socket_name)
    {
        auto sink = std::make_unique<logging::ConsoleSink>();
        m_logger.add_sink(std::move(sink));
    }

    ~SeqpacketClient()
    {
        disconnect();
        m_logger.shutdown();
    }

    SeqpacketClient(const SeqpacketClient &) = delete;
    SeqpacketClient &operator=(const SeqpacketClient &) = delete;

    bool connect(PacketHandler handler, DisconnectHandler disconnect_handler = {});

    bool send(const void *data, std::size_t size);

    bool send(const Packet &packet)
    {
        return send(packet.data(), packet.size());
    }

    void disconnect();

    bool is_connected() const
    {
        return m_connected.load();
    }

  private:
    void wake();
    void drain_wake_fd();
    bool has_queued_packets();
    bool receive_packets();
    bool send_queued_packets();
    void io_thread();
    void cleanup();

    std::string m_socketName;
    PacketHandler m_handler;
    DisconnectHandler m_disconnectHandler;

    int m_socketFd{-1};
    int m_wakeFd{-1};

    std::atomic<bool> m_connected{false};
    std::atomic<bool> m_stopping{false};

    std::mutex m_queueMutex;
    std::deque<Packet> m_queue;

    std::thread m_thread;

    logging::Logger m_logger;
};
