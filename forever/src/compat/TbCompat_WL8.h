// ForeverCore side of the both-core helpers for W8 (auction house).
// Turtle side: ai/cmangos-compat-wl8.h.
#pragma once

#include "host/TbHostAuction.h"
#include "playerbot/RandomItemMgr.h"
#include "playerbot/policy/ah/AhBuyPolicy.h"
#include "playerbot/policy/ah/AhSellPolicy.h"
#include "runtime/policy/ah/AhSellPolicy.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

inline Creature* AuctioneerIfCanInteractCompat(Player* bot, ObjectGuid const& guid)
{
    return tbhost::ah::NpcIfCanInteract(bot, guid);
}

inline uint64 AhUnitValueCompat(ItemTemplate const* proto)
{
    return tbhost::ah::UnitValue(proto);
}

inline std::vector<tbhost::ah::AhListingView> AhQueryMarketByItemIdCompat(Player* bot, Creature const* auctioneer, uint32 itemId)
{
    return tbhost::ah::QueryMarketByItemId(bot, auctioneer, itemId);
}

inline std::vector<tbhost::ah::AhListingView> AhQueryMarketByNameCompat(Player* bot, Creature const* auctioneer, std::string const& name)
{
    return tbhost::ah::QueryMarketByName(bot, auctioneer, name);
}

inline std::vector<tbhost::ah::AhOwnedView> AhListOwnedCompat(Player* bot, Creature const* auctioneer, uint32* outSoldCount = nullptr)
{
    return tbhost::ah::Owned(bot, auctioneer, outSoldCount);
}

inline bool AhPostFromDecisionCompat(Player* bot, Creature* auctioneer, Item* item, uint32 count,
    ai::AhSellDecision const& decision, std::string* failReason = nullptr)
{
    if (!decision.shouldPost)
        return false;
    if (decision.kind == ai::AhPostKind::Commodity)
        return tbhost::ah::PostCommodity(bot, auctioneer, item, count, decision.unitPrice, decision.runTimeMinutes, failReason);
    if (decision.kind == ai::AhPostKind::Item)
        return tbhost::ah::PostItem(bot, auctioneer, item, decision.minBid, decision.buyoutPrice, decision.runTimeMinutes, failReason);
    return false;
}

inline bool AhBuyFromDecisionCompat(Player* bot, Creature* auctioneer, tbhost::ah::AhListingView const& listing,
    ai::AhBuyDecision const& decision, std::string* failReason = nullptr)
{
    if (!decision.shouldBuy)
        return false;
    if (decision.isCommodity)
        return tbhost::ah::BuyCommodity(bot, auctioneer, listing.itemId, decision.buyQuantity, failReason);
    return tbhost::ah::BuyItem(bot, auctioneer, listing.auctionId, decision.totalPrice, failReason);
}

inline bool AhCancelCompat(Player* bot, Creature* auctioneer, uint32 auctionId, uint32 itemId, std::string* failReason = nullptr)
{
    return tbhost::ah::Cancel(bot, auctioneer, auctionId, itemId, failReason);
}
