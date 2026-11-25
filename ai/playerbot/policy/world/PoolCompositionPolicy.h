#pragma once

// W0 (FOREVER-W0-W4-SPEC.md, round W0-A): the race and class of the next pool character, as a pure decision. Core-free, so
// forever/tools/test_pool_composition_policy.cpp checks it; TbPoolCreate static_asserts the ids are the core's.
//
// Two compositions share this file:
//
//  * `CompositionAt`, the balanced sequence of W0: the serial of the character (0, 1, 2 ... as the pool grows) picks the
//    faction (alternating) and the race (the four of that faction in turn), so any even number of picks is 50/50 and every
//    vanilla race appears as often as the rest (100 picks: 12 or 13 each). The class roll only picks within the race's own
//    vanilla class list, so a pair the client could not make (human death knight, orc paladin, tauren rogue) never comes out.
//  * `CensusCompositionAt`, the live-server shares of aotc.gg (TortoiseBots.Pool.CensusComposition): the class by its share
//    of its faction, the race within the faction by its share and only among the races the class can be, the gender by the
//    class's female share. The faction still alternates with the serial, so the pool stays 50/50.
//
// Race and class ids are the client's (SharedDefines.h): races 1 human, 2 orc, 3 dwarf, 4 night elf, 5 undead, 6 tauren,
// 7 gnome, 8 troll, 95 High Order Skyborne (Alliance), 96 Windshaper Skyborne (Horde); classes 1 warrior, 2 paladin,
// 3 hunter, 4 rogue, 5 priest, 7 shaman, 8 mage, 9 warlock, 11 druid. Other race ids are never pool races.

#include <cstddef>
#include <cstdint>
#include <initializer_list>

