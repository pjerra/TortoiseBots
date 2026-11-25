// GENERATED FILE: do not edit. Written by forever/tools/gen_data_tables.py from
// forever/data/raid/consumables.csv; --check fails on a hand edit.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace tbdata
{
// A raid consumable of one role and category, with the item, spell and aura that apply it (issue #22).
// `aura` is the buff the spell leaves on the bot (the food rows' 15-minute Well Fed); 0 on a weapon row,
// whose effect is the main hand's temporary enchantment and whose `weapon` names the family it goes on.
// The host keeps a row only when the item exists,
// it lists that spell and the spell leaves that aura (host/TbRaidConsumables.cpp: BuildIndex).
namespace raidcons
{
struct ConsumableRow
{
    char const* role;        // role
    char const* category;    // category
    std::uint32_t item;      // item_id
    std::uint32_t spell;     // spell_id
    std::uint32_t aura;      // aura_id
    std::uint8_t minLevel;   // min_level
    char const* weapon;      // weapon: a weapon row's main-hand family (edged/blunt/any), else empty
    char const* note;        // note
};

inline constexpr ConsumableRow kConsumables[] = {
    { "tank", "flask", 13510u, 17626u, 17626u, 50, "", "Flask of the Titans" },
    { "melee", "flask", 13510u, 17626u, 17626u, 50, "", "Flask of the Titans (the tank flask is melee and hunter prep too)" },
    { "ranged", "flask", 13510u, 17626u, 17626u, 50, "", "Flask of the Titans (the tank flask is melee and hunter prep too)" },
    { "caster", "flask", 13512u, 17628u, 17628u, 50, "", "Flask of Supreme Power" },
    { "healer", "flask", 13511u, 17627u, 17627u, 50, "", "Flask of Distilled Wisdom" },
    { "melee", "battle", 13452u, 17538u, 17538u, 46, "", "Elixir of the Mongoose" },
    { "ranged", "battle", 13452u, 17538u, 17538u, 46, "", "Elixir of the Mongoose" },
    { "melee", "battle", 9206u, 11405u, 11405u, 48, "", "Elixir of Giants (the pin names item 9206 Elixir of Greater Strength)" },
    { "caster", "battle", 13454u, 17539u, 17539u, 47, "", "Greater Arcane Elixir" },
    { "tank", "guardian", 13445u, 11348u, 11348u, 43, "", "Elixir of Superior Defense (the pin names it Elixir of Greater Defense)" },
    { "melee", "guardian", 13445u, 11348u, 11348u, 43, "", "Elixir of Superior Defense (the pin names it Elixir of Greater Defense)" },
    { "ranged", "guardian", 13445u, 11348u, 11348u, 43, "", "Elixir of Superior Defense (the pin names it Elixir of Greater Defense)" },
    { "healer", "guardian", 20007u, 24363u, 24363u, 40, "", "Mageblood Potion (the pin names it Mageblood Elixir)" },
    { "melee", "food", 20452u, 1248401u, 1248422u, 45, "", "Smoked Desert Dumplings (strength; Well Fed 1248422)" },
    { "ranged", "food", 18254u, 1248396u, 1248421u, 35, "", "Runn Tum Tuber Surprise (agility at the pin; Well Fed 1248421)" },
    { "caster", "food", 13931u, 1249513u, 1249520u, 35, "", "Nightfin Soup (spell power; Well Fed 1249520)" },
    { "melee", "weapon", 18262u, 22756u, 0u, 50, "edged", "Elemental Sharpening Stone (edged weapon)" },
    { "melee", "weapon", 12404u, 16138u, 0u, 35, "edged", "Dense Sharpening Stone (edged weapon)" },
    { "caster", "weapon", 20749u, 25122u, 0u, 45, "any", "Brilliant Wizard Oil" },
    { "healer", "weapon", 20748u, 25123u, 0u, 45, "any", "Brilliant Mana Oil" },
};
inline constexpr std::size_t kConsumableCount = sizeof(kConsumables) / sizeof(kConsumables[0]);

// A role/category pair the table deliberately has no consumable for: the `.tb state` read-out names the slot
// (`consnotcovered=`) instead of leaving it silently ready.
struct NotCoveredRow
{
    char const* role;        // role
    char const* category;    // category
    char const* reason;      // why the table has no consumable for it
};

inline constexpr std::array<NotCoveredRow, 7> kNotCovered = {{
    { "tank", "battle", "the table's battle elixirs are the damage ones; a tank's elixir slot is its flask and the armor elixir" },
    { "caster", "guardian", "the table's guardian elixirs are the armor and the healer mana ones; a caster's elixir slot is its flask and the spell power elixir" },
    { "tank", "food", "the table's food buffs are strength/agility/spell power; none is a tank buff" },
    { "tank", "weapon", "a tank may wield a blunt weapon; the table's stones are for edged weapons" },
    { "ranged", "weapon", "a bow or gun takes no temporary weapon enchantment" },
    { "healer", "battle", "the table's battle elixirs are the damage ones; a healer's elixir slot is its flask and the mana elixir" },
    { "healer", "food", "the table's food buffs are strength/agility/spell power; none is a healing buff" },
}};
inline constexpr std::size_t kNotCoveredCount = kNotCovered.size();
}
}
