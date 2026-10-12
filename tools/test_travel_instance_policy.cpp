#include "../ai/playerbot/TravelInstancePolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::TravelSelectionBlockedByInstance;

int main()
{
    // A companion inside a dungeon or raid must not run travel selection: this
    // is the hired-raid stall traced to ChooseTravelTargetAction (issue #388).
    CHECK(TravelSelectionBlockedByInstance(true, true));

    // In the open world the companion travels as before.
    CHECK(!TravelSelectionBlockedByInstance(true, false));

    // A masterless pool bot keeps today's behaviour inside instances; its
    // travel is already throttled by the IN_INSTANCE priority branch, and this
    // fix deliberately leaves that path untouched.
    CHECK(!TravelSelectionBlockedByInstance(false, true));
    CHECK(!TravelSelectionBlockedByInstance(false, false));

    // The gather sweep follows the same rule: a companion stops sweeping for
    // nodes inside an instance, everyone else is unchanged.
    CHECK(ai::GatherSweepBlockedByInstance(true, true));
    CHECK(!ai::GatherSweepBlockedByInstance(true, false));
    CHECK(!ai::GatherSweepBlockedByInstance(false, true));
    CHECK(!ai::GatherSweepBlockedByInstance(false, false));

    std::cout << "travel instance policy: OK\n";
    return 0;
}
