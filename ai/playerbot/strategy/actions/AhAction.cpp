
#include "playerbot/playerbot.h"
#include "playerbot/PerformanceMonitor.h"
#include "AhAction.h"
#include "playerbot/strategy/values/ItemCountValue.h"
#include "playerbot/RandomItemMgr.h"
#include "playerbot/strategy/values/BudgetValues.h"
#include "playerbot/strategy/values/ItemUsageValue.h"
#include "playerbot/strategy/ItemVisitors.h"
#include "playerbot/TravelMgr.h"
#include "playerbot/TbCompatW3.h"
#include "playerbot/TbCompatWL8.h"

using namespace ai;

#if __has_include("TbCompat_WL8.h")

namespace
{
std::string TrimCopy(std::string s)
{
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
        s.erase(s.begin());
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
        s.pop_back();
    return s;
}

std::string LowerCopy(std::string s)
{
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

Creature* FindNearbyAuctioneer(PlayerbotAI* ai, Player* bot)
{
    std::list<ObjectGuid> npcs = ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid>>("nearest npcs")->Get();
    for (ObjectGuid const& guid : npcs)
        if (Creature* npc = AuctioneerIfCanInteractCompat(bot, guid))
            return npc;
    return nullptr;
}

uint64 LowestMarketUnitPrice(std::vector<tbhost::ah::AhListingView> const& listings)
{
    uint64 best = 0;
    for (tbhost::ah::AhListingView const& l : listings)
    {
        if (l.unitPrice == 0)
            continue;
        if (best == 0 || l.unitPrice < best)
            best = l.unitPrice;
    }
    return best;
}

bool MatchesItemQuery(Item* item, std::string const& query)
{
    if (!item)
        return false;
    std::string const q = TrimCopy(query);
    if (q.empty() || q == "vendor" || q == "all")
        return true;
    ItemPrototype const* proto = GetItemProtoCompat(item);
    if (!proto)
        return false;
    if (std::all_of(q.begin(), q.end(), [](unsigned char c) { return std::isdigit(c) != 0; }))
        return proto->ItemId == static_cast<uint32>(std::stoul(q));
    return LowerCopy(proto->Name1).find(LowerCopy(q)) != std::string::npos;
}
} // namespace

bool AhAction::Execute(Event& event)
{
    Player* requester = event.GetOwner() ? event.GetOwner() : GetMaster();
    std::string text = TrimCopy(event.GetParam());

    tbhost::ah::ConfirmPendingPosts(bot);

    Creature* auctioneer = FindNearbyAuctioneer(ai, bot);
    if (!auctioneer)
    {
        ai->TellPlayerNoFacing(requester, "Cannot find faction auctioneer nearby");
        return false;
    }

    return ExecuteCommand(requester, text, auctioneer);
}

bool AhAction::ExecuteCommand(Player* requester, std::string text, Unit* auctioneerUnit)
{
    Creature* auctioneer = auctioneerUnit ? auctioneerUnit->ToCreature() : nullptr;
    if (!auctioneer || !tbhost::ah::IsFactionAuctionHouse(auctioneer))
        return false;

    std::string const trimmed = TrimCopy(text);
    if (LowerCopy(trimmed) == "list")
    {
        uint32 soldCount = 0;
        std::vector<tbhost::ah::AhOwnedView> const owned = AhListOwnedCompat(bot, auctioneer, &soldCount);
        std::ostringstream out;
        out << "AH owned=" << owned.size() << " sold=" << soldCount;
        for (tbhost::ah::AhOwnedView const& o : owned)
            out << " [id=" << o.auctionId << " item=" << o.itemId << " x" << o.count << " price=" << o.buyoutOrUnitPrice << "]";
        ai->TellPlayerNoFacing(requester, out.str(), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
        return true;
    }

    std::string itemFilter = trimmed;
    if (LowerCopy(itemFilter) == "sell")
        itemFilter.clear();
    else if (LowerCopy(itemFilter).rfind("sell ", 0) == 0)
        itemFilter = TrimCopy(itemFilter.substr(5));

    std::list<Item*> items = AI_VALUE2(std::list<Item*>, "inventory items", "all");
    uint32 freeMoney = AI_VALUE2(uint32, "free money for", static_cast<uint32>(NeedMoneyFor::ah));
    bool postedAny = false;

    for (Item* item : items)
    {
        if (!item || !MatchesItemQuery(item, itemFilter))
            continue;

        ItemPrototype const* proto = GetItemProtoCompat(item);
        if (!proto)
            continue;

        ItemUsage const usage = AI_VALUE2(ItemUsage, "item usage", ItemQualifier(item).GetQualifier());
        bool const isUpgrade = (usage == ItemUsage::ITEM_USAGE_EQUIP || usage == ItemUsage::ITEM_USAGE_BAD_EQUIP);
        bool const isCommodity = proto->Stackable > 1;
        uint32 const count = item->GetCount();
        uint64 const deposit = tbhost::ah::GetDeposit(bot, auctioneer, item, count, kAhDefaultRunTimeMinutes, isCommodity);
        std::vector<tbhost::ah::AhListingView> const market = AhQueryMarketByItemIdCompat(bot, auctioneer, proto->ItemId);

        AhSellInput in;
        in.itemId = proto->ItemId;
        in.itemClass = proto->Class;
        in.maxStackSize = proto->Stackable;
        in.count = count;
        in.vendorSellPrice = proto->SellPrice;
        in.depositTotal = deposit;
        in.marketLowestUnitPrice = LowestMarketUnitPrice(market);
        in.unitValue = AhUnitValueCompat(GetItemTemplateCompat(proto));
        in.freeMoneyForAh = freeMoney;
        in.soulbound = item->IsSoulBound();
        in.equipped = item->IsEquipped();
        in.conjured = (proto->Flags & ITEM_FLAG_CONJURED) != 0;
        in.nonEmptyBag = item->IsNotEmptyBag();
        in.isW6aUpgrade = isUpgrade;
        in.blacklisted = AhItemSellBlacklisted(proto->ItemId, sPlayerbotAIConfig.ahSellBlacklist);

        AhSellDecision const decision = DecideAhSell(in);
        if (!decision.shouldPost)
            continue;

        std::string failReason;
        if (AhPostFromDecisionCompat(bot, auctioneer, item, count, decision, &failReason))
        {
            postedAny = true;
            if (deposit <= freeMoney)
                freeMoney -= static_cast<uint32>(deposit);
            else
                freeMoney = 0;
            if (!itemFilter.empty() && itemFilter != "vendor")
                break;
        }
    }

    return postedAny;
}

bool AhAction::PostItem(Player* /*requester*/, Item* /*item*/, uint32 /*price*/, Unit* /*auctioneer*/, uint32 /*time*/)
{
    return false;
}


bool AhBidAction::ExecuteCommand(Player* requester, std::string text, Unit* auctioneerUnit)
{
    Creature* auctioneer = auctioneerUnit ? auctioneerUnit->ToCreature() : nullptr;
    if (!auctioneer || !tbhost::ah::IsFactionAuctionHouse(auctioneer))
        return false;

    std::string query = TrimCopy(text);
    if (LowerCopy(query).rfind("buy ", 0) == 0)
        query = TrimCopy(query.substr(4));

    uint32 requestedQty = 0;
    size_t const lastSpace = query.rfind(' ');
    if (lastSpace != std::string::npos)
    {
        std::string const tail = TrimCopy(query.substr(lastSpace + 1));
        if (!tail.empty() && std::all_of(tail.begin(), tail.end(), [](unsigned char c) { return std::isdigit(c) != 0; }))
        {
            requestedQty = static_cast<uint32>(std::stoul(tail));
            query = TrimCopy(query.substr(0, lastSpace));
        }
    }

    if (query.empty())
        return false;

    std::vector<tbhost::ah::AhListingView> listings;
    if (std::all_of(query.begin(), query.end(), [](unsigned char c) { return std::isdigit(c) != 0; }))
        listings = AhQueryMarketByItemIdCompat(bot, auctioneer, static_cast<uint32>(std::stoul(query)));
    else
        listings = AhQueryMarketByNameCompat(bot, auctioneer, query);

    if (listings.empty())
        return false;

    std::sort(listings.begin(), listings.end(), [](tbhost::ah::AhListingView const& a, tbhost::ah::AhListingView const& b)
    {
        return a.unitPrice < b.unitPrice;
    });

    uint32 const budget = AI_VALUE2(uint32, "free money for", static_cast<uint32>(NeedMoneyFor::anything));

    for (tbhost::ah::AhListingView const& l : listings)
    {
        if (l.isOwn || l.unitPrice == 0)
            continue;

        AhBuyInput in;
        in.auctionId = l.auctionId;
        in.itemId = l.itemId;
        in.isCommodity = l.isCommodity;
        in.isOwnAuction = l.isOwn;
        in.buyoutOrUnitPrice = l.isCommodity ? l.unitPrice : l.buyoutPrice;
        in.isNeededMatOrExplicit = true;
        in.budgetCopper = budget;

        if (l.isCommodity)
        {
            uint32 const want = requestedQty ? requestedQty : 1;
            in.count = AhClampCommodityBuyQty(want, l.count, l.unitPrice, budget);
        }
        else
        {
            in.count = l.count ? l.count : 1;
        }

        AhBuyDecision const decision = DecideAhBuy(in);
        if (!decision.shouldBuy)
            continue;

        std::string failReason;
        if (AhBuyFromDecisionCompat(bot, auctioneer, l, decision, &failReason))
        {
            std::ostringstream out;
            out << "Bought item=" << l.itemId << " x" << decision.buyQuantity << " for " << decision.totalPrice << "c on AH";
            ai->TellPlayerNoFacing(requester, out.str(), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
            return true;
        }
    }

    return false;
}


bool AhBidAction::BidItem(Player* /*requester*/, AuctionEntry* /*auction*/, uint32 /*price*/, Unit* /*auctioneer*/, bool /*isBuyout*/, std::string /*reason*/)
{
    return false;
}


bool AhCancelAction::ExecuteCommand(Player* requester, std::string text, Unit* auctioneerUnit)
{
    Creature* auctioneer = auctioneerUnit ? auctioneerUnit->ToCreature() : nullptr;
    if (!auctioneer || !tbhost::ah::IsFactionAuctionHouse(auctioneer))
        return false;

    std::string target = TrimCopy(text);
    if (LowerCopy(target).rfind("cancel ", 0) == 0)
        target = TrimCopy(target.substr(7));

    bool const cancelAll = target.empty() || LowerCopy(target) == "all";
    uint32 targetId = 0;
    if (!cancelAll && std::all_of(target.begin(), target.end(), [](unsigned char c) { return std::isdigit(c) != 0; }))
        targetId = static_cast<uint32>(std::stoul(target));

    std::vector<tbhost::ah::AhOwnedView> const owned = AhListOwnedCompat(bot, auctioneer, nullptr);
    bool anyCancelled = false;
    for (tbhost::ah::AhOwnedView const& o : owned)
    {
        if (!cancelAll && targetId && o.auctionId != targetId)
            continue;
        if (!cancelAll && !targetId)
        {
            ItemPrototype const* proto = GetItemProtoByIdCompat(o.itemId);
            if (!proto || LowerCopy(proto->Name1).find(LowerCopy(target)) == std::string::npos)
                continue;
        }
        std::string failReason;
        if (AhCancelCompat(bot, auctioneer, o.auctionId, o.itemId, &failReason))
        {
            anyCancelled = true;
            std::ostringstream out;
            out << "Cancelled auction id=" << o.auctionId << " item=" << o.itemId;
            ai->TellPlayerNoFacing(requester, out.str(), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
            if (targetId)
                break;
        }
    }
    return anyCancelled;
}

bool AhCancelAction::CancelAuctionEntry(Player* /*requester*/, AuctionEntry* /*auction*/, Unit* /*auctioneer*/)
{
    return false;
}


#else

using namespace ai;

namespace
{
uint32 AuctionItemCount(AuctionEntry const* auction)
{
    if (!auction)
        return 0;

    Item* item = AuctionMgrW3().GetAItem(auction->itemGuidLow);
    return item ? item->GetCount() : 0;
}
}

bool AhAction::Execute(Event& event)
{
    Player* requester = event.GetOwner() ? event.GetOwner() : GetMaster();
    std::string text = event.GetParam();

    std::list<ObjectGuid> npcs = AI_VALUE(std::list<ObjectGuid>, "nearest npcs");
    for (std::list<ObjectGuid>::iterator i = npcs.begin(); i != npcs.end(); i++)
    {
        Unit* npc = bot->GetNPCIfCanInteractWith(*i, UNIT_NPC_FLAG_AUCTIONEER);
        if (!npc)
            continue;

        if (!sRandomBotFacade.m_ahActionMutex.try_lock()) //Another bot is using the Auction right now. Try again later.
            return false;

        bool doneAuction = ExecuteCommand(requester, text, npc);

        sRandomBotFacade.m_ahActionMutex.unlock();

        // Fruitless seller errand: the bot stood at the auctioneer and posted
        // nothing (deposit, price or profit check refused every item), but its
        // AH stock still raises the seller need, so it re-picked the AH every
        // ~35 s without leaving. Park the AH purpose like ParkVendorErrand.
        if (!doneAuction && text == "vendor" && event.getSource() == "rpg action" && getName() == "ah")
        {
            std::string const purposeKey = std::to_string((uint32)TravelDestinationPurpose::AH);
            SET_AI_VALUE2(bool, "no active travel destinations", purposeKey, true);
            SET_AI_VALUE2(time_t, "manual time", "no travel purpose until::" + purposeKey, time(0) + 10 * MINUTE);
            sPlayerbotAIConfig.logEvent(ai, "ErrandParked", "post items on ah", WorldPosition(bot).GetAreaName());
        }

        return doneAuction;
    }

    ai->TellPlayerNoFacing(requester, "Cannot find auctioneer nearby");
    return false;
}


bool AhAction::ExecuteCommand(Player* requester, std::string text, Unit* auctioneer)
{
    uint32 time;
    time = 8 * HOUR / MINUTE;

    if (text == "vendor")
    {
        AuctionHouseEntry const* auctionHouseEntry = bot->GetSession()->GetCheckedAuctionHouseForAuctioneer(auctioneer->getObjectGuid());
        if (!auctionHouseEntry)
            return false;

        std::list<Item*> items = AI_VALUE2(std::list<Item*>, "inventory items", "usage " + std::to_string((uint8)ItemUsage::ITEM_USAGE_AH));

        bool postedItem = false;

        std::map<uint32, uint32> pricePerItemCache;

        //resulting undercut value for reporting
        uint32 resultingUndercut = 0;
        uint32 postedItems = 0;

        for (auto item : items)
        {
            RESET_AI_VALUE2(ItemUsage, "item usage", ItemQualifier(item).GetQualifier());
            if(AI_VALUE2(ItemUsage, "item usage", ItemQualifier(item).GetQualifier()) != ItemUsage::ITEM_USAGE_AH)
                continue;

            auto pmo = sPerformanceMonitor.start(PERF_MON_VALUE, "IsMoreProfitableToSellToAHThanToVendor", ai);
            bool isMoreProfitableToSellToAHThanToVendor = ItemUsageValue::IsMoreProfitableToSellToAHThanToVendor(GetItemProtoCompat(item), bot);
            pmo.reset();

            if (!isMoreProfitableToSellToAHThanToVendor)
                continue;

            uint32 deposit = AuctionHouseMgr::GetAuctionDeposit(auctionHouseEntry, time * MINUTE, item);

            RESET_AI_VALUE2(uint32, "free money for", (uint32)NeedMoneyFor::ah);
            uint32 freeMoney = AI_VALUE2(uint32, "free money for", (uint32)NeedMoneyFor::ah);

            if (deposit > freeMoney)
                return false;

            const ItemPrototype* proto = GetItemProtoCompat(item);

            if (!pricePerItemCache[proto->ItemId])
            {
                uint32 pricePerItem = ItemUsageValue::DesiredPricePerItem(bot, proto, item->GetCount(), urand(40, 60));
                if (!pricePerItem)
                {
                    uint32 basePerItem = ItemUsageValue::GetBotSellPrice(proto, bot);
                    uint32 initialPricePercentage = urand(75, 100);
                    pricePerItem = (basePerItem * initialPricePercentage) / 100;
                }
                if (!pricePerItem)
                    pricePerItem = 1;
                pricePerItemCache[proto->ItemId] = pricePerItem;
            }

            bool didPost = PostItem(requester, item, pricePerItemCache[proto->ItemId] * item->GetCount(), auctioneer, time);

            if (didPost)
            {
                    postedItem |= true;
                    postedItems++;
            }

            if (!urand(0, 5 + (items.size()- postedItems)/10))
                break;
        }

        return postedItem;
    }

    int pos = text.find(" ");
    if (pos == std::string::npos) return false;

    std::string priceStr = text.substr(0, pos);
    uint32 price = ChatHelper::parseMoney(priceStr);

    std::list<Item*> found = ai->InventoryParseItems(text, IterateItemsMask::ITERATE_ITEMS_IN_BAGS);
    if (found.empty())
        return false;

    Item* item = *found.begin();

    return PostItem(requester, item, price, auctioneer, time);
}

bool AhAction::PostItem(Player* requester, Item* item, uint32 price, Unit* auctioneer, uint32 time)
{
    // The order protected set bypasses usage lists on explicit posts: never
    // hand over the order item, the supply item or a known-recipe reagent.
    if (item && bot->GetGuildId() && GuildOrderConsumesItem(bot, GetItemProtoCompat(item)->ItemId))
        return false;
    ObjectGuid itemGuid = item->getObjectGuid();
    ItemPrototype const* proto = GetItemProtoCompat(item);

    ItemQualifier itemQualifier(item);

    uint32 cnt = item->GetCount();

    WorldPacket packet;
    packet << auctioneer->getObjectGuid();
    packet << itemGuid;
    packet << price * 95 / 100; //bid price?
    packet << price; //buyout price?
    packet << time;

    bot->GetSession()->HandleAuctionSellItem(packet);

    if (bot->GetItemByGuid(itemGuid))
        return false;

    sPlayerbotAIConfig.logEvent(ai, "AhAction", proto->Name1, std::to_string(proto->ItemId));

    std::ostringstream out;
    out << "Posting " << ChatHelper::formatItem(itemQualifier, cnt) << " for " << ChatHelper::formatMoney(price) << " to the AH";
    ai->TellPlayerNoFacing(requester, out.str(), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
    return true;
}

bool AhBidAction::ExecuteCommand(Player* requester, std::string text, Unit* auctioneer)
{
    AuctionHouseEntry const* auctionHouseEntry = bot->GetSession()->GetCheckedAuctionHouseForAuctioneer(auctioneer->getObjectGuid());
    if (!auctionHouseEntry)
        return false;

    // always return pointer
    AuctionHouseObject* auctionHouse = AuctionMgrW3().GetAuctionsMap(auctionHouseEntry);

    if (!auctionHouse)
        return false;

    // Copy entries before scanning; live entries are resolved by id when needed.
    std::vector<AuctionEntry> map;
    for (auto const& entry : *auctionHouse->GetAuctions())
    {
        if (entry.second)
            map.push_back(*entry.second);
    }

    if (map.empty())
        return false;

    AuctionEntry* auction = nullptr;

    std::vector<std::pair<uint32, uint32>> auctionPowers;

    if (text == "vendor")
    {
        ItemUsage usage;
        auto data = WorldPacket();
        uint32 count, totalcount = 0;
        auctionHouse->BuildListBidderItems(data, bot, 9999, count, totalcount);

        if (totalcount > 10) //Already have 10 bids, stop.
            return false;

        std::unordered_map <ItemUsage, int32> freeMoney;

        freeMoney[ItemUsage::ITEM_USAGE_EQUIP] = freeMoney[ItemUsage::ITEM_USAGE_BAD_EQUIP] = (uint32)NeedMoneyFor::gear;
        freeMoney[ItemUsage::ITEM_USAGE_USE] = (uint32)NeedMoneyFor::consumables;
        freeMoney[ItemUsage::ITEM_USAGE_SKILL] = freeMoney[ItemUsage::ITEM_USAGE_DISENCHANT] =(uint32)NeedMoneyFor::tradeskill;
        freeMoney[ItemUsage::ITEM_USAGE_AMMO] = (uint32)NeedMoneyFor::ammo;
        freeMoney[ItemUsage::ITEM_USAGE_QUEST] = freeMoney[ItemUsage::ITEM_USAGE_AH] = freeMoney[ItemUsage::ITEM_USAGE_VENDOR] = freeMoney[ItemUsage::ITEM_USAGE_FORCE_NEED] = freeMoney[ItemUsage::ITEM_USAGE_FORCE_GREED] = (uint32)NeedMoneyFor::anything;

        uint32 checkNumAuctions = map.size();
        if (!sPlayerbotAIConfig.botCheckAllAuctionListings)
        {
            checkNumAuctions = urand(50, 250);
        }

        for (uint32 i = 0; i < checkNumAuctions; i++)
        {
            const uint32 auctionId = map[urand(0, map.size()-1)].Id;

            if (std::find_if(auctionPowers.begin(), auctionPowers.end(), [auctionId](std::pair<uint32, uint32> i){return i.first == auctionId;}) != auctionPowers.end())
                continue;

            auction = auctionHouse->GetAuction(auctionId);

            if (!auction)
                continue;

            if (auction->owner == bot->GetGUIDLow() || auction->ownerAccount == bot->GetSession()->GetAccountId())
                continue;

            if (!AuctionItemCount(auction))
                continue;

            if (sRandomItemMgr.IsTransmogClone(auction->itemTemplate))
                continue;

            uint32 totalCost = std::min(auction->buyout, uint32(std::max(auction->bid, auction->startbid) * frand(1.05f, 1.25f)));
            uint32 const auctionItemCount = AuctionItemCount(auction);
            if (!auctionItemCount)
                continue;

            usage = AI_VALUE2(ItemUsage, "item usage", ItemQualifier(auction).GetQualifier());

            if (freeMoney.find(usage) == freeMoney.end() || totalCost > AI_VALUE2(uint32, "free money for", freeMoney[usage]))
                continue;

            uint32 power = 1;

            switch (usage)
            {
            case ItemUsage::ITEM_USAGE_EQUIP:
            case ItemUsage::ITEM_USAGE_BAD_EQUIP:
                power = sRandomItemMgr.GetLiveStatWeight(bot, auction->itemTemplate);
                break;
            case ItemUsage::ITEM_USAGE_AH:
            {
                auto pmo = sPerformanceMonitor.start(PERF_MON_VALUE, "IsWorthBuyingFromAhToResellAtAH", ai);
                bool isWorthBuyingFromAhToResellAtAH = ItemUsageValue::IsWorthBuyingFromAhToResellAtAH(GetItemProtoByIdCompat(auction->itemTemplate), totalCost, AuctionItemCount(auction));
                pmo.reset();

                if (!isWorthBuyingFromAhToResellAtAH)
                    continue;
                power = 1000;
                break;
            }
            case ItemUsage::ITEM_USAGE_VENDOR:
                //basically if AH price is lower than vendor sell price then it's worth it
                if (totalCost / auctionItemCount >= (int32)GetItemProtoByIdCompat(auction->itemTemplate)->SellPrice)
                    continue;
                power = 1000;
                break;
            case ItemUsage::ITEM_USAGE_FORCE_NEED:
            case ItemUsage::ITEM_USAGE_FORCE_GREED:
                power = 1000;
                break;
            }

            power *= 1000;
            power /= (totalCost +1);

            auctionPowers.push_back(std::make_pair(auction->Id, power));
        }

        std::sort(auctionPowers.begin(), auctionPowers.end(), [](std::pair<uint32, uint32> i, std::pair<uint32, uint32> j) {return i > j; });

        bool bidItems = false;

        for (auto auctionPower : auctionPowers)
        {
            auction = auctionHouse->GetAuction(auctionPower.first);

            if (!auction)
                continue;

            usage = AI_VALUE2(ItemUsage, "item usage", ItemQualifier(auction).GetQualifier());

            uint32 currentBidPrice = std::max(auction->bid, auction->startbid);
            uint32 currentBuyoutPrice = auction->buyout;

            bool shouldBuyout = false;

            //determine if should look at buyout or bid price depending on item usage
            uint32 price = currentBuyoutPrice;

            if (usage == ItemUsage::ITEM_USAGE_VENDOR || usage == ItemUsage::ITEM_USAGE_FORCE_GREED || usage == ItemUsage::ITEM_USAGE_NONE)
            {
                //do not care for buyout price for items that bot does not need
                price = currentBidPrice;
            }
            else if (currentBidPrice < static_cast<uint32>(currentBuyoutPrice * 0.3f) && !urand(0,1))
            {
                //if bid price < 30% of buyout, then might as well (50/50) chance consider bid price directly
                price = currentBidPrice;
            }

            //first check if has money for buyout price (if checking against buyout price)
            if (price == currentBuyoutPrice && (freeMoney.find(usage) == freeMoney.end() || price > AI_VALUE2(uint32, "free money for", freeMoney[usage])))
            {
                //check for free money for bid price next if has no money for buyout
                price = currentBidPrice;
            }

            //check if have money for bid price (if checking against bid price)
            if (price != currentBuyoutPrice && (freeMoney.find(usage) == freeMoney.end() || price > AI_VALUE2(uint32, "free money for", freeMoney[usage])))
            {
                if (!urand(0, 5))
                    break;
                else
                    continue;
            }

            freeMoney[ItemUsage::ITEM_USAGE_EQUIP] = freeMoney[ItemUsage::ITEM_USAGE_BAD_EQUIP] = (uint32)NeedMoneyFor::gear;
            freeMoney[ItemUsage::ITEM_USAGE_USE] = (uint32)NeedMoneyFor::consumables;
            freeMoney[ItemUsage::ITEM_USAGE_SKILL] = freeMoney[ItemUsage::ITEM_USAGE_DISENCHANT] = (uint32)NeedMoneyFor::tradeskill;
            freeMoney[ItemUsage::ITEM_USAGE_AMMO] = (uint32)NeedMoneyFor::ammo;
            freeMoney[ItemUsage::ITEM_USAGE_QUEST] = freeMoney[ItemUsage::ITEM_USAGE_AH] = freeMoney[ItemUsage::ITEM_USAGE_VENDOR] = freeMoney[ItemUsage::ITEM_USAGE_FORCE_NEED] = freeMoney[ItemUsage::ITEM_USAGE_FORCE_GREED] = (uint32)NeedMoneyFor::anything;

            ItemUsage usage = AI_VALUE2(ItemUsage, "item usage", ItemQualifier(auction).GetQualifier());

            std::string reason = ItemUsageValue::ReasonForNeed(usage, auction, AuctionItemCount(auction), bot);

            bidItems = BidItem(requester, auction, price, auctioneer, price == currentBuyoutPrice, reason);

            if (bidItems)
                totalcount++;

            if (!urand(0, 5) || totalcount > 10)
                break;

            RESET_AI_VALUE2(uint32, "free money for", freeMoney[usage]);
        }

        return bidItems;
    }

    int pos = text.find(" ");
    if (pos == std::string::npos) return false;

    std::string priceStr = text.substr(0, pos);
    uint32 price = ChatHelper::parseMoney(priceStr);

    for (auto curAuction : map)
    {
        if (curAuction.owner == bot->GetGUIDLow() || curAuction.ownerAccount == bot->GetSession()->GetAccountId())
            continue;

        ItemPrototype const* proto = GetItemProtoByIdCompat(curAuction.itemTemplate);

        if (!proto)
            continue;

        if(proto->Name1.empty())
            continue;

        if (!strstri(proto->Name1, text.c_str()))
            continue;

        // A clone carries the base item's name, so the search can match one.
        if (sRandomItemMgr.IsTransmogClone(proto))
            continue;

        if (price && curAuction.bid + 5 > price)
            continue;

        uint32 cost = std::min(curAuction.buyout, uint32(std::max(curAuction.bid, curAuction.startbid) * frand(1.05f, 1.25f)));

        if (!cost)
            continue;

        uint32 power = AuctionItemCount(&curAuction);
        power *= 1000;
        power /= cost;

        auctionPowers.push_back(std::make_pair(curAuction.Id, power));
    }

    if (auctionPowers.empty())
        return false;

    std::sort(auctionPowers.begin(), auctionPowers.end(), [](std::pair<uint32, uint32> i, std::pair<uint32, uint32> j) {return i > j; });

    auction = auctionHouse->GetAuction(auctionPowers.begin()->first);

    if (!auction)
        return false;

    uint32 cost = std::min(auction->buyout, uint32(std::max(auction->bid, auction->startbid) * frand(1.05f, 1.25f)));

    return BidItem(requester, auction, cost, auctioneer, cost == auction->buyout);
}

bool AhBidAction::BidItem(Player* requester, AuctionEntry* auction, uint32 price, Unit* auctioneer, bool isBuyout, std::string reason)
{
    AuctionHouseEntry const* auctionHouseEntry = bot->GetSession()->GetCheckedAuctionHouseForAuctioneer(auctioneer->getObjectGuid());
    if (!auctionHouseEntry)
        return false;

    // always return pointer
    AuctionHouseObject* auctionHouse = AuctionMgrW3().GetAuctionsMap(auctionHouseEntry);

    if (!auctionHouse)
        return false;

    auction = auctionHouse->GetAuction(auction->Id);

    if (!auction)
        return false;

    WorldPacket packet;
    packet << auctioneer->getObjectGuid();
    packet << auction->Id;
    packet << price;

    uint32 oldMoney = bot->GetMoney();
    ItemQualifier itemQualifier(auction);
    uint32 count = AuctionItemCount(auction);
    if (!count)
        return false;

    ItemPrototype const* proto = GetItemProtoByIdCompat(auction->itemTemplate);

    bot->GetSession()->HandleAuctionPlaceBid(packet);

    if (bot->GetMoney() < oldMoney)
    {
        sPlayerbotAIConfig.logEvent(ai, "AhBidAction", proto->Name1, std::to_string(proto->ItemId));
        std::ostringstream out;
        if (isBuyout)
        {
            out << "Buying out " << ChatHelper::formatItem(itemQualifier, count) << " for " << ChatHelper::formatMoney(price) << " on the AH";
        }
        else
        {
            out << "Bidding " << ChatHelper::formatMoney(price) << " on " << ChatHelper::formatItem(itemQualifier, count) << " on the AH";
        }
        if (!reason.empty())
            out << " " << reason;
        ai->TellPlayerNoFacing(requester, out.str(), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
        return true;
    }
    return false;
}


bool AhCancelAction::ExecuteCommand(Player* requester, std::string text, Unit* auctioneer)
{
    AuctionHouseEntry const* auctionHouseEntry = bot->GetSession()->GetCheckedAuctionHouseForAuctioneer(auctioneer->getObjectGuid());
    if (!auctionHouseEntry)
        return false;

    AuctionHouseObject* auctionHouse = AuctionMgrW3().GetAuctionsMap(auctionHouseEntry);
    if (!auctionHouse)
        return false;

    bool cancelAll = (text.empty() || text == "all");
    uint32 targetAuctionId = 0;
    if (!cancelAll && std::all_of(text.begin(), text.end(), ::isdigit))
    {
        targetAuctionId = std::stoul(text);
    }

    std::vector<AuctionEntry*> toCancel;
    for (auto const& pair : *auctionHouse->GetAuctions())
    {
        AuctionEntry* entry = pair.second;
        if (!entry || entry->owner != bot->GetGUIDLow())
            continue;

        if (targetAuctionId && entry->Id == targetAuctionId)
        {
            toCancel.push_back(entry);
            break;
        }

        if (cancelAll)
        {
            toCancel.push_back(entry);
            continue;
        }

        ItemPrototype const* proto = GetItemProtoByIdCompat(entry->itemTemplate);
        if (proto && !proto->Name1.empty() && strstri(proto->Name1, text.c_str()))
        {
            toCancel.push_back(entry);
        }
    }

    if (toCancel.empty())
    {
        ai->TellPlayerNoFacing(requester, "No matching auctions found to cancel.");
        return false;
    }

    bool anyCancelled = false;
    for (auto* entry : toCancel)
    {
        if (CancelAuctionEntry(requester, entry, auctioneer))
            anyCancelled = true;
    }

    return anyCancelled;
}

bool AhCancelAction::CancelAuctionEntry(Player* requester, AuctionEntry* auction, Unit* auctioneer)
{
    if (!auction || !auctioneer)
        return false;

    uint32 auctionId = auction->Id;
    uint32 itemTemplate = auction->itemTemplate;
    ItemPrototype const* proto = GetItemProtoByIdCompat(itemTemplate);

    WorldPacket packet;
    packet << auctioneer->getObjectGuid();
    packet << auctionId;

    bot->GetSession()->HandleAuctionRemoveItem(packet);

    AuctionHouseEntry const* ahEntry = bot->GetSession()->GetCheckedAuctionHouseForAuctioneer(auctioneer->getObjectGuid());
    AuctionHouseObject* ahObj = ahEntry ? AuctionMgrW3().GetAuctionsMap(ahEntry) : nullptr;
    if (ahObj && !ahObj->GetAuction(auctionId))
    {
        std::string itemName = proto ? proto->Name1 : "Item";
        std::ostringstream out;
        out << "Cancelled auction for " << itemName << " (id: " << auctionId << ", returned via mail)";
        ai->TellPlayerNoFacing(requester, out.str(), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
        return true;
    }

    return false;
}

#endif
