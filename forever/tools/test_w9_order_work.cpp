// Pure unit tests for W9 guild order work step-choice policy (GuildOrderWorkPolicy.h).
// Build & run from repo root:
//   g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/test_w9_order_work.cpp -o /tmp/test_w9_order_work && /tmp/test_w9_order_work

#include "runtime/GuildOrderWorkPolicy.h"

#include <cassert>
#include <iostream>
#include <string>

int main()
{
    using namespace TortoiseBots::GuildOrders;
    int passed = 0;

    // 1. Failing case first: when no local craft/gather/AH source is in reach (no W4 travel),
    //    DecideOrderStep MUST return CannotTravel ("cannot: travel"), never Complete.
    {
        OrderStepInput in;
        in.type = OrderType::Craft;
        in.delivered = 0;
        in.want = 10;
        in.inBags = 0;
        OrderStepDecision const d = DecideOrderStep(in);
        assert(d.action == OrderStepAction::CannotTravel);
        assert(d.action != OrderStepAction::Complete);
        assert(std::string(d.reason) == "cannot: travel");
        ++passed;
    }

    // 2. Source priority for Craft order: craft > gather > AH buy.
    {
        OrderStepInput in;
        in.type = OrderType::Craft;
        in.want = 10;
        in.canCraft = true;
        in.canGather = true;
        in.canBuyAh = true;
        assert(DecideOrderStep(in).action == OrderStepAction::Craft);

        in.canCraft = false;
        assert(DecideOrderStep(in).action == OrderStepAction::Gather);

        in.canGather = false;
        assert(DecideOrderStep(in).action == OrderStepAction::BuyAh);
        ++passed;
    }

    // 3. OrderType::Buy honours sources policy (does not craft or gather).
    {
        OrderStepInput in;
        in.type = OrderType::Buy;
        in.want = 5;
        in.canCraft = true;
        in.canGather = true;
        in.canBuyAh = false;
        assert(DecideOrderStep(in).action == OrderStepAction::CannotTravel);

        in.canBuyAh = true;
        assert(DecideOrderStep(in).action == OrderStepAction::BuyAh);
        ++passed;
    }

    // 4. OrderType::Farm honours sources policy (gathers, does not buy on AH).
    {
        OrderStepInput in;
        in.type = OrderType::Farm;
        in.want = 5;
        in.canBuyAh = true;
        in.canGather = false;
        assert(DecideOrderStep(in).action == OrderStepAction::CannotTravel);

        in.canGather = true;
        assert(DecideOrderStep(in).action == OrderStepAction::Gather);
        ++passed;
    }

    // 5. Bags count toward have; when have >= want, deliver bank first, else mail, else cannot: travel.
    {
        OrderStepInput in;
        in.type = OrderType::Craft;
        in.delivered = 4;
        in.inBags = 6;
        in.want = 10;
        in.canCraft = true;
        in.canDeliverBank = true;
        in.canDeliverMail = true;
        OrderStepDecision const dBank = DecideOrderStep(in);
        assert(dBank.have == 10);
        assert(dBank.action == OrderStepAction::DeliverBank);

        in.canDeliverBank = false;
        assert(DecideOrderStep(in).action == OrderStepAction::DeliverMail);

        in.canDeliverMail = false;
        assert(DecideOrderStep(in).action == OrderStepAction::CannotTravel);
        ++passed;
    }

    // 6. Complete when delivered >= want and bags have 0 left to deliver.
    {
        OrderStepInput in;
        in.type = OrderType::Craft;
        in.delivered = 10;
        in.inBags = 0;
        in.want = 10;
        assert(DecideOrderStep(in).action == OrderStepAction::Complete);
        ++passed;
    }

    // 7. Live item 10: a personal craft order with everything crafted delivers by
    // mail (no guild bank exists); a reachable mailbox must decide DeliverMail.
    {
        OrderStepInput in;
        in.type = OrderType::Craft;
        in.delivered = 0;
        in.inBags = 2;
        in.want = 2;
        in.canCraft = true;
        in.canDeliverBank = false;
        in.canDeliverMail = true;
        OrderStepDecision const d = DecideOrderStep(in);
        assert(d.have == 2);
        assert(d.action == OrderStepAction::DeliverMail);
        ++passed;
    }

    std::cout << "PASS: test_w9_order_work (" << passed << " checks, 0 failed)\n";
    return 0;
}
