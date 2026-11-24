// Pure unit tests for the automatic AH market buyer rules (TbHostAhMarketBuyPure.h).
// Build & run from repo root:
//   g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/test_ah_market_buy_policy.cpp -o /tmp/test_ah_market_buy_policy && /tmp/test_ah_market_buy_policy

#include "forever/src/host/TbHostAhMarketBuyPure.h"

#include <cassert>
#include <cstdint>
#include <iostream>

int main()
{
    int passed = 0;
    using namespace tbhost::ah_market;

    BuyCandidateInput base;
    base.itemClass = kItemClassWeapon;
    base.inventoryType = 13; // INVTYPE_WEAPON
    base.quality = kQualityUncommon;
    base.requiredLevel = 10;
    base.botLevel = 20;
    base.canUse = true;
    base.hasNonOwnListing = true;
    base.unitPrice = 300;
    base.maxUnitPrice = 300;

    // 1. Failing case first: a listing over the price ceiling is never a candidate (the `ah bid` action would refuse it).
    {
        assert(IsBuyCandidate(base));
        BuyCandidateInput over = base;
        over.unitPrice = 301;
        assert(!IsBuyCandidate(over));
        ++passed;
    }

    // 2. Only a non-own, usable, equippable, uncommon-or-better weapon or armour piece the bot can wear is considered.
    {
        struct Block { std::uint32_t BuyCandidateInput::*field; std::uint32_t value; char const* name; };
        Block itemBlocks[] = {
            { &BuyCandidateInput::itemClass, 7, "trade goods class" },
            { &BuyCandidateInput::itemClass, 0, "consumable class" },
            { &BuyCandidateInput::quality, 1, "common quality" },
            { &BuyCandidateInput::inventoryType, kInventoryNonEquip, "non-equip inventory type" },
            { &BuyCandidateInput::requiredLevel, 21, "required level above the bot" },
        };
        for (Block const& b : itemBlocks)
        {
            BuyCandidateInput in = base;
            in.*(b.field) = b.value;
            if (IsBuyCandidate(in))
            {
                std::cerr << "candidate accepted with " << b.name << "\n";
                return 1;
            }
        }

        BuyCandidateInput noUse = base;
        noUse.canUse = false;
        assert(!IsBuyCandidate(noUse));
        BuyCandidateInput ownOnly = base;
        ownOnly.hasNonOwnListing = false;
        assert(!IsBuyCandidate(ownOnly));
        BuyCandidateInput armor = base;
        armor.itemClass = kItemClassArmor;
        armor.inventoryType = 5; // INVTYPE_CHEST
        assert(IsBuyCandidate(armor));
        BuyCandidateInput uncommon = base;
        uncommon.quality = 3;
        assert(IsBuyCandidate(uncommon));
        ++passed;
    }

    // 3. AiPlayerbot.AhMarketMaxSpendPerBot caps a single purchase; 0 means no cap.
    {
        BuyCandidateInput capped = base;
        capped.maxSpendPerBot = 250;
        assert(!IsBuyCandidate(capped));
        capped.unitPrice = 250;
        assert(IsBuyCandidate(capped));
        BuyCandidateInput unlimited = capped;
        unlimited.maxSpendPerBot = 0;
        unlimited.unitPrice = 100000;
        unlimited.maxUnitPrice = 100000;
        assert(IsBuyCandidate(unlimited));
        ++passed;
    }

    // 4. The chance roll is per pass: 0 never buys, 100 always does, otherwise the roll is compared as a percentage.
    {
        assert(!BuyChancePasses(0, 0));
        assert(!BuyChancePasses(0, 99));
        assert(BuyChancePasses(100, 99));
        assert(BuyChancePasses(100, 0));
        assert(BuyChancePasses(10, 9));
        assert(!BuyChancePasses(10, 10));
        assert(!BuyChancePasses(50, 50));
        assert(BuyChancePasses(50, 49));
        ++passed;
    }

    // 5. Pages advance by the bucket cap and restart after an empty page.
    {
        assert(NextPageOffset(0, false) == kMarketPageBuckets);
        assert(NextPageOffset(kMarketPageBuckets, false) == 2 * kMarketPageBuckets);
        assert(NextPageOffset(150, true) == 0);
        ++passed;
    }

    std::cout << "PASS: test_ah_market_buy_policy (" << passed << " checks, 0 failed)\n";
    return 0;
}
