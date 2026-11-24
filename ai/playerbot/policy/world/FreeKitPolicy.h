#pragma once

// W1 (spec D4): the non-combat kit of a free bot, as a pure decision. The factory adds the class's spec strategy plus the world
// kit; no follow, no wander, no rpg, quest, vendor, maintenance, group or guild strategy. The travel strategy is in the kit:
// a free bot picks a destination and walks to it. The emote strategy is in it too, so a free bot can greet a real player when
// the owner turns AiPlayerbot.RandomBotGreet on (off by default; GreetAction does the gating). `rpg` is added by the factory on top of this array when
// AiPlayerbot.AutoDoQuests is on (a config choice, not part of the pure kit). Core-free, so
// forever/tools/test_free_kit_policy.cpp checks it; the factory static_asserts the class ids are the core's.
//
// Class ids are the client's (SharedDefines.h): warrior 1, paladin 2, hunter 3, rogue 4, priest 5, shaman 7, mage 8,
// warlock 9, druid 11 (the ids the slice's other policies name).

#include <cstdint>

namespace TortoiseBots
{
namespace world
{

inline constexpr std::uint8_t ClassWarrior = 1;
inline constexpr std::uint8_t ClassPaladin = 2;
inline constexpr std::uint8_t ClassHunter = 3;
inline constexpr std::uint8_t ClassRogue = 4;
inline constexpr std::uint8_t ClassPriest = 5;
inline constexpr std::uint8_t ClassShaman = 7;
inline constexpr std::uint8_t ClassMage = 8;
inline constexpr std::uint8_t ClassWarlock = 9;
inline constexpr std::uint8_t ClassDruid = 11;

// The class strategy of the free kit: the same tab the slice gives the class everywhere else (warrior arms, priest discipline,
// mage fire, rogue combat, hunter marksmanship, warlock affliction, druid/paladin/shaman healer kit).
inline char const* FreeSpecStrategy(std::uint8_t classId)
{
    switch (classId)
    {
        case ClassPriest: return "discipline";
        case ClassMage: return "fire";
        case ClassRogue: return "combat";
        case ClassHunter: return "marksmanship";
        case ClassWarlock: return "affliction";
        case ClassDruid: return "restoration";
        case ClassPaladin: return "holy";
        case ClassShaman: return "restoration";
        default: return "arms";
    }
}

// The rest of the kit, in the order the factory adds it. `nullptr` ends the array (Strategy::addStrategies takes a NULL-terminated
// list). Everything here is registered in the slice's context.
inline char const* const* FreeWorldKit()
{
    static char const* const kit[] = { "grind", "travel", "food", "loot", "roll", "default", "buff", "emote", nullptr };
    return kit;
}

} // namespace world
} // namespace TortoiseBots
