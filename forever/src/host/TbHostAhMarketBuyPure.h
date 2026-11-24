#pragma once

// Pure rules for the automatic auction-house buyer scan. No core includes:
// forever/tools/test_ah_market_buy_policy.cpp builds this header on its own.

#include <algorithm>
#include <cstdint>

namespace tbhost
{
namespace ah_market
{

// TbHostAuction::QueryMarketByName keeps the first 50 buckets of the house page.
inline constexpr std::uint32_t kMarketPageBuckets = 50;
// The bot's own "item usage" verdict is the expensive probe; cap how many a page may ask.
inline constexpr std::uint32_t kBuyCandidatesPerPage = 8;

// Values mirror ItemTemplate.h's enums on ForeverCore (ItemClass, InventoryType, ItemQuality).
inline constexpr std::uint32_t kItemClassWeapon = 2;
inline constexpr std::uint32_t kItemClassArmor = 4;
inline constexpr std::uint32_t kInventoryNonEquip = 0;
inline constexpr std::uint32_t kQualityUncommon = 2;

// AiPlayerbot.AhMarketChanceBuy: one percentage roll per market pass, as TB rolls it before entering a buy scan.
inline bool BuyChancePasses(std::uint32_t chancePercent, std::uint32_t roll0to99)
{
    return chancePercent >= 100 || roll0to99 < chancePercent;
}

struct BuyCandidateInput
{
    std::uint32_t itemClass = 0;
    std::uint32_t inventoryType = 0;
    std::uint32_t quality = 0;
    std::uint32_t requiredLevel = 0;
    std::uint32_t botLevel = 1;
    bool canUse = false;
    bool hasNonOwnListing = false; // a listing of this item not owned by the bot or its account
    std::uint64_t unitPrice = 0;   // cheapest such listing
    std::uint64_t maxUnitPrice = 0; // ai::AhComputeMaxUnitPrice for the item
    std::uint64_t maxSpendPerBot = 0; // AiPlayerbot.AhMarketMaxSpendPerBot, 0 = no cap
};

// Only an affordable, usable, uncommon-or-better weapon or armour piece is worth a probe and a bid.
inline bool IsBuyCandidate(BuyCandidateInput const& in)
{
    if (!in.hasNonOwnListing || !in.canUse)
        return false;
    if (in.inventoryType == kInventoryNonEquip)
        return false;
    if (in.itemClass != kItemClassWeapon && in.itemClass != kItemClassArmor)
        return false;
    if (in.quality < kQualityUncommon)
        return false;
    if (in.requiredLevel > in.botLevel)
        return false;
    if (in.unitPrice == 0 || in.unitPrice > in.maxUnitPrice)
        return false;
    if (in.maxSpendPerBot > 0 && in.unitPrice > in.maxSpendPerBot)
        return false;
    return true;
}

// Buckets are paged by offset; an empty page means the scan ran past the end and starts over.
inline std::uint32_t NextPageOffset(std::uint32_t offset, bool pageEmpty)
{
    return pageEmpty ? 0 : offset + kMarketPageBuckets;
}

} // namespace ah_market
} // namespace tbhost
