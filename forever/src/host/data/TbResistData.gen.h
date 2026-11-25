// GENERATED FILE: do not edit. Written by forever/tools/gen_data_tables.py from forever/data/raid/boss-resist.csv.
// Regenerate after a CSV change: python3 forever/tools/gen_data_tables.py (--check fails while this is stale).
#pragma once

#include <cstddef>
#include <cstdint>

namespace tbdata
{
// A raid boss and the school its damage is mostly one of (issue #20: resistance gear before the pull).
// `school` is the client's SpellSchools id (1 holy, 2 fire, 3 nature, 4 frost, 5 shadow, 6 arcane);
// `evidenceSpell` is a spell the boss casts at the core pin (its script, or EventAI where it has none),
// and the module drops the row at boot when the spell mirror says that spell's school is another one.
namespace resist
{
struct BossResistRow
{
    std::uint32_t entry;           // entry
    char const* name;              // name
    std::uint8_t school;           // school
    std::uint32_t evidenceSpell;   // evidence_spell
    char const* note;              // note
};

inline constexpr BossResistRow kBossResist[] = {
    { 10184u, "Onyxia", 2, 18435u, "Onyxias Lair: classic_boss_onyxia.cpp SPELL_ONY_FLAMEBREATH" },
    { 11502u, "Ragnaros", 2, 20565u, "Molten Core: classic_boss_ragnaros.cpp SPELL_RAG_MAGMA_BLAST" },
    { 11583u, "Nefarian", 5, 22539u, "Blackwing Lair: classic_boss_nefarian.cpp CLASSIC_BWL_SPELL_NEF_SHADOWFLAME" },
    { 11982u, "Magmadar", 2, 19272u, "Molten Core: EventAI at the pin (no C++ script) Lava Breath" },
    { 11988u, "Golemagg the Incinerator", 2, 20228u, "Molten Core: classic_boss_golemagg.cpp SPELL_GOLEMAGG_PYROBLAST" },
    { 12056u, "Baron Geddon", 2, 20475u, "Molten Core: classic_boss_baron_geddon.cpp SPELL_GEDDON_LIVINGBOMB" },
    { 12118u, "Lucifron", 5, 19702u, "Molten Core: classic_boss_lucifron.cpp SPELL_LUCIFRON_IMPENDING_DOOM" },
    { 12259u, "Gehennas", 5, 19729u, "Molten Core: classic_boss_gehennas.cpp SPELL_GEHENNAS_SHADOW_BOLT_TARGET" },
    { 12435u, "Razorgore the Untamed", 2, 22425u, "Blackwing Lair: classic_boss_razorgore.cpp CLASSIC_BWL_SPELL_RAZ_FIREBALL_VOLLEY" },
    { 13020u, "Vaelastrasz the Corrupt", 2, 23461u, "Blackwing Lair: classic_boss_vaelastrasz.cpp CLASSIC_BWL_SPELL_VAEL_FLAME_BREATH" },
    { 15299u, "Viscidus", 3, 25991u, "Temple of Ahn'Qiraj: classic_boss_viscidus.cpp SPELL_VISCIDUS_POISONBOLT_VOLLEY" },
    { 15509u, "Princess Huhuran", 3, 26053u, "Temple of Ahn'Qiraj: classic_boss_huhuran.cpp SPELL_HUHURAN_NOXIOUSPOISON" },
    { 15989u, "Sapphiron", 4, 28524u, "Naxxramas: classic_boss_sapphiron.cpp SPELL_SAPPH_FROST_BREATH" },
};
inline constexpr std::size_t kResistRowCount = sizeof(kBossResist) / sizeof(kBossResist[0]);
}
}
