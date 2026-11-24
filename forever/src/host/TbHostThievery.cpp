// Host thievery (TBF #153): the three rogue services on the per-bot tick.
//
// Pickpocket: Stealth 1784 then Pick Pocket 921 on a nearby humanoid whose
// pickpocketing loot is still there (Creature::CanGeneratePickPocketLoot,
// Creature.cpp:3930), then the loot window the core opened
// (Spell::EffectPickPocket, SpellEffects.cpp:2352). Lockbox: Pick Lock 1804
// on a bag item (Spell::EffectOpenLock, SpellEffects.cpp:1730, clears
// ITEM_FIELD_FLAG_UNLOCKED at :1793), then CMSG_OPEN_ITEM
// (WorldSession::HandleOpenItemOpcode, SpellHandler.cpp:66) and the loot
// window behind it. Lockbox mail: a guild bot mails a box it cannot open to
// the guild's best rogue bot (online or offline), the rogue opens it at a
// mailbox and mails the contents back; the core charges the 30-copper
// postage per mail (MailHandler.cpp:171) and the box's own coins cover it.
//
// The gates are in ai/playerbot/policy/profession/ThieveryPolicy.h
// (core-free, standalone-tested). Nothing here is stored: the mailbox is the
// queue until the rogue takes the box, and the walk/cast state is memory.
// World thread, from BotMgr's per-bot pass beside the other host services.
#include "host/TbHostThievery.h"

#include "TbBotMgr.h"
#include "runtime/GuildOrderContract.h"
#include "host/TbHostCapitalErrands.h"
#include "host/TbHostGuild.h"
#include "host/TbHostGuildMail.h"
#include "host/TbHostGuildOrderDisenchant.h"
#include "host/TbHostGuildOrderEnchant.h"
#include "host/TbHostGuildOrders.h"
#include "host/TbHostGuildPure.h"
#include "host/TbHostProfession.h"
#include "host/TbHostTown.h"
#include "host/pool/TbPoolRegistry.h"
#include "runtime/AutoToolsKitPolicy.h"

#include "playerbot/policy/profession/ThieveryPolicy.h"
#include "playerbot/policy/world/PoolRegistryPolicy.h"

