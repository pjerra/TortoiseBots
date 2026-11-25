// Standalone policy check for the one-time spawn kit's quality mix (runtime/GearSeedMixPolicy.h).
// Build: g++ -std=c++17 -Wall -Wextra -I. tools/test_gear_seed_mix_policy.cpp -o /tmp/t
#include "../runtime/GearSeedMixPolicy.h"

#include <cstdio>
#include <cstdlib>
#include <set>

using namespace TortoiseBots;

static int checks = 0;
#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        std::exit(1); \
    } \
} while (0)

// Deterministic "random" source: cycles 0, 1, 2, ... modulo the bound.
struct CycleRandom
{
    std::uint32_t next = 0;
    std::uint32_t operator()(std::uint32_t bound) { return next++ % bound; }
};

static void CheckBand(std::uint32_t level, std::uint8_t base, std::uint32_t minUpgraded, std::uint32_t maxUpgraded)
{
    GearSeedQualityPlan const plan = SeedQualityPlan(level);
    CHECK(plan.baseQuality == base);
    CHECK(plan.upgradedQuality == base + 1);
    CHECK(plan.minUpgradedSlots == minUpgraded);
    CHECK(plan.maxUpgradedSlots == maxUpgraded);

    // Every roll in the band is reachable, and nothing outside it ever is.
    std::set<std::uint32_t> rolled;
    std::uint32_t const span = maxUpgraded - minUpgraded + 1;
    for (std::uint32_t offset = 0; offset < span; ++offset)
    {
        CycleRandom random;
        random.next = offset;
        std::uint32_t const count = SeedUpgradedSlotCount(plan, random);
        CHECK(count >= minUpgraded);
        CHECK(count <= maxUpgraded);
        rolled.insert(count);
    }
    CHECK(rolled.size() == span);
}

int main()
{
    // Below 5 the starter outfit stays and there is no mix at all.
    for (std::uint32_t level = 1; level < kSpawnSeedMinLevel; ++level)
    {
        CHECK(!SpawnSeedReady(level));
        GearSeedQualityPlan const plan = SeedQualityPlan(level);
        CHECK(plan.minUpgradedSlots == 0);
        CHECK(plan.maxUpgradedSlots == 0);
        CycleRandom random;
        CHECK(SeedUpgradedSlotCount(plan, random) == 0);
    }

    // The owner's bands (contract rule 4). The floor is GearSeedQualityPolicy's: white below 20, green from 20.
    CheckBand(5, 1, 0, 1);
    CheckBand(9, 1, 0, 1);
    CheckBand(10, 1, 1, 2);
    CheckBand(14, 1, 1, 2);
    CheckBand(15, 1, 2, 3);
    CheckBand(19, 1, 2, 3);
    CheckBand(20, 2, 0, 2);
    CheckBand(29, 2, 0, 2);
    CheckBand(30, 2, 3, 5);
    CheckBand(49, 2, 3, 5);
    CheckBand(50, 2, 6, 9);
    CheckBand(60, 2, 6, 9);

    // No epics, ever.
    for (std::uint32_t level = kSpawnSeedMinLevel; level <= 60; ++level)
        CHECK(SeedQualityPlan(level).upgradedQuality <= 3);

    // Upgraded slots come from the eligible set only, are distinct, and the count is clamped to it.
    CycleRandom random;
    std::vector<std::uint32_t> const chosen = SeedChooseUpgradedSlots({ 0, 1, 2, 3, 4 }, 3, random);
    CHECK(chosen.size() == 3);
    std::set<std::uint32_t> distinct(chosen.begin(), chosen.end());
    CHECK(distinct.size() == 3);
    for (std::uint32_t slot : chosen)
        CHECK(slot <= 4);

    CycleRandom smallRandom;
    std::vector<std::uint32_t> const clamped = SeedChooseUpgradedSlots({ 12, 13 }, 9, smallRandom);
    CHECK(clamped.size() == 2);
    for (std::uint32_t slot : clamped)
        CHECK(slot == 12 || slot == 13);

    CHECK(SeedChooseUpgradedSlots({}, 2, CycleRandom()).empty());
    CHECK(SeedChooseUpgradedSlots({ 0, 1, 2 }, 0, CycleRandom()).empty());

    // Different rolls spread the same count over different eligible slots.
    bool varied = false;
    std::vector<std::uint32_t> const first = SeedChooseUpgradedSlots({ 0, 1, 2, 3 }, 2, CycleRandom());
    for (std::uint32_t offset = 1; offset < 4 && !varied; ++offset)
    {
        CycleRandom roll;
        roll.next = offset;
        std::vector<std::uint32_t> const other = SeedChooseUpgradedSlots({ 0, 1, 2, 3 }, 2, roll);
        varied = other != first;
    }
    CHECK(varied);

    // A slot picks among its five best candidates, or everything within 85% of the best score, whichever is wider.
    CHECK(SeedCandidatePickCount({}) == 0);
    CHECK(SeedCandidatePickCount({ 10 }) == 1);
    CHECK(SeedCandidatePickCount({ 10, 9, 8, 7 }) == 4);
    // Nine evenly scoring candidates: all are within 85% of the best, so the whole list is in play.
    CHECK(SeedCandidatePickCount({ 100, 99, 98, 97, 96, 95, 94, 93, 92 }) == 9);
    // A long tail within 85% widens the window past five.
    CHECK(SeedCandidatePickCount({ 100, 99, 98, 97, 96, 95, 94, 93, 92, 91, 90, 85 }) == 12);
    // Below 85% of the best the window stops.
    CHECK(SeedCandidatePickCount({ 100, 100, 100, 100, 100, 84, 83 }) == 5);
    // Score zero: every candidate ties, so the whole list is in play (the low-level white pool).
    CHECK(SeedCandidatePickCount({ 0, 0, 0, 0, 0, 0, 0 }) == 7);

    // A slot at the green floor may take white; below 20 the floor is white and an empty slot stays empty.
    CHECK(SeedFallbackQuality(2) == 1);
    CHECK(SeedFallbackQuality(1) == 1);
    CHECK(SeedFallbackQuality(0) == 0);

    std::printf("PASS: test_gear_seed_mix_policy (%d checks, 0 failed)\n", checks);
    return 0;
}
