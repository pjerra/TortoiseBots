// Standalone test of W0's pool composition (ai/playerbot/policy/world/PoolCompositionPolicy.h):
//   g++ -std=c++17 -O1 -Wall -Wextra -If. forever/tools/test_pool_composition_policy.cpp -o /tmp/test_pool_composition_policy && /tmp/test_pool_composition_policy
//
// The composition is a decision (FOREVER-W0-W4-SPEC.md, round W0-A): 100 picks are 50/50 factions (plus or minus one), every
// vanilla race 12 or 13 times, and no pair outside the vanilla race/class table. A race the policy does not know, or a pair the
// client cannot make, would put a character that cannot be created in the pool: it must never come out.
#include "ai/playerbot/policy/world/PoolCompositionPolicy.h"

#include <cstdio>

namespace w = TortoiseBots::world;

static int g_checks = 0, g_fail = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

int main()
{
    CHECK(w::RaceHuman == 1 && w::RaceOrc == 2 && w::RaceDwarf == 3 && w::RaceNightElf == 4);
    CHECK(w::RaceUndead == 5 && w::RaceTauren == 6 && w::RaceGnome == 7 && w::RaceTroll == 8);
    CHECK(w::ClassWarrior == 1 && w::ClassPaladin == 2 && w::ClassHunter == 3 && w::ClassRogue == 4);
    CHECK(w::ClassPriest == 5 && w::ClassShaman == 7 && w::ClassMage == 8 && w::ClassWarlock == 9 && w::ClassDruid == 11);

    // Only the eight vanilla races; the later expansions' (9+) never.
    for (int race = 0; race < 16; ++race)
        CHECK(w::IsVanillaRace(std::uint8_t(race)) == (race >= 1 && race <= 8));
    CHECK(w::IsAllianceRace(w::RaceHuman) && w::IsAllianceRace(w::RaceDwarf) && w::IsAllianceRace(w::RaceNightElf) && w::IsAllianceRace(w::RaceGnome));
    CHECK(!w::IsAllianceRace(w::RaceOrc) && !w::IsAllianceRace(w::RaceUndead) && !w::IsAllianceRace(w::RaceTauren) && !w::IsAllianceRace(w::RaceTroll));

    // The class lists are the vanilla ones and only valid pairs: the spec's refusals hold, the legal pairs do not.
    CHECK(!w::IsVanillaPair(w::RaceHuman, w::ClassDruid) && !w::IsVanillaPair(w::RaceHuman, 6 /*death knight*/) && !w::IsVanillaPair(w::RaceHuman, w::ClassShaman));
    CHECK(!w::IsVanillaPair(w::RaceOrc, w::ClassPaladin) && !w::IsVanillaPair(w::RaceOrc, w::ClassDruid) && !w::IsVanillaPair(w::RaceOrc, w::ClassPriest));
    CHECK(!w::IsVanillaPair(w::RaceTauren, w::ClassRogue) && !w::IsVanillaPair(w::RaceTauren, w::ClassMage) && !w::IsVanillaPair(w::RaceTauren, w::ClassWarlock));
    CHECK(!w::IsVanillaPair(w::RaceDwarf, w::ClassDruid) && !w::IsVanillaPair(w::RaceGnome, w::ClassPaladin));
    CHECK(!w::IsVanillaPair(w::RaceTroll, w::ClassPaladin) && !w::IsVanillaPair(w::RaceTroll, w::ClassWarlock));
    CHECK(w::IsVanillaPair(w::RaceHuman, w::ClassPaladin) && w::IsVanillaPair(w::RaceTauren, w::ClassDruid));
    CHECK(w::IsVanillaPair(w::RaceUndead, w::ClassWarlock) && w::IsVanillaPair(w::RaceTroll, w::ClassMage));
    CHECK(!w::IsVanillaPair(9, w::ClassWarrior));          // goblin: never a pool race
    CHECK(!w::IsVanillaPair(0, w::ClassWarrior));

    // 100 picks: serial 0..99, a class roll that changes every call.
    int perRace[9] = {};
    int alliance = 0, horde = 0;
    for (std::uint32_t serial = 0; serial < 100; ++serial)
    {
        w::PoolComposition pick = w::CompositionAt(serial, serial * 7u + 3u);
        CHECK(w::IsVanillaRace(pick.race));
        CHECK(w::IsVanillaPair(pick.race, pick.klass));
        CHECK(pick.race >= 1 && pick.race <= 8);
        ++perRace[pick.race];
        (w::IsAllianceRace(pick.race) ? alliance : horde) += 1;
    }
    CHECK(alliance + horde == 100);
    CHECK(alliance >= 49 && alliance <= 51);
    CHECK(horde >= 49 && horde <= 51);
    for (int race = 1; race <= 8; ++race)
        CHECK(perRace[race] >= 12 && perRace[race] <= 13);

    // The class roll only chooses inside the race: every class of every race comes out, and no roll ever produces a class the
    // race does not have.
    for (int race = 1; race <= 8; ++race)
    {
        std::uint8_t count = 0;
        std::uint8_t const* classes = w::ClassesOfRace(std::uint8_t(race), count);
        CHECK(classes != nullptr && count >= 4 && count <= 6);
        std::uint32_t serial = 0;
        while (serial < 100 && w::CompositionAt(serial, 0).race != race)
            ++serial;
        CHECK(serial < 100);                       // every vanilla race is reachable
        bool seen[12] = {};
        for (std::uint32_t roll = 0; roll < 100; ++roll)
        {
            w::PoolComposition pick = w::CompositionAt(serial, roll);
            CHECK(pick.race == race);
            CHECK(w::IsVanillaPair(race, pick.klass));
            if (pick.klass < 12)
                seen[pick.klass] = true;
        }
        for (std::uint8_t i = 0; i < count; ++i)
            CHECK(seen[classes[i]]);
    }
    // Both factions' own sequences stay on their side, what the serial is.
    for (std::uint32_t serial = 0; serial < 40; ++serial)
        CHECK(w::IsAllianceRace(w::CompositionAt(serial * 2, 0).race));
    for (std::uint32_t serial = 0; serial < 40; ++serial)
        CHECK(!w::IsAllianceRace(w::CompositionAt(serial * 2 + 1, 0).race));

    CHECK(w::GenderAt(0) == 0 && w::GenderAt(1) == 1 && w::GenderAt(2) == 0 && w::GenderAt(3) == 1);

    // A race the core refuses is retired for the rest of the job: the roll moves to the faction's next race, and when every
    // race of the turn (or of the class) is retired the composition is empty, so the host skips it and never makes it.
    {
        std::uint16_t all = 0;
        for (std::uint8_t race : { w::RaceHuman, w::RaceDwarf, w::RaceNightElf, w::RaceGnome, w::RaceSkyborneAlliance,
                 w::RaceOrc, w::RaceUndead, w::RaceTauren, w::RaceTroll, w::RaceSkyborneHorde })
        {
            CHECK(w::CompositionRaceBit(race) != 0);
            CHECK((all & w::CompositionRaceBit(race)) == 0);    // one bit per race
            all = std::uint16_t(all | w::CompositionRaceBit(race));
        }
        CHECK(all == w::kCompositionAllRaces);
        CHECK(w::CompositionRaceBit(0) == 0 && w::CompositionRaceBit(9) == 0 && w::CompositionRaceBit(97) == 0);
    }

    // Balanced: serial 0's race is Human; retiring it moves to the next of the faction's rotation, all four leaves none.
    CHECK(w::CompositionAt(0, 0).race == w::RaceHuman);
    {
        std::uint16_t const human = w::CompositionRaceBit(w::RaceHuman);
        w::PoolComposition afterHuman = w::CompositionAt(0, 0, human);
        CHECK(afterHuman.race == w::RaceDwarf);
        CHECK(w::IsVanillaPair(afterHuman.race, afterHuman.klass));
        CHECK(w::CompositionAt(0, 0, std::uint16_t(human | w::CompositionRaceBit(w::RaceDwarf))).race == w::RaceNightElf);
        w::PoolComposition none = w::CompositionAt(0, 0, std::uint16_t(human | w::CompositionRaceBit(w::RaceDwarf)
            | w::CompositionRaceBit(w::RaceNightElf) | w::CompositionRaceBit(w::RaceGnome)));
        CHECK(none.race == 0 && none.klass == 0);
        // The horde rotation retires on its own: Orc moves to Undead, and all four retired leaves no composition.
        CHECK(w::CompositionAt(1, 0, w::CompositionRaceBit(w::RaceOrc)).race == w::RaceUndead);
        CHECK(w::CompositionAt(1, 0, w::kCompositionAllRaces).race == 0);
    }

    // Census: an alliance paladin is Human or Dwarf literally (no other race can be one), so retiring Human leaves Dwarf for
    // every race roll and retiring both leaves no race. A refused Skyborne never comes out for any class roll.
    {
        w::CensusPoolComposition pick = w::CensusCompositionAt(0, 200, 0, 0);
        CHECK(pick.klass == w::ClassPaladin && pick.race == w::RaceHuman);
        std::uint16_t const human = w::CompositionRaceBit(w::RaceHuman);
        for (std::uint32_t raceRoll = 0; raceRoll < 1000; ++raceRoll)
        {
            w::CensusPoolComposition dwarf = w::CensusCompositionAt(0, 200, raceRoll, 0, human);
            CHECK(dwarf.klass == w::ClassPaladin && dwarf.race == w::RaceDwarf);
        }
        w::CensusPoolComposition empty = w::CensusCompositionAt(0, 200, 0, 0, std::uint16_t(human | w::CompositionRaceBit(w::RaceDwarf)));
        CHECK(empty.klass == w::ClassPaladin && empty.race == 0);
        std::uint16_t const noSkyborne = std::uint16_t(w::CompositionRaceBit(w::RaceSkyborneAlliance) | w::CompositionRaceBit(w::RaceSkyborneHorde));
        for (std::uint32_t roll = 0; roll < 1000; ++roll)
        {
            w::CensusPoolComposition side = w::CensusCompositionAt(roll % 2, roll, roll * 7u, 0, noSkyborne);
            CHECK(side.race != w::RaceSkyborneAlliance && side.race != w::RaceSkyborneHorde);
            CHECK(w::IsCensusPair(side.race, side.klass));
        }
    }

    if (g_fail)
    {
        std::printf("RESULT: FAIL (%d of %d checks)\n", g_fail, g_checks);
        return 1;
    }
    std::printf("RESULT: PASS (%d checks)\n", g_checks);
    return 0;
}
