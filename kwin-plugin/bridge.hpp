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
    void windowActivated(KWin::Window *window);

  private:
    void packetHandler(const SeqpacketServer::Packet &packet);
    void sendCursorPos(std::uint64_t id);
    logging::Logger m_logger;
    SeqpacketServer m_server;
};

} // namespace KWin
