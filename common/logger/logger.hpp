#pragma once

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <source_location>
#include <string>
#include <thread>
#include <vector>

namespace logging
{
enum class LogLevel
{
    Info,
    Warning,
    Error
};

struct LogMessage
{
    LogLevel level;
    std::string message;
    std::chrono::system_clock::time_point timestamp;
    std::thread::id thread_id;

    std::source_location location;
};

class LogSink
{
  public:
    virtual ~LogSink() = default;

    virtual void write(const LogMessage &message) = 0;
    virtual void flush() = 0;
};

class Logger final
{
  public:
    Logger();
    Logger(std::vector<std::unique_ptr<LogSink>> sinks) : m_sinks(std::move(sinks))
    {
    }
    ~Logger();

    Logger(const Logger &) = delete;
    Logger &operator=(const Logger &) = delete;

    Logger(Logger &&) = delete;
    Logger &operator=(Logger &&) = delete;

    void add_sink(std::unique_ptr<LogSink> sink);

    void info(std::string message, const std::source_location &location = std::source_location::current());

    void warning(std::string message, const std::source_location &location = std::source_location::current());

    void error(std::string message, const std::source_location &location = std::source_location::current());

    void error_errno(std::string message, const std::source_location &location = std::source_location::current())
    {
        error(message, location);
    }

    // Reject new messages and drain the queue.
    // Safe to call multiple times and concurrently.
    void shutdown();

  private:
    enum class State
    {
        Created,
        Running,
        Stopping,
        Stopped
    };

    void log(LogLevel level, std::string message, const std::source_location &location);
    void process_messages();

    std::queue<LogMessage> m_queue;

    std::mutex m_queueMutex;
    std::condition_variable m_queueCondition;

    // Serializes concurrent shutdown/join operations.
    std::mutex m_shutdownMutex;

    std::vector<std::unique_ptr<LogSink>> m_sinks;

    std::jthread m_worker;

    State m_state = State::Created;

    // Protected by queueMutex_.
    std::thread::id m_workerThreadId;
};

class GlobalLogger final
{
  public:
    static Logger &instance();

    static void shutdown();

    GlobalLogger() = delete;
};

} // namespace logging
