#pragma once

// W0 (FOREVER-W0-W4-SPEC.md, round W0-A): the race and class of the next pool character, as a pure decision. Core-free, so
// forever/tools/test_pool_composition_policy.cpp checks it; TbPoolCreate static_asserts the ids are the core's.
//
// The composition is a BALANCED sequence, not a random one: the serial of the character (0, 1, 2 ... as the pool grows) picks
// the faction (alternating) and the race (the four of that faction in turn), so any even number of picks is 50/50 and every
// vanilla race appears as often as the rest (100 picks: 12 or 13 each). The class roll only picks within the race's own
// vanilla class list, so a pair the client could not make (human death knight, orc paladin, tauren rogue) never comes out.
//
// Race and class ids are the client's (SharedDefines.h): races 1 human, 2 orc, 3 dwarf, 4 night elf, 5 undead, 6 tauren,
// 7 gnome, 8 troll; classes 1 warrior, 2 paladin, 3 hunter, 4 rogue, 5 priest, 7 shaman, 8 mage, 9 warlock, 11 druid. Races 9+
// (the later expansions) are never pool races.

#include <cstdint>

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
    return race == RaceHuman || race == RaceDwarf || race == RaceNightElf || race == RaceGnome;
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

} // namespace world
} // namespace TortoiseBots
