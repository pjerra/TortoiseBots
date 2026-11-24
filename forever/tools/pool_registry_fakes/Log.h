// The log surface the pool uses: ForeverCore's Log.h writes through the log singleton; the pool test only needs the calls to
// compile, and the lines the pool writes while it works (the test counts none of them).
#pragma once

#include <string>

inline std::string fakeLastLog;

template <class... Args>
inline void TC_LOG_ERROR(char const* /*filter*/, char const* /*format*/, Args const&...)
{
}

template <class... Args>
inline void TC_LOG_INFO(char const* /*filter*/, char const* /*format*/, Args const&...)
{
}

template <class... Args>
inline void TC_LOG_WARN(char const* /*filter*/, char const* /*format*/, Args const&...)
{
}
