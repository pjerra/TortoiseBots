#include "host/TbHostGuildOrderReassign.h"

#include "host/TbHostGuild.h"
#include "host/TbHostGuildOrderAccess.h"
#include "host/TbHostGuildOrderAccessPure.h"
#include "host/TbHostGuildOrders.h"

#include "Log.h"
#include "ObjectAccessor.h"
#include "ObjectGuid.h"
#include "Player.h"

#include "runtime/GuildOrderContract.h"

#include <optional>
#include <string>

namespace tbhost
{
namespace guild_orders
{

namespace
{

using namespace TortoiseBots::GuildOrders;

Player* FindOnlineBotLow(uint32_t botGuidLow)
{
    if (!botGuidLow)
        return nullptr;
    Player* bot = ObjectAccessor::FindPlayer(ObjectGuid::Create<HighGuid::Player>(botGuidLow));
    if (!bot || !bot->IsInWorld() || !bot->FindMap())
        return nullptr;
    return bot;
}

void Refuse(std::string& code, std::string& detail, char const* c, char const* d)
{
    code = c;
    detail = d;
}

bool ReassignVerdictRefusal(guild_order_access::ReassignVerdict verdict, std::string& code, std::string& detail)
{
    switch (verdict)
    {
        case guild_order_access::ReassignVerdict::Allowed:
            return true;
        case guild_order_access::ReassignVerdict::NoOrder:
            Refuse(code, detail, "not-found", "That bot has no order.");
            return false;
        case guild_order_access::ReassignVerdict::SameBot:
            Refuse(code, detail, "invalid", "The order is already on that bot.");
            return false;
        case guild_order_access::ReassignVerdict::NoGuild:
            Refuse(code, detail, "not-guild", "That order is not in a guild.");
            return false;
        case guild_order_access::ReassignVerdict::TargetElsewhere:
            Refuse(code, detail, "no-bot", "The new bot is offline or not in the order's guild.");
            return false;
        case guild_order_access::ReassignVerdict::TargetBusy:
            Refuse(code, detail, "invalid", "The new bot already has an order.");
            return false;
        case guild_order_access::ReassignVerdict::TargetNotOrderBot:
            Refuse(code, detail, "no-bot", "Only guild bots take orders: never alts or hires.");
            return false;
    }
    Refuse(code, detail, "invalid", "The order could not be moved.");
    return false;
}

// The work state that belongs to the bot, not to the order: cleared so the new
// bot plans its own farm zone and supply leg.
void ResetWorkState(OrderRecord& record)
{
    record.state = OrderState::Working;
    record.reason = OrderReason::None;
    record.cancelled = false;
    record.farmZoneId = 0;
    record.farmChance = 0;
    record.zoneIndex = 0;
    record.farmItemId = 0;
    record.deathsInZone = 0;
    record.searchFailsInZone = 0;
    record.searchZoneMoves = 0;
    record.bankEmptyItemId = 0;
    record.bankEmptyMs = 0;
    record.mailEmptyItemId = 0;
    record.mailEmptyMs = 0;
    record.farmRest = false;
    record.supplyStep = SupplyStep::None;
    record.supplyReturnStep = SupplyStep::None;
    record.supplyItemId = 0;
    record.supplyCount = 0;
    record.supplyGoldCount = 0;
    record.supplyGains.clear();
    record.needItems.clear();
    record.needWhispered = false;
    record.lastNeedWhisper.clear();
    record.needBankDenied = false;
    record.workedSinceNewItemSec = 0;
    record.workedTotalSec = 0;
    record.workedNoGainSec = 0;
    record.reportedCarry = 0;
    record.handedOver = false;
    record.readyWhispered = false;
}

} // namespace

bool ReassignOrder(uint32_t fromBotGuidLow, uint32_t toBotGuidLow,
    std::string& outCode, std::string& outDetail)
{
    outCode.clear();
    outDetail.clear();
    if (!fromBotGuidLow || !toBotGuidLow)
    {
        Refuse(outCode, outDetail, "invalid", "Usage: reassign <bot> <newBot>.");
        return false;
    }

    std::optional<OrderRecord> const source = FindOrder(fromBotGuidLow);
    bool const sourceHasOrder = source && !source->cancelled;
    bool const orderHasGuild = source && source->guildId != 0;

    std::optional<OrderRecord> const targetRow = FindOrder(toBotGuidLow);
    bool const targetBusy = targetRow && guild_order_access::OrderKeepsBotBusy(true, targetRow->cancelled,
        IsFinished(targetRow->state), targetRow->state == OrderState::Stocked);
    Player* target = sourceHasOrder ? FindOnlineBotLow(toBotGuidLow) : nullptr;
    bool const targetOnlineInGuild = target && orderHasGuild && target->GetGuildId() == source->guildId;
    bool const targetIsOrderBot = sourceHasOrder && guild_order_access::IsOrderBot(toBotGuidLow);

    guild_order_access::ReassignVerdict const verdict = guild_order_access::CheckReassign(
        sourceHasOrder, fromBotGuidLow == toBotGuidLow, orderHasGuild,
        targetOnlineInGuild, targetBusy, targetIsOrderBot);
    if (!ReassignVerdictRefusal(verdict, outCode, outDetail))
        return false;

    OrderRecord const original = *source;
    OrderRecord moved = original;
    moved.botGuidLow = toBotGuidLow;
    moved.guildGoldHeld = targetRow ? targetRow->guildGoldHeld : 0;
    moved.reportedHave = original.delivered;
    ResetWorkState(moved);

    // Unspent guild gold the order carried stays with the bot that holds it, as a
    // parked refund stub the delivery sweep repays; the moved row takes only the
    // target's own unrepaid gold.
    if (original.guildGoldHeld > 0)
    {
        // A refund stub is not an order: only the debt is left, so the refund
        // sweep collects it at the old bot's next banker visit.
        OrderRecord stub = original;
        stub.cancelled = true;
        stub.state = OrderState::Working;
        stub.reason = OrderReason::None;
        stub.supplyStep = SupplyStep::None;
        stub.supplyReturnStep = SupplyStep::None;
        stub.supplyItemId = 0;
        stub.supplyCount = 0;
        stub.supplyGoldCount = 0;
        stub.needItems.clear();
        stub.needWhispered = false;
        stub.needBankDenied = false;
        stub.reportedHave = 0;
        if (!GiveOrder(stub))
        {
            Refuse(outCode, outDetail, "store-write-failed", "The old bot's refund row could not be written.");
            return false;
        }
    }
    else if (!CancelOrder(fromBotGuidLow))
    {
        Refuse(outCode, outDetail, "store-write-failed", "The old order row could not be removed.");
        return false;
    }

    if (!GiveOrder(moved))
    {
        GiveOrder(original);
        Refuse(outCode, outDetail, "store-write-failed", "The moved order could not be stored.");
        return false;
    }

    // Grandchildren follow the moved row, so completion and failure still resolve
    // a parent.
    for (OrderRecord const& row : ListOrdersForGuild(original.guildId))
    {
        if (row.botGuidLow == toBotGuidLow || row.parentBotGuidLow != fromBotGuidLow)
            continue;
        OrderRecord child = row;
        child.parentBotGuidLow = toBotGuidLow;
        if (!GiveOrder(child))
            TC_LOG_ERROR("tortoisebots", "[W9] reassign parent={} child={}: the child link could not be stored", toBotGuidLow, row.botGuidLow);
    }

    bool const wasWorking = IsOrderWorking(fromBotGuidLow);
    SetOrderWorking(fromBotGuidLow, false);
    SetOrderWorking(toBotGuidLow, wasWorking);

    TC_LOG_INFO("tortoisebots", "[W9] reassign from={} to={} item={} have={} want={}",
        fromBotGuidLow, toBotGuidLow, moved.itemId, moved.delivered, moved.amount);
    return true;
}

} // namespace guild_orders
} // namespace tbhost
