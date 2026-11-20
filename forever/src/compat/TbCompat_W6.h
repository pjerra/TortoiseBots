// ForeverCore side of the both-core helpers for W6a (item weights and auto-equip).
// Turtle side: ai/cmangos-compat-w6.h.
#pragma once

#include "TbCompat_S5.h"

// weights
#define SPELL_AURA_MOD_CRIT_PERCENT SPELL_AURA_MOD_CRIT_PCT

inline QueryResult WeightScaleQueryCompat(char const* sql, bool* schemaMissing = nullptr)
{
    return WorldQueryCompat(sql, schemaMissing);
}

inline uint32 EnchantEffectTypeCompat(SpellItemEnchantmentEntry const* e, int s) { return e->Effect[s]; }
inline int32 EnchantEffectAmountCompat(SpellItemEnchantmentEntry const* e, int s) { return e->EffectPointsMin[s]; }
inline uint32 EnchantEffectSpellCompat(SpellItemEnchantmentEntry const* e, int s) { return e->EffectArg[s]; }
inline uint32 RandomPropertyRawIdCompat(ItemPrototype const*) { return 0; }
inline uint32 RandomPropertyModIdCompat(ItemPrototype const*) { return 0; }
inline uint32 RandomPropertyEnchantIdCompat(uint32, uint8) { return 0; }
