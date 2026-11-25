// The core logger as the host calls it (Log::instance()->OutMessage), with fmt's format_string reduced to the
// literal: every line is recorded as written, placeholders and all, so the check can count the passes a hold took
// and read the words that say why a piece stayed.
#pragma once

#include <string>
#include <vector>

enum LogLevel
{
    LOG_LEVEL_TRACE,
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR,
    LOG_LEVEL_FATAL,
};

namespace fmt
{
template <typename... Args>
using format_string = char const*;
}

struct LogLine
{
    std::string logger;
    LogLevel level = LOG_LEVEL_INFO;
    std::string message;
};

inline std::vector<LogLine>& LogLines()
{
    static std::vector<LogLine> lines;
    return lines;
}

class Log
{
public:
    static Log* instance()
    {
        static Log log;
        return &log;
    }

    template <typename... Args>
    void OutMessage(char const* logger, LogLevel level, fmt::format_string<Args...> message, Args&&...)
    {
        LogLines().push_back(LogLine{ logger ? logger : "", level, message ? message : "" });
    }
};
