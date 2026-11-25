// Standalone test of the census pool composition (ai/playerbot/policy/world/PoolCompositionPolicy.h,
// `CensusCompositionAt`): 1000 picks, one per serial, must land within a few points of the shares copied from
// forever/data/aotc (aotc.gg, read 2026-11-24).
//   g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/test_pool_census_policy.cpp -o /tmp/test_pool_census_policy && /tmp/test_pool_census_policy
//
// The checks are the acceptance: class shares within 3 points of census-classes.csv, race shares of the whole pool
// within 4 points of census-races.csv (`pct_of_faction` halved, because the pool is half each faction), gender within 3
// points of the class female shares, and every class reachable in both factions. A pair outside ForeverCore's
// playercreateinfo table would make a character the core refuses, so it must never come out.

#include "ai/playerbot/policy/world/PoolCompositionPolicy.h"

#include <cmath>
#include <cstdio>

namespace w = TortoiseBots::world;

static int g_checks = 0, g_fail = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static int const kClasses[] = { w::ClassWarrior, w::ClassPaladin, w::ClassHunter, w::ClassRogue, w::ClassPriest,
    w::ClassShaman, w::ClassMage, w::ClassWarlock, w::ClassDruid };
static int const kRaces[] = { w::RaceHuman, w::RaceNightElf, w::RaceDwarf, w::RaceGnome, w::RaceSkyborneAlliance,
    w::RaceUndead, w::RaceOrc, w::RaceTauren, w::RaceTroll, w::RaceSkyborneHorde };

int main()
{
    // The census pair table: every most and least played pair of census-race-class-pairs.csv is a valid Forever pair, and
    // the vanilla impossibles the old policy refused now exist only where ForeverCore's playercreateinfo says so.
    CHECK(w::IsCensusPair(w::RaceUndead, w::ClassPaladin));         // Undead Paladin, the census's most played pair
    CHECK(w::IsCensusPair(w::RaceDwarf, w::ClassShaman));           // Dwarf Shaman
    CHECK(w::IsCensusPair(w::RaceTroll, w::ClassWarlock));          // Troll Warlock
    CHECK(w::IsCensusPair(w::RaceSkyborneAlliance, w::ClassDruid));
    CHECK(w::IsCensusPair(w::RaceSkyborneHorde, w::ClassShaman));
    CHECK(!w::IsCensusPair(w::RaceSkyborneHorde, w::ClassMage));    // 96: warrior, hunter, rogue, shaman, druid
    CHECK(!w::IsCensusPair(w::RaceSkyborneAlliance, w::ClassShaman));
    CHECK(!w::IsCensusPair(w::RaceGnome, w::ClassPaladin));
    CHECK(!w::IsCensusPair(w::RaceNightElf, w::ClassShaman));
    CHECK(!w::IsCensusPair(9, w::ClassWarrior));                    // goblin: never a pool race
    CHECK(!w::IsCensusPair(0, w::ClassWarrior));

    // Every class has a race in both factions, so the race roll can never come out empty.
    for (int klass : kClasses)
    {
        bool alliance = false, horde = false;
        for (int race : kRaces)
        {
            if (!w::IsCensusPair(std::uint8_t(race), std::uint8_t(klass)))
                continue;
            (w::IsAllianceRace(std::uint8_t(race)) ? alliance : horde) = true;
        }
        CHECK(alliance && horde);
    }

    // 1000 characters: serial 0..999, three independent-looking rolls each (odd multipliers coprime with the moduli the
    // picker uses, so the sample is not a short cycle).
    int classCount[12] = {};
    int raceCount[97] = {};
    int genderCount[2] = {};
    int factionCount[2] = {};
    for (std::uint32_t serial = 0; serial < 1000; ++serial)
    {
        w::CensusPoolComposition const pick = w::CensusCompositionAt(
            serial, serial * 7919u + 13u, serial * 104729u + 7u, serial * 3571u + 11u);
        CHECK(w::IsCensusRace(pick.race));
        CHECK(w::IsCensusPair(pick.race, pick.klass));
        CHECK(pick.klass == w::ClassWarrior || pick.klass == w::ClassPaladin || pick.klass == w::ClassHunter
            || pick.klass == w::ClassRogue || pick.klass == w::ClassPriest || pick.klass == w::ClassShaman
            || pick.klass == w::ClassMage || pick.klass == w::ClassWarlock || pick.klass == w::ClassDruid);
        CHECK(pick.gender == 0 || pick.gender == 1);
        ++classCount[pick.klass];
        ++raceCount[pick.race];
        ++genderCount[pick.gender];
        ++factionCount[w::IsAllianceRace(pick.race) ? 0 : 1];
    }

    // The faction still alternates with the serial: 500/500, nothing else.
    CHECK(factionCount[0] == 500 && factionCount[1] == 500);

    // Class shares: the global census share is the average of the two faction shares, because the pool is half each. A few
    // points is 3 percentage points (30 per mille).
    for (int klass : kClasses)
    {
        double const expected = (w::CensusClassSharePm(0, std::uint8_t(klass)) + w::CensusClassSharePm(1, std::uint8_t(klass))) / 20.0;
        double const got = classCount[klass] / 10.0;
        CHECK(classCount[klass] > 0);
        CHECK(std::fabs(got - expected) <= 3.0);
        if (std::fabs(got - expected) > 3.0)
            std::printf("      class %d: pool %.1f%% census %.1f%%\n", klass, got, expected);
    }

    // Race shares of the whole pool: the census gives a share of its faction, and the pool is half each, so halve it.
    for (int race : kRaces)
    {
        double const expected = w::CensusRaceSharePm(std::uint8_t(race)) / 20.0;
        double const got = raceCount[race] / 10.0;
        CHECK(raceCount[race] > 0);
        if (std::fabs(got - expected) > 4.0)
            std::printf("      race %d: pool %.1f%% census %.1f%%\n", race, got, expected);
        CHECK(std::fabs(got - expected) <= 4.0);
    }

    // Gender: the expected female share is the class shares weighted by the class female shares.
    double expectedFemale = 0.0;
    for (int klass : kClasses)
        expectedFemale += ((w::CensusClassSharePm(0, std::uint8_t(klass)) + w::CensusClassSharePm(1, std::uint8_t(klass))) / 2.0)
            * w::CensusFemaleSharePm(std::uint8_t(klass)) / 10000.0;
    double const gotFemale = genderCount[1] / 10.0;
    CHECK(genderCount[0] > 0 && genderCount[1] > 0);
    CHECK(std::fabs(gotFemale - expectedFemale) <= 3.0);

    // A race that can be every class carries every class bit (the mask is 1 << class id per class).
    CHECK(w::CensusClassMask(w::RaceTauren) == 0xbbe);              // all nine classes

    if (g_fail)
    {
        std::printf("RESULT: FAIL (%d of %d checks)\n", g_fail, g_checks);
        return 1;
    }
    std::printf("RESULT: PASS (%d checks)\n", g_checks);
    return 0;
}
