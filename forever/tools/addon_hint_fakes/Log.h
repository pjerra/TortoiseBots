// TC_LOG_* as no-ops for the standalone add-on hint test (the live half only logs).
#pragma once

template <class... Args>
inline void TC_LOG_INFO(Args const&...)
{
}

template <class... Args>
inline void TC_LOG_WARN(Args const&...)
{
}

template <class... Args>
inline void TC_LOG_ERROR(Args const&...)
{
}
