#include "host/TbHostAuction.h"

#include "AuctionHouseBotVanilla.h"
#include "AuctionHouseMgr.h"
#include "AuctionHousePackets.h"
#include "CellImpl.h"
#include "Config.h"
#include "Creature.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "Log.h"
#include "Map.h"
#include "ObjectMgr.h"
#include "Opcodes.h"
#include "Player.h"
#include "StringConvert.h"
#include "WorldPacket.h"
#include "WorldSession.h"

#include <algorithm>
#include <cctype>
#include <unordered_map>

namespace tbhost
{
namespace ah
{
namespace
{

constexpr uint32_t kNeutralAuctionHouseId = 7;

struct PendingPostRecord
{
    uint32_t itemId = 0;
    uint32_t count = 0;
    uint64_t price = 0;
    uint64_t deposit = 0;
};

struct BotAhTracker
{
    std::vector<PendingPostRecord> pendingPosts;
    uint32_t confirmedPosted = 0;
};

std::unordered_map<ObjectGuid, BotAhTracker>& Trackers()
{
    static std::unordered_map<ObjectGuid, BotAhTracker> map;
    return map;
}

void LogW8(char const* verb, Player const* bot, uint32_t itemId, uint32_t count,
    uint64_t price, uint64_t deposit, std::string const& result)
{
    TC_LOG_INFO("tortoisebots",
        "[W8] {} bot={} item={} count={} price={} deposit={} result={}",
        verb,
        bot ? bot->GetName() : "?",
        itemId,
        count,
        price,
        deposit,
        result);
}

std::string LowerAscii(std::string s)
{
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

AuctionHouseObject* FactionHouseOf(Creature const* auctioneer)
{
    if (!auctioneer || !IsFactionAuctionHouse(auctioneer))
        return nullptr;
    return sAuctionMgr->GetAuctionsMap(auctioneer->GetFaction());
}

void AppendItemsFromResult(Player const* bot, AuctionHouseObject* house,
    WorldPackets::AuctionHouse::AuctionListItemsResult const& result,
    std::vector<AhListingView>& out)
{
    ObjectGuid const botGuid = bot->GetGUID();
    ObjectGuid const botAccGuid = bot->GetSession() ? bot->GetSession()->GetAccountGUID() : ObjectGuid::Empty;

    for (WorldPackets::AuctionHouse::AuctionItem const& ai : result.Items)
    {
        if (!ai.Item)
            continue;
        AhListingView v;
        v.auctionId = static_cast<uint32_t>(ai.AuctionID);
        v.itemId = ai.Item->ItemID;
        v.count = static_cast<uint32_t>(std::max<int32_t>(0, ai.Count));
        if (ai.UnitPrice)
        {
            v.isCommodity = true;
            v.unitPrice = *ai.UnitPrice;
            v.buyoutPrice = v.unitPrice * v.count;
        }
        else
        {
            v.isCommodity = false;
            v.buyoutPrice = ai.BuyoutPrice.value_or(0);
            v.unitPrice = (v.count > 0) ? (v.buyoutPrice / v.count) : v.buyoutPrice;
        }
        v.minBid = ai.MinBid.value_or(0);
        v.isOwn = (ai.Owner == botGuid) || (!botAccGuid.IsEmpty() && ai.OwnerAccountID == botAccGuid);
        if (house)
        {
            if (AuctionPosting* posting = house->GetAuction(v.auctionId))
                v.isCommodity = posting->IsCommodity();
        }
        out.push_back(v);
    }
}

} // namespace

bool IsWorldAuctionEnabled()
{
    return sConfigMgr->GetBoolDefault("TortoiseBots.World.Auction", false);
}

bool IsFactionAuctionHouse(Creature const* auctioneer)
{
    if (!auctioneer)
        return false;
    uint32 houseId = 0;
    AuctionHouseEntry const* entry = AuctionHouseMgr::GetAuctionHouseEntry(auctioneer->GetFaction(), &houseId);
    return entry != nullptr && houseId != kNeutralAuctionHouseId;
}

Creature* NpcIfCanInteract(Player* bot, ObjectGuid const& guid)
{
    if (!bot || guid.IsEmpty())
        return nullptr;
    Creature* creature = bot->GetNPCIfCanInteractWith(guid, UNIT_NPC_FLAG_AUCTIONEER, UNIT_NPC_FLAG_2_NONE);
    if (!creature || !IsFactionAuctionHouse(creature))
        return nullptr;
    return creature;
}

Creature* FindNearestFactionAuctioneer(Player* bot, float maxDistYd)
{
    if (!bot || !bot->IsInWorld() || !bot->FindMap())
        return nullptr;

    std::list<Creature*> creatures;
    Trinity::AllCreaturesOfEntryInRange check(bot, 0, maxDistYd);
    Trinity::CreatureListSearcher<Trinity::AllCreaturesOfEntryInRange> searcher(bot, creatures, check);
    Cell::VisitGridObjects(bot, searcher, maxDistYd);

    Creature* best = nullptr;
    float bestDist = maxDistYd + 1.0f;
    uint32 const botZone = bot->GetZoneId();
    for (Creature* c : creatures)
    {
        if (!c || !c->IsAlive() || !c->HasNpcFlag(UNIT_NPC_FLAG_AUCTIONEER))
            continue;
        if (!IsFactionAuctionHouse(c))
            continue;
        if (c->GetZoneId() != botZone)
            continue;
        float const d = bot->GetDistance(c);
        if (d <= maxDistYd && d < bestDist)
        {
            bestDist = d;
            best = c;
        }
    }
    return best;
}

uint64_t UnitValue(ItemTemplate const* proto)
{
    if (!proto)
        return 0;
    return AuctionBotVanilla::UnitValue(proto);
}

uint64_t GetDeposit(Player const* bot, Creature const* auctioneer, Item const* item, uint32_t count,
    uint32_t runTimeMinutes, bool isCommodity)
{
    if (!bot || !auctioneer || !item)
        return 0;
    uint32 houseId = 0;
    AuctionHouseEntry const* entry = AuctionHouseMgr::GetAuctionHouseEntry(auctioneer->GetFaction(), &houseId);
    if (!entry || houseId == kNeutralAuctionHouseId)
        return 0;
    if (isCommodity)
        return AuctionHouseMgr::GetCommodityAuctionDeposit(item->GetTemplate(), Minutes(runTimeMinutes), count, entry->DepositRate);
    return AuctionHouseMgr::GetItemAuctionDeposit(bot, item, Minutes(runTimeMinutes), entry->DepositRate);
}

std::vector<AhListingView> QueryMarketByItemId(Player* bot, Creature const* auctioneer, uint32_t itemId)
{
    std::vector<AhListingView> out;
    if (!bot || !bot->GetSession() || !itemId)
        return out;
    AuctionHouseObject* house = FactionHouseOf(auctioneer);
    if (!house)
        return out;

    WorldPackets::AuctionHouse::AuctionListItemsResult result;
    house->BuildListAuctionItems(result, bot, itemId, 0, {});
    AppendItemsFromResult(bot, house, result, out);
    return out;
}

std::vector<AhListingView> QueryMarketByName(Player* bot, Creature const* auctioneer, std::string const& nameSubstr)
{
    std::vector<AhListingView> out;
    if (!bot || !bot->GetSession())
        return out;
    AuctionHouseObject* house = FactionHouseOf(auctioneer);
    if (!house)
        return out;

    std::wstring wname;
    if (!Utf8toWStr(nameSubstr, wname))
        wname.clear();

    WorldPackets::AuctionHouse::AuctionListBucketsResult bucketsResult;
    house->BuildListBuckets(bucketsResult, bot, wname, 0, MAX_LEVEL, AuctionHouseFilterMask::None,
        std::nullopt, {}, 0, 0, {});

    std::string const lowerSub = LowerAscii(nameSubstr);
    size_t const maxBuckets = std::min<size_t>(bucketsResult.Buckets.size(), 50);
    for (size_t i = 0; i < maxBuckets; ++i)
    {
        uint32_t const itemId = bucketsResult.Buckets[i].Key.ItemID;
        if (!lowerSub.empty())
        {
            ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId);
            if (!proto)
                continue;
            std::string const itemName = LowerAscii(proto->GetDefaultLocaleName());
            if (itemName.find(lowerSub) == std::string::npos)
                continue;
        }
        WorldPackets::AuctionHouse::AuctionListItemsResult itemsResult;
        house->BuildListAuctionItems(itemsResult, bot, itemId, 0, {});
        AppendItemsFromResult(bot, house, itemsResult, out);
    }
    return out;
}

std::vector<AhOwnedView> Owned(Player* bot, Creature const* auctioneer, uint32_t* outSoldCount)
{
    std::vector<AhOwnedView> out;
    if (outSoldCount)
        *outSoldCount = 0;
    if (!bot || !bot->GetSession())
        return out;

    AuctionHouseObject* house = auctioneer ? FactionHouseOf(auctioneer) : sAuctionMgr->GetAuctionsMap(bot->GetFaction());
    if (!house)
        return out;

    WorldPackets::AuctionHouse::AuctionListOwnedItemsResult result;
    house->BuildListOwnedItems(result, bot, 0, {});
    if (outSoldCount)
        *outSoldCount = static_cast<uint32_t>(result.SoldItems.size());

    for (WorldPackets::AuctionHouse::AuctionItem const& ai : result.Items)
    {
        if (!ai.Item)
            continue;
        AhOwnedView v;
        v.auctionId = static_cast<uint32_t>(ai.AuctionID);
        v.itemId = ai.Item->ItemID;
        v.count = static_cast<uint32_t>(std::max<int32_t>(0, ai.Count));
        v.isCommodity = ai.UnitPrice.has_value();
        v.buyoutOrUnitPrice = ai.UnitPrice ? *ai.UnitPrice : ai.BuyoutPrice.value_or(0);
        v.hasBidder = ai.Bidder.has_value();
        out.push_back(v);
    }
    return out;
}

bool PostItem(Player* bot, Creature* auctioneer, Item* item, uint64_t minBid, uint64_t buyoutPrice,
    uint32_t runTimeMinutes, std::string* failReason)
{
    auto fail = [&](char const* reason)
    {
        if (failReason)
            *failReason = reason;
        LogW8("post", bot, item ? item->GetEntry() : 0, item ? item->GetCount() : 0, buyoutPrice, 0,
            std::string("refused:") + reason);
        return false;
    };

    if (!bot || !bot->GetSession() || !item)
        return fail("invalid");
    if (!NpcIfCanInteract(bot, auctioneer ? auctioneer->GetGUID() : ObjectGuid::Empty))
        return fail("no-auctioneer");
    if (item->GetTemplate()->GetMaxStackSize() > 1)
        return fail("stackable-must-use-commodity");

    uint32_t const itemId = item->GetEntry();
    uint64_t const deposit = GetDeposit(bot, auctioneer, item, 1, runTimeMinutes, false);
    ObjectGuid const itemGuid = item->GetGUID();
    size_t const pendingBefore = sAuctionMgr->PendingAuctionCount(bot);

    WorldPackets::AuctionHouse::AuctionSellItem pkt{ WorldPacket(CMSG_AUCTION_SELL_ITEM) };
    pkt.Auctioneer = auctioneer->GetGUID();
    pkt.MinBid = minBid;
    pkt.BuyoutPrice = buyoutPrice;
    pkt.RunTime = runTimeMinutes;
    WorldPackets::AuctionHouse::AuctionItemForSale& sale = pkt.Items.emplace_back();
    sale.Guid = itemGuid;
    sale.UseCount = 1;

    bot->GetSession()->HandleAuctionSellItem(pkt);

    // HandleAuctionSellItem removes the item from inventory and increments PendingAuctionCount before AsyncCommitTransaction.
    if (bot->GetItemByGuid(itemGuid) && sAuctionMgr->PendingAuctionCount(bot) == pendingBefore)
        return fail("handler-refused-or-throttled");

    Trackers()[bot->GetGUID()].pendingPosts.push_back({ itemId, 1, buyoutPrice, deposit });
    LogW8("post", bot, itemId, 1, buyoutPrice, deposit, "pending");
    return true;
}

bool PostCommodity(Player* bot, Creature* auctioneer, Item* item, uint32_t count, uint64_t unitPrice,
    uint32_t runTimeMinutes, std::string* failReason)
{
    auto fail = [&](char const* reason)
    {
        if (failReason)
            *failReason = reason;
        LogW8("post", bot, item ? item->GetEntry() : 0, count, unitPrice, 0,
            std::string("refused:") + reason);
        return false;
    };

    if (!bot || !bot->GetSession() || !item || count == 0)
        return fail("invalid");
    if (!NpcIfCanInteract(bot, auctioneer ? auctioneer->GetGUID() : ObjectGuid::Empty))
        return fail("no-auctioneer");
    if (item->GetTemplate()->GetMaxStackSize() <= 1)
        return fail("non-stackable-must-use-item");
    if (item->GetCount() < count)
        return fail("insufficient-count");

    uint32_t const itemId = item->GetEntry();
    uint32_t const countBefore = item->GetCount();
    uint64_t const deposit = GetDeposit(bot, auctioneer, item, count, runTimeMinutes, true);
    ObjectGuid const itemGuid = item->GetGUID();
    size_t const pendingBefore = sAuctionMgr->PendingAuctionCount(bot);

    WorldPackets::AuctionHouse::AuctionSellCommodity pkt{ WorldPacket(CMSG_AUCTION_SELL_COMMODITY) };
    pkt.Auctioneer = auctioneer->GetGUID();
    pkt.UnitPrice = unitPrice;
    pkt.RunTime = runTimeMinutes;
    WorldPackets::AuctionHouse::AuctionItemForSale& sale = pkt.Items.emplace_back();
    sale.Guid = itemGuid;
    sale.UseCount = count;

    bot->GetSession()->HandleAuctionSellCommodity(pkt);

    Item* afterItem = bot->GetItemByGuid(itemGuid);
    bool const countDropped = !afterItem || afterItem->GetCount() < countBefore;
    if (!countDropped && sAuctionMgr->PendingAuctionCount(bot) == pendingBefore)
        return fail("handler-refused-or-throttled");

    Trackers()[bot->GetGUID()].pendingPosts.push_back({ itemId, count, unitPrice, deposit });
    LogW8("post", bot, itemId, count, unitPrice, deposit, "pending");
    return true;
}

bool BuyItem(Player* bot, Creature* auctioneer, uint32_t auctionId, uint64_t buyoutPrice,
    std::string* failReason)
{
    auto fail = [&](char const* reason)
    {
        if (failReason)
            *failReason = reason;
        LogW8("buy", bot, 0, 1, buyoutPrice, 0, std::string("refused:") + reason);
        return false;
    };

    if (!bot || !bot->GetSession() || !auctionId || !buyoutPrice)
        return fail("invalid");
    if (!NpcIfCanInteract(bot, auctioneer ? auctioneer->GetGUID() : ObjectGuid::Empty))
        return fail("no-auctioneer");

    AuctionHouseObject* house = FactionHouseOf(auctioneer);
    AuctionPosting* posting = house ? house->GetAuction(auctionId) : nullptr;
    if (!posting)
        return fail("not-found");

    uint32_t const itemId = posting->Items.empty() ? 0 : posting->Items.front()->GetEntry();
    uint32_t const count = posting->GetTotalItemCount();
    uint64_t const moneyBefore = bot->GetMoney();

    WorldPackets::AuctionHouse::AuctionPlaceBid pkt{ WorldPacket(CMSG_AUCTION_PLACE_BID) };
    pkt.Auctioneer = auctioneer->GetGUID();
    pkt.AuctionID = static_cast<int32_t>(auctionId);
    pkt.BidAmount = buyoutPrice;

    bot->GetSession()->HandleAuctionPlaceBid(pkt);

    if (bot->GetMoney() >= moneyBefore)
        return fail("handler-refused-or-throttled");

    LogW8("buy", bot, itemId, count, buyoutPrice, 0, "ok");
    return true;
}

bool BuyCommodity(Player* bot, Creature* auctioneer, uint32_t itemId, uint32_t quantity,
    std::string* failReason)
{
    auto fail = [&](char const* reason)
    {
        if (failReason)
            *failReason = reason;
        LogW8("buy", bot, itemId, quantity, 0, 0, std::string("refused:") + reason);
        return false;
    };

    if (!bot || !bot->GetSession() || !itemId || !quantity)
        return fail("invalid");
    if (!NpcIfCanInteract(bot, auctioneer ? auctioneer->GetGUID() : ObjectGuid::Empty))
        return fail("no-auctioneer");

    uint64_t const moneyBefore = bot->GetMoney();

    WorldPackets::AuctionHouse::AuctionGetCommodityQuote quotePkt{ WorldPacket(CMSG_AUCTION_GET_COMMODITY_QUOTE) };
    quotePkt.Auctioneer = auctioneer->GetGUID();
    quotePkt.ItemID = static_cast<int32_t>(itemId);
    quotePkt.Quantity = quantity;
    bot->GetSession()->HandleAuctionGetCommodityQuote(quotePkt);

    WorldPackets::AuctionHouse::AuctionConfirmCommoditiesPurchase confirmPkt{ WorldPacket(CMSG_AUCTION_CONFIRM_COMMODITIES_PURCHASE) };
    confirmPkt.Auctioneer = auctioneer->GetGUID();
    confirmPkt.ItemID = static_cast<int32_t>(itemId);
    confirmPkt.Quantity = quantity;
    bot->GetSession()->HandleAuctionConfirmCommoditiesPurchase(confirmPkt);

    if (bot->GetMoney() >= moneyBefore)
        return fail("quote-or-confirm-refused");

    uint64_t const spent = moneyBefore - bot->GetMoney();
    LogW8("buy", bot, itemId, quantity, spent, 0, "ok");
    return true;
}

bool Cancel(Player* bot, Creature* auctioneer, uint32_t auctionId, uint32_t itemId,
    std::string* failReason)
{
    auto fail = [&](char const* reason)
    {
        if (failReason)
            *failReason = reason;
        LogW8("cancel", bot, itemId, 0, 0, 0, std::string("refused:") + reason);
        return false;
    };

    if (!bot || !bot->GetSession() || !auctionId)
        return fail("invalid");
    if (!NpcIfCanInteract(bot, auctioneer ? auctioneer->GetGUID() : ObjectGuid::Empty))
        return fail("no-auctioneer");

    AuctionHouseObject* house = FactionHouseOf(auctioneer);
    if (!house || !house->GetAuction(auctionId))
        return fail("not-found");

    WorldPackets::AuctionHouse::AuctionRemoveItem pkt{ WorldPacket(CMSG_AUCTION_REMOVE_ITEM) };
    pkt.Auctioneer = auctioneer->GetGUID();
    pkt.AuctionID = static_cast<int32_t>(auctionId);
    pkt.ItemID = static_cast<int32_t>(itemId);
    bot->GetSession()->HandleAuctionRemoveItem(pkt);

    if (house->GetAuction(auctionId))
        return fail("handler-refused-or-throttled");

    LogW8("cancel", bot, itemId, 0, 0, 0, "ok");
    return true;
}

void ConfirmPendingPosts(Player* bot)
{
    if (!bot || !bot->GetSession())
        return;

    auto it = Trackers().find(bot->GetGUID());
    if (it == Trackers().end() || it->second.pendingPosts.empty())
        return;

    std::vector<AhOwnedView> const owned = Owned(bot, nullptr, nullptr);
    auto& pending = it->second.pendingPosts;
    for (auto pit = pending.begin(); pit != pending.end();)
    {
        bool const found = std::any_of(owned.begin(), owned.end(), [&](AhOwnedView const& o)
        {
            return o.itemId == pit->itemId;
        });
        if (found)
        {
            ++it->second.confirmedPosted;
            LogW8("post", bot, pit->itemId, pit->count, pit->price, pit->deposit, "ok");
            pit = pending.erase(pit);
        }
        else
        {
            ++pit;
        }
    }
}

void GetBotStateCounts(Player* bot, uint32_t& outPosted, uint32_t& outSold)
{
    outPosted = 0;
    outSold = 0;
    if (!bot || !bot->GetSession())
        return;

    ConfirmPendingPosts(bot);
    std::vector<AhOwnedView> const owned = Owned(bot, nullptr, &outSold);
    outPosted = static_cast<uint32_t>(owned.size());
}

} // namespace ah
} // namespace tbhost
