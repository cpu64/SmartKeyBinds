#pragma once

#include "macro_registry.hpp"

#include "./overlay/OverlayManager.hpp"
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
    std::string GetActiveWindowName() const;

    std::optional<SKB::CursorPosition> GetMousePosition(std::chrono::milliseconds timeout = std::chrono::seconds(2));

    SeqpacketClient kwinBridge;

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
        std::optional<SKB::CursorPosition> response;
    };

    std::atomic<std::uint64_t> next_request_id{1};

    std::mutex pending_requests_mutex;

    std::unordered_map<std::uint64_t, std::shared_ptr<PendingRequest>> pending_requests;

    void CompleteCursorRequest(const SKB::CursorPosition &response);
    void FailPendingRequests();
};
