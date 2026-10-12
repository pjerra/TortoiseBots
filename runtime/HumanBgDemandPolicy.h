#pragma once

// Pure rules for the human-demand battleground backfill.
// Ported donor behaviour (mod-playerbots @ 79bd4281):
//   BGJoinAction::shouldJoinBg
//   (src/Ai/Base/Actions/BattleGroundJoinAction.cpp:290-305): for EACH team,
//   bots join while bots + players of that team are below a target. Both
//   sides are filled.
// Adapted: the old backfill queued bots only for the faction with fewer
// waiting humans, so a solo human's own side never reached the core minimum
// (WSG 4, AB 5, AV 15) and no match formed. Here each side is filled, counting
// everyone already waiting on that side (humans and bots), on a fast fixed
// cadence while a human waits. Unlike the donor, the target is the core's
// match minimum plus a small margin, not the team size: the core starts a
// match as soon as both sides reach the minimum, so bots queued past that
// would be left waiting and could form a second match with no human in it
// (an AV queue filled to 40 a side leaves 25 a side behind). Bots left behind
// stay below the minimum and join the human's match through the core's
// free-slot fill. No core includes: callers translate game state into plain
// inputs so the rules stay testable in tools/test_human_bg_demand_policy.cpp.

#include <cstdint>

namespace TortoiseBots
{
    // Human-demand check cadence, independent of RandomBotBgQueueInterval
    // (which keeps pacing the autonomous bot-only seeder). It only does work
    // while a real player waits in a WSG/AB/AV queue.
    constexpr std::uint32_t kHumanBgDemandIntervalMs = 5000;

    // Bots queued per check over all demand buckets, and per side of one
    // bucket. Five per side per 5 s check: a solo WSG queue (3 own + 4 enemy
    // to reach 4v4) forms on the first check, AV (14 + 15) in about 15 s.
    constexpr std::uint32_t kHumanBgDemandMaxPerCheck = 10;
    constexpr std::uint32_t kHumanBgDemandPerSidePerCheck = 5;

    // Extra bots per side above the match minimum. It covers a bot that drops
    // out of the queue between checks; it is clamped below the minimum so
    // the bots left over after the match forms can never start a second one.
    constexpr std::uint32_t kHumanBgDemandMargin = 1;

    // Fill target for one side: the core's match minimum (raised to
    // queueFloor when the core needs more waiting per side before it builds
    // a match, e.g. Alterac.MinPlayersInQueue) plus the margin, never above
    // the template team size unless the minimum itself is.
    inline std::uint32_t HumanBgSideTarget(std::uint32_t minPerTeam, std::uint32_t maxPerTeam,
        std::uint32_t queueFloor = 0)
    {
        std::uint32_t base = queueFloor > minPerTeam ? queueFloor : minPerTeam;
        std::uint32_t margin = minPerTeam > kHumanBgDemandMargin ? kHumanBgDemandMargin
            : (minPerTeam ? minPerTeam - 1 : 0);
        std::uint32_t target = base + margin;
        if (maxPerTeam >= base && target > maxPerTeam)
            target = maxPerTeam;
        return target;
    }

    // Bots still wanted on one side: target minus everyone already waiting on
    // that side (humans and bots, not yet invited).
    inline std::uint32_t HumanBgSideNeed(std::uint32_t target, std::uint32_t waitingOnSide)
    {
        return waitingOnSide >= target ? 0 : target - waitingOnSide;
    }

    // Queue one more queue entry of entrySize players (1 solo, the group size
    // for a bot group leader) for this side in this check: it fits in what
    // the side still needs, so a group never pushes the side past its target,
    // and the side's per-check share is not used up (both sides grow
    // together).
    inline bool HumanBgSideWantsBot(std::uint32_t need, std::uint32_t queuedThisCheck,
        std::uint32_t entrySize = 1)
    {
        if (!entrySize)
            entrySize = 1;
        return need >= entrySize && queuedThisCheck < kHumanBgDemandPerSidePerCheck;
    }
}
