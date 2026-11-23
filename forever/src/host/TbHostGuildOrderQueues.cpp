// The world-thread banker pass of the guild order queues (TBF #155): pay
// unspent guild gold back (any row holding it, including the cancelled stub a
// reassign leaves) and pay collected relist value to the guild bank. Both go
// through the deposit-only host guild seam, and only when a guild banker is in
// reach or can be walked to, exactly like the order delivery.
#include "host/TbHostGuildOrderQueues.h"

#include "host/TbHostGuild.h"
#include "host/TbHostGuildOrders.h"

#include "Bag.h"
#include "CellImpl.h"
#include "GameObject.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Item.h"
#include "Log.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "ObjectGuid.h"
#include "Player.h"

#include "runtime/GuildOrderContract.h"
#include "runtime/GuildOrderDeliveryPolicy.h"
#include "runtime/GuildOrderRefundPolicy.h"

#include <ctime>
#include <list>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace tbhost
{
namespace guild_orders
{
namespace
{
using namespace TortoiseBots::GuildOrders;

constexpr float kMaxVisitDistYd = 300.0f;
// A bot owing value scans for a banker at this cadence, not on every manager
// pass: the scan walks the grid and the boat has all the time it needs.
constexpr uint64_t kBankerScanIntervalSec = 5;

// Session start per bot, for the relist expiry sweep: ForeverCore's Player
// has no login time, so the first world tick after a bot entered the world
// stands in for it. World thread only (the bot manager pass and OnBotLogout).
std::unordered_map<uint32_t, uint64_t>& Sessions()
{
    static std::unordered_map<uint32_t, uint64_t> sessions;
    return sessions;
}

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

// One stack of the item no larger than `maxCount`: depositing it can never
// hand over more than the ledger owes. Oversized stacks stay in the bags.
Item* PickStackAtMost(Player* bot, uint32_t itemId, uint32_t maxCount)
{
    Item* best = nullptr;
    auto consider = [&](Item* item)
    {
        if (!item || item->GetEntry() != itemId || !item->GetCount() || item->GetCount() > maxCount)
            return;
        if (!best || item->GetCount() > best->GetCount())
            best = item;
    };
    for (uint8_t slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
        consider(bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot));
    for (uint8_t bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END; ++bag)
        if (Bag* pBag = bot->GetBagByPos(bag))
            for (uint32_t slot = 0; slot < pBag->GetBagSize(); ++slot)
                consider(pBag->GetItemByPos(slot));
    return best;
}

uint32_t DepositBagItems(Player* bot, ObjectGuid const& bankerGuid, uint32_t itemId, uint32_t want)
{
    uint32_t moved = 0;
    while (moved < want)
    {
        Item* stack = PickStackAtMost(bot, itemId, want - moved);
        if (!stack)
            break;
        uint32_t const count = stack->GetCount();
        bool deposited = false;
        for (uint8_t tab = 0; tab < kDeliveryTabCount && !deposited; ++tab)
        {
            std::string failReason;
            if (tbhost::guild::DepositItem(bot, bankerGuid, tab, stack, &failReason))
                deposited = true;
            else if (failReason == "cannot-interact-bank" || failReason == "not-in-world")
                break;
        }
        if (!deposited)
            break;
        moved += count;
    }
    return moved;
}

// The ledger rows the bank pass rewrites (the policy's checked Save/Delete).
struct RelistLedgerWrites
{
    bool Save(RelistLedgerEntry const& row) { return tbhost::guild_orders::SaveRelistRow(row); }
    bool Delete(RelistLedgerEntry const& row) { return tbhost::guild_orders::DeleteRelistRow(row.botGuidLow, row.seq); }
};

// The guild bank as one ledger row's trip sees it: gold and bag items go in
// through the deposit-only seam, the same path the order delivery uses.
struct RelistBankWrites
{
    Player* bot = nullptr;
    ObjectGuid bankerGuid;
    uint32_t itemId = 0;

    RelistMoved Deposit(uint32_t gold, uint32_t items)
    {
        RelistMoved moved;
        if (gold)
        {
            uint64_t const before = bot->GetMoney();
            std::string failReason;
            if (tbhost::guild::DepositMoney(bot, bankerGuid, gold, &failReason))
            {
                uint64_t const after = bot->GetMoney();
                moved.gold = before > after ? uint32_t(before - after) : 0u;
            }
        }
        if (items)
            moved.items = DepositBagItems(bot, bankerGuid, itemId, items);
        return moved;
    }
};

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

void PayRelistRows(Player* bot, GameObject* banker, std::vector<RelistLedgerEntry> const& rows)
{
    for (RelistLedgerEntry const& entry : rows)
    {
        RelistTripBot const view{ uint32_t(bot->GetGuildId()), true, uint32_t(bot->GetMoney()),
            bot->GetItemCount(entry.itemId) };
        RelistLedgerWrites ledger;
        RelistBankWrites bank{ bot, banker->GetGUID(), entry.itemId };
        RelistTripOutcome const outcome = RelistBankTrip(entry, view, ledger, bank);
        switch (outcome)
        {
            case RelistTripOutcome::Dropped:
                TC_LOG_INFO("tortoisebots", "[W9] relist bot={} left guild {}; dropped row item={} seq={}",
                    entry.botGuidLow, entry.guildId, entry.itemId, entry.seq);
                break;
            case RelistTripOutcome::Refused:
                TC_LOG_ERROR("tortoisebots", "[W9] relist row write failed bot={} item={} seq={}; nothing deposited",
                    entry.botGuidLow, entry.itemId, entry.seq);
                break;
            case RelistTripOutcome::Lost:
                TC_LOG_ERROR("tortoisebots", "[W9] bank took less than planned bot={} item={} seq={} and the rest "
                    "could not be written back", entry.botGuidLow, entry.itemId, entry.seq);
                break;
            case RelistTripOutcome::Deposited:
            case RelistTripOutcome::Returned:
                TC_LOG_INFO("tortoisebots", "[W9] relist paid bot={} item={} seq={} result={}",
                    entry.botGuidLow, entry.itemId, entry.seq,
                    outcome == RelistTripOutcome::Deposited ? "deposited" : "partial");
                break;
            case RelistTripOutcome::Idle:
                break;
        }
    }
}

} // namespace

void TickGuildOrderQueues(Player* bot)
{
    if (!bot || !bot->IsInWorld() || !bot->FindMap())
        return;
    uint32_t const botGuidLow = bot->GetGUID().GetCounter();
    uint64_t const nowSec = static_cast<uint64_t>(std::time(nullptr));
    Sessions().try_emplace(botGuidLow, nowSec);

    if (RelistSweepDue(nowSec))
    {
        std::vector<uint32_t> const aged = AgedRelistBots(nowSec);
        if (!aged.empty())
        {
            std::unordered_map<uint32_t, RelistBotPresence> presence;
            for (uint32_t low : aged)
            {
                RelistBotPresence p;
                Player* other = ObjectAccessor::FindPlayer(ObjectGuid::Create<HighGuid::Player>(low));
                if (other && other->IsInWorld())
                {
                    p.online = true;
                    auto const seen = Sessions().find(low);
                    if (seen != Sessions().end())
                        p.loginSec = seen->second;
                }
                presence[low] = p;
            }
            ExpireRelistRows(nowSec, presence);
        }
    }

    std::optional<OrderRecord> order = FindOrder(botGuidLow);
    bool const owesGold = order && order->guildGoldHeld != 0 && order->guildId == bot->GetGuildId();
    std::vector<RelistLedgerEntry> const relists = RelistDeposits(botGuidLow);
    if (!owesGold && relists.empty())
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
    if (owesGold)
        PayHeldGold(bot, *order, banker->GetGUID());
    if (!relists.empty())
        PayRelistRows(bot, banker, relists);
}

void ForgetGuildOrderQueueSession(uint32_t botGuidLow)
{
    Sessions().erase(botGuidLow);
    NextBankerScan().erase(botGuidLow);
}

} // namespace guild_orders
} // namespace tbhost
