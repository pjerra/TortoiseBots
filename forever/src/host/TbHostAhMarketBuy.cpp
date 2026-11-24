// Buyer half of the automatic auction-house market: on a passed AiPlayerbot.AhMarketChanceBuy roll, a free bot
// already standing at a faction auctioneer pages the house, asks its own item-usage value about the affordable
// weapon/armour candidates and buys a real upgrade through the existing `ah bid` action. It never teleports, walks
// or creates an item; own and same-account listings are skipped.
#include "host/TbHostAhMarketBuy.h"
#include "host/TbHostAhMarketBuyPure.h"

#include "TbBotMgr.h"
#include "host/TbEngineBrain.h"
#include "host/TbHostAuction.h"
#include "host/TbHostCapitalErrands.h"
#include "host/TbHostGuildMail.h"
#include "host/TbHostGuildOrderDisenchant.h"
#include "host/TbHostGuildOrderEnchant.h"
#include "host/TbHostGuildOrders.h"
#include "host/TbHostProfession.h"

#include "compat/botpch_forever.h"
#include "slice/SliceLog.h"

#include "Creature.h"
#include "ItemTemplate.h"
#include "Log.h"
#include "Map.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "Random.h"
#include "SharedDefines.h"

#include "playerbot/PlayerbotAI.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/policy/ah/AhBuyPolicy.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/strategy/Value.h"
#include "playerbot/strategy/values/ItemUsageValue.h"
#include "runtime/PlayerbotAIStorage.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
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

struct BuyerCycle
{
    std::uint32_t pageOffset = 0;
    std::uint32_t nextBuyer = 0;
};

BuyerCycle& Cycle()
{
    static BuyerCycle cycle;
    return cycle;
}

struct PageItem
{
    std::uint32_t itemId = 0;
    std::uint64_t cheapest = 0;
};

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

// The organic model: a bot that reached the auctioneer on its own feet (an AH errand, the seller sweep, or standing
// in its capital). No walking and no teleport here.
bool AtFactionAuctioneer(Player* bot, Creature*& auctioneer)
{
    auctioneer = ah::FindNearestFactionAuctioneer(bot, INTERACTION_DISTANCE);
    return auctioneer && bot->GetDistance(auctioneer) <= INTERACTION_DISTANCE;
}

bool IsBuyerCandidate(Player* bot, Creature*& auctioneer)
{
    if (!bot->IsInWorld() || !bot->IsAlive() || bot->IsInCombat() || bot->duel != nullptr || bot->isMoving())
        return false;
    if (!TortoiseBots::BotMgr::Instance().IsFreeBot(bot->GetGUID()))
        return false;
    if (bot->GetGroup() != nullptr || !OpenWorldMap(bot) || BusyElsewhere(bot))
        return false;
    return AtFactionAuctioneer(bot, auctioneer);
}

bool FindBuyer(Player*& buyer, Creature*& auctioneer)
{
    std::vector<Player*> candidates;
    std::vector<Creature*> houses;
    for (ObjectGuid const& guid : TortoiseBots::BotMgr::Instance().BotGuids())
    {
        Player* bot = TortoiseBots::BotMgr::Instance().FindBot(guid);
        if (!bot || bot->IsBeingTeleported())
            continue;
        Creature* house = nullptr;
        if (!IsBuyerCandidate(bot, house))
            continue;
        candidates.push_back(bot);
        houses.push_back(house);
    }
    if (candidates.empty())
        return false;

    std::uint32_t const start = Cycle().nextBuyer % static_cast<std::uint32_t>(candidates.size());
    Cycle().nextBuyer = (start + 1) % static_cast<std::uint32_t>(candidates.size());
    buyer = candidates[start];
    auctioneer = houses[start];
    return true;
}

// One cheapest non-own single-item price per item id, in the page's order (buckets are ordered by item id).
std::vector<PageItem> GroupPage(std::vector<ah::AhListingView> const& page)
{
    std::vector<PageItem> items;
    std::unordered_map<std::uint32_t, std::size_t> index;
    for (ah::AhListingView const& listing : page)
    {
        if (listing.itemId == 0 || listing.isCommodity || listing.buyoutPrice == 0)
            continue;
        if (listing.isOwn) // own or same-account, per TbHostAuction's listing view
            continue;
        auto inserted = index.emplace(listing.itemId, items.size());
        if (inserted.second)
            items.push_back({ listing.itemId, listing.buyoutPrice });
        else
            items[inserted.first->second].cheapest = std::min(items[inserted.first->second].cheapest, listing.buyoutPrice);
    }
    return items;
}

