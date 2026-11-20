#include "host/TbHostTown.h"

#include "ConditionMgr.h"
#include "Creature.h"
#include "CreatureData.h"
#include "Item.h"
#include "ItemPackets.h"
#include "ItemTemplate.h"
#include "Log.h"
#include "Mail.h"
#include "MailPackets.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Trainer.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include "playerbot/policy/town/TownErrandPolicy.h"

#include <algorithm>
#include <cmath>
#include <ctime>

namespace tbhost
{
namespace town
{

void LogErrand(char const* verb, Player const* bot, Creature const* npc, uint64_t copperBefore, uint64_t copperAfter, std::string const& detail)
{
    uint32_t const npcEntry = npc ? npc->GetEntry() : 0;
    char const* botName = bot ? bot->GetName().c_str() : "unknown";
    if (detail.empty())
    {
        TC_LOG_INFO("tortoisebots", "[W2] {} bot={} npc={} copper_before={} copper_after={}",
            verb, botName, npcEntry, copperBefore, copperAfter);
    }
    else
    {
        TC_LOG_INFO("tortoisebots", "[W2] {} bot={} npc={} copper_before={} copper_after={} {}",
            verb, botName, npcEntry, copperBefore, copperAfter, detail);
    }
}

Creature* NpcIfCanInteract(Player* bot, ObjectGuid const& guid, uint32_t npcFlags)
{
    if (!bot || !bot->IsInWorld())
        return nullptr;
    return bot->GetNPCIfCanInteractWith(guid, static_cast<NPCFlags>(npcFlags), UNIT_NPC_FLAG_2_NONE);
}

bool HasVendorStock(Creature const* vendor)
{
    if (!vendor)
        return false;
    VendorItemData const* items = vendor->GetVendorItems();
    return items && !items->Empty();
}

bool SellItem(Player* bot, Creature* vendor, Item* item, uint32_t count, std::string* failReason)
{
    if (!bot || !vendor || !item)
    {
        if (failReason)
            *failReason = "invalid-arg";
        return false;
    }

    if (Optional<SellResult> check = bot->CanSellItemToVendor(item, count))
    {
        if (failReason)
            *failReason = "sell-refused:" + std::to_string(static_cast<uint32_t>(*check));
        LogErrand("sell-refused", bot, vendor, bot->GetMoney(), bot->GetMoney(),
            "item=" + std::to_string(item->GetEntry()) + " code=" + std::to_string(static_cast<uint32_t>(*check)));
        return false;
    }

    uint64_t const before = bot->GetMoney();
    uint32_t const entry = item->GetEntry();
    Optional<SellResult> res = bot->SellItemToVendor(item, count);
    if (res.has_value())
    {
        if (failReason)
            *failReason = "sell-failed:" + std::to_string(static_cast<uint32_t>(*res));
        LogErrand("sell-refused", bot, vendor, before, bot->GetMoney(),
            "item=" + std::to_string(entry) + " code=" + std::to_string(static_cast<uint32_t>(*res)));
        return false;
    }

    LogErrand("sell", bot, vendor, before, bot->GetMoney(),
        "item=" + std::to_string(entry) + " count=" + std::to_string(count));
    return true;
}

bool CheckVendorItemAllowed(Player* bot, Creature const* vendor, VendorItem const& vItem, std::string* refusalReason)
{
    if (!bot || !vendor)
    {
        if (refusalReason)
            *refusalReason = "invalid-arg";
        return false;
    }

    ItemTemplate const* proto = sObjectMgr->GetItemTemplate(vItem.item);
    if (!proto)
    {
        if (refusalReason)
            *refusalReason = "unknown-item";
        return false;
    }

    bool const meetsPlayerCond = vItem.PlayerConditionId == 0 || bot->MeetPlayerCondition(vItem.PlayerConditionId);
    bool const meetsVendorCond = sConditionMgr->IsObjectMeetingVendorItemConditions(
        vendor->GetEntry(), vItem.item, bot, const_cast<Creature*>(vendor));
    bool const repOk = !proto->GetRequiredReputationFaction() ||
        uint32_t(bot->GetReputationRank(proto->GetRequiredReputationFaction())) >= proto->GetRequiredReputationRank();
    uint32_t const currentCount = vItem.maxcount != 0 ? const_cast<Creature*>(vendor)->GetVendorItemCurrentCount(&vItem) : 1;

    std::string reason;
    if (!ai::VendorItemPickAllowed(vItem.PlayerConditionId, meetsPlayerCond, vItem.ExtendedCost,
            meetsVendorCond, repOk, vItem.maxcount, currentCount, &reason))
    {
        if (refusalReason)
            *refusalReason = reason;
        LogErrand("buy-refused", bot, vendor, bot->GetMoney(), bot->GetMoney(),
            "item=" + std::to_string(vItem.item) + " reason=" + reason);
        return false;
    }

    return true;
}

bool BuyItemFromVendorSlot(Player* bot, Creature* vendor, uint32_t vendorSlot, uint32_t itemId, uint32_t count, std::string* refusalReason)
{
    if (!bot || !vendor)
    {
        if (refusalReason)
            *refusalReason = "invalid-arg";
        return false;
    }

    VendorItemData const* vItems = vendor->GetVendorItems();
    if (!vItems || vendorSlot >= vItems->GetItemCount())
    {
        if (refusalReason)
            *refusalReason = "bad-slot";
        return false;
    }

    VendorItem const* vItem = vItems->GetItem(vendorSlot);
    if (!vItem || vItem->item != itemId)
    {
        if (refusalReason)
            *refusalReason = "slot-mismatch";
        return false;
    }

    if (!CheckVendorItemAllowed(bot, vendor, *vItem, refusalReason))
        return false;

    uint64_t const beforeMoney = bot->GetMoney();
    uint32_t const beforeCount = bot->GetItemCount(itemId, false);

    // The core's return value is "the vendor row has a limited stock", not "the purchase went through" (Player.cpp, end of
    // BuyItemFromVendorSlot): the item count is the proof.
    bot->BuyItemFromVendorSlot(vendor->GetGUID(), vendorSlot, itemId, count, NULL_BAG, NULL_SLOT);
    uint32_t const afterCount = bot->GetItemCount(itemId, false);
    if (afterCount <= beforeCount)
    {
        if (refusalReason)
            *refusalReason = "buy-failed";
        LogErrand("buy-refused", bot, vendor, beforeMoney, bot->GetMoney(),
            "item=" + std::to_string(itemId) + " reason=core-refused");
        return false;
    }

    LogErrand("buy", bot, vendor, beforeMoney, bot->GetMoney(),
        "item=" + std::to_string(itemId) + " count=" + std::to_string(afterCount - beforeCount));
    return true;
}

bool BuyBackItem(Player* bot, ObjectGuid const& vendorGuid, uint32_t slot)
{
    if (!bot || !bot->GetSession() || !bot->IsInWorld())
        return false;
    WorldPackets::Item::BuyBackItem packet{ WorldPacket(CMSG_BUY_BACK_ITEM) };
    packet.VendorGUID = vendorGuid;
    packet.Slot = slot;
    bot->GetSession()->HandleBuybackItem(packet);
    return bot->GetItemFromBuyBackSlot(slot) == nullptr;
}

uint32_t RepairAll(Player* bot, Creature* vendor, float discountMod)
{
    if (!bot || !vendor)
        return 0;

    uint64_t const before = bot->GetMoney();
    bot->DurabilityRepair((INVENTORY_SLOT_BAG_0 << 8) | EQUIPMENT_SLOT_MAINHAND, true, discountMod);
    bot->DurabilityRepair((INVENTORY_SLOT_BAG_0 << 8) | EQUIPMENT_SLOT_RANGED, true, discountMod);
    bot->DurabilityRepair((INVENTORY_SLOT_BAG_0 << 8) | EQUIPMENT_SLOT_OFFHAND, true, discountMod);
    bot->DurabilityRepairAll(true, discountMod, false);
    uint64_t const after = bot->GetMoney();
    uint32_t const spent = before > after ? static_cast<uint32_t>(before - after) : 0;
    LogErrand("repair", bot, vendor, before, after);
    return spent;
}

uint8_t CalcInventoryDurabilityPct(Player const* bot)
{
    if (!bot)
        return 100;
    uint32_t totalCur = 0;
    uint32_t totalMax = 0;
    for (uint8_t slot = EQUIPMENT_SLOT_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
    {
        Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        if (!item)
            continue;
        uint32_t const maxDur = *item->m_itemData->MaxDurability;
        if (!maxDur)
            continue;
        totalMax += maxDur;
        totalCur += *item->m_itemData->Durability;
    }
    if (totalMax == 0)
        return 100;
    return static_cast<uint8_t>((totalCur * 100) / totalMax);
}

bool IsTrainer(Creature const* creature)
{
    return creature && creature->GetTrainerId() != 0 && sObjectMgr->GetTrainer(creature->GetTrainerId()) != nullptr;
}

bool IsTrainerOf(Creature const* creature, Player const* bot)
{
    if (!creature || !bot)
        return false;
    uint32_t const trainerId = creature->GetTrainerId();
    Trainer::Trainer const* trainer = trainerId ? sObjectMgr->GetTrainer(trainerId) : nullptr;
    if (!trainer)
        return false;
    if (trainer->GetType() == Trainer::Type::None)
        return (trainer->GetClassMask() & bot->GetClassMask()) != 0;
    return true;
}

TrainerKind GetTrainerKind(Creature const* creature)
{
    if (!creature)
        return TrainerKind::Class;
    uint32_t const trainerId = creature->GetTrainerId();
    Trainer::Trainer const* trainer = trainerId ? sObjectMgr->GetTrainer(trainerId) : nullptr;
    if (!trainer)
        return TrainerKind::Class;
    return static_cast<TrainerKind>(trainer->GetType());
}

std::vector<TrainerSpellView> GetTrainerSpells(Creature const* creature)
{
    std::vector<TrainerSpellView> out;
    if (!creature)
        return out;
    uint32_t const trainerId = creature->GetTrainerId();
    Trainer::Trainer const* trainer = trainerId ? sObjectMgr->GetTrainer(trainerId) : nullptr;
    if (!trainer)
        return out;
    std::vector<Trainer::Spell> const& spells = trainer->GetSpells();
    out.reserve(spells.size());
    for (Trainer::Spell const& sp : spells)
    {
        TrainerSpellView view;
        view.spellId = sp.SpellId;
        view.moneyCost = sp.MoneyCost;
        view.reqSkillLine = sp.ReqSkillLine;
        view.reqSkillRank = sp.ReqSkillRank;
        view.reqAbility = sp.ReqAbility;
        view.reqLevel = sp.ReqLevel;
        out.push_back(view);
    }
    return out;
}

bool CanTeachTrainerSpell(Player* bot, Creature* creature, TrainerSpellView const& spell, float discountMod, std::string* refusalReason)
{
    if (!bot || !creature)
        return false;
    SpellInfo const* si = sSpellMgr->GetSpellInfo(spell.spellId, DIFFICULTY_NONE);
    if (!si)
    {
        if (refusalReason)
            *refusalReason = "unknown-spell";
        return false;
    }

    bool const known = bot->HasSpell(spell.spellId);
    bool const classRaceOk = bot->IsSpellFitByClassAndRace(spell.spellId);
    bool prereqsOk = true;
    if (spell.reqSkillLine && bot->GetSkillValue(spell.reqSkillLine) < spell.reqSkillRank)
        prereqsOk = false;
    for (uint32_t req : spell.reqAbility)
    {
        if (req && !bot->HasSpell(req))
        {
            prereqsOk = false;
            break;
        }
    }

    return ai::TrainerRankDueAndAffordable(
        known,
        bot->GetLevel(),
        spell.reqLevel,
        classRaceOk,
        prereqsOk,
        spell.moneyCost,
        discountMod,
        static_cast<uint32_t>(bot->GetMoney()),
        refusalReason);
}

bool TeachTrainerSpell(Player* bot, Creature* creature, uint32_t spellId, std::string* refusalReason)
{
    if (!bot || !creature)
        return false;
    uint32_t const trainerId = creature->GetTrainerId();
    Trainer::Trainer const* trainer = trainerId ? sObjectMgr->GetTrainer(trainerId) : nullptr;
    if (!trainer)
    {
        if (refusalReason)
            *refusalReason = "no-trainer";
        return false;
    }

    uint64_t const beforeMoney = bot->GetMoney();
    bool const hadBefore = bot->HasSpell(spellId);
    trainer->TeachSpell(creature, bot, spellId);
    bool const hasAfter = bot->HasSpell(spellId);
    uint64_t const afterMoney = bot->GetMoney();

    if ((hasAfter && !hadBefore) || afterMoney < beforeMoney)
    {
        LogErrand("train", bot, creature, beforeMoney, afterMoney, "spell=" + std::to_string(spellId));
        return true;
    }

    if (refusalReason)
        *refusalReason = "core-refused";
    LogErrand("train-refused", bot, creature, beforeMoney, afterMoney, "spell=" + std::to_string(spellId));
    return false;
}

std::vector<MailView> GetMails(Player* bot)
{
    std::vector<MailView> out;
    if (!bot)
        return out;
    PlayerMails const& mails = bot->GetMails();
    out.reserve(mails.size());
    for (Mail const* m : mails)
    {
        if (!m)
            continue;
        MailView view;
        view.messageId = m->messageID;
        view.messageType = m->messageType;
        view.stationery = m->stationery;
        view.sender = m->sender;
        view.subject = m->subject;
        view.body = m->body;
        view.money = m->money;
        view.hasItems = !m->items.empty();
        view.deliverTime = m->deliver_time;
        view.checked = m->checked;
        view.state = static_cast<uint8_t>(m->state);
        for (MailItemInfo const& info : m->items)
            view.items.emplace_back(info.item_guid, info.item_template);
        out.push_back(std::move(view));
    }
    return out;
}

Item* GetMailItem(Player* bot, uint64_t itemGuid)
{
    return bot ? bot->GetMItem(itemGuid) : nullptr;
}

uint32_t GetUnreadMailCount(Player* bot)
{
    if (!bot)
        return 0;
    time_t const now = std::time(nullptr);
    uint32_t count = 0;
    for (Mail const* m : bot->GetMails())
    {
        if (!m || m->state == MAIL_STATE_DELETED || now < m->deliver_time)
            continue;
        if ((m->checked & MAIL_CHECK_MASK_READ) == 0 || m->money > 0 || !m->items.empty())
            ++count;
    }
    return count;
}

bool TakeMailMoney(Player* bot, ObjectGuid const& mailboxGuid, uint64_t mailId, uint64_t money)
{
    if (!bot || !bot->GetSession() || !bot->IsInWorld())
        return false;
    uint64_t const before = bot->GetMoney();
    WorldPackets::Mail::MailTakeMoney packet{ WorldPacket(CMSG_MAIL_TAKE_MONEY) };
    packet.Mailbox = mailboxGuid;
    packet.MailID = mailId;
    packet.Money = money;
    bot->GetSession()->HandleMailTakeMoney(packet);
    uint64_t const after = bot->GetMoney();
    LogErrand("mail", bot, nullptr, before, after, "take=money mail=" + std::to_string(mailId));
    return after > before;
}

bool TakeMailItem(Player* bot, ObjectGuid const& mailboxGuid, uint64_t mailId, uint64_t attachId)
{
    if (!bot || !bot->GetSession() || !bot->IsInWorld())
        return false;
    uint64_t const before = bot->GetMoney();
    WorldPackets::Mail::MailTakeItem packet{ WorldPacket(CMSG_MAIL_TAKE_ITEM) };
    packet.Mailbox = mailboxGuid;
    packet.MailID = mailId;
    packet.AttachID = attachId;
    bot->GetSession()->HandleMailTakeItem(packet);
    LogErrand("mail", bot, nullptr, before, bot->GetMoney(),
        "take=item mail=" + std::to_string(mailId) + " attach=" + std::to_string(attachId));
    return true;
}

bool DeleteMail(Player* bot, uint64_t mailId)
{
    if (!bot || !bot->GetSession() || !bot->IsInWorld())
        return false;
    WorldPackets::Mail::MailDelete packet{ WorldPacket(CMSG_MAIL_DELETE) };
    packet.MailID = mailId;
    packet.DeleteReason = 0;
    bot->GetSession()->HandleMailDelete(packet);
    return true;
}

} // namespace town
} // namespace tbhost
