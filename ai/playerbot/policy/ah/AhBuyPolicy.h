#pragma once

// Pure W8 auction-buy decision policy (std-only, no core includes).
// Evaluates buyout-only purchases of upgrades (gated on W6a score) and
// needed mats/orders against the bot's copper budget.

#include <algorithm>
#include <cstdint>

namespace ai
{
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
