#pragma once

// Quality mix of the one-time spawn kit (owner's contract, rule 4): a low-level pool bot must not arrive in a full
// green or blue outfit. The kit has a floor (white below level 20, green from 20: GearSeedQualityPolicy.h) and a
// range of slots that may take one quality above the floor:
//
//   levels 5-9    white, 0-1 green
//   levels 10-14  white, 1-2 green
//   levels 15-19  white, 2-3 green
//   levels 20-29  green, 0-2 blue
//   levels 30-49  green, 3-5 blue
//   levels 50-60  green, 6-9 blue
//
// The count is rolled per bot inside the band. The upgraded slots are picked at random among the slots that really
// have a candidate of the upgraded quality, so a level-20 kit never spends its blues on an empty trinket slot and two
// bots of one level do not wear the same qualities on the same slots. Each slot then picks among the best-fitting
// candidates (top five, or everything within 85% of the best score, whichever is wider). A slot at the green floor with
// nothing green falls back to white (level 20 and up) instead of staying empty. Epics are never part of the mix.
//
// Pure and core-free, with the random source injected, so tools/test_gear_seed_mix_policy.cpp exercises the rolls
// deterministically.

#include "GearSeedQualityPolicy.h"

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

namespace TortoiseBots
{
inline constexpr std::uint32_t kSpawnSeedMinLevel = 5;
// GearSetSlotOrder() in ai/playerbot/policy/item/GearSetPolicy.h: the 17 equipment slots a kit fills.
inline constexpr std::uint32_t kSpawnKitSlots = 17;
// A slot may pick among its five best-scoring candidates, or every candidate scoring at least this share of the best.
inline constexpr std::uint32_t kSeedCandidateWindow = 5;
inline constexpr std::uint32_t kSeedCandidateScorePercent = 85;

struct GearSeedQualityPlan
{
    std::uint8_t baseQuality = 0;      // SeedQualityFloor(level)
    std::uint8_t upgradedQuality = 0;  // base + 1, never above rare
    std::uint32_t minUpgradedSlots = 0;
    std::uint32_t maxUpgradedSlots = 0;
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
    std::uint32_t low = 0;
    std::uint32_t high = 0;
    if (level < 10)
        low = 0, high = 1;
    else if (level < 15)
        low = 1, high = 2;
    else if (level < 20)
        low = 2, high = 3;
    else if (level < 30)
        low = 0, high = 2;
    else if (level < 50)
        low = 3, high = 5;
    else
        low = 6, high = 9;

    return { base, static_cast<std::uint8_t>(base + 1), low, high };
}

// Rolls the upgraded-slot count inside the plan's range. `randomBelow(n)` returns a value in [0, n).
template <typename RandomBelow>
inline std::uint32_t SeedUpgradedSlotCount(GearSeedQualityPlan const& plan, RandomBelow&& randomBelow)
{
    std::uint32_t const span = plan.maxUpgradedSlots - plan.minUpgradedSlots + 1;
    return plan.minUpgradedSlots + (span > 1 ? randomBelow(span) : 0);
}

// Picks `count` distinct entries of `eligible` (partial Fisher-Yates with the injected source).
template <typename RandomBelow>
inline std::vector<std::uint32_t> SeedChooseUpgradedSlots(std::vector<std::uint32_t> eligible, std::uint32_t count,
    RandomBelow&& randomBelow)
{
    count = std::min<std::uint32_t>(count, static_cast<std::uint32_t>(eligible.size()));
    for (std::uint32_t i = 0; i < count; ++i)
    {
        std::uint32_t const remaining = static_cast<std::uint32_t>(eligible.size()) - i;
        std::uint32_t const pick = i + (remaining > 1 ? randomBelow(remaining) : 0);
        std::swap(eligible[i], eligible[pick]);
    }
    eligible.resize(count);
    return eligible;
}

// How many leading entries of a slot's score-sorted candidate list the kit may pick from.
inline std::uint32_t SeedCandidatePickCount(std::vector<std::uint32_t> const& sortedScores)
{
    if (sortedScores.empty())
        return 0;
    std::uint32_t const best = sortedScores.front();
    std::uint32_t const floor = best - (best * (100 - kSeedCandidateScorePercent)) / 100;
    std::size_t window = 0;
    while (window < sortedScores.size() && (window < kSeedCandidateWindow || sortedScores[window] >= floor))
        ++window;
    return static_cast<std::uint32_t>(window);
}

// A slot at the green floor may fall back to white; below level 20 the floor is already white and the owner's mix is
// "the rest white", so an empty slot stays empty.
inline std::uint8_t SeedFallbackQuality(std::uint8_t baseQuality)
{
    return baseQuality > 1 ? static_cast<std::uint8_t>(baseQuality - 1) : baseQuality;
}
}
