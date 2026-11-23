#include "host/TbHostCraftOrderPure.h"
#include "host/TbHostGuildOrderFish.h"
#include "host/TbHostGuildOrders.h"
#include "host/TbHostGuildOrderAccess.h"
#include "host/TbHostGuildOrderReassign.h"
#include "host/TbHostGuildOrderDisenchant.h"
#include "host/TbHostGuildOrderQueues.h"

#include "SpellInfo.h"
#include "SpellMgr.h"

#include "TbBotMgr.h"
#include "compat/TbCompat_CharStore.h"
#include "compat/TbItemPrototype.h"
#include "compat/TbSqlNames.h"
#include "host/TbH3.h"
#include "host/TbHostAuction.h"
#include "host/TbHostGuild.h"
#include "host/TbHostGuildOrderRead.h"
#include "host/TbHostGuildPure.h"
#include "host/TbHostProfession.h"
#include "host/TbHostTown.h"

#include "Bag.h"
#include "CellImpl.h"
#include "CharacterCache.h"
#include "Chat.h"
#include "Creature.h"
#include "DatabaseEnv.h"
#include "GameObject.h"
#include "GameTime.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Item.h"
#include "Log.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "StringFormat.h"

#include "playerbot/policy/ah/AhBuyPolicy.h"
#include "PlayerbotAIStorage.h"
#include "runtime/GuildOrderBringPolicy.h"
#include "runtime/GuildOrderContract.h"
#include "runtime/GuildOrderDeliveryPolicy.h"
#include "runtime/GuildOrderGiveUpPolicy.h"
#include "runtime/GuildOrderNeedPolicy.h"
#include "runtime/GuildOrderStoreColumns.h"
#include "runtime/GuildOrderWorkPolicy.h"

#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <list>
#include <mutex>
#include <set>
#include <unordered_map>
#include <unordered_set>

// The engine's room-making pass (ai/playerbot/strategy/actions/LootRollAction.cpp).
// Declared here, not included: that header pulls the bot-brain tree and the cmangos
// compat shim, whose function-like macros also rename host calls (GetLeaderGuid).
namespace ai
{
bool MakeRoomForGuildOrderLoot(PlayerbotAI* ai, Player* bot, uint32 itemId, uint32 count);
}

namespace tbhost
{
namespace guild_orders
{

namespace
{
using namespace TortoiseBots::GuildOrders;

constexpr char const* kOrderTable = "tortoise_bots_guild_order";

struct StoreState
{
    std::mutex mutex;
    bool loaded = false;
    bool available = false;
    bool warned = false;
    std::unordered_map<uint32_t, OrderRecord> orders;
    std::unordered_set<uint32_t> workingBots;
    std::unordered_map<uint32_t, tbhost::guild::OrderTickThrottleState> tickThrottle;
};

StoreState& State()
{
    static StoreState s;
    return s;
}

std::set<std::string> const& RequiredColumns()
{
    static std::set<std::string> const cols = []()
    {
        std::set<std::string> s;
        for (size_t i = 0; i < StoreColumns::kCount; ++i)
            s.insert(StoreColumns::kSelect[i]);
        s.insert("ah_gold_spent");
        return s;
    }();
    return cols;
}

bool EnsureSchema(StoreState& s)
{
    if (s.available)
        return true;
    if (!CharacterSchemaHasCompat(kOrderTable, RequiredColumns()))
    {
        if (!s.warned)
        {
            s.warned = true;
            TC_LOG_WARN("tortoisebots", "TortoiseBots: `{}` is missing or lacks columns; guild orders are disabled. Apply forever/sql/characters/2026_11_20_01_tb_guild_orders.sql.", kOrderTable);
        }
        return false;
    }
    s.available = true;
    return true;
}

QueryResult GuildOrderQueryCompat(std::string const& sql)
{
    std::string table;
    std::set<std::string> columns;
    if (!WorldQueryNamesCompat(sql, table, columns) || table != kOrderTable)
    {
        TC_LOG_ERROR("tortoisebots", "TortoiseBots: guild order query refused: {}", sql);
        return QueryResult();
    }
    if (!CharacterSchemaHasCompat(table, columns))
        return QueryResult();
    return CharacterQueryGuardedCompat(sql);
}

std::string EscapeSql(std::string value)
{
    CharacterDatabase.EscapeString(value);
    return value;
}

constexpr float kW2MaxVisitDistYd = 300.0f;

Player* FindOnlineBot(uint32_t botGuidLow)
{
    if (!botGuidLow)
        return nullptr;
    Player* bot = ObjectAccessor::FindPlayer(ObjectGuid::Create<HighGuid::Player>(botGuidLow));
    if (!bot || !bot->IsInWorld() || !bot->FindMap())
        return nullptr;
    return bot;
}

GameObject* FindNearestGameObjectByType(Player* bot, GameobjectTypes type, float maxDistYd)
{
    if (!bot || !bot->IsInWorld() || !bot->FindMap())
        return nullptr;
    std::list<GameObject*> objects;
    Trinity::GameObjectInRangeCheck check(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), maxDistYd, 0);
    Trinity::GameObjectListSearcher<Trinity::GameObjectInRangeCheck> searcher(bot, objects, check);
    Cell::VisitGridObjects(bot, searcher, maxDistYd);

    GameObject* best = nullptr;
    float bestDist = maxDistYd + 1.0f;
    uint32 const botZone = bot->GetZoneId();
    for (GameObject* go : objects)
    {
        if (!go || !go->isSpawned() || go->GetGoType() != type)
            continue;
        if (go->GetZoneId() != botZone)
            continue;
        float const d = bot->GetDistance(go);
        if (d <= maxDistYd && d < bestDist)
        {
            bestDist = d;
            best = go;
        }
    }
    return best;
}

Item* FindBagItemByEntry(Player* bot, uint32_t itemId)
{
    if (!bot || !itemId)
        return nullptr;
    Item* best = nullptr;
    auto checkItem = [&](Item* item)
    {
        if (!item || item->GetEntry() != itemId)
            return;
        if (!best || item->GetCount() < best->GetCount())
            best = item;
    };
    for (uint8_t slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
        checkItem(bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot));
    for (uint8_t bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END; ++bag)
    {
        if (Bag* pBag = bot->GetBagByPos(bag))
        {
            for (uint32_t slot = 0; slot < pBag->GetBagSize(); ++slot)
                checkItem(pBag->GetItemByPos(slot));
        }
    }
    return best;
}

// Items the order can hand over: the backpack and the bags' contents, never the bags the bot wears (GetItemCount counts those too,
// so a Linen Bag order was "done" while every bag was equipped).
uint32_t BagItemCount(Player* bot, uint32_t itemId)
{
    if (!bot || !itemId)
        return 0;
    uint32_t total = 0;
    auto add = [&](Item* item)
    {
        if (item && item->GetEntry() == itemId)
            total += item->GetCount();
    };
    for (uint8_t slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
        add(bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot));
    for (uint8_t bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END; ++bag)
        if (Bag* pBag = bot->GetBagByPos(bag))
            for (uint32_t slot = 0; slot < pBag->GetBagSize(); ++slot)
                add(pBag->GetItemByPos(slot));
    return total;
}

// One more of `itemId` fits: a free slot, or a stack of it that can grow.
// CanStoreNewItem also refuses on unique-count and other eligibility rules,
// which are not "bags full".
bool HasBagRoomForItem(Player* bot, uint32_t itemId)
{
    if (!bot || !itemId)
        return false;
    auto hasRoom = [&](Item* item) -> bool
    {
        return !item || (item->GetEntry() == itemId && item->GetCount() < item->GetMaxStackCount());
    };
    for (uint8_t slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
        if (hasRoom(bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot)))
            return true;
    for (uint8_t bagSlot = INVENTORY_SLOT_BAG_START; bagSlot < INVENTORY_SLOT_BAG_END; ++bagSlot)
        if (Bag* bag = bot->GetBagByPos(bagSlot))
            for (uint32_t slot = 0; slot < bag->GetBagSize(); ++slot)
                if (hasRoom(bag->GetItemByPos(slot)))
                    return true;
    return false;
}

class HostGatherCraftProvider final : public IGatherCraftProvider
{
    struct PendingStep
    {
        uint32_t itemId = 0;
        uint32_t beforeCount = 0;
        bool isCraft = false;
    };
    std::unordered_map<uint32_t, PendingStep> _pending;
    std::unordered_map<uint32_t, uint32_t> _pendingAhMail;

    static bool HasMailWithItem(Player* bot, uint32_t itemId)
    {
        if (!bot || !itemId)
            return false;
        for (tbhost::town::MailView const& mv : tbhost::town::GetMails(bot))
        {
            if (mv.state == 2)
                continue;
            for (auto const& [itemGuid, itemTemplate] : mv.items)
            {
                if (itemTemplate == itemId)
                    return true;
            }
        }
        return false;
    }

