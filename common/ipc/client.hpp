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

    static constexpr std::size_t MaxQueuedPackets = 256;

    explicit SeqpacketClient(std::string_view socketName) : m_socketName(socketName)
    {
        auto sink = std::make_unique<logging::ConsoleSink>();
        m_logger.addSink(std::move(sink));
    }

    ~SeqpacketClient()
    {
        Disconnect();
        m_logger.shutdown();
    }

    SeqpacketClient(const SeqpacketClient &) = delete;
    SeqpacketClient &operator=(const SeqpacketClient &) = delete;

    bool Connect(PacketHandler handler, DisconnectHandler disconnectHandler = {});

    bool Send(const void *data, std::size_t size);

    bool Send(const Packet &packet)
    {
        return Send(packet.data(), packet.size());
    }

    void Disconnect();

    bool IsConnected() const
    {
        return m_connected.load();
    }

  private:
    void Wake();
    void DrainWakeFd();
    bool HasQueuedPackets();
    bool ReceivePackets();
    bool SendQueuedPackets();
    void IoThread();
    void Cleanup();

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
