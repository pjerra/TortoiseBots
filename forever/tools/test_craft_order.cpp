// Pure unit tests for craft-on-order reagent resolution policy (TbHostCraftOrderPure.h).
// Build & run from repo root:
//   g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/test_craft_order.cpp -o /tmp/test_craft_order && /tmp/test_craft_order

#include "forever/src/host/TbHostCraftOrderPure.h"

#include <cassert>
#include <iostream>

int main()
{
    using namespace tbhost::craft_order;
    int passed = 0;

    // 1. All reagents present -> CraftTarget.
    {
        std::vector<ReagentRequirement> reqs = {
            { 2996, 2, 4, false, false, false }, // Bolt of Linen Cloth
            { 2320, 1, 2, false, false, false }, // Coarse Thread
        };
        CraftReagentDecision const d = DecideCraftReagentStep(reqs);
        assert(d.action == CraftReagentAction::CraftTarget);
        assert(d.reagentItemId == 0);
        ++passed;
    }

    // 2. Missing reagent craftable locally -> CraftSub (with missing count).
    {
        std::vector<ReagentRequirement> reqs = {
            { 2996, 3, 1, true, false, true }, // Bolt of Linen Cloth: need 3, have 1, canCraftSub
            { 2320, 1, 1, false, false, false },
        };
        CraftReagentDecision const d = DecideCraftReagentStep(reqs);
        assert(d.action == CraftReagentAction::CraftSub);
        assert(d.reagentItemId == 2996);
        assert(d.missingCount == 2);
        ++passed;
    }

    // 3. Missing reagent not craftable, gatherable -> Gather.
    {
        std::vector<ReagentRequirement> reqs = {
            { 2770, 2, 0, false, true, true }, // Copper Ore: gather preferred over AH
        };
        CraftReagentDecision const d = DecideCraftReagentStep(reqs);
        assert(d.action == CraftReagentAction::Gather);
        assert(d.reagentItemId == 2770);
        assert(d.missingCount == 2);
        ++passed;
    }

    // 4. Missing reagent only on AH -> BuyAh.
    {
        std::vector<ReagentRequirement> reqs = {
            { 2320, 2, 0, false, false, true }, // Coarse Thread on AH
        };
        CraftReagentDecision const d = DecideCraftReagentStep(reqs);
        assert(d.action == CraftReagentAction::BuyAh);
        assert(d.reagentItemId == 2320);
        assert(d.missingCount == 2);
        ++passed;
    }

    // 5. Missing reagent with no local source -> MissingReagents.
    {
        std::vector<ReagentRequirement> reqs = {
            { 2320, 2, 1, false, false, false },
        };
        CraftReagentDecision const d = DecideCraftReagentStep(reqs);
        assert(d.action == CraftReagentAction::MissingReagents);
        assert(d.reagentItemId == 2320);
        assert(d.missingCount == 1);
        ++passed;
    }

    std::cout << "PASS: test_craft_order (" << passed << " checks, 0 failed)\n";
    return 0;
}
