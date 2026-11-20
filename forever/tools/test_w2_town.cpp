// Pure unit tests for W2 town errands (vendor, repair, trainer).
// Build & run from repo root:
//   g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/test_w2_town.cpp -o /tmp/test_w2_town && /tmp/test_w2_town

#include "ai/playerbot/policy/town/TownErrandPolicy.h"

#include <cassert>
#include <iostream>
#include <string>

int main()
{
    int passed = 0;

    // 1. Failing case: PlayerConditionId != 0 and unmet -> MUST be refused with reason.
    {
        std::string reason;
        bool const allowed = ai::VendorItemPickAllowed(
            /*playerConditionId=*/1234,
            /*meetsPlayerCondition=*/false,
            /*extendedCost=*/0,
            /*meetsVendorCondition=*/true,
            /*reputationOk=*/true,
            /*maxCount=*/0,
            /*currentCount=*/1,
            &reason);
        assert(!allowed);
        assert(reason == "player-condition:1234");
        ++passed;
    }

    // 2. Failing case: ExtendedCost != 0 -> MUST be refused with reason.
    {
        std::string reason;
        bool const allowed = ai::VendorItemPickAllowed(
            /*playerConditionId=*/0,
            /*meetsPlayerCondition=*/true,
            /*extendedCost=*/42,
            /*meetsVendorCondition=*/true,
            /*reputationOk=*/true,
            /*maxCount=*/0,
            /*currentCount=*/1,
            &reason);
        assert(!allowed);
        assert(reason == "extended-cost:42");
        ++passed;
    }

    // 3. Normal vendor item passes.
    {
        std::string reason;
        bool const allowed = ai::VendorItemPickAllowed(0, true, 0, true, true, 0, 1, &reason);
        assert(allowed);
        assert(reason.empty());
        ++passed;
    }

    // 4. Sell list filter: greys only, never quest items, never soulbound/equipped, never zero price.
    {
        assert(ai::ShouldSellItemW2(0, 0, false, false, 15));    // grey junk -> sell
        assert(!ai::ShouldSellItemW2(1, 0, false, false, 15));   // white -> keep
        assert(!ai::ShouldSellItemW2(2, 0, false, false, 15));   // green -> keep
        assert(!ai::ShouldSellItemW2(0, 12, false, false, 15));  // quest item -> never sell
        assert(!ai::ShouldSellItemW2(0, 0, true, false, 15));    // soulbound -> never sell
        assert(!ai::ShouldSellItemW2(0, 0, false, true, 15));    // equipped -> never sell
        assert(!ai::ShouldSellItemW2(0, 0, false, false, 0));    // no sell price -> skip
        ++passed;
    }

    // 5. Repair threshold: autonomous (<30% avg or <10% lowest) vs ordered (<100%).
    {
        assert(!ai::ShouldRepairW2(80, 50, false));
        assert(ai::ShouldRepairW2(29, 50, false));
        assert(ai::ShouldRepairW2(80, 9, false));
        assert(ai::ShouldRepairW2(80, 80, true));
        assert(!ai::ShouldRepairW2(100, 100, true));
        ++passed;
    }

    // 6. Trainer rank due & affordable (including reputation discount).
    {
        std::string reason;
        assert(!ai::TrainerRankDueAndAffordable(true, 10, 4, true, true, 100, 1.0f, 500, &reason));
        assert(reason == "known");

        assert(!ai::TrainerRankDueAndAffordable(false, 3, 4, true, true, 100, 1.0f, 500, &reason));
        assert(reason == "level");

        assert(!ai::TrainerRankDueAndAffordable(false, 10, 4, true, true, 100, 1.0f, 99, &reason));
        assert(reason == "money");

        // 10% rep discount: 100 * 0.9 = 90 copper -> affordable with 90 copper.
        assert(ai::TrainerRankDueAndAffordable(false, 10, 4, true, true, 100, 0.9f, 90, &reason));
        ++passed;
    }

    // 7. Supplies by level and class (matching forever/data/waterys/supplies-by-level.csv).
    {
        ai::SupplyTargetW2 warrior1 = ai::SupplyTargetForLevelAndClass(1, 1);
        assert(warrior1.needsFood && !warrior1.needsWater && !warrior1.needsAmmo);
        assert(warrior1.foodItemId == 117);

        ai::SupplyTargetW2 mage10 = ai::SupplyTargetForLevelAndClass(10, 8);
        assert(mage10.needsFood && mage10.needsWater && !mage10.needsAmmo);
        assert(mage10.foodItemId == 414 && mage10.waterItemId == 1179);

        ai::SupplyTargetW2 hunter20 = ai::SupplyTargetForLevelAndClass(20, 3);
        assert(hunter20.needsFood && hunter20.needsWater && hunter20.needsAmmo);
        assert(hunter20.ammoItemId == 3030 && hunter20.targetStacks == 2);
        ++passed;
    }

    std::cout << "PASS: test_w2_town (" << passed << " checks, 0 failed)\n";
    return 0;
}
