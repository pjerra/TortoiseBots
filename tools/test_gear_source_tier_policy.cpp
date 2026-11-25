// Standalone regression test for the generated-gear source gate: only items
// with a known ordinary source inside the configured tier, and without
// reputation/PvP requirements, may be handed out. Guards
//  - the tier cap (base / end-game dungeon / raid, TB's meaning),
//  - reputation and PvP rejection and their explicit opt-ins,
//  - unknown sources failing closed (custom/unobtainable rows),
//  - the owner blacklist winning over the whitelist,
//  - retired names, item level 0 (outside shirt/tabard) and legendaries,
//  - the whitelist bypassing the "gear players cannot really have" rules but
//    never the tier, reputation or PvP rules.
//
// Build and run (from the repo root):
//   g++ -std=c++17 -Wall -Wextra tools/test_gear_source_tier_policy.cpp -o /tmp/test_gear_source_tier
//   /tmp/test_gear_source_tier

#include "../runtime/policy/provision/GearSourceTierPolicy.h"

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

static GearSourceFacts VendorItem()
{
    GearSourceFacts facts;
    facts.knownSource = true;
    facts.tier = GEAR_SOURCE_TIER_BASE;
    facts.itemLevel = 20;
    facts.quality = 2;
    return facts;
}

static void TestTierCap()
{
    GearSourceFacts base = VendorItem();
    CHECK(GearSourceGateAllows(base, 0, false, false));
    CHECK(GearSourceGateAllows(base, -1, false, false)); // a negative cap clamps to 0

    GearSourceFacts dungeon = VendorItem();
    dungeon.tier = GEAR_SOURCE_TIER_DUNGEON;
    CHECK(!GearSourceGateAllows(dungeon, 0, false, false));
    CHECK(GearSourceGateAllows(dungeon, 1, false, false));
    CHECK(GearSourceGateAllows(dungeon, 2, false, false));

    GearSourceFacts raid = VendorItem();
    raid.tier = GEAR_SOURCE_TIER_RAID;
    CHECK(!GearSourceGateAllows(raid, 0, false, false));
    CHECK(!GearSourceGateAllows(raid, 1, false, false));
    CHECK(GearSourceGateAllows(raid, 2, false, false));

    // The cap is a ceiling: a negative cap never allows above base.
    GearSourceFacts dungeonAtNegative = VendorItem();
    dungeonAtNegative.tier = GEAR_SOURCE_TIER_DUNGEON;
    CHECK(!GearSourceGateAllows(dungeonAtNegative, -5, false, false));
}

static void TestReputationAndPvp()
{
    GearSourceFacts rep = VendorItem();
    rep.reputationGated = true;
    CHECK(!GearSourceGateAllows(rep, 0, false, false));
    CHECK(GearSourceGateAllows(rep, 0, true, false));
    CHECK(!GearSourceGateAllows(rep, 2, false, false));

    GearSourceFacts pvp = VendorItem();
    pvp.pvp = true;
    CHECK(!GearSourceGateAllows(pvp, 0, false, false));
    CHECK(GearSourceGateAllows(pvp, 0, false, true));
    CHECK(!GearSourceGateAllows(pvp, 2, false, false));

    GearSourceFacts both = VendorItem();
    both.reputationGated = true;
    both.pvp = true;
    CHECK(!GearSourceGateAllows(both, 0, true, false));
    CHECK(!GearSourceGateAllows(both, 0, false, true));
    CHECK(GearSourceGateAllows(both, 0, true, true));
}

static void TestUnknownSourceFailsClosed()
{
    GearSourceFacts unknown;
    unknown.itemLevel = 60;
    unknown.quality = 4;
    CHECK(!GearSourceGateAllows(unknown, 2, true, true));

    unknown.whitelisted = true;
    CHECK(GearSourceGateAllows(unknown, 0, false, false));

    unknown.blacklisted = true;
    CHECK(!GearSourceGateAllows(unknown, 2, true, true));
}

static void TestUnobtainableGear()
{
    GearSourceFacts retired = VendorItem();
    retired.retired = true;
    CHECK(!GearSourceGateAllows(retired, 0, false, false));
    retired.whitelisted = true;
    CHECK(GearSourceGateAllows(retired, 0, false, false));

    GearSourceFacts zeroLevel = VendorItem();
    zeroLevel.itemLevel = 0;
    CHECK(!GearSourceGateAllows(zeroLevel, 0, false, false));
    zeroLevel.cosmeticSlot = true;
    CHECK(GearSourceGateAllows(zeroLevel, 0, false, false));

    GearSourceFacts legendary = VendorItem();
    legendary.quality = kGearQualityLegendary;
    CHECK(!GearSourceGateAllows(legendary, 0, false, false));
    legendary.quality = kGearQualityLegendary - 1;
    CHECK(GearSourceGateAllows(legendary, 0, false, false));
    legendary.quality = kGearQualityLegendary;
    legendary.whitelisted = true;
    CHECK(GearSourceGateAllows(legendary, 0, false, false));
}

static void TestWhitelistDoesNotLiftSourceRules()
{
    GearSourceFacts raid = VendorItem();
    raid.tier = GEAR_SOURCE_TIER_RAID;
    raid.whitelisted = true;
    CHECK(!GearSourceGateAllows(raid, 0, false, false));
    CHECK(GearSourceGateAllows(raid, 2, false, false));

    GearSourceFacts rep = VendorItem();
    rep.reputationGated = true;
    rep.whitelisted = true;
    CHECK(!GearSourceGateAllows(rep, 0, false, false));
    CHECK(GearSourceGateAllows(rep, 0, true, false));

    GearSourceFacts pvp = VendorItem();
    pvp.pvp = true;
    pvp.whitelisted = true;
    CHECK(!GearSourceGateAllows(pvp, 0, false, false));
    CHECK(GearSourceGateAllows(pvp, 0, false, true));
}

int main()
{
    TestTierCap();
    TestReputationAndPvp();
    TestUnknownSourceFailsClosed();
    TestUnobtainableGear();
    TestWhitelistDoesNotLiftSourceRules();
    std::printf("gear source tier policy: %d checks passed\n", checks);
    return 0;
}
