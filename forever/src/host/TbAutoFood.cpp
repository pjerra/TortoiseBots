#include "host/TbAutoFood.h"

#include "host/TbAutoFoodPure.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <unordered_map>

#include "TbBotMgr.h"

#include "Config.h"
#include "ItemTemplate.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "SharedDefines.h"

namespace TortoiseBots
{
namespace autofood
{
namespace
{

std::atomic<bool> g_enabled{ true };
std::unordered_map<uint64_t, uint64_t> g_lastTopUpMs;

uint64_t NowSteadyMs()
{
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

uint32_t StockItem(Player* bot, uint32_t itemId, std::vector<uint32_t> const& staleIds)
{
    ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId);
    if (!proto)
        return 0;

    uint32_t const targetCount = std::min<uint32_t>(proto->GetMaxStackSize(), kMaxRationStack);
    uint32_t const have = bot->GetItemCount(itemId, false);
    if (have >= targetCount)
        return 0;

    for (uint32_t oldId : staleIds)
    {
        if (uint32_t const oldCount = bot->GetItemCount(oldId, false))
            bot->DestroyItemCount(oldId, oldCount, true, false);
    }

    uint32_t const needed = targetCount - have;
    if (!bot->StoreNewItemInBestSlots(itemId, needed, ItemContext::NONE))
        return 0;

    return needed;
}

void ApplyStock(Player* bot, bool onlyWhenLow, char const* why)
{
    uint8_t const level = bot->GetLevel();
    uint32_t const foodId = PickFoodItem(level, bot->GetGUID().GetCounter());
    uint32_t addedFood = 0;
    if (!onlyWhenLow || NeedsTopUp(bot->GetItemCount(foodId, false)))
        addedFood = StockItem(bot, foodId, StaleLadderFoodItems(level));

    uint32_t drinkId = 0;
    uint32_t addedDrink = 0;
    if (bot->GetMaxPower(POWER_MANA) > 0)
    {
        drinkId = PickDrinkItem(level);
        if (!onlyWhenLow || NeedsTopUp(bot->GetItemCount(drinkId, false)))
            addedDrink = StockItem(bot, drinkId, StaleLadderDrinkItems(level));
    }

    if (addedFood > 0 || addedDrink > 0)
    {
        TC_LOG_INFO("tortoisebots", "auto-food: {} (level {}) stocked food={}x{} drink={}x{} on {}",
            bot->GetName(), uint32_t(level), foodId, addedFood, drinkId, addedDrink, why ? why : "sync");
    }
}

} // namespace

void LoadConfig()
{
    g_enabled = sConfigMgr->GetBoolDefault("TortoiseBots.AutoFood", true);
}

bool Enabled()
{
    return g_enabled.load(std::memory_order_relaxed);
}

void Sync(Player* bot, char const* why)
{
    if (!Enabled() || !bot)
        return;

    ObjectGuid const guid = bot->GetGUID();
    if (!BotMgr::Instance().IsPoolBot(guid) && !BotMgr::Instance().IsFreeBot(guid))
        return;

    ApplyStock(bot, false, why);
}

void TickTopUp(Player* bot)
{
    if (!Enabled() || !bot || !bot->IsAlive() || !bot->IsInWorld())
        return;

    ObjectGuid const guid = bot->GetGUID();
    if (!BotMgr::Instance().IsPoolBot(guid) && !BotMgr::Instance().IsFreeBot(guid))
        return;

    uint64_t const nowMs = NowSteadyMs();
    uint64_t& lastMs = g_lastTopUpMs[guid.GetCounter()];
    if (!TopUpCooldownElapsed(lastMs, nowMs))
        return;
    lastMs = nowMs;

    ApplyStock(bot, true, "topup");
}

} // namespace autofood
} // namespace TortoiseBots
