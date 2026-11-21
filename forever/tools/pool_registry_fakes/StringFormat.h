// Text formatting for the standalone pool test: `{}` replaced in order, with the arithmetic and string arguments the pool uses
// and std::to_string results (ForeverCore's StringFormat.h is fmt-based).
#pragma once

#include <string>
#include <type_traits>
#include <vector>

namespace Trinity
{
inline std::string TbText(std::string const& value) { return value; }
inline std::string TbText(char const* value) { return value; }

template <class T, std::enable_if_t<std::is_arithmetic_v<T>, int> = 0>
std::string TbText(T value)
{
    return std::to_string(value);
}

template <class... Args>
std::string StringFormat(char const* format, Args const&... args)
{
    std::vector<std::string> const values = { TbText(args)... };
    std::string out;
    std::size_t next = 0;
    for (char const* at = format; *at; ++at)
    {
        if (at[0] == '{' && at[1] == '}')
        {
            out += values.at(next++);
            ++at;
        }
        else
            out += *at;
    }
    return out;
}
} // namespace Trinity
