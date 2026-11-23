// Enchant orders on the ForeverCore build (TBF #155).
//
// The giver places one permanent enchant on their own item. The bot reserves
// the cast's reagents, meets the giver in its faction capital, takes the
// missing reagents in a materials trade, then opens the enchant trade: the
// giver puts the item in the "will not be traded" slot, the bot records the
// enchant on the window (TradeData::SetSpell) and accepts; the core casts it
// on the item and takes the bot's reagents when both sides accept
// (TradeHandler.cpp:386-406 creates the spell, Spell.cpp:3831 stores a trade
// cast until the accept). The item is verified in the giver's bags before the
// row ends done.
//
// The decisions are the ported policy headers; this file only reads the core
// and runs the effects. World thread only.
#include "host/TbHostGuildOrderEnchant.h"

#include "host/TbH3.h"
#include "host/TbHostGuild.h"
#include "host/TbHostGuildOrderAccess.h"
#include "host/TbHostGuildOrders.h"

#include "compat/TbItemPrototype.h"
#include "playerbot/GuildOrderEnchants.h"
#include "playerbot/policy/guild/EnchantGatherPolicy.h"
#include "playerbot/policy/guild/EnchantTradePolicy.h"
#include "playerbot/policy/profession/ProfessionPickPolicy.h"
#include "runtime/GuildOrderEnchantLifecyclePolicy.h"
#include "runtime/GuildOrderEnchantPolicy.h"
#include "runtime/GuildOrderMeetPolicy.h"
#include "runtime/policy/guild/EnchantCapitalPolicy.h"

#include "Chat.h"
#include "GameTime.h"
#include "Group.h"
#include "Guild.h"
#include "Item.h"
#include "ItemDefines.h"
#include "Log.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "ObjectDefines.h"
#include "ObjectGuid.h"
#include "Player.h"
#include "SharedDefines.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "TradeData.h"
#include "TradePackets.h"
#include "WorldPacket.h"
#include "WorldSession.h"

