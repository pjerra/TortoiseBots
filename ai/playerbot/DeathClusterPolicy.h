#pragma once

#include <cstdint>

namespace ai
{
    // Death-cluster area avoidance (issue #398): the per-kind blacklist in
    // PlayerbotAI::OnDeath cannot break a camp loop whose mobs rotate as they
    // kill the bot (measured: median 7 distinct killer kinds per loop bot, 728
    // DeathClusterEscape rows across 191 bots with dying continuing). The
    // first escape already escalates from "leave this kind alone" to "leave
    // this hunting ground alone": the spot around the death is avoided, so
    // grind and quest-objective picks inside it are refused and the bot walks
    // elsewhere. Waiting for a second escape cost three more deaths per loop
    // (Oct 2026 pool: 240 bots in a graveyard camp loop, 1,090 deaths within
    // 300 yd/15 min of a spirit-healer revive), and the kind blacklist the
    // first escape buys cannot help when the killers rotate.
    //
    // Pure decision rule, no core includes: PlayerbotAI owns the per-bot
    // avoidance list, SetBestTarget only asks IsDeathSpotAvoided. All clocks
    // are WorldTimer ms (uint32, wraps every ~49 d); every window/expiry
    // comparison below uses unsigned subtraction, which stays correct across
    // the wrap (a direct nowMs >= expiryMs read goes true the instant a
    // wrapped expiry is stored).
    std::uint32_t const kDeathAvoidEscapes = 1; // escapes inside the window that trigger avoidance

    std::uint32_t const kDeathAvoidWindowMs = 60u * 60u * 1000u; // escapes this far apart belong to one loop

    float const kDeathAvoidRadiusYd = 300.0f; // the hunting-ground size the cluster rule measures

    std::uint32_t const kDeathAvoidMs = 60u * 60u * 1000u; // how long the spot stays avoided (outlasts the 30-min kind blacklist)

    // Starter valleys are only ~400-500 yd across and a level <= 5 bot cannot
    // walk out of its own (1500 yd cap, area-level gate), so a full-size spot
    // would swallow every eligible destination and strand it for the hour.
    // Lowbies avoid a smaller camp for a shorter while; a ding clears the
    // list outright (see ClearDeathAvoidanceOnLevelUp), since the band that
    // killed them no longer applies.
    float const kDeathAvoidRadiusLowYd = 100.0f;

    std::uint32_t const kDeathAvoidLowMs = 15u * 60u * 1000u;

    std::uint32_t const kDeathAvoidLowLevel = 5;

    std::uint32_t const kDeathAvoidSpots = 3; // avoided spots kept per bot; the oldest live one is replaced

    // Radius and duration for the bot's current level.
    inline float DeathAvoidRadiusYd(std::uint32_t botLevel)
    {
        return botLevel <= kDeathAvoidLowLevel ? kDeathAvoidRadiusLowYd : kDeathAvoidRadiusYd;
    }

    inline std::uint32_t DeathAvoidDurationMs(std::uint32_t botLevel)
    {
        return botLevel <= kDeathAvoidLowLevel ? kDeathAvoidLowMs : kDeathAvoidMs;
    }

    // Escape count after one more escape now: a repeat inside the window
    // extends the streak, an escape after the window starts a fresh one.
    inline std::uint32_t NextDeathEscapeCount(std::uint32_t prevCount, std::uint32_t nowMs, std::uint32_t lastEscapeMs,
        std::uint32_t windowMs = kDeathAvoidWindowMs)
    {
        if (prevCount > 0 && (nowMs - lastEscapeMs) <= windowMs)
            return prevCount + 1;
        return 1;
    }

    inline bool DeathAvoidanceEscalated(std::uint32_t escapeCount, std::uint32_t threshold = kDeathAvoidEscapes)
    {
        return escapeCount >= threshold;
    }

