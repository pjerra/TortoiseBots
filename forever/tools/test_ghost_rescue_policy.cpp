// Standalone test of W1's ghost rescue rule (ai/playerbot/policy/death/GhostRescuePolicy.h):
//   g++ -std=c++17 -O1 -Wall -Wextra -If. forever/tools/test_ghost_rescue_policy.cpp -o /tmp/test_ghost_rescue_policy && /tmp/test_ghost_rescue_policy
//
// The only teleport a free bot may take beyond the core's release is the rescue of a ghost that made no headway toward its corpse
// or toward the spirit healer for five minutes (spec D2). The rule must not fire on the first observation, must count real ground
// gained (5 yd) as progress toward either target, and must fire exactly at the five-minute mark.
#include "ai/playerbot/policy/death/GhostRescuePolicy.h"

#include <cstdio>

using TortoiseBots::GhostRescueProgress;

static int g_checks = 0, g_fail = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

int main()
{
    // The constants are the decision: five minutes without headway, five yards count as headway.
    CHECK(TortoiseBots::GHOST_RESCUE_STALL_MS == 300000u);
    CHECK(TortoiseBots::GHOST_RESCUE_PROGRESS_YD == 5.0f);

    {
        GhostRescueProgress p;
        // The first look only arms the tracker: a ghost just released is not stuck.
        CHECK(!p.Observe(120.0f, 80.0f, 500));
        CHECK(!p.Due());
        // Standing still for 299.5 s is not enough.
        p.Observe(120.0f, 80.0f, 299500);
        CHECK(!p.Due());
        // At 300 s the rescue is due.
        p.Observe(120.0f, 80.0f, 500);
        CHECK(p.Due());
    }

    {
        GhostRescueProgress p;
        p.Observe(120.0f, 80.0f, 500);
        // Four yards closer is NOT progress (the threshold is five).
        CHECK(!p.Observe(116.0f, 80.0f, 1000));
        CHECK(!p.Due());
        // Five yards closer to the corpse is.
        CHECK(p.Observe(111.0f, 80.0f, 1000));
        CHECK(!p.Due());
        // ... and it re-arms the clock: the stall restarts from here.
        p.Observe(111.0f, 80.0f, 299000);
        CHECK(!p.Due());
        p.Observe(111.0f, 80.0f, 1000);
        CHECK(p.Due());
    }

    {
        GhostRescueProgress p;
        p.Observe(120.0f, 300.0f, 500);
        // Headway toward the healer counts even while the corpse stays away (the spirit-healer leg).
        CHECK(p.Observe(120.0f, 294.0f, 1000));
        CHECK(!p.Due());
        // A ghost that moves away is not making progress: the stall keeps counting.
        p.Observe(140.0f, 310.0f, 300000);
        CHECK(p.Due());
    }

    {
        GhostRescueProgress p;
        p.Observe(120.0f, 80.0f, 500);
        p.Observe(120.0f, 80.0f, 300000);
        CHECK(p.Due());
        // Reset (the bot revived, or a new death) forgets the run: the next look arms again.
        p.Reset();
        CHECK(!p.Due());
        CHECK(!p.Observe(20.0f, 20.0f, 500));
        CHECK(!p.Due());
    }

    if (g_fail)
    {
        std::printf("RESULT: FAIL (%d of %d checks)\n", g_fail, g_checks);
        return 1;
    }
    std::printf("RESULT: PASS (%d checks)\n", g_checks);
    return 0;
}
