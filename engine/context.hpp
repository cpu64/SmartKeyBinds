#pragma once

#include "./OverlayManager.hpp"
#include "./macro_registry.hpp"

#include "ipc/client.hpp"
#include "protocol/messages.hpp"

struct Context
{
    std::atomic<bool> real_keyboard_state[NUMBER_OF_KEYS] = {0};
    std::atomic<bool> virtual_keyboard_state[NUMBER_OF_KEYS] = {0};

    int fake_input_device;
    overlay::OverlayManager overlay_manager;

    mutable std::mutex active_window_mutex;
    std::string active_window_name;
    std::string get_active_window_name() const;

    std::optional<kwin_messages::CursorPosition> get_mouse_position(
        std::chrono::milliseconds timeout = std::chrono::seconds(2));

    SeqpacketClient kwin_bridge;

    Context();
    ~Context();
    Context(const Context &) = delete;
    Context &operator=(const Context &) = delete;

  private:
    struct PendingRequest
    {
        std::mutex mutex;
        std::condition_variable cv;

        bool completed = false;
        std::optional<kwin_messages::CursorPosition> response;
    };

    std::atomic<std::uint64_t> m_next_request_id{1};

    std::mutex m_pending_requests_mutex;

    std::unordered_map<std::uint64_t, std::shared_ptr<PendingRequest>> m_pending_requests;

    void complete_cursor_request(const kwin_messages::CursorPosition &response);
    void fail_pending_requests();
};
