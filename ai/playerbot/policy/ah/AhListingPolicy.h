#pragma once

// Pure W8 auction listing reader (std-only, no core includes).
// BuildListAuctionItems sends a commodity listing without its Item field (only
// UnitPrice: the client already knows the bucket it asked for), so the item id
// has to come from another source.

#include <cstdint>

namespace ai
{
struct AhListingIdSources
{
    std::uint32_t itemFieldId = 0;    // AuctionItem::Item->ItemID (non-commodities, owned lists)
    std::uint32_t bucketKeyItemId = 0; // AuctionItem::AuctionBucketKey->ItemID (sent by the item-id list)
    std::uint32_t postingItemId = 0;   // AuctionPosting::Items[0] entry (the house's own record)
    std::uint32_t queriedItemId = 0;   // the item id the bot asked for (0 when unknown)
};

// First source that names an item, in the order above; 0 = unknown (skip the listing).
inline std::uint32_t ResolveAhListingItemId(AhListingIdSources const& s)
{
    if (s.itemFieldId)
        return s.itemFieldId;
    if (s.bucketKeyItemId)
        return s.bucketKeyItemId;
    if (s.postingItemId)
        return s.postingItemId;
    return s.queriedItemId;
}

struct AhListingPrices
{
    bool isCommodity = false;
    std::uint64_t unitPrice = 0;
    std::uint64_t buyoutPrice = 0;
};

// A commodity listing carries a unit price; an item listing a buyout for the whole stack.
inline AhListingPrices ComputeAhListingPrices(bool hasUnitPrice, std::uint64_t unitPrice,
    std::uint64_t buyoutPrice, std::uint32_t count)
{
    AhListingPrices p;
    if (hasUnitPrice)
    {
        p.isCommodity = true;
        p.unitPrice = unitPrice;
        p.buyoutPrice = unitPrice * count;
    }
    else
    {
        p.buyoutPrice = buyoutPrice;
        p.unitPrice = count > 0 ? buyoutPrice / count : buyoutPrice;
    }
    return p;
}
} // namespace ai
