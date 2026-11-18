#include "bridge.hpp"

#include "logger/sinks.hpp"
#include "protocol/messages.hpp"
#include "protocol/protocol.hpp"

#include <cstdint>
#include <kwin/window.h>
#include <kwin/workspace.h>

namespace KWin
{

static constexpr auto SOCKET_NAME = "/tmp/smartkeybinds.sock";

std::vector<std::unique_ptr<logging::LogSink>> create_sinks()
{
    std::vector<std::unique_ptr<logging::LogSink>> sinks;

    sinks.emplace_back(std::make_unique<logging::FileSink>("/tmp/smartkeybinds-kwin-plugin.log"));
    return sinks;
}

void Bridge::packet_handler(const SeqpacketServer::Packet &packet)
{
    auto result = kwin_messages::deserialize(packet);

    if (!result)
    {
        m_logger.error("Invalid IPC packet");
        return;
    }

    std::visit(
        [this](const auto &message) {
            using T = std::decay_t<decltype(message)>;

            if constexpr (std::is_same_v<T, kwin_messages::GetCursorPosition>)
            {
                send_cursor_pos(message.id);
            }
        },
        *result);
}

Bridge::Bridge()
    : m_logger(create_sinks()), m_server(SOCKET_NAME, m_logger, [this](const SeqpacketServer::Packet &packet) {
          if (!QMetaObject::invokeMethod(this, [this, packet] { packet_handler(packet); }, Qt::QueuedConnection))
          {
              m_logger.warning("Failed to schedule packet handler");
          }
      })
{
    m_logger.info("Loaded SmartKeyBinds KWin bridge");

    Workspace *workspace = Workspace::self();

    connect(workspace, &Workspace::windowActivated, this, &Bridge::window_activated);
}

Bridge::~Bridge()
{
    // m_logger.shutdown(); //bad the m_server hasnt stopped yet
}

void Bridge::window_activated(KWin::Window *window)
{
    if (!window)
        return;

    const std::string Name = window->resourceName().toStdString();

    m_logger.info("Active window: " + Name);

    kwin_messages::ActiveWindowChanged message{.name = Name};

    auto packet = kwin_messages::serialize(message);

    if (!m_server.send(packet))
    {
        m_logger.warning("Failed to send active window change");
    }
}

void Bridge::send_cursor_pos(std::uint64_t id)
{
    QPoint pos = Cursors::self()->mouse()->pos().toPoint();

    kwin_messages::CursorPosition message{.id = id, .x = pos.x(), .y = pos.y()};

    auto packet = kwin_messages::serialize(message);

    if (!m_server.send(packet))
    {
        m_logger.warning("Failed to send cursor position");
    }
}

} // namespace KWin