namespace TortoiseBots
{
namespace world
{

inline constexpr std::uint8_t RaceHuman = 1;
inline constexpr std::uint8_t RaceOrc = 2;
inline constexpr std::uint8_t RaceDwarf = 3;
inline constexpr std::uint8_t RaceNightElf = 4;
inline constexpr std::uint8_t RaceUndead = 5;
inline constexpr std::uint8_t RaceTauren = 6;
inline constexpr std::uint8_t RaceGnome = 7;
inline constexpr std::uint8_t RaceTroll = 8;
inline constexpr std::uint8_t RaceSkyborneAlliance = 95;
inline constexpr std::uint8_t RaceSkyborneHorde = 96;

inline constexpr std::uint8_t ClassWarrior = 1;
inline constexpr std::uint8_t ClassPaladin = 2;
inline constexpr std::uint8_t ClassHunter = 3;
inline constexpr std::uint8_t ClassRogue = 4;
inline constexpr std::uint8_t ClassPriest = 5;
inline constexpr std::uint8_t ClassShaman = 7;
inline constexpr std::uint8_t ClassMage = 8;
inline constexpr std::uint8_t ClassWarlock = 9;
inline constexpr std::uint8_t ClassDruid = 11;

struct PoolComposition
{
    std::uint8_t race = 0;
    std::uint8_t klass = 0;
};

// A vanilla race (1-8), never a later expansion's.
inline bool IsVanillaRace(std::uint8_t race)
{
    return race >= RaceHuman && race <= RaceTroll;
}

inline bool IsAllianceRace(std::uint8_t race)
{
    return race == RaceHuman || race == RaceDwarf || race == RaceNightElf || race == RaceGnome || race == RaceSkyborneAlliance;
}

// The class list of a race, in the order the sequence visits them; `count` is 0 for a race outside 1-8.
inline std::uint8_t const* ClassesOfRace(std::uint8_t race, std::uint8_t& count)
{
    static std::uint8_t const human[] = { ClassWarrior, ClassPaladin, ClassRogue, ClassPriest, ClassMage, ClassWarlock };
    static std::uint8_t const orc[] = { ClassWarrior, ClassHunter, ClassRogue, ClassShaman, ClassWarlock };
    static std::uint8_t const dwarf[] = { ClassWarrior, ClassPaladin, ClassHunter, ClassRogue, ClassPriest };
    static std::uint8_t const nightElf[] = { ClassWarrior, ClassHunter, ClassRogue, ClassPriest, ClassDruid };
    static std::uint8_t const undead[] = { ClassWarrior, ClassRogue, ClassPriest, ClassMage, ClassWarlock };
    static std::uint8_t const tauren[] = { ClassWarrior, ClassHunter, ClassShaman, ClassDruid };
    static std::uint8_t const gnome[] = { ClassWarrior, ClassRogue, ClassMage, ClassWarlock };
    static std::uint8_t const troll[] = { ClassWarrior, ClassHunter, ClassRogue, ClassPriest, ClassShaman, ClassMage };
    switch (race)
    {
        case RaceHuman:    count = sizeof(human) / sizeof(human[0]);    return human;
        case RaceOrc:      count = sizeof(orc) / sizeof(orc[0]);        return orc;
        case RaceDwarf:    count = sizeof(dwarf) / sizeof(dwarf[0]);    return dwarf;
        case RaceNightElf: count = sizeof(nightElf) / sizeof(nightElf[0]); return nightElf;
        case RaceUndead:   count = sizeof(undead) / sizeof(undead[0]);  return undead;
        case RaceTauren:   count = sizeof(tauren) / sizeof(tauren[0]);  return tauren;
        case RaceGnome:    count = sizeof(gnome) / sizeof(gnome[0]);    return gnome;
        case RaceTroll:    count = sizeof(troll) / sizeof(troll[0]);    return troll;
        default:           count = 0;                                   return nullptr;
    }
}

// Whether the client can make this race and class at all.
inline bool IsVanillaPair(std::uint8_t race, std::uint8_t klass)
{
    std::uint8_t count = 0;
    std::uint8_t const* classes = ClassesOfRace(race, count);
    for (std::uint8_t i = 0; classes && i < count; ++i)
        if (classes[i] == klass)
            return true;
    return false;
}

// The next character of the sequence: `serial` 0, 1, 2 ... (the pool's character count, so a restart continues it), and any
// class roll (it only chooses within the race's list). The faction alternates with the serial, so serials 0..99 are 50/50.
inline PoolComposition CompositionAt(std::uint32_t serial, std::uint32_t classRoll)
{
    static std::uint8_t const alliance[] = { RaceHuman, RaceDwarf, RaceNightElf, RaceGnome };
    static std::uint8_t const horde[] = { RaceOrc, RaceUndead, RaceTauren, RaceTroll };
    bool const allianceSide = (serial % 2u) == 0u;
    std::uint8_t const (&races)[4] = allianceSide ? alliance : horde;
    PoolComposition pick;
    pick.race = races[(serial / 2u) % 4u];
    std::uint8_t count = 0;
    std::uint8_t const* classes = ClassesOfRace(pick.race, count);
    pick.klass = classes[classRoll % count];
    return pick;
}

// 0 male, 1 female (the two the client offers).
inline std::uint8_t GenderAt(std::uint32_t roll)
{
    return roll % 2u == 0u ? 0 : 1;
}

// --- Census composition ----------------------------------------------------------------------------------------------------
//
// Shares are per mille, copied from forever/data/aotc (aotc.gg, read 2026-11-24): the class tables from census-classes.csv
// (alliance and horde counts over each faction's total), the races from census-races.csv (`pct_of_faction`), the female
// share from census-class-gender.csv. `CensusCompositionAt` uses them as relative weights; rounding leaves some tables
// summing to 999 or 1001, which only changes the last digit of a pick's odds.
//
// The valid race/class pairs are the rows of ForeverCore's `playercreateinfo` (the client's CharBaseInfo for Classic 1.60
// plus the custom Skyborne and Undead Paladin rows), not the vanilla pairs of `IsVanillaPair`: Forever lets a dwarf be a
// shaman and an undead a paladin. Every most and least played pair of census-race-class-pairs.csv is in this table.

struct CensusShare
{
    std::uint8_t id;
    std::uint16_t permille;
};

inline constexpr CensusShare kCensusAllianceClasses[] = {
    { ClassHunter,    164 }, { ClassPaladin, 136 }, { ClassDruid, 119 }, { ClassWarrior, 111 }, { ClassMage, 119 },
    { ClassShaman,     77 }, { ClassWarlock,  86 }, { ClassPriest, 102 }, { ClassRogue,    87 },
};

inline constexpr CensusShare kCensusHordeClasses[] = {
    { ClassHunter,    136 }, { ClassPaladin, 133 }, { ClassDruid, 122 }, { ClassWarrior, 115 }, { ClassMage,  95 },
    { ClassShaman,    120 }, { ClassWarlock, 108 }, { ClassPriest,  89 }, { ClassRogue,   82 },
};

inline constexpr CensusShare kCensusAllianceRaces[] = {
    { RaceHuman,           367 }, { RaceNightElf,         221 }, { RaceDwarf,          178 },
    { RaceGnome,           130 }, { RaceSkyborneAlliance, 105 },
};

inline constexpr CensusShare kCensusHordeRaces[] = {
    { RaceUndead,          376 }, { RaceOrc,              192 }, { RaceTauren,         182 },
    { RaceTroll,           138 }, { RaceSkyborneHorde,    113 },
};

inline constexpr CensusShare kCensusFemaleShares[] = {
    { ClassWarrior, 228 }, { ClassPaladin, 240 }, { ClassHunter, 380 }, { ClassRogue, 373 }, { ClassPriest, 504 },
    { ClassShaman,  244 }, { ClassMage,    424 }, { ClassWarlock, 361 }, { ClassDruid, 450 },
};

inline bool IsCensusRace(std::uint8_t race)
{
    return IsVanillaRace(race) || race == RaceSkyborneAlliance || race == RaceSkyborneHorde;
}

// The classes ForeverCore's playercreateinfo can make for that race, as a bit mask of `1 << class id` (0 = no pair).
inline std::uint16_t CensusClassMask(std::uint8_t race)
{
    auto mask = [](std::initializer_list<std::uint8_t> classes)
    {
        std::uint16_t out = 0;
        for (std::uint8_t klass : classes)
            out = std::uint16_t(out | (1u << klass));
        return out;
    };
    switch (race)
    {
        case RaceHuman:            return mask({ ClassWarrior, ClassPaladin, ClassHunter, ClassRogue, ClassPriest, ClassMage, ClassWarlock });
        case RaceOrc:              return mask({ ClassWarrior, ClassHunter, ClassRogue, ClassPriest, ClassShaman, ClassMage, ClassWarlock });
        case RaceDwarf:            return mask({ ClassWarrior, ClassPaladin, ClassHunter, ClassRogue, ClassPriest, ClassShaman, ClassMage, ClassWarlock });
        case RaceNightElf:         return mask({ ClassWarrior, ClassHunter, ClassRogue, ClassPriest, ClassMage, ClassWarlock, ClassDruid });
        case RaceUndead:           return mask({ ClassWarrior, ClassPaladin, ClassHunter, ClassRogue, ClassPriest, ClassMage, ClassWarlock });
        case RaceTauren:           return mask({ ClassWarrior, ClassPaladin, ClassHunter, ClassRogue, ClassPriest, ClassShaman, ClassMage, ClassWarlock, ClassDruid });
        case RaceGnome:            return mask({ ClassWarrior, ClassHunter, ClassRogue, ClassPriest, ClassMage, ClassWarlock });
        case RaceTroll:            return mask({ ClassWarrior, ClassHunter, ClassRogue, ClassPriest, ClassShaman, ClassMage, ClassWarlock, ClassDruid });
        case RaceSkyborneAlliance: return mask({ ClassWarrior, ClassHunter, ClassRogue, ClassMage, ClassDruid });
        case RaceSkyborneHorde:    return mask({ ClassWarrior, ClassHunter, ClassRogue, ClassShaman, ClassDruid });
        default:                   return 0;
    }
}

inline bool IsCensusPair(std::uint8_t race, std::uint8_t klass)
{
    return (CensusClassMask(race) & (1u << klass)) != 0;
}

// A class's share of its faction (0 Alliance, 1 Horde); 0 for an id outside the tables.
inline std::uint32_t CensusClassSharePm(std::uint8_t faction, std::uint8_t klass)
{
    CensusShare const* table = faction ? kCensusHordeClasses : kCensusAllianceClasses;
    std::size_t const count = faction ? sizeof(kCensusHordeClasses) / sizeof(kCensusHordeClasses[0])
                                      : sizeof(kCensusAllianceClasses) / sizeof(kCensusAllianceClasses[0]);
    for (std::size_t i = 0; i < count; ++i)
        if (table[i].id == klass)
            return table[i].permille;
    return 0;
}

// A race's share of its own faction; the whole pool is half each, so its share of all picks is half of this.
inline std::uint32_t CensusRaceSharePm(std::uint8_t race)
{
    for (CensusShare const& share : kCensusAllianceRaces)
        if (share.id == race)
            return share.permille;
    for (CensusShare const& share : kCensusHordeRaces)
        if (share.id == race)
            return share.permille;
    return 0;
}

inline std::uint32_t CensusFemaleSharePm(std::uint8_t klass)
{
    for (CensusShare const& share : kCensusFemaleShares)
        if (share.id == klass)
            return share.permille;
    return 0;
}

struct CensusPoolComposition
{
    std::uint8_t race = 0;
    std::uint8_t klass = 0;
    std::uint8_t gender = 0;        // 0 male, 1 female
};

// The next character in census order: class first (`classRoll`), then a race of the serial's faction that can be that class
// (`raceRoll`), then the gender from the class's female share (`genderRoll` in 0..999). A roll that finds no valid race
// cannot happen: every class has a race in each faction, and the unit test checks it. The faction still alternates with the
// serial, so any even pool is 50/50.
inline CensusPoolComposition CensusCompositionAt(std::uint32_t serial, std::uint32_t classRoll, std::uint32_t raceRoll, std::uint32_t genderRoll)
{
    std::uint8_t const faction = (serial % 2u) == 0u ? 0 : 1;
    CensusShare const* classes = faction ? kCensusHordeClasses : kCensusAllianceClasses;
    std::size_t const classCount = faction ? sizeof(kCensusHordeClasses) / sizeof(kCensusHordeClasses[0])
                                           : sizeof(kCensusAllianceClasses) / sizeof(kCensusAllianceClasses[0]);
    CensusShare const* races = faction ? kCensusHordeRaces : kCensusAllianceRaces;
    std::size_t const raceCount = faction ? sizeof(kCensusHordeRaces) / sizeof(kCensusHordeRaces[0])
                                          : sizeof(kCensusAllianceRaces) / sizeof(kCensusAllianceRaces[0]);

    CensusPoolComposition pick;
    std::uint32_t total = 0;
    for (std::size_t i = 0; i < classCount; ++i)
        total += classes[i].permille;
    std::uint32_t roll = classRoll % total;
    for (std::size_t i = 0; i < classCount; ++i)
    {
        if (roll < classes[i].permille)
        {
            pick.klass = classes[i].id;
            break;
        }
        roll -= classes[i].permille;
    }

    total = 0;
    for (std::size_t i = 0; i < raceCount; ++i)
        if (IsCensusPair(races[i].id, pick.klass))
            total += races[i].permille;
    if (total)
    {
        roll = raceRoll % total;
        for (std::size_t i = 0; i < raceCount; ++i)
        {
            if (!IsCensusPair(races[i].id, pick.klass))
                continue;
            if (roll < races[i].permille)
            {
                pick.race = races[i].id;
                break;
            }
            roll -= races[i].permille;
        }
    }

    pick.gender = (genderRoll % 1000u) < CensusFemaleSharePm(pick.klass) ? 1 : 0;
    return pick;
}

} // namespace world
} // namespace TortoiseBots
