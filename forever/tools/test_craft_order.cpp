// Pure unit tests for craft-on-order reagent resolution policy (TbHostCraftOrderPure.h).
// Build & run from repo root:
//   g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/test_craft_order.cpp -o /tmp/test_craft_order && /tmp/test_craft_order

#include "forever/src/host/TbHostCraftOrderPure.h"

#include <cassert>
#include <iostream>
#include <string>

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

    // 6. Craft AH budget respects goldCap.
    {
        CraftAhBudget const uncapped = ResolveCraftAhBudget(false, 0);
        assert(uncapped.canBuy && uncapped.maxCopper == 0);

        CraftAhBudget const cappedZero = ResolveCraftAhBudget(true, 0);
        assert(!cappedZero.canBuy && cappedZero.maxCopper == 0);

        CraftAhBudget const cappedSome = ResolveCraftAhBudget(true, 5000);
        assert(cappedSome.canBuy && cappedSome.maxCopper == 5000);
        ++passed;
    }

    // 7. Pending sub-craft/gather verification produces exact gain/no-gain reasons.
    {
        PendingStepOutcome const craftOk = EvaluatePendingStep(1, 2, true);
        assert(craftOk.ok && std::string(craftOk.reason) == "crafted");
        PendingStepOutcome const craftFail = EvaluatePendingStep(1, 1, true);
        assert(!craftFail.ok && std::string(craftFail.reason) == "craft-no-gain");

        PendingStepOutcome const gatherOk = EvaluatePendingStep(0, 1, false);
        assert(gatherOk.ok && std::string(gatherOk.reason) == "gathered");
        PendingStepOutcome const gatherFail = EvaluatePendingStep(2, 2, false);
        assert(!gatherFail.ok && std::string(gatherFail.reason) == "gather-no-item");
        ++passed;
    }

    // 8. Node loot matching checks direct items, reference loot, missing items, and cycles.
    {
        std::unordered_map<uint32_t, LootTemplateItems> goLoot;
        std::unordered_map<uint32_t, LootTemplateItems> refLoot;
        goLoot[10] = { { 2770 }, { 500 } };      // Copper Vein -> Copper Ore + ref 500
        refLoot[500] = { { 2771 }, { 501 } };     // ref 500 -> Tin Ore + ref 501
        refLoot[501] = { {}, { 500 } };           // cycle back to 500

        assert(LootContainsItem(goLoot, refLoot, 10, 2770));
        assert(LootContainsItem(goLoot, refLoot, 10, 2771));
        assert(!LootContainsItem(goLoot, refLoot, 10, 2320)); // Coarse Thread not in Copper Vein
        assert(!LootContainsItem(goLoot, refLoot, 99, 2770)); // unknown lootId
        ++passed;
    }

    std::cout << "PASS: test_craft_order (" << passed << " checks, 0 failed)\n";
    return 0;
}
