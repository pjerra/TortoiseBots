// ForeverCore side of the both-core helpers for W2 (vendor, repair, trainer, mail read).
// Turtle side: ai/cmangos-compat-w2town.h.
#pragma once

#include "CreatureData.h"
#include "DB2Structure.h"
#include "Mail.h"
#include "host/TbHostTown.h"
#include "TbItemPrototype.h"

#include <cmath>
#include <vector>

#ifndef TRAINER_TYPE_MOUNTS
#define TRAINER_TYPE_MOUNTS static_cast<TrainerType>(1)
#endif
#ifndef TRAINER_TYPE_TRADESKILLS
#define TRAINER_TYPE_TRADESKILLS static_cast<TrainerType>(2)
#endif
#ifndef TRAINER_TYPE_PETS
#define TRAINER_TYPE_PETS static_cast<TrainerType>(3)
#endif

struct TrainerSpell
{
    uint32 spell = 0;
    uint32 spellCost = 0;
    uint32 reqSkill = 0;
    uint32 reqSkillValue = 0;
    uint32 reqLevel = 0;
};

#define MAIL_STATIONERY_AUCTION MAIL_STATIONERY_NORMAL

inline Creature* VendorIfCanInteractCompat(Player* bot, ObjectGuid const& guid)
{
    return tbhost::town::NpcIfCanInteract(bot, guid, UNIT_NPC_FLAG_VENDOR);
}

inline Creature* RepairIfCanInteractCompat(Player* bot, ObjectGuid const& guid)
{
    return tbhost::town::NpcIfCanInteract(bot, guid, UNIT_NPC_FLAG_REPAIR);
}

inline bool HasVendorStockCompat(Creature* vendor)
{
    return tbhost::town::HasVendorStock(vendor);
}

inline bool SellItemToVendorCompat(Player* bot, Creature* vendor, ObjectGuid const& /*vendorguid*/, Item* item, uint32 count)
{
    return tbhost::town::SellItem(bot, vendor, item, count);
}

inline std::vector<VendorItem const*> CollectVendorItemsCompat(Creature* vendor)
{
    std::vector<VendorItem const*> out;
    if (!vendor)
        return out;
    if (VendorItemData const* tItems = vendor->GetVendorItems())
        for (VendorItem const& vi : tItems->m_items)
            out.push_back(&vi);
    return out;
}

inline bool VendorItemBuyAllowedCompat(Player* bot, Creature* vendor, VendorItem const* vItem)
{
    return vItem && tbhost::town::CheckVendorItemAllowed(bot, vendor, *vItem);
}

inline bool BuyItemFromVendorSlotCompat(Player* bot, Creature* vendor, ObjectGuid const& /*vendorguid*/, uint32 slot, uint32 itemId, uint32 count)
{
    return tbhost::town::BuyItemFromVendorSlot(bot, vendor, slot, itemId, count);
}

inline void BuyBackItemCompat(Player* bot, ObjectGuid const& vendorguid, uint32 slot)
{
    tbhost::town::BuyBackItem(bot, vendorguid, slot);
}

inline uint32 RepairAllCompat(Player* bot, Creature* unit, float discountMod)
{
    return tbhost::town::RepairAll(bot, unit, discountMod);
}

inline uint32 DurabilityCostMultiplierCompat(DurabilityCostsEntry const* dcost, uint32 itemClass, uint32 itemSubClass)
{
    uint32 const idx = ItemSubClassToDurabilityMultiplierId(itemClass, itemSubClass);
    if (idx < 21)
        return dcost->WeaponSubClassCost[idx];
    uint32 const armorIdx = idx - 21;
    if (armorIdx < 8)
        return dcost->ArmorSubClassCost[armorIdx];
    return 0;
}

inline float DurabilityQualityModCompat(DurabilityQualityEntry const* dQualitymodEntry)
{
    return dQualitymodEntry->Data;
}

inline bool IsTrainerCompat(Creature const* creature)
{
    return tbhost::town::IsTrainer(creature);
}

