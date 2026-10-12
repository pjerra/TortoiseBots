#pragma once

// Pure rules for the human-demand battleground backfill.
// Ported donor behaviour (mod-playerbots @ 79bd4281):
//   BGJoinAction::shouldJoinBg
//   (src/Ai/Base/Actions/BattleGroundJoinAction.cpp:290-305): for EACH team,
//   bots join while bots + players of that team are below TeamSize
//   (the template's max players per team). Both sides are filled.
// Adapted: the old backfill queued bots only for the faction with fewer
// waiting humans, so a solo human's own side never reached the core minimum
// (WSG 4, AB 5, AV 15) and no match formed. Here each side is filled toward
// the template team size, counting everyone already waiting on that side
// (humans and bots), on a fast fixed cadence while a human waits. No core
// includes: callers translate game state into plain inputs so the rules stay
// testable in tools/test_human_bg_demand_policy.cpp.

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

    // Fill target for one side: the template team size (donor TeamSize), never
    // below the core minimum so a misconfigured template still forms a match.
    inline std::uint32_t HumanBgSideTarget(std::uint32_t minPerTeam, std::uint32_t maxPerTeam)
    {
        return maxPerTeam > minPerTeam ? maxPerTeam : minPerTeam;
    }

    // Bots still wanted on one side: target minus everyone already waiting on
    // that side (humans and bots, not yet invited).
    inline std::uint32_t HumanBgSideNeed(std::uint32_t target, std::uint32_t waitingOnSide)
    {
        return waitingOnSide >= target ? 0 : target - waitingOnSide;
    }

    // Queue one more bot for this side in this check: it still needs bots and
    // its per-check share is not used up (keeps both sides growing together).
    inline bool HumanBgSideWantsBot(std::uint32_t need, std::uint32_t queuedThisCheck)
    {
        return need > 0 && queuedThisCheck < kHumanBgDemandPerSidePerCheck;
    }
}
