#include "../ai/playerbot/MainTankPolicy.h"
#include "../ai/playerbot/TankAddPickupPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::MainTankSticksToCurrent;
using ai::OffTankPeelsToAdds;
using ai::TankHoldsCurrentTarget;
using ai::TankPeelsToLooseAdd;

int main()
{
    std::cout << "Starting TortoiseBots tank add-pickup tests...\n";

    // (1) Holding follows the donor multi-tank rule: own mob is held, and —
    // anyone but the explicit main tank — so is a mob on another tank.
    {
        // Own mob: held, whoever the tank is.
        CHECK(TankHoldsCurrentTarget(true, false, false));
        CHECK(TankHoldsCurrentTarget(true, false, true));
        // Off-tank parked on the main tank's skull: still holding it.
        CHECK(TankHoldsCurrentTarget(false, true, false));
        // Explicit main tank: only its own mob counts.
        CHECK(!TankHoldsCurrentTarget(false, true, true));
        // Loose add on a healer: nobody holds it.
        CHECK(!TankHoldsCurrentTarget(false, false, false));
        CHECK(!TankHoldsCurrentTarget(false, false, true));
        std::cout << "  [PASS] off-tank on the MT skull counts as holding, MT needs its own mob\n";
    }

    // (2) Peel only while holding, and finish a held mob at LowHealth first.
    {
        // Not holding: no peel (stays on the skull, no ping-pong).
        CHECK(!TankPeelsToLooseAdd(false, 100.0f, 50.0f));
        // Holding, healthy: peel to the loose add.
        CHECK(TankPeelsToLooseAdd(true, 100.0f, 50.0f));
        CHECK(TankPeelsToLooseAdd(true, 51.0f, 50.0f));
        // Holding, at or below LowHealth: finish it first.
        CHECK(!TankPeelsToLooseAdd(true, 50.0f, 50.0f));
        CHECK(!TankPeelsToLooseAdd(true, 10.0f, 50.0f));
        std::cout << "  [PASS] peel while holding, finish a low held mob first\n";
    }

    // (3) Explicit main tank pins to its mob only with 2+ tanks; anyone else
    // peels. Explicit-only: no raid Main Tank flag, no pin, no split.
    {
        // Lone tank (or solo): normal tournament, no stick, no split.
        CHECK(!MainTankSticksToCurrent(true, 1));
        CHECK(!MainTankSticksToCurrent(true, 0));
        CHECK(!OffTankPeelsToAdds(true, 1));
        CHECK(!OffTankPeelsToAdds(true, 0));
        // Off-tank in a 2-tank group: peels to loose adds, never sticks.
        CHECK(!MainTankSticksToCurrent(false, 2));
        CHECK(OffTankPeelsToAdds(false, 2));
        CHECK(OffTankPeelsToAdds(false, 3));
        // Explicit main tank with an off-tank beside it: sticks, never peels.
        CHECK(MainTankSticksToCurrent(true, 2));
        CHECK(MainTankSticksToCurrent(true, 3));
        CHECK(!OffTankPeelsToAdds(true, 2));
        CHECK(!OffTankPeelsToAdds(true, 3));
        std::cout << "  [PASS] explicit MT sticks, off-tanks peel, lone tank unchanged\n";
    }

    std::cout << "All tank add-pickup checks PASSED!\n";
    return 0;
}
