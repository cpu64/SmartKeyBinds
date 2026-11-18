#include "logger.hpp"
#include "sinks.hpp"

#include <stdexcept>
#include <utility>

namespace logging
{

Logger::Logger() = default;

// --------------------------------------------------
// Logger destructor
// --------------------------------------------------

Logger::~Logger()
{
    shutdown();
}

// --------------------------------------------------
// Sink registration
// --------------------------------------------------

void Logger::addSink(std::unique_ptr<LogSink> sink)
{
    if (!sink)
        return;

    std::lock_guard lock(queueMutex_);

    if (state_ != State::Created)
    {
        throw std::logic_error("Cannot add sinks after logger startup or shutdown");
    }

    sinks_.push_back(std::move(sink));
}

// --------------------------------------------------
// Public logging interface
// --------------------------------------------------

void Logger::info(std::string message, const std::source_location &location)
{
    log(LogLevel::Info, std::move(message), location);
}

void Logger::warning(std::string message, const std::source_location &location)
{
    log(LogLevel::Warning, std::move(message), location);
}

void Logger::error(std::string message, const std::source_location &location)
{
    log(LogLevel::Error, std::move(message), location);
}

// --------------------------------------------------
// Internal logging implementation
// --------------------------------------------------

void Logger::log(LogLevel level, std::string message, const std::source_location &location)
{
    {
        std::lock_guard lock(queueMutex_);

        if (state_ == State::Stopping || state_ == State::Stopped)
        {
            return;
        }

        if (state_ == State::Created)
        {
            worker_ = std::jthread([this] { processMessages(); });

            state_ = State::Running;
        }

        queue_.push(LogMessage{level, std::move(message), std::chrono::system_clock::now(), std::this_thread::get_id(),
                               location});
    }

    queueCondition_.notify_one();
}

// --------------------------------------------------
// Worker thread
// --------------------------------------------------

void Logger::processMessages()
{
    {
        std::lock_guard lock(queueMutex_);
        workerThreadId_ = std::this_thread::get_id();
    }

    while (true)
    {
        LogMessage message;

        {
            std::unique_lock lock(queueMutex_);

            queueCondition_.wait(lock, [this] { return !queue_.empty() || state_ == State::Stopping; });

            // Exit only after draining the queue.
            if (queue_.empty() && state_ == State::Stopping)
            {
                break;
            }

            message = std::move(queue_.front());
            queue_.pop();
        }

        // Only the worker accesses sinks.
        for (const auto &sink : sinks_)
        {
            try
            {
                sink->write(message);
            }
            catch (...)
            {
                // Isolate sink failures.
            }
        }
    }

    // Flush after all accepted messages are processed.
    for (const auto &sink : sinks_)
    {
        try
        {
            sink->flush();
        }
        catch (...)
        {
            // Ignore sink flush failures.
        }
    }
}

// --------------------------------------------------
// Shutdown
// --------------------------------------------------

void Logger::shutdown()
{
    // A worker cannot join itself.
    {
        std::lock_guard lock(queueMutex_);

        if (workerThreadId_ == std::this_thread::get_id())
        {
            if (state_ == State::Running)
                state_ = State::Stopping;

            queueCondition_.notify_all();
            return;
        }
    }

    // Serialize all external shutdown callers.
    std::lock_guard shutdownLock(shutdownMutex_);

    {
        std::lock_guard lock(queueMutex_);

        if (state_ == State::Stopped)
            return;

        if (state_ == State::Created)
        {
            state_ = State::Stopped;
            return;
        }

        state_ = State::Stopping;
    }

    queueCondition_.notify_all();

    if (worker_.joinable())
        worker_.join();

    {
        std::lock_guard lock(queueMutex_);
        state_ = State::Stopped;
        workerThreadId_ = {};
    }
}

// --------------------------------------------------
// GlobalLogger
// --------------------------------------------------

Logger &GlobalLogger::instance()
{
    static Logger *logger = new Logger();
    return *logger;
}

void GlobalLogger::shutdown()
{
    instance().shutdown();
}

} // namespace logging