#include <algorithm>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace tbhost
{
namespace guild_orders
{
namespace
{
using namespace TortoiseBots::GuildOrders;
using TortoiseBots::EnchantCapital::CapitalMapId;
using TortoiseBots::EnchantCapital::CapitalZoneId;
using TortoiseBots::EnchantCapital::IsInCapital;
using TortoiseBots::EnchantCapital::TeleportWait;

// The capital inn stand: the measured fallback positions beside each
// innkeeper spawn that the Turtle adapter uses when its spawn scan finds
// nothing. A TeleportTo never touches the homebind.
struct CapitalInn
{
    uint32_t mapId = 0;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float o = 0.0f;
};

CapitalInn FallbackInn(bool alliance)
{
    if (alliance)
        return CapitalInn{ CapitalMapId(true), -8867.2f, 675.9f, 97.9f, 5.20f };
    return CapitalInn{ CapitalMapId(false), -1672.6f, 2026.2f, 54.1f, 2.78f };
}

// One bot's live enchant leg. Memory only: a restart drops the Meet request
// and the trade session, like the Turtle store does.
struct Session
{
    uint32_t giverGuidLow = 0;
    uint64_t orderCreatedAt = 0;
    uint32_t meetRequestedMs = 0; // 0 = no Meet press
    uint32_t nextTeleportMs = 0;
    uint32_t openedMs = 0;        // this window's open time (server uptime ms)
    uint32_t tries = 0;           // windows opened since the Meet press
    uint64_t serial = 0;          // the trade window this handshake answers
    bool busySent = false;
    bool begun = false;
    bool spellSet = false;        // TradeData::SetSpell was recorded on this window
    uint64_t parkedItemGuidLow = 0;
    uint64_t lastVerdictItemGuid = 0; // one whisper per verdict and item
    uint32_t lastVerdict = 0;
};

std::unordered_map<uint32_t, Session>& Sessions()
{
    static std::unordered_map<uint32_t, Session> sessions;
    return sessions;
}

std::unordered_map<uint32_t, uint32_t>& MeetFailed()
{
    static std::unordered_map<uint32_t, uint32_t> failed;
    return failed;
}

uint32_t NowMs()
{
    return uint32_t(GameTime::GetGameTimeMS());
}

ObjectGuid PlayerGuid(uint32_t guidLow)
{
    return ObjectGuid::Create<HighGuid::Player>(guidLow);
}

Player* OnlinePlayer(uint32_t guidLow)
{
    if (!guidLow)
        return nullptr;
    Player* player = ObjectAccessor::FindPlayer(PlayerGuid(guidLow));
    return player && player->IsInWorld() ? player : nullptr;
}

bool BotAlliance(Player* bot)
{
    return bot && bot->GetTeam() == ALLIANCE;
}

bool InCapital(Player* player, bool alliance)
{
    return player && IsInCapital(player->GetZoneId(), alliance);
}

bool InInstanceMap(Player* player)
{
    if (!player)
        return true;
    Map* map = player->GetMap();
    return map && (map->IsDungeon() || map->IsRaid() || map->IsBattlegroundOrArena());
}

// A real player in the bot's group: the bot must not be pulled away from it.
bool GroupedWithRealPlayer(Player* bot)
{
    Group* group = bot ? bot->GetGroup() : nullptr;
    if (!group)
        return false;
    for (GroupReference const& ref : group->GetMembers())
    {
        Player* member = ref.GetSource();
        if (member && member != bot && member->GetSession() && !member->GetSession()->IsBot())
            return true;
    }
    return false;
}

bool TiedUp(Player* bot)
{
    return InInstanceMap(bot) || GroupedWithRealPlayer(bot);
}

TeleportWait WaitReason(Player* bot)
{
    if (!bot || !bot->GetSession() || !bot->IsInWorld())
        return TeleportWait::Dead;
    return TortoiseBots::EnchantCapital::CanTeleportNow(bot->IsInCombat(), InInstanceMap(bot),
        GroupedWithRealPlayer(bot), bot->IsAlive(), bot->IsInFlight());
}

bool TeleportToCapitalInn(Player* bot)
{
    if (WaitReason(bot) != TeleportWait::Ok)
        return false;
    if (bot->IsMounted())
        bot->Dismount();
    bot->StopMoving();
    CapitalInn const inn = FallbackInn(BotAlliance(bot));
    return bot->TeleportTo(inn.mapId, inn.x, inn.y, inn.z, inn.o);
}

void Whisper(Player* bot, Player* to, char const* text)
{
    if (bot && to && to->IsInWorld())
        bot->Whisper(text, LANG_UNIVERSAL, to);
}

std::vector<EnchantMaterialState> MaterialStates(Player* bot, uint32_t spellId)
{
    std::vector<EnchantMaterialState> out;
    for (ai::GuildOrderEnchantMaterial const& reagent : ai::GuildOrderEnchantReagents(spellId))
    {
        if (!reagent.itemId || !reagent.count)
            continue;
        out.push_back(EnchantMaterialState{ reagent.itemId, reagent.count, BotBagItemCount(bot, reagent.itemId), false });
    }
    return out;
}

bool MaterialsComplete(Player* bot, uint32_t spellId)
{
    std::vector<EnchantMaterialState> const rows = MaterialStates(bot, spellId);
    return EnchantMaterialsComplete(rows.data(), rows.size());
}

std::vector<NeedItem> MissingMaterials(Player* bot, uint32_t spellId)
{
    std::vector<EnchantMaterialState> const rows = MaterialStates(bot, spellId);
    std::vector<NeedItem> out;
    for (EnchantMaterial const& missing : EnchantMissingMaterials(rows.data(), rows.size()))
        out.push_back(NeedItem{ missing.itemId, missing.need });
    return out;
}

// The enchant id the ordered spell applies (SPELL_EFFECT_ENCHANT_ITEM's
// misc value), 0 when the spell is not an enchant.
uint32_t OrderedEnchantId(uint32_t spellId)
{
    SpellInfo const* spell = sSpellMgr->GetSpellInfo(spellId, DIFFICULTY_NONE);
    if (!spell)
        return 0;
    for (SpellEffectInfo const& effect : spell->GetEffects())
        if (effect.Effect == SPELL_EFFECT_ENCHANT_ITEM && effect.MiscValue > 0)
            return uint32_t(effect.MiscValue);
    return 0;
}

uint32_t BotSkill(Player* bot)
{
    if (!bot || !bot->HasSkill(TortoiseBots::prof::SkillEnchanting))
        return 0;
    return uint32_t(bot->GetSkillValue(TortoiseBots::prof::SkillEnchanting));
}

bool SameNeedList(std::vector<NeedItem> const& a, std::vector<NeedItem> const& b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].itemId != b[i].itemId || a[i].count != b[i].count)
            return false;
    return true;
}

void CancelTrade(Player* bot)
{
    if (!bot || !bot->GetSession() || !bot->GetTrader())
        return;
    WorldPackets::Trade::CancelTrade cancel{ WorldPacket(CMSG_CANCEL_TRADE) };
    bot->GetSession()->HandleCancelTradeOpcode(cancel);
}

bool OpenTrade(Player* bot, Player* giver)
{
    if (!bot || !giver || !bot->GetSession())
        return false;
    if (bot->GetTrader() == giver)
        return true;
    if (bot->GetTrader() || giver->GetTrader())
        return false;
    WorldPackets::Trade::InitiateTrade packet{ WorldPacket(CMSG_INITIATE_TRADE) };
    packet.Guid = giver->GetGUID();
    bot->GetSession()->HandleInitiateTradeOpcode(packet);
    return bot->GetTrader() == giver;
}

void AcceptTrade(Player* bot, Player* giver)
{
    if (!bot || !giver || !bot->GetSession() || !bot->GetTradeData() || !giver->GetTradeData())
        return;
    WorldPackets::Trade::AcceptTrade accept{ WorldPacket(CMSG_ACCEPT_TRADE) };
    accept.StateIndex = giver->GetTradeData()->GetServerStateIndex();
    bot->GetSession()->HandleAcceptTradeOpcode(accept);
}

void ResetTrade(Session& session)
{
    session.openedMs = 0;
    session.busySent = false;
    session.begun = false;
    session.spellSet = false;
    session.parkedItemGuidLow = 0;
    session.lastVerdictItemGuid = 0;
    session.lastVerdict = 0;
}

void ResetSession(Session& session)
{
    ResetTrade(session);
    session.giverGuidLow = 0;
    session.orderCreatedAt = 0;
    session.meetRequestedMs = 0;
    session.nextTeleportMs = 0;
    session.tries = 0;
}

void DropSession(Player* bot, uint32_t botGuidLow, Session& session)
{
    if (session.giverGuidLow && bot && bot->GetTrader() &&
        bot->GetTrader()->GetGUID().GetCounter() == session.giverGuidLow)
        CancelTrade(bot);
    ResetSession(session);
    Sessions().erase(botGuidLow);
}

void ClearMeet(Session& session)
{
    session.meetRequestedMs = 0;
    session.nextTeleportMs = 0;
    ResetTrade(session);
}

void FailMeet(Session& session, uint32_t botGuidLow, OrderRecord& record)
{
    ClearMeet(session);
    MeetFailed()[botGuidLow] = NowMs();
    if (record.state == OrderState::Coming || record.state == OrderState::Enchanting)
    {
        bool const need = record.reason == OrderReason::NeedItems;
        record.state = need ? OrderState::Waiting : OrderState::Ready;
        record.reason = need ? OrderReason::NeedItems : OrderReason::None;
        GiveOrder(record);
    }
}

// One refusal whisper per verdict and parked item, so an open window the
// giver is fixing does not spam the chat.
bool VerdictIsNew(Session& session, uint32_t verdict, uint64_t itemGuidLow)
{
    if (session.lastVerdict == verdict && session.lastVerdictItemGuid == itemGuidLow)
        return false;
    session.lastVerdict = verdict;
    session.lastVerdictItemGuid = itemGuidLow;
    return true;
}

void LogLine(Player* bot, OrderRecord const& record, uint32_t skill, char const* state, char const* reason)
{
    TC_LOG_INFO("tortoisebots", "[W9] enchant id={} bot={} spell={} state={} reason={} skill={}",
        record.itemId, bot->GetName(), record.itemId, state, reason, skill);
}

void SetState(OrderRecord& record, OrderState state, OrderReason reason)
{
    record.state = state;
    record.reason = reason;
}

// The materials trade: the giver's normal slots carry the missing reagents,
// nothing else. Accepting hands them over; the row then reopens for the
// enchant trade (or waits for more materials).
void HandleMaterialsTrade(Player* bot, Player* giver, uint32_t botGuidLow, Session& session,
    OrderRecord& record)
{
    TradeData* mine = bot->GetTradeData();
    TradeData* theirs = giver->GetTradeData();
    if (!mine || !theirs)
        return;

    std::vector<EnchantMaterialState> const rows = MaterialStates(bot, record.itemId);
    ai::MaterialsTradeState state;
    for (EnchantMaterialState const& row : rows)
    {
        uint32_t const have = row.inBags;
        uint32_t const need = row.need > have ? row.need - have : 0;
        if (need)
            state.allowed.push_back(ai::EnchantReagentNeed{ row.itemId, need });
    }
    for (uint32_t slot = 0; slot < TRADE_SLOT_TRADED_COUNT; ++slot)
        if (Item* offered = theirs->GetItem(TradeSlots(slot)))
            state.offered.push_back(ai::OfferedStack{ offered->GetEntry(), offered->GetCount() });
    state.traderGold = uint32_t(theirs->GetMoney());

    ai::MaterialsTradeResult const checked = ai::CheckMaterialsTrade(state);
    if (checked.verdict == ai::MaterialsTradeVerdict::NotMaterial)
    {
        Whisper(bot, giver, ai::kMaterialsNotMaterialText);
        CancelTrade(bot);
        SetState(record, OrderState::Coming, OrderReason::None);
        GiveOrder(record);
        return;
    }
    if (checked.verdict == ai::MaterialsTradeVerdict::NoGold)
    {
        Whisper(bot, giver, ai::kMaterialsNoGoldText);
        CancelTrade(bot);
        SetState(record, OrderState::Coming, OrderReason::None);
        GiveOrder(record);
        return;
    }
    if (!theirs->IsAccepted() || mine->IsAccepted())
        return;

    AcceptTrade(bot, giver);
    if (bot->GetTrader())
        return;
    ResetTrade(session);
    bool const complete = MaterialsComplete(bot, record.itemId);
    LogLine(bot, record, BotSkill(bot), complete ? "materials-ok" : "materials-short", "-");
    SetState(record, complete ? OrderState::Coming : OrderState::Waiting,
        complete ? OrderReason::None : OrderReason::NeedItems);
    if (!complete)
        record.needItems = MissingMaterials(bot, record.itemId);
    GiveOrder(record);
}

// The enchant trade: the giver's item fits the spell and the reagents are in
// the bag. The bot records the enchant on the window and accepts once the
// giver accepted; the core applies it in the same accept.
void HandleEnchantTrade(Player* bot, Player* giver, uint32_t botGuidLow, Session& session,
    uint32_t nowMs, OrderRecord& record)
{
    TradeData* mine = bot->GetTradeData();
    TradeData* theirs = giver->GetTradeData();
    if (!mine || !theirs)
        return;

    Item* parked = theirs->GetItem(TRADE_SLOT_NONTRADED);
    uint64_t const parkedGuid = parked ? parked->GetGUID().GetCounter() : 0;

    ai::EnchantTradeState tradeState;
    tradeState.traderIsGiver = true;
    tradeState.hasItem = parked != nullptr;
    tradeState.traderExtras = theirs->GetMoney() != 0;
    for (uint32_t slot = 0; slot < TRADE_SLOT_TRADED_COUNT && !tradeState.traderExtras; ++slot)
        if (theirs->GetItem(TradeSlots(slot)))
            tradeState.traderExtras = true;
    tradeState.replaceConfirmed = false;

    if (parked)
    {
        if (ItemPrototype const* proto = tbcompat::ItemProto(parked->GetEntry()))
        {
            if (SpellInfo const* spell = sSpellMgr->GetSpellInfo(record.itemId, DIFFICULTY_NONE))
            {
                EnchantTargetFacts facts;
                facts.equippedItemClass = spell->EquippedItemClass;
                facts.subClassMask = spell->EquippedItemSubClassMask;
                facts.invTypeMask = spell->EquippedItemInventoryTypeMask;
                facts.itemTarget = true;
                facts.spellId = record.itemId;
                EnchantTargetItem item;
                item.itemClass = proto->Class;
                item.subClass = proto->SubClass;
                item.inventoryType = proto->InventoryType;
                tradeState.fitsEnchant = EnchantFitsItem(facts, item);
            }
        }
        tradeState.hasPermanentEnchant = parked->GetEnchantmentId(PERM_ENCHANTMENT_SLOT) != 0;
    }

    switch (ai::CheckEnchantTrade(tradeState))
    {
        case ai::EnchantTradeVerdict::WrongPlayer:
            Whisper(bot, giver, ai::kEnchantWrongPlayerText);
            CancelTrade(bot);
            FailMeet(session, botGuidLow, record);
            return;
        case ai::EnchantTradeVerdict::Extras:
            if (VerdictIsNew(session, uint32_t(ai::EnchantTradeVerdict::Extras), parkedGuid))
                Whisper(bot, giver, ai::kEnchantExtrasText);
            return;
        case ai::EnchantTradeVerdict::NoItem:
            if (VerdictIsNew(session, uint32_t(ai::EnchantTradeVerdict::NoItem), parkedGuid))
                Whisper(bot, giver, ai::kEnchantNoItemText);
            return;
        case ai::EnchantTradeVerdict::WrongItem:
            if (VerdictIsNew(session, uint32_t(ai::EnchantTradeVerdict::WrongItem), parkedGuid))
                Whisper(bot, giver, ai::kEnchantWrongItemText);
            return;
        case ai::EnchantTradeVerdict::AskReplace:
            // This build has no "yes" whisper hook for a replace answer: ask
            // and let the giver clear the window, then Meet again with a
            // clean item.
            if (VerdictIsNew(session, uint32_t(ai::EnchantTradeVerdict::AskReplace), parkedGuid))
                Whisper(bot, giver, "That item already carries an enchant. Bring one without it.");
            CancelTrade(bot);
            FailMeet(session, botGuidLow, record);
            return;
        case ai::EnchantTradeVerdict::Ok:
            break;
    }

    if (!session.spellSet)
    {
        // The client's trade-slot cast, in the state it leaves behind:
        // TradeHandler reads it on the accept and casts the spell at the
        // giver's non-traded slot (Spell.cpp:3831 stores the cast).
        mine->SetSpell(record.itemId);
        session.spellSet = true;
        session.parkedItemGuidLow = parkedGuid;
        return;
    }

    if (!theirs->IsAccepted() || mine->IsAccepted())
        return;

    AcceptTrade(bot, giver);
    if (bot->GetTrader())
        return;

    ResetTrade(session);
    Player* owner = OnlinePlayer(record.giverGuidLow);
    uint32_t const want = OrderedEnchantId(record.itemId);
    Item* done = owner && parkedGuid
        ? owner->GetItemByGuid(ObjectGuid::Create<HighGuid::Item>(parkedGuid)) : nullptr;
    bool const verified = done && EnchantDoneVerified(done->GetEnchantmentId(PERM_ENCHANTMENT_SLOT), want);

    if (verified)
    {
        SetState(record, OrderState::Done, OrderReason::None);
        record.needItems.clear();
        GiveOrder(record);
        Whisper(bot, owner, "Enchanted, thank you.");
        LogLine(bot, record, BotSkill(bot), "done", "-");
        ClearMeet(session);
        MeetFailed().erase(botGuidLow);
    }
    else
    {
        SetState(record, OrderState::Ready, OrderReason::None);
        GiveOrder(record);
        LogLine(bot, record, BotSkill(bot), "verify-failed", "-");
        ClearMeet(session);
        MeetFailed()[botGuidLow] = nowMs;
    }
}
} // namespace

