#include "./sinks.hpp"

#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace logging
{

namespace
{

// --------------------------------------------------
// Formatting helpers
// --------------------------------------------------

constexpr int LOG_LEVEL_WIDTH = 7;

constexpr const char *RESET_COLOR = "\033[0m";
constexpr const char *WARNING_COLOR = "\033[33m";
constexpr const char *ERROR_COLOR = "\033[31m";

const char *level_name(LogLevel level)
{
    switch (level)
    {
    case LogLevel::Info:
        return "INFO";

    case LogLevel::Warning:
        return "WARNING";

    case LogLevel::Error:
        return "ERROR";
    }

    return "UNKNOWN";
}

const char *level_color(LogLevel level)
{
    switch (level)
    {
    case LogLevel::Info:
        return RESET_COLOR;

    case LogLevel::Warning:
        return WARNING_COLOR;

    case LogLevel::Error:
        return ERROR_COLOR;
    }

    return RESET_COLOR;
}

std::string format_message(const LogMessage &message)
{
    using namespace std::chrono;

    const auto Seconds = floor<std::chrono::seconds>(message.timestamp);

    const auto Milliseconds = duration_cast<std::chrono::milliseconds>(message.timestamp - Seconds).count();

    const auto Time = system_clock::to_time_t(message.timestamp);

    std::tm tm{};

#ifdef _WIN32
    localtime_s(&tm, &time);
#else
    localtime_r(&Time, &tm);
#endif

    std::ostringstream output;

    output << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << '.' << std::setfill('0') << std::setw(3) << Milliseconds
           << " [" << std::left << std::setw(LOG_LEVEL_WIDTH) << level_name(message.level) << std::right << "] ["
           << message.thread_id << "] " << message.location.file_name() << ':' << message.location.line() << " ("
           << message.location.function_name() << ") " << message.message;

    return output.str();
}

} // namespace

// ==================================================
// ConsoleSink
// ==================================================

void ConsoleSink::write(const LogMessage &message)
{
    std::ostream &output = message.level == LogLevel::Error ? std::cerr : std::cout;

    output << level_color(message.level) << format_message(message) << RESET_COLOR << '\n';

    if (!output)
    {
        throw std::runtime_error("Failed to write to console");
    }
}

void ConsoleSink::flush()
{
    std::cout.flush();
    std::cerr.flush();

    if (!std::cout || !std::cerr)
    {
        throw std::runtime_error("Failed to flush console");
    }
}

// ==================================================
// FileSink
// ==================================================

FileSink::FileSink(const std::string &filename) : m_file(filename, std::ios::out | std::ios::app)
{
    if (!m_file.is_open())
    {
        throw std::runtime_error("Failed to open log file: " + filename);
    }
}

void FileSink::write(const LogMessage &message)
{
    m_file << format_message(message) << '\n';

    if (!m_file)
    {
        throw std::runtime_error("Failed to write to log file");
    }

    // Flush every message to reduce data loss
    // if the application terminates unexpectedly.
    m_file.flush();

    if (!m_file)
    {
        throw std::runtime_error("Failed to flush log file");
    }
}

void FileSink::flush()
{
    m_file.flush();

    if (!m_file)
    {
        throw std::runtime_error("Failed to flush log file");
    }
}

} // namespace logging
