// See SliceLog.h. No legacy header may be included here.
#include "Log.h"
#include "SliceLog.h"

namespace ai
{
namespace slice
{
    void LogInfo(std::string const& message)
    {
        TC_LOG_INFO("tortoisebots", "{}", message);
    }

    void LogWarn(std::string const& message)
    {
        TC_LOG_WARN("tortoisebots", "{}", message);
    }
}
}
