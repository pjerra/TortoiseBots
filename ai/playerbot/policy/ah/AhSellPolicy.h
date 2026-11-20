#pragma once

// Pure W8 auction-sell decision policy (std-only, no core includes).
// Decides whether an inventory item may be posted on the faction AH,
// routes single items vs stackables (commodities), and computes the
// floor-safe listing price.

#include <algorithm>
#include <cstdint>

namespace ai
{
enum class AhPostKind : std::uint8_t
{
    None = 0,
    Item = 1,
    Commodity = 2
};

inline constexpr std::uint32_t kAhItemClassTradeGoods = 7;
inline constexpr std::uint32_t kAhItemClassQuest = 12;
inline constexpr std::uint32_t kAhDefaultRunTimeMinutes = 8 * 60; // Classic 1.60: 2h, 8h, 24h

// Reserve trade goods only when needed by the bot's own professions or an open guild order.
inline bool ShouldReserveTradeGood(std::uint32_t itemId, std::uint32_t itemClass,
    bool neededByOwnProfessions, bool neededByGuildOrder)
{
    if (itemClass != kAhItemClassTradeGoods || itemId == 0)
        return false;
    return neededByOwnProfessions || neededByGuildOrder;
}

struct AhSellInput
{
    std::uint32_t itemId = 0;
    std::uint32_t itemClass = 0;
    std::uint32_t maxStackSize = 1;
    std::uint32_t count = 1;
    std::uint64_t vendorSellPrice = 0;       // per-unit vendor sell price (copper)
    std::uint64_t depositTotal = 0;          // total deposit for this posting (copper)
    std::uint64_t marketLowestUnitPrice = 0; // lowest competing unit price on AH (0 = none)
    std::uint64_t unitValue = 0;             // fallback unit value e.g. AuctionBotVanilla::UnitValue (0 = none)
    std::uint64_t freeMoneyForAh = 0;        // copper available for deposit
    bool soulbound = false;
    bool equipped = false;
    bool conjured = false;
    bool nonEmptyBag = false;
    bool isW6aUpgrade = false;
    bool blacklisted = false;
    bool reservedTradeGood = false;
};

struct AhSellDecision
{
    bool shouldPost = false;
    AhPostKind kind = AhPostKind::None;
    std::uint64_t unitPrice = 0;
    std::uint64_t minBid = 0;
    std::uint64_t buyoutPrice = 0;
    std::uint32_t runTimeMinutes = kAhDefaultRunTimeMinutes;
    char const* reason = "";
};

inline AhSellDecision DecideAhSell(AhSellInput const& in)
{
    AhSellDecision out;
    if (in.itemId == 0 || in.count == 0)
    {
        out.reason = "invalid-item";
        return out;
    }
    if (in.soulbound || in.equipped)
    {
        out.reason = "bound-or-equipped";
        return out;
    }
    if (in.conjured || in.nonEmptyBag)
    {
        out.reason = "not-tradable";
        return out;
    }
    if (in.itemClass == kAhItemClassQuest)
    {
        out.reason = "quest-item";
        return out;
    }
    if (in.itemClass == kAhItemClassTradeGoods && in.reservedTradeGood)
    {
        out.reason = "trade-goods-reserved";
        return out;
    }
    if (in.blacklisted)
    {
        out.reason = "blacklisted";
        return out;
    }
    if (in.isW6aUpgrade)
    {
        out.reason = "w6a-upgrade";
        return out;
    }
    if (in.depositTotal > in.freeMoneyForAh)
    {
        out.reason = "no-deposit-money";
        return out;
    }

    // HandleAuctionSellItem refuses maxStackSize > 1; stackables must use the commodity path.
    AhPostKind const kind = (in.maxStackSize > 1) ? AhPostKind::Commodity : AhPostKind::Item;
    if (kind == AhPostKind::Item && in.count != 1)
    {
        out.reason = "non-stackable-bad-count";
        return out;
    }

    if (in.marketLowestUnitPrice == 0 && in.unitValue == 0)
    {
        out.reason = "no-market-or-unit-value";
        return out;
    }

    std::uint64_t const depositPerUnit = (in.depositTotal + in.count - 1) / in.count;
    std::uint64_t const floorPerUnit = std::max<std::uint64_t>(1, in.vendorSellPrice + depositPerUnit);

    std::uint64_t candidate = 0;
    if (in.marketLowestUnitPrice > 0)
    {
        std::uint64_t const step = std::max<std::uint64_t>(1, in.marketLowestUnitPrice / 20);
        candidate = (in.marketLowestUnitPrice > step) ? (in.marketLowestUnitPrice - step) : 1;
    }
    else
    {
        candidate = in.unitValue;
    }

    std::uint64_t const unitPrice = std::max(candidate, floorPerUnit);

    out.shouldPost = true;
    out.kind = kind;
    out.unitPrice = unitPrice;
    out.buyoutPrice = unitPrice * in.count;
    out.minBid = (kind == AhPostKind::Item) ? std::max<std::uint64_t>(1, (out.buyoutPrice * 95) / 100) : 0;
    out.runTimeMinutes = kAhDefaultRunTimeMinutes;
    out.reason = "ok";
    return out;
}

} // namespace ai