bool IsEnchantReagent(uint32_t spellId, uint32_t itemId)
{
    if (!spellId || !itemId)
        return false;
    static std::unordered_map<uint32_t, std::vector<uint32_t>> reagentsBySpell;
    auto it = reagentsBySpell.find(spellId);
    if (it == reagentsBySpell.end())
    {
        std::vector<uint32_t> ids;
        for (ai::GuildOrderEnchantMaterial const& reagent : ai::GuildOrderEnchantReagents(spellId))
            if (reagent.itemId)
                ids.push_back(reagent.itemId);
        it = reagentsBySpell.emplace(spellId, std::move(ids)).first;
    }
    return std::find(it->second.begin(), it->second.end(), itemId) != it->second.end();
}

bool IsEnchantOrderTrader(Player* bot, Player* trader)
{
    if (!bot || !trader)
        return false;
    std::optional<OrderRecord> const order = FindOrder(bot->GetGUID().GetCounter());
    if (!order || order->type != OrderType::Enchant || order->cancelled || IsFinished(order->state))
        return false;
    bool const meetState = order->state == OrderState::Ready || order->state == OrderState::Coming ||
        order->state == OrderState::Enchanting ||
        (order->state == OrderState::Waiting && order->reason == OrderReason::NeedItems);
    return meetState && trader->GetGUID().GetCounter() == order->giverGuidLow;
}

