// Pure unit tests for W8 auction house review fixes (PR #97):
//   - Finding 1: ItemUsage::ITEM_USAGE_AH gate for bulk sell, profession tool refusal,
//                and TortoiseBots.World.Auction config gate.
//   - Finding 10: Per-unit price ceiling and single exact item-id resolution for ah buy.
//   - Low 1: ConfirmPendingPosts matches posted item guid (single) / count+price (commodity).
//
// Build & run from repo root:
//   g++ -std=c++17 -O1 -Wall -Wextra -I. -Iai forever/tools/test_ah.cpp -o /tmp/test_ah && /tmp/test_ah

#include "ai/playerbot/policy/ah/AhBuyPolicy.h"
#include "ai/playerbot/policy/ah/AhSellPolicy.h"

#include <cassert>
#include <iostream>
#include <string>
#include <vector>

int main()
{
    int passed = 0;

    // 1. Bulk `ah sell` refuses items whose usage is not ITEM_USAGE_AH (ammo, reagents, food, tools).
    {
        ai::AhSellInput in;
        in.itemId = 2512; // Rough Arrow (ammo)
        in.maxStackSize = 200;
        in.count = 200;
        in.vendorSellPrice = 1;
        in.depositTotal = 1;
        in.marketLowestUnitPrice = 5;
        in.freeMoneyForAh = 500;
        in.explicitItem = false;
        in.isUsageAh = false;

        ai::AhSellDecision const d = ai::DecideAhSell(in);
        assert(!d.shouldPost);
        assert(std::string(d.reason) == "not-ah-usage");

        in.isUsageAh = true;
        assert(ai::DecideAhSell(in).shouldPost);
        ++passed;
    }

    // 2. Profession tools (Mining Pick, Skinning Knife, crafting tools) are ALWAYS refused,
    //    even on an explicit `ah sell <item>`.
    {
        ai::AhSellInput in;
        in.itemId = 2901; // Mining Pick
        in.maxStackSize = 1;
        in.count = 1;
        in.vendorSellPrice = 16;
        in.depositTotal = 5;
        in.marketLowestUnitPrice = 100;
        in.freeMoneyForAh = 500;
        in.explicitItem = true;
        in.isUsageAh = false;
        in.isProfessionTool = ai::IsAhProfessionTool(in.itemId, false);
        assert(in.isProfessionTool);

        ai::AhSellDecision const d = ai::DecideAhSell(in);
        assert(!d.shouldPost);
        assert(std::string(d.reason) == "profession-tool");

        // Explicit `ah sell <item>` on a normal non-tool item is allowed.
        in.itemId = 2264;
        in.isProfessionTool = ai::IsAhProfessionTool(in.itemId, false);
        assert(!in.isProfessionTool);
        assert(ai::DecideAhSell(in).shouldPost);
        ++passed;
    }

    // 3. Config gate: when TortoiseBots.World.Auction is off, both sell and buy refuse with "auction-disabled".
    {
        ai::AhSellInput s;
        s.itemId = 2264;
        s.maxStackSize = 1;
        s.count = 1;
        s.vendorSellPrice = 50;
        s.depositTotal = 10;
        s.marketLowestUnitPrice = 200;
        s.freeMoneyForAh = 500;
        s.isUsageAh = true;
        s.worldAuctionEnabled = false;
        ai::AhSellDecision const sd = ai::DecideAhSell(s);
        assert(!sd.shouldPost);
        assert(std::string(sd.reason) == "auction-disabled");

        ai::AhBuyInput b;
        b.auctionId = 10;
        b.itemId = 2264;
        b.count = 1;
        b.buyoutOrUnitPrice = 100;
        b.isNeededMatOrExplicit = true;
        b.budgetCopper = 1000;
        b.maxUnitPrice = 300;
        b.worldAuctionEnabled = false;
        ai::AhBuyDecision const bd = ai::DecideAhBuy(b);
        assert(!bd.shouldBuy);
        assert(std::string(bd.reason) == "auction-disabled");
        ++passed;
    }

    // 4. Buy price ceiling: refuses listings above maxUnitPrice even when purse budget is huge.
    {
        std::uint64_t const ceiling = ai::AhComputeMaxUnitPrice(100, 0, 10);
        assert(ceiling == 300);

        ai::AhBuyInput b;
        b.auctionId = 42;
        b.itemId = 2589;
        b.count = 1;
        b.buyoutOrUnitPrice = 50000; // 5g for 1 Linen Cloth
        b.isCommodity = true;
        b.isNeededMatOrExplicit = true;
        b.budgetCopper = 100000; // 10g in purse
        b.maxUnitPrice = ceiling;

        ai::AhBuyDecision const d1 = ai::DecideAhBuy(b);
        assert(!d1.shouldBuy);
        assert(std::string(d1.reason) == "over-price-ceiling");

        b.buyoutOrUnitPrice = 250;
        ai::AhBuyDecision const d2 = ai::DecideAhBuy(b);
        assert(d2.shouldBuy);
        ++passed;
    }

    // 5. `ah buy <name>` resolves to a single exact item id; ambiguous name -> "ambiguous-item".
    {
        std::vector<ai::AhItemCandidate> const candidates = {
            { 2589, "Linen Cloth" },
            { 2996, "Bolt of Linen Cloth" },
            { 2589, "Linen Cloth" } // duplicate listing for same itemId
        };

        ai::AhResolveItemResult const rAmbig = ai::ResolveAhBuyItemId("Linen", candidates);
        assert(!rAmbig.ok);
        assert(std::string(rAmbig.failCode) == "ambiguous-item");

        ai::AhResolveItemResult const rExact = ai::ResolveAhBuyItemId("linen cloth", candidates);
        assert(rExact.ok);
        assert(rExact.itemId == 2589);

        ai::AhResolveItemResult const rNone = ai::ResolveAhBuyItemId("Mithril", {});
        assert(!rNone.ok);
        assert(std::string(rNone.failCode) == "no-match");
        ++passed;
    }

    // 6. ConfirmPendingPosts matching: single item matches by itemGuid, not itemId alone.
    {
        // Different guid, same itemId -> must NOT match.
        assert(!ai::MatchesPendingPost(2002, 2264, 1, 190, 1001, 2264, 1, 190, false));
        // Same guid -> matches.
        assert(ai::MatchesPendingPost(2002, 2264, 1, 190, 2002, 2264, 1, 190, false));
        // Commodity (guid 0): matches same itemId + count + unitPrice.
        assert(ai::MatchesPendingPost(0, 2589, 5, 100, 0, 2589, 5, 100, true));
        assert(!ai::MatchesPendingPost(0, 2589, 5, 100, 0, 2589, 5, 150, true));
        ++passed;
    }

    std::cout << "PASS: test_ah (" << passed << " checks, 0 failed)\n";
    return 0;
}
