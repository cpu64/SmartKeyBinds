#pragma once

#include "./logger.hpp"

#include <fstream>
#include <string>

namespace logging
{

// --------------------------------------------------
// ConsoleSink
// --------------------------------------------------

class ConsoleSink final : public LogSink
{
  public:
    void write(const LogMessage &message) override;
    void flush() override;
};

// --------------------------------------------------
// FileSink
// --------------------------------------------------

class FileSink final : public LogSink
{
  public:
    explicit FileSink(const std::string &filename);

    void write(const LogMessage &message) override;
    void flush() override;

  private:
    std::ofstream m_file;
};

} // namespace logging
