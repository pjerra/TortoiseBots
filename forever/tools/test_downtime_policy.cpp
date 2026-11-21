// Standalone proof of the `.bot downtime` town-visit rules (runtime/DowntimePolicy.h):
//   g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/test_downtime_policy.cpp -o /tmp/test_downtime_policy && /tmp/test_downtime_policy
//
// The visit order (train before the junk sale before repair) and the gold
// check for the trainer the bot will actually visit (the nearest reachable
// one, not the cheapest rank anywhere).
#include "runtime/DowntimePolicy.h"

#include <cassert>
#include <iostream>
#include <vector>

using TortoiseBots::DowntimeErrand;
using TortoiseBots::DowntimeStartRefusal;
using TortoiseBots::DowntimeTrainerRank;

int main()
{
    int passed = 0;

    // 1. Errand order: the class trainer first, then the junk vendor, then repair.
    {
        std::vector<DowntimeErrand> tried;
        assert(TortoiseBots::NextDowntimeErrand(tried, true, true, true) == DowntimeErrand::Trainer);
        tried.push_back(DowntimeErrand::Trainer);
        assert(TortoiseBots::NextDowntimeErrand(tried, true, true, true) == DowntimeErrand::Vendor);
        tried.push_back(DowntimeErrand::Vendor);
        assert(TortoiseBots::NextDowntimeErrand(tried, true, true, true) == DowntimeErrand::Repair);
        tried.push_back(DowntimeErrand::Repair);
        assert(TortoiseBots::NextDowntimeErrand(tried, true, true, true) == DowntimeErrand::None);
        // A failed walk never loops: a tried errand is skipped even when still needed.
        assert(TortoiseBots::NextDowntimeErrand(tried, true, false, false) == DowntimeErrand::None);
        // Nothing needed: no errand.
        std::vector<DowntimeErrand> fresh;
        assert(TortoiseBots::NextDowntimeErrand(fresh, false, false, false) == DowntimeErrand::None);
        // Training waits for nothing: trainer first even when only repair is also due.
        assert(TortoiseBots::NextDowntimeErrand(fresh, true, false, true) == DowntimeErrand::Trainer);
        ++passed;
    }

    // 2. An errand is done when its need is gone.
    {
        assert(TortoiseBots::DowntimeErrandDone(DowntimeErrand::Trainer, false, true, true));
        assert(!TortoiseBots::DowntimeErrandDone(DowntimeErrand::Trainer, true, false, false));
        assert(TortoiseBots::DowntimeErrandDone(DowntimeErrand::Vendor, true, false, true));
        assert(TortoiseBots::DowntimeErrandDone(DowntimeErrand::Repair, true, true, false));
        assert(TortoiseBots::DowntimeErrandDone(DowntimeErrand::None, true, true, true));
        ++passed;
    }

    // 3. The gold check is per visited trainer, not per global cheapest rank:
    // the nearest trainer teaches only a 500c rank, the purse holds 200c, and
    // the cheapest rank anywhere (100c, taught far away) must not admit this one.
    {
        std::vector<DowntimeTrainerRank> due = {
            {1001, 100, {77}},   // cheap rank, taught only by far entry 77
            {1002, 500, {12}},   // nearest entry 12 teaches only the 500c rank
        };
        assert(!TortoiseBots::DowntimeTrainerEntryTeachesAffordable(12, due, 200));
        // The far trainer itself is admitted on the same purse.
        assert(TortoiseBots::DowntimeTrainerEntryTeachesAffordable(77, due, 200));
        // A purse covering the nearest trainer's rank admits it.
        assert(TortoiseBots::DowntimeTrainerEntryTeachesAffordable(12, due, 500));
        // No due ranks, or none the entry teaches: refused.
        std::vector<DowntimeTrainerRank> empty;
        assert(!TortoiseBots::DowntimeTrainerEntryTeachesAffordable(12, empty, 500));
        std::vector<DowntimeTrainerRank> elsewhere = { {1001, 100, {77}} };
        assert(!TortoiseBots::DowntimeTrainerEntryTeachesAffordable(12, elsewhere, 500));
        // An unaffordable rank the entry teaches does not admit it.
        assert(!TortoiseBots::DowntimeTrainerEntryTeachesAffordable(12, due, 99));
        ++passed;
    }

    // 4. Start gate: only owned, living, non-autonomous alts.
    {
        assert(TortoiseBots::CheckDowntimeStart(true, true, false, false, false) == DowntimeStartRefusal::None);
        assert(TortoiseBots::CheckDowntimeStart(false, true, false, false, false) == DowntimeStartRefusal::Dead);
        assert(TortoiseBots::CheckDowntimeStart(true, false, false, true, false) == DowntimeStartRefusal::NotOwnedAlt);
        assert(TortoiseBots::CheckDowntimeStart(true, false, true, false, false) == DowntimeStartRefusal::NotOwnedAlt);
        assert(TortoiseBots::CheckDowntimeStart(true, true, false, false, true) == DowntimeStartRefusal::Autonomous);
        ++passed;
    }

    // 5. Window cap: fifteen minutes, no more.
    {
        assert(!TortoiseBots::DowntimeWindowExpired(1000, 1000 + 14 * 60));
        assert(TortoiseBots::DowntimeWindowExpired(1000, 1000 + 15 * 60));
        ++passed;
    }

    std::cout << "PASS: test_downtime_policy (" << passed << " checks, 0 failed)\n";
    return 0;
}