inline bool IsTrainerOfCompat(Creature* creature, Player* bot)
{
    return tbhost::town::IsTrainerOf(creature, bot);
}

inline TrainerType GetTrainerTypeCompat(Creature const* creature)
{
    return static_cast<TrainerType>(tbhost::town::GetTrainerKind(creature));
}

inline std::vector<TrainerSpell> CollectTrainerSpellsCompat(Creature* creature)
{
    std::vector<TrainerSpell> out;
    for (tbhost::town::TrainerSpellView const& sp : tbhost::town::GetTrainerSpells(creature))
    {
        TrainerSpell ts;
        ts.spell = sp.spellId;
        ts.spellCost = sp.moneyCost;
        ts.reqSkill = sp.reqSkillLine;
        ts.reqSkillValue = sp.reqSkillRank;
        ts.reqLevel = sp.reqLevel;
        out.push_back(ts);
    }
    return out;
}

inline bool HasTrainerSpellsCompat(Creature* creature)
{
    return !tbhost::town::GetTrainerSpells(creature).empty();
}

inline bool LearnTrainerSpellForeverCompat(Player* bot, Creature* creature, uint32 spellId)
{
    return tbhost::town::TeachTrainerSpell(bot, creature, spellId);
}

inline bool UsesForeverTrainerCompat() { return true; }

inline bool CanTeachTrainerSpellForeverCompat(Player* bot, Creature* creature, TrainerSpell const& tSpell)
{
    tbhost::town::TrainerSpellView view;
    view.spellId = tSpell.spell;
    view.moneyCost = tSpell.spellCost;
    view.reqSkillLine = tSpell.reqSkill;
    view.reqSkillRank = tSpell.reqSkillValue;
    view.reqLevel = tSpell.reqLevel;
    // Check eligibility without money (discountMod=0) so Learn() can track visitCheapestUnaffordable.
    return tbhost::town::CanTeachTrainerSpell(bot, creature, view, 0.0f);
}

inline bool MailHasTextCompat(Mail const* mail)
{
    return mail && !mail->body.empty();
}

inline bool MailHasItemsCompat(Mail const* mail)
{
    return mail && !mail->items.empty();
}

inline bool MailIsAuctionCompat(Mail const* mail)
{
    return mail && mail->messageType == MAIL_AUCTION;
}

inline uint32 BuyBackPriceCompat(Player const* bot, uint32 slot)
{
    return bot->m_activePlayerData->BuybackPrice[slot - BUYBACK_SLOT_START];
}

inline void RemoveFeignDeathCompat(Player* bot)
{
    bot->RemoveAurasByType(SPELL_AURA_FEIGN_DEATH);
}

inline bool IsPrimaryProfessionFirstRankSpellCompat(uint32)
{
    return false;
}

inline Player* MailOwnerCompat(Player* bot)
{
    return bot;
}

inline PlayerMails::const_iterator MailBeginCompat(Player* owner) { return owner->GetMails().begin(); }
inline PlayerMails::const_iterator MailEndCompat(Player* owner) { return owner->GetMails().end(); }
inline Mail* MailByIdCompat(Player* owner, uint64 id) { return owner->GetMail(id); }
inline Item* MailItemCompat(Player* owner, uint64 itemGuid) { return owner->GetMItem(itemGuid); }

inline std::string MailBodyTextCompat(Mail const* mail)
{
    return mail ? mail->body : std::string();
}

inline void MailTakeMoneyCompat(Player* bot, ObjectGuid const& mailbox, Mail const* mail)
{
    if (mail)
        tbhost::town::TakeMailMoney(bot, mailbox, mail->messageID, mail->money);
}

inline void MailTakeItemCompat(Player* bot, ObjectGuid const& mailbox, Mail const* mail, uint64 itemGuid)
{
    if (mail)
        tbhost::town::TakeMailItem(bot, mailbox, mail->messageID, itemGuid);
}

inline void MailDeleteCompat(Player* bot, ObjectGuid const& /*mailbox*/, uint64 mailId)
{
    tbhost::town::DeleteMail(bot, mailId);
}
