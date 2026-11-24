// Standalone test of the module's creature combat clock (forever/src/compat/TbCombatClockPure.h), the core-free half of
// CombatTimeCompat on ForeverCore (TBF #208: the constant -1 kept the cc mark's fight-start hold window closed, so the party
// fought an ordered mark from the first second and broke every Polymorph):
//   g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/test_combat_clock.cpp -o /tmp/test_combat_clock && /tmp/test_combat_clock
#include "forever/src/compat/TbCombatClockPure.h"
#include "ai/playerbot/strategy/values/CcMarkHoldPolicy.h"

#include <cstdio>

static int checks = 0;
static int failures = 0;
#define CHECK(cond) do { ++checks; if (!(cond)) { ++failures; std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); } } while (0)

int main()
{
    using TortoiseBots::CombatClock;
    CombatClock clock;

    // Out of combat: unknown, nothing kept.
    CHECK(clock.Observe(7, false, 100) == -1);
    CHECK(clock.Size() == 0);

    // First sight in combat starts the clock; later questions count from it.
    CHECK(clock.Observe(7, true, 100) == 0);
    CHECK(clock.Observe(7, true, 104) == 4);
    CHECK(clock.Observe(7, true, 111) == 11);
    // A clock that went backwards never answers a negative age.
    CHECK(clock.Observe(7, true, 99) == 0);

    // Two creatures, two clocks.
    CHECK(clock.Observe(8, true, 105) == 0);
    CHECK(clock.Observe(8, true, 106) == 1);
    CHECK(clock.Observe(7, true, 106) == 6);

    // Leaving combat (evade, death, reset) drops the clock: the next fight starts at 0.
    CHECK(clock.Observe(7, false, 120) == -1);
    CHECK(clock.Observe(7, true, 130) == 0);

    // A creature that despawned in combat is never asked again: stale clocks go before the table grows past its cap.
    CombatClock big;
    for (std::uint64_t k = 0; k < CombatClock::kPruneAbove; ++k)
        big.Observe(k, true, 0);
    CHECK(big.Size() == CombatClock::kPruneAbove);
    CHECK(big.Observe(CombatClock::kPruneAbove + 1, true, CombatClock::kStaleSeconds + 1) == 0);
    CHECK(big.Size() == 1);

    // The reason it exists: a ready cc bot's mark is held at the start of its fight only with a known engage time.
    ai::CcMarkHoldState state;
    state.ccBotReady = true;
    CombatClock fight;
    state.combatSeconds = -1;                       // the old CombatTimeCompat answer
    CHECK(!ai::CcMarkHeld(state));
    state.combatSeconds = fight.Observe(42, true, 1000);
    CHECK(ai::CcMarkHeld(state));                   // pull: held
    state.combatSeconds = fight.Observe(42, true, 1000 + ai::CcMarkHoldSeconds);
    CHECK(ai::CcMarkHeld(state));                   // last second of the window
    state.combatSeconds = fight.Observe(42, true, 1000 + ai::CcMarkHoldSeconds + 1);
    CHECK(!ai::CcMarkHeld(state));                  // a free mark past the window is a normal enemy again

    std::printf("combat clock: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
