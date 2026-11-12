#include "../ai/playerbot/policy/combat/MealGatePolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using namespace ai;

int main()
{
    std::cout << "Starting TortoiseBots meal gate policy tests...\n";

    // The named failing case: the run is fighting (its tick marked this bot),
    // the bot stands 80 yd from the fight with no attackers and no member with
    // a victim inside 60 yd. Today the meal starts and puts the AI to sleep.
    CHECK(MealBlocked(false, false, 12000, 10000));
    std::cout << "  [PASS] run fight holds: meal blocked far from the fight\n";

    // A mark never set means no run fight.
    CHECK(!MealBlocked(false, false, 0, 10000));
    std::cout << "  [PASS] never set: meal allowed\n";

    // Expiry at the boundary: the hold is over the moment now reaches it.
    CHECK(!MealBlocked(false, false, 10000, 10000));
    std::cout << "  [PASS] expired at the boundary: meal allowed\n";

    CHECK(!MealBlocked(false, false, 9000, 10000));
    std::cout << "  [PASS] expired before: meal allowed\n";

    // The timer wraps; the subtraction stays wrap-safe.
    CHECK(RunFightHolds(5, 0xFFFFFFF0u));
    CHECK(!RunFightHolds(0xFFFFFFF0u, 5));
    std::cout << "  [PASS] hold is wrap-safe across the timer wrap\n";

    // The hold is at most RUN_FIGHT_HOLD_MS: a stale or corrupted mark far
    // ahead, even half a timer away, cannot keep the meal held.
    CHECK(!RunFightHolds(20000, 10000));
    CHECK(!RunFightHolds(10000, 10000 + 0x80000000u + 5000));
    CHECK(RunFightHolds(10000 + RUN_FIGHT_HOLD_MS, 10000));
    CHECK(!RunFightHolds(10000 + RUN_FIGHT_HOLD_MS + 1, 10000));
    std::cout << "  [PASS] hold is bounded to one run tick\n";

    // The pre-existing gates keep refusing on their own.
    CHECK(MealBlocked(true, false, 0, 10000));
    CHECK(MealBlocked(false, true, 0, 10000));
    std::cout << "  [PASS] attackers alone and a near fighting member alone still block\n";

    // A live mark holds even when both old gates are clear.
    CHECK(MealBlocked(false, false, 12001, 10001));
    std::cout << "  [PASS] live mark holds until its expiry\n";

    // The named failing case (paladin-addpickup rep3): damage stood the bot up
    // and it is in combat, but the drink's sleep still holds the AI for 13 s.
    // The old wake check needs a seated bot with the aura and never fires.
    CHECK(MealSleepShouldWake(true, /*seated*/false, false, /*selfInCombat*/true, false, false));
    std::cout << "  [PASS] stood up in combat: the meal sleep wakes\n";

    // A seated bot with its aura and nobody fighting keeps its meal.
    CHECK(!MealSleepShouldWake(true, true, true, false, false, false));
    std::cout << "  [PASS] seated, aura on, nobody fighting: meal continues\n";

    // Any one broken term wakes it.
    CHECK(MealSleepShouldWake(true, false, true, false, false, false));
    CHECK(MealSleepShouldWake(true, true, false, false, false, false));
    CHECK(MealSleepShouldWake(true, true, true, true, false, false));
    CHECK(MealSleepShouldWake(true, true, true, false, true, false));
    CHECK(MealSleepShouldWake(true, true, true, false, false, true));
    std::cout << "  [PASS] each broken term wakes the meal sleep\n";

    // No meal sleep, no wake: an unrelated delay is never cut short.
    CHECK(!MealSleepShouldWake(false, false, false, true, true, true));
    CHECK(!MealSleepShouldWake(false, true, true, false, false, false));
    std::cout << "  [PASS] no meal sleep never wakes\n";

    std::cout << "All meal gate policy tests passed.\n";
    return 0;
}
