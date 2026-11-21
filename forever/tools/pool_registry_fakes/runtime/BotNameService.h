// The name service the pool's creation calls: a fixed list, answered in order. An exhausted list answers empty (the pool's
// "no usable character name came up" path).
#pragma once

#include "Define.h"

#include <cstddef>
#include <string>
#include <vector>

namespace TortoiseBots
{
inline std::vector<std::string> fakeBotNames;
inline std::size_t fakeBotNameIndex = 0;

inline std::string PickBotCharacterName(uint8 /*race*/, uint8 /*cls*/, uint8 /*gender*/)
{
    if (fakeBotNameIndex >= fakeBotNames.size())
        return "";
    return fakeBotNames[fakeBotNameIndex++];
}
} // namespace TortoiseBots
