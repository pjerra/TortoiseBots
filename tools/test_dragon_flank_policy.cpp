#include "../ai/playerbot/DragonFlankPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::IsInDragonCone;
using ai::IsRaidDragonEntry;
using ai::ShouldFlankDragon;

int main()
{
    std::cout << "Starting TortoiseBots dragon-flank policy tests...\n";

    const float pi = 3.14159265358979f;

    // Listed dragons only; whelps (11262) and drakes are not dragons.
    CHECK(IsRaidDragonEntry(10184));
    CHECK(IsRaidDragonEntry(11983));
    CHECK(IsRaidDragonEntry(60748));
    CHECK(!IsRaidDragonEntry(11262));
    CHECK(!IsRaidDragonEntry(0));
    std::cout << "  [PASS] dragon entry list\n";

    // Cone math: front 45-degree half-angle, rear 30-degree tail half-angle,
    // flanks clear. Wraps across 0 / 2PI.
    CHECK(IsInDragonCone(0.0f, 0.0f));
    CHECK(IsInDragonCone(0.0f, 0.7f));
    CHECK(IsInDragonCone(0.1f, 2.0f * pi - 0.3f));
    CHECK(IsInDragonCone(2.0f * pi - 0.1f, 0.3f));
    CHECK(!IsInDragonCone(0.0f, pi / 2.0f));
    CHECK(!IsInDragonCone(0.0f, 3.0f * pi / 2.0f));
    CHECK(IsInDragonCone(0.0f, pi));
    CHECK(IsInDragonCone(1.0f, 1.0f + pi - 0.4f));
    CHECK(!IsInDragonCone(1.0f, 1.0f + pi - 0.6f));
    std::cout << "  [PASS] breath and tail cone geometry\n";

    // Own target is the dragon (unchanged rule): non-tanks flank; the
    // victim tank and a tank with no victim hold; a spare tank flanks.
    CHECK(ShouldFlankDragon(false, true, true, false, false));
    CHECK(ShouldFlankDragon(false, true, false, false, true));
    CHECK(!ShouldFlankDragon(true, true, true, true, false));
    CHECK(!ShouldFlankDragon(true, true, false, false, false));
    CHECK(ShouldFlankDragon(true, true, true, false, false));
    std::cout << "  [PASS] own-target dragon keeps the tank rule\n";

    // Dragon decoupled from the bot's target (healer, whelp/add killer):
    // non-tanks flank while it is grounded and tanked; tanks on other
    // targets hold; nothing while it is airborne or has no victim.
    CHECK(ShouldFlankDragon(false, false, true, false, false));
    CHECK(ShouldFlankDragon(false, false, true, true, false));
    CHECK(!ShouldFlankDragon(false, false, true, false, true));
    CHECK(!ShouldFlankDragon(false, false, false, false, false));
    CHECK(!ShouldFlankDragon(true, false, true, false, false));
    std::cout << "  [PASS] healers and add killers flank the encounter dragon\n";

    std::cout << "TortoiseBots dragon-flank policy tests passed.\n";
    return 0;
}