    struct DeathAvoidSpot
    {
        std::uint32_t mapId = 0;
        float x = 0.0f;
        float y = 0.0f;
        std::uint32_t expiryMs = 0; // 0 = slot free
        float radiusYd = kDeathAvoidRadiusYd;
    };

    // Wrap-safe expiry: remaining = expiryMs - nowMs in unsigned arithmetic;
    // a wrapped (already passed) expiry yields a huge remainder, never <= 0.
    // An int32 cast reads the sign back: remaining > 0 means still avoided.
    inline bool DeathSpotActive(std::uint32_t nowMs, std::uint32_t expiryMs)
    {
        if (expiryMs == 0)
            return false;
        return static_cast<std::int32_t>(expiryMs - nowMs) > 0;
    }

    // Whether map position (mapId, x, y) falls inside one active avoidance
    // spot. Radius compares squared like the cluster rule.
    inline bool IsDeathPositionAvoided(std::uint32_t mapId, float x, float y, std::uint32_t nowMs,
        DeathAvoidSpot const* spots, std::uint32_t spotCount)
    {
        for (std::uint32_t i = 0; i < spotCount; ++i)
        {
            DeathAvoidSpot const& spot = spots[i];
            if (!DeathSpotActive(nowMs, spot.expiryMs))
                continue;
            if (mapId != spot.mapId)
                continue;
            float const dx = x - spot.x;
            float const dy = y - spot.y;
            if (dx * dx + dy * dy <= spot.radiusYd * spot.radiusYd)
                return true;
        }
        return false;
    }

    // Record one more avoided spot: reuse an expired/free slot when there is
    // one, otherwise replace the oldest live spot so a second camp cannot
    // erase the first while a third still rotates in.
    inline void AddDeathAvoidSpot(DeathAvoidSpot* spots, std::uint32_t spotCount, std::uint32_t mapId, float x,
        float y, std::uint32_t nowMs, std::uint32_t durationMs, float radiusYd)
    {
        std::uint32_t slot = spotCount;
        for (std::uint32_t i = 0; i < spotCount; ++i)
        {
            if (!DeathSpotActive(nowMs, spots[i].expiryMs))
            {
                slot = i;
                break;
            }
        }
        if (slot == spotCount)
        {
            slot = 0;
            for (std::uint32_t i = 1; i < spotCount; ++i)
            {
                // Oldest = smallest remaining life (unsigned compare would
                // flip across the wrap; both are live so signed is safe).
                if (static_cast<std::int32_t>(spots[i].expiryMs - nowMs) <
                    static_cast<std::int32_t>(spots[slot].expiryMs - nowMs))
                    slot = i;
            }
        }
        spots[slot].mapId = mapId;
        spots[slot].x = x;
        spots[slot].y = y;
        spots[slot].expiryMs = nowMs + durationMs;
        spots[slot].radiusYd = radiusYd;
    }

    // Quest purposes (bitmask enum) share one bitmask with the composite
    // QuestAllObjective flag, so test with & instead of a range: a composite
    // purpose id would fail a >=/<= check. questMask covers the objectives
    // and the quest giver/taker NPCs: a hand-in inside a camp the bot keeps
    // dying in is a death loop too (Rane Yorick at Ivar Patch: 38 level 10-12
    // bots died 837 times walking to him through the Moonrage worgens).
    // gatherMask covers mining/herbalism nodes the same way: a node inside
    // the avoided camp is re-mined otherwise (Khuceburn: 7 deaths in 12 min
    // to Searing Hatchlings on the same Gold Vein, Oct 2026 pool).
    // Services (vendor, trainer, repair) and fishing stay walkable.
    inline bool IsDeathGatedPurpose(std::uint32_t purposeId, std::uint32_t grindId, std::uint32_t questMask,
        std::uint32_t gatherMask = 0)
    {
        return purposeId == grindId || (purposeId & questMask) != 0 || (purposeId & gatherMask) != 0;
    }

