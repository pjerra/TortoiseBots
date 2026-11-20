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


// equip
#include "ItemPackets.h"
#include "runtime/CommandedGroupQuery.h"

#define EQUIP_ERR_INVENTORY_FULL EQUIP_ERR_INV_FULL

// Auto-equip an item from a bag slot through the core's typed CMSG_AUTOEQUIP_ITEM handler.
inline void AutoEquipCompat(Player* bot, uint8 bagIndex, uint8 slot)
{
    if (!bot || !bot->GetSession() || !bot->IsInWorld())
        return;
    WorldPackets::Item::AutoEquipItem packet{ WorldPacket(CMSG_AUTO_EQUIP_ITEM) };
    packet.PackSlot = bagIndex;
    packet.Slot = slot;
    packet.Inv.Items.push_back({ bagIndex, slot });
    bot->GetSession()->HandleAutoEquipItemOpcode(packet);
}

// On ForeverCore, Player::CanUseItem(ItemTemplate const*) skips weapon skills and armor proficiency
// (those are only in Player::CanUseItem(Item const*)). Check ItemTemplate::GetSkill() as well so
// both bag items and template-only queries enforce proficiency.
inline InventoryResult CanUseItemForEquipCompat(Player const* bot, ItemPrototype const* proto)
{
    if (!bot || !proto)
        return EQUIP_ERR_ITEM_NOT_FOUND;
    ItemTemplate const* tpl = GetItemTemplateCompat(proto);
    InventoryResult const res = bot->CanUseItem(tpl);
    if (res != EQUIP_ERR_OK)
        return res;
    if (tpl)
    {
        uint32 const itemSkill = tpl->GetSkill();
        if (itemSkill != 0 && bot->GetSkillValue(itemSkill) == 0)
            return EQUIP_ERR_PROFICIENCY_NEEDED;
    }
    return EQUIP_ERR_OK;
}

// D4: auto-equip is ON for free bots only; a bot bound in a commanded/run party never auto-equips.
inline bool MayAutoEquipUpgradesCompat(Player* bot)
{
    return bot && !TortoiseBots::BotCommands::IsInCommandedGroup(bot);
}
