// The disenchant service's mailbox and spell half (TBF #155).
//
// A guild member mails an item to a bot with the subject `disenchant`. The
// bot takes it at a mailbox into a queued row (the guarded store in
// TbHostGuildOrderDisenchantStore.cpp), casts Disenchant on it, takes the
// loot the cast lands, and mails the loot back one item per mail. A refusal
// (no Enchanting, too low, nothing to disenchant) mails the item back with
// the reason instead, so nothing of the member's is eaten.
//
// The cast and the loot go through the core's own handlers, exactly as the
// client would: CMSG_CAST_SPELL with the stack as the item target
// (SpellHandler.cpp:229 -> Spell.cpp:110), the item's loot window through
// CMSG_LOOT_ITEM (LootPackets.h), and the result mails through
// tbhost::guild::SendMail (HandleSendMail, TbHostGuild.cpp:271). The skill
// and loot gates are the core's own (Item::GetDisenchantLootId /
// GetDisenchantSkillRequired, Item.cpp:2491,2521), not TB's item-level
// formula: the cast check in Spell.cpp:8213-8237 reads the same two.
//
// World thread, from BotMgr's per-bot pass beside TickBotOrder and
// TickGuildMail. No database call here: the store API owns every query.
#include "host/TbHostGuildOrderDisenchant.h"

#include "host/TbHostCapitalErrands.h"
#include "host/TbHostGuild.h"
#include "host/TbHostGuildMail.h"
#include "host/TbHostGuildOrders.h"
#include "host/TbHostGuildPure.h"
#include "host/TbHostTown.h"

#include "Bag.h"
#include "CharacterCache.h"
#include "GameTime.h"
#include "GameObject.h"
#include "Guild.h"
#include "GuildMgr.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "Log.h"
#include "Loot.h"
#include "LootPackets.h"
#include "Mail.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "ObjectGuid.h"
#include "ObjectMgr.h"
#include "Opcodes.h"
#include "Player.h"
#include "SharedDefines.h"
#include "SpellPackets.h"
#include "WorldPacket.h"
#include "WorldSession.h"

