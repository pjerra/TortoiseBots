// The store's log calls, as no-ops for the standalone check.
#pragma once

template <class... Args>
inline void TC_LOG_INFO(Args const&...)
{
}

template <class... Args>
inline void TC_LOG_ERROR(Args const&...)
{
}
