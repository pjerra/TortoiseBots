// Core-free vendor ration ladder (levels 1-60) for pool and free bots.
// Item entries match Classic vendor rations verified against ForeverCore's ItemSparse DB2 and world npc_vendor.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace TortoiseBots
{
namespace autofood
{

struct FoodTier
{
    uint8_t minLevel;
    std::array<uint32_t, 6> foodIds;
    uint32_t drinkId;
};

inline constexpr std::array<FoodTier, 6> kRationTiers = {{
    {  1, {{  117, 4540, 4536, 2070,   787, 4604 }},  159 },
    {  5, {{ 2287, 4541, 4537,  414,  4592, 4605 }}, 1179 },
    { 15, {{ 3770, 4542, 4538,  422,  4593, 4606 }}, 1205 },
    { 25, {{ 3771, 4544, 4539, 1707,  4594, 4607 }}, 1708 },
    { 35, {{ 4599, 4601, 4602, 3927, 21552, 4608 }}, 1645 },
    { 45, {{ 8952, 8950, 8953, 8932,  8957, 8948 }}, 8766 },
}};

inline std::size_t TierIndexForLevel(uint8_t level)
{
    std::size_t idx = 0;
    for (std::size_t i = 0; i < kRationTiers.size(); ++i)
    {
        if (level >= kRationTiers[i].minLevel)
            idx = i;
    }
    return idx;
}

inline uint32_t PickFoodItem(uint8_t level, uint32_t pick = 0)
{
    auto const& foods = kRationTiers[TierIndexForLevel(level)].foodIds;
    return foods[pick % foods.size()];
}

inline uint32_t PickDrinkItem(uint8_t level)
{
    return kRationTiers[TierIndexForLevel(level)].drinkId;
}

inline std::vector<uint32_t> StaleLadderFoodItems(uint8_t level)
{
    std::vector<uint32_t> stale;
    std::size_t const current = TierIndexForLevel(level);
    for (std::size_t i = 0; i < current; ++i)
        for (uint32_t id : kRationTiers[i].foodIds)
            stale.push_back(id);
    return stale;
}

inline std::vector<uint32_t> StaleLadderDrinkItems(uint8_t level)
{
    std::vector<uint32_t> stale;
    std::size_t const current = TierIndexForLevel(level);
    for (std::size_t i = 0; i < current; ++i)
        stale.push_back(kRationTiers[i].drinkId);
    return stale;
}

inline constexpr uint32_t kTopUpThreshold = 5;
inline constexpr uint32_t kMaxRationStack = 20;
inline constexpr uint64_t kTopUpIntervalMs = 60000;

inline bool NeedsTopUp(uint32_t haveCount, uint32_t minThreshold = kTopUpThreshold)
{
    return haveCount < minThreshold;
}

inline bool TopUpCooldownElapsed(uint64_t lastMs, uint64_t nowMs, uint64_t intervalMs = kTopUpIntervalMs)
{
    return lastMs == 0 || (nowMs >= lastMs && (nowMs - lastMs) >= intervalMs);
}

} // namespace autofood
} // namespace TortoiseBots
