// Trinity::ChatCommands::Tail: a command's remaining text, convertible to std::string as the live half reads it.
#pragma once

#include <string>

namespace Trinity
{
namespace ChatCommands
{
struct Tail
{
    std::string text;

    operator std::string() const { return text; }
};
} // namespace ChatCommands
} // namespace Trinity
