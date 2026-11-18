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
    std::thread::id threadId;

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
    Logger(std::vector<std::unique_ptr<LogSink>> sinks) : sinks_(std::move(sinks))
    {
    }
    ~Logger();

    Logger(const Logger &) = delete;
    Logger &operator=(const Logger &) = delete;

    Logger(Logger &&) = delete;
    Logger &operator=(Logger &&) = delete;

    void addSink(std::unique_ptr<LogSink> sink);

    void info(std::string message, const std::source_location &location = std::source_location::current());

    void warning(std::string message, const std::source_location &location = std::source_location::current());

    void error(std::string message, const std::source_location &location = std::source_location::current());

    void Errno(std::string message, const std::source_location &location = std::source_location::current())
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
    void processMessages();

    std::queue<LogMessage> queue_;

    std::mutex queueMutex_;
    std::condition_variable queueCondition_;

    // Serializes concurrent shutdown/join operations.
    std::mutex shutdownMutex_;

    std::vector<std::unique_ptr<LogSink>> sinks_;

    std::jthread worker_;

    State state_ = State::Created;

    // Protected by queueMutex_.
    std::thread::id workerThreadId_;
};

class GlobalLogger final
{
  public:
    static Logger &instance();

    static void shutdown();

    GlobalLogger() = delete;
};

} // namespace logging
