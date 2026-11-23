// Pure unit tests for W8 auction house policies (AhSellPolicy, AhBuyPolicy).
// Build & run from repo root:
//   g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/test_w8_ah.cpp -o /tmp/test_w8_ah && /tmp/test_w8_ah

#include "ai/playerbot/policy/ah/AhBuyPolicy.h"
#include "ai/playerbot/policy/ah/AhListingPolicy.h"
#include "ai/playerbot/policy/ah/AhSellPolicy.h"

#include <cassert>
#include <iostream>
#include <string>

int main()
{
    int passed = 0;

    // 1. Failing case first: a stackable item (maxStackSize > 1) MUST be routed to
    //    the commodity path, never AhPostKind::Item (HandleAuctionSellItem refuses maxStackSize > 1).
    {
        ai::AhSellInput in;
        in.itemId = 2589; // Linen Cloth (stackable)
        in.itemClass = 0; // non-trade-goods test to isolate stackable routing
        in.maxStackSize = 20;
        in.count = 5;
        in.vendorSellPrice = 13;
        in.depositTotal = 10;
        in.marketLowestUnitPrice = 100;
        in.freeMoneyForAh = 500;
        in.isUsageAh = true;
        ai::AhSellDecision const d = ai::DecideAhSell(in);
        assert(d.shouldPost);
        assert(d.kind == ai::AhPostKind::Commodity);
        assert(d.kind != ai::AhPostKind::Item);
        ++passed;
    }

    // 2. Single item (maxStackSize == 1) routes to Item path with 95% minBid and undercut.
    {
        ai::AhSellInput in;
        in.itemId = 2264;
        in.maxStackSize = 1;
        in.count = 1;
        in.vendorSellPrice = 50;
        in.depositTotal = 15;
        in.marketLowestUnitPrice = 200;
        in.freeMoneyForAh = 500;
        in.isUsageAh = true;
        ai::AhSellDecision const d = ai::DecideAhSell(in);
        assert(d.shouldPost);
        assert(d.kind == ai::AhPostKind::Item);
        assert(d.unitPrice == 190); // 200 - 5% (10) = 190, >= floor (50 + 15 = 65)
        assert(d.buyoutPrice == 190);
        assert(d.minBid > 0 && d.minBid < d.buyoutPrice);
        ++passed;
    }

    // 3. Price floor: never post below vendor sell price + deposit.
    {
        ai::AhSellInput in;
        in.itemId = 2265;
        in.maxStackSize = 1;
        in.count = 1;
        in.vendorSellPrice = 100;
        in.depositTotal = 25;
        in.marketLowestUnitPrice = 110; // undercut would be 105 < 125 floor
        in.freeMoneyForAh = 500;
        in.isUsageAh = true;
        ai::AhSellDecision const d = ai::DecideAhSell(in);
        assert(d.shouldPost);
        assert(d.unitPrice >= in.vendorSellPrice + in.depositTotal);
        assert(d.unitPrice == 125);
        ++passed;
    }

    // 4. No market and no UnitValue = do not post (LESS rule).
    {
        ai::AhSellInput in;
        in.itemId = 2266;
        in.maxStackSize = 1;
        in.count = 1;
        in.vendorSellPrice = 40;
        in.depositTotal = 10;
        in.marketLowestUnitPrice = 0;
        in.unitValue = 0;
        in.freeMoneyForAh = 500;
        in.isUsageAh = true;
        ai::AhSellDecision const d = ai::DecideAhSell(in);
        assert(!d.shouldPost);
        assert(std::string(d.reason) == "no-market-or-unit-value");
        ++passed;
    }

    // 5. Fallback to UnitValue when market has no listing.
    {
        ai::AhSellInput in;
        in.itemId = 2267;
        in.maxStackSize = 1;
        in.count = 1;
        in.vendorSellPrice = 40;
        in.depositTotal = 10;
        in.marketLowestUnitPrice = 0;
        in.unitValue = 180;
        in.freeMoneyForAh = 500;
        in.isUsageAh = true;
        ai::AhSellDecision const d = ai::DecideAhSell(in);
        assert(d.shouldPost);
        assert(d.unitPrice == 180);
        ++passed;
    }

    // 6. Never sell quest items, soulbound/equipped, W6a upgrades, or W7-reserved trade goods.
    {
        ai::AhSellInput base;
        base.itemId = 3000;
        base.maxStackSize = 1;
        base.count = 1;
        base.vendorSellPrice = 10;
        base.depositTotal = 5;
        base.marketLowestUnitPrice = 100;
        base.freeMoneyForAh = 500;
        base.isUsageAh = true;

        {
            auto in = base;
            in.itemClass = ai::kAhItemClassQuest;
            assert(!ai::DecideAhSell(in).shouldPost);
        }
        {
            auto in = base;
            in.soulbound = true;
            assert(!ai::DecideAhSell(in).shouldPost);
        }
        {
            auto in = base;
            in.equipped = true;
            assert(!ai::DecideAhSell(in).shouldPost);
        }
        {
            auto in = base;
            in.isW6aUpgrade = true;
            assert(!ai::DecideAhSell(in).shouldPost);
        }
        {
            auto in = base;
            in.itemClass = ai::kAhItemClassTradeGoods;
            in.maxStackSize = 20;
            // Reserved when needed by bot's own profession or an open guild order.
            assert(ai::ShouldReserveTradeGood(in.itemId, in.itemClass, true, false));
            in.reservedTradeGood = ai::ShouldReserveTradeGood(in.itemId, in.itemClass, true, false);
            assert(!ai::DecideAhSell(in).shouldPost);

            assert(ai::ShouldReserveTradeGood(in.itemId, in.itemClass, false, true));
            in.reservedTradeGood = ai::ShouldReserveTradeGood(in.itemId, in.itemClass, false, true);
            assert(!ai::DecideAhSell(in).shouldPost);

            // Other trade goods (not needed by own professions or open guild order) may be posted.
            assert(!ai::ShouldReserveTradeGood(in.itemId, in.itemClass, false, false));
            in.reservedTradeGood = ai::ShouldReserveTradeGood(in.itemId, in.itemClass, false, false);
            ai::AhSellDecision const d = ai::DecideAhSell(in);
            assert(d.shouldPost);
            assert(d.kind == ai::AhPostKind::Commodity);
            assert(d.unitPrice >= in.vendorSellPrice + in.depositTotal);
        }
        ++passed;
    }

    // 7. Buy policy: upgrade bought only when W6a scores it better than worn and inside budget.
    {
        ai::AhBuyInput in;
        in.auctionId = 101;
        in.itemId = 5001;
        in.count = 1;
        in.buyoutOrUnitPrice = 300;
        in.isUpgrade = true;
        in.w6aNewScore = 45;
        in.w6aWornScore = 50; // worse than worn -> refuse
        in.budgetCopper = 1000;
        assert(!ai::DecideAhBuy(in).shouldBuy);

        in.w6aNewScore = 60; // better than worn
        in.budgetCopper = 299; // over budget -> refuse
        assert(!ai::DecideAhBuy(in).shouldBuy);

        in.budgetCopper = 300; // exact budget -> buy
        ai::AhBuyDecision const d = ai::DecideAhBuy(in);
        assert(d.shouldBuy);
        assert(d.totalPrice == 300);

        in.isOwnAuction = true; // never buy own
        assert(!ai::DecideAhBuy(in).shouldBuy);
        ++passed;
    }

    // 8. Commodity quantity clamping by availability and budget.
    {
        assert(ai::AhClampCommodityBuyQty(10, 4, 50, 1000) == 4);
        assert(ai::AhClampCommodityBuyQty(10, 20, 50, 220) == 4);
        assert(ai::AhClampCommodityBuyQty(10, 20, 50, 40) == 0);
        ++passed;
    }

    // Commodity listings in an item-id list come without the Item field: the item id comes from the
    // bucket key, then the posting, then the queried id; a listing nobody names is still skipped.
    {
        ai::AhListingIdSources commodity;
        commodity.bucketKeyItemId = 2589;
        commodity.postingItemId = 2589;
        commodity.queriedItemId = 2589;
        assert(ai::ResolveAhListingItemId(commodity) == 2589);

        ai::AhListingIdSources postingOnly;
        postingOnly.postingItemId = 2589;
        assert(ai::ResolveAhListingItemId(postingOnly) == 2589);

        ai::AhListingIdSources queriedOnly;
        queriedOnly.queriedItemId = 2589;
        assert(ai::ResolveAhListingItemId(queriedOnly) == 2589);

        ai::AhListingIdSources item;
        item.itemFieldId = 15210;
        item.bucketKeyItemId = 99;
        assert(ai::ResolveAhListingItemId(item) == 15210);

        assert(ai::ResolveAhListingItemId(ai::AhListingIdSources{}) == 0);
        ++passed;
    }

    // Prices: a commodity listing (unit price, 5 Linen Cloth at 2500c) and an item listing (buyout for the stack).
    {
        ai::AhListingPrices const c = ai::ComputeAhListingPrices(true, 2500, 0, 5);
        assert(c.isCommodity && c.unitPrice == 2500 && c.buyoutPrice == 12500);

        ai::AhListingPrices const i = ai::ComputeAhListingPrices(false, 0, 5000, 1);
        assert(!i.isCommodity && i.unitPrice == 5000 && i.buyoutPrice == 5000);

        ai::AhListingPrices const zero = ai::ComputeAhListingPrices(false, 0, 300, 0);
        assert(zero.unitPrice == 300);
        ++passed;
    }

    // The visible commodity listing feeds the sell price: undercut of the 2500c unit price.
    {
        ai::AhSellInput in;
        in.itemId = 2589;
        in.itemClass = 0;
        in.maxStackSize = 20;
        in.count = 20;
        in.vendorSellPrice = 13;
        in.depositTotal = 10;
        in.marketLowestUnitPrice = ai::ComputeAhListingPrices(true, 2500, 0, 5).unitPrice;
        in.freeMoneyForAh = 100000;
        in.isUsageAh = true;
        ai::AhSellDecision const d = ai::DecideAhSell(in);
        assert(d.shouldPost);
        assert(d.kind == ai::AhPostKind::Commodity);
        assert(d.unitPrice > 0 && d.unitPrice < 2500);
        ++passed;
    }

    std::cout << "PASS: test_w8_ah (" << passed << " checks, 0 failed)\n";
    return 0;
}