    bool TryCollectPendingAhMail(Player* bot, uint32_t botGuidLow, uint32_t mailItemId, std::string& outReason, bool& outCollected)
    {
        outCollected = false;
        if (!bot || !mailItemId)
        {
            _pendingAhMail.erase(botGuidLow);
            return false;
        }
        time_t const now = std::time(nullptr);
        bool anyMatchingMail = false;
        bool anyDeliverable = false;
        for (tbhost::town::MailView const& mv : tbhost::town::GetMails(bot))
        {
            if (mv.state == 2)
                continue;
            for (auto const& [itemGuid, itemTemplate] : mv.items)
            {
                if (itemTemplate == mailItemId)
                {
                    anyMatchingMail = true;
                    if (now >= mv.deliverTime)
                        anyDeliverable = true;
                }
            }
        }
        if (!anyMatchingMail)
        {
            _pendingAhMail.erase(botGuidLow);
            return false;
        }
        if (!anyDeliverable)
        {
            outReason = "walking";
            return true;
        }

        GameObject* mailbox = tbhost::town::FindNearestMailbox(bot);
        if (!mailbox)
            mailbox = tbhost::town::FindNearestMailbox(bot, kW2MaxVisitDistYd);
        if (!mailbox)
        {
            _pendingAhMail.erase(botGuidLow);
            outReason = "cannot: travel";
            return true;
        }
        if (!bot->GetGameObjectIfCanInteractWith(mailbox->GetGUID(), GAMEOBJECT_TYPE_MAILBOX))
        {
            bot->GetMotionMaster()->MovePoint(0, mailbox->GetPositionX(), mailbox->GetPositionY(), mailbox->GetPositionZ());
            outReason = "walking";
            return true;
        }

        bool tookAny = false;
        for (tbhost::town::MailView const& mv : tbhost::town::GetMails(bot))
        {
            if (mv.state == 2 || now < mv.deliverTime)
                continue;
            for (auto const& [itemGuid, itemTemplate] : mv.items)
            {
                if (itemTemplate == mailItemId)
                {
                    if (tbhost::town::TakeMailItem(bot, mailbox->GetGUID(), mv.messageId, itemGuid))
                        tookAny = true;
                }
            }
        }
        _pendingAhMail.erase(botGuidLow);
        if (tookAny)
        {
            outCollected = true;
            outReason = "mail-collected";
            return true;
        }
        outReason = "mail-collect-failed";
        return true;
    }

public:
    void ClearPending(uint32_t botGuidLow)
    {
        _pending.erase(botGuidLow);
        _pendingAhMail.erase(botGuidLow);
    }

    bool CanGather(uint32_t botGuidLow, uint32_t itemId) const override
    {
        Player* bot = FindOnlineBot(botGuidLow);
        return bot && tbhost::prof::HasGatherableNodeInSight(bot, 150.0f, itemId);
    }

    bool CanCraft(uint32_t botGuidLow, uint32_t itemId) const override
    {
        Player* bot = FindOnlineBot(botGuidLow);
        return bot && tbhost::prof::FindCraftSpellForItem(bot, itemId) != 0;
    }

    bool StepGatherOrCraft(uint32_t botGuidLow, uint32_t itemId, uint32_t /*wantCount*/, std::string& outReason,
        bool hasGoldCap = false, uint32_t remainingCopper = 0) override
    {
        Player* bot = FindOnlineBot(botGuidLow);
        if (!bot)
        {
            ClearPending(botGuidLow);
            outReason = "not-in-world";
            return false;
        }

        if (tbhost::prof::HasActiveErrand(bot))
        {
            outReason = tbhost::prof::ActiveErrandPhase(bot);
            return false;
        }

        if (auto it = _pending.find(botGuidLow); it != _pending.end())
        {
            uint32_t const afterCount = BagItemCount(bot, it->second.itemId);
            tbhost::craft_order::PendingStepOutcome const outcome =
                tbhost::craft_order::EvaluatePendingStep(it->second.beforeCount, afterCount, it->second.isCraft);
            _pending.erase(it);
            outReason = outcome.reason;
            return outcome.ok;
        }

        if (auto mit = _pendingAhMail.find(botGuidLow); mit != _pendingAhMail.end())
        {
            bool collected = false;
            if (TryCollectPendingAhMail(bot, botGuidLow, mit->second, outReason, collected))
                return collected;
        }

        uint32_t const beforeCount = BagItemCount(bot, itemId);
        if (uint32_t const spellId = tbhost::prof::FindCraftSpellForItem(bot, itemId))
        {
            auto hasAllReagents = [&](uint32_t checkSpellId) -> bool
            {
                SpellInfo const* csi = sSpellMgr->GetSpellInfo(checkSpellId, DIFFICULTY_NONE);
                if (!csi)
                    return false;
                for (uint32_t i = 0; i < MAX_SPELL_REAGENTS; ++i)
                {
                    if (csi->Reagent[i] > 0 && csi->ReagentCount[i] > 0)
                    {
                        if (BagItemCount(bot, uint32_t(csi->Reagent[i])) < uint32_t(csi->ReagentCount[i]))
                            return false;
                    }
                }
                return true;
            };

            if (SpellInfo const* si = sSpellMgr->GetSpellInfo(spellId, DIFFICULTY_NONE))
            {
                std::vector<tbhost::craft_order::ReagentRequirement> reqs;
                tbhost::craft_order::CraftAhBudget const ahBudget =
                    tbhost::craft_order::ResolveCraftAhBudget(hasGoldCap, remainingCopper);
                for (uint32_t i = 0; i < MAX_SPELL_REAGENTS; ++i)
                {
                    if (si->Reagent[i] <= 0 || si->ReagentCount[i] <= 0)
                        continue;
                    uint32_t const rId = uint32_t(si->Reagent[i]);
                    uint32_t const need = uint32_t(si->ReagentCount[i]);
                    uint32_t const have = BagItemCount(bot, rId);
                    if (have < need && HasMailWithItem(bot, rId))
                    {
                        _pendingAhMail[botGuidLow] = rId;
                        bool collected = false;
                        if (TryCollectPendingAhMail(bot, botGuidLow, rId, outReason, collected))
                            return collected;
                    }
                    tbhost::craft_order::ReagentRequirement r;
                    r.itemId = rId;
                    r.needPerCraft = need;
                    r.inBags = have;
                    if (have < need)
                    {
                        uint32_t const subSpell = tbhost::prof::FindCraftSpellForItem(bot, rId);
                        r.canCraftSub = (subSpell != 0 && hasAllReagents(subSpell));
                        r.canGather = tbhost::prof::HasGatherableNodeInSight(bot, 150.0f, rId);
                        r.canBuyAh = ahBudget.canBuy && AuctionBuy().CanBuyOnAuction(botGuidLow, rId, ahBudget.maxCopper);
                    }
                    reqs.push_back(r);
                }

                tbhost::craft_order::CraftReagentDecision const rdec =
                    tbhost::craft_order::DecideCraftReagentStep(reqs);
                if (rdec.action == tbhost::craft_order::CraftReagentAction::CraftSub)
                {
                    uint32_t const subSpell = tbhost::prof::FindCraftSpellForItem(bot, rdec.reagentItemId);
                    std::string stateOut, detail;
                    if (subSpell && tbhost::prof::StartCraft(bot, subSpell, 1, 0, stateOut, detail))
                    {
                        _pending[botGuidLow] = { rdec.reagentItemId, BagItemCount(bot, rdec.reagentItemId), true };
                        outReason = "casting";
                        return false;
                    }
                    outReason = stateOut.empty() ? "craft-failed" : stateOut;
                    return false;
                }
                if (rdec.action == tbhost::craft_order::CraftReagentAction::Gather)
                {
                    std::string stateOut, detail;
                    if (tbhost::prof::StartGatherNearest(bot, 150.0f, 0, stateOut, detail, rdec.reagentItemId))
                    {
                        _pending[botGuidLow] = { rdec.reagentItemId, BagItemCount(bot, rdec.reagentItemId), false };
                        outReason = tbhost::prof::ActiveErrandPhase(bot);
                        return false;
                    }
                    outReason = stateOut.empty() ? "gather-failed" : stateOut;
                    return false;
                }
                if (rdec.action == tbhost::craft_order::CraftReagentAction::BuyAh)
                {
                    if (AuctionBuy().StepBuyOnAuction(botGuidLow, rdec.reagentItemId, rdec.missingCount, ahBudget.maxCopper, outReason))
                    {
                        _pendingAhMail[botGuidLow] = rdec.reagentItemId;
                        outReason = "walking";
                    }
                    return false;
                }
                if (rdec.action == tbhost::craft_order::CraftReagentAction::MissingReagents)
                {
                    outReason = "missing-reagents";
                    return false;
                }
            }

            std::string stateOut, detail;
            if (tbhost::prof::StartCraft(bot, spellId, 1, 0, stateOut, detail))
            {
                _pending[botGuidLow] = { itemId, beforeCount, true };
                outReason = "casting";
                return false;
            }
            outReason = stateOut.empty() ? "craft-failed" : stateOut;
            return false;
        }

        std::string stateOut, detail;
        if (tbhost::prof::StartGatherNearest(bot, 150.0f, 0, stateOut, detail, itemId))
        {
            _pending[botGuidLow] = { itemId, beforeCount, false };
            outReason = tbhost::prof::ActiveErrandPhase(bot);
            return false;
        }

        outReason = "cannot: travel";
        return false;
    }
};

class HostAuctionBuyProvider final : public IAuctionBuyProvider
{
public:
    bool CanBuyOnAuction(uint32_t botGuidLow, uint32_t itemId, uint32_t maxCopper) const override
    {
        Player* bot = FindOnlineBot(botGuidLow);
        if (!bot)
            return false;
        Creature* auctioneer = tbhost::ah::FindNearestFactionAuctioneer(bot, kW2MaxVisitDistYd);
        if (!auctioneer)
            return false;
        uint64_t const budget = (maxCopper > 0 && maxCopper < bot->GetMoney()) ? uint64_t(maxCopper) : bot->GetMoney();
        if (budget == 0)
            return false;
        auto const listings = tbhost::ah::QueryMarketByItemId(bot, auctioneer, itemId);
        for (auto const& l : listings)
        {
            if (l.isOwn || l.unitPrice == 0)
                continue;
            ai::AhBuyInput in;
            in.auctionId = l.auctionId;
            in.itemId = l.itemId;
            in.isCommodity = l.isCommodity;
            in.isOwnAuction = l.isOwn;
            in.buyoutOrUnitPrice = l.isCommodity ? l.unitPrice : l.buyoutPrice;
            in.isNeededMatOrExplicit = true;
            in.budgetCopper = budget;
            in.count = l.isCommodity ? ai::AhClampCommodityBuyQty(1, l.count, l.unitPrice, budget) : (l.count ? l.count : 1);
            if (ai::DecideAhBuy(in).shouldBuy)
                return true;
        }
        return false;
    }

