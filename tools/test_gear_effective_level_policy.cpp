// Standalone test for the effective gear level requirement: the stored
// RequiredLevel wins untouched, and a stored 0 is derived as item level - 5,
// clamped to the realm's level cap. On the live Forever data 91% of the rows
// that do store a requirement match that rule, and the endgame rows that store
// 0 (item level 71-90 epics) are wearable at 60.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_gear_effective_level_policy.cpp -o /tmp/test_gear_effective_level_policy
//   /tmp/test_gear_effective_level_policy

#include "../runtime/GearEffectiveLevelPolicy.h"

#include <cstdio>
#include <cstdlib>

using namespace TortoiseBots;

static int checks = 0;
#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        std::exit(1); \
    } \
} while (0)

int main()
{
    // A stored requirement is what the core checks, so it passes through
    // unchanged, including values above the cap (custom PvP rows store 90).
    CHECK(GearEffectiveRequiredLevel(1, 62) == 1);
    CHECK(GearEffectiveRequiredLevel(5, 5) == 5);
    CHECK(GearEffectiveRequiredLevel(40, 60) == 40);
    CHECK(GearEffectiveRequiredLevel(60, 298) == 60);
    CHECK(GearEffectiveRequiredLevel(90, 298) == 90);

    // Stored 0: item level - 5, never below 1 (starter and junk rows).
    CHECK(GearEffectiveRequiredLevel(0, 0) == 1);
    CHECK(GearEffectiveRequiredLevel(0, 1) == 1);
    CHECK(GearEffectiveRequiredLevel(0, 5) == 1);
    CHECK(GearEffectiveRequiredLevel(0, 6) == 1);
    CHECK(GearEffectiveRequiredLevel(0, 7) == 2);
    CHECK(GearEffectiveRequiredLevel(0, 10) == 5);
    CHECK(GearEffectiveRequiredLevel(0, 25) == 20);
    CHECK(GearEffectiveRequiredLevel(0, 62) == 57);
    CHECK(GearEffectiveRequiredLevel(0, 65) == 60);

    // At and above the level cap the derived requirement stays 60: Naxx/AQ
    // epics at item level 80-90 store 60, not 75-85.
    CHECK(GearEffectiveRequiredLevel(0, 66) == kGearMaxRequiredLevel);
    CHECK(GearEffectiveRequiredLevel(0, 80) == kGearMaxRequiredLevel);
    CHECK(GearEffectiveRequiredLevel(0, 90) == kGearMaxRequiredLevel);
    CHECK(GearEffectiveRequiredLevel(0, 298) == kGearMaxRequiredLevel);
    CHECK(kGearMaxRequiredLevel == 60);

    // Non-decreasing in item level, and never above the cap once derived.
    std::uint32_t previous = 0;
    for (std::uint32_t itemLevel = 0; itemLevel <= 300; ++itemLevel)
    {
        std::uint32_t const effective = GearEffectiveRequiredLevel(0, itemLevel);
        CHECK(effective >= previous);
        CHECK(effective >= 1);
        CHECK(effective <= kGearMaxRequiredLevel);
        previous = effective;
    }

    // The owner's reported leak: a level-10 bot must not wear an item level 62
    // blue (57 > 10), while a level-60 bot may (57 <= 60).
    CHECK(GearEffectiveRequiredLevel(0, 62) > 10);
    CHECK(GearEffectiveRequiredLevel(0, 62) <= 60);

    std::printf("test_gear_effective_level_policy: %d checks passed\n", checks);
    return 0;
}
