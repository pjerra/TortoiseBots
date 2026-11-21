#pragma once

#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace tbhost
{
namespace craft_order
{

enum class CraftReagentAction : uint8_t
{
    CraftTarget = 0,
    CraftSub = 1,
    Gather = 2,
    BuyAh = 3,
    MissingReagents = 4,
};

struct ReagentRequirement
{
    uint32_t itemId = 0;
    uint32_t needPerCraft = 0;
    uint32_t inBags = 0;
    bool canCraftSub = false;
    bool canGather = false;
    bool canBuyAh = false;
};

struct CraftReagentDecision
{
    CraftReagentAction action = CraftReagentAction::MissingReagents;
    uint32_t reagentItemId = 0;
    uint32_t missingCount = 0;
};

// Decide the next step for a craft order given the target recipe's reagents:
// craft target if all reagents are in bags, else acquire the first missing
// reagent via sub-craft -> local gather -> AH buy.
inline CraftReagentDecision DecideCraftReagentStep(std::vector<ReagentRequirement> const& reagents)
{
    for (ReagentRequirement const& r : reagents)
    {
        if (r.needPerCraft == 0 || r.inBags >= r.needPerCraft)
            continue;
        uint32_t const missing = r.needPerCraft - r.inBags;
        if (r.canCraftSub)
            return { CraftReagentAction::CraftSub, r.itemId, missing };
        if (r.canGather)
            return { CraftReagentAction::Gather, r.itemId, missing };
        if (r.canBuyAh)
            return { CraftReagentAction::BuyAh, r.itemId, missing };
        return { CraftReagentAction::MissingReagents, r.itemId, missing };
    }
    return { CraftReagentAction::CraftTarget, 0, 0 };
}

struct CraftAhBudget
{
    bool canBuy = false;
    uint32_t maxCopper = 0;
};

inline CraftAhBudget ResolveCraftAhBudget(bool hasGoldCap, uint32_t remainingCopper)
{
    if (hasGoldCap && remainingCopper == 0)
        return { false, 0 };
    return { true, hasGoldCap ? remainingCopper : 0 };
}

struct PendingStepOutcome
{
    bool ok = false;
    char const* reason = "";
};

inline PendingStepOutcome EvaluatePendingStep(uint32_t beforeCount, uint32_t afterCount, bool isCraft)
{
    if (afterCount > beforeCount)
        return { true, isCraft ? "crafted" : "gathered" };
    return { false, isCraft ? "craft-no-gain" : "gather-no-item" };
}

struct LootTemplateItems
{
    std::vector<uint32_t> items;
    std::vector<uint32_t> refIds;
};

inline bool RefLootContainsItem(
    std::unordered_map<uint32_t, LootTemplateItems> const& refLoot,
    uint32_t refId,
    uint32_t targetItemId,
    int depth,
    std::vector<uint32_t>& visited)
{
    if (depth <= 0 || !refId || !targetItemId)
        return false;
    if (std::find(visited.begin(), visited.end(), refId) != visited.end())
        return false;
    visited.push_back(refId);

    auto it = refLoot.find(refId);
    if (it == refLoot.end())
        return false;
    if (std::find(it->second.items.begin(), it->second.items.end(), targetItemId) != it->second.items.end())
        return true;
    for (uint32_t subRef : it->second.refIds)
    {
        if (RefLootContainsItem(refLoot, subRef, targetItemId, depth - 1, visited))
            return true;
    }
    return false;
}

inline bool LootContainsItem(
    std::unordered_map<uint32_t, LootTemplateItems> const& goLoot,
    std::unordered_map<uint32_t, LootTemplateItems> const& refLoot,
    uint32_t lootId,
    uint32_t targetItemId)
{
    if (!lootId || !targetItemId)
        return false;
    auto it = goLoot.find(lootId);
    if (it == goLoot.end())
        return false;
    if (std::find(it->second.items.begin(), it->second.items.end(), targetItemId) != it->second.items.end())
        return true;
    std::vector<uint32_t> visited;
    for (uint32_t refId : it->second.refIds)
    {
        if (RefLootContainsItem(refLoot, refId, targetItemId, 4, visited))
            return true;
    }
    return false;
}

} // namespace craft_order
} // namespace tbhost