    bool StepBuyOnAuction(uint32_t botGuidLow, uint32_t itemId, uint32_t wantCount, uint32_t maxCopper, std::string& outReason) override
    {
        Player* bot = FindOnlineBot(botGuidLow);
        if (!bot)
        {
            outReason = "not-in-world";
            return false;
        }
        Creature* auctioneer = tbhost::ah::FindNearestFactionAuctioneer(bot, kW2MaxVisitDistYd);
        if (!auctioneer)
        {
            outReason = "cannot: travel";
            return false;
        }
        if (!tbhost::ah::NpcIfCanInteract(bot, auctioneer->GetGUID()))
        {
            bot->GetMotionMaster()->MovePoint(0, auctioneer->GetPositionX(), auctioneer->GetPositionY(), auctioneer->GetPositionZ());
            outReason = "walking";
            return false;
        }

        auto listings = tbhost::ah::QueryMarketByItemId(bot, auctioneer, itemId);
        if (listings.empty())
        {
            outReason = "no-listing";
            return false;
        }
        std::sort(listings.begin(), listings.end(), [](tbhost::ah::AhListingView const& a, tbhost::ah::AhListingView const& b)
        {
            return a.unitPrice < b.unitPrice;
        });

        uint64_t const budget = (maxCopper > 0 && maxCopper < bot->GetMoney()) ? uint64_t(maxCopper) : bot->GetMoney();
        for (auto const& l : listings)
        {
            if (l.isOwn || l.unitPrice == 0)
                continue;
            ai::AhBuyInput in;
            in.auctionId = l.auctionId;
            in.itemId = l.itemId;
            in.isCommodity = l.isCommodity;
            in.isOwnAuction = l.isOwn;
            in.buyoutOrUnitPrice = l.isCommodity ? l.unitPrice : l.buyoutPrice;
            in.isNeededMatOrExplicit = true;
            in.budgetCopper = budget;
            in.count = l.isCommodity
                ? ai::AhClampCommodityBuyQty(std::max<uint32_t>(1, wantCount), l.count, l.unitPrice, budget)
                : (l.count ? l.count : 1);

            ai::AhBuyDecision const decision = ai::DecideAhBuy(in);
            if (!decision.shouldBuy)
                continue;

            std::string failReason;
            bool const ok = decision.isCommodity
                ? tbhost::ah::BuyCommodity(bot, auctioneer, l.itemId, decision.buyQuantity, &failReason)
                : tbhost::ah::BuyItem(bot, auctioneer, l.auctionId, decision.totalPrice, &failReason);
            if (ok)
            {
                outReason = "bought";
                return true;
            }
        }

        outReason = "no-affordable-listing";
        return false;
    }
};

class HostDeliveryProvider final : public IDeliveryProvider
{
    static uint32_t ResolveMailTarget(Player const* bot, uint64_t guildId, uint32_t giverGuidLow, std::string& outReason)
    {
        uint32_t const botGuidLow = bot->GetGUID().GetCounter();
        uint32_t const leaderLow = tbhost::guild::GetLeaderGuid(guildId).GetCounter();
        uint32_t const probeGuidLow = (giverGuidLow != 0 && giverGuidLow != botGuidLow) ? giverGuidLow : leaderLow;
        bool const targetIsBot = probeGuidLow != 0 &&
            TortoiseBots::BotMgr::Instance().IsBot(ObjectGuid::Create<HighGuid::Player>(probeGuidLow));
        tbhost::guild::MailRecipientDecision const dec =
            tbhost::guild::DecideMailRecipient(botGuidLow, giverGuidLow, leaderLow, targetIsBot, guildId);
        if (!dec.allowed)
        {
            outReason = dec.reason;
            return 0;
        }
        return dec.targetGuidLow;
    }

