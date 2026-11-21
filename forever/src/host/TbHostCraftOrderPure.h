#pragma once

#include <cstdint>
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

} // namespace craft_order
} // namespace tbhost