bool HasActiveEnchantErrand(Player* bot)
{
    if (!bot)
        return false;
    std::optional<OrderRecord> const order = FindOrder(bot->GetGUID().GetCounter());
    if (!order || order->type != OrderType::Enchant || order->cancelled || IsFinished(order->state))
        return false;
    auto const it = Sessions().find(bot->GetGUID().GetCounter());
    if (it != Sessions().end() && it->second.meetRequestedMs)
        return true;
    return order->state == OrderState::Coming || order->state == OrderState::Enchanting;
}

void ForgetEnchantOrder(uint32_t botGuidLow)
{
    Sessions().erase(botGuidLow);
    MeetFailed().erase(botGuidLow);
}

void TickEnchantOrder(Player* bot)
{
    if (!bot || !bot->IsInWorld() || !bot->FindMap() || !bot->IsAlive() || bot->IsBeingTeleported())
        return;
    uint32_t const botGuidLow = bot->GetGUID().GetCounter();
    std::optional<OrderRecord> stored = FindOrder(botGuidLow);

    if (!stored || stored->type != OrderType::Enchant || stored->cancelled || IsFinished(stored->state))
    {
        auto const it = Sessions().find(botGuidLow);
        if (it != Sessions().end())
            DropSession(bot, botGuidLow, it->second);
        return;
    }
    Session& session = Sessions()[botGuidLow];
    if (session.giverGuidLow &&
        (session.giverGuidLow != stored->giverGuidLow || session.orderCreatedAt != stored->createdAt))
        ResetSession(session);
    if (!session.giverGuidLow)
    {
        session.giverGuidLow = stored->giverGuidLow;
        session.orderCreatedAt = stored->createdAt;
    }

    OrderRecord record = *stored;
    if (!session.giverGuidLow)
    {
        session.giverGuidLow = record.giverGuidLow;
        session.orderCreatedAt = record.createdAt;
        // Coming/Enchanting do not survive a restart (or a relog): no Meet
        // request and no trade session load back, so the row waits again.
        bool const need = record.reason == OrderReason::NeedItems;
        OrderState const back = OrderState(EnchantRestartState(
            uint8_t(OrderState::Coming), uint8_t(OrderState::Enchanting),
            uint8_t(OrderState::Ready), uint8_t(OrderState::Waiting), need, uint8_t(record.state)));
        if (back != record.state)
        {
            SetState(record, back, need ? OrderReason::NeedItems : OrderReason::None);
            GiveOrder(record);
            LogLine(bot, record, BotSkill(bot), StateToken(back), ReasonToken(record.reason));
        }
    }

    // Gathering is the reagent gate: the order may turn ready only when the
    // bags hold one cast. There is no bank or auction leg (bags only), so a
    // shortfall waits for the giver's materials trade or for a mail the bot
    // takes itself.
    if (record.state == OrderState::Working || record.state == OrderState::Waiting ||
        record.state == OrderState::Ready)
    {
        bool const complete = MaterialsComplete(bot, record.itemId);
        if (complete)
        {
            if (record.state != OrderState::Ready || record.reason != OrderReason::None)
            {
                SetState(record, OrderState::Ready, OrderReason::None);
                record.needItems.clear();
                GiveOrder(record);
                LogLine(bot, record, BotSkill(bot), "ready", "-");
                if (Player* giver = OnlinePlayer(record.giverGuidLow))
                    Whisper(bot, giver, BotAlliance(bot) ? "Your enchant is ready. Meet me in Stormwind."
                                                         : "Your enchant is ready. Meet me in Orgrimmar.");
            }
        }
        else
        {
            std::vector<NeedItem> const missing = MissingMaterials(bot, record.itemId);
            if (record.state != OrderState::Waiting || record.reason != OrderReason::NeedItems ||
                !SameNeedList(record.needItems, missing))
            {
                SetState(record, OrderState::Waiting, OrderReason::NeedItems);
                record.needItems = missing;
                GiveOrder(record);
                LogLine(bot, record, BotSkill(bot), "waiting", "needitems");
                std::string detail;
                for (NeedItem const& need : missing)
                    detail += (detail.empty() ? "" : ",") + std::to_string(need.itemId) + ":" + std::to_string(need.count);
                TC_LOG_INFO("tortoisebots", "[W9] enchant-need bot={} spell={} missing={}", bot->GetName(),
                    record.itemId, detail.empty() ? "-" : detail);
            }
        }
    }

    bool const meetState = record.state == OrderState::Ready ||
        (record.state == OrderState::Waiting && record.reason == OrderReason::NeedItems) ||
        record.state == OrderState::Coming || record.state == OrderState::Enchanting;
    if (!meetState)
        return;

    Player* giver = OnlinePlayer(record.giverGuidLow);
    bool const alliance = BotAlliance(bot);
    if (!giver || !InCapital(giver, alliance))
    {
        if (record.state == OrderState::Coming || record.state == OrderState::Enchanting)
        {
            CancelTrade(bot);
            FailMeet(session, botGuidLow, record);
            LogLine(bot, record, BotSkill(bot), "meet-lost", "giver-left");
        }
        else if (session.meetRequestedMs)
            FailMeet(session, botGuidLow, record);
        return;
    }

    if (TiedUp(bot) || bot->IsInCombat())
        return;

    // A window with anyone but the giver is refused in every Meet state.
    if (Player* stranger = bot->GetTrader())
    {
        if (stranger != giver)
        {
            Whisper(bot, stranger, ai::kEnchantWrongPlayerText);
            CancelTrade(bot);
            ClearMeet(session);
            if (record.state == OrderState::Coming || record.state == OrderState::Enchanting)
            {
                bool const need = record.reason == OrderReason::NeedItems;
                SetState(record, need ? OrderState::Waiting : OrderState::Ready,
                    need ? OrderReason::NeedItems : OrderReason::None);
                GiveOrder(record);
            }
            LogLine(bot, record, BotSkill(bot), "stranger", "-");
            return;
        }
    }

    uint32_t const nowMs = NowMs();
    if (Player* trader = bot->GetTrader())
    {
        // A giver-opened window works the same as a bot-opened one.
        if (record.state != OrderState::Enchanting)
        {
            SetState(record, OrderState::Enchanting, OrderReason::None);
            GiveOrder(record);
        }
        if (!session.openedMs)
        {
            session.openedMs = nowMs;
            session.tries = session.tries ? session.tries : 1;
            ResetTrade(session);
            session.openedMs = nowMs;
        }

        // The headless side answers the proposal the way a client does: the
        // proposer clears its own busy flag, then the window begins.
        TradeData* mine = bot->GetTradeData();
        if (mine && mine->GetSerial() != session.serial)
        {
            session.serial = mine->GetSerial();
            session.busySent = false;
            session.begun = false;
        }
        if (mine && mine->IsAwaitingProposerBusy() && !session.busySent)
        {
            WorldPackets::Trade::BusyTrade busy{ WorldPacket(CMSG_BUSY_TRADE) };
            bot->GetSession()->HandleBusyTradeOpcode(busy);
            session.busySent = true;
        }
        else if (mine && !session.begun)
        {
            WorldPackets::Trade::BeginTrade begin{ WorldPacket(CMSG_BEGIN_TRADE) };
            bot->GetSession()->HandleBeginTradeOpcode(begin);
            session.begun = true;
        }

        bool const traderActive = trader->GetTradeData() && trader->GetTradeData()->IsAccepted();
        switch (ai::CheckTradeTry(nowMs, session.openedMs, session.tries ? session.tries : 1, traderActive))
        {
            case ai::TradeTryAction::CancelRetry:
                CancelTrade(bot);
                ResetTrade(session);
                SetState(record, OrderState::Coming, OrderReason::None);
                GiveOrder(record);
                LogLine(bot, record, BotSkill(bot), "retry", "idle");
                return;
            case ai::TradeTryAction::GiveUp:
                Whisper(bot, trader, ai::kTradeGiveUpText);
                CancelTrade(bot);
                ResetTrade(session);
                FailMeet(session, botGuidLow, record);
                LogLine(bot, record, BotSkill(bot), "giveup", "timeout");
                return;
            case ai::TradeTryAction::KeepWaiting:
                break;
        }

        if (MaterialsComplete(bot, record.itemId))
            HandleEnchantTrade(bot, giver, botGuidLow, session, nowMs, record);
        else
            HandleMaterialsTrade(bot, giver, botGuidLow, session, record);
        return;
    }

    // No window: a closed enchant trade is verified (the giver may have
    // accepted last), otherwise the walk reopens it.
    if (record.state == OrderState::Enchanting)
    {
        if (session.spellSet && session.parkedItemGuidLow)
        {
            uint32_t const want = OrderedEnchantId(record.itemId);
            Item* done = giver->GetItemByGuid(ObjectGuid::Create<HighGuid::Item>(session.parkedItemGuidLow));
            bool const verified = done && EnchantDoneVerified(done->GetEnchantmentId(PERM_ENCHANTMENT_SLOT), want);
            ResetTrade(session);
            if (verified)
            {
                SetState(record, OrderState::Done, OrderReason::None);
                record.needItems.clear();
                GiveOrder(record);
                Whisper(bot, giver, "Enchanted, thank you.");
                LogLine(bot, record, BotSkill(bot), "done", "-");
                ClearMeet(session);
                MeetFailed().erase(botGuidLow);
            }
            else
            {
                SetState(record, OrderState::Ready, OrderReason::None);
                GiveOrder(record);
                LogLine(bot, record, BotSkill(bot), "verify-failed", "-");
                ClearMeet(session);
                MeetFailed()[botGuidLow] = nowMs;
            }
            return;
        }
        ResetTrade(session);
        SetState(record, OrderState::Coming, OrderReason::None);
        GiveOrder(record);
        return;
    }

    if (!session.meetRequestedMs)
        return;

    if (!InCapital(bot, alliance))
    {
        if (session.nextTeleportMs && nowMs < session.nextTeleportMs)
            return;
        session.nextTeleportMs = nowMs + 5000;
        if (TeleportToCapitalInn(bot))
            LogLine(bot, record, BotSkill(bot), "teleport", "-");
        return;
    }

    if (record.state == OrderState::Ready ||
        (record.state == OrderState::Waiting && record.reason == OrderReason::NeedItems))
    {
        SetState(record, OrderState::Coming, OrderReason::None);
        GiveOrder(record);
    }

    float const distance = bot->GetDistance(giver);
    if (distance <= TRADE_DISTANCE)
    {
        if (session.tries >= ai::kEnchantTradeMaxTries)
        {
            Whisper(bot, giver, ai::kTradeGiveUpText);
            ResetTrade(session);
            FailMeet(session, botGuidLow, record);
            LogLine(bot, record, BotSkill(bot), "giveup", "tries");
            return;
        }
        if (OpenTrade(bot, giver))
        {
            ++session.tries;
            session.openedMs = nowMs;
            ResetTrade(session);
            session.openedMs = nowMs;
            SetState(record, OrderState::Enchanting, OrderReason::None);
            GiveOrder(record);
            LogLine(bot, record, BotSkill(bot), "trade", "-");
        }
        return;
    }

    if (distance > 60.0f && bot->GetMapId() == giver->GetMapId())
    {
        if (bot->IsMounted())
            bot->Dismount();
        bot->NearTeleportTo(giver->GetPositionX() + 1.5f, giver->GetPositionY(), giver->GetPositionZ(),
            giver->GetOrientation());
        return;
    }
    if (!bot->isMoving())
        bot->GetMotionMaster()->MovePoint(0, giver->GetPositionX(), giver->GetPositionY(), giver->GetPositionZ());
}

