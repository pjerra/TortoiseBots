// Standalone test of BG2's target order (ai/playerbot/policy/bg/BgTargetPolicy.h):
//   g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/test_bg_target_policy.cpp -o /tmp/test_bg_target_policy && /tmp/test_bg_target_policy
//
// The spec's cases (BATTLEGROUNDS-SPEC.md section 4, test_bg_target_policy): the enemy flag carrier at 35 yd beats a 10 % HP enemy at
// 10 yd; the leash drops a non-carrier chase at 41 yd; a bound bot assists its human's target.
#include "ai/playerbot/policy/bg/BgTargetPolicy.h"

#include <cstdio>
#include <vector>

namespace bg = TortoiseBots::bg;

static int g_checks = 0, g_fail = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static bg::BgTargetCandidate Enemy(std::uint32_t handle, float distance, float health = 100.0f)
{
    bg::BgTargetCandidate c;
    c.handle = handle;
    c.distanceYd = distance;
    c.healthPct = health;
    return c;
}

static bg::BgTargetPick Pick(std::vector<bg::BgTargetCandidate> const& list, bool bound = false, bool hasObjective = false)
{
    bg::BgTargetContext context;
    context.bound = bound;
    context.hasObjective = hasObjective;
    return bg::BgPickTarget(list, context);
}

