// Turtle side of the both-core helpers for W6a (item weights and auto-equip).
// ForeverCore side: forever/src/compat/TbCompat_W6.h.
#pragma once

#include "cmangos-compat-s5.h"

// weights
inline std::unique_ptr<QueryResult> WeightScaleQueryCompat(char const* sql, bool* schemaMissing = nullptr)
{
    return WorldQueryCompat(sql, schemaMissing);
}

inline uint32 EnchantEffectTypeCompat(SpellItemEnchantmentEntry const* e, int s) { return e->type[s]; }
inline int32 EnchantEffectAmountCompat(SpellItemEnchantmentEntry const* e, int s) { return e->amount[s]; }
inline uint32 EnchantEffectSpellCompat(SpellItemEnchantmentEntry const* e, int s) { return e->spellid[s]; }
inline uint32 RandomPropertyRawIdCompat(ItemPrototype const* proto) { return proto ? proto->RandomProperty : 0; }
inline uint32 RandomPropertyModIdCompat(ItemPrototype const* proto)
{
    return (proto && proto->RandomProperty) ? GetItemEnchantMod(proto->RandomProperty) : 0;
}
inline uint32 RandomPropertyEnchantIdCompat(uint32 propId, uint8 slot)
{
    ItemRandomPropertiesEntry const* entry = sItemRandomPropertiesStore.LookupEntry(propId);
    return entry ? entry->enchant_id[slot] : 0;
}

// equip
inline void AutoEquipCompat(Player* bot, uint8 bagIndex, uint8 slot)
{
    WorldPacket packet(CMSG_AUTOEQUIP_ITEM, 2);
    packet << bagIndex << slot;
    bot->GetSession()->HandleAutoEquipItemOpcode(packet);
}

inline InventoryResult CanUseItemForEquipCompat(Player const* bot, ItemPrototype const* proto)
{
    return bot->CanUseItem(proto);
}

inline bool MayAutoEquipUpgradesCompat(Player* /*bot*/)
{
    return true;
}
