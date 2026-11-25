#pragma once

#include <cstdint>

namespace ai
{

// Maps ForeverCore Classic 1.60 ItemModType stat ids (41/42/43/45/47/48/50,
// ratings 12-21/31-32/37-39, and Classic 1.60 ids 83-139) to the legacy
// weightscale stat name in ai_playerbot_weightscale_data.
// Returns nullptr for base stats handled by weightStatLink and for unknown or
// unweighted ids so they score 0.
inline char const* ClassicItemStatWeightName(uint32_t statType)
{
    switch (statType)
    {
    case 12: return "defrtng";           // ITEM_MOD_DEFENSE_SKILL_RATING
    case 13: return "dodgertng";         // ITEM_MOD_DODGE_RATING
    case 14: return "parryrtng";         // ITEM_MOD_PARRY_RATING
    case 15: return "blockrtng";         // ITEM_MOD_BLOCK_RATING
    case 16: return "hitrtng";           // ITEM_MOD_HIT_MELEE_RATING
    case 17: return "hitrtng";           // ITEM_MOD_HIT_RANGED_RATING
    case 18: return "spellhitrtng";      // ITEM_MOD_HIT_SPELL_RATING
    case 19: return "critstrkrtng";      // ITEM_MOD_CRIT_MELEE_RATING
    case 20: return "critstrkrtng";      // ITEM_MOD_CRIT_RANGED_RATING
    case 21: return "spellcritstrkrtng"; // ITEM_MOD_CRIT_SPELL_RATING
    case 31: return "hitrtng";           // ITEM_MOD_HIT_RATING
    case 32: return "critstrkrtng";      // ITEM_MOD_CRIT_RATING
    case 37: return "exprtng";           // ITEM_MOD_EXPERTISE_RATING
    case 38: return "atkpwr";            // ITEM_MOD_ATTACK_POWER
    case 39: return "atkpwr";            // ITEM_MOD_RANGED_ATTACK_POWER
    case 41: return "splheal";           // ITEM_MOD_SPELL_HEALING_DONE
    case 42: return "splpwr";            // ITEM_MOD_SPELL_DAMAGE_DONE
    case 43: return "manargn";           // ITEM_MOD_MANA_REGENERATION
    case 45: return "splpwr";            // ITEM_MOD_SPELL_POWER
    case 47: return "spellpenrtng";      // ITEM_MOD_SPELL_PENETRATION
    case 48: return "block";             // ITEM_MOD_BLOCK_VALUE
    case 50: return "armor";             // ITEM_MOD_EXTRA_ARMOR
    case 85: return "firsplpwr";         // ITEM_MOD_CLASSIC_FIRE_DAMAGE_DONE
    case 86: return "natsplpwr";         // ITEM_MOD_CLASSIC_NATURE_DAMAGE_DONE
    case 87: return "frosplpwr";         // ITEM_MOD_CLASSIC_FROST_DAMAGE_DONE
    case 88: return "shasplpwr";         // ITEM_MOD_CLASSIC_SHADOW_DAMAGE_DONE
    case 89: return "arcsplpwr";         // ITEM_MOD_CLASSIC_ARCANE_DAMAGE_DONE
    case 119:                            // ITEM_MOD_CLASSIC_FIRE_PENETRATION
    case 120:                            // ITEM_MOD_CLASSIC_NATURE_PENETRATION
    case 121:                            // ITEM_MOD_CLASSIC_FROST_PENETRATION
    case 122:                            // ITEM_MOD_CLASSIC_SHADOW_PENETRATION
    case 123:                            // ITEM_MOD_CLASSIC_ARCANE_PENETRATION
        return "spellpenrtng";
    default:
        return nullptr;
    }
}

// The weight scales price crit, hit, dodge, parry and block per percent and
// expertise per point; a ForeverCore item stat carries rating points instead.
// These are the level 60 CombatRatings ratios (14 crit, 10 hit, 12 dodge, 15
// parry, 5 block, 10 expertise rating per point), so dividing the raw item
// stat by them turns the value into the unit the scale was written in.
// 1 means the stat is stored in scale units already (defense skill, block
// value, spell penetration, armor, the +damage/+healing stats).
inline uint32_t ClassicItemStatRatingDivisor(uint32_t statType)
{
    switch (statType)
    {
    case 13: return 12;  // ITEM_MOD_DODGE_RATING
    case 14: return 15;  // ITEM_MOD_PARRY_RATING
    case 15: return 5;   // ITEM_MOD_BLOCK_RATING
    case 16:             // ITEM_MOD_HIT_MELEE_RATING
    case 17:             // ITEM_MOD_HIT_RANGED_RATING
    case 18:             // ITEM_MOD_HIT_SPELL_RATING
    case 31: return 10;  // ITEM_MOD_HIT_RATING
    case 19:             // ITEM_MOD_CRIT_MELEE_RATING
    case 20:             // ITEM_MOD_CRIT_RANGED_RATING
    case 21:             // ITEM_MOD_CRIT_SPELL_RATING
    case 32: return 14;  // ITEM_MOD_CRIT_RATING
    case 37: return 10;  // ITEM_MOD_EXPERTISE_RATING
    default: return 1;
    }
}

} // namespace ai
