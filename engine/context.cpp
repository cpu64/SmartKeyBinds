#include "./context.hpp"

#include "./fake_input.hpp"
#include "protocol/protocol.hpp"

#include <iostream>
#include <utility>
#include <vector>

Context::Context() : kwinBridge("/tmp/smartkeybinds.sock")
{
    fake_input_device = create_virtual_input_device();

    kwinBridge.Connect(
        [this](const SeqpacketClient::Packet &packet) {
            auto result = SKB::Deserialize(packet);

            if (!result)
            {
                std::cerr << "Invalid IPC packet\n";
                return;
            }

            std::visit(
                [this](const auto &message) {
                    using T = std::decay_t<decltype(message)>;

                    if constexpr (std::is_same_v<T, SKB::ActiveWindowChanged>)
                    {
                        {
                            std::lock_guard<std::mutex> lock(active_window_mutex);
                            active_window_name = message.name;
                        }

                        std::cout << "Active window changed: " << message.name << '\n';
                    }
                    else if constexpr (std::is_same_v<T, SKB::CursorPosition>)
                    {
                        CompleteCursorRequest(message);
                    }
                },
                *result);
        },
        [this]() { FailPendingRequests(); });
}

Context::~Context()
{
    FailPendingRequests();
    kwinBridge.Disconnect();
}

std::string Context::GetActiveWindowName() const
{
    std::lock_guard<std::mutex> lock(active_window_mutex);
    return active_window_name;
}

std::optional<SKB::CursorPosition> Context::GetMousePosition(std::chrono::milliseconds timeout)
{
    // Generate a unique request ID.
    std::uint64_t id = next_request_id.fetch_add(1, std::memory_order_relaxed);

    // Reserve zero as an invalid ID.
    if (id == 0)
        id = next_request_id.fetch_add(1, std::memory_order_relaxed);

    auto pending = std::make_shared<PendingRequest>();

    // Register the request BEFORE sending it.
    {
        std::lock_guard<std::mutex> lock(pending_requests_mutex);
        pending_requests.emplace(id, pending);
    }

    // Send the request.
    SKB::Message request = SKB::GetCursorPosition{.id = id};

    auto packet = SKB::Serialize(request);

    if (!kwinBridge.Send(packet))
    {
        std::lock_guard<std::mutex> lock(pending_requests_mutex);
        pending_requests.erase(id);

        return std::nullopt;
    }

    // Wait for the matching response.
    std::unique_lock<std::mutex> lock(pending->mutex);

    const bool received = pending->cv.wait_for(lock, timeout, [&pending]() { return pending->completed; });

    if (!received)
    {
        lock.unlock();

        // Remove timed-out request.
        std::lock_guard<std::mutex> mapLock(pending_requests_mutex);
        pending_requests.erase(id);

        return std::nullopt;
    }

    return pending->response;
}

void Context::CompleteCursorRequest(const SKB::CursorPosition &response)
{
    std::shared_ptr<PendingRequest> pending;

    {
        std::lock_guard<std::mutex> lock(pending_requests_mutex);

        auto it = pending_requests.find(response.id);

        if (it == pending_requests.end())
            return;

        pending = it->second;
        pending_requests.erase(it);
    }

    {
        std::lock_guard<std::mutex> lock(pending->mutex);

        pending->response = response;
        pending->completed = true;
    }

    pending->cv.notify_one();
}