    // Errand death park: the rules above key on the spot the bot died at, so
    // a bot that dies on the ROUTE to a far hand-in, node or trainer keeps the
    // errand - the destination point is nowhere near the death - and the next
    // pick hands it the same nearest destination along the same path. Oct 2026
    // pool (500 bots, 27.8 min): 80 % of all deaths were a bot dying 2+ times
    // on one errand (Molor 19 deaths in 26 min on the way to one taker). A
    // second death on the same destination (purpose + entry) inside the window
    // parks that destination for the bot; the pick then lands elsewhere. This
    // is the donor's "give the target up after N failed attempts" (TravelTarget
    // retry cap -> cooldown) with a death counted as the failed attempt.
    // Grind is left to the lethal-kind and death-cluster rules above.
    std::uint32_t const kErrandDeathsToPark = 2;

    std::uint32_t const kErrandDeathWindowMs = 15u * 60u * 1000u; // deaths this far apart belong to one errand

    std::uint32_t const kErrandParkMs = 30u * 60u * 1000u; // how long the destination stays parked

    std::uint32_t const kErrandParkSlots = 4; // parked destinations kept per bot; the oldest live one is replaced

    struct ErrandPark
    {
        std::uint32_t purposeId = 0;
        std::int32_t entry = 0;
        std::uint32_t expiryMs = 0; // 0 = slot free
    };

    // Death count on the errand after one more death now: the same
    // destination inside the window extends the streak, anything else starts
    // a fresh one. Wrap-safe like NextDeathEscapeCount.
    inline std::uint32_t NextErrandDeathCount(std::uint32_t prevCount, std::uint32_t prevPurposeId,
        std::int32_t prevEntry, std::uint32_t purposeId, std::int32_t entry, std::uint32_t nowMs,
        std::uint32_t lastDeathMs, std::uint32_t windowMs = kErrandDeathWindowMs)
    {
        if (prevCount > 0 && prevPurposeId == purposeId && prevEntry == entry && (nowMs - lastDeathMs) <= windowMs)
            return prevCount + 1;
        return 1;
    }

    // Whether destination (purposeId, entry) is parked for this bot now.
    inline bool IsErrandParked(std::uint32_t purposeId, std::int32_t entry, std::uint32_t nowMs,
        ErrandPark const* parks, std::uint32_t parkCount)
    {
        for (std::uint32_t i = 0; i < parkCount; ++i)
        {
            if (parks[i].purposeId == purposeId && parks[i].entry == entry && DeathSpotActive(nowMs, parks[i].expiryMs))
                return true;
        }
        return false;
    }

    // Park one more destination: refresh its own slot when it is already
    // parked, else take a free/expired slot, else replace the oldest live one.
    inline void AddErrandPark(ErrandPark* parks, std::uint32_t parkCount, std::uint32_t purposeId, std::int32_t entry,
        std::uint32_t nowMs, std::uint32_t durationMs = kErrandParkMs)
    {
        std::uint32_t slot = parkCount;
        for (std::uint32_t i = 0; i < parkCount && slot == parkCount; ++i)
        {
            if (parks[i].purposeId == purposeId && parks[i].entry == entry && DeathSpotActive(nowMs, parks[i].expiryMs))
                slot = i;
        }
        for (std::uint32_t i = 0; i < parkCount && slot == parkCount; ++i)
        {
            if (!DeathSpotActive(nowMs, parks[i].expiryMs))
                slot = i;
        }
        if (slot == parkCount)
        {
            slot = 0;
            for (std::uint32_t i = 1; i < parkCount; ++i)
            {
                if (static_cast<std::int32_t>(parks[i].expiryMs - nowMs) <
                    static_cast<std::int32_t>(parks[slot].expiryMs - nowMs))
                    slot = i;
            }
        }
        parks[slot].purposeId = purposeId;
        parks[slot].entry = entry;
        parks[slot].expiryMs = nowMs + durationMs;
        if (parks[slot].expiryMs == 0) // 0 marks a free slot; nudge the rare wrap onto it
            parks[slot].expiryMs = 1;
    }
}
