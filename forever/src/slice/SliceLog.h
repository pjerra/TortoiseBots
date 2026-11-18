// Slice 1: log lines from the slice sources. The legacy header playerbot/playerbot.h redefines `sLog` as BotLog::Instance(),
// which breaks the core's TC_LOG_* macros in any file that includes it; these two functions are defined in SliceLog.cpp,
// which includes the core's Log.h only. Category "tortoisebots", like TbBotMgr.cpp.
#pragma once

#include <string>

namespace ai
{
namespace slice
{
    void LogInfo(std::string const& message);
    void LogWarn(std::string const& message);
}
}
