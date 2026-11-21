#pragma once

// Pure W8 auction-buy decision policy (std-only, no core includes).
// Evaluates buyout-only purchases of upgrades (gated on W6a score) and
// needed mats/orders against the bot's copper budget.

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

namespace ai
{
struct AhItemCandidate
{
    std::uint32_t itemId = 0;
    std::string name;
};

struct AhResolveItemResult
{
    bool ok = false;
    std::uint32_t itemId = 0;
    char const* failCode = "";
};

inline std::string AhLowerAscii(std::string s)
{
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

inline AhResolveItemResult ResolveAhBuyItemId(std::string const& query, std::vector<AhItemCandidate> const& candidates)
{
    AhResolveItemResult out;
    std::vector<AhItemCandidate> unique;
    std::unordered_set<std::uint32_t> seen;
    for (AhItemCandidate const& c : candidates)
    {
        if (c.itemId == 0 || !seen.insert(c.itemId).second)
            continue;
        unique.push_back(c);
    }

    if (unique.empty())
    {
        out.failCode = "no-match";
        return out;
    }
    if (unique.size() == 1)
    {
        out.ok = true;
        out.itemId = unique[0].itemId;
        out.failCode = "ok";
        return out;
    }

    std::string const qLower = AhLowerAscii(query);
    std::uint32_t exactId = 0;
    std::size_t exactCount = 0;
    for (AhItemCandidate const& c : unique)
    {
        if (AhLowerAscii(c.name) == qLower)
        {
            exactId = c.itemId;
            ++exactCount;
        }
    }
    if (exactCount == 1)
    {
        out.ok = true;
        out.itemId = exactId;
        out.failCode = "ok";
        return out;
    }

    out.failCode = "ambiguous-item";
    return out;
}

// Per-unit price ceiling: 3x AHBot unit value, else 4x vendor buy price,
// else 10x vendor sell price, else 1g (10000c) fallback cap.
inline std::uint64_t AhComputeMaxUnitPrice(std::uint64_t unitValue, std::uint64_t vendorBuyPrice, std::uint64_t vendorSellPrice)
{
    if (unitValue > 0)
        return unitValue * 3;
    if (vendorBuyPrice > 0)
        return vendorBuyPrice * 4;
    if (vendorSellPrice > 0)
        return vendorSellPrice * 10;
    return 10000;
}

struct AhBuyInput
{
    std::uint32_t auctionId = 0;
    std::uint32_t itemId = 0;
    std::uint32_t count = 1;
    std::uint64_t buyoutOrUnitPrice = 0; // total buyout for single item, or per-unit price for commodity
    bool isCommodity = false;
    bool isOwnAuction = false;
    bool isUpgrade = false;
    std::uint32_t w6aNewScore = 0;
    std::uint32_t w6aWornScore = 0;
    bool isNeededMatOrExplicit = false;
    std::uint64_t budgetCopper = 0;
    std::uint64_t maxUnitPrice = 0;
    bool worldAuctionEnabled = true;
};

struct AhBuyDecision
{
    bool shouldBuy = false;
    bool isCommodity = false;
    std::uint32_t buyQuantity = 0;
    std::uint64_t totalPrice = 0;
    char const* reason = "";
};

inline std::uint32_t AhClampCommodityBuyQty(std::uint32_t requestedQty, std::uint32_t availableQty,
    std::uint64_t unitPrice, std::uint64_t budgetCopper)
{
    if (requestedQty == 0 || availableQty == 0 || unitPrice == 0)
        return 0;
    std::uint64_t const affordable = budgetCopper / unitPrice;
    if (affordable == 0)
        return 0;
    std::uint64_t const capped = std::min<std::uint64_t>({ requestedQty, availableQty, affordable });
    return static_cast<std::uint32_t>(capped);
}

inline AhBuyDecision DecideAhBuy(AhBuyInput const& in)
{
    AhBuyDecision out;
    if (!in.worldAuctionEnabled)
    {
        out.reason = "auction-disabled";
        return out;
    }
    if (in.itemId == 0 || in.count == 0)
    {
        out.reason = "invalid-item";
        return out;
    }
    if (in.isOwnAuction)
    {
        out.reason = "own-auction";
        return out;
    }
    if (in.buyoutOrUnitPrice == 0)
    {
        out.reason = "no-buyout";
        return out;
    }

    if (in.isUpgrade)
    {
        if (in.w6aNewScore <= in.w6aWornScore)
        {
            out.reason = "not-better-than-worn";
            return out;
        }
    }
    else if (!in.isNeededMatOrExplicit)
    {
        out.reason = "not-needed";
        return out;
    }

    std::uint64_t const unitPrice = in.isCommodity
        ? in.buyoutOrUnitPrice
        : ((in.buyoutOrUnitPrice + in.count - 1) / in.count);
    if (in.maxUnitPrice > 0 && unitPrice > in.maxUnitPrice)
    {
        out.reason = "over-price-ceiling";
        return out;
    }

    std::uint64_t const totalPrice = in.isCommodity ? (in.buyoutOrUnitPrice * in.count) : in.buyoutOrUnitPrice;
    if (totalPrice > in.budgetCopper)
    {
        out.reason = "over-budget";
        return out;
    }

    out.shouldBuy = true;
    out.isCommodity = in.isCommodity;
    out.buyQuantity = in.count;
    out.totalPrice = totalPrice;
    out.reason = "ok";
    return out;
}

} // namespace ai
