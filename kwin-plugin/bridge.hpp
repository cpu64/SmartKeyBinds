#pragma once

#include "ipc/server.hpp"
#include "logger/logger.hpp"

#include <cstdint>
#include <kwin/plugin.h>

namespace KWin
{

class Window;

class Bridge : public Plugin
{
    Q_OBJECT

  public:
    Bridge();
    ~Bridge();

  private Q_SLOTS:
    void window_activated(KWin::Window *window);

  private:
    void packet_handler(const SeqpacketServer::Packet &packet);
    void send_cursor_pos(std::uint64_t id);
    logging::Logger m_logger;
    SeqpacketServer m_server;
};

} // namespace KWin
