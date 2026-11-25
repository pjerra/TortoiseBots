#pragma once

// Many WoW Forever item rows carry RequiredLevel 0 even for high item levels, so the core lets a level-10
// character wear an item level 62 blue. For pool-bot gear decisions an item's level requirement is the
// stored one, or, when that is 0, one derived from its item level.
//
// The live data says the missing requirement is `item level - 5`, clamped: 11,223 of the 12,289 weapon and
// armour rows that do store one (91%) match that rule. Above the level cap the stored requirement stays at
// the cap (Naxx/AQ epics at item level 80-90 store 58-64), so the derived value is clamped too; without the
// clamp every endgame epic would look unwearable.
//
// 177 weapon and armour rows store RequiredLevel 1 next to an item level of 15 or more (Demonbane Knife, ilvl 59) -
// placeholder rows the core reads as "wearable from level 1". A stored 1 is the real requirement of the starter
// items, whose item levels are 1-5, so the placeholder is only recognised above that.
//
// Pure and core-free, so tools/test_gear_effective_level_policy.cpp includes it directly.

#include <cstdint>

namespace TortoiseBots
{
// The realm's level cap (ForeverCore worldserver.conf.dist: MaxPlayerLevel = 60).
inline constexpr std::uint32_t kGearMaxRequiredLevel = 60;

inline std::uint32_t GearEffectiveRequiredLevel(std::uint32_t requiredLevel, std::uint32_t itemLevel)
{
    if (requiredLevel > 1 || (requiredLevel == 1 && itemLevel < 15))
        return requiredLevel;
    std::uint32_t const derived = itemLevel > 5 ? itemLevel - 5 : 1;
    return derived < kGearMaxRequiredLevel ? derived : kGearMaxRequiredLevel;
}
}
