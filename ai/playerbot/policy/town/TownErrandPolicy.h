#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
#include <string>

namespace ai
{

// Pure W2 vendor item gate: checks PlayerConditionId, ExtendedCost, vendor condition,
// reputation requirement, and limited-stock availability before calling BuyItemFromVendorSlot.
inline bool VendorItemPickAllowed(
    uint32_t playerConditionId,
    bool meetsPlayerCondition,
    uint32_t extendedCost,
    bool meetsVendorCondition,
    bool reputationOk,
    uint32_t maxCount,
    uint32_t currentCount,
    std::string* refusalReason = nullptr)
{
    if (playerConditionId != 0 && !meetsPlayerCondition)
    {
        if (refusalReason)
            *refusalReason = "player-condition:" + std::to_string(playerConditionId);
        return false;
    }
    if (extendedCost != 0)
    {
        if (refusalReason)
            *refusalReason = "extended-cost:" + std::to_string(extendedCost);
        return false;
    }
    if (!meetsVendorCondition)
    {
        if (refusalReason)
            *refusalReason = "vendor-condition";
        return false;
    }
    if (!reputationOk)
    {
        if (refusalReason)
            *refusalReason = "reputation";
        return false;
    }
    if (maxCount != 0 && currentCount < 1)
    {
        if (refusalReason)
            *refusalReason = "sold-out";
        return false;
    }
    return true;
}

// Pure W2 sell-list filter: greys only (quality == 0), never quest items,
// never soulbound or currently equipped items, and must have a non-zero sell price.
inline bool ShouldSellItemW2(
    uint32_t quality,
    uint32_t itemClass,
    bool isSoulbound,
    bool isEquipped,
    uint32_t sellPrice)
{
    constexpr uint32_t kQualityPoor = 0;
    constexpr uint32_t kClassQuest = 12;
    if (sellPrice == 0)
        return false;
    if (itemClass == kClassQuest)
        return false;
    if (isSoulbound || isEquipped)
        return false;
    return quality == kQualityPoor;
}

// Pure W2 repair threshold: repair when average durability < 30% or lowest piece < 10%,
// or when explicitly ordered and any piece is below 100%.
inline bool ShouldRepairW2(uint8_t avgDurabilityPct, uint8_t lowestDurabilityPct, bool ordered = false)
{
    if (ordered)
        return avgDurabilityPct < 100 || lowestDurabilityPct < 100;
    return avgDurabilityPct < 30 || lowestDurabilityPct < 10;
}

// Pure W2 trainer rank affordability & readiness check.
inline bool TrainerRankDueAndAffordable(
    bool alreadyKnown,
    uint8_t botLevel,
    uint8_t reqLevel,
    bool classRaceOk,
    bool prereqsOk,
    uint32_t moneyCost,
    float discountMod,
    uint32_t botMoney,
    std::string* refusalReason = nullptr)
{
    if (alreadyKnown)
    {
        if (refusalReason)
            *refusalReason = "known";
        return false;
    }
    if (!classRaceOk)
    {
        if (refusalReason)
            *refusalReason = "class-race";
        return false;
    }
    if (botLevel < reqLevel)
    {
        if (refusalReason)
            *refusalReason = "level";
        return false;
    }
    if (!prereqsOk)
    {
        if (refusalReason)
            *refusalReason = "prereq";
        return false;
    }
    uint32_t const discounted = static_cast<uint32_t>(std::floor(static_cast<double>(moneyCost) * static_cast<double>(discountMod)));
    if (botMoney < discounted)
    {
        if (refusalReason)
            *refusalReason = "money";
        return false;
    }
    return true;
}

// Pure W2 supply targets by level and class, matching forever/data/waterys/supplies-by-level.csv.
struct SupplyTargetW2
{
    bool needsFood = true;
    bool needsWater = false;
    bool needsAmmo = false;
    uint32_t foodItemId = 0;
    uint32_t waterItemId = 0;
    uint32_t ammoItemId = 0;
    uint32_t targetStacks = 1;
};

inline SupplyTargetW2 SupplyTargetForLevelAndClass(uint8_t level, uint8_t playerClass)
{
    // Class IDs: 1=Warrior, 2=Paladin, 3=Hunter, 4=Rogue, 5=Priest, 7=Shaman, 8=Mage, 9=Warlock, 11=Druid
    SupplyTargetW2 out;
    bool const usesMana = (playerClass == 2 || playerClass == 3 || playerClass == 5 ||
                           playerClass == 7 || playerClass == 8 || playerClass == 9 || playerClass == 11);
    out.needsFood = true;
    out.needsWater = usesMana;
    out.needsAmmo = (playerClass == 3);
    out.targetStacks = out.needsAmmo ? 2 : 1;

    if (level < 5)
    {
        out.foodItemId = 117;   // Tough Jerky
        out.waterItemId = 159;  // Refreshing Spring Water
        out.ammoItemId = 2512;  // Rough Arrow
    }
    else if (level < 15)
    {
        out.foodItemId = 414;   // Dalaran Sharp
        out.waterItemId = 1179; // Ice Cold Milk
        out.ammoItemId = 2515;  // Sharp Arrow
    }
    else if (level < 25)
    {
        out.foodItemId = 422;   // Dwarven Mild
        out.waterItemId = 1205; // Melon Juice
        out.ammoItemId = 3030;  // Razor Arrow
    }
    else if (level < 35)
    {
        out.foodItemId = 1707;  // Stormwind Brie
        out.waterItemId = 1708; // Sweet Nectar
        out.ammoItemId = 3030;  // Razor Arrow
    }
    else if (level < 45)
    {
        out.foodItemId = 3927;  // Fine Aged Cheddar
        out.waterItemId = 1645; // Moonberry Juice
        out.ammoItemId = 11285; // Jagged Arrow
    }
    else
    {
        out.foodItemId = 8952;  // Roasted Quail
        out.waterItemId = 8766; // Morning Glory Dew
        out.ammoItemId = 11285; // Jagged Arrow
    }
    return out;
}

// One rank the trainer visit considers, as plain data so the learning order is
// pinned without a server (test_w2_town.cpp). reqLevel is the row's required
// level, moneyCost the discounted price the visit pays.
struct TrainerVisitRank
{
    uint32_t spellId = 0;
    uint32_t reqLevel = 0;
    uint32_t moneyCost = 0;
};

// Learning order for several affordable ranks: lowest required level first
// (chain order: a rank that needs another lists the higher level), then
// cheapest, then spell id for ties. A purse that covers only some ranks then
// keeps the foundational ones, and a rank never jumps ahead of its own
// prerequisite mid-visit.
inline bool TrainerVisitRankLess(TrainerVisitRank const& a, TrainerVisitRank const& b)
{
    if (a.reqLevel != b.reqLevel)
        return a.reqLevel < b.reqLevel;
    if (a.moneyCost != b.moneyCost)
        return a.moneyCost < b.moneyCost;
    return a.spellId < b.spellId;
}

// Sorts the visit's ranks into learning order (above).
inline void SortTrainerVisitRanks(std::vector<TrainerVisitRank>& ranks)
{
    std::sort(ranks.begin(), ranks.end(), TrainerVisitRankLess);
}

} // namespace ai
