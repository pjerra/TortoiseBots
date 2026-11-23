// Core-free decisions of the host fishing errand: the phase walk of one
// catch (walk to the spot, cast, wait for the splash, use the bobber, take
// the loot) and the timeouts that end it. Standalone-tested by
// forever/tools/test_fishing_host.cpp; TbHostFishing.cpp supplies the checks.
#pragma once

#include "playerbot/policy/loot/FishingSpotPolicy.h"

#include <cstdint>

namespace tbhost
{
namespace fishing
{

enum class Phase : std::uint8_t
{
    Idle,
    Walking,
    Casting,      // the next step sends the fishing cast
    CastingWait,  // the cast was sent; wait for the channel or the bobber
    BobberWait,   // the bobber is out; wait for the splash
    Looting,      // the bobber was used; take the loot
};

enum class Action : std::uint8_t
{
    None,
    MoveToSpot,
    Cast,
    UseBobber,
    TakeLoot,
    Done,
    Fail,
};

struct Facts
{
    Phase phase = Phase::Idle;
    bool inWorld = true;
    bool alive = true;
    bool inCombat = false;
    bool hasSpot = false;
    bool channelingFishing = false;
    bool hasBobber = false;
    bool bobberReady = false;
    bool bobberUsed = false;
    bool lootAvailable = false;
    bool castSent = false;
    std::uint8_t casts = 0;       // catches cast in this errand
    std::uint8_t lootTries = 0;   // takes attempted from the open bobber
    float distanceToSpot = 0.0f;
    std::uint32_t phaseMs = 0;
    std::uint32_t castMs = 0;     // since the last cast was sent
    std::uint32_t itemsGained = 0;
};

struct Decision
{
    Action action = Action::None;
    Phase next = Phase::Idle;
    char const* fail = nullptr;   // set when action is Fail
};

// One transition. `Casting` always asks for a cast; the caller increments
// `casts` and moves to CastingWait. A failed cast or a lost bobber retries up
// to three times, then the errand fails so the order tick records the miss.
inline Decision Step(Facts const& f)
{
    if (!f.inWorld)
        return { Action::Fail, Phase::Idle, "not-in-world" };
    if (!f.alive)
        return { Action::Fail, Phase::Idle, "dead" };
    if (f.inCombat)
        return { Action::Fail, Phase::Idle, "combat" };
    if (!f.hasSpot)
        return { Action::Fail, Phase::Idle, "no-water" };

    switch (f.phase)
    {
        case Phase::Walking:
            if (f.distanceToSpot <= 12.0f)
                return { Action::None, Phase::Casting, nullptr };
            // The spot pick may be hundreds of yards away on foot.
            if (f.phaseMs > 240000)
                return { Action::Fail, Phase::Idle, "unreachable" };
            return { Action::MoveToSpot, Phase::Walking, nullptr };

        case Phase::Casting:
            return { Action::Cast, Phase::CastingWait, nullptr };

        case Phase::CastingWait:
            if (f.channelingFishing || f.hasBobber || f.lootAvailable)
                return { Action::None, Phase::BobberWait, nullptr };
            if (f.castMs >= 12000)
            {
                if (f.casts >= 3)
                    return { Action::Fail, Phase::Idle, "no-cast" };
                return { Action::Cast, Phase::CastingWait, nullptr };
            }
            return { Action::None, Phase::CastingWait, nullptr };

        case Phase::BobberWait:
            if (f.lootAvailable)
                return { Action::TakeLoot, Phase::Looting, nullptr };
            if (f.hasBobber && f.bobberReady)
                return { Action::UseBobber, Phase::BobberWait, nullptr };
            if (f.bobberUsed && !f.lootAvailable && f.castMs >= 4000)
            {
                if (f.casts >= 3)
                    return { Action::Fail, Phase::Idle, "no-catch" };
                return { Action::Cast, Phase::CastingWait, nullptr };
            }
            if (!f.hasBobber && f.castMs >= 12000)
            {
                if (f.casts >= 3)
                    return { Action::Fail, Phase::Idle, "no-catch" };
                return { Action::Cast, Phase::CastingWait, nullptr };
            }
            // A bobber that never splashed or never despawned must not wedge
            // the errand: the channel is long over by then.
            if (f.castMs >= 40000)
            {
                if (f.casts >= 3)
                    return { Action::Fail, Phase::Idle, "no-catch" };
                return { Action::Cast, Phase::CastingWait, nullptr };
            }
            return { Action::None, Phase::BobberWait, nullptr };

        case Phase::Looting:
            if (f.itemsGained > 0)
                return { Action::Done, Phase::Idle, nullptr };
            if (f.lootAvailable && f.lootTries < 2)
                return { Action::TakeLoot, Phase::Looting, nullptr };
            if (f.lootAvailable)
                return { Action::Fail, Phase::Idle, "bags-full" };
            if (f.castMs >= 40000)
            {
                if (f.casts >= 3)
                    return { Action::Fail, Phase::Idle, "no-catch" };
                return { Action::Cast, Phase::CastingWait, nullptr };
            }
            return { Action::None, Phase::Looting, nullptr };

        case Phase::Idle:
        default:
            return { Action::Fail, Phase::Idle, "idle" };
    }
}

// Idle (masterless) fishing is one catch at a time inside the travelled
// side-activity budget: one session per hour, at most 5 casts / 5 minutes.
// These wrappers keep the host tick on the same numbers the legacy fish
// action uses (ai/playerbot/policy/loot/FishingSpotPolicy.h).
inline bool IdleSessionStartable(std::uint32_t sessionStartMs, std::uint32_t sessionEndMs,
    int sessionCasts, std::uint32_t nowMs)
{
    return ai::FishingSessionStartable(sessionStartMs, sessionEndMs, sessionCasts, nowMs);
}

inline bool IdleSessionOver(std::uint32_t sessionStartMs, int sessionCasts, std::uint32_t nowMs)
{
    return ai::FishingSessionExpired(sessionStartMs, sessionCasts, nowMs);
}

// A failed water search parks the bot for 15 minutes instead of re-scanning
// the terrain every tick; a running search keeps the 15 s throttle.
inline bool IdleSearchAllowed(std::uint32_t lastSearchMs, std::uint32_t noWaterAtMs, std::uint32_t nowMs)
{
    if (noWaterAtMs != 0 && (nowMs - noWaterAtMs) < ai::FISH_NO_WATER_RETRY_MS)
        return false;
    return ai::FishingSearchAllowed(lastSearchMs, nowMs);
}

// A fishing hole wins over open water; a stand next to hostile spawns drops
// the whole pick (the spot is a place to stand still, not a fight).
inline bool IdleSpotChosen(bool holeFound, bool waterFound, bool guarded)
{
    return !guarded && (holeFound || waterFound);
}

} // namespace fishing
} // namespace tbhost
