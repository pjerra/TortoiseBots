// Pure unit tests for the guild order read-verb policy (runtime/GuildOrderReadPolicy.h).
// Build & run from repo root:
//   g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/test_guild_order_read.cpp -o /tmp/test_guild_order_read && /tmp/test_guild_order_read

#include "runtime/GuildOrderReadPolicy.h"

#include <cassert>
#include <iostream>

int main()
{
    using namespace TortoiseBots::GuildOrders;
    int passed = 0;
    constexpr uint32_t kPlainClass = 7; // ITEM_CLASS_TRADE_GOODS

    // 1. The hand-over rule: bound-on-pickup, quest-bound, quest class and
    //    timed items can never reach the guild; a named plain item can.
    {
        OrderableFacts const plain = CheckOrderable(kPlainClass, 0, 0, false);
        assert(IsOrderableItem(plain));
        assert(!IsOrderableItem(CheckOrderable(kPlainClass, kBindOnAcquire, 0, false)));
        assert(!IsOrderableItem(CheckOrderable(kPlainClass, kBindQuest, 0, false)));
        assert(!IsOrderableItem(CheckOrderable(kItemClassQuest, 0, 0, false)));
        assert(!IsOrderableItem(CheckOrderable(kPlainClass, 0, 3600, false)));
        OrderableFacts unnamed = CheckOrderable(kPlainClass, 0, 0, false);
        unnamed.named = false;
        assert(!IsOrderableItem(unnamed));
        // Conjured is a search exclusion, not part of the hand-over rule.
        assert(IsOrderableItem(CheckOrderable(kPlainClass, 0, 0, true)));
        ++passed;
    }

    // 2. Farm verdict: no node source, no profession, low skill, then ok; a
    //    second source kind rescues a bot that cannot serve the first.
    {
        FarmSkill const none;
        FarmSkill const herbLow{ true, 50 };
        FarmSkill const herbOk{ true, 120 };
        FarmSkill const miningOk{ true, 150 };

        assert(FarmVerdict(false, 0, false, 0, herbOk, miningOk) == OrderReason::NoSource);
        assert(FarmVerdict(true, 1, false, 0, none, miningOk) == OrderReason::NoSkill);
        assert(FarmVerdict(true, 75, false, 0, herbLow, miningOk) == OrderReason::LowSkill);
        assert(FarmVerdict(true, 75, false, 0, herbOk, none) == OrderReason::None);
        assert(FarmVerdict(false, 0, true, 75, none, miningOk) == OrderReason::None);
        // Both kinds: an ore-capable bot serves an item whose herb lock is too high.
        assert(FarmVerdict(true, 300, true, 75, herbLow, miningOk) == OrderReason::None);
        ++passed;
    }

    // 3. Craft verdict: a recipe the bot does not know or an item no recipe
    //    creates is norecipe; knowing it below its rank is lowskill.
    {
        assert(CraftVerdict(false, true, 200, 1) == OrderReason::NoRecipe);
        assert(CraftVerdict(true, false, 200, 1) == OrderReason::NoRecipe);
        assert(CraftVerdict(true, true, 40, 75) == OrderReason::LowSkill);
        assert(CraftVerdict(true, true, 75, 75) == OrderReason::None);
        ++passed;
    }

    // 4. Busy rule: stocked keeps the bot busy; finished and cancelled do not.
    {
        assert(!OrderBotBusy(false, false, OrderState::Working));
        assert(!OrderBotBusy(true, true, OrderState::Working));
        assert(OrderBotBusy(true, false, OrderState::Working));
        assert(OrderBotBusy(true, false, OrderState::Ready));
        assert(OrderBotBusy(true, false, OrderState::Stocked));
        assert(!OrderBotBusy(true, false, OrderState::Done));
        assert(!OrderBotBusy(true, false, OrderState::Mailed));
        assert(!OrderBotBusy(true, false, OrderState::Cant));
        ++passed;
    }

    // 5. Preview types and browse rules.
    {
        assert(PreviewTypeSupported(OrderType::Farm));
        assert(PreviewTypeSupported(OrderType::Craft));
        assert(PreviewTypeSupported(OrderType::Buy));
        assert(!PreviewTypeSupported(OrderType::Enchant));

        assert(IsBrowsableCategory(OrderItemCategory::Herb));
        assert(IsBrowsableCategory(OrderItemCategory::Ore));
        assert(IsBrowsableCategory(OrderItemCategory::Leather));
        assert(IsBrowsableCategory(OrderItemCategory::Fish));
        assert(IsBrowsableCategory(OrderItemCategory::Cloth));
        assert(!IsBrowsableCategory(OrderItemCategory::Drop));
        assert(!IsBrowsableCategory(OrderItemCategory::Other));

        assert(kSearchMinChars == 3);
        assert(kSearchMaxRows == 30);
        assert(kWhoMaxRows == 30);
        ++passed;
    }

    std::cout << "PASS: test_guild_order_read (" << passed << " checks, 0 failed)\n";
    return 0;
}
