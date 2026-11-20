// TC_LOG_ERROR as a no-op for the standalone store test (ForeverCore's Log.h writes through the log singleton). The store only logs, so the test
// needs the call to compile and nothing else.
#pragma once

template <class... Args>
inline void TC_LOG_ERROR(Args const&...)
{
}
