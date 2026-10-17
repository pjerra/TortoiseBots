#pragma once

// Pure rules for walking a short straight step when the open-world follow
// stands still on a navmesh gap.
//
// The server's follow movement silently gives up when the navmesh path to the
// leader is NOPATH: it launches no spline while the leader is in sight, so
// the bot is shown "moving" with no motion and FollowAction never becomes
// useful again (Windhorn Canyon entrance, Thousand Needles, 2026-10: portal
// trigger 5611 at (-5117.5, -3534.3) sits on grid [41,38] whose vmap tile
// 001_41_38.vmtile is missing from the shipped data while the mmap tile
// 0014138.mmtile exists; the core follow stands still there in sight of the
// leader and owned bots never recover because the stuck rescues only run for
// masterless pool bots).
//
// The step below reuses the DungeonFollowStallWatch clock (2 s still while
// behind, paced walks, give-up) but runs in the open world and, instead of a
// navmesh walk that would fail the same way, walks a short straight line the
// way `.bot cmd move <x> <y> <z> direct` does (RemoteCommanderPolicy.h): a
// MovePoint without pathfinding toward the leader, at most
// kOpenFollowStepYd per step, only while the leader is seen (no step cuts
// through a wall) and on roughly the same floor (no step off a cliff). Each
// step must close the gap, so the walk cannot loop; when the leader leaves
// sight or range the normal follow takes over again.
//
// No core includes; tested in tools/test_open_follow_step_policy.cpp.

#include <cmath>
#include <cstdint>

namespace ai
{
    // One straight step covers at most this far (2D): a short hop like the
    // `direct` command's kDirectMaxDistance, not a cross-zone walk.
    constexpr float kOpenFollowStepYd = 25.0f;

    // The leader must be within this 2D range to step: beyond it the normal
    // follow (or a regroup) owns the movement, not a blind walk.
    constexpr float kOpenFollowStepLeaderYd = 60.0f;

    // The step stays on roughly the same floor: a bigger height gap is a
    // ledge or a cliff the bot must not walk off.
    constexpr float kOpenFollowStepVerticalYd = 8.0f;

    // Each step must end at least this much closer to the leader than the
    // step started, or the rescue stops: without headway the bot would pace
    // back and forth across the same gap.
    constexpr float kOpenFollowStepHeadwayYd = 5.0f;

    // After this many steps in a row without headway the rescue gives up for
    // the watch's give-up window: the gap is not step-sized.
    constexpr uint32_t kOpenFollowStepMaxNoHeadway = 3;

    // A straight step toward the leader: horizontal yards, height difference
    // (either sign) and whether the bot sees the leader. A bound exactly on
    // the limit is allowed.
    inline bool OpenFollowStepAllowed(float horizontal, float vertical, bool seesLeader)
    {
        if (!seesLeader)
            return false;
        if (!std::isfinite(horizontal) || !std::isfinite(vertical))
            return false;
        if (horizontal > kOpenFollowStepYd || std::fabs(vertical) > kOpenFollowStepVerticalYd)
            return false;
        return true;
    }

    // The leader is close enough (and on a sane floor) to step toward at all.
    // Far or far above/below: the normal follow owns it.
    inline bool OpenFollowStepLeaderNear(float horizontal, float vertical)
    {
        if (!std::isfinite(horizontal) || !std::isfinite(vertical))
            return false;
        if (horizontal > kOpenFollowStepLeaderYd || std::fabs(vertical) > kOpenFollowStepVerticalYd)
            return false;
        return true;
    }

    // Step target: the point kOpenFollowStepYd toward the leader (2D), kept at
    // the bot's own height so the walk stays on its floor.
    struct OpenFollowStep
    {
        float x = 0.0f;
        float y = 0.0f;
    };

    inline OpenFollowStep OpenFollowStepToward(float botX, float botY, float leaderX, float leaderY)
    {
        OpenFollowStep step{ leaderX, leaderY };
        float const dx = leaderX - botX;
        float const dy = leaderY - botY;
        float const dist = std::sqrt(dx * dx + dy * dy);
        if (dist > kOpenFollowStepYd && dist > 0.0f)
        {
            step.x = botX + dx / dist * kOpenFollowStepYd;
            step.y = botY + dy / dist * kOpenFollowStepYd;
        }
        return step;
    }

    // Headway bookkeeping: counts steps in a row whose end was not at least
    // kOpenFollowStepHeadwayYd closer to the leader than the previous step's
    // start. True while the rescue may keep stepping.
    struct OpenFollowStepHeadway
    {
        float lastStartDist = 0.0f;
        bool hasStart = false;
        uint32_t noHeadway = 0;

        void Reset() { *this = OpenFollowStepHeadway(); }

        // `startDist`: bot-to-leader distance before the step just taken;
        // `endDist`: after it. Returns false when the rescue must stop.
        bool Stepped(float startDist, float endDist)
        {
            if (!hasStart)
            {
                hasStart = true;
                lastStartDist = startDist;
            }
            if (lastStartDist - endDist >= kOpenFollowStepHeadwayYd)
                noHeadway = 0;
            else if (++noHeadway >= kOpenFollowStepMaxNoHeadway)
                return false;
            lastStartDist = startDist;
            return true;
        }
    };
}