bool HandleEnchantCreate(ChatHandler* handler, TortoiseBots::h3::Words const& words, Player* sender)
{
    using TortoiseBots::h3::Fail;
    if (words.w.size() < 4)
        return Fail(handler, words, "guildorder", "usage",
            "create <bot> enchant <spell link or id>");
    if (words.w.size() > 4)
        return Fail(handler, words, "guildorder", "unexpected-argument", words.w[4]);

    Player* bot = TortoiseBots::h3::FindOnline(words.w[1]);
    if (!bot || !TortoiseBots::h3::IsHostBot(bot))
        return Fail(handler, words, "guildorder", "no-bot", words.w[1]);
    if (!sender)
        return Fail(handler, words, "guildorder", "no-character",
            "an enchant order needs the giver's character (the item is theirs)");
    uint64_t const guildId = tbhost::guild::GetGuildId(bot);
    if (!guildId)
        return Fail(handler, words, "guildorder", "not-in-guild", bot->GetName());
    {
        std::string code, detail;
        if (!tbhost::guild_order_access::MayGiveOrders(uint32_t(guildId), sender, code, detail))
            return Fail(handler, words, "guildorder", code, detail);
    }

    uint32_t spellId = 0;
    if (!ParseEnchantSpellToken(words.w[3], spellId))
        return Fail(handler, words, "guildorder", "bad-spell", words.w[3]);

    std::vector<EnchantEntry> const catalogue = ai::GuildOrderEnchants();
    auto const entry = std::find_if(catalogue.begin(), catalogue.end(),
        [&](EnchantEntry const& row) { return row.spellId == spellId; });
    EnchantGiveArgs args;
    args.inCatalogue = entry != catalogue.end();
    args.knownByBot = bot->HasSpell(spellId);
    args.skillEnough = !args.inCatalogue || entry->reqSkill == 0 ||
        BotSkill(bot) >= entry->reqSkill;
    if (OrderReason const verdict = EnchantGiveVerdict(args); verdict != OrderReason::None)
        return Fail(handler, words, "guildorder", ReasonToken(verdict), EnchantGiveRefusal(verdict));

    // An unfinished enchant order of another giver blocks a new one; the
    // guild master or a rank that may remove members replaces it.
    std::optional<OrderRecord> const existing = FindOrder(bot->GetGUID().GetCounter());
    if (existing && !existing->cancelled && existing->type == OrderType::Enchant &&
        !IsFinished(existing->state))
    {
        bool const sameGiver = sender && existing->giverGuidLow == sender->GetGUID().GetCounter();
        bool const mayOverride = sender &&
            tbhost::guild::HasAnyRankRight(guildId, sender->GetGUID(), GR_RIGHT_REMOVE);
        if (DecideEnchantReplace(true, sameGiver, mayOverride) == EnchantReplaceVerdict::Busy)
        {
            std::string giverName = "?";
            if (Player* giver = OnlinePlayer(existing->giverGuidLow))
                giverName = giver->GetName();
            return Fail(handler, words, "guildorder", "busy",
                bot->GetName() + std::string(" is busy with an enchant order for ") + giverName);
        }
    }

    if (!IsOrderStoreAvailable())
        return Fail(handler, words, "guildorder", "store-unavailable", "tortoise_bots_guild_order missing");

    OrderRecord record;
    record.botGuidLow = bot->GetGUID().GetCounter();
    record.guildId = uint32_t(guildId);
    record.giverGuidLow = sender->GetGUID().GetCounter();
    record.rootGiverGuidLow = record.giverGuidLow;
    record.type = OrderType::Enchant;
    record.itemId = spellId;
    record.amount = 1;
    record.hours = 0;
    record.state = OrderState::Working;
    record.reason = OrderReason::None;
    if (!GiveOrder(record))
        return Fail(handler, words, "guildorder", "store-write-failed", bot->GetName());

    Sessions()[record.botGuidLow] = Session{};
    MeetFailed().erase(record.botGuidLow);
    TC_LOG_INFO("tortoisebots", "[W9] enchant-give bot={} spell={} giver={} skill={}", bot->GetName(), spellId,
        sender->GetName(), BotSkill(bot));

    TortoiseBots::h3::Reply reply(handler, words, "guildorder");
    reply.Begin();
    TortoiseBots::h3::Rec rec("guildorder");
    rec.S("op", "create").S("bot", bot->GetName()).N("guild", guildId).N("spell", spellId)
        .S("name", args.inCatalogue ? std::string(entry->name) : std::string())
        .N("want", 1).S("type", "enchant").S("state", StateToken(record.state));
    reply.Line(rec);
    reply.End(TortoiseBots::h3::Rec("guildorder-end"));
    return true;
}

