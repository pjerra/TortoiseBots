#pragma once

#include <ctime>

// Pure policy: when does a pull that has not landed yet end?
//
// A pull stamps its clock when the order goes out (PullStrategy::RequestPull),
// but the walk to the mob only starts after that. A cross-room approach -
// Uldaman's dais down to Rockchewer 4851 (issue #910), a corner firing
// position, a re-pathed dungeon walk - can still be closing in when the flat
// 15 s pull window (PullStrategy::GetMaxPullTime) runs out. Ending the pull
// there wipes the pull target (PullEndAction's failure branch) although the
// tank was doing the right thing, and every reorder restarts the same clock
// at the same distance, so the pull can never land.
//
// The window therefore slides while the approach makes headway (the tank
// closes the distance or is moving toward the mob) and only a pull with no
// headway for a full window - or one past the hard total cap - ends. A wedged
// pull still ends: standing still with no progress expires exactly like
// before, and the total cap bounds even a shuffling one.
//
// Core-free, so tools/test_pull_reach_policy.cpp includes it directly.

namespace ai
{
    // How many base windows one pull may last at most, however healthy its
    // approach looks. Four windows of GetMaxPullTime() (60 s): a dungeon walk
    // gets there, a wedged pull never holds the party for good.
    inline constexpr int kPullApproachMaxWindows = 4;

    // The approach must close by this much to count as headway. Below it is
    // spline jitter on arrival, not progress.
    inline constexpr float kPullApproachHeadwayYd = 0.5f;

    struct PullApproachFacts
    {
        long elapsedOrderSec = 0;    // since the pull was requested
        long elapsedHeadwaySec = 0;  // since the approach last made headway
        long baseWindowSec = 15;     // PullStrategy::GetMaxPullTime()
        int maxWindows = kPullApproachMaxWindows;
    };

    // True when the un-landed pull is over: past the hard total cap, or a
    // full base window without headway. A walking tank keeps its target.
    inline bool PullApproachExpired(PullApproachFacts const& facts)
    {
        if (facts.maxWindows > 0 &&
            facts.elapsedOrderSec >= facts.baseWindowSec * facts.maxWindows)
            return true;
        return facts.elapsedHeadwaySec >= facts.baseWindowSec;
    }

    // True when this approach sample is headway: the tank closed in, or it is
    // moving while holding its ground (a step mid-spline). Moving away is
    // never headway - a detour that never closes re-anchors on its next
    // closing sample, and one that never closes ends the pull as before.
    inline bool PullApproachHeadway(float distanceNow, float distanceBefore,
        bool hasBefore, bool moving)
    {
        if (!hasBefore)
            return true;
        if (distanceNow < distanceBefore - kPullApproachHeadwayYd)
            return true;
        return moving && distanceNow <= distanceBefore;
    }
}
