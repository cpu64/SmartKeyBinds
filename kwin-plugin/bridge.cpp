#include "bridge.hpp"

#include "logger/sinks.hpp"
#include "protocol/messages.hpp"
#include "protocol/protocol.hpp"

#include <cstdint>
#include <kwin/window.h>
#include <kwin/workspace.h>

namespace KWin
{

static constexpr auto SocketName = "/tmp/smartkeybinds.sock";

std::vector<std::unique_ptr<logging::LogSink>> CreateSinks()
{
    std::vector<std::unique_ptr<logging::LogSink>> sinks;

    sinks.emplace_back(std::make_unique<logging::FileSink>("/tmp/smartkeybinds-kwin-plugin.log"));
    return sinks;
}

void Bridge::packetHandler(const SeqpacketServer::Packet &packet)
{
    auto result = SKB::Deserialize(packet);

    if (!result)
    {
        m_logger.error("Invalid IPC packet");
        return;
    }

    std::visit(
        [this](const auto &message) {
            using T = std::decay_t<decltype(message)>;

            if constexpr (std::is_same_v<T, SKB::GetCursorPosition>)
            {
                sendCursorPos(message.id);
            }
        },
        *result);
}

Bridge::Bridge()
    : m_logger(CreateSinks()), m_server(SocketName, m_logger, [this](const SeqpacketServer::Packet &packet) {
          if (!QMetaObject::invokeMethod(this, [this, packet] { packetHandler(packet); }, Qt::QueuedConnection))
          {
              m_logger.warning("Failed to schedule packet handler");
          }
      })
{
    m_logger.info("Loaded SmartKeyBinds KWin bridge");

    Workspace *workspace = Workspace::self();

    connect(workspace, &Workspace::windowActivated, this, &Bridge::windowActivated);
}

Bridge::~Bridge()
{
    // m_logger.shutdown(); //bad the m_server hasnt stopped yet
}

void Bridge::windowActivated(KWin::Window *window)
{
    if (!window)
        return;

    const std::string name = window->resourceName().toStdString();

    m_logger.info("Active window: " + name);

    SKB::ActiveWindowChanged message{.name = name};

    auto packet = SKB::Serialize(message);

    if (!m_server.Send(packet))
    {
        m_logger.warning("Failed to send active window change");
    }
}

void Bridge::sendCursorPos(std::uint64_t id)
{
    QPoint pos = Cursors::self()->mouse()->pos().toPoint();

    SKB::CursorPosition message{.id = id, .x = pos.x(), .y = pos.y()};

    auto packet = SKB::Serialize(message);

    if (!m_server.Send(packet))
    {
        m_logger.warning("Failed to send cursor position");
    }
}

} // namespace KWin
