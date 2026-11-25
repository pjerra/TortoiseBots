#pragma once

// Quality mix of the one-time spawn kit (owner's contract, rule 4): a low-level pool bot must not arrive in a full
// green or blue outfit. The kit has a floor (white below level 20, green from 20: GearSeedQualityPolicy.h) and a
// fixed number of slots that may take one quality above the floor:
//
//   levels 5-9    white, up to 1 green
//   levels 10-19  white, 3 green
//   levels 20-29  green, 2 blue
//   levels 30-49  green, 5 blue
//   levels 50-60  green, 9 blue
//
// Epics are never part of the mix. The upgraded slots are one contiguous run that an offset rotates per bot, so the
// count is exact while two bots of one level do not wear the same pieces in the same qualities.
//
// Pure and core-free, so tools/test_gear_seed_mix_policy.cpp includes it directly.

#include "GearSeedQualityPolicy.h"

#include <cstdint>

namespace TortoiseBots
{
inline constexpr std::uint32_t kSpawnSeedMinLevel = 5;
// GearSetSlotOrder() in ai/playerbot/policy/item/GearSetPolicy.h: the 17 equipment slots a kit fills.
inline constexpr std::uint32_t kSpawnKitSlots = 17;

struct GearSeedQualityPlan
{
    std::uint8_t baseQuality = 0;      // SeedQualityFloor(level)
    std::uint8_t upgradedQuality = 0;  // base + 1, never above rare
    std::uint32_t upgradedSlots = 0;
};

// Below level 5 the starter outfit stays (the core's own starting items).
inline bool SpawnSeedReady(std::uint32_t level)
{
    return level >= kSpawnSeedMinLevel;
}

inline GearSeedQualityPlan SeedQualityPlan(std::uint32_t level)
{
    if (!SpawnSeedReady(level))
        return {};

    std::uint8_t const base = static_cast<std::uint8_t>(SeedQualityFloor(level));
    std::uint32_t upgraded = 0;
    if (level < 10)
        upgraded = 1;
    else if (level < 20)
        upgraded = 3;
    else if (level < 30)
        upgraded = 2;
    else if (level < 50)
        upgraded = 5;
    else
        upgraded = 9;

    return { base, static_cast<std::uint8_t>(base + 1), upgraded };
}

// The upgraded slots are the first `upgradedSlots` of the ring `slotRank + offset`.
inline bool SeedSlotUpgraded(std::uint32_t slotRank, std::uint32_t slotCount, std::uint32_t upgradedSlots,
    std::uint32_t offset)
{
    if (slotCount == 0 || upgradedSlots == 0)
        return false;
    if (upgradedSlots >= slotCount)
        return true;
    return ((slotRank + offset) % slotCount) < upgradedSlots;
}

// Highest item quality one slot of the kit may wear. `offset` is urand(0, slotCount - 1) at seed time.
inline std::uint8_t SeedSlotQualityCap(std::uint32_t level, std::uint32_t slotRank, std::uint32_t slotCount,
    std::uint32_t offset)
{
    GearSeedQualityPlan const plan = SeedQualityPlan(level);
    if (plan.upgradedSlots == 0)
        return 0;
    return SeedSlotUpgraded(slotRank, slotCount, plan.upgradedSlots, offset) ? plan.upgradedQuality
                                                                           : plan.baseQuality;
}
}
