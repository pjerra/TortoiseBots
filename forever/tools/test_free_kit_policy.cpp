// Standalone test of W1's free-bot kit (ai/playerbot/policy/world/FreeKitPolicy.h):
//   g++ -std=c++17 -O1 -Wall -Wextra -If. forever/tools/test_free_kit_policy.cpp -o /tmp/test_free_kit_policy && /tmp/test_free_kit_policy
//
// The kit is a decision (spec D4): the class spec strategy plus grind, food, loot, roll, default and buff, and nothing else. A
// missing strategy here means the bot does less; an extra one (follow, wander, rpg, travel) would make it follow or journey.
#include "ai/playerbot/policy/world/FreeKitPolicy.h"

#include <cstdio>
#include <cstring>
#include <initializer_list>

namespace w = TortoiseBots::world;

static int g_checks = 0, g_fail = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

int main()
{
    CHECK(w::ClassWarrior == 1 && w::ClassPaladin == 2 && w::ClassHunter == 3 && w::ClassRogue == 4);
    CHECK(w::ClassPriest == 5 && w::ClassShaman == 7 && w::ClassMage == 8 && w::ClassWarlock == 9 && w::ClassDruid == 11);

    // Every class the slice knows gets its own spec; an unknown class falls back to the warrior's arms (the slice gives it the
    // warrior context too).
    CHECK(std::strcmp(w::FreeSpecStrategy(w::ClassWarrior), "arms") == 0);
    CHECK(std::strcmp(w::FreeSpecStrategy(w::ClassPriest), "discipline") == 0);
    CHECK(std::strcmp(w::FreeSpecStrategy(w::ClassMage), "fire") == 0);
    CHECK(std::strcmp(w::FreeSpecStrategy(w::ClassRogue), "combat") == 0);
    CHECK(std::strcmp(w::FreeSpecStrategy(w::ClassHunter), "marksmanship") == 0);
    CHECK(std::strcmp(w::FreeSpecStrategy(w::ClassWarlock), "affliction") == 0);
    CHECK(std::strcmp(w::FreeSpecStrategy(w::ClassDruid), "restoration") == 0);
    CHECK(std::strcmp(w::FreeSpecStrategy(w::ClassPaladin), "holy") == 0);
    CHECK(std::strcmp(w::FreeSpecStrategy(w::ClassShaman), "restoration") == 0);
    CHECK(std::strcmp(w::FreeSpecStrategy(0), "arms") == 0);

    // The world kit, exactly, in order.
    char const* const* kit = w::FreeWorldKit();
    char const* const expected[] = { "grind", "food", "loot", "roll", "default", "buff", nullptr };
    for (int i = 0; expected[i] || kit[i]; ++i)
        CHECK(kit[i] && expected[i] && std::strcmp(kit[i], expected[i]) == 0);
    for (int i = 0; kit[i]; ++i)
        for (char const* banned : { "follow", "wander", "rpg", "travel", "tfish", "maintenance", "guild", "group", "collision" })
            CHECK(std::strcmp(kit[i], banned) != 0);

    if (g_fail)
    {
        std::printf("RESULT: FAIL (%d of %d checks)\n", g_fail, g_checks);
        return 1;
    }
    std::printf("RESULT: PASS (%d checks)\n", g_checks);
    return 0;
}
