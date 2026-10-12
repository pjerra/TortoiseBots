#pragma once

#include <cstdint>

#include "DeathClusterPolicy.h"

// Gather errands (mining/herbalism) that pay. Live pool 2026-10-12: gather
// trips were a fifth of all bot-samples, yet only 24 of 793 trips gathered
// anything, the median trip lasted 48 s and 47 bots ping-ponged between node
// types (Mithril <-> Truesilver 11 times). Two rules close it:
//
// - A trip is judged at its own spawn point. The destination verdict looks at
//   whichever spawn of the entry is nearest the bot, so passing within sight
//   of another, empty spawn of the same node ended the trip half way and the
//   next pick walked the other node type. Until the bot can see its own point
//   only the static rules (skill, level) apply.
// - A spawn point the bot reached and saw empty (no spawned mining or herb
//   node on it) is remembered for about a
//   respawn, so the next pick walks to another spawn instead of back to it.
//
// Pure decision rules, no core includes: PlayerbotAI owns the per-bot list,
// TravelTarget and SetBestTarget only ask. The spot list reuses the
// death-spot slot rules (DeathClusterPolicy.h): wrap-safe WorldTimer ms
// expiry, free/expired slot first, else the oldest live one is replaced.
namespace ai
{
    std::uint32_t const kEmptyGatherSpots = 8; // empty spawn points kept per bot

    std::uint32_t const kEmptyGatherSpotMs = 15u * 60u * 1000u; // roughly one node respawn

    // Pooled ores and herbs share coordinates (Mithril/Truesilver on one
    // spot), so one radius covers every entry that spawns there; spawn points
    // of one node are further apart than this.
    float const kEmptyGatherSpotRadiusYd = 10.0f;

    // Whether the bot can judge its own gather point by sight: same map, and
    // close enough that the node scan (sight range around the bot) covers the
    // whole spot radius around the point, so a node standing there is seen.
    inline bool GatherPointInSight(bool sameMap, float distanceYd, float sightYd)
    {
        return sameMap && distanceYd + kEmptyGatherSpotRadiusYd <= sightYd;
    }

    // Whether an expired gather trip may record its point as empty: the bot
    // judged the point by sight, could still gather there (an expiry on skill
    // or level says nothing about the node) and no mining or herb node of any
    // entry stands on the spot (a pooled alternate counts as a node).
    inline bool ShouldMarkGatherSpotEmpty(bool pointInSight, bool stillPossible, bool nodeAtPoint)
    {
        return pointInSight && stillPossible && !nodeAtPoint;
    }
}