    static Item* SplitDeliveryStack(Player* bot, uint32_t itemId, Item* item, uint32_t missingCount, std::string& outReason)
    {
        tbhost::guild::DeliveryStackPlan const plan = tbhost::guild::PlanDeliveryStack(item->GetCount(), missingCount);
        if (plan.deliverCount == 0)
        {
            outReason = "done";
            return nullptr;
        }
        if (!plan.needSplit)
            return item;

        uint16 const srcPos = (uint16(item->GetBagSlot()) << 8) | item->GetSlot();
        auto trySlot = [&](uint8_t dstBag, uint8_t dstSlot) -> Item*
        {
            if (bot->GetItemByPos(dstBag, dstSlot))
                return nullptr;
            uint16 const dstPos = (uint16(dstBag) << 8) | dstSlot;
            bot->SplitItem(srcPos, dstPos, plan.splitCount);
            Item* split = bot->GetItemByPos(dstBag, dstSlot);
            if (split && split->GetEntry() == itemId && split->GetCount() == plan.splitCount)
                return split;
            return nullptr;
        };

        for (uint8_t slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
        {
            if (Item* split = trySlot(INVENTORY_SLOT_BAG_0, slot))
                return split;
        }
        for (uint8_t bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END; ++bag)
        {
            if (Bag* pBag = bot->GetBagByPos(bag))
            {
                for (uint32_t slot = 0; slot < pBag->GetBagSize(); ++slot)
                {
                    if (Item* split = trySlot(bag, static_cast<uint8_t>(slot)))
                        return split;
                }
            }
        }
        outReason = "bags-full";
        return nullptr;
    }

public:
    bool CanDeliverToBank(uint32_t botGuidLow, uint64_t guildId) const override
    {
        Player* bot = FindOnlineBot(botGuidLow);
        if (!bot || !guildId || tbhost::guild::GetGuildId(bot) != guildId)
            return false;
        return FindNearestGameObjectByType(bot, GAMEOBJECT_TYPE_GUILD_BANK, kW2MaxVisitDistYd) != nullptr;
    }

    bool CanDeliverByMail(uint32_t botGuidLow, uint32_t recipientGuidLow) const override
    {
        Player* bot = FindOnlineBot(botGuidLow);
        if (!bot)
            return false;
        uint64_t const guildId = tbhost::guild::GetGuildId(bot);
        std::string ignoreReason;
        if (!ResolveMailTarget(bot, guildId, recipientGuidLow, ignoreReason))
            return false;
        return tbhost::town::FindNearestMailbox(bot, kW2MaxVisitDistYd) != nullptr;
    }

    bool Deliver(uint32_t botGuidLow, uint64_t guildId, uint32_t recipientGuidLow, uint32_t itemId, uint32_t count, std::string& outReason) override
    {
        Player* bot = FindOnlineBot(botGuidLow);
        if (!bot)
        {
            outReason = "not-in-world";
            return false;
        }
        if (count == 0)
        {
            outReason = "done";
            return false;
        }
        Item* item = FindBagItemByEntry(bot, itemId);
        if (!item)
        {
            outReason = "no-items";
            return false;
        }
        Item* deliveryItem = SplitDeliveryStack(bot, itemId, item, count, outReason);
        if (!deliveryItem)
            return false;

        if (guildId != 0 && tbhost::guild::GetGuildId(bot) == guildId)
        if (GameObject* banker = FindNearestGameObjectByType(bot, GAMEOBJECT_TYPE_GUILD_BANK, kW2MaxVisitDistYd))
        {
            // Walk to the vault only when no mailbox can take the delivery where the bot stands: a guild without bank tabs (or a
            // vault the bot cannot reach) otherwise sent it walking to the bank every tick and the order never mailed.
            bool const mailHere = tbhost::town::FindNearestMailbox(bot) != nullptr;
            if (!bot->GetGameObjectIfCanInteractWith(banker->GetGUID(), GAMEOBJECT_TYPE_GUILD_BANK) && !mailHere)
            {
                bot->GetMotionMaster()->MovePoint(0, banker->GetPositionX(), banker->GetPositionY(), banker->GetPositionZ());
                outReason = "walking";
                return false;
            }
            for (uint8_t tab = 0; tab < kDeliveryTabCount && bot->GetGameObjectIfCanInteractWith(banker->GetGUID(), GAMEOBJECT_TYPE_GUILD_BANK); ++tab)
            {
                std::string failReason;
                if (tbhost::guild::DepositItem(bot, banker->GetGUID(), tab, deliveryItem, &failReason))
                {
                    outReason = "bank";
                    return true;
                }
            }
        }

        uint32_t const targetLow = ResolveMailTarget(bot, guildId, recipientGuidLow, outReason);
        if (!targetLow)
            return false;

        // A mailbox the core accepts right here wins; the far search can name one the bot never reaches (two spawns on one spot,
        // a box behind geometry), and walking to it every tick kept the bot from mailing at the box it stood at.
        GameObject* mailbox = tbhost::town::FindNearestMailbox(bot);
        if (!mailbox)
            mailbox = tbhost::town::FindNearestMailbox(bot, kW2MaxVisitDistYd);
        if (!mailbox)
        {
            outReason = "cannot: travel";
            return false;
        }
        if (!bot->GetGameObjectIfCanInteractWith(mailbox->GetGUID(), GAMEOBJECT_TYPE_MAILBOX))
        {
            bot->GetMotionMaster()->MovePoint(0, mailbox->GetPositionX(), mailbox->GetPositionY(), mailbox->GetPositionZ());
            outReason = "walking";
            return false;
        }

        std::string targetName;
        if (!sCharacterCache->GetCharacterNameByGuid(ObjectGuid::Create<HighGuid::Player>(targetLow), targetName))
        {
            outReason = "no-recipient";
            return false;
        }

        uint32_t const beforeCount = BagItemCount(bot, itemId);
        std::vector<ObjectGuid> attachments;
        attachments.push_back(deliveryItem->GetGUID());
        std::string failReason;
        if (!tbhost::guild::SendMail(bot, mailbox->GetGUID(), targetName, "Guild Order", "", 0, attachments, &failReason))
        {
            outReason = failReason.empty() ? "mail-failed" : failReason;
            return false;
        }
        if (BagItemCount(bot, itemId) >= beforeCount)
        {
            outReason = "mail-refused";
            return false;
        }
        outReason = "mailed";
        return true;
    }
};

HostGatherCraftProvider g_hostGatherCraft;
HostAuctionBuyProvider g_hostAuctionBuy;
HostDeliveryProvider g_hostDelivery;

IGatherCraftProvider* g_gatherCraft = &g_hostGatherCraft;
IAuctionBuyProvider* g_auctionBuy = &g_hostAuctionBuy;
IDeliveryProvider* g_delivery = &g_hostDelivery;

} // namespace

uint32_t BotBagItemCount(Player* bot, uint32_t itemId)
{
    return BagItemCount(bot, itemId);
}

void LoadOrders()
{
    tbhost::guild_order_fish::LoadFishingSources();

    StoreState& s = State();
    std::lock_guard<std::mutex> lock(s.mutex);
    s.orders.clear();
    TortoiseBots::h3::guild_read::ForgetAllOrderHistory();
    s.loaded = true;
    s.available = false;

    if (!EnsureSchema(s))
        return;

    std::string const sql = "SELECT " + StoreColumns::SelectList() + ", `ah_gold_spent` FROM `tortoise_bots_guild_order`";
    QueryResult result = GuildOrderQueryCompat(sql);
    if (!result)
        return;

    do
    {
        Field* fields = result->Fetch();
        OrderRecord record;
        record.botGuidLow = fields[StoreColumns::ColBotGuid].GetUInt32();
        record.guildId = fields[StoreColumns::ColGuildId].GetUInt32();
        record.giverGuidLow = fields[StoreColumns::ColGiverGuid].GetUInt32();
        record.parentBotGuidLow = fields[StoreColumns::ColParentBot].GetUInt32();
        record.rootGiverGuidLow = fields[StoreColumns::ColRootGiver].GetUInt32();
        ParseType(fields[StoreColumns::ColType].GetString(), record.type);
        record.itemId = fields[StoreColumns::ColItemId].GetUInt32();
        record.amount = fields[StoreColumns::ColAmount].GetUInt32();
        record.hours = fields[StoreColumns::ColHours].GetUInt32();
        record.delivered = fields[StoreColumns::ColDelivered].GetUInt32();
        ParseState(fields[StoreColumns::ColState].GetString(), record.state);
        ParseReason(fields[StoreColumns::ColReason].GetString(), record.reason);
        record.farmZoneId = fields[StoreColumns::ColFarmZone].GetUInt32();
        record.farmChance = static_cast<uint8_t>(fields[StoreColumns::ColFarmChance].GetUInt32());
        record.farmItemId = fields[StoreColumns::ColFarmItem].GetUInt32();
        record.zoneIndex = fields[StoreColumns::ColZoneIndex].GetUInt32();
        record.deathsInZone = fields[StoreColumns::ColDeathsInZone].GetUInt32();
        record.workedSinceNewItemSec = fields[StoreColumns::ColWorkedSec].GetUInt32();
        record.workedTotalSec = fields[StoreColumns::ColWorkedTotal].GetUInt32();
        record.workedNoGainSec = fields[StoreColumns::ColWorkedNoGain].GetUInt32();
        record.reportedHave = fields[StoreColumns::ColReported].GetUInt32();
        record.farmRest = fields[StoreColumns::ColFarmRest].GetUInt32() != 0;
        record.keep = fields[StoreColumns::ColKeep].GetUInt32() != 0;
        record.goldCap = fields[StoreColumns::ColGoldCap].GetUInt32();
        record.hasGoldCap = fields[StoreColumns::ColGoldCapSet].GetUInt32() != 0;
        record.guildGoldWithdrawn = fields[StoreColumns::ColGuildGoldWithdrawn].GetUInt32();
        record.guildGoldHeld = fields[StoreColumns::ColGuildGoldHeld].GetUInt32();
        record.cancelled = fields[StoreColumns::ColCancelled].GetUInt32() != 0;
        ParseSupplyStep(fields[StoreColumns::ColSupplyStep].GetString(), record.supplyStep);
        ParseSupplyStep(fields[StoreColumns::ColSupplyReturnStep].GetString(), record.supplyReturnStep);
        record.supplyItemId = fields[StoreColumns::ColSupplyItem].GetUInt32();
        record.supplyCount = fields[StoreColumns::ColSupplyCount].GetUInt32();
        record.supplyGoldCount = fields[StoreColumns::ColSupplyGoldCount].GetUInt32();
        record.needItems = ParseNeedItems(fields[StoreColumns::ColNeedItems].GetString());
        record.needWhispered = fields[StoreColumns::ColNeedWhispered].GetUInt32() != 0;
        record.lastNeedWhisper = fields[StoreColumns::ColLastNeedWhisper].GetString();
        record.needBankDenied = fields[StoreColumns::ColNeedBankDenied].GetUInt32() != 0;
        record.stockTarget = fields[StoreColumns::ColStockTarget].GetUInt32();
        record.lastBankCheckMs = fields[StoreColumns::ColLastBankCheck].GetUInt32();
        record.createdAt = fields[StoreColumns::ColCreatedAt].GetUInt64();
        record.updatedAt = fields[StoreColumns::ColUpdatedAt].GetUInt64();
        record.reportedCarry = fields[StoreColumns::ColCarried].GetUInt32();
        record.bringItems = ParseBringItemsToken(fields[StoreColumns::ColBring].GetString());
        record.given = ParseGivenItems(fields[StoreColumns::ColGiven].GetString());
        record.ahGoldSpent = fields[StoreColumns::kCount].GetUInt32();

        if (record.botGuidLow && record.itemId && record.amount)
            s.orders[record.botGuidLow] = std::move(record);
    } while (result->NextRow());

    TC_LOG_INFO("tortoisebots", "[W9] loaded {} guild orders", uint32_t(s.orders.size()));
}

bool IsOrderStoreAvailable()
{
    StoreState& s = State();
    std::lock_guard<std::mutex> lock(s.mutex);
    if (!s.loaded)
        return EnsureSchema(s);
    return s.available;
}

std::optional<OrderRecord> FindOrder(uint32_t botGuidLow)
{
    StoreState& s = State();
    std::lock_guard<std::mutex> lock(s.mutex);
    auto const it = s.orders.find(botGuidLow);
    if (it == s.orders.end())
        return std::nullopt;
    return it->second;
}

std::vector<OrderRecord> ListOrdersForGuild(uint32_t guildId)
{
    StoreState& s = State();
    std::lock_guard<std::mutex> lock(s.mutex);
    std::vector<OrderRecord> out;
    for (auto const& [_, rec] : s.orders)
    {
        if (rec.guildId == guildId && !rec.cancelled)
            out.push_back(rec);
    }
    return out;
}

bool GiveOrder(OrderRecord record)
{
    if (!record.botGuidLow || !record.itemId || !record.amount)
        return false;

    StoreState& s = State();
    std::lock_guard<std::mutex> lock(s.mutex);
    if (!EnsureSchema(s))
        return false;

    uint64_t const now = uint64_t(std::time(nullptr));
    if (!record.createdAt)
        record.createdAt = now;
    record.updatedAt = now;
    // Unspent guild gold outlives the order: the row parks as a refund stub
    // instead of disappearing, and the refund pass pays it from any banker
    // visit, then deletes the stub.
    if (IsFinished(record.state) && record.guildGoldHeld > 0)
        record.cancelled = true;

    std::string const sql = Trinity::StringFormat(
        "REPLACE INTO `tortoise_bots_guild_order` ({}, `ah_gold_spent`) VALUES "
        "({}, {}, {}, {}, {}, '{}', {}, {}, {}, {}, '{}', '{}', {}, {}, {}, {}, {}, "
        "{}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, '{}', '{}', {}, {}, {}, '{}', {}, '{}', {}, {}, {}, "
        "{}, {}, {}, '{}', '{}', {})",
        StoreColumns::SelectList(),
        record.botGuidLow, record.guildId, record.giverGuidLow, record.parentBotGuidLow, record.rootGiverGuidLow,
        TypeToken(record.type), record.itemId, record.amount, record.hours, record.delivered,
        StateToken(record.state), ReasonToken(record.reason),
        record.farmZoneId, uint32_t(record.farmChance), record.farmItemId, record.zoneIndex, record.deathsInZone,
        record.workedSinceNewItemSec, record.workedTotalSec, record.workedNoGainSec, record.reportedHave,
        record.farmRest ? 1u : 0u, record.keep ? 1u : 0u, record.goldCap, record.hasGoldCap ? 1u : 0u,
        record.guildGoldWithdrawn, record.guildGoldHeld, record.cancelled ? 1u : 0u,
        SupplyStepToken(record.supplyStep), SupplyStepToken(record.supplyReturnStep),
        record.supplyItemId, record.supplyCount, record.supplyGoldCount,
        EscapeSql(NeedItemsToken(record.needItems)), record.needWhispered ? 1u : 0u,
        EscapeSql(record.lastNeedWhisper), record.needBankDenied ? 1u : 0u,
        record.stockTarget, record.lastBankCheckMs,
        record.createdAt, record.updatedAt,
        record.reportedCarry, EscapeSql(BringItemsToken(record.bringItems)), EscapeSql(GivenItemsToken(record.given)),
        record.ahGoldSpent);

    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
    trans->Append(sql.c_str());
    CharacterDatabase.CommitTransaction(trans);

    bool fresh = true;
    if (auto const existing = s.orders.find(record.botGuidLow); existing != s.orders.end())
    {
        fresh = existing->second.createdAt != record.createdAt ||
            existing->second.itemId != record.itemId || existing->second.type != record.type;
    }
    TortoiseBots::h3::guild_read::NoteOrderHistory(record, fresh);

    s.orders[record.botGuidLow] = std::move(record);
    return true;
}

bool CancelOrder(uint32_t botGuidLow)
{
    if (!botGuidLow)
        return false;

    StoreState& s = State();
    std::lock_guard<std::mutex> lock(s.mutex);
    if (!EnsureSchema(s))
        return false;

    auto const it = s.orders.find(botGuidLow);
    if (it == s.orders.end())
        return false;

    std::string const sql = Trinity::StringFormat(
        "DELETE FROM `tortoise_bots_guild_order` WHERE `bot_guid` = {}", botGuidLow);
    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
    trans->Append(sql.c_str());
    CharacterDatabase.CommitTransaction(trans);

    s.orders.erase(it);
    TortoiseBots::h3::guild_read::ForgetOrderHistory(botGuidLow);
    return true;
}

IGatherCraftProvider& GatherCraft() { return *g_gatherCraft; }
IAuctionBuyProvider& AuctionBuy() { return *g_auctionBuy; }
IDeliveryProvider& Delivery() { return *g_delivery; }

void SetGatherCraftProvider(IGatherCraftProvider* provider) { g_gatherCraft = provider ? provider : &g_hostGatherCraft; }
void SetAuctionBuyProvider(IAuctionBuyProvider* provider) { g_auctionBuy = provider ? provider : &g_hostAuctionBuy; }
void SetDeliveryProvider(IDeliveryProvider* provider) { g_delivery = provider ? provider : &g_hostDelivery; }

void SetOrderWorking(uint32_t botGuidLow, bool working)
{
    if (!botGuidLow)
        return;
    StoreState& s = State();
    std::lock_guard<std::mutex> lock(s.mutex);
    if (working)
    {
        s.workingBots.insert(botGuidLow);
        s.tickThrottle.erase(botGuidLow);
    }
    else
        s.workingBots.erase(botGuidLow);
}

void OnBotLogout(uint32_t botGuidLow)
{
    if (!botGuidLow)
        return;
    DisenchantForgetBot(botGuidLow);
    ForgetGuildOrderQueueSession(botGuidLow);
    g_hostGatherCraft.ClearPending(botGuidLow);
    StoreState& s = State();
    std::lock_guard<std::mutex> lock(s.mutex);
    s.workingBots.erase(botGuidLow);
    s.tickThrottle.erase(botGuidLow);
}

bool IsOrderWorking(uint32_t botGuidLow)
{
    StoreState& s = State();
    std::lock_guard<std::mutex> lock(s.mutex);
    return s.workingBots.count(botGuidLow) > 0;
}

bool IsItemNeededByGuildOrder(uint64_t guildId, uint32_t itemId)
{
    if (!guildId || !itemId)
        return false;
    auto const orders = ListOrdersForGuild(uint32_t(guildId));
    for (auto const& o : orders)
    {
        if (o.cancelled || o.state == OrderState::Done || o.state == OrderState::Mailed)
            continue;
        if (o.itemId == itemId)
            return true;
        if (o.type == OrderType::Craft && tbhost::prof::IsReagentForItem(o.itemId, itemId))
            return true;
        for (auto const& ni : o.needItems)
        {
            if (ni.itemId == itemId)
                return true;
        }
    }
    return false;
}

void TickBotOrder(Player* bot)
{
    if (!bot || !bot->IsInWorld() || !bot->FindMap())
        return;
    uint32_t const botGuidLow = bot->GetGUID().GetCounter();
    if (!IsOrderWorking(botGuidLow))
        return;

    uint64_t const nowMs = GameTime::GetGameTimeMS();
    {
        StoreState& s = State();
        std::lock_guard<std::mutex> lock(s.mutex);
        auto const it = s.tickThrottle.find(botGuidLow);
        if (it != s.tickThrottle.end() && !it->second.ShouldRun(nowMs))
            return;
    }

    auto ordOpt = FindOrder(botGuidLow);
    if (!ordOpt || ordOpt->cancelled || ordOpt->state == OrderState::Done || ordOpt->state == OrderState::Mailed)
    {
        SetOrderWorking(botGuidLow, false);
        return;
    }

    OrderRecord rec = *ordOpt;
    uint32_t const inBagsBefore = BagItemCount(bot, rec.itemId);
    tbhost::guild::OrderGoldBudget const budget =
        tbhost::guild::RemainingOrderGoldCap(rec.hasGoldCap, rec.goldCap, rec.ahGoldSpent);

    OrderStepInput in;
    in.type = rec.type;
    in.delivered = rec.delivered;
    in.want = rec.amount;
    in.inBags = inBagsBefore;
    in.canCraft = GatherCraft().CanCraft(botGuidLow, rec.itemId);
    in.canGather = GatherCraft().CanGather(botGuidLow, rec.itemId);
    in.canBuyAh = (budget.capped && budget.remainingCopper == 0)
        ? false
        : AuctionBuy().CanBuyOnAuction(botGuidLow, rec.itemId, budget.capped ? budget.remainingCopper : 0);
    in.canDeliverBank = Delivery().CanDeliverToBank(botGuidLow, rec.guildId);
    in.canDeliverMail = Delivery().CanDeliverByMail(botGuidLow, rec.giverGuidLow);
    in.bagsFull = !HasBagRoomForItem(bot, rec.itemId);

    OrderStepDecision const dec = DecideOrderStep(in);
    std::string reason;
    auto recordTick = [&](bool succeeded, bool inProgress, std::string const& failReason) -> bool
    {
        StoreState& s = State();
        std::lock_guard<std::mutex> lock(s.mutex);
        return s.tickThrottle[botGuidLow].RecordResult(nowMs, succeeded, inProgress, failReason);
    };

    switch (dec.action)
    {
        case OrderStepAction::Complete:
            rec.state = OrderState::Done;
            rec.reason = OrderReason::None;
            GiveOrder(rec);
            SetOrderWorking(botGuidLow, false);
            TC_LOG_INFO("tortoisebots", "[W9] order id={} bot={} item={} have={} want={} state=done",
                rec.itemId, bot->GetName(), rec.itemId, rec.delivered, rec.amount);
            return;

        case OrderStepAction::DeliverBank:
        case OrderStepAction::DeliverMail:
        {
            rec.state = OrderState::Delivering;
            if (std::string(dec.reason) == "bags-full")
                rec.reason = OrderReason::BagsFull;
            uint32_t const missing = (rec.amount > rec.delivered) ? (rec.amount - rec.delivered) : 0u;
            bool const ok = Delivery().Deliver(botGuidLow, rec.guildId, rec.giverGuidLow, rec.itemId, missing, reason);
            if (!ok)
            {
                if (reason == "cannot: travel" || reason == "real-player-mail-refused")
                {
                    rec.state = OrderState::Cant;
                    rec.reason = (reason == "real-player-mail-refused") ? OrderReason::BankDenied : OrderReason::NoBanker;
                    GiveOrder(rec);
                    SetOrderWorking(botGuidLow, false);
                    TC_LOG_INFO("tortoisebots", "[W9] refuse id={} bot={} item={} have={} want={} reason={}",
                        rec.itemId, bot->GetName(), rec.itemId, dec.have, rec.amount, reason);
                }
                else
                {
                    bool const inProgress = (reason == "walking");
                    if (recordTick(false, inProgress, reason))
                    {
                        TC_LOG_INFO("tortoisebots", "[W9] deliver id={} bot={} item={} have={} want={} mode=none result={}",
                            rec.itemId, bot->GetName(), rec.itemId, dec.have, rec.amount, reason);
                    }
                    else if (!inProgress)
                    {
                        TC_LOG_DEBUG("tortoisebots", "[W9] deliver id={} bot={} item={} have={} want={} mode=none result={}",
                            rec.itemId, bot->GetName(), rec.itemId, dec.have, rec.amount, reason);
                    }
                }
                return;
            }
            recordTick(true, false, "");
            uint32_t const inBagsAfter = BagItemCount(bot, rec.itemId);
            uint32_t const moved = (inBagsBefore > inBagsAfter) ? (inBagsBefore - inBagsAfter) : 0;
            if (moved > 0)
            {
                rec.delivered += moved;
                rec.reportedHave = rec.delivered + inBagsAfter;
                if (rec.delivered >= rec.amount)
                {
                    rec.state = (reason == "mailed") ? OrderState::Mailed : OrderState::Done;
                    rec.reason = OrderReason::None;
                    SetOrderWorking(botGuidLow, false);
                }
                else if (DeliveryLostItsStock(rec.state, rec.reason, rec.delivered, rec.amount, inBagsAfter))
                {
                    // A bags-full delivery handed over what it carried: back to work.
                    rec.state = OrderState::Working;
                    rec.reason = OrderReason::None;
                }
                GiveOrder(rec);
            }
            TC_LOG_INFO("tortoisebots", "[W9] deliver id={} bot={} item={} have={} want={} mode={} result={}",
                rec.itemId, bot->GetName(), rec.itemId, rec.delivered, rec.amount, reason,
                (rec.delivered >= rec.amount) ? "done" : "ok");
            return;
        }

        case OrderStepAction::Craft:
        case OrderStepAction::Gather:
        {
            // A farm order with full bags and none of the item throws out grey
            // or white junk to make room; with nothing to throw out it ends now
            // instead of walking past sources it cannot loot.
            if (in.bagsFull && FullBagsWithoutStock(rec.type, inBagsBefore))
            {
                PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
                if (!ai::MakeRoomForGuildOrderLoot(ai, bot, rec.itemId, 1))
                {
                    rec.state = OrderState::Cant;
                    rec.reason = OrderReason::BagsFull;
                    GiveOrder(rec);
                    SetOrderWorking(botGuidLow, false);
                    TC_LOG_INFO("tortoisebots", "[W9] refuse id={} bot={} item={} have={} want={} reason=bags-full",
                        rec.itemId, bot->GetName(), rec.itemId, inBagsBefore, rec.amount);
                    return;
                }
                TC_LOG_INFO("tortoisebots", "[W9] room id={} bot={} item={} have={} want={}",
                    rec.itemId, bot->GetName(), rec.itemId, inBagsBefore, rec.amount);
                return;
            }
            uint64_t const moneyBefore = bot->GetMoney();
            bool const ok = GatherCraft().StepGatherOrCraft(botGuidLow, rec.itemId, dec.remaining, reason,
                budget.capped, budget.remainingCopper);
            uint64_t const moneyAfter = bot->GetMoney();
            if (moneyBefore > moneyAfter)
            {
                uint32_t const spent = static_cast<uint32_t>(std::min<uint64_t>(moneyBefore - moneyAfter, UINT32_MAX));
                rec.ahGoldSpent = (spent > UINT32_MAX - rec.ahGoldSpent) ? UINT32_MAX : (rec.ahGoldSpent + spent);
                GiveOrder(rec);
            }
            uint32_t const inBagsAfter = BagItemCount(bot, rec.itemId);
            uint32_t const haveAfter = rec.delivered + inBagsAfter;
            if (ok)
            {
                rec.reportedHave = haveAfter;
                GiveOrder(rec);
            }
            std::string const res = ok ? reason : (reason.empty() ? "fail" : reason);
            bool const inProgress = !ok && (res == "casting" || res == "walking" || res == "gathering");
            bool const logInfo = ok || recordTick(false, inProgress, res);
            if (ok)
                recordTick(true, false, "");
            if (logInfo)
            {
                TC_LOG_INFO("tortoisebots", "[W9] step id={} bot={} item={} have={} want={} action={} result={}",
                    rec.itemId, bot->GetName(), rec.itemId, haveAfter, rec.amount,
                    dec.action == OrderStepAction::Craft ? "craft" : "gather", res);
            }
            else
            {
                TC_LOG_DEBUG("tortoisebots", "[W9] step id={} bot={} item={} have={} want={} action={} result={}",
                    rec.itemId, bot->GetName(), rec.itemId, haveAfter, rec.amount,
                    dec.action == OrderStepAction::Craft ? "craft" : "gather", res);
            }
            return;
        }

        case OrderStepAction::BuyAh:
        {
            uint64_t const moneyBefore = bot->GetMoney();
            bool const ok = AuctionBuy().StepBuyOnAuction(
                botGuidLow, rec.itemId, dec.remaining, budget.capped ? budget.remainingCopper : 0, reason);
            uint64_t const moneyAfter = bot->GetMoney();
            if (ok)
            {
                uint32_t const spent = (moneyBefore > moneyAfter)
                    ? static_cast<uint32_t>(std::min<uint64_t>(moneyBefore - moneyAfter, UINT32_MAX))
                    : 0u;
                if (spent > 0)
                {
                    rec.ahGoldSpent = (spent > UINT32_MAX - rec.ahGoldSpent) ? UINT32_MAX : (rec.ahGoldSpent + spent);
                    GiveOrder(rec);
                }
            }
            uint32_t const inBagsAfter = BagItemCount(bot, rec.itemId);
            uint32_t const haveAfter = rec.delivered + inBagsAfter;
            std::string const res = ok ? reason : (reason.empty() ? "fail" : reason);
            bool const inProgress = !ok && (res == "walking");
            bool const logInfo = ok || recordTick(false, inProgress, res);
            if (ok)
                recordTick(true, false, "");
            if (logInfo)
            {
                TC_LOG_INFO("tortoisebots", "[W9] step id={} bot={} item={} have={} want={} action=buy result={}",
                    rec.itemId, bot->GetName(), rec.itemId, haveAfter, rec.amount, res);
            }
            else
            {
                TC_LOG_DEBUG("tortoisebots", "[W9] step id={} bot={} item={} have={} want={} action=buy result={}",
                    rec.itemId, bot->GetName(), rec.itemId, haveAfter, rec.amount, res);
            }
            return;
        }

        case OrderStepAction::CannotTravel:
        default:
        {
            // A farm order that can neither craft nor gather the item may be a
            // fishing order: the bot brain's fishing path is not built here, so
            // the store reports the verdict the fishing plan itself would give
            // instead of the generic no-source.
            if (rec.type == OrderType::Farm && (in.delivered + inBagsBefore) < rec.amount &&
                !in.canCraft && !in.canGather)
            {
                if (std::optional<OrderReason> const fishReason =
                        tbhost::guild_order_fish::ResolveFarmRefusal(bot, rec.itemId))
                {
                    rec.state = StateForReason(*fishReason);
                    rec.reason = *fishReason;
                    GiveOrder(rec);
                    SetOrderWorking(botGuidLow, false);
                    TC_LOG_INFO("tortoisebots", "[W9] refuse id={} bot={} item={} have={} want={} reason={} fishing=1",
                        rec.itemId, bot->GetName(), rec.itemId, dec.have, rec.amount, ReasonToken(*fishReason));
                    return;
                }
            }
            rec.state = OrderState::Cant;
            rec.reason = OrderReason::NoSource;
            GiveOrder(rec);
            SetOrderWorking(botGuidLow, false);
            TC_LOG_INFO("tortoisebots", "[W9] refuse id={} bot={} item={} have={} want={} reason=cannot:%20travel",
                rec.itemId, bot->GetName(), rec.itemId, dec.have, rec.amount);
            return;
        }
    }
}

} // namespace guild_orders
} // namespace tbhost

