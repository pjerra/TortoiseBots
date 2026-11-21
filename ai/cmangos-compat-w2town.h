// Turtle (CMaNGOS/MaNGOS) side of the both-core helpers for W2 (vendor, repair, trainer, mail read).
// ForeverCore side: forever/src/compat/TbCompat_W2Town.h.
#pragma once

#include <vector>

#include "Mail/Mail.h"
#include "playerbot/policy/town/TownErrandPolicy.h"

inline Creature* VendorIfCanInteractCompat(Player* bot, ObjectGuid const& guid)
{
    return bot->GetNPCIfCanInteractWith(guid, UNIT_NPC_FLAG_VENDOR);
}

inline Creature* RepairIfCanInteractCompat(Player* bot, ObjectGuid const& guid)
{
    return bot->GetNPCIfCanInteractWith(guid, UNIT_NPC_FLAG_REPAIR);
}

inline bool HasVendorStockCompat(Creature* vendor)
{
    if (!vendor)
        return false;
    VendorItemData const* items = vendor->GetVendorItems();
    VendorItemData const* templateItems = vendor->GetVendorTemplateItems();
    return (items && !items->Empty()) || (templateItems && !templateItems->Empty());
}

inline VendorItemData const* VendorTemplateItemsCompat(Creature* vendor)
{
    return vendor ? vendor->GetVendorTemplateItems() : nullptr;
}

inline bool SellItemToVendorCompat(Player* bot, Creature* /*vendor*/, ObjectGuid const& vendorguid, Item* item, uint32 count)
{
    if (!bot || !item)
        return false;
    WorldPacket p;
    p << vendorguid << item->getObjectGuid() << count;
    bot->GetSession()->HandleSellItemOpcode(p);
    return true;
}

inline std::vector<VendorItem const*> CollectVendorItemsCompat(Creature* vendor)
{
    std::vector<VendorItem const*> out;
    if (!vendor)
        return out;
    if (VendorItemData const* tItems = vendor->GetVendorItems())
        for (VendorItem* vi : tItems->m_items)
            if (vi)
                out.push_back(vi);
    if (VendorItemData const* vItems = vendor->GetVendorTemplateItems())
        for (VendorItem* vi : vItems->m_items)
            if (vi)
                out.push_back(vi);
    return out;
}

inline bool VendorItemBuyAllowedCompat(Player* /*bot*/, Creature* /*vendor*/, VendorItem const* /*vItem*/)
{
    return true;
}

inline bool BuyItemFromVendorSlotCompat(Player* bot, Creature* /*vendor*/, ObjectGuid const& vendorguid, uint32 /*slot*/, uint32 itemId, uint32 count)
{
    bot->BuyItemFromVendor(vendorguid, itemId, count, NULL_BAG, NULL_SLOT);
    return true;
}

inline void BuyBackItemCompat(Player* bot, ObjectGuid const& vendorguid, uint32 slot)
{
    WorldPacket p1(CMSG_BUYBACK_ITEM);
    p1 << vendorguid;
    p1 << slot;
    bot->GetSession()->HandleBuybackItem(p1);
}

inline uint32 RepairAllCompat(Player* bot, Creature* /*unit*/, float discountMod)
{
    uint32 totalCost = bot->DurabilityRepair((INVENTORY_SLOT_BAG_0 << 8) | EQUIPMENT_SLOT_MAINHAND, true, discountMod);
    totalCost += bot->DurabilityRepair((INVENTORY_SLOT_BAG_0 << 8) | EQUIPMENT_SLOT_RANGED, true, discountMod);
    totalCost += bot->DurabilityRepair((INVENTORY_SLOT_BAG_0 << 8) | EQUIPMENT_SLOT_OFFHAND, true, discountMod);
    totalCost += bot->DurabilityRepairAll(true, discountMod);
    return totalCost;
}

inline uint32 DurabilityCostMultiplierCompat(DurabilityCostsEntry const* dcost, uint32 itemClass, uint32 itemSubClass)
{
    return dcost->multiplier[ItemSubClassToDurabilityMultiplierId(itemClass, itemSubClass)];
}

inline float DurabilityQualityModCompat(DurabilityQualityEntry const* dQualitymodEntry)
{
    return float(dQualitymodEntry->quality_mod);
}

inline bool IsTrainerCompat(Creature const* creature)
{
    return creature && creature->IsTrainer();
}

inline bool IsTrainerOfCompat(Creature* creature, Player* bot)
{
    return creature && creature->IsTrainerOf(bot, false);
}

