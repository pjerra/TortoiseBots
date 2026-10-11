#pragma once

// Pure rules for the pool-bot flee step in MovementAction::Flee.
//
// Pool bots (no real player master) used to flee with a MoveChase at flee
// range. The core chase generator holds position while a standing target is
// within 1.4x that range ("prevent redundant micro-movement"), so the flee
// reported success every tick without moving and nothing else could run.
// The step below is a checked point move instead (mod-playerbots @ 79bd4281
// MovementAction::MoveAway shape): straight away from the target first, then
// fanned out to either side in 22.5 degree steps up to 90 degrees.
#include <cstdint>

namespace ai
{
    constexpr int kPoolFleeHeadingCount = 9;

    // Heading offsets from "straight away from the target", in try order.
    inline int PoolFleeHeadingOffsets(float out[kPoolFleeHeadingCount])
    {
        float const step = 0.3926990817f; // PI/8
        int count = 0;
        out[count++] = 0.0f;
        for (int i = 1; i <= 4; ++i)
        {
            out[count++] = step * i;
            out[count++] = -step * i;
        }
        return count;
    }

    // After a search that found no spot, flee fails at once for this long:
    // the bot fights instead of paying the candidate checks every tick.
    constexpr std::uint32_t kPoolFleeNoSpotBackoffMs = 2000;

    // noSpotMs == 0 means no failed search is remembered.
    inline bool PoolFleeSearchBackedOff(std::uint32_t noSpotMs, std::uint32_t nowMs)
    {
        return noSpotMs != 0 && std::uint32_t(nowMs - noSpotMs) < kPoolFleeNoSpotBackoffMs;
    }

    // Extra yards kept from an unpulled mob's aggro radius.
    constexpr float kPoolFleeAggroMargin = 2.0f;

    // An unpulled hostile mob vetoes a landing that is inside its aggro
    // radius (plus margin) and closer to it than the bot stands now, so a
    // bot already inside a radius may still step away from that mob.
    inline bool PoolFleeLandingWakesMob(float mobToLanding, float mobToBot, float aggroRadius)
    {
        return mobToLanding < aggroRadius + kPoolFleeAggroMargin && mobToLanding < mobToBot;
    }

    // A mob too far for one step to bring the bot into its radius is not
    // worth checking per candidate.
    inline bool PoolFleeMobInReach(float mobToBot, float aggroRadius, float stepLength)
    {
        return mobToBot < aggroRadius + kPoolFleeAggroMargin + stepLength;
    }
}
