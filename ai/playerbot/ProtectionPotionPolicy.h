#pragma once

#include <cstdint>

// Pure decision rule: which Greater Protection Potion a raid boss calls for.
// Keyed on the boss's creature entry, so the caller only walks the bot's own
// cached attacker list (no world scan). Each school was checked against the
// 1.18.1 core: the spells the boss script casts (src/scripts/dungeons/
// molten_core, onyxias_lair, blackwing_lair, temple_of_ahnqiraj) and their
// `school` in sql/base/tw_world_spell_template.sql (2 fire, 3 nature,
// 5 shadow). Bosses whose damage is mixed or not school-based (Garr, Hakkar,
// Chromaggus, the BWL drakes) are left out on purpose.
// No core includes: tools/test_protection_potion_policy.cpp covers it.

namespace ai
{
    struct ProtectionPotion
    {
        std::uint32_t itemId;      // potion item
        std::uint32_t buffSpellId; // aura the potion applies
    };

    constexpr ProtectionPotion kNoProtectionPotion = { 0, 0 };
    constexpr ProtectionPotion kFireProtectionPotion = { 13457, 17543 };   // Greater Fire Protection Potion
    constexpr ProtectionPotion kShadowProtectionPotion = { 13459, 17548 }; // Greater Shadow Protection Potion
    constexpr ProtectionPotion kNatureProtectionPotion = { 13458, 17546 }; // Greater Nature Protection Potion

    inline ProtectionPotion BossProtectionPotion(std::uint32_t entry)
    {
        switch (entry)
        {
            // Fire
            case 11982: // Magmadar: Lava Breath
            case 12056: // Baron Geddon: Living Bomb, Inferno
            case 11988: // Golemagg the Incinerator: Pyroblast, Magma Splash
            case 11502: // Ragnaros: Wrath of Ragnaros, Magma Blast, Elemental Fire
            case 10184: // Onyxia: Flame Breath, Fireball, Deep Breath
            case 12435: // Razorgore the Untamed: Fireball Volley, Conflagration
            case 13020: // Vaelastrasz the Corrupt: Flame Breath, Fire Nova
                return kFireProtectionPotion;
            // Shadow
            case 12118: // Lucifron: Impending Doom, Shadow Shock
            case 12259: // Gehennas: Shadow Bolt (Rain of Fire is stepped out of)
            case 11583: // Nefarian: Shadow Flame
                return kShadowProtectionPotion;
            // Nature
            case 15509: // Princess Huhuran: Noxious Poison, Acid Spit, Wyvern Sting
            case 15299: // Viscidus: Poison Bolt Volley, Poison Shock, Toxin
                return kNatureProtectionPotion;
            default:
                return kNoProtectionPotion;
        }
    }
}