void Context::FailPendingRequests()
{
    std::vector<std::shared_ptr<PendingRequest>> requests;

    {
        std::lock_guard<std::mutex> lock(pending_requests_mutex);

        for (auto &[id, pending] : pending_requests)
            requests.push_back(pending);

        pending_requests.clear();
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

// struct ContextData
// {
//     std::string active_window_name;
//     int CursorX = 0;
//     int CursorY = 0;
//
//     int virtual_input_device_;
//     std::atomic<bool> real_keyboard_state_[NUMBER_OF_KEYS];
//     std::atomic<bool> virtual_keyboard_state_[NUMBER_OF_KEYS];
//     // void (*macros_[NUMBER_OF_KEYS])( input_event); // array containing a macro function for every key (default
//     macro is to simulate normal behavior)
//     // std::atomic<int> unfinished_threadcount_;
// };
//
// class ContextView
// {
//   public:
//     const int &age() const
//     {
//         return data_.age;
//     }
//
//     const std::string &name() const
//     {
//         return data_.name;
//     }
//
//   protected:
//     explicit ContextView(ContextData &data) : data_(data)
//     {
//     }
//
//     ContextData &data_;
// };
//
// // User-facing interface
// class Context : public ContextView
// {
//   public:
//     using ContextView::ContextView;
//
//     // Users can change age
//     void set_age(int age)
//     {
//         data_.age = age;
//     }
//
//     // No set_name()!
// };
//
// // Engine-facing interface
// class EngineContext : public ContextView
// {
//   public:
//     using ContextView::ContextView;
//
//     // Engine can change both
//     void set_age(int age)
//     {
//         data_.age = age;
//     }
//
//     void set_name(std::string name)
//     {
//         data_.name = std::move(name);
//     }
//
//     // Create a restricted user view
//     Context as_user_context() const
//     {
//         return Context(data_);
//     }
// };
/*
#include "ipc/client.hpp"
#include "protocol/protocol.hpp"
#include <iostream>
#include <string>
#include <utility>

// Actual shared storage
struct ContextData
{
    SeqpacketClient kwinBridge;

    std::string active_window_name;
    int CursorX = 0;
    int CursorY = 0;

    int virtual_input_device_;
    std::atomic<bool> real_keyboard_state_[NUMBER_OF_KEYS];
    std::atomic<bool> virtual_keyboard_state_[NUMBER_OF_KEYS];
    void (*macros_[NUMBER_OF_KEYS])( input_event); // array containing a macro function for every key (default macro
    is to simulate normal behavior) std::atomic<int> unfinished_threadcount_;

    ContextData() : kwinBridge("/tmp/smartkeybinds.sock")
    {
        kwinBridge.Connect([&](const SeqpacketClient::Packet &packet) {
            auto result = SKB::Deserialize(packet);

            if (!result)
            {
                std::cerr << "Invalid IPC packet\n";
                return;
            }

            std::visit(
                [&](const auto &message) {
                    using T = std::decay_t<decltype(message)>;

                    if constexpr (std::is_same_v<T, SKB::ActiveWindowChanged>)
                    {
                        std::cout << "Active window changed: " << message.name << '\n';
                        active_window_name = message.name;
                    }
                    else if constexpr (std::is_same_v<T, SKB::CursorPosition>)
                    {
                        std::cout << "Cursor position: x=" << message.x << ", y=" << message.y << '\n';
                        CursorX = message.x;
                        CursorY = message.y;
                    }
                },
                *result);
        });
    }

    int age = 25;
    std::string name = "John";
};*/
//
// // Forward declaration
// class Context;
//
// // Shared read-only interface
// class ContextView
// {
//   public:
//     const int &age() const
//     {
//         return data_.age;
//     }
//
//     const std::string &name() const
//     {
//         return data_.name;
//     }
//
//   protected:
//     explicit ContextView(ContextData &data) : data_(data)
//     {
//     }
//
//     ContextData &data_;
// };
//
// // User-facing interface
// class Context : public ContextView
// {
//   public:
//     using ContextView::ContextView;
//
//     // Users can change age
//     void set_age(int age)
//     {
//         data_.age = age;
//     }
//
//     // No set_name()!
// };
//
// // Engine-facing interface
// class EngineContext : public ContextView
// {
//   public:
//     using ContextView::ContextView;
//
//     // Engine can change both
//     void set_age(int age)
//     {
//         data_.age = age;
//     }
//
//     void set_name(std::string name)
//     {
//         data_.name = std::move(name);
//     }
//
//     // Create a restricted user view
//     Context as_user_context() const
//     {
//         return Context(data_);
//     }
// };
//
// // Example user function
// void user_script(Context ctx)
// {
//     std::cout << "\n--- User script ---\n";
//
//     std::cout << "Name: " << ctx.name() << '\n';
//     std::cout << "Age: " << ctx.age() << '\n';
//
//     ctx.set_age(30);
//
//     // This would fail to compile:
//     // ctx.set_name("Jane");
//
//     std::cout << "Changed age to: " << ctx.age() << '\n';
// }
//
// // Example engine function
// void engine_function(EngineContext ctx)
// {
//     std::cout << "--- Engine ---\n";
//
//     ctx.set_name("Alice");
//     ctx.set_age(28);
//
//     std::cout << "Name: " << ctx.name() << '\n';
//     std::cout << "Age: " << ctx.age() << '\n';
//
//     // Pass restricted context to user code
//     user_script(ctx.as_user_context());
// }
//
// int main()
// {
//     // Create the actual data
//     ContextData data;
//
//     // Create engine access
//     EngineContext engineContext(data);
//
//     // Run engine
//     engine_function(engineContext);
//
//     std::cout << "\n--- Final state ---\n";
//     std::cout << "Name: " << data.name << '\n';
//     std::cout << "Age: " << data.age << '\n';
//
//     return 0;
// }