bool HandleEnchantMeet(ChatHandler* handler, TortoiseBots::h3::Words const& words, Player* sender)
{
    using TortoiseBots::h3::Fail;
    if (words.w.size() < 2)
        return Fail(handler, words, "guildorder", "usage", "meet <bot>");
    if (!sender)
        return Fail(handler, words, "guildorder", "no-character", "Meet needs the giver's character");
    Player* bot = TortoiseBots::h3::FindOnline(words.w[1]);
    if (!bot || !TortoiseBots::h3::IsHostBot(bot))
        return Fail(handler, words, "guildorder", "no-bot", words.w[1]);
    uint64_t const guildId = tbhost::guild::GetGuildId(bot);
    if (!guildId)
        return Fail(handler, words, "guildorder", "not-in-guild", bot->GetName());
    if (sender->GetGuildId() != guildId)
        return Fail(handler, words, "guildorder", "wrong-guild", "the bot is not in your guild");

    uint32_t const botGuidLow = bot->GetGUID().GetCounter();
    std::optional<OrderRecord> const row = FindOrder(botGuidLow);
    bool const live = row && !row->cancelled && row->type == OrderType::Enchant &&
        !IsFinished(row->state) && row->giverGuidLow == sender->GetGUID().GetCounter();
    if (!live)
        return Fail(handler, words, "guildorder", "noorder", "That bot has no enchant order from you.");

    MeetFacts facts;
    facts.liveEnchantOrder = true;
    facts.giverInCapital = GiverInBotCapital(sender->GetZoneId(), BotAlliance(bot));
    facts.botTiedUp = TiedUp(bot);
    if (row->state == OrderState::Working)
        facts.gathering = true;
    else if (row->state == OrderState::Waiting)
        facts.gathering = row->reason != OrderReason::NeedItems;
    if (row->state == OrderState::Waiting && row->reason == OrderReason::NeedItems)
    {
        facts.nothingToBring = true;
        for (NeedItem const& need : row->needItems)
            if (need.itemId && sender->GetItemCount(need.itemId) > 0)
                facts.nothingToBring = false;
    }
    facts.cooldownLeftMs = MeetCooldownLeftMs(NowMs(), MeetFailed()[botGuidLow]);

    TortoiseBots::h3::Reply reply(handler, words, "guildorder");
    reply.Begin();
    TortoiseBots::h3::Rec rec("guildorder");
    rec.S("op", "meet").S("bot", bot->GetName()).N("spell", row->itemId);
    MeetVerdict const verdict = DecideMeet(facts);
    switch (verdict)
    {
        case MeetVerdict::Cooldown:
        {
            uint32_t const minutes = (facts.cooldownLeftMs + 59999) / 60000;
            rec.S("status", "refused").S("code", "cooldown").N("wait", minutes);
            reply.Line(rec);
            reply.End(TortoiseBots::h3::Rec("guildorder-end"));
            return true;
        }
        case MeetVerdict::NothingToBring:
            rec.S("status", "refused").S("code", "nothingtobring").S("msg", "I still need materials you do not carry.");
            reply.Line(rec);
            reply.End(TortoiseBots::h3::Rec("guildorder-end"));
            return true;
        case MeetVerdict::NotCapital:
            rec.S("status", "refused").S("code", "notcapital")
                .S("msg", BotAlliance(bot) ? "Meet me in Stormwind." : "Meet me in Orgrimmar.");
            reply.Line(rec);
            reply.End(TortoiseBots::h3::Rec("guildorder-end"));
            return true;
        case MeetVerdict::NoOrder:
            return Fail(handler, words, "guildorder", "noorder", "That bot has no enchant order from you.");
        case MeetVerdict::Later:
        case MeetVerdict::Gathering:
        case MeetVerdict::Coming:
            break;
    }

    Session& session = Sessions()[botGuidLow];
    session.giverGuidLow = row->giverGuidLow;
    session.orderCreatedAt = row->createdAt;
    session.meetRequestedMs = NowMs();
    session.tries = 0;
    ResetTrade(session);
    session.meetRequestedMs = NowMs();
    rec.S("status", verdict == MeetVerdict::Later ? "later"
        : (verdict == MeetVerdict::Gathering ? "gathering" : "coming"));
    reply.Line(rec);
    reply.End(TortoiseBots::h3::Rec("guildorder-end"));
    return true;
}

} // namespace guild_orders
} // namespace tbhost
