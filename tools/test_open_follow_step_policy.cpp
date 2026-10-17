// Standalone policy test for the open-world follow step: a bot whose server
// follow stands still on a navmesh gap walks short straight steps toward a
// seen leader on its floor, each closing the gap, and stops otherwise.
// Mirrors ai/playerbot/OpenFollowStepPolicy.h; no server headers needed.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_open_follow_step_policy.cpp -o /tmp/test_open_follow_step_policy && /tmp/test_open_follow_step_policy

#include "../ai/playerbot/OpenFollowStepPolicy.h"

#include <iostream>

namespace
{
int failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { \
        std::cerr << "FAIL line " << __LINE__ << ": " #cond "\n"; \
        ++failures; \
    } \
} while (0)
} // namespace

using ai::OpenFollowStepHeadway;

int main()
{
    // Windhorn entrance: leader 15 yd ahead on the same floor, in sight.
    CHECK(ai::OpenFollowStepAllowed(15.0f, 0.5f, true));
    CHECK(ai::OpenFollowStepLeaderNear(15.0f, 0.5f));
    // Step length caps at 25 yd toward the leader.
    {
        ai::OpenFollowStep step = ai::OpenFollowStepToward(0.0f, 0.0f, 40.0f, 0.0f);
        CHECK(step.x == 25.0f && step.y == 0.0f);
    }
    // Inside the step: aim at the leader itself.
    {
        ai::OpenFollowStep step = ai::OpenFollowStepToward(0.0f, 0.0f, 10.0f, 0.0f);
        CHECK(step.x == 10.0f && step.y == 0.0f);
    }
    // No sight: no step (never cut through a wall).
    CHECK(!ai::OpenFollowStepAllowed(10.0f, 0.0f, false));
    // Too far for one step, or off the floor: no step.
    CHECK(!ai::OpenFollowStepAllowed(30.0f, 0.0f, true));
    CHECK(!ai::OpenFollowStepAllowed(10.0f, 10.0f, true));
    CHECK(!ai::OpenFollowStepAllowed(10.0f, -10.0f, true));
    // Bound exactly on the limit is allowed.
    CHECK(ai::OpenFollowStepAllowed(25.0f, 8.0f, true));
    // Leader beyond 60 yd or far above: the normal follow owns it.
    CHECK(!ai::OpenFollowStepLeaderNear(70.0f, 0.0f));
    CHECK(!ai::OpenFollowStepLeaderNear(20.0f, 20.0f));

    // Headway: steps that close the gap keep the rescue alive...
    {
        OpenFollowStepHeadway headway;
        CHECK(headway.Stepped(40.0f, 30.0f));
        CHECK(headway.Stepped(30.0f, 20.0f));
        CHECK(headway.Stepped(20.0f, 10.0f));
    }
    // ...three in a row without 5 yd of headway stop it.
    {
        OpenFollowStepHeadway headway;
        CHECK(headway.Stepped(40.0f, 39.0f));
        CHECK(headway.Stepped(39.0f, 38.5f));
        CHECK(!headway.Stepped(38.5f, 38.0f));
    }

    if (failures)
    {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "test_open_follow_step_policy: all checks passed\n";
    return 0;
}