inline TrainerType GetTrainerTypeCompat(Creature const* creature)
{
    return static_cast<TrainerType>(creature->GetCreatureInfo()->trainer_type);
}

inline std::vector<TrainerSpell> CollectTrainerSpellsCompat(Creature* creature)
{
    std::vector<TrainerSpell> out;
    if (!creature)
        return out;
    TrainerSpellData const* cSpells = creature->GetTrainerSpells();
    TrainerSpellData const* tSpells = creature->GetTrainerTemplateSpells();
    if (cSpells)
        for (auto const& kv : cSpells->spellList)
            out.push_back(kv.second);
    if (tSpells)
        for (auto const& kv : tSpells->spellList)
            out.push_back(kv.second);
    return out;
}

inline bool HasTrainerSpellsCompat(Creature* creature)
{
    if (!creature)
        return false;
    return creature->GetTrainerSpells() || creature->GetTrainerTemplateSpells();
}

inline bool LearnTrainerSpellForeverCompat(Player* /*bot*/, Creature* /*creature*/, uint32 /*spellId*/)
{
    return false;
}

inline bool UsesForeverTrainerCompat() { return false; }

inline bool CanTeachTrainerSpellForeverCompat(Player* /*bot*/, Creature* /*creature*/, TrainerSpell const& /*tSpell*/)
{
    return false;
}

inline bool MailHasTextCompat(Mail const* mail)
{
    return mail && mail->itemTextId != 0;
}

inline bool MailHasItemsCompat(Mail const* mail)
{
    return mail && mail->has_items;
}

inline bool MailIsAuctionCompat(Mail const* mail)
{
    return mail && mail->stationery == MAIL_STATIONERY_AUCTION;
}

inline uint32 BuyBackPriceCompat(Player const* bot, uint32 slot)
{
    return bot->GetUInt32Value(PLAYER_FIELD_BUYBACK_PRICE_1 + slot - BUYBACK_SLOT_START);
}

inline void RemoveFeignDeathCompat(Player* bot)
{
    bot->RemoveSpellsCausingAura(SPELL_AURA_FEIGN_DEATH);
}

inline bool IsPrimaryProfessionFirstRankSpellCompat(uint32 id)
{
    return sSpellMgr.IsPrimaryProfessionFirstRankSpell(id);
}

inline MasterPlayer* MailOwnerCompat(Player* bot)
{
    return bot && bot->GetSession() ? bot->GetSession()->GetMasterPlayer() : nullptr;
}

inline PlayerMails::iterator MailBeginCompat(MasterPlayer* owner) { return owner->GetMailBegin(); }
inline PlayerMails::iterator MailEndCompat(MasterPlayer* owner) { return owner->GetMailEnd(); }
inline Mail* MailByIdCompat(MasterPlayer* owner, uint32 id) { return owner->GetMail(id); }
inline Item* MailItemCompat(MasterPlayer* owner, uint32 itemGuid) { return owner->GetMItem(itemGuid); }

inline std::string MailBodyTextCompat(Mail const* mail)
{
    return mail && mail->itemTextId ? sObjectMgr.GetItemText(mail->itemTextId) : std::string();
}

inline ObjectGuid FindNearestMailboxCompat(Player* /*bot*/, bool* handled = nullptr)
{
    if (handled)
        *handled = false;
    return ObjectGuid();
}

inline bool MailTakeMoneyCompat(Player* bot, ObjectGuid const& mailbox, Mail const* mail)
{
    if (!bot || !bot->GetSession() || !mail)
        return false;
    uint32 const before = bot->GetMoney();
    WorldPacket packet;
    packet << mailbox;
    packet << mail->messageID;
    bot->GetSession()->HandleMailTakeMoney(packet);
    return bot->GetMoney() > before;
}

inline bool MailTakeItemCompat(Player* bot, ObjectGuid const& mailbox, Mail const* mail, uint32 /*itemGuid*/)
{
    if (!bot || !bot->GetSession() || !mail)
        return false;
    WorldPacket packet;
    packet << mailbox;
    packet << mail->messageID;
    bot->GetSession()->HandleMailTakeItem(packet);
    return true;
}

inline void MailDeleteCompat(Player* bot, ObjectGuid const& mailbox, uint32 mailId)
{
    WorldPacket packet;
    packet << mailbox;
    packet << mailId;
    bot->GetSession()->HandleMailDelete(packet);
}

inline bool MailRequiresMailboxCompat()
{
    return false;
}

inline bool MailTakeAfterResultCompat(uint32 confirmedTakes)
{
    return ai::MailTakeAfterResult(false, confirmedTakes);
}
