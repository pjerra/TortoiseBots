// The world-thread banker pass of the guild gold refund queue (TBF #155):
// repay unspent guild gold (any row holding it, including the cancelled stub a
// reassign leaves) through the deposit-only host guild seam, and only when a
// guild banker is in reach or can be walked to, exactly like the order
// delivery.
#include "host/TbHostGuildOrderQueues.h"

#include "host/TbHostGuild.h"
#include "host/TbHostGuildOrders.h"

#include "CellImpl.h"
#include "GameObject.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Log.h"
#include "MotionMaster.h"
#include "ObjectGuid.h"
#include "Player.h"

#include "runtime/GuildOrderContract.h"
#include "runtime/GuildOrderRefundPolicy.h"

#include <ctime>
#include <list>
#include <optional>
#include <string>
#include <unordered_map>

namespace tbhost
{
namespace guild_orders
{
namespace
{
using namespace TortoiseBots::GuildOrders;

constexpr float kMaxVisitDistYd = 300.0f;
// A bot owing gold scans for a banker at this cadence, not on every manager
// pass: the scan walks the grid and the bot has all the time it needs.
constexpr uint64_t kBankerScanIntervalSec = 5;

std::unordered_map<uint32_t, uint64_t>& NextBankerScan()
{
    static std::unordered_map<uint32_t, uint64_t> next;
    return next;
}

GameObject* FindGuildBanker(Player* bot, float maxDistYd)
{
    std::list<GameObject*> objects;
    Trinity::GameObjectInRangeCheck check(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), maxDistYd, 0);
    Trinity::GameObjectListSearcher<Trinity::GameObjectInRangeCheck> searcher(bot, objects, check);
    Cell::VisitGridObjects(bot, searcher, maxDistYd);

    GameObject* best = nullptr;
    float bestDist = maxDistYd + 1.0f;
    uint32 const botZone = bot->GetZoneId();
    for (GameObject* go : objects)
    {
        if (!go || !go->isSpawned() || go->GetGoType() != GAMEOBJECT_TYPE_GUILD_BANK)
            continue;
        if (go->GetZoneId() != botZone)
            continue;
        float const distance = bot->GetDistance(go);
        if (distance <= maxDistYd && distance < bestDist)
        {
            bestDist = distance;
            best = go;
        }
    }
    return best;
}

void WalkTo(Player* bot, GameObject* go)
{
    bot->GetMotionMaster()->MovePoint(0, go->GetPositionX(), go->GetPositionY(), go->GetPositionZ());
}

void ApplyHeldGoldRefund(uint32_t botGuidLow, uint32_t moved)
{
    std::optional<OrderRecord> row = FindOrder(botGuidLow);
    if (!row || !moved)
        return;
    uint32_t const left = GuildGoldRefundRemaining(row->guildGoldHeld, moved);
    row->guildGoldHeld = left;
    if (row->cancelled && left == 0)
    {
        CancelOrder(botGuidLow);
        TC_LOG_INFO("tortoisebots", "[W9] refund stub repaid bot={} copper={}", botGuidLow, moved);
        return;
    }
    GiveOrder(*row);
    TC_LOG_INFO("tortoisebots", "[W9] refunded guild gold bot={} copper={} left={}", botGuidLow, moved, left);
}

void PayHeldGold(Player* bot, OrderRecord const& order, ObjectGuid const& bankerGuid)
{
    uint32_t const repay = GuildGoldRefundAmount(order.guildGoldHeld, uint32_t(bot->GetMoney()));
    if (!repay)
        return;
    uint64_t const before = bot->GetMoney();
    std::string failReason;
    if (!tbhost::guild::DepositMoney(bot, bankerGuid, repay, &failReason))
        return;
    uint64_t const after = bot->GetMoney();
    if (before <= after)
        return;
    ApplyHeldGoldRefund(bot->GetGUID().GetCounter(), uint32_t(before - after));
}

} // namespace

void TickGuildOrderQueues(Player* bot)
{
    if (!bot || !bot->IsInWorld() || !bot->FindMap())
        return;
    uint32_t const botGuidLow = bot->GetGUID().GetCounter();
    uint64_t const nowSec = static_cast<uint64_t>(std::time(nullptr));

    std::optional<OrderRecord> order = FindOrder(botGuidLow);
    if (!order || order->guildGoldHeld == 0 || order->guildId != bot->GetGuildId())
        return;
    if (bot->isDead() || bot->IsInCombat())
        return;
    uint64_t& nextScan = NextBankerScan()[botGuidLow];
    if (nowSec < nextScan)
        return;
    nextScan = nowSec + kBankerScanIntervalSec;
    GameObject* banker = FindGuildBanker(bot, kMaxVisitDistYd);
    if (!banker)
        return;
    if (!bot->GetGameObjectIfCanInteractWith(banker->GetGUID(), GAMEOBJECT_TYPE_GUILD_BANK))
    {
        WalkTo(bot, banker);
        return;
    }
    PayHeldGold(bot, *order, banker->GetGUID());
}

void ForgetGuildOrderQueueSession(uint32_t botGuidLow)
{
    NextBankerScan().erase(botGuidLow);
}

} // namespace guild_orders
} // namespace tbhost
