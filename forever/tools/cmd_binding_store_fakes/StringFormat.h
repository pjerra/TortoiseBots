// Trinity::StringFormat as the standalone store test needs it (ForeverCore's StringFormat.h is fmt-based): every `{}` is replaced, in order, by the
// argument's text. The store formats numbers and one unquoted text field.
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
}
