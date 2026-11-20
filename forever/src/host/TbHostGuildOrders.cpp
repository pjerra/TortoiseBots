#include "host/TbHostGuildOrders.h"

#include "compat/TbCompat_CharStore.h"
#include "compat/TbItemPrototype.h"
#include "compat/TbSqlNames.h"
#include "host/TbH3.h"
#include "host/TbHostAuction.h"
#include "host/TbHostGuild.h"
#include "host/TbHostProfession.h"

#include "Bag.h"
#include "CellImpl.h"
#include "CharacterCache.h"
#include "Chat.h"
#include "Creature.h"
#include "DatabaseEnv.h"
#include "GameObject.h"
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

class HostGatherCraftProvider final : public IGatherCraftProvider
{
public:
    bool CanGather(uint32_t botGuidLow, uint32_t /*itemId*/) const override
    {
        Player* bot = FindOnlineBot(botGuidLow);
        return bot && tbhost::prof::HasGatherableNodeInSight(bot, 150.0f);
    }

    bool CanCraft(uint32_t botGuidLow, uint32_t itemId) const override
    {
        Player* bot = FindOnlineBot(botGuidLow);
        return bot && tbhost::prof::FindCraftSpellForItem(bot, itemId) != 0;
    }

    bool StepGatherOrCraft(uint32_t botGuidLow, uint32_t itemId, uint32_t /*wantCount*/, std::string& outReason) override
    {
        Player* bot = FindOnlineBot(botGuidLow);
        if (!bot)
        {
            outReason = "not-in-world";
            return false;
        }

        if (uint32_t const spellId = tbhost::prof::FindCraftSpellForItem(bot, itemId))
        {
            uint32_t const beforeCount = bot->GetItemCount(itemId, false);
            std::string code, detail;
            if (tbhost::prof::Craft(bot, spellId, 1, code, detail))
            {
                if (bot->GetItemCount(itemId, false) > beforeCount)
                {
                    outReason = "crafted";
                    return true;
                }
                outReason = "craft-no-gain";
                return false;
            }
            outReason = code.empty() ? "craft-failed" : code;
            return false;
        }

        uint32_t const beforeCount = bot->GetItemCount(itemId, false);
        std::string code, detail;
        if (tbhost::prof::GatherNearestInSight(bot, 150.0f, code, detail))
        {
            if (code == "walking")
            {
                outReason = "walking";
                return false;
            }
            if (bot->GetItemCount(itemId, false) > beforeCount)
            {
                outReason = "gathered";
                return true;
            }
            outReason = "gather-no-item";
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
        if (!bot || !recipientGuidLow)
            return false;
        return FindNearestGameObjectByType(bot, GAMEOBJECT_TYPE_MAILBOX, kW2MaxVisitDistYd) != nullptr;
    }

    bool Deliver(uint32_t botGuidLow, uint64_t guildId, uint32_t recipientGuidLow, uint32_t itemId, uint32_t /*count*/, std::string& outReason) override
    {
        Player* bot = FindOnlineBot(botGuidLow);
        if (!bot)
        {
            outReason = "not-in-world";
            return false;
        }
        Item* item = FindBagItemByEntry(bot, itemId);
        if (!item)
        {
            outReason = "no-items";
            return false;
        }

        if (GameObject* banker = FindNearestGameObjectByType(bot, GAMEOBJECT_TYPE_GUILD_BANK, kW2MaxVisitDistYd))
        {
            if (!bot->GetGameObjectIfCanInteractWith(banker->GetGUID(), GAMEOBJECT_TYPE_GUILD_BANK))
            {
                bot->GetMotionMaster()->MovePoint(0, banker->GetPositionX(), banker->GetPositionY(), banker->GetPositionZ());
                outReason = "walking";
                return false;
            }
            for (uint8_t tab = 0; tab < kDeliveryTabCount; ++tab)
            {
                std::string failReason;
                if (tbhost::guild::DepositItem(bot, banker->GetGUID(), tab, item, &failReason))
                {
                    outReason = "bank";
                    return true;
                }
            }
        }

        GameObject* mailbox = FindNearestGameObjectByType(bot, GAMEOBJECT_TYPE_MAILBOX, kW2MaxVisitDistYd);
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

        uint32_t targetLow = recipientGuidLow;
        if (!targetLow || targetLow == botGuidLow)
            targetLow = tbhost::guild::GetLeaderGuid(guildId).GetCounter();
        std::string targetName;
        if (!targetLow || !sCharacterCache->GetCharacterNameByGuid(ObjectGuid::Create<HighGuid::Player>(targetLow), targetName))
        {
            outReason = "no-recipient";
            return false;
        }

        uint32_t const beforeCount = bot->GetItemCount(itemId, false);
        std::vector<ObjectGuid> attachments;
        attachments.push_back(item->GetGUID());
        std::string failReason;
        if (!tbhost::guild::SendMail(bot, mailbox->GetGUID(), targetName, "Guild Order", "", 0, attachments, &failReason))
        {
            outReason = failReason.empty() ? "mail-failed" : failReason;
            return false;
        }
        if (bot->GetItemCount(itemId, false) >= beforeCount)
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
        s.workingBots.insert(botGuidLow);
    else
        s.workingBots.erase(botGuidLow);
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

    auto ordOpt = FindOrder(botGuidLow);
    if (!ordOpt || ordOpt->cancelled || ordOpt->state == OrderState::Done || ordOpt->state == OrderState::Mailed)
    {
        SetOrderWorking(botGuidLow, false);
        return;
    }

    OrderRecord rec = *ordOpt;
    uint32_t const inBagsBefore = bot->GetItemCount(rec.itemId, false);
    uint32_t const recipientLow = rec.giverGuidLow ? rec.giverGuidLow : tbhost::guild::GetLeaderGuid(rec.guildId).GetCounter();

    OrderStepInput in;
    in.type = rec.type;
    in.delivered = rec.delivered;
    in.want = rec.amount;
    in.inBags = inBagsBefore;
    in.canCraft = GatherCraft().CanCraft(botGuidLow, rec.itemId);
    in.canGather = GatherCraft().CanGather(botGuidLow, rec.itemId);
    in.canBuyAh = AuctionBuy().CanBuyOnAuction(botGuidLow, rec.itemId, rec.hasGoldCap ? rec.goldCap : 0);
    in.canDeliverBank = Delivery().CanDeliverToBank(botGuidLow, rec.guildId);
    in.canDeliverMail = Delivery().CanDeliverByMail(botGuidLow, recipientLow);

    OrderStepDecision const dec = DecideOrderStep(in);
    std::string reason;

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
            bool const ok = Delivery().Deliver(botGuidLow, rec.guildId, recipientLow, rec.itemId, rec.amount - rec.delivered, reason);
            if (!ok)
            {
                if (reason == "cannot: travel")
                {
                    rec.state = OrderState::Cant;
                    rec.reason = OrderReason::NoBanker;
                    GiveOrder(rec);
                    SetOrderWorking(botGuidLow, false);
                    TC_LOG_INFO("tortoisebots", "[W9] refuse id={} bot={} item={} have={} want={} reason={}",
                        rec.itemId, bot->GetName(), rec.itemId, dec.have, rec.amount, reason);
                }
                else if (reason != "walking")
                {
                    TC_LOG_INFO("tortoisebots", "[W9] deliver id={} bot={} item={} have={} want={} mode=none result={}",
                        rec.itemId, bot->GetName(), rec.itemId, dec.have, rec.amount, reason);
                }
                return;
            }
            uint32_t const inBagsAfter = bot->GetItemCount(rec.itemId, false);
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
            bool const ok = GatherCraft().StepGatherOrCraft(botGuidLow, rec.itemId, dec.remaining, reason);
            uint32_t const inBagsAfter = bot->GetItemCount(rec.itemId, false);
            uint32_t const haveAfter = rec.delivered + inBagsAfter;
            if (ok)
            {
                rec.reportedHave = haveAfter;
                GiveOrder(rec);
            }
            TC_LOG_INFO("tortoisebots", "[W9] step id={} bot={} item={} have={} want={} action={} result={}",
                rec.itemId, bot->GetName(), rec.itemId, haveAfter, rec.amount,
                dec.action == OrderStepAction::Craft ? "craft" : "gather",
                ok ? reason : (reason.empty() ? "fail" : reason));
            return;
        }

        case OrderStepAction::BuyAh:
        {
            bool const ok = AuctionBuy().StepBuyOnAuction(botGuidLow, rec.itemId, dec.remaining, rec.hasGoldCap ? rec.goldCap : 0, reason);
            uint32_t const inBagsAfter = bot->GetItemCount(rec.itemId, false);
            uint32_t const haveAfter = rec.delivered + inBagsAfter;
            TC_LOG_INFO("tortoisebots", "[W9] step id={} bot={} item={} have={} want={} action=buy result={}",
                rec.itemId, bot->GetName(), rec.itemId, haveAfter, rec.amount,
                ok ? reason : (reason.empty() ? "fail" : reason));
            return;
        }

        case OrderStepAction::CannotTravel:
        default:
            rec.state = OrderState::Cant;
            rec.reason = OrderReason::NoSource;
            GiveOrder(rec);
            SetOrderWorking(botGuidLow, false);
            TC_LOG_INFO("tortoisebots", "[W9] refuse id={} bot={} item={} have={} want={} reason=cannot:%20travel",
                rec.itemId, bot->GetName(), rec.itemId, dec.have, rec.amount);
            return;
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
        return Fail(handler, words, "guildorder", "usage", "use: create|list|cancel|status|work");

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
        if (sender && guildId && !tbhost::guild::CanEditOfficerNotes(guildId, sender->GetGUID()))
            return Fail(handler, words, "guildorder", "no-rank-right", "requires edit officer notes");
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
        if (sender && guildId && !tbhost::guild::CanEditOfficerNotes(guildId, sender->GetGUID()))
            return Fail(handler, words, "guildorder", "no-rank-right", "requires edit officer notes");

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
        uint32_t const have = ord->delivered + bot->GetItemCount(ord->itemId, false);
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

    return Fail(handler, words, "guildorder", "usage", "use: create|list|cancel|status|work");
}

} // namespace h3
} // namespace TortoiseBots
