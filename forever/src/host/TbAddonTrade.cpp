// The `trade` addon verb and headless trade-window driver (TBF #134 piece P134-C).
#include "host/TbAddonTrade.h"
#include "host/TbAddonTradePure.h"

#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1

#include "host/TbAddonChannel.h"
#include "host/cmd/TbCmdBinding.h"
#include "host/player/TbPlayerAuthPure.h"
#include "host/run/TbRunHooks.h"
#include "TbBotMgr.h"

#include "BattlenetAccountMgr.h"
#include "CharacterCache.h"
#include "Group.h"
#include "Item.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "RBAC.h"
#include "ScriptMgr.h"
#include "TradeData.h"
#include "TradePackets.h"
#include "WorldPacket.h"
#include "WorldSession.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace TortoiseBots
{
namespace trade
{
namespace
{
std::unordered_map<std::uint64_t, TradeBotState> g_tradeStates;

std::uint32_t CallerBnet(Player* requester)
{
    WorldSession* session = requester ? requester->GetSession() : nullptr;
    if (!session)
        return 0;
    std::uint32_t const bnet = session->GetBattlenetAccountId();
    if (!bnet || Battlenet::AccountMgr::GetIdByGameAccount(session->GetAccountId()) != bnet)
        return 0;
    return bnet;
}

player::BotFacts FactsFor(Player* requester, Player* bot, std::uint32_t bnet)
{
    player::BotFacts facts;
    if (!bot)
        return facts;
    ObjectGuid const guid = bot->GetGUID();
    facts.isBot = BotMgr::Instance().IsBot(guid);
    std::uint32_t const gameAccount = sCharacterCache->GetCharacterAccountIdByGuid(guid);
    if (bnet && gameAccount && Battlenet::AccountMgr::GetIdByGameAccount(gameAccount) == bnet)
        facts.account = bnet;
    facts.master = BotMgr::Instance().MasterOf(guid).GetCounter();
    facts.commandedByCaller = requester && cmd::IsCommandedBy(requester, bot);
    if (Group* group = bot->GetGroup())
    {
        ObjectGuid const leader = group->GetLeaderGUID();
        facts.groupLeader = leader.GetCounter();
        facts.leaderIsBot = BotMgr::Instance().IsBot(leader);
    }
    facts.inRun = run::BotInRunningRun(bot);
    return facts;
}

player::Verdict CheckOwnerVerdict(Player* requester, Player* bot)
{
    WorldSession* const session = requester ? requester->GetSession() : nullptr;
    if (!session)
        return player::Verdict::NotYours;
    player::Caller caller;
    caller.gm = session->HasPermission(rbac::RBAC_PERM_COMMAND_GM);
    caller.account = CallerBnet(requester);
    caller.guid = requester->GetGUID().GetCounter();
    return player::Decide(caller, FactsFor(requester, bot, caller.account), player::Tier::Owner);
}

bool OpenTradeWith(Player* bot, Player* requester)
{
    if (!bot || !requester || !bot->GetSession())
        return false;
    if (bot->GetTrader() == requester)
        return true;
    if (bot->GetTrader() || requester->GetTrader())
        return false;

    WorldPackets::Trade::InitiateTrade packet{ WorldPacket(CMSG_INITIATE_TRADE) };
    packet.Guid = requester->GetGUID();
    bot->GetSession()->HandleInitiateTradeOpcode(packet);
    return bot->GetTrader() == requester;
}

void HandleTradeVerb(Player* requester, std::string_view args)
{
    if (!requester || !requester->GetSession())
        return;

    ParsedTradeArgs const parsed = ParseTradeArgs(args);
    if (!parsed.valid)
    {
        addon::Err(requester, "trade", "invalid", "Usage: trade [<bot>]");
        return;
    }

    Player* bot = nullptr;
    if (parsed.hasBotName)
    {
        bot = ObjectAccessor::FindPlayerByName(parsed.botName);
    }
    else
    {
        Unit* selected = requester->GetSelectedUnit();
        bot = selected ? selected->ToPlayer() : nullptr;
    }

    if (!bot || !BotMgr::Instance().IsBot(bot->GetGUID()) || !bot->IsInWorld())
    {
        addon::Err(requester, "trade", "no-bot", "Target an alive companion bot to trade.");
        return;
    }

    player::Verdict const verdict = CheckOwnerVerdict(requester, bot);
    if (verdict != player::Verdict::Allowed)
    {
        addon::Err(requester, "trade", player::CodeOf(verdict), "You cannot trade with that bot.");
        return;
    }

    if (!bot->IsAlive())
    {
        addon::Err(requester, "trade", "dead", "The bot must be alive.");
        return;
    }

    if (!OpenTradeWith(bot, requester))
    {
        addon::Err(requester, "trade", "busy", "Trade could not be opened (busy, too far, or in combat).");
        return;
    }

    std::string const scope = "bot:" + addon::Safe(bot->GetName());
    addon::Ack(requester, "trade", scope, 1, "open");
}

class tortoisebots_trade_tick : public WorldScript
{
public:
    tortoisebots_trade_tick() : WorldScript("tortoisebots_trade_tick") { }

    void OnUpdate(uint32 /*diff*/) override
    {
        if (!BotMgr::Instance().Enabled())
        {
            g_tradeStates.clear();
            return;
        }

        std::vector<std::uint64_t> activeBotCounters;
        for (ObjectGuid const& guid : BotMgr::Instance().BotGuids())
        {
            Player* bot = ObjectAccessor::FindConnectedPlayer(guid);
            if (!bot || !bot->IsInWorld() || !bot->GetSession())
                continue;

            TradeData* myTrade = bot->GetTradeData();
            if (!myTrade)
                continue;

            std::uint64_t const key = guid.GetCounter();
            activeBotCounters.push_back(key);

            Player* trader = myTrade->GetTrader();
            TradeData* hisTrade = trader ? trader->GetTradeData() : nullptr;

            TradeTickInput in;
            in.hasTrade = true;
            in.hasPartnerTrade = hisTrade && hisTrade->GetTrader() == bot;
            in.serial = myTrade->GetSerial();
            in.awaitingProposerBusy = myTrade->IsAwaitingProposerBusy();
            in.traderAllowed = trader && CheckOwnerVerdict(trader, bot) == player::Verdict::Allowed;
            if (in.hasPartnerTrade)
            {
                in.hisAccepted = hisTrade->IsAccepted();
                in.myAccepted = myTrade->IsAccepted();
                in.hisServerStateIndex = hisTrade->GetServerStateIndex();
            }

            TradeBotState prevState;
            if (auto it = g_tradeStates.find(key); it != g_tradeStates.end())
                prevState = it->second;

            TradeTickDecision const decision = DecideTradeTick(in, prevState);
            g_tradeStates[key] = decision.nextState;

            switch (decision.action)
            {
                case TradeTickAction::Cancel:
                {
                    WorldPackets::Trade::CancelTrade cancel{ WorldPacket(CMSG_CANCEL_TRADE) };
                    bot->GetSession()->HandleCancelTradeOpcode(cancel);
                    break;
                }
                case TradeTickAction::BeginTrade:
                {
                    WorldPackets::Trade::BeginTrade begin{ WorldPacket(CMSG_BEGIN_TRADE) };
                    bot->GetSession()->HandleBeginTradeOpcode(begin);
                    break;
                }
                case TradeTickAction::AcceptTrade:
                {
                    WorldPackets::Trade::AcceptTrade accept{ WorldPacket(CMSG_ACCEPT_TRADE) };
                    accept.StateIndex = in.hisServerStateIndex;
                    bot->GetSession()->HandleAcceptTradeOpcode(accept);
                    break;
                }
                case TradeTickAction::None:
                    break;
            }
        }

        for (auto it = g_tradeStates.begin(); it != g_tradeStates.end();)
        {
            bool stillActive = false;
            for (std::uint64_t counter : activeBotCounters)
            {
                if (counter == it->first)
                {
                    stillActive = true;
                    break;
                }
            }
            if (!stillActive)
                it = g_tradeStates.erase(it);
            else
                ++it;
        }
    }
};
} // namespace

void RegisterAddonVerbsTrade()
{
    addon::RegisterVerb("trade", &HandleTradeVerb);
    new tortoisebots_trade_tick();
}

bool HandleItemTrade(Player* requester, std::string_view botName)
{
    if (!requester || !requester->GetSession())
        return false;

    Player* bot = ObjectAccessor::FindPlayerByName(std::string(botName));
    if (!bot || !BotMgr::Instance().IsBot(bot->GetGUID()) || !bot->IsInWorld())
    {
        addon::Err(requester, "item trade", "no-bot", "Unknown or uncontrollable bot name.");
        return true;
    }

    player::Verdict const verdict = CheckOwnerVerdict(requester, bot);
    if (verdict != player::Verdict::Allowed)
    {
        addon::Err(requester, "item trade", player::CodeOf(verdict), "Unknown or uncontrollable bot name.");
        return true;
    }

    if (!bot->IsAlive())
    {
        addon::Err(requester, "item trade", "dead", "The bot must be alive.");
        return true;
    }

    if (!OpenTradeWith(bot, requester))
    {
        addon::Err(requester, "item trade", "busy", "Trade could not be opened (busy, too far, or in combat).");
        return true;
    }

    std::string const scope = "bot:" + addon::Safe(bot->GetName());
    addon::Ack(requester, "item trade", scope, 1, "open");
    return true;
}

bool PlaceItemInOpenTrade(Player* requester, Player* bot, Item* item, std::uint8_t bag, std::uint8_t slot)
{
    if (!requester || !bot || !item || !bot->GetSession())
        return false;

    std::string const scope = "bot:" + addon::Safe(bot->GetName());
    std::uint32_t const itemId = item->GetEntry();

    if (!item->CanBeTraded(false, true) || item->IsBindedNotWith(requester))
    {
        addon::Err(requester, "item give", "no-trade", "This item cannot be traded (soulbound or quest item).");
        return true;
    }

    if (bot->GetTrader() != requester)
    {
        if (!OpenTradeWith(bot, requester))
        {
            addon::Err(requester, "item give", "busy", "Trade could not be opened (busy, too far, or in combat).");
            return true;
        }
        addon::Ack(requester, "item give", scope, 1, std::to_string(itemId) + " pending");
        return true;
    }

    TradeData* trade = bot->GetTradeData();
    if (!trade)
    {
        addon::Err(requester, "item give", "busy", "Trade could not be opened (busy, too far, or in combat).");
        return true;
    }

    if (trade->HasItem(item->GetGUID()))
    {
        addon::Ack(requester, "item give", scope, 1, std::to_string(itemId));
        return true;
    }

    std::array<bool, kTradedSlotCount> occupied{};
    for (std::size_t i = 0; i < kTradedSlotCount; ++i)
        occupied[i] = trade->GetItem(TradeSlots(i)) != nullptr;

    int const tradeSlot = FindFreeTradeSlot(occupied);
    if (tradeSlot < 0)
    {
        addon::Err(requester, "item give", "trade-full", "The trade window is full.");
        return true;
    }

    WorldPackets::Trade::SetTradeItem packet{ WorldPacket(CMSG_SET_TRADE_ITEM) };
    packet.TradeSlot = static_cast<uint8>(tradeSlot);
    packet.PackSlot = bag;
    packet.ItemSlotInPack = slot;
    bot->GetSession()->HandleSetTradeItemOpcode(packet);

    if (!trade->HasItem(item->GetGUID()))
    {
        addon::Err(requester, "item give", "failed", "The trade refused this item.");
        return true;
    }

    addon::Ack(requester, "item give", scope, 1, std::to_string(itemId));
    return true;
}

} // namespace trade
} // namespace TortoiseBots

#else

namespace TortoiseBots
{
namespace trade
{
void RegisterAddonVerbsTrade() { }
bool HandleItemTrade(Player*, std::string_view) { return false; }
bool PlaceItemInOpenTrade(Player*, Player*, Item*, std::uint8_t, std::uint8_t) { return false; }
} // namespace trade
} // namespace TortoiseBots

#endif
