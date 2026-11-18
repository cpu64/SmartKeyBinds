#include "./logger.hpp"

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

void Logger::add_sink(std::unique_ptr<LogSink> sink)
{
    if (!sink)
        return;

    std::lock_guard lock(m_queueMutex);

    if (m_state != State::Created)
    {
        throw std::logic_error("Cannot add sinks after logger startup or shutdown");
    }

    m_sinks.push_back(std::move(sink));
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
        std::lock_guard lock(m_queueMutex);

        if (m_state == State::Stopping || m_state == State::Stopped)
        {
            return;
        }

        if (m_state == State::Created)
        {
            m_worker = std::jthread([this] { process_messages(); });

            m_state = State::Running;
        }

        m_queue.push(LogMessage{level, std::move(message), std::chrono::system_clock::now(), std::this_thread::get_id(),
                                location});
    }

    m_queueCondition.notify_one();
}

// --------------------------------------------------
// Worker thread
// --------------------------------------------------

void Logger::process_messages()
{
    {
        std::lock_guard lock(m_queueMutex);
        m_workerThreadId = std::this_thread::get_id();
    }

    while (true)
    {
        LogMessage message;

        {
            std::unique_lock lock(m_queueMutex);

            m_queueCondition.wait(lock, [this] { return !m_queue.empty() || m_state == State::Stopping; });

            // Exit only after draining the queue.
            if (m_queue.empty() && m_state == State::Stopping)
            {
                break;
            }

            message = std::move(m_queue.front());
            m_queue.pop();
        }

        // Only the worker accesses sinks.
        for (const auto &sink : m_sinks)
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
    for (const auto &sink : m_sinks)
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
        std::lock_guard lock(m_queueMutex);

        if (m_workerThreadId == std::this_thread::get_id())
        {
            if (m_state == State::Running)
                m_state = State::Stopping;

            m_queueCondition.notify_all();
            return;
        }
    }

    // Serialize all external shutdown callers.
    std::lock_guard shutdown_lock(m_shutdownMutex);

    {
        std::lock_guard lock(m_queueMutex);

        if (m_state == State::Stopped)
            return;

        if (m_state == State::Created)
        {
            m_state = State::Stopped;
            return;
        }

        m_state = State::Stopping;
    }

    m_queueCondition.notify_all();

    if (m_worker.joinable())
        m_worker.join();

    {
        std::lock_guard lock(m_queueMutex);
        m_state = State::Stopped;
        m_workerThreadId = {};
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
