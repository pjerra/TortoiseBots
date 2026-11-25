#pragma once

// Many WoW Forever item rows carry RequiredLevel 0 even for high item levels, so the core lets a level-10
// character wear an item level 62 blue. For pool-bot gear decisions an item's level requirement is the
// stored one, or, when that is 0, one derived from its item level.
//
// Pure and core-free, so tools/test_gear_effective_level_policy.cpp includes it directly.

#include <cstdint>

namespace TortoiseBots
{
inline std::uint32_t GearEffectiveRequiredLevel(std::uint32_t requiredLevel, std::uint32_t itemLevel)
{
    if (requiredLevel > 0)
        return requiredLevel;
    return itemLevel > 5 ? itemLevel - 5 : 1;
}
}