#include "runtime/GuildOrderDisenchantPolicy.h"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace tbhost
{
namespace guild_orders
{
namespace
{
using TortoiseBots::GuildOrders::DisenchantQueueEntry;
using TortoiseBots::GuildOrders::DisenchantServiceResult;
using TortoiseBots::GuildOrders::DisenchantVerdict;

// The client's Disenchant spell. The core decides whether it may be cast
// (SPELL_EFFECT_DISENCHANT, Spell.cpp:8213-8237); a server whose spell table
// moved refuses the cast and the service mails the item back.
constexpr uint32_t kSpellDisenchant = 13262;

constexpr float kWalkDistYd = 500.0f;
constexpr uint64_t kWalkTimeoutMs = 45000;
constexpr uint64_t kMailRetryMs = 5000;
constexpr uint64_t kScanRetryMs = 2000;
constexpr uint64_t kCastTimeoutMs = 15000;
constexpr uint32_t kMaxCastAttempts = 3;
// One mailbox visit mails this many result stacks before yielding a tick.
constexpr uint32_t kMaxMailsPerVisit = 6;

struct BotState
{
    uint64_t nowMs = 0;
    bool walking = false;
    ObjectGuid mailboxGuid;
    uint64_t walkStartedMs = 0;
    bool castPending = false;
    uint64_t castStartedMs = 0;
    uint32_t castFailures = 0;
    uint64_t castFailureItemGuid = 0;
    uint64_t refuseItemGuidLow = 0; // cast would not start: mail this row back as `cannot disenchant`
    bool mailStalled = false;       // a result mail landed but its row update did not: stop mailing this session
    uint64_t nextScanMs = 0;
    uint64_t nextDebugMs = 0;
    std::unordered_map<uint32_t, uint32_t> bagBefore;
};

std::unordered_map<uint32_t, BotState>& States()
{
    static std::unordered_map<uint32_t, BotState> states;
    return states;
}

void LogRefused(BotState& st, Player* bot, char const* reason)
{
    if (st.nowMs < st.nextDebugMs)
        return;
    st.nextDebugMs = st.nowMs + 60000;
    TC_LOG_DEBUG("tortoisebots", "[W9] disenchant-skip bot={} reason={}", bot->GetName(), reason);
}

// A cast that did not start or produced nothing against the same stack: after
// three the row is mailed back intact, so a core that keeps refusing cannot
// trap the member's item. The counter follows the item, never the bot.
void NoteCastFailure(BotState& st, uint64_t itemGuidLow)
{
    if (st.castFailureItemGuid != itemGuidLow)
    {
        st.castFailureItemGuid = itemGuidLow;
        st.castFailures = 0;
    }
    ++st.castFailures;
    if (st.castFailures >= kMaxCastAttempts)
        st.refuseItemGuidLow = itemGuidLow;
}

std::unordered_map<uint32_t, uint32_t> BagCounts(Player* bot)
{
    std::unordered_map<uint32_t, uint32_t> counts;
    bot->ForEachItem(ItemSearchLocation::Inventory, [&counts](Item* item)
    {
        counts[item->GetEntry()] += item->GetCount();
        return ItemSearchCallbackResult::Continue;
    });
    return counts;
}

uint32_t FreeBagSlots(Player* bot)
{
    uint32_t free = 0;
    for (uint8_t slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
        if (!bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
            ++free;
    for (uint8_t bagSlot = INVENTORY_SLOT_BAG_START; bagSlot < INVENTORY_SLOT_BAG_END; ++bagSlot)
        if (Bag* bag = bot->GetBagByPos(bagSlot))
            free += bag->GetFreeSlots();
    return free;
}

bool IsGuildMember(Player const* bot, uint64_t senderLow)
{
    Guild const* guild = sGuildMgr->GetGuildById(bot->GetGuildId());
    return guild && guild->GetMember(ObjectGuid::Create<HighGuid::Player>(senderLow)) != nullptr;
}

// The core's cast gate, in the same order the effect check reads it. A bot
// that never learned Disenchant or has no Enchanting cannot serve at all.
DisenchantVerdict ClassifyCore(Player* bot, Item* item)
{
    if (!item || !item->GetDisenchantLootId())
        return DisenchantVerdict::CannotDisenchant;
    if (!bot->HasSkill(SKILL_ENCHANTING) || !bot->HasSpell(kSpellDisenchant))
        return DisenchantVerdict::NoSkill;
    Optional<uint16> const required = item->GetDisenchantSkillRequired();
    if (required && *required > bot->GetSkillValue(SKILL_ENCHANTING))
        return DisenchantVerdict::LowSkill;
    return DisenchantVerdict::Ok;
}

Item* QueuedItem(Player* bot, DisenchantQueueEntry const& entry)
{
    Item* item = bot->GetItemByGuid(ObjectGuid::Create<HighGuid::Item>(entry.itemGuidLow));
    if (!item || item->GetEntry() != entry.itemId)
        return nullptr;
    return item;
}

ObjectGuid ItemGuid(DisenchantQueueEntry const& entry)
{
    return ObjectGuid::Create<HighGuid::Item>(entry.itemGuidLow);
}

bool MailItem(Player* bot, ObjectGuid const& mailbox, uint32_t senderLow, Item* item,
    std::string const& subject, std::string const& body)
{
    if (!bot || !item || !senderLow)
        return false;
    std::string recipient;
    if (!sCharacterCache->GetCharacterNameByGuid(ObjectGuid::Create<HighGuid::Player>(senderLow), recipient))
        return false;
    std::vector<ObjectGuid> attachments;
    attachments.push_back(item->GetGUID());
    std::string failReason;
    if (!tbhost::guild::SendMail(bot, mailbox, recipient, subject, body, 0, attachments, &failReason))
    {
        TC_LOG_DEBUG("tortoisebots", "[W9] disenchant-mail bot={} to={} failed={}", bot->GetName(), recipient, failReason);
        return false;
    }
    return true;
}

// A stack holding exactly what is left to mail, splitting one down when the
// bag stack is bigger. False when none of that loot is in the bags any more:
// the caller drops the entry instead of keeping the row forever.
bool FindStackForMail(Player* bot, uint32_t itemId, uint32_t want, Item*& out)
{
    out = nullptr;
    if (!want)
        return false;
    Item* whole = nullptr;
    Item* bigger = nullptr;
    bot->ForEachItem(ItemSearchLocation::Inventory, [&](Item* item)
    {
        if (item->GetEntry() != itemId)
            return ItemSearchCallbackResult::Continue;
        if (item->GetCount() <= want)
        {
            whole = item;
            return ItemSearchCallbackResult::Stop;
        }
        if (!bigger)
            bigger = item;
        return ItemSearchCallbackResult::Continue;
    });
    if (whole)
    {
        out = whole;
        return true;
    }
    if (!bigger)
        return false;

    guild::DeliveryStackPlan const plan = guild::PlanDeliveryStack(bigger->GetCount(), want);
    if (!plan.deliverCount)
        return false;
    if (!plan.needSplit)
    {
        out = bigger;
        return true;
    }

    uint16 const src = uint16(bigger->GetBagSlot() << 8) | bigger->GetSlot();
    auto trySlot = [&](uint8_t dstBag, uint8_t dstSlot) -> bool
    {
        if (bot->GetItemByPos(dstBag, dstSlot))
            return false;
        bot->SplitItem(src, uint16(dstBag << 8) | dstSlot, plan.splitCount);
        Item* split = bot->GetItemByPos(dstBag, dstSlot);
        if (!split || split->GetEntry() != itemId || split->GetCount() != plan.splitCount)
            return false;
        out = split;
        return true;
    };
    for (uint8_t slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
        if (trySlot(INVENTORY_SLOT_BAG_0, slot))
            return true;
    for (uint8_t bagSlot = INVENTORY_SLOT_BAG_START; bagSlot < INVENTORY_SLOT_BAG_END; ++bagSlot)
    {
        if (Bag* bag = bot->GetBagByPos(bagSlot))
            for (uint32_t slot = 0; slot < bag->GetBagSize(); ++slot)
                if (trySlot(bagSlot, static_cast<uint8_t>(slot)))
                    return true;
    }
    return false;
}

// Every unlooted piece of the item's disenchant window, through the core's
// own autostore handler; the window is released only when nothing is left.
void TakeItemLoot(Player* bot, ObjectGuid const& itemGuid)
{
    Loot* loot = bot->GetLootByWorldObjectGUID(itemGuid);
    if (!loot || !bot->GetSession())
        return;
    std::vector<uint8_t> listIds;
    for (LootItem const& lootItem : loot->items)
        if (!lootItem.is_looted)
            listIds.push_back(static_cast<uint8_t>(lootItem.LootListId));
    for (uint8_t listId : listIds)
    {
        Loot* current = bot->GetLootByWorldObjectGUID(itemGuid);
        if (!current)
            break;
        WorldPackets::Loot::LootItem packet{ WorldPacket(CMSG_LOOT_ITEM) };
        WorldPackets::Loot::LootRequest request;
        request.Object = current->GetGUID();
        request.LootListID = listId;
        packet.Loot.push_back(request);
        bot->GetSession()->HandleAutostoreLootItemOpcode(packet);
    }
    if (Loot* rest = bot->GetLootByWorldObjectGUID(itemGuid); rest && rest->isLooted())
        bot->GetSession()->DoLootRelease(rest);
}

// Starts the client's cast on the queued stack. The bot is stopped and
// dismounted first: a moving or mounted bot cannot finish the cast.
bool StartCast(Player* bot, BotState& st, DisenchantQueueEntry const& head, Item* item)
{
    if (!bot->GetSession())
        return false;
    if (bot->isMoving())
        bot->StopMoving();
    if (bot->IsMounted())
        bot->Dismount();

    st.bagBefore = BagCounts(bot);
    WorldPackets::Spells::CastSpell packet{ WorldPacket(CMSG_CAST_SPELL) };
    packet.Cast.SpellID = int32_t(kSpellDisenchant);
    packet.Cast.Target.Flags = TARGET_FLAG_ITEM;
    packet.Cast.Target.Item = item->GetGUID();
    bot->GetSession()->HandleCastSpellOpcode(packet);
    // The core may queue the request behind a global cooldown instead of
    // starting it now (Player::CanExecutePendingSpellCastRequest); CollectCast
    // owns the grace, so a queued request is not counted as a refused cast.
    st.castPending = true;
    st.castStartedMs = st.nowMs;
    if (!bot->IsNonMeleeSpellCast(false))
        TC_LOG_DEBUG("tortoisebots", "[W9] disenchant-cast-queued bot={} item={} guid={}",
            bot->GetName(), head.itemId, head.itemGuidLow);
    return true;
}

// The cast is over (or its window is open): take the loot and record what
// the bags really gained. A save that did not land keeps the snapshot so the
// next tick recomputes the same result instead of losing it.
void CollectCast(Player* bot, BotState& st, DisenchantQueueEntry const& head)
{
    if (bot->IsNonMeleeSpellCast(false))
        return;
    if (st.nowMs - st.castStartedMs >= kCastTimeoutMs)
    {
        st.castPending = false;
        st.bagBefore.clear();
        NoteCastFailure(st, head.itemGuidLow);
        return;
    }

    ObjectGuid const itemGuid = ItemGuid(head);
    if (bot->GetLootByWorldObjectGUID(itemGuid))
        TakeItemLoot(bot, itemGuid);
    if (Loot* rest = bot->GetLootByWorldObjectGUID(itemGuid); rest && !rest->isLooted())
        TC_LOG_ERROR("tortoisebots", "[W9] disenchant-loot bot={} item={} guid={}: the loot window still holds pieces after the take",
            bot->GetName(), head.itemId, head.itemGuidLow);

    bool const itemGone = bot->GetItemByGuid(itemGuid) == nullptr;
    std::unordered_set<uint32_t> queuedItems;
    for (DisenchantQueueEntry const& row : DisenchantQueued(head.botGuidLow))
        queuedItems.insert(row.itemId);

    std::vector<DisenchantServiceResult> results;
    std::unordered_map<uint32_t, uint32_t> const after = BagCounts(bot);
    for (auto const& [itemId, count] : after)
    {
        if (queuedItems.count(itemId))
            continue;
        uint32_t had = 0;
        auto const before = st.bagBefore.find(itemId);
        if (before != st.bagBefore.end())
            had = before->second;
        if (count > had)
            results.push_back({itemId, count - had});
    }

    if (!itemGone && results.empty())
    {
        // A request queued behind a global cooldown (1.5 s at most) may not
        // have run yet; count only after the grace.
        if (st.nowMs - st.castStartedMs < 2500)
            return;
        // The client's cast never landed (interrupted, or the core refused
        // it): retry a bounded number of times, then mail it back.
        st.castPending = false;
        st.bagBefore.clear();
        NoteCastFailure(st, head.itemGuidLow);
        return;
    }

    DisenchantQueueEntry row = head;
    row.pendingResults = true;
    row.results = TortoiseBots::GuildOrders::FormatDisenchantResults(results);
    if (ItemTemplate const* proto = sObjectMgr->GetItemTemplate(row.itemId))
        row.itemName = proto->GetName(LOCALE_enUS);
    if (!SaveDisenchantRow(row))
        return; // keep the snapshot for the retry

    if (itemGone && results.empty())
        TC_LOG_ERROR("tortoisebots", "[W9] disenchant-empty bot={} item={} guid={}: the cast consumed the item but its loot did not reach the bags",
            bot->GetName(), head.itemId, head.itemGuidLow);
    else
        TC_LOG_INFO("tortoisebots", "[W9] disenchant-cast bot={} item={} guid={} loot={}",
            bot->GetName(), head.itemId, head.itemGuidLow, row.results);
    st.castPending = false;
    st.bagBefore.clear();
    st.castFailures = 0;
    st.castFailureItemGuid = 0;
}

// Mails one stack of one result entry and records the remainder before the
// next visit. The row is rewritten after the mail lands; a rewrite that does
// not land stops the service for this bot until a restart, because a stale
// row could mail the bot's own stock next time.
bool MailOneResult(Player* bot, BotState& st, ObjectGuid const& mailbox, DisenchantQueueEntry const& head)
{
    if (st.mailStalled)
        return false;
    std::vector<DisenchantServiceResult> results = TortoiseBots::GuildOrders::ParseDisenchantResults(head.results);
    for (DisenchantServiceResult& result : results)
    {
        if (!result.count)
            continue;
        Item* stack = nullptr;
        if (!FindStackForMail(bot, result.itemId, result.count, stack))
        {
            TC_LOG_ERROR("tortoisebots", "[W9] disenchant-lost bot={} item={} guid={}: {} x{} of the cast's loot is no longer in the bags",
                bot->GetName(), result.itemId, head.itemGuidLow, result.itemId, result.count);
            result.count = 0;
            DisenchantQueueEntry row = head;
            row.results = TortoiseBots::GuildOrders::FormatDisenchantResults(results);
            return SaveDisenchantRow(row);
        }
        uint32_t const mailed = stack->GetCount();
        std::string const subject = TortoiseBots::GuildOrders::DisenchantServiceMailSubject(head.itemName);
        if (!MailItem(bot, mailbox, head.senderGuidLow, stack, subject,
                "Here are the materials from your disenchant request."))
            return false;
        result.count = (mailed >= result.count) ? 0 : result.count - mailed;
        TC_LOG_INFO("tortoisebots", "[W9] disenchant-sent bot={} item={} guid={} stack={} left={}",
            bot->GetName(), result.itemId, head.itemGuidLow, mailed, result.count);
        DisenchantQueueEntry row = head;
        row.results = TortoiseBots::GuildOrders::FormatDisenchantResults(results);
        if (!SaveDisenchantRow(row))
        {
            st.mailStalled = true;
            TC_LOG_ERROR("tortoisebots", "[W9] disenchant-mail bot={} item={} guid={}: the result mail landed but its row did not; the service stops for this bot until restart",
                bot->GetName(), head.itemId, head.itemGuidLow);
            return false;
        }
        return true;
    }
    if (DeleteDisenchantRow(head.botGuidLow, head.itemGuidLow))
    {
        TC_LOG_INFO("tortoisebots", "[W9] disenchant-done bot=#{} item={} guid={}",
            head.botGuidLow, head.itemId, head.itemGuidLow);
        return true;
    }
    return false;
}

void IntakeMail(Player* bot, BotState& st, ObjectGuid const& mailbox);

// The mailbox visit: pending results first, then refusals, then new mail. A
// failure sets the retry gate; a clean visit clears it.
void ServeAtMailbox(Player* bot, BotState& st, ObjectGuid const& mailbox)
{
    uint32_t const botGuidLow = bot->GetGUID().GetCounter();
    bool progress = false;

    for (uint32_t mailed = 0; mailed < kMaxMailsPerVisit; ++mailed)
    {
        std::vector<DisenchantQueueEntry> const rows = DisenchantQueued(botGuidLow);
        if (rows.empty())
            break;
        DisenchantQueueEntry const head = rows.front();
        if (!head.pendingResults)
            break;
        if (!MailOneResult(bot, st, mailbox, head))
        {
            st.nextScanMs = st.nowMs + kMailRetryMs;
            return;
        }
        progress = true;
    }

    for (uint32_t guard = 0; guard < kMaxMailsPerVisit; ++guard)
    {
        std::vector<DisenchantQueueEntry> const rows = DisenchantQueued(botGuidLow);
        if (rows.empty())
            break;
        DisenchantQueueEntry const head = rows.front();
        if (head.pendingResults)
            break;
        Item* item = QueuedItem(bot, head);
        if (!item)
        {
            if (!DeleteDisenchantRow(botGuidLow, head.itemGuidLow))
            {
                st.nextScanMs = st.nowMs + kMailRetryMs;
                return;
            }
            progress = true;
            continue;
        }
        DisenchantVerdict const verdict = (st.refuseItemGuidLow == head.itemGuidLow)
            ? DisenchantVerdict::CannotDisenchant
            : ClassifyCore(bot, item);
        if (verdict == DisenchantVerdict::Ok)
            break;
        std::string const token = TortoiseBots::GuildOrders::DisenchantVerdictToken(verdict);
        std::string const name = item->GetTemplate() ? item->GetTemplate()->GetName(LOCALE_enUS) : std::to_string(head.itemId);
        std::string const body = "Hello,\n\nI could not disenchant this (" + name + "): " + token +
            ".\n\n" + bot->GetName() + "\n";
        if (!MailItem(bot, mailbox, head.senderGuidLow, item, "Disenchant: " + token, body))
        {
            st.nextScanMs = st.nowMs + kMailRetryMs;
            return;
        }
        if (!DeleteDisenchantRow(botGuidLow, head.itemGuidLow))
        {
            st.nextScanMs = st.nowMs + kMailRetryMs;
            return;
        }
        if (st.refuseItemGuidLow == head.itemGuidLow)
            st.refuseItemGuidLow = 0;
        TC_LOG_INFO("tortoisebots", "[W9] disenchant-refuse bot={} item={} guid={} reason={}",
            bot->GetName(), head.itemId, head.itemGuidLow, token);
        progress = true;
    }

    IntakeMail(bot, st, mailbox);
    if (progress)
        st.nextScanMs = 0;
}

// Takes service mail into the bags, one queued row per item, as far as the
// room check and the core's own take handler allow. Mail the store cannot
// queue goes straight back to the member instead of sitting unshielded.
void IntakeMail(Player* bot, BotState& st, ObjectGuid const& mailbox)
{
    uint32_t const botGuidLow = bot->GetGUID().GetCounter();
    if (!IsDisenchantStoreAvailable())
    {
        LogRefused(st, bot, "no-store");
        return;
    }

    uint32_t freeSlots = FreeBagSlots(bot);
    uint32_t waiting = 0;
    for (DisenchantQueueEntry const& row : DisenchantQueued(botGuidLow))
        if (!row.pendingResults)
            ++waiting;

    std::vector<uint64_t> emptyMails;
    bool stop = false;
    time_t const now = GameTime::GetGameTime();
    for (Mail* mail : bot->GetMails())
    {
        if (stop)
            break;
        if (!IsDisenchantServiceMail(bot, mail) || mail->deliver_time > now)
            continue;
        std::vector<MailItemInfo> const attachments = mail->items;
        for (MailItemInfo const& info : attachments)
        {
            if (!TortoiseBots::GuildOrders::DisenchantIntakeHasRoom(freeSlots, waiting))
            {
                stop = true;
                break;
            }
            Item* item = bot->GetMItem(info.item_guid);
            if (!item)
                continue;
            ItemPosCountVec dest;
            if (bot->CanStoreItem(NULL_BAG, NULL_SLOT, dest, item, false) != EQUIP_ERR_OK)
            {
                stop = true;
                break;
            }
            uint32_t const itemId = item->GetEntry();
            ItemTemplate const* proto = item->GetTemplate();
            uint64_t const itemGuid = info.item_guid;
            if (!tbhost::town::TakeMailItem(bot, mailbox, mail->messageID, info.item_guid))
            {
                stop = true;
                break;
            }
            Item* moved = bot->GetItemByGuid(ObjectGuid::Create<HighGuid::Item>(itemGuid));
            if (!moved)
            {
                TC_LOG_ERROR("tortoisebots", "[W9] disenchant-take bot={} mail={} attach={}: the take landed but the item is not in the bags",
                    bot->GetName(), mail->messageID, itemGuid);
                stop = true;
                break;
            }
            DisenchantQueueEntry row;
            row.botGuidLow = botGuidLow;
            row.senderGuidLow = uint32_t(mail->sender);
            row.itemGuidLow = itemGuid;
            row.itemId = itemId;
            if (proto)
                row.itemName = proto->GetName(LOCALE_enUS);
            if (!NoteDisenchantQueued(row))
            {
                std::string const name = row.itemName.empty() ? std::to_string(itemId) : row.itemName;
                std::string const body = "Hello,\n\nI could not queue this (" + name +
                    ") for disenchanting: cannot disenchant.\n\n" + bot->GetName() + "\n";
                if (!MailItem(bot, mailbox, row.senderGuidLow, moved, "Disenchant: cannot disenchant", body))
                    TC_LOG_ERROR("tortoisebots", "[W9] disenchant-take bot={} item={} guid={}: the queue row did not store and the item could not be mailed back",
                        bot->GetName(), itemId, itemGuid);
                stop = true;
                break;
            }
            TC_LOG_INFO("tortoisebots", "[W9] disenchant-intake bot={} item={} guid={} sender={}",
                bot->GetName(), itemId, itemGuid, row.senderGuidLow);
            if (freeSlots)
                --freeSlots;
            ++waiting;
        }
        if (!stop && mail->items.empty() && mail->money == 0)
            emptyMails.push_back(mail->messageID);
    }
    for (uint64_t mailId : emptyMails)
        tbhost::town::DeleteMail(bot, mailId);
}

bool HasServiceMail(Player* bot)
{
    time_t const now = GameTime::GetGameTime();
    for (Mail* mail : bot->GetMails())
        if (IsDisenchantServiceMail(bot, mail) && mail->deliver_time <= now)
            return true;
    return false;
}

void GoToMailbox(Player* bot, BotState& st)
{
    GameObject* mailbox = tbhost::town::FindNearestMailbox(bot);
    if (mailbox)
    {
        st.walking = false;
        ServeAtMailbox(bot, st, mailbox->GetGUID());
        return;
    }
    mailbox = tbhost::town::FindNearestMailbox(bot, kWalkDistYd);
    if (!mailbox)
    {
        LogRefused(st, bot, "no-mailbox");
        st.nextScanMs = st.nowMs + kScanRetryMs;
        return;
    }
    if (bot->isMoving())
        bot->StopMoving();
    st.walking = true;
    st.mailboxGuid = mailbox->GetGUID();
    st.walkStartedMs = st.nowMs;
    bot->GetMotionMaster()->MovePoint(0, mailbox->GetPositionX(), mailbox->GetPositionY(), mailbox->GetPositionZ());
}

} // namespace

bool IsDisenchantServiceMail(Player* bot, Mail* mail)
{
    if (!bot || !mail || !bot->GetGuildId())
        return false;
    if (mail->state == MAIL_STATE_DELETED || mail->messageType != MAIL_NORMAL ||
        mail->stationery == MAIL_STATIONERY_AUCTION)
        return false;
    if (mail->COD != 0 || mail->items.empty())
        return false;
    if (!TortoiseBots::GuildOrders::IsDisenchantServiceSubject(mail->subject))
        return false;
    return IsGuildMember(bot, mail->sender);
}

void TickDisenchantService(Player* bot, uint32_t diffMs)
{
    if (!bot || !bot->IsInWorld() || !bot->FindMap() || bot->IsBeingTeleported() || !bot->GetGuildId())
        return;
    uint32_t const botGuidLow = bot->GetGUID().GetCounter();
    BotState& st = States()[botGuidLow];
    st.nowMs += diffMs;
    if (!bot->IsAlive())
        return;

    if (st.walking)
    {
        if (bot->IsInCombat() || bot->duel != nullptr || st.nowMs - st.walkStartedMs >= kWalkTimeoutMs)
        {
            st.walking = false;
            st.nextScanMs = st.nowMs + kMailRetryMs;
            return;
        }
        GameObject* mailbox = ObjectAccessor::GetGameObject(*bot, st.mailboxGuid);
        if (!mailbox || !mailbox->IsInWorld() || mailbox->GetMap() != bot->FindMap())
        {
            st.walking = false;
            st.nextScanMs = st.nowMs + kMailRetryMs;
            return;
        }
        if (bot->GetGameObjectIfCanInteractWith(mailbox->GetGUID(), GAMEOBJECT_TYPE_MAILBOX))
        {
            bot->StopMoving();
            st.walking = false;
            ServeAtMailbox(bot, st, mailbox->GetGUID());
            return;
        }
        if (!bot->isMoving())
            bot->GetMotionMaster()->MovePoint(0, mailbox->GetPositionX(), mailbox->GetPositionY(), mailbox->GetPositionZ());
        return;
    }

    if (bot->IsInCombat())
        return;
    // Another host errand or a running guild order owns this bot's walk; the
    // queued mail waits for the next free tick.
    if (tbhost::guild_mail::HasActiveErrand(bot) || tbhost::errands::HasActiveErrand(bot) ||
        FindOrder(botGuidLow).has_value())
        return;

    if (st.castPending)
    {
        std::vector<DisenchantQueueEntry> const rows = DisenchantQueued(botGuidLow);
        if (rows.empty())
        {
            st.castPending = false;
            st.bagBefore.clear();
            return;
        }
        CollectCast(bot, st, rows.front());
        return;
    }

    std::vector<DisenchantQueueEntry> const rows = DisenchantQueued(botGuidLow);
    if (!rows.empty())
    {
        DisenchantQueueEntry const head = rows.front();
        if (head.pendingResults)
        {
            if (st.nowMs >= st.nextScanMs)
                GoToMailbox(bot, st);
            return;
        }
        Item* item = QueuedItem(bot, head);
        if (!item)
        {
            if (DeleteDisenchantRow(botGuidLow, head.itemGuidLow))
                TC_LOG_INFO("tortoisebots", "[W9] disenchant-drop bot={} item={} guid={}: the item is no longer in the bags",
                    bot->GetName(), head.itemId, head.itemGuidLow);
            return;
        }
        if (st.refuseItemGuidLow == head.itemGuidLow)
        {
            if (st.nowMs >= st.nextScanMs)
                GoToMailbox(bot, st);
            return;
        }
        if (ClassifyCore(bot, item) != DisenchantVerdict::Ok)
        {
            if (st.nowMs >= st.nextScanMs)
                GoToMailbox(bot, st);
            return;
        }
        if (!TortoiseBots::GuildOrders::DisenchantCastReady(bot->IsInCombat(), false, false,
                bot->IsNonMeleeSpellCast(false), bot->GetTrader() != nullptr, true))
            return;
        if (!StartCast(bot, st, head, item))
            NoteCastFailure(st, head.itemGuidLow);
        return;
    }

    if (st.nowMs < st.nextScanMs)
        return;
    if (!IsDisenchantStoreAvailable())
    {
        LogRefused(st, bot, "no-store");
        st.nextScanMs = st.nowMs + kScanRetryMs;
        return;
    }
    if (!HasServiceMail(bot))
    {
        st.nextScanMs = st.nowMs + kScanRetryMs;
        return;
    }
    GoToMailbox(bot, st);
}

bool HasActiveDisenchantErrand(Player* bot)
{
    if (!bot)
        return false;
    auto const found = States().find(bot->GetGUID().GetCounter());
    return found != States().end() && (found->second.walking || found->second.castPending);
}

void DisenchantForgetBot(uint32_t botGuidLow)
{
    States().erase(botGuidLow);
}

} // namespace guild_orders
} // namespace tbhost