#include "AccountMgr.h"
#include "Bag.h"
#include "CellImpl.h"
#include "CharacterCache.h"
#include "Config.h"
#include "Creature.h"
#include "DB2Stores.h"
#include "GameObject.h"
#include "GameTime.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Guild.h"
#include "GuildMgr.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "Log.h"
#include "Loot.h"
#include "LootMgr.h"
#include "LootPackets.h"
#include "Mail.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "ObjectGuid.h"
#include "Player.h"
#include "SharedDefines.h"
#include "SpellDefines.h"
#include "SpellPackets.h"
#include "WorldPacket.h"
#include "WorldSession.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <list>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace tbhost
{
namespace thievery
{

using namespace TortoiseBots::prof;

namespace
{

std::atomic<bool> g_pickpocket{ true };
std::atomic<bool> g_lockboxes{ true };
std::atomic<bool> g_lockboxMail{ true };

constexpr uint64_t kPickScanIntervalMs = 3000;
constexpr uint64_t kBoxScanIntervalMs = 10000;
constexpr uint64_t kMailScanIntervalMs = 20000;
constexpr uint64_t kMailRetryMs = 5000;
constexpr uint64_t kDebugIntervalMs = 60000;
constexpr uint64_t kCastGraceMs = 2500;
constexpr uint64_t kWalkTimeoutMs = 45000;
constexpr uint64_t kBoxErrandTimeoutMs = 60000;
constexpr uint64_t kPickErrandTimeoutMs = 25000;
constexpr uint64_t kPickBackoffMs = 60000;
constexpr uint64_t kStealthRetryMs = 1200;
constexpr float kPickSearchYd = 20.0f;
constexpr float kPickCastYd = 5.0f;
constexpr float kMailboxWalkYd = 500.0f;
constexpr uint32_t kMaxBoxCastAttempts = 2;
constexpr uint32_t kMaxBoxOpenAttempts = 3;
constexpr uint32_t kMaxStealthAttempts = 4;
constexpr uint32_t kIntakeFreeSlots = 4;
constexpr uint32_t kPickFreeSlots = 2;

enum class BoxPhase : std::uint8_t
{
    Locking,       // cast Pick Lock (or skip the cast when the box is unlocked)
    AwaitLockCast, // the cast is running; check the lock flag
    Opening,       // CMSG_OPEN_ITEM and the item loot window
};

enum class PickPhase : std::uint8_t
{
    Stealth,
    Approach,
    Cast,
    Loot,
};

enum class MailTask : std::uint8_t
{
    None,
    ReceiveLockbox, // a rogue's service mail: take the box out and open it
    SendLockbox,    // a bot mails a box it cannot open to the guild's rogue
    SendReturn,     // the rogue mails the contents back to the sender
};

struct BotState
{
    uint64_t nowMs = 0;
    uint64_t nextPickScanMs = 0;
    uint64_t nextBoxScanMs = 0;
    uint64_t nextMailScanMs = 0;
    uint64_t nextMailRetryMs = 0;
    uint64_t nextDebugMs = 0;

    // Box errand (own bag or a mailed one).
    bool boxActive = false;
    BoxPhase boxPhase = BoxPhase::Locking;
    uint64_t boxItemGuidLow = 0;
    uint32_t boxItemId = 0;
    std::string boxName;
    uint64_t boxSenderLow = 0;
    std::string boxSenderName;
    uint64_t boxStartedMs = 0;
    uint64_t boxPhaseMs = 0;
    uint32_t boxCastAttempts = 0;
    uint32_t boxOpenAttempts = 0;
    uint64_t boxCoins = 0;
    std::unordered_map<uint32_t, uint32_t> bagBefore;
    uint64_t moneyBefore = 0;
    std::vector<std::pair<uint32_t, uint32_t>> returnItems;

    // Pickpocket errand.
    bool pickActive = false;
    PickPhase pickPhase = PickPhase::Stealth;
    ObjectGuid pickTarget;
    uint64_t pickStartedMs = 0;
    uint64_t pickPhaseMs = 0;
    uint32_t pickStealthAttempts = 0;
    std::unordered_map<uint32_t, uint64_t> pickTriedUntil;

    // Mailbox walk and its task payload.
    bool walking = false;
    ObjectGuid mailboxGuid;
    uint64_t walkStartedMs = 0;
    MailTask mailTask = MailTask::None;
    std::string mailRecipient;
    std::string mailBoxName;
    uint64_t mailItemGuidLow = 0;
    uint32_t mailSendAttempts = 0;
};

std::unordered_map<uint32_t, BotState>& States()
{
    static std::unordered_map<uint32_t, BotState> states;
    return states;
}

void LogSkip(BotState& st, Player* bot, char const* reason)
{
    if (st.nowMs < st.nextDebugMs)
        return;
    st.nextDebugMs = st.nowMs + kDebugIntervalMs;
    TC_LOG_DEBUG("tortoisebots", "[W7] thievery-skip bot={} reason={}", bot->GetName(), reason);
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

struct LockFacts
{
    bool hasLock = false;
    bool lockpickingLock = false;
    uint32_t requiredSkill = 0;
};

// The lock's Lockpicking case, read the way Spell::CanOpenLock does
// (Spell.cpp:9146-9188): the first LOCK_KEY_SKILL row whose
// SkillByLockType is Lockpicking.
LockFacts ReadLock(ItemTemplate const* proto)
{
    LockFacts facts;
    if (!proto)
        return facts;
    uint32_t const lockId = proto->GetLockID();
    if (!lockId)
        return facts;
    facts.hasLock = true;
    LockEntry const* lockInfo = sLockStore.LookupEntry(lockId);
    if (!lockInfo)
        return facts;
    for (int j = 0; j < MAX_LOCK_CASE; ++j)
    {
        if (lockInfo->Type[j] != LOCK_KEY_SKILL)
            continue;
        if (SkillByLockType(LockType(lockInfo->Index[j])) != SKILL_LOCKPICKING)
            continue;
        facts.lockpickingLock = true;
        facts.requiredSkill = lockInfo->Skill[j];
        return facts;
    }
    return facts;
}

LockboxGate ClassifyBox(Player* bot, Item* item)
{
    if (!item)
        return LockboxGate::NoLock;
    ItemTemplate const* proto = item->GetTemplate();
    LockFacts const lock = ReadLock(proto);
    LockboxFacts facts;
    facts.hasLock = lock.hasLock;
    facts.locked = item->IsLocked();
    facts.hasLoot = proto && proto->HasFlag(ITEM_FLAG_HAS_LOOT);
    facts.lockpickingLock = lock.lockpickingLock;
    facts.hasPickLockSpell = bot->HasSpell(kPickLockSpellId);
    facts.hasLockpicking = bot->HasSkill(SKILL_LOCKPICKING);
    facts.botSkill = bot->GetSkillValue(SKILL_LOCKPICKING);
    facts.requiredSkill = lock.requiredSkill;
    return EvaluateLockbox(facts);
}

std::string ItemName(Item* item)
{
    ItemTemplate const* proto = item ? item->GetTemplate() : nullptr;
    return proto ? proto->GetName(LOCALE_enUS) : (item ? std::to_string(item->GetEntry()) : std::string());
}

// The first bag box this bot may work on now (locked with enough skill, or
// already unlocked with loot inside).
Item* FindOpenableBox(Player* bot)
{
    Item* found = nullptr;
    bot->ForEachItem(ItemSearchLocation::Inventory, [&](Item* item)
    {
        if (!LockboxOpenable(ClassifyBox(bot, item)))
            return ItemSearchCallbackResult::Continue;
        found = item;
        return ItemSearchCallbackResult::Stop;
    });
    return found;
}

// The first bag box this bot cannot open itself but a guild rogue could:
// the mailed service's input.
Item* FindBoxForMail(Player* bot)
{
    Item* found = nullptr;
    bot->ForEachItem(ItemSearchLocation::Inventory, [&](Item* item)
    {
        if (item->IsSoulBound())
            return ItemSearchCallbackResult::Continue;
        LockboxGate const gate = ClassifyBox(bot, item);
        if (!LockboxNeedsABetterRogue(gate) && !LockboxNeedsARogue(gate))
            return ItemSearchCallbackResult::Continue;
        found = item;
        return ItemSearchCallbackResult::Stop;
    });
    return found;
}

void CastSpellAt(Player* bot, uint32_t spellId, uint32_t targetFlags, ObjectGuid const& unit,
    ObjectGuid const& item)
{
    if (!bot->GetSession())
        return;
    if (bot->isMoving())
        bot->StopMoving();
    if (bot->IsMounted())
        bot->Dismount();
    WorldPackets::Spells::CastSpell packet{ WorldPacket(CMSG_CAST_SPELL) };
    packet.Cast.SpellID = int32_t(spellId);
    packet.Cast.Target.Flags = targetFlags;
    if (!unit.IsEmpty())
        packet.Cast.Target.Unit = unit;
    if (!item.IsEmpty())
        packet.Cast.Target.Item = item;
    bot->GetSession()->HandleCastSpellOpcode(packet);
}

// Every unlooted piece of an open loot window through the core's autostore
// handler, then its money; the window is released only once nothing is left
// (an item container is destroyed by that release, DoLootRelease
// LootHandler.cpp:345-373).
struct LootTake
{
    bool fullyLooted = false;
    uint32_t itemUnits = 0;
};

LootTake TakeLoot(Player* bot, ObjectGuid const& lootGuid)
{
    LootTake out;
    if (!bot->GetSession())
        return out;

    std::vector<uint8_t> listIds;
    if (Loot* loot = bot->GetLootByWorldObjectGUID(lootGuid))
    {
        for (LootItem const& item : loot->items)
        {
            if (item.is_looted)
                continue;
            listIds.push_back(static_cast<uint8_t>(item.LootListId));
            out.itemUnits += item.count;
        }
    }
    for (uint8_t listId : listIds)
    {
        Loot* current = bot->GetLootByWorldObjectGUID(lootGuid);
        if (!current)
            break;
        WorldPackets::Loot::LootItem packet{ WorldPacket(CMSG_LOOT_ITEM) };
        WorldPackets::Loot::LootRequest request;
        request.Object = current->GetGUID();
        request.LootListID = listId;
        packet.Loot.push_back(request);
        bot->GetSession()->HandleAutostoreLootItemOpcode(packet);
    }

    Loot* loot = bot->GetLootByWorldObjectGUID(lootGuid);
    if (loot && loot->gold > 0)
    {
        WorldPackets::Loot::LootMoney packet{ WorldPacket(CMSG_LOOT_MONEY) };
        bot->GetSession()->HandleLootMoneyOpcode(packet);
        loot = bot->GetLootByWorldObjectGUID(lootGuid);
    }
    if (!loot)
    {
        out.fullyLooted = true;
        return out;
    }
    out.fullyLooted = loot->isLooted();
    if (out.fullyLooted)
        bot->GetSession()->DoLootRelease(loot);
    return out;
}

// A stack holding exactly what is left to mail, splitting one down when the
// bag stack is bigger; the boxes' own loot usually needs no split.
bool FindStackForMail(Player* bot, uint32_t itemId, uint32_t want, Item*& out)
{
    out = nullptr;
    if (!want)
        return false;
    Item* whole = nullptr;
    Item* bigger = nullptr;
    bot->ForEachItem(ItemSearchLocation::Inventory, [&](Item* item)
    {
        if (item->GetEntry() != itemId || item->IsSoulBound())
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

bool IsGuildMember(Player const* bot, uint64_t senderLow)
{
    Guild const* guild = sGuildMgr->GetGuildById(bot->GetGuildId());
    return guild && guild->GetMember(ObjectGuid::Create<HighGuid::Player>(senderLow)) != nullptr;
}

// A guild member is a bot when this manager holds it, or when its account is
// the pool's (an offline pool bot has no manager entry). The same test the
// guild-mail service makes.
bool IsBotCharacter(ObjectGuid const& guid)
{
    auto const& mgr = TortoiseBots::BotMgr::Instance();
    if (mgr.IsBot(guid))
        return true;
    uint32_t const accountId = sCharacterCache->GetCharacterAccountIdByGuid(guid);
    if (!accountId)
        return false;
    if (TortoiseBots::pool::AccountRegistered(accountId))
        return true;
    std::string accountName;
    return AccountMgr::GetName(accountId, accountName) && TortoiseBots::world::IsPoolAccountName(accountName);
}

bool HasServiceMail(Player* bot)
{
    time_t const now = GameTime::GetGameTime();
    for (Mail* mail : bot->GetMails())
    {
        if (!IsLockboxServiceMail(bot, mail) || mail->deliver_time > now)
            continue;
        return true;
    }
    return false;
}

std::vector<LockboxRogueCandidate> GuildRogueCandidates(Player* bot)
{
    std::vector<LockboxRogueCandidate> out;
    Guild const* guild = sGuildMgr->GetGuildById(bot->GetGuildId());
    if (!guild)
        return out;

    for (auto const& [memberGuid, member] : guild->GetMembers())
    {
        if (memberGuid == bot->GetGUID())
            continue;
        if (!IsBotCharacter(memberGuid))
            continue;

        LockboxRogueCandidate candidate;
        candidate.guidLow = memberGuid.GetCounter();
        candidate.name = member.GetName();
        candidate.isBot = true;
        Player* online = ObjectAccessor::FindPlayer(memberGuid);
        if (online && online->IsInWorld())
        {
            candidate.online = true;
            candidate.isRogue = online->GetClass() == CLASS_ROGUE;
            candidate.lockpickingSkill = online->HasSpell(kPickLockSpellId)
                ? online->GetSkillValue(SKILL_LOCKPICKING) : 0;
            continue;
        }
        CharacterCacheEntry const* entry = sCharacterCache->GetCharacterCacheByGuid(memberGuid);
        if (!entry)
            continue;
        candidate.isRogue = entry->Class == CLASS_ROGUE;
        // A rogue bot's Lockpicking is always max-for-level (PlayerbotAI's
        // cheat kit sets it at login, PlayerbotAI.cpp:1697-1703); offline
        // there is no Player to ask.
        candidate.lockpickingSkill = TortoiseBots::RogueWantsLockpickSkill(entry->Class, entry->Level)
            ? TortoiseBots::LockpickSkillForLevel(entry->Level)
            : 0;
    }
    return out;
}

// Engine pause and the walk owner: another host service with work on this bot
// keeps its turn, exactly as the disenchant service yields to them.
bool AnotherErrandOwns(Player* bot)
{
    if (tbhost::guild_mail::HasActiveErrand(bot) || tbhost::errands::HasActiveErrand(bot) ||
        tbhost::guild_orders::HasActiveDisenchantErrand(bot) || tbhost::guild_orders::HasActiveEnchantErrand(bot) ||
        tbhost::prof::HasActiveErrand(bot))
        return true;
    // A finished or cancelled order stays in the store until the giver collects it; only an open one is work.
    std::optional<TortoiseBots::GuildOrders::OrderRecord> const order = tbhost::guild_orders::FindOrder(bot->GetGUID().GetCounter());
    return order && !order->cancelled && !TortoiseBots::GuildOrders::IsFinished(order->state);
}

void ClearMailTask(BotState& st)
{
    st.mailTask = MailTask::None;
    st.mailRecipient.clear();
    st.mailBoxName.clear();
    st.mailItemGuidLow = 0;
    st.mailSendAttempts = 0;
    st.walking = false;
}

void EndBox(BotState& st)
{
    st.boxActive = false;
    st.boxPhase = BoxPhase::Locking;
    st.boxItemGuidLow = 0;
    st.boxItemId = 0;
    st.boxName.clear();
    st.boxSenderLow = 0;
    st.boxSenderName.clear();
    st.boxPhaseMs = 0;
    st.boxCastAttempts = 0;
    st.boxOpenAttempts = 0;
    st.boxCoins = 0;
    st.bagBefore.clear();
    st.returnItems.clear();
}

void EndPick(BotState& st)
{
    st.pickActive = false;
    st.pickPhase = PickPhase::Stealth;
    st.pickTarget.Clear();
    st.pickPhaseMs = 0;
    st.pickStealthAttempts = 0;
}

void StartBox(Player* bot, BotState& st, Item* item, uint64_t senderLow, std::string senderName)
{
    st.boxActive = true;
    st.boxPhase = BoxPhase::Locking;
    st.boxItemGuidLow = item->GetGUID().GetCounter();
    st.boxItemId = item->GetEntry();
    st.boxName = ItemName(item);
    st.boxSenderLow = senderLow;
    st.boxSenderName = std::move(senderName);
    st.boxStartedMs = st.nowMs;
    st.boxPhaseMs = st.nowMs;
    st.boxCastAttempts = 0;
    st.boxOpenAttempts = 0;
    st.boxCoins = 0;
    st.bagBefore = BagCounts(bot);
    st.moneyBefore = bot->GetMoney();
    st.returnItems.clear();
}

struct MailPayload
{
    std::string recipient;
    std::string boxName;
    std::vector<std::pair<uint32_t, uint32_t>> items;
    uint64_t itemGuidLow = 0;
    uint64_t boxCoins = 0;
};

void StartMailWalk(Player* bot, BotState& st, MailTask task, MailPayload payload);

void FinishBox(Player* bot, BotState& st)
{
    std::unordered_map<uint32_t, uint32_t> const after = BagCounts(bot);
    std::vector<std::pair<uint32_t, uint32_t>> gained;
    for (auto const& [itemId, count] : after)
    {
        uint32_t had = 0;
        auto const before = st.bagBefore.find(itemId);
        if (before != st.bagBefore.end())
            had = before->second;
        if (count > had)
            gained.emplace_back(itemId, count - had);
    }
    uint64_t const moneyAfter = bot->GetMoney();
    uint64_t const coins = moneyAfter > st.moneyBefore ? moneyAfter - st.moneyBefore : 0;

    if (coins || !gained.empty())
    {
        if (st.boxSenderLow)
            TC_LOG_INFO("tortoisebots", "[W7] lockbox-open bot={} item={} guid={} from={} items={} coins={}",
                bot->GetName(), st.boxItemId, st.boxItemGuidLow, st.boxSenderName, uint32_t(gained.size()), coins);
        else
            TC_LOG_INFO("tortoisebots", "[W7] lockbox-open bot={} item={} guid={} own=1 items={} coins={}",
                bot->GetName(), st.boxItemId, st.boxItemGuidLow, uint32_t(gained.size()), coins);
    }
    else
    {
        TC_LOG_INFO("tortoisebots", "[W7] lockbox-empty bot={} item={} guid={} from={}",
            bot->GetName(), st.boxItemId, st.boxItemGuidLow, st.boxSenderName);
    }

    bool const fromMail = st.boxSenderLow != 0;
    std::string const senderName = st.boxSenderName;
    std::string const boxName = st.boxName;
    EndBox(st);

    if (!fromMail)
        return;
    if (gained.empty() && coins == 0)
        return; // nothing was inside; the log line above says so
    MailPayload payload;
    payload.recipient = senderName;
    payload.boxName = boxName;
    payload.items = std::move(gained);
    payload.boxCoins = coins;
    StartMailWalk(bot, st, MailTask::SendReturn, std::move(payload));
    if (st.mailTask != MailTask::SendReturn)
        TC_LOG_ERROR("tortoisebots", "[W7] lockbox-return bot={} to={}: the contents stay in the rogue's bags, no mailbox",
            bot->GetName(), senderName);
}

void AbortBox(Player* bot, BotState& st, char const* reason)
{
    TC_LOG_INFO("tortoisebots", "[W7] lockbox-fail bot={} item={} guid={} reason={}",
        bot->GetName(), st.boxItemId, st.boxItemGuidLow, reason);
    if (st.boxSenderLow)
        TC_LOG_ERROR("tortoisebots", "[W7] lockbox-return bot={} to={} item={} guid={}: the box stays with the rogue ({})",
            bot->GetName(), st.boxSenderName, st.boxItemId, st.boxItemGuidLow, reason);
    EndBox(st);
}

void TickBox(Player* bot, BotState& st)
{
    if (!bot->IsAlive())
    {
        AbortBox(bot, st, "dead");
        return;
    }
    if (bot->IsInCombat())
    {
        AbortBox(bot, st, "combat");
        return;
    }
    if (st.nowMs - st.boxStartedMs >= kBoxErrandTimeoutMs)
    {
        AbortBox(bot, st, "timeout");
        return;
    }

    ObjectGuid const itemGuid = ObjectGuid::Create<HighGuid::Item>(st.boxItemGuidLow);
    Item* item = bot->GetItemByGuid(itemGuid);
    if (!item)
    {
        AbortBox(bot, st, "item-gone");
        return;
    }

    switch (st.boxPhase)
    {
        case BoxPhase::Locking:
        {
            if (!item->IsLocked())
            {
                st.boxPhase = BoxPhase::Opening;
                st.boxPhaseMs = st.nowMs;
                return;
            }
            if (bot->IsNonMeleeSpellCast(false))
                return;
            if (st.boxCastAttempts >= kMaxBoxCastAttempts)
            {
                AbortBox(bot, st, "cast-refused");
                return;
            }
            ++st.boxCastAttempts;
            CastSpellAt(bot, kPickLockSpellId, TARGET_FLAG_ITEM, ObjectGuid::Empty, item->GetGUID());
            st.boxPhase = BoxPhase::AwaitLockCast;
            st.boxPhaseMs = st.nowMs;
            return;
        }
        case BoxPhase::AwaitLockCast:
        {
            if (bot->IsNonMeleeSpellCast(false))
                return;
            if (!item->IsLocked())
            {
                st.boxPhase = BoxPhase::Opening;
                st.boxPhaseMs = st.nowMs;
                return;
            }
            if (st.nowMs - st.boxPhaseMs >= kCastGraceMs)
                st.boxPhase = BoxPhase::Locking;
            return;
        }
        case BoxPhase::Opening:
        {
            Loot* loot = bot->GetLootByWorldObjectGUID(item->GetGUID());
            if (!loot)
            {
                if (st.boxOpenAttempts >= kMaxBoxOpenAttempts)
                {
                    AbortBox(bot, st, "open-refused");
                    return;
                }
                ++st.boxOpenAttempts;
                WorldPackets::Spells::OpenItem packet{ WorldPacket(CMSG_OPEN_ITEM) };
                packet.Slot = item->GetBagSlot();
                packet.PackSlot = item->GetSlot();
                bot->GetSession()->HandleOpenItemOpcode(packet);
                loot = bot->GetLootByWorldObjectGUID(item->GetGUID());
                if (!loot)
                    return;
            }
            LootTake const take = TakeLoot(bot, item->GetGUID());
            if (!take.fullyLooted)
            {
                if (Loot* rest = bot->GetLootByWorldObjectGUID(item->GetGUID()))
                    bot->GetSession()->DoLootRelease(rest);
                if (st.nowMs - st.boxPhaseMs >= kCastGraceMs)
                    AbortBox(bot, st, "bags-full");
                return;
            }
            FinishBox(bot, st);
            return;
        }
    }
}

void MarkPickTried(BotState& st, Creature* target)
{
    if (!target)
        return;
    st.pickTriedUntil[target->GetGUID().GetCounter()] = st.nowMs + kPickBackoffMs;
    if (st.pickTriedUntil.size() > 64)
    {
        for (auto it = st.pickTriedUntil.begin(); it != st.pickTriedUntil.end();)
        {
            if (it->second < st.nowMs)
                it = st.pickTriedUntil.erase(it);
            else
                ++it;
        }
    }
}

PickpocketTargetGate TargetGate(Player* bot, Creature* target)
{
    CreatureDifficulty const* difficulty = target->GetCreatureDifficulty();
    uint32_t const lootId = difficulty ? difficulty->PickPocketLootID : 0;
    PickpocketTargetFacts facts;
    facts.alive = target->IsAlive();
    facts.inCombat = target->IsInCombat();
    facts.humanoid = target->GetCreatureType() == CREATURE_TYPE_HUMANOID;
    facts.canHaveLoot = target->CanHaveLoot();
    facts.hasPockets = lootId != 0 && LootTemplates_Pickpocketing.HaveLootFor(lootId);
    facts.pocketsReady = target->CanGeneratePickPocketLoot();
    facts.distanceYd = bot->GetDistance(target);
    facts.maxDistanceYd = kPickSearchYd;
    return EvaluatePickpocketTarget(facts);
}

Creature* FindPickpocketTarget(Player* bot, BotState& st)
{
    std::list<Creature*> creatures;
    Trinity::AnyUnitInObjectRangeCheck check(bot, kPickSearchYd);
    Trinity::CreatureListSearcher<Trinity::AnyUnitInObjectRangeCheck> searcher(bot, creatures, check);
    Cell::VisitGridObjects(bot, searcher, kPickSearchYd);

    Creature* best = nullptr;
    float bestDist = kPickSearchYd + 1.0f;
    for (Creature* creature : creatures)
    {
        if (!creature || !creature->IsInWorld())
            continue;
        if (!bot->IsHostileTo(creature) || !creature->InSamePhase(bot))
            continue;
        uint32_t const guidLow = creature->GetGUID().GetCounter();
        auto const tried = st.pickTriedUntil.find(guidLow);
        if (tried != st.pickTriedUntil.end() && tried->second > st.nowMs)
            continue;
        if (TargetGate(bot, creature) != PickpocketTargetGate::Ok)
            continue;
        float const distance = bot->GetDistance(creature);
        if (distance < bestDist)
        {
            bestDist = distance;
            best = creature;
        }
    }
    return best;
}

void AbortPick(Player* bot, BotState& st, char const* reason)
{
    LogSkip(st, bot, reason);
    EndPick(st);
}

void TickPick(Player* bot, BotState& st)
{
    if (!bot->IsAlive())
    {
        AbortPick(bot, st, "dead");
        return;
    }
    if (bot->IsInCombat())
    {
        AbortPick(bot, st, "combat");
        return;
    }
    if (st.nowMs - st.pickStartedMs >= kPickErrandTimeoutMs)
    {
        AbortPick(bot, st, "timeout");
        return;
    }

    Creature* target = ObjectAccessor::GetCreature(*bot, st.pickTarget);
    if (!target)
    {
        AbortPick(bot, st, "target-gone");
        return;
    }

    switch (st.pickPhase)
    {
        case PickPhase::Stealth:
        {
            if (bot->HasStealthAura())
            {
                st.pickPhase = PickPhase::Approach;
                st.pickPhaseMs = st.nowMs;
                return;
            }
            if (bot->IsNonMeleeSpellCast(false))
                return;
            if (st.pickStealthAttempts >= kMaxStealthAttempts)
            {
                AbortPick(bot, st, "no-stealth");
                return;
            }
            if (st.nowMs - st.pickPhaseMs >= kStealthRetryMs)
            {
                ++st.pickStealthAttempts;
                CastSpellAt(bot, kStealthSpellId, TARGET_FLAG_NONE, ObjectGuid::Empty, ObjectGuid::Empty);
                st.pickPhaseMs = st.nowMs;
            }
            return;
        }
        case PickPhase::Approach:
        {
            if (!bot->HasStealthAura())
            {
                st.pickPhase = PickPhase::Stealth;
                st.pickPhaseMs = st.nowMs;
                return;
            }
            PickpocketTargetGate const gate = TargetGate(bot, target);
            if (gate != PickpocketTargetGate::Ok)
            {
                AbortPick(bot, st, PickpocketTargetGateToken(gate));
                return;
            }
            if (bot->GetDistance(target) <= kPickCastYd)
            {
                bot->StopMoving();
                bot->SetFacingToObject(target);
                CastSpellAt(bot, kPickPocketSpellId, TARGET_FLAG_UNIT, target->GetGUID(), ObjectGuid::Empty);
                st.pickPhase = PickPhase::Cast;
                st.pickPhaseMs = st.nowMs;
                return;
            }
            if (!bot->isMoving() || st.nowMs - st.pickPhaseMs >= 1500)
            {
                bot->GetMotionMaster()->MovePoint(0, target->GetPositionX(), target->GetPositionY(),
                    target->GetPositionZ());
                st.pickPhaseMs = st.nowMs;
            }
            return;
        }
        case PickPhase::Cast:
        {
            if (bot->GetLootByWorldObjectGUID(target->GetGUID()))
            {
                st.pickPhase = PickPhase::Loot;
                st.pickPhaseMs = st.nowMs;
                return;
            }
            if (st.nowMs - st.pickPhaseMs < kCastGraceMs)
                return;
            // No window: either the core refused the cast (the refill timer
            // is untouched) or the pockets rolled empty (the effect ran and
            // started it). Neither is retried for this creature.
            bool const effectRan = !target->CanGeneratePickPocketLoot();
            TC_LOG_INFO("tortoisebots", "[W7] pickpocket-{} bot={} target={} entry={}",
                effectRan ? "empty" : "refused", bot->GetName(), target->GetGUID().ToString(), target->GetEntry());
            MarkPickTried(st, target);
            EndPick(st);
            return;
        }
        case PickPhase::Loot:
        {
            ObjectGuid const lootGuid = target->GetGUID();
            LootTake const take = TakeLoot(bot, lootGuid);
            if (!take.fullyLooted)
            {
                if (st.nowMs - st.pickPhaseMs >= kCastGraceMs)
                {
                    if (Loot* rest = bot->GetLootByWorldObjectGUID(lootGuid))
                        bot->GetSession()->DoLootRelease(rest);
                    AbortPick(bot, st, "loot-stuck");
                }
                return;
            }
            uint64_t const money = bot->GetMoney();
            TC_LOG_INFO("tortoisebots", "[W7] pickpocket bot={} target={} entry={} items={} coins={}",
                bot->GetName(), target->GetGUID().ToString(), target->GetEntry(), take.itemUnits,
                money >= st.moneyBefore ? money - st.moneyBefore : 0);
            MarkPickTried(st, target);
            EndPick(st);
            return;
        }
    }
}

void ExecuteMailTask(Player* bot, BotState& st, GameObject* mailbox);

void TickWalk(Player* bot, BotState& st)
{
    if (bot->IsInCombat() || st.nowMs - st.walkStartedMs >= kWalkTimeoutMs)
    {
        TC_LOG_INFO("tortoisebots", "[W7] thievery-skip bot={} reason={}", bot->GetName(),
            bot->IsInCombat() ? "combat" : "walk-timeout");
        st.walking = false;
        ClearMailTask(st);
        return;
    }
    GameObject* mailbox = ObjectAccessor::GetGameObject(*bot, st.mailboxGuid);
    if (!mailbox || !mailbox->IsInWorld() || mailbox->GetMap() != bot->FindMap())
    {
        st.walking = false;
        ClearMailTask(st);
        return;
    }
    if (bot->GetGameObjectIfCanInteractWith(mailbox->GetGUID(), GAMEOBJECT_TYPE_MAILBOX))
    {
        bot->StopMoving();
        st.walking = false;
        ExecuteMailTask(bot, st, mailbox);
        return;
    }
    if (!bot->isMoving())
        bot->GetMotionMaster()->MovePoint(0, mailbox->GetPositionX(), mailbox->GetPositionY(),
            mailbox->GetPositionZ());
}

// Sends the bot to a mailbox and runs the pending task there (or on the spot
// when a mailbox is already in reach). The payload rides in the state because
// the visit can come ticks later.
void ResumeMailTask(Player* bot, BotState& st)
{
    if (GameObject* mailbox = tbhost::town::FindNearestMailbox(bot))
    {
        st.walking = false;
        ExecuteMailTask(bot, st, mailbox);
        return;
    }
    GameObject* mailbox = tbhost::town::FindNearestMailbox(bot, kMailboxWalkYd);
    if (!mailbox)
    {
        TC_LOG_DEBUG("tortoisebots", "[W7] thievery-skip bot={} reason=no-mailbox", bot->GetName());
        ClearMailTask(st);
        return;
    }
    if (bot->isMoving())
        bot->StopMoving();
    st.walking = true;
    st.mailboxGuid = mailbox->GetGUID();
    st.walkStartedMs = st.nowMs;
    bot->GetMotionMaster()->MovePoint(0, mailbox->GetPositionX(), mailbox->GetPositionY(),
        mailbox->GetPositionZ());
}

void StartMailWalk(Player* bot, BotState& st, MailTask task, MailPayload payload)
{
    ClearMailTask(st);
    st.mailTask = task;
    st.mailRecipient = std::move(payload.recipient);
    st.mailBoxName = std::move(payload.boxName);
    st.mailItemGuidLow = payload.itemGuidLow;
    st.boxCoins = payload.boxCoins;
    st.returnItems = std::move(payload.items);
    ResumeMailTask(bot, st);
}

void ReceiveLockboxMail(Player* bot, BotState& st, GameObject* mailbox)
{
    time_t const now = GameTime::GetGameTime();
    for (Mail* mail : bot->GetMails())
    {
        if (!IsLockboxServiceMail(bot, mail) || mail->deliver_time > now)
            continue;
        for (MailItemInfo const& info : mail->items)
        {
            Item* mailed = bot->GetMItem(info.item_guid);
            if (!mailed)
                continue;
            if (!LockboxOpenable(ClassifyBox(bot, mailed)))
                continue; // another rogue got here first: leave the rest in the mail
            if (FreeBagSlots(bot) < kIntakeFreeSlots)
            {
                LogSkip(st, bot, "bags");
                return;
            }

            uint64_t const mailId = mail->messageID;
            uint64_t const senderLow = mail->sender;
            std::string senderName;
            if (!sCharacterCache->GetCharacterNameByGuid(ObjectGuid::Create<HighGuid::Player>(senderLow), senderName))
                senderName = std::to_string(senderLow);

            if (!tbhost::town::TakeMailItem(bot, mailbox->GetGUID(), mailId, info.item_guid))
            {
                TC_LOG_ERROR("tortoisebots", "[W7] lockbox-take bot={} mail={} attach={}: the take did not land",
                    bot->GetName(), mailId, info.item_guid);
                return;
            }
            Item* taken = bot->GetItemByGuid(ObjectGuid::Create<HighGuid::Item>(info.item_guid));
            if (!taken)
            {
                TC_LOG_ERROR("tortoisebots", "[W7] lockbox-take bot={} mail={} attach={}: the take landed but the box is not in the bags",
                    bot->GetName(), mailId, info.item_guid);
                return;
            }
            bool const mailEmpty = mail->items.empty() && mail->money == 0;
            if (mailEmpty)
                tbhost::town::DeleteMail(bot, mailId);
            TC_LOG_INFO("tortoisebots", "[W7] lockbox-intake bot={} item={} guid={} from={}",
                bot->GetName(), taken->GetEntry(), info.item_guid, senderName);
            StartBox(bot, st, taken, senderLow, senderName);
            return;
        }
    }
}

void SendLockboxMail(Player* bot, BotState& st, GameObject* mailbox)
{
    Item* box = bot->GetItemByGuid(ObjectGuid::Create<HighGuid::Item>(st.mailItemGuidLow));
    if (!box)
    {
        ClearMailTask(st);
        return;
    }
    std::vector<ObjectGuid> attachments{ box->GetGUID() };
    std::string fail;
    if (!tbhost::guild::SendMail(bot, mailbox->GetGUID(), st.mailRecipient, kLockboxServiceSubject,
            "Please open this lockbox and mail the contents back.", 0, attachments, &fail))
    {
        TC_LOG_INFO("tortoisebots", "[W7] lockbox-send-fail bot={} to={} item={} reason={}",
            bot->GetName(), st.mailRecipient, box->GetEntry(), fail);
        st.nextMailRetryMs = st.nowMs + kMailRetryMs * 4;
        ClearMailTask(st);
        return;
    }
    TC_LOG_INFO("tortoisebots", "[W7] lockbox-sent bot={} to={} item={} guid={}",
        bot->GetName(), st.mailRecipient, box->GetEntry(), box->GetGUID().GetCounter());
    ClearMailTask(st);
    st.nextMailScanMs = st.nowMs + kMailScanIntervalMs;
}

void SendReturnMail(Player* bot, BotState& st, GameObject* mailbox)
{
    std::vector<std::pair<uint32_t, uint32_t>> remaining = st.returnItems;
    std::vector<ObjectGuid> attachments;
    std::vector<std::pair<uint32_t, uint32_t>> left;
    for (auto const& [itemId, count] : remaining)
    {
        if (attachments.size() >= kLockboxMailMaxItems)
        {
            left.emplace_back(itemId, count);
            continue;
        }
        Item* stack = nullptr;
        if (!FindStackForMail(bot, itemId, count, stack))
        {
            TC_LOG_ERROR("tortoisebots", "[W7] lockbox-mail-lost bot={} to={} item={} count={}: no stack left in the bags",
                bot->GetName(), st.mailRecipient, itemId, count);
            continue;
        }
        attachments.push_back(stack->GetGUID());
        uint32_t const mailed = stack->GetCount();
        if (mailed < count)
            left.emplace_back(itemId, count - mailed);
    }

    uint64_t const postage = MailPostageCopper(static_cast<uint32_t>(attachments.size()));
    uint64_t const coins = LockboxReturnCopper(st.boxCoins, postage);
    if (attachments.empty() && coins == 0)
    {
        if (!left.empty())
            TC_LOG_ERROR("tortoisebots", "[W7] lockbox-mail-lost bot={} to={}: nothing of the box's contents is left in the bags",
                bot->GetName(), st.mailRecipient);
        ClearMailTask(st);
        return;
    }
    if (!bot->HasEnoughMoney(postage + coins))
    {
        if (++st.mailSendAttempts > 3)
        {
            TC_LOG_ERROR("tortoisebots", "[W7] lockbox-mail-nomoney bot={} to={} postage={} coins={}: the contents stay with the rogue",
                bot->GetName(), st.mailRecipient, postage, coins);
            ClearMailTask(st);
            return;
        }
        st.nextMailRetryMs = st.nowMs + kMailRetryMs;
        return;
    }

    std::string const subject = "Lockbox opened: " + st.mailBoxName;
    std::string fail;
    if (!tbhost::guild::SendMail(bot, mailbox->GetGUID(), st.mailRecipient, subject,
            "Here are the contents of your lockbox.", coins, attachments, &fail))
    {
        TC_LOG_INFO("tortoisebots", "[W7] lockbox-mail-fail bot={} to={} reason={}",
            bot->GetName(), st.mailRecipient, fail);
        st.nextMailRetryMs = st.nowMs + kMailRetryMs;
        if (++st.mailSendAttempts > 3)
            ClearMailTask(st);
        return;
    }
    TC_LOG_INFO("tortoisebots", "[W7] lockbox-mail bot={} to={} box={} stacks={} coins={} postage={}",
        bot->GetName(), st.mailRecipient, st.mailBoxName, uint32_t(attachments.size()), coins, postage);
    st.returnItems = std::move(left);
    if (st.returnItems.empty())
    {
        ClearMailTask(st);
        return;
    }
    // More than one mail's worth: what the box's coins paid stays spent, and
    // the rest of the stacks follow on the next visit.
    st.boxCoins = (coins + postage < st.boxCoins) ? st.boxCoins - coins - postage : 0;
    st.nextMailRetryMs = 0;
}

void ExecuteMailTask(Player* bot, BotState& st, GameObject* mailbox)
{
    switch (st.mailTask)
    {
        case MailTask::ReceiveLockbox:
            ReceiveLockboxMail(bot, st, mailbox);
            ClearMailTask(st);
            return;
        case MailTask::SendLockbox:
            SendLockboxMail(bot, st, mailbox);
            return;
        case MailTask::SendReturn:
            SendReturnMail(bot, st, mailbox);
            return;
        case MailTask::None:
            return;
    }
}

// A bot holding a box it cannot open: pick the guild's best rogue and mail
// it. The sender pays this mail's postage out of its own purse (30 copper,
// MailHandler.cpp:171).
bool StartLockboxMail(Player* bot, BotState& st, Item* box)
{
    if (!box)
        return false;
    ItemTemplate const* proto = box->GetTemplate();
    LockFacts const lock = ReadLock(proto);
    std::vector<LockboxRogueCandidate> const candidates = GuildRogueCandidates(bot);
    LockboxRogueCandidate const* rogue = PickLockboxRogue(bot->GetGUID().GetCounter(), lock.requiredSkill, candidates);
    if (!rogue)
    {
        LogSkip(st, bot, "no-rogue");
        return false;
    }
    uint64_t const postage = MailPostageCopper(1);
    if (!bot->HasEnoughMoney(postage))
    {
        LogSkip(st, bot, "postage");
        return false;
    }
    TC_LOG_INFO("tortoisebots", "[W7] lockbox-mail-pick bot={} item={} guid={} gate={} skill={} req={} rogue={} online={}",
        bot->GetName(), box->GetEntry(), box->GetGUID().GetCounter(),
        LockboxGateToken(ClassifyBox(bot, box)), bot->GetSkillValue(SKILL_LOCKPICKING),
        lock.requiredSkill, rogue->name, rogue->online ? 1 : 0);
    MailPayload payload;
    payload.recipient = rogue->name;
    payload.itemGuidLow = box->GetGUID().GetCounter();
    StartMailWalk(bot, st, MailTask::SendLockbox, std::move(payload));
    return st.mailTask == MailTask::SendLockbox;
}

bool StartPickpocket(Player* bot, BotState& st)
{
    PickpocketBotFacts facts;
    facts.isRogue = bot->GetClass() == CLASS_ROGUE;
    facts.hasStealthSpell = bot->HasSpell(kStealthSpellId);
    facts.hasPickPocketSpell = bot->HasSpell(kPickPocketSpellId);
    facts.alive = bot->IsAlive();
    facts.inWorld = bot->IsInWorld();
    facts.inCombat = bot->IsInCombat();
    facts.busy = AnotherErrandOwns(bot) || bot->IsNonMeleeSpellCast(false);
    PickpocketBotGate const gate = EvaluatePickpocketBot(facts);
    if (gate != PickpocketBotGate::Ready)
    {
        if (gate != PickpocketBotGate::NotRogue && gate != PickpocketBotGate::NoSpell)
            LogSkip(st, bot, PickpocketBotGateToken(gate));
        return false;
    }
    if (FreeBagSlots(bot) < kPickFreeSlots)
    {
        LogSkip(st, bot, "bags");
        return false;
    }
    Creature* target = FindPickpocketTarget(bot, st);
    if (!target)
    {
        LogSkip(st, bot, "no-target");
        return false;
    }
    st.pickActive = true;
    st.pickTarget = target->GetGUID();
    st.pickStartedMs = st.nowMs;
    st.pickPhaseMs = st.nowMs;
    st.pickStealthAttempts = 0;
    st.pickPhase = bot->HasStealthAura() ? PickPhase::Approach : PickPhase::Stealth;
    st.moneyBefore = bot->GetMoney();
    TC_LOG_DEBUG("tortoisebots", "[W7] pickpocket-start bot={} target={} entry={}",
        bot->GetName(), target->GetGUID().ToString(), target->GetEntry());
    return true;
}

} // namespace

void LoadConfig()
{
    g_pickpocket.store(sConfigMgr->GetBoolDefault("TortoiseBots.World.Pickpocket", true), std::memory_order_release);
    g_lockboxes.store(sConfigMgr->GetBoolDefault("TortoiseBots.World.Lockboxes", true), std::memory_order_release);
    g_lockboxMail.store(sConfigMgr->GetBoolDefault("TortoiseBots.World.LockboxMail", true), std::memory_order_release);
}

bool PickpocketEnabled() { return g_pickpocket.load(std::memory_order_acquire); }
bool LockboxesEnabled() { return g_lockboxes.load(std::memory_order_acquire); }
bool LockboxMailEnabled() { return g_lockboxMail.load(std::memory_order_acquire); }

bool IsLockboxServiceMail(Player* bot, Mail* mail)
{
    if (!bot || !mail || !bot->GetGuildId())
        return false;
    if (mail->state == MAIL_STATE_DELETED || mail->messageType != MAIL_NORMAL ||
        mail->stationery == MAIL_STATIONERY_AUCTION)
        return false;
    if (mail->COD != 0 || mail->items.empty())
        return false;
    if (!IsLockboxServiceSubject(mail->subject))
        return false;
    if (!IsGuildMember(bot, mail->sender))
        return false;
    // Only a bot's box: a real player's mail is never taken by the service.
    return IsBotCharacter(ObjectGuid::Create<HighGuid::Player>(mail->sender));
}

void TickThievery(Player* bot, uint32_t diffMs)
{
    if (!bot || !bot->IsInWorld() || !bot->FindMap() || !bot->GetSession())
        return;
    if (!PickpocketEnabled() && !LockboxesEnabled() && !LockboxMailEnabled())
        return;

    BotState& st = States()[bot->GetGUID().GetCounter()];
    st.nowMs += diffMs;

    if (st.boxActive)
    {
        TickBox(bot, st);
        return;
    }
    if (st.pickActive)
    {
        TickPick(bot, st);
        return;
    }
    if (st.walking)
    {
        TickWalk(bot, st);
        return;
    }
    if (!bot->IsAlive() || bot->IsInCombat())
        return;

    // A queued mailbox task (a return mail with more stacks than one mail
    // holds, or a send that needs a retry) keeps its job until it runs.
    if (st.mailTask != MailTask::None)
    {
        if (st.nowMs < st.nextMailRetryMs)
            return;
        ResumeMailTask(bot, st);
        return;
    }

    if (PickpocketEnabled() && bot->GetClass() == CLASS_ROGUE && st.nowMs >= st.nextPickScanMs)
    {
        st.nextPickScanMs = st.nowMs + kPickScanIntervalMs;
        if (StartPickpocket(bot, st))
            return;
    }

    if (LockboxesEnabled() && bot->GetClass() == CLASS_ROGUE && st.nowMs >= st.nextBoxScanMs)
    {
        st.nextBoxScanMs = st.nowMs + kBoxScanIntervalMs;
        if (Item* box = FindOpenableBox(bot))
        {
            TC_LOG_INFO("tortoisebots", "[W7] lockbox-start bot={} item={} guid={} own=1",
                bot->GetName(), box->GetEntry(), box->GetGUID().GetCounter());
            StartBox(bot, st, box, 0, {});
            return;
        }
    }

    if (LockboxMailEnabled() && bot->GetGuildId() && st.nowMs >= st.nextMailScanMs)
    {
        st.nextMailScanMs = st.nowMs + kMailScanIntervalMs;
        bool const wantIntake = bot->GetClass() == CLASS_ROGUE && HasServiceMail(bot);
        Item* mailBox = wantIntake ? nullptr : FindBoxForMail(bot);
        if (wantIntake || mailBox)
        {
            if (AnotherErrandOwns(bot))
            {
                LogSkip(st, bot, "busy");
                return;
            }
            if (wantIntake)
            {
                StartMailWalk(bot, st, MailTask::ReceiveLockbox, {});
                return;
            }
            StartLockboxMail(bot, st, mailBox);
            return;
        }
    }
}

bool HasActiveErrand(Player const* bot)
{
    if (!bot)
        return false;
    auto const found = States().find(bot->GetGUID().GetCounter());
    if (found == States().end())
        return false;
    BotState const& st = found->second;
    return st.boxActive || st.pickActive || st.walking || st.mailTask != MailTask::None;
}

void ForgetBot(uint32_t botGuidLow)
{
    States().erase(botGuidLow);
}

} // namespace thievery
} // namespace tbhost
