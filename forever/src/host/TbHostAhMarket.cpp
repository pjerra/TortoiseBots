// The automatic auction-house seller sweep: once per AiPlayerbot.AhMarketInterval, up to AhMarketBatchSize free
// bots with sellable loot either post at the auctioneer they stand next to or walk to one within 500 yd of their
// own zone, then post through the priced seller action (`ah vendor`, AhSellPolicy). No item is created and no
// auction is written directly: the existing host seam and the core handlers do the work.
#include "host/TbHostAhMarket.h"
#include "host/TbHostAhMarketBuy.h"

#include "TbBotMgr.h"
#include "host/TbEngineBrain.h"
#include "host/TbHostAhMarketPure.h"
#include "host/TbHostAuction.h"
#include "host/TbHostCapitalErrands.h"
#include "host/TbHostGuildMail.h"
#include "host/TbHostGuildOrderDisenchant.h"
#include "host/TbHostGuildOrderEnchant.h"
#include "host/TbHostGuildOrders.h"
#include "host/TbHostProfession.h"

#include "compat/botpch_forever.h"
#include "slice/SliceLog.h"

#include "Bag.h"
#include "Creature.h"
#include "GameTime.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "Log.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "SharedDefines.h"

#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/policy/ah/AhSellPolicy.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace tbhost
{
namespace ah_market
{
namespace
{

using ::ai::slice::LogInfo;

constexpr float kAuctioneerSearchYd = 500.0f;

struct BotMarketState
{
    bool walking = false;
    ObjectGuid target;
    uint64_t startedAtMs = 0;
    uint64_t nextTryMs = 0;
};

struct SweepCycle
{
    uint64_t lastCycleMs = 0;
    uint32_t nextIndex = 0;
    bool announced = false;
};

std::unordered_map<ObjectGuid, BotMarketState>& States()
{
    static std::unordered_map<ObjectGuid, BotMarketState> map;
    return map;
}

SweepCycle& Cycle()
{
    static SweepCycle cycle;
    return cycle;
}

template <typename Fn>
void ForEachBagItem(Player* bot, Fn&& fn)
{
    for (uint8_t slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
    {
        if (Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
            fn(item);
    }
    for (uint8_t bagSlot = INVENTORY_SLOT_BAG_START; bagSlot < INVENTORY_SLOT_BAG_END; ++bagSlot)
    {
        Bag* bag = bot->GetBagByPos(bagSlot);
        if (!bag)
            continue;
        for (uint32_t slot = 0; slot < bag->GetBagSize(); ++slot)
        {
            if (Item* item = bag->GetItemByPos(slot))
                fn(item);
        }
    }
}

bool IsFoodOrWaterOrAmmo(ItemTemplate const* proto)
{
    if (!proto)
        return false;
    if (proto->GetClass() == ITEM_CLASS_PROJECTILE)
        return true;
    return proto->GetClass() == ITEM_CLASS_CONSUMABLE &&
        (proto->GetSubClass() == ITEM_SUBCLASS_FOOD_DRINK || proto->GetSubClass() == ITEM_SUBCLASS_CONSUMABLE_OTHER);
}

// The coarse pre-check the capital AH errand uses: it only has to prove that walking there can post something;
// AhSellPolicy makes the real per-item decision.
bool HasSellableStock(Player* bot)
{
    bool found = false;
    ForEachBagItem(bot, [&](Item* item)
    {
        if (found)
            return;
        ItemTemplate const* proto = item->GetTemplate();
        if (!proto)
            return;
        if (item->IsSoulBound() || proto->GetClass() == ITEM_CLASS_QUEST ||
            proto->GetQuality() < ITEM_QUALITY_UNCOMMON)
            return;
        if (ai::IsAhProfessionTool(proto->GetId(), false) || IsFoodOrWaterOrAmmo(proto))
            return;
        found = true;
    });
    return found;
}

bool BusyElsewhere(Player* bot)
{
    return errands::HasActiveErrand(bot) || prof::HasActiveErrand(bot) ||
        guild_mail::HasActiveErrand(bot) || guild_orders::HasActiveDisenchantErrand(bot) ||
        guild_orders::HasActiveEnchantErrand(bot) ||
        guild_orders::FindOrder(bot->GetGUID().GetCounter()).has_value();
}

bool OpenWorldMap(Player* bot)
{
    Map* map = bot ? bot->FindMap() : nullptr;
    return map && !map->IsDungeon() && !map->IsBattleground();
}

SweepCandidateInput BuildCandidate(Player* bot)
{
    SweepCandidateInput in;
    in.freeBot = TortoiseBots::BotMgr::Instance().IsFreeBot(bot->GetGUID());
    in.inWorld = bot->IsInWorld();
    in.alive = bot->IsAlive();
    in.inCombat = bot->IsInCombat();
    in.moving = bot->isMoving();
    in.casting = bot->GetCurrentSpell(CURRENT_GENERIC_SPELL) != nullptr;
    in.inDuel = bot->duel != nullptr;
    in.grouped = bot->GetGroup() != nullptr;
    in.openWorldMap = OpenWorldMap(bot);
    in.busyElsewhere = BusyElsewhere(bot);
    in.hasSellableStock = HasSellableStock(bot);
    return in;
}

void StartWalk(Player* bot, Creature* auctioneer, BotMarketState& state, uint64_t nowMs, uint64_t retryMs)
{
    state.walking = true;
    state.target = auctioneer->GetGUID();
    state.startedAtMs = nowMs;
    state.nextTryMs = nowMs + retryMs;
    if (bot->isMoving())
        bot->StopMoving();
    bot->GetMotionMaster()->MovePoint(0, auctioneer->GetPositionX(), auctioneer->GetPositionY(),
        auctioneer->GetPositionZ());
}

void ContinueWalk(Player* bot, BotMarketState& state, uint64_t nowMs, uint64_t retryMs)
{
    auto finish = [&](char const* result, char const* reason)
    {
        std::string line = std::string("[W8] ah-market bot=") + bot->GetName() + " kind=sell result=" + result;
        if (reason)
            line += std::string(" reason=") + reason;
        LogInfo(line);
        ClearBotAhMarket(bot->GetGUID());
    };

    if (!bot->IsAlive() || bot->IsInCombat() || bot->duel != nullptr)
    {
        finish("failed", "interrupted");
        return;
    }
    if (WalkTimedOut(nowMs, state.startedAtMs))
    {
        finish("failed", "timeout");
        return;
    }

    Creature* auctioneer = ObjectAccessor::GetCreature(*bot, state.target);
    if (!auctioneer || !auctioneer->IsInWorld() || auctioneer->GetMap() != bot->FindMap())
    {
        finish("failed", "no-auctioneer");
        return;
    }

    if (bot->GetDistance(auctioneer) > INTERACTION_DISTANCE)
    {
        if (!bot->isMoving())
            bot->GetMotionMaster()->MovePoint(0, auctioneer->GetPositionX(), auctioneer->GetPositionY(),
                auctioneer->GetPositionZ());
        return;
    }

    bot->StopMoving();
    bool const posted = TortoiseBots::engine::RunAction(bot, "ah", "vendor");
    state.nextTryMs = nowMs + retryMs;
    finish(posted ? "posted" : "refused", nullptr);
}

void RunCycle(uint64_t nowMs)
{
    SweepCycle& cycle = Cycle();
    cycle.lastCycleMs = nowMs;

    uint32_t const intervalSeconds = ClampIntervalSeconds(sPlayerbotAIConfig.ahMarketInterval);
    uint32_t const batch = ClampBatchSize(sPlayerbotAIConfig.ahMarketBatchSize);
    uint64_t const retryMs = IntervalMs(intervalSeconds);
    if (!cycle.announced)
    {
        cycle.announced = true;
        LogInfo(std::string("TortoiseBots: ah-market on interval=") + std::to_string(intervalSeconds) +
            " batch=" + std::to_string(batch));
    }

    TortoiseBots::BotMgr& mgr = TortoiseBots::BotMgr::Instance();
    std::vector<Player*> eligible;
    for (ObjectGuid const& guid : mgr.BotGuids())
    {
        Player* bot = mgr.FindBot(guid);
        if (!bot || bot->IsBeingTeleported())
            continue;
        if (!IsSweepCandidate(BuildCandidate(bot)))
            continue;
        eligible.push_back(bot);
    }

    if (eligible.empty())
    {
        cycle.nextIndex = 0;
        LogInfo(std::string("[W8] ah-market interval=") + std::to_string(intervalSeconds) +
            " eligible=0 picked=0 posted=0 walking=0 skipped=0");
        RunBuyStep();
        return;
    }

    size_t const size = eligible.size();
    uint32_t const start = cycle.nextIndex % static_cast<uint32_t>(size);
    uint32_t considered = 0;
    uint32_t picked = 0;
    uint32_t posted = 0;
    uint32_t walking = 0;
    uint32_t skipped = 0;

    for (size_t offset = 0; offset < size && considered < batch; ++offset)
    {
        Player* bot = eligible[(start + offset) % size];
        ++considered;

        auto& state = States()[bot->GetGUID()];
        if (nowMs < state.nextTryMs)
        {
            ++skipped;
            continue;
        }

        Creature* auctioneer = ah::FindNearestFactionAuctioneer(bot, kAuctioneerSearchYd);
        if (!auctioneer)
        {
            state.nextTryMs = nowMs + retryMs;
            ++skipped;
            LogInfo(std::string("[W8] ah-market bot=") + bot->GetName() +
                " kind=sell result=skipped reason=no-auctioneer");
            continue;
        }

        ++picked;
        if (bot->GetDistance(auctioneer) <= INTERACTION_DISTANCE)
        {
            bot->StopMoving();
            bool const ok = TortoiseBots::engine::RunAction(bot, "ah", "vendor");
            state.nextTryMs = nowMs + retryMs;
            if (ok)
                ++posted;
            LogInfo(std::string("[W8] ah-market bot=") + bot->GetName() + " kind=sell result=" +
                (ok ? "posted" : "refused"));
        }
        else
        {
            StartWalk(bot, auctioneer, state, nowMs, retryMs);
            ++walking;
            LogInfo(std::string("[W8] ah-market bot=") + bot->GetName() + " kind=sell result=walking");
        }
    }

    cycle.nextIndex = AdvanceRotation(start, considered, static_cast<uint32_t>(size));
    LogInfo(std::string("[W8] ah-market interval=") + std::to_string(intervalSeconds) +
        " eligible=" + std::to_string(size) + " picked=" + std::to_string(picked) +
        " posted=" + std::to_string(posted) + " walking=" + std::to_string(walking) +
        " skipped=" + std::to_string(skipped));

    RunBuyStep();
}

bool MarketEnabled()
{
    return sPlayerbotAIConfig.ahMarketEnabled && ah::IsWorldAuctionEnabled();
}

} // namespace

void Tick(Player* bot, uint32_t /*diffMs*/)
{
    if (!bot || !bot->IsInWorld() || !bot->FindMap() || bot->IsBeingTeleported())
        return;
    if (!sPlayerbotAIConfig.ahMarketEnabled)
        return;

    uint64_t const nowMs = GameTime::GetGameTimeMS();
    auto walking = States().find(bot->GetGUID());
    if (walking != States().end() && walking->second.walking)
    {
        if (!ah::IsWorldAuctionEnabled())
        {
            ClearBotAhMarket(bot->GetGUID());
            return;
        }
        ContinueWalk(bot, walking->second, nowMs, IntervalMs(sPlayerbotAIConfig.ahMarketInterval));
        return;
    }

    if (!SweepCycleDue(nowMs, Cycle().lastCycleMs, sPlayerbotAIConfig.ahMarketInterval))
        return;

    if (!MarketEnabled())
    {
        // One check per interval while the gate is off; enabling it takes effect on the next one.
        Cycle().lastCycleMs = nowMs;
        return;
    }
    RunCycle(nowMs);
}

bool HasActiveErrand(Player const* bot)
{
    if (!bot)
        return false;
    auto const itr = States().find(bot->GetGUID());
    return itr != States().end() && itr->second.walking;
}

void ClearBotAhMarket(ObjectGuid const& botGuid)
{
    States().erase(botGuid);
}

} // namespace ah_market
} // namespace tbhost
