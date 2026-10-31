// Standalone check for ai/playerbot/policy/combat/RaidSnarePolicy.h:
//   g++ -std=c++17 -Wall -Wextra tools/test_raid_snare_policy.cpp -o /tmp/test_raid_snare && /tmp/test_raid_snare
#include "../ai/playerbot/policy/combat/RaidSnarePolicy.h"

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
    std::cout << "Starting TortoiseBots raid snare policy tests...\n";

    // Outside a raid group nothing changes: victim on a tank is still snared.
    CHECK(SnareAllowedInRaid(false, true, false));
    CHECK(SnareAllowedInRaid(false, false, false));
    // Fleeing/assist mobs are never a positioning problem: always allowed.
    CHECK(SnareAllowedInRaid(true, true, true));
    CHECK(SnareAllowedInRaid(true, false, true));
    // Raid group: only snare a mob attacking someone who is not a tank.
    CHECK(SnareAllowedInRaid(true, false, false));
    CHECK(!SnareAllowedInRaid(true, true, false));
    std::cout << "  [PASS] raid snares only non-tank victims\n";

    std::cout << "All TortoiseBots raid snare policy tests passed.\n";
    return 0;
}
