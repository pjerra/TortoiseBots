// Standalone policy check for the one-time spawn kit's quality mix (runtime/GearSeedMixPolicy.h).
// Build: g++ -std=c++17 -Wall -Wextra -I. tools/test_gear_seed_mix_policy.cpp -o /tmp/t
#include "../runtime/GearSeedMixPolicy.h"

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

static void CheckBand(uint32_t level, uint8_t base, uint32_t upgradedSlots)
{
    GearSeedQualityPlan const plan = SeedQualityPlan(level);
    CHECK(plan.baseQuality == base);
    CHECK(plan.upgradedQuality == base + 1);
    CHECK(plan.upgradedSlots == upgradedSlots);

    // Every rotation puts exactly `upgradedSlots` slots on the upgraded cap, and none above it.
    for (uint32_t offset = 0; offset < kSpawnKitSlots; ++offset)
    {
        uint32_t upgraded = 0;
        for (uint32_t slot = 0; slot < kSpawnKitSlots; ++slot)
        {
            uint8_t const cap = SeedSlotQualityCap(level, slot, kSpawnKitSlots, offset);
            CHECK(cap == base || cap == base + 1);
            if (cap == base + 1)
                ++upgraded;
        }
        CHECK(upgraded == upgradedSlots);
    }
}

int main()
{
    // Below 5 the starter outfit stays and there is no mix at all.
    for (uint32_t level = 1; level < kSpawnSeedMinLevel; ++level)
    {
        CHECK(!SpawnSeedReady(level));
        CHECK(SeedQualityPlan(level).upgradedSlots == 0);
        CHECK(SeedSlotQualityCap(level, 0, kSpawnKitSlots, 0) == 0);
    }

    // The owner's bands (contract rule 4). The floor is GearSeedQualityPolicy's: white below 20, green from 20.
    CheckBand(5, 1, 1);
    CheckBand(9, 1, 1);
    CheckBand(10, 1, 3);
    CheckBand(19, 1, 3);
    CheckBand(20, 2, 2);
    CheckBand(29, 2, 2);
    CheckBand(30, 2, 5);
    CheckBand(49, 2, 5);
    CheckBand(50, 2, 9);
    CheckBand(60, 2, 9);

    // No epics, ever.
    for (uint32_t level = kSpawnSeedMinLevel; level <= 60; ++level)
        CHECK(SeedQualityPlan(level).upgradedQuality <= 3);

    // The rotation actually moves the upgraded run: offset 0 and offset 1 disagree on one slot.
    CHECK(SeedSlotQualityCap(20, 0, kSpawnKitSlots, 0) == 3);
    CHECK(SeedSlotQualityCap(20, 1, kSpawnKitSlots, 0) == 3);
    CHECK(SeedSlotQualityCap(20, 2, kSpawnKitSlots, 0) == 2);
    CHECK(SeedSlotQualityCap(20, 0, kSpawnKitSlots, 1) == 3);
    CHECK(SeedSlotQualityCap(20, 1, kSpawnKitSlots, 1) == 2);
    CHECK(SeedSlotQualityCap(20, 16, kSpawnKitSlots, 1) == 3);

    // Degenerate counts are total, not a crash.
    CHECK(!SeedSlotUpgraded(0, 0, 3, 0));
    CHECK(SeedSlotUpgraded(0, 5, 9, 0));
    CHECK(SeedSlotUpgraded(0, 5, 0, 0) == false);

    std::printf("PASS: test_gear_seed_mix_policy (%d checks, 0 failed)\n", checks);
    return 0;
}