int main()
{
    // Nothing to pick.
    CHECK(!Pick({}).found);

    // The spec's case: the carrier at 35 yd beats a 10 % HP enemy at 10 yd.
    {
        bg::BgTargetCandidate carrier = Enemy(1, 35.0f);
        carrier.isFlagCarrier = true;
        bg::BgTargetPick const pick = Pick({ Enemy(2, 10.0f, 10.0f), carrier });
        CHECK(pick.found && pick.handle == 1 && pick.rule == bg::BgTargetRule::FlagCarrier);
    }
    // 40 yd is the edge of the carrier rule, 41 yd is outside it (the weakest enemy in 30 yd then wins).
    {
        bg::BgTargetCandidate carrier = Enemy(1, 40.0f);
        carrier.isFlagCarrier = true;
        CHECK(Pick({ Enemy(2, 10.0f, 10.0f), carrier }).handle == 1);
        carrier.distanceYd = 41.0f;
        bg::BgTargetPick const pick = Pick({ Enemy(2, 10.0f, 10.0f), carrier });
        CHECK(pick.found && pick.handle == 2 && pick.rule == bg::BgTargetRule::Weakest);
    }

    // An enemy hitting our carrier or one of our healers is taken before an enemy healer and the weakest enemy.
    {
        bg::BgTargetCandidate hitter = Enemy(1, 25.0f);
        hitter.hitsOurCarrier = true;
        bg::BgTargetCandidate healer = Enemy(2, 5.0f);
        healer.isHealer = true;
        bg::BgTargetPick pick = Pick({ Enemy(3, 4.0f, 5.0f), healer, hitter });
        CHECK(pick.found && pick.handle == 1 && pick.rule == bg::BgTargetRule::Defend);
        hitter.hitsOurCarrier = false;
        hitter.hitsOurHealer = true;
        pick = Pick({ Enemy(3, 4.0f, 5.0f), healer, hitter });
        CHECK(pick.handle == 1 && pick.rule == bg::BgTargetRule::Defend);
        hitter.distanceYd = 41.0f;
        CHECK(Pick({ hitter }).found == false);
    }

    // An enemy healer inside 30 yd beats the weakest enemy; at 31 yd it is not taken by that rule.
    {
        bg::BgTargetCandidate healer = Enemy(2, 30.0f);
        healer.isHealer = true;
        bg::BgTargetPick pick = Pick({ Enemy(3, 4.0f, 5.0f), healer });
        CHECK(pick.found && pick.handle == 2 && pick.rule == bg::BgTargetRule::Healer);
        healer.distanceYd = 31.0f;
        CHECK(!Pick({ healer }).found);
        pick = Pick({ Enemy(3, 4.0f, 5.0f), healer });
        CHECK(pick.handle == 3 && pick.rule == bg::BgTargetRule::Weakest);
    }

    // The weakest enemy in 30 yd; a tie in health goes to the nearer one.
    {
        bg::BgTargetPick pick = Pick({ Enemy(1, 20.0f, 80.0f), Enemy(2, 25.0f, 30.0f), Enemy(3, 10.0f, 60.0f) });
        CHECK(pick.found && pick.handle == 2 && pick.rule == bg::BgTargetRule::Weakest);
        pick = Pick({ Enemy(1, 20.0f, 50.0f), Enemy(2, 10.0f, 50.0f) });
        CHECK(pick.handle == 2);
        CHECK(!Pick({ Enemy(1, 31.0f, 1.0f) }).found);
    }

    // My own attacker is the last resort, at any range.
    {
        bg::BgTargetCandidate attacker = Enemy(1, 45.0f);
        attacker.isMyAttacker = true;
        bg::BgTargetPick pick = Pick({ attacker });
        CHECK(pick.found && pick.handle == 1 && pick.rule == bg::BgTargetRule::MyAttacker);
        pick = Pick({ attacker, Enemy(2, 29.0f, 90.0f) });
        CHECK(pick.handle == 2 && pick.rule == bg::BgTargetRule::Weakest);
    }

    // The leash: a chase more than 40 yd from the objective is dropped, the carrier excepted, and someone hitting me is no chase.
    {
        bg::BgTargetCandidate far = Enemy(1, 10.0f, 10.0f);
        far.objectiveDistanceYd = 41.0f;
        CHECK(!Pick({ far }, false, true).found);
        CHECK(Pick({ far }, false, false).found);              // no objective, no leash
        far.objectiveDistanceYd = 40.0f;
        CHECK(Pick({ far }, false, true).found);
        far.objectiveDistanceYd = 200.0f;
        far.isMyAttacker = true;
        CHECK(Pick({ far }, false, true).found);
        far.isMyAttacker = false;
        far.isFlagCarrier = true;
        far.distanceYd = 38.0f;
        CHECK(Pick({ far }, false, true).found);
        // a dropped chase leaves the others
        bg::BgTargetCandidate chase = Enemy(2, 5.0f, 1.0f);
        chase.objectiveDistanceYd = 90.0f;
        bg::BgTargetCandidate near = Enemy(3, 20.0f, 90.0f);
        near.objectiveDistanceYd = 10.0f;
        CHECK(Pick({ chase, near }, false, true).handle == 3);
    }

    // A bound bot assists its human's target first, even before the carrier; a free bot has no human and ignores the mark.
    {
        bg::BgTargetCandidate carrier = Enemy(1, 10.0f);
        carrier.isFlagCarrier = true;
        bg::BgTargetCandidate mark = Enemy(2, 38.0f, 95.0f);
        mark.isMasterTarget = true;
        bg::BgTargetPick pick = Pick({ carrier, mark }, true);
        CHECK(pick.found && pick.handle == 2 && pick.rule == bg::BgTargetRule::MasterTarget);
        pick = Pick({ carrier, mark }, false);
        CHECK(pick.handle == 1 && pick.rule == bg::BgTargetRule::FlagCarrier);
        mark.distanceYd = 41.0f;
        CHECK(Pick({ carrier, mark }, true).handle == 1);
        // a bound bot has no objective leash on its human's target
        mark.distanceYd = 20.0f;
        mark.objectiveDistanceYd = 500.0f;
        CHECK(Pick({ mark }, true, true).found);
    }

    // The carrier rule picks the nearest carrier when two are listed (two flags are never carried by one side, but a stale list may hold both).
    {
        bg::BgTargetCandidate a = Enemy(1, 30.0f);
        a.isFlagCarrier = true;
        bg::BgTargetCandidate b = Enemy(2, 12.0f);
        b.isFlagCarrier = true;
        CHECK(Pick({ a, b }).handle == 2);
    }

    std::printf("%d checks, %d failed\n", g_checks, g_fail);
    return g_fail == 0 ? 0 : 1;
}