bool IsUpgradeFor(Player* buyer, std::uint32_t itemId)
{
    PlayerbotAI* pai = PlayerbotAIStorage::Instance().GetAI(buyer);
    if (!pai || !pai->GetAiObjectContext())
        return false;
    ai::Value<ai::ItemUsage>* value = pai->GetAiObjectContext()->GetValue<ai::ItemUsage>(
        "item usage", ai::ItemQualifier(itemId).GetQualifier());
    return value && value->Get() == ai::ItemUsage::ITEM_USAGE_EQUIP;
}

} // namespace

void RunBuyStep()
{
    if (!sPlayerbotAIConfig.ahMarketBuyer)
        return;

    std::uint32_t const chance = std::min<std::uint32_t>(sPlayerbotAIConfig.ahMarketChanceBuy, 100);
    std::uint32_t const roll = urand(0, 99);
    if (!BuyChancePasses(chance, roll))
    {
        LogInfo(std::string("[W8] ah-market-buy chance=") + std::to_string(roll) + "/" +
            std::to_string(chance) + " result=skipped");
        return;
    }

    Player* buyer = nullptr;
    Creature* auctioneer = nullptr;
    if (!FindBuyer(buyer, auctioneer))
    {
        LogInfo(std::string("[W8] ah-market-buy chance=") + std::to_string(roll) + "/" +
            std::to_string(chance) + " buyer=none page=0 scanned=0 bought=0");
        return;
    }

    std::uint32_t const offset = Cycle().pageOffset;
    std::vector<ah::AhListingView> const page = ah::QueryMarketByName(buyer, auctioneer, "", offset);
    Cycle().pageOffset = NextPageOffset(offset, page.empty());
    std::vector<PageItem> const items = GroupPage(page);

    std::uint32_t const maxOperations = std::clamp<std::uint32_t>(sPlayerbotAIConfig.ahMarketMaxOperations, 1, 256);
    std::uint64_t const budgetUs = std::clamp<std::uint32_t>(sPlayerbotAIConfig.ahMarketBudgetUs, 100, 20000);
    auto const started = std::chrono::steady_clock::now();

    std::uint32_t scanned = 0;
    std::uint32_t bought = 0;
    std::uint64_t spent = 0;

    for (PageItem const& item : items)
    {
        if (scanned >= maxOperations || scanned >= kBuyCandidatesPerPage)
            break;
        if (scanned > 0)
        {
            auto const elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - started).count();
            if (elapsedUs >= static_cast<std::int64_t>(budgetUs))
                break;
        }
        ++scanned;

        ItemTemplate const* proto = sObjectMgr.GetItemTemplate(item.itemId);
        if (!proto)
            continue;

        BuyCandidateInput in;
        in.itemClass = proto->GetClass();
        in.inventoryType = static_cast<std::uint32_t>(proto->GetInventoryType());
        in.quality = proto->GetQuality();
        in.requiredLevel = static_cast<std::uint32_t>(std::max<std::int32_t>(0, proto->GetBaseRequiredLevel()));
        in.botLevel = buyer->GetLevel();
        in.canUse = buyer->CanUseItem(proto) == EQUIP_ERR_OK;
        in.hasNonOwnListing = true;
        in.unitPrice = item.cheapest;
        in.maxUnitPrice = ai::AhComputeMaxUnitPrice(ah::UnitValue(proto), proto->GetBuyPrice(), proto->GetSellPrice());
        in.maxSpendPerBot = sPlayerbotAIConfig.ahMarketMaxSpendPerBot;
        if (!IsBuyCandidate(in))
            continue;
        if (buyer->GetMoney() < item.cheapest)
            continue;
        if (!IsUpgradeFor(buyer, item.itemId))
            continue;

        std::uint64_t const moneyBefore = buyer->GetMoney();
        std::string const itemParam = std::to_string(item.itemId);
        if (!TortoiseBots::engine::RunAction(buyer, "ah bid", itemParam.c_str()))
            continue;

        ++bought;
        if (moneyBefore > buyer->GetMoney())
            spent += moneyBefore - buyer->GetMoney();
        break;
    }

    LogInfo(std::string("[W8] ah-market-buy bot=") + buyer->GetName() + " chance=" + std::to_string(roll) +
        "/" + std::to_string(chance) + " page=" + std::to_string(offset) +
        " items=" + std::to_string(items.size()) + " scanned=" + std::to_string(scanned) +
        " bought=" + std::to_string(bought) + " spent=" + std::to_string(spent));
}

} // namespace ah_market
} // namespace tbhost
