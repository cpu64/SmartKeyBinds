#include "./context.hpp"

#include "./fake_input.hpp"

#include "protocol/protocol.hpp"

#include <iostream>
#include <vector>

Context::Context() : kwin_bridge("/tmp/smartkeybinds.sock")
{
    fake_input_device = create_virtual_input_device();

    kwin_bridge.connect(
        [this](const SeqpacketClient::Packet &packet) {
            auto result = kwin_messages::deserialize(packet);

            if (!result)
            {
                std::cerr << "Invalid IPC packet\n";
                return;
            }

            std::visit(
                [this](const auto &message) {
                    using T = std::decay_t<decltype(message)>;

                    if constexpr (std::is_same_v<T, kwin_messages::ActiveWindowChanged>)
                    {
                        {
                            std::lock_guard<std::mutex> lock(active_window_mutex);
                            active_window_name = message.name;
                        }

                        std::cout << "Active window changed: " << message.name << '\n';
                    }
                    else if constexpr (std::is_same_v<T, kwin_messages::CursorPosition>)
                    {
                        complete_cursor_request(message);
                    }
                },
                *result);
        },
        [this]() { fail_pending_requests(); });
}

Context::~Context()
{
    fail_pending_requests();
    kwin_bridge.disconnect();
}

std::string Context::get_active_window_name() const
{
    std::lock_guard<std::mutex> lock(active_window_mutex);
    return active_window_name;
}

std::optional<kwin_messages::CursorPosition> Context::get_mouse_position(std::chrono::milliseconds timeout)
{
    // Generate a unique request ID.
    std::uint64_t id = m_next_request_id.fetch_add(1, std::memory_order_relaxed);

    // Reserve zero as an invalid ID.
    if (id == 0)
        id = m_next_request_id.fetch_add(1, std::memory_order_relaxed);

    auto pending = std::make_shared<PendingRequest>();

    // Register the request BEFORE sending it.
    {
        std::lock_guard<std::mutex> lock(m_pending_requests_mutex);
        m_pending_requests.emplace(id, pending);
    }

    // Send the request.
    kwin_messages::Message request = kwin_messages::GetCursorPosition{.id = id};

    auto packet = kwin_messages::serialize(request);

    if (!kwin_bridge.send(packet))
    {
        std::lock_guard<std::mutex> lock(m_pending_requests_mutex);
        m_pending_requests.erase(id);

        return std::nullopt;
    }

    // Wait for the matching response.
    std::unique_lock<std::mutex> lock(pending->mutex);

    const bool Received = pending->cv.wait_for(lock, timeout, [&pending]() { return pending->completed; });

    if (!Received)
    {
        lock.unlock();

        // Remove timed-out request.
        std::lock_guard<std::mutex> map_lock(m_pending_requests_mutex);
        m_pending_requests.erase(id);

        return std::nullopt;
    }

    return pending->response;
}

void Context::complete_cursor_request(const kwin_messages::CursorPosition &response)
{
    std::shared_ptr<PendingRequest> pending;

    {
        std::lock_guard<std::mutex> lock(m_pending_requests_mutex);

        auto it = m_pending_requests.find(response.id);

        if (it == m_pending_requests.end())
            return;

        pending = it->second;
        m_pending_requests.erase(it);
    }

    {
        std::lock_guard<std::mutex> lock(pending->mutex);

        pending->response = response;
        pending->completed = true;
    }

    pending->cv.notify_one();
}

void Context::fail_pending_requests()
{
    std::vector<std::shared_ptr<PendingRequest>> requests;

    {
        std::lock_guard<std::mutex> lock(m_pending_requests_mutex);

        for (auto &[Id, pending] : m_pending_requests)
            requests.push_back(pending);

        m_pending_requests.clear();
    }

    for (auto &pending : requests)
    {
        {
            std::lock_guard<std::mutex> lock(pending->mutex);

            pending->completed = true;
            pending->response = std::nullopt;
        }

        pending->cv.notify_one();
    }
}
