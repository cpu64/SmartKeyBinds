#include "sinks.hpp"

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

constexpr int LogLevelWidth = 7;

constexpr const char *ResetColor = "\033[0m";
constexpr const char *WarningColor = "\033[33m";
constexpr const char *ErrorColor = "\033[31m";

const char *levelName(LogLevel level)
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

const char *levelColor(LogLevel level)
{
    switch (level)
    {
    case LogLevel::Info:
        return ResetColor;

    case LogLevel::Warning:
        return WarningColor;

    case LogLevel::Error:
        return ErrorColor;
    }

    return ResetColor;
}

std::string formatMessage(const LogMessage &message)
{
    using namespace std::chrono;

    const auto seconds = floor<std::chrono::seconds>(message.timestamp);

    const auto milliseconds = duration_cast<std::chrono::milliseconds>(message.timestamp - seconds).count();

    const auto time = system_clock::to_time_t(message.timestamp);

    std::tm tm{};

#ifdef _WIN32
    localtime_s(&tm, &time);
#else
    localtime_r(&time, &tm);
#endif

    std::ostringstream output;

    output << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << '.' << std::setfill('0') << std::setw(3) << milliseconds
           << " [" << std::left << std::setw(LogLevelWidth) << levelName(message.level) << std::right << "] ["
           << message.threadId << "] " << message.location.file_name() << ':' << message.location.line() << " ("
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

    output << levelColor(message.level) << formatMessage(message) << ResetColor << '\n';

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

FileSink::FileSink(const std::string &filename) : file_(filename, std::ios::out | std::ios::app)
{
    if (!file_.is_open())
    {
        throw std::runtime_error("Failed to open log file: " + filename);
    }
}

void FileSink::write(const LogMessage &message)
{
    file_ << formatMessage(message) << '\n';

    if (!file_)
    {
        throw std::runtime_error("Failed to write to log file");
    }

    // Flush every message to reduce data loss
    // if the application terminates unexpectedly.
    file_.flush();

    if (!file_)
    {
        throw std::runtime_error("Failed to flush log file");
    }
}

void FileSink::flush()
{
    file_.flush();

    if (!file_)
    {
        throw std::runtime_error("Failed to flush log file");
    }
}

} // namespace logging
