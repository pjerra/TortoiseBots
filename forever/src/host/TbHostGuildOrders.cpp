#include "host/TbHostGuildOrders.h"

#include "compat/TbCompat_CharStore.h"
#include "compat/TbItemPrototype.h"
#include "compat/TbSqlNames.h"
#include "host/TbH3.h"
#include "host/TbHostGuild.h"

#include "Chat.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "Player.h"
#include "StringFormat.h"

#include "runtime/GuildOrderBringPolicy.h"
#include "runtime/GuildOrderContract.h"
#include "runtime/GuildOrderGiveUpPolicy.h"
#include "runtime/GuildOrderNeedPolicy.h"
#include "runtime/GuildOrderStoreColumns.h"

#include <cstdlib>
#include <ctime>
#include <mutex>
#include <set>
#include <unordered_map>

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

class NullGatherCraftProvider final : public IGatherCraftProvider
{
public:
    bool CanGather(uint32_t, uint32_t) const override { return false; }
    bool CanCraft(uint32_t, uint32_t) const override { return false; }
    bool StepGatherOrCraft(uint32_t, uint32_t, uint32_t, std::string& outReason) override
    {
        outReason = "w7-not-wired";
        return false;
    }
};

class NullAuctionBuyProvider final : public IAuctionBuyProvider
{
public:
    bool CanBuyOnAuction(uint32_t, uint32_t, uint32_t) const override { return false; }
    bool StepBuyOnAuction(uint32_t, uint32_t, uint32_t, uint32_t, std::string& outReason) override
    {
        outReason = "w8-not-wired";
        return false;
    }
};

class NullDeliveryProvider final : public IDeliveryProvider
{
public:
    bool CanDeliverToBank(uint32_t, uint64_t) const override { return false; }
    bool CanDeliverByMail(uint32_t, uint32_t) const override { return false; }
    bool Deliver(uint32_t, uint64_t, uint32_t, uint32_t, uint32_t, std::string& outReason) override
    {
        outReason = "w4-travel-not-wired";
        return false;
    }
};

NullGatherCraftProvider g_nullGatherCraft;
NullAuctionBuyProvider g_nullAuctionBuy;
NullDeliveryProvider g_nullDelivery;

IGatherCraftProvider* g_gatherCraft = &g_nullGatherCraft;
IAuctionBuyProvider* g_auctionBuy = &g_nullAuctionBuy;
IDeliveryProvider* g_delivery = &g_nullDelivery;

} // namespace

void LoadOrders()
{
    StoreState& s = State();
    std::lock_guard<std::mutex> lock(s.mutex);
    s.orders.clear();
    s.loaded = true;
    s.available = false;

    if (!EnsureSchema(s))
        return;

    std::string const sql = "SELECT " + StoreColumns::SelectList() + " FROM `tortoise_bots_guild_order`";
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
    if (!record.botGuidLow || !record.guildId || !record.itemId || !record.amount)
        return false;

    StoreState& s = State();
    std::lock_guard<std::mutex> lock(s.mutex);
    if (!EnsureSchema(s))
        return false;

    uint64_t const now = uint64_t(std::time(nullptr));
    if (!record.createdAt)
        record.createdAt = now;
    record.updatedAt = now;

    std::string const sql = Trinity::StringFormat(
        "REPLACE INTO `tortoise_bots_guild_order` ({}) VALUES "
        "({}, {}, {}, {}, {}, '{}', {}, {}, {}, {}, '{}', '{}', {}, {}, {}, {}, {}, "
        "{}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, '{}', '{}', {}, {}, {}, '{}', {}, '{}', {}, {}, {}, "
        "{}, {}, {}, '{}', '{}')",
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
        record.reportedCarry, EscapeSql(BringItemsToken(record.bringItems)), EscapeSql(GivenItemsToken(record.given)));

    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
    trans->Append(sql.c_str());
    CharacterDatabase.CommitTransaction(trans);

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
    return true;
}

IGatherCraftProvider& GatherCraft() { return *g_gatherCraft; }
IAuctionBuyProvider& AuctionBuy() { return *g_auctionBuy; }
IDeliveryProvider& Delivery() { return *g_delivery; }

void SetGatherCraftProvider(IGatherCraftProvider* provider) { g_gatherCraft = provider ? provider : &g_nullGatherCraft; }
void SetAuctionBuyProvider(IAuctionBuyProvider* provider) { g_auctionBuy = provider ? provider : &g_nullAuctionBuy; }
void SetDeliveryProvider(IDeliveryProvider* provider) { g_delivery = provider ? provider : &g_nullDelivery; }

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
        return Fail(handler, words, "guildorder", "usage", "use: create|list|cancel|status");

    std::string const sub = Lower(words.w[0]);
    Player* sender = SenderPlayer(handler);

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
            if (tbhost::guild::GetGuildId(sender) != guildId)
                return Fail(handler, words, "guildorder", "wrong-guild", bot->GetName());
            if (!tbhost::guild::CanEditOfficerNotes(guildId, sender->GetGUID()))
                return Fail(handler, words, "guildorder", "no-rank-right", "requires edit officer notes");
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
        if (sender && guildId && !tbhost::guild::CanEditOfficerNotes(guildId, sender->GetGUID()))
            return Fail(handler, words, "guildorder", "no-rank-right", "requires edit officer notes");
        if (!tbhost::guild_orders::CancelOrder(bot->GetGUID().GetCounter()))
            return Fail(handler, words, "guildorder", "not-found", bot->GetName());

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

    return Fail(handler, words, "guildorder", "usage", "use: create|list|cancel|status");
}

} // namespace h3
} // namespace TortoiseBots
