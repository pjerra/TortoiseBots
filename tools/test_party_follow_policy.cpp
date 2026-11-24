#include "../ai/playerbot/policy/group/PartyFollowPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::FollowingPartyCounts;

int main()
{
    std::cout << "Starting TortoiseBots following-party tests...\n";

    // (1) A bot with no real player master decides for itself. This is the
    // free bot of the Forever slice: its kit carries neither follow nor wander,
    // so the old follow/wander answer left every quest travel gate reading
    // "no party member can take this quest" (live: all quest destinations
    // rejected as inactive, no QuestTravelToGiver/Taker rows).
    {
        CHECK(FollowingPartyCounts(false, false, false));
        CHECK(FollowingPartyCounts(false, false, true));
        CHECK(FollowingPartyCounts(false, true, false));
        std::cout << "  [PASS] masterless bot answers for itself\n";
    }

    // (2) A bot with a real player master follows that player: only the group
    // leader or the follow/wander kit lets its own menu count.
    {
        CHECK(!FollowingPartyCounts(true, false, false));
        CHECK(FollowingPartyCounts(true, true, false));
        CHECK(FollowingPartyCounts(true, false, true));
        std::cout << "  [PASS] a mastered bot needs leadership or a follow kit\n";
    }

    std::cout << "All following-party tests passed.\n";
    return 0;
}