namespace TortoiseBots
{
namespace h3
{

bool HandleGuildOrder(ChatHandler* handler, Trinity::ChatCommands::Tail args)
{
    Words const words = Split(std::string_view(args));
    if (words.badToken)
        return Fail(handler, words, "guildorder", "bad-token", "invalid #token");
    if (!RequireBuilt(handler, words, "guildorder"))
        return false;
    if (words.w.empty())
        return Fail(handler, words, "guildorder", "usage", "use: create|list|cancel|status|work|search|who|preview|steps|access|rank|reassign");

    std::string const sub = Lower(words.w[0]);
    Player* sender = SenderPlayer(handler);

    if (guild_read::IsReadVerb(sub))
        return guild_read::Handle(handler, words, sender, sub);

    if (sub == "create")
    {
        if (words.w.size() < 4)
            return Fail(handler, words, "guildorder", "usage", "create <bot> <item_id> <amount> [farm|craft|buy]");
        Player* bot = FindOnline(words.w[1]);
        if (!bot || !IsHostBot(bot))
            return Fail(handler, words, "guildorder", "no-bot", words.w[1]);
        uint64_t const guildId = tbhost::guild::GetGuildId(bot);
        if (!guildId)
            return Fail(handler, words, "guildorder", "not-in-guild", bot->GetName());
        if (sender)
        {
            std::string code, detail;
            if (!tbhost::guild_order_access::MayGiveOrders(static_cast<uint32_t>(guildId), sender, code, detail))
                return Fail(handler, words, "guildorder", code, detail);
        }
        uint32_t const itemId = static_cast<uint32_t>(std::strtoul(words.w[2].c_str(), nullptr, 10));
        uint32_t const amount = static_cast<uint32_t>(std::strtoul(words.w[3].c_str(), nullptr, 10));
        if (!itemId || !tbcompat::ItemProto(itemId))
            return Fail(handler, words, "guildorder", "bad-item", words.w[2]);
        if (!amount)
            return Fail(handler, words, "guildorder", "bad-amount", words.w[3]);

        GuildOrders::OrderType orderType = GuildOrders::OrderType::Farm;
        if (words.w.size() >= 5)
        {
            std::string const t = Lower(words.w[4]);
            if (t == "craft")
                orderType = GuildOrders::OrderType::Craft;
            else if (t == "buy")
                orderType = GuildOrders::OrderType::Buy;
            else if (t != "farm")
                return Fail(handler, words, "guildorder", "bad-type", words.w[4]);
        }

        if (!tbhost::guild_orders::IsOrderStoreAvailable())
            return Fail(handler, words, "guildorder", "store-unavailable", "tortoise_bots_guild_order missing");

        GuildOrders::OrderRecord rec;
        rec.botGuidLow = bot->GetGUID().GetCounter();
        rec.guildId = static_cast<uint32_t>(guildId);
        rec.giverGuidLow = sender ? sender->GetGUID().GetCounter() : rec.botGuidLow;
        rec.rootGiverGuidLow = rec.giverGuidLow;
        rec.type = orderType;
        rec.itemId = itemId;
        rec.amount = amount;
        rec.hours = GuildOrders::kDefaultOrderHours;
        rec.state = GuildOrders::OrderState::Working;
        rec.reason = GuildOrders::OrderReason::None;
        if (!tbhost::guild_orders::GiveOrder(rec))
            return Fail(handler, words, "guildorder", "store-write-failed", bot->GetName());

        TC_LOG_INFO("tortoisebots", "[W9] order id={} bot={} item={} have=0 want={} type={}",
            itemId, bot->GetName(), itemId, amount, GuildOrders::TypeToken(orderType));

        Reply reply(handler, words, "guildorder");
        reply.Begin();
        Rec r("guildorder");
        r.S("op", "create").S("bot", bot->GetName()).N("guild", guildId).N("item", itemId).N("want", amount)
            .S("type", GuildOrders::TypeToken(orderType)).S("state", GuildOrders::StateToken(rec.state));
        reply.Line(r);
        reply.End(Rec("guildorder-end"));
        return true;
    }

    if (sub == "list")
    {
        uint32_t guildId = 0;
        if (words.w.size() >= 2)
        {
            if (Player* bot = FindOnline(words.w[1]))
                guildId = static_cast<uint32_t>(tbhost::guild::GetGuildId(bot));
            else
                guildId = static_cast<uint32_t>(std::strtoul(words.w[1].c_str(), nullptr, 10));
        }
        else if (sender)
            guildId = static_cast<uint32_t>(tbhost::guild::GetGuildId(sender));

        if (!guildId)
            return Fail(handler, words, "guildorder", "no-guild", "specify <bot|guild_id>");

        auto const orders = tbhost::guild_orders::ListOrdersForGuild(guildId);
        Reply reply(handler, words, "guildorder");
        reply.Begin();
        for (auto const& o : orders)
        {
            Rec r("guildorder");
            r.S("op", "list").N("botguid", o.botGuidLow).N("guild", o.guildId).N("item", o.itemId)
                .N("have", o.delivered).N("want", o.amount)
                .S("type", GuildOrders::TypeToken(o.type)).S("state", GuildOrders::StateToken(o.state));
            reply.Line(r);
        }
        Rec end("guildorder-end");
        end.N("guild", guildId);
        reply.End(end);
        return true;
    }

    if (sub == "cancel")
    {
        if (words.w.size() < 2)
            return Fail(handler, words, "guildorder", "usage", "cancel <bot>");
        Player* bot = FindOnline(words.w[1]);
        if (!bot || !IsHostBot(bot))
            return Fail(handler, words, "guildorder", "no-bot", words.w[1]);
        uint64_t const guildId = tbhost::guild::GetGuildId(bot);
        if (sender)
        {
            std::string code, detail;
            if (!tbhost::guild_order_access::MayGiveOrders(static_cast<uint32_t>(guildId), sender, code, detail))
                return Fail(handler, words, "guildorder", code, detail);
        }
        if (!tbhost::guild_orders::CancelOrder(bot->GetGUID().GetCounter()))
            return Fail(handler, words, "guildorder", "not-found", bot->GetName());
        tbhost::guild_orders::SetOrderWorking(bot->GetGUID().GetCounter(), false);

        Reply reply(handler, words, "guildorder");
        reply.Begin();
        Rec r("guildorder");
        r.S("op", "cancel").S("bot", bot->GetName()).S("state", "cancelled");
        reply.Line(r);
        reply.End(Rec("guildorder-end"));
        return true;
    }

    if (sub == "status")
    {
        if (words.w.size() < 2)
            return Fail(handler, words, "guildorder", "usage", "status <bot>");
        Player* bot = FindOnline(words.w[1]);
        if (!bot || !IsHostBot(bot))
            return Fail(handler, words, "guildorder", "no-bot", words.w[1]);
        auto const ord = tbhost::guild_orders::FindOrder(bot->GetGUID().GetCounter());
        Reply reply(handler, words, "guildorder");
        reply.Begin();
        Rec r("guildorder");
        r.S("op", "status").S("bot", bot->GetName());
        if (ord)
        {
            r.N("item", ord->itemId).N("have", ord->delivered).N("want", ord->amount)
                .S("type", GuildOrders::TypeToken(ord->type))
                .S("state", GuildOrders::StateToken(ord->state))
                .S("reason", GuildOrders::ReasonToken(ord->reason));
        }
        else
            r.S("state", "none");
        reply.Line(r);
        reply.End(Rec("guildorder-end"));
        return true;
    }

    if (sub == "work")
    {
        if (words.w.size() < 2)
            return Fail(handler, words, "guildorder", "usage", "work <bot> [id]");
        Player* bot = FindOnline(words.w[1]);
        if (!bot || !IsHostBot(bot))
            return Fail(handler, words, "guildorder", "no-bot", words.w[1]);
        uint64_t const guildId = tbhost::guild::GetGuildId(bot);
        if (sender)
        {
            std::string code, detail;
            if (!tbhost::guild_order_access::MayGiveOrders(static_cast<uint32_t>(guildId), sender, code, detail))
                return Fail(handler, words, "guildorder", code, detail);
        }

        auto const ord = tbhost::guild_orders::FindOrder(bot->GetGUID().GetCounter());
        if (!ord)
            return Fail(handler, words, "guildorder", "not-found", bot->GetName());
        if (words.w.size() >= 3)
        {
            uint32_t const reqId = static_cast<uint32_t>(std::strtoul(words.w[2].c_str(), nullptr, 10));
            if (reqId && reqId != ord->itemId)
                return Fail(handler, words, "guildorder", "order-mismatch", words.w[2]);
        }

        tbhost::guild_orders::SetOrderWorking(bot->GetGUID().GetCounter(), true);
        uint32_t const have = ord->delivered + tbhost::guild_orders::BagItemCount(bot, ord->itemId);
        TC_LOG_INFO("tortoisebots", "[W9] order id={} bot={} item={} have={} want={} type={}",
            ord->itemId, bot->GetName(), ord->itemId, have, ord->amount, GuildOrders::TypeToken(ord->type));

        Reply reply(handler, words, "guildorder");
        reply.Begin();
        Rec r("guildorder");
        r.S("op", "work").S("bot", bot->GetName()).N("item", ord->itemId)
            .N("have", have).N("want", ord->amount)
            .S("state", GuildOrders::StateToken(ord->state));
        reply.Line(r);
        reply.End(Rec("guildorder-end"));
        return true;
    }

    if (sub == "access")
    {
        uint32_t guildId = 0;
        if (words.w.size() >= 2)
        {
            if (Player* bot = FindOnline(words.w[1]))
                guildId = static_cast<uint32_t>(tbhost::guild::GetGuildId(bot));
            else
                guildId = static_cast<uint32_t>(std::strtoul(words.w[1].c_str(), nullptr, 10));
        }
        else if (sender)
            guildId = static_cast<uint32_t>(tbhost::guild::GetGuildId(sender));
        if (!guildId)
            return Fail(handler, words, "guildorder", "no-guild", "specify <bot|guild_id>");

        tbhost::guild_order_access::AccessReport report;
        if (!tbhost::guild_order_access::BuildAccessReport(guildId, sender, report))
            return Fail(handler, words, "guildorder", "no-guild", "no such guild");

        Reply reply(handler, words, "guildorder");
        reply.Begin();
        Rec r("guildorder");
        r.S("op", "access").N("guild", guildId);
        if (report.enabled)
            r.N("rank", report.botRank).S("rankname", report.rankName)
                .S("tabs", report.tabsOk).S("denied", report.tabsDenied);
        else
            r.S("rank", "-").S("rankname", "-").S("tabs", "-").S("denied", "-");
        r.B("cansetrank", report.canSetRank).B("canorder", report.canGiveOrders);
        reply.Line(r);
        reply.End(Rec("guildorder-end"));
        return true;
    }

    if (sub == "rank")
    {
        if (words.w.size() < 2)
            return Fail(handler, words, "guildorder", "usage", "rank <rankIndex>");
        uint32_t const guildId = sender ? static_cast<uint32_t>(tbhost::guild::GetGuildId(sender)) : 0;
        if (!sender || !guildId)
            return Fail(handler, words, "guildorder", "not-guild", "You are not in a guild.");
        std::string code, detail;
        if (!tbhost::guild_order_access::MayChangeBotRank(guildId, sender, code, detail))
            return Fail(handler, words, "guildorder", code, detail);

        uint32_t const wanted = static_cast<uint32_t>(std::strtoul(words.w[1].c_str(), nullptr, 10));
        uint32_t moved = 0;
        if (!tbhost::guild_order_access::SetBotRank(guildId, wanted, &code, &detail, &moved))
            return Fail(handler, words, "guildorder", code, detail);

        tbhost::guild_order_access::AccessReport report;
        tbhost::guild_order_access::BuildAccessReport(guildId, sender, report);
        Reply reply(handler, words, "guildorder");
        reply.Begin();
        Rec r("guildorder");
        r.S("op", "rank").N("guild", guildId).N("rank", wanted).N("bots", moved)
            .S("rankname", report.rankName).S("tabs", report.tabsOk).S("denied", report.tabsDenied)
            .B("cansetrank", report.canSetRank).B("canorder", report.canGiveOrders);
        reply.Line(r);
        reply.End(Rec("guildorder-end"));
        return true;
    }

    if (sub == "reassign")
    {
        if (words.w.size() < 3)
            return Fail(handler, words, "guildorder", "usage", "reassign <bot> <newBot>");
        Player* bot = FindOnline(words.w[1]);
        if (!bot || !IsHostBot(bot))
            return Fail(handler, words, "guildorder", "no-bot", words.w[1]);
        Player* newBot = FindOnline(words.w[2]);
        if (!newBot || !IsHostBot(newBot))
            return Fail(handler, words, "guildorder", "no-bot", words.w[2]);
        uint64_t const guildId = tbhost::guild::GetGuildId(bot);
        if (sender)
        {
            std::string code, detail;
            if (!tbhost::guild_order_access::MayGiveOrders(static_cast<uint32_t>(guildId), sender, code, detail))
                return Fail(handler, words, "guildorder", code, detail);
        }

        std::string code, detail;
        if (!tbhost::guild_orders::ReassignOrder(bot->GetGUID().GetCounter(),
                newBot->GetGUID().GetCounter(), code, detail))
            return Fail(handler, words, "guildorder", code, detail);

        Reply reply(handler, words, "guildorder");
        reply.Begin();
        Rec r("guildorder");
        r.S("op", "reassign").S("bot", newBot->GetName()).S("from", bot->GetName());
        if (auto const ord = tbhost::guild_orders::FindOrder(newBot->GetGUID().GetCounter()))
            r.N("item", ord->itemId).N("have", ord->delivered).N("want", ord->amount)
                .S("state", GuildOrders::StateToken(ord->state));
        reply.Line(r);
        reply.End(Rec("guildorder-end"));
        return true;
    }

    return Fail(handler, words, "guildorder", "usage", "use: create|list|cancel|status|work|search|who|preview|steps|access|rank|reassign");
}

bool HandleCraftOrder(ChatHandler* handler, Trinity::ChatCommands::Tail args)
{
    Words const words = Split(std::string_view(args));
    if (words.badToken)
        return Fail(handler, words, "craftorder", "bad-token", "invalid #token");
    if (!RequireBuilt(handler, words, "craftorder"))
        return false;
    if (words.w.empty())
        return Fail(handler, words, "craftorder", "usage", "use: create|status|cancel");

    std::string const sub = Lower(words.w[0]);
    Player* sender = SenderPlayer(handler);

    if (sub == "create")
    {
        if (words.w.size() < 4)
            return Fail(handler, words, "craftorder", "usage", "create <bot> <item_id> <amount>");
        Player* bot = FindOnline(words.w[1]);
        if (!bot || !IsHostBot(bot))
            return Fail(handler, words, "craftorder", "no-bot", words.w[1]);
        uint32_t const itemId = static_cast<uint32_t>(std::strtoul(words.w[2].c_str(), nullptr, 10));
        uint32_t const amount = static_cast<uint32_t>(std::strtoul(words.w[3].c_str(), nullptr, 10));
        if (!itemId || !tbcompat::ItemProto(itemId))
            return Fail(handler, words, "craftorder", "bad-item", words.w[2]);
        if (!amount)
            return Fail(handler, words, "craftorder", "bad-amount", words.w[3]);
        if (!tbhost::prof::FindCraftSpellForItem(bot, itemId))
            return Fail(handler, words, "craftorder", "no-recipe", words.w[2]);
        if (!tbhost::guild_orders::IsOrderStoreAvailable())
            return Fail(handler, words, "craftorder", "store-unavailable", "tortoise_bots_guild_order missing");

        GuildOrders::OrderRecord rec;
        rec.botGuidLow = bot->GetGUID().GetCounter();
        rec.guildId = 0; // personal craft order: deliver by mail to giverGuidLow
        rec.giverGuidLow = sender ? sender->GetGUID().GetCounter() : rec.botGuidLow;
        rec.rootGiverGuidLow = rec.giverGuidLow;
        rec.type = GuildOrders::OrderType::Craft;
        rec.itemId = itemId;
        rec.amount = amount;
        rec.hours = GuildOrders::kDefaultOrderHours;
        rec.state = GuildOrders::OrderState::Working;
        rec.reason = GuildOrders::OrderReason::None;
        if (!tbhost::guild_orders::GiveOrder(rec))
            return Fail(handler, words, "craftorder", "store-write-failed", bot->GetName());

        tbhost::guild_orders::SetOrderWorking(rec.botGuidLow, true);
        TC_LOG_INFO("tortoisebots", "[W9] craftorder id={} bot={} item={} done=0 total={} recipient={}",
            itemId, bot->GetName(), itemId, amount, rec.giverGuidLow);

        Reply reply(handler, words, "craftorder");
        reply.Begin();
        Rec r("craftorder");
        r.S("op", "create").S("bot", bot->GetName()).N("item", itemId).N("done", 0).N("total", amount)
            .S("state", GuildOrders::StateToken(rec.state));
        reply.Line(r);
        reply.End(Rec("craftorder-end"));
        return true;
    }

    if (sub == "status")
    {
        if (words.w.size() < 2)
            return Fail(handler, words, "craftorder", "usage", "status <bot>");
        Player* bot = FindOnline(words.w[1]);
        if (!bot || !IsHostBot(bot))
            return Fail(handler, words, "craftorder", "no-bot", words.w[1]);
        auto const ord = tbhost::guild_orders::FindOrder(bot->GetGUID().GetCounter());
        Reply reply(handler, words, "craftorder");
        reply.Begin();
        Rec r("craftorder");
        r.S("op", "status").S("bot", bot->GetName());
        if (ord)
        {
            r.N("item", ord->itemId).N("done", ord->delivered).N("total", ord->amount)
                .S("state", GuildOrders::StateToken(ord->state))
                .S("reason", GuildOrders::ReasonToken(ord->reason));
        }
        else
            r.S("state", "none");
        reply.Line(r);
        reply.End(Rec("craftorder-end"));
        return true;
    }

    if (sub == "cancel")
    {
        if (words.w.size() < 2)
            return Fail(handler, words, "craftorder", "usage", "cancel <bot>");
        Player* bot = FindOnline(words.w[1]);
        if (!bot || !IsHostBot(bot))
            return Fail(handler, words, "craftorder", "no-bot", words.w[1]);
        if (!tbhost::guild_orders::CancelOrder(bot->GetGUID().GetCounter()))
            return Fail(handler, words, "craftorder", "not-found", bot->GetName());
        tbhost::guild_orders::SetOrderWorking(bot->GetGUID().GetCounter(), false);

        Reply reply(handler, words, "craftorder");
        reply.Begin();
        Rec r("craftorder");
        r.S("op", "cancel").S("bot", bot->GetName()).S("state", "cancelled");
        reply.Line(r);
        reply.End(Rec("craftorder-end"));
        return true;
    }

    return Fail(handler, words, "craftorder", "usage", "use: create|status|cancel");
}

} // namespace h3
} // namespace TortoiseBots
