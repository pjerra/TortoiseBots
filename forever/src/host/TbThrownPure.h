// S3 (throws and falls): the decision half of the host's thrown-flight watcher. Core-free: it sees only the
// movement generator's type, the spline's state, where the bot is and the liquid at the landing point, so
// forever/tools/test_thrown.cpp compiles it with a bare g++.
//
// One State per bot. Observe() is called every world tick for every bot in the world: a flight starts when the
// bot's current movement generator is EFFECT_MOTION and its spline is not done, and ends on the first later tick
// where either is no longer true. The landing is reported exactly once; the apex is the highest z seen during the
// flight (the server's own rule, Player::UpdateFallInformationIfNeed).
//
// A flight that ends somewhere else is not a landing (a `.tb tele`, a summon, a host catch-up to another map moves
// the bot mid-flight): the core would charge the fall from the old apex at the new spot, possibly across maps.
// A landing in liquid is a swim start on a client, not CMSG_MOVE_FALL_LAND, so it is not charged either.
#pragma once

#include <cstdint>

namespace TortoiseBots
{
namespace kb
{
enum class Kind : uint8_t
{
    None,     // not flying, nothing to do
    Start,    // the flight began this tick
    Flying,   // still in the air: keep the engine's wait out
    Land      // the flight ended this tick: fall damage from apexZ to landZ when chargesFall
};

// What one world tick shows of the bot. inWater is a callable, asked only when the flight ends (a plain tick costs
// no terrain read).
struct Sample
{
    bool effectMotion = false;   // the current movement generator is EFFECT_MOTION_TYPE (a knockback or another core flight)
    bool splineDone = true;      // the movespline is absent or Finalized()
    uint32_t mapId = 0;
    int32_t instanceId = 0;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    uint32_t nowMs = 0;
    uint32_t remainingMs = 0;    // the spline's remaining duration, 0 when there is none
};

struct State
{
    bool flying = false;
    bool landed = true;          // a Land is reported once and then the state is quiet until the next Start
    float apexZ = 0.0f;          // highest z of the flight, start included
    uint32_t jumpUntilMs = 0;    // absolute ms the engine's IsJumping must hold; only raised, never lowered
    // The flight's map and the last place it was seen in the air: a landing away from them is a teleport's, not this flight's.
    uint32_t mapId = 0;
    int32_t instanceId = 0;
    float x = 0.0f;
    float y = 0.0f;
};

struct Step
{
    Kind kind = Kind::None;
    float apexZ = 0.0f;
    float landZ = 0.0f;
    uint32_t jumpUntilMs = 0;
    bool chargesFall = false;    // Land only: the core's fall damage applies (same map and instance, at the flight's own spot, dry)
};

// How far a landing may be from the last airborne sample and still be this flight's.
constexpr float kLandingToleranceYd = 5.0f;

// A deadline is raised, never lowered, and 0 means "none set". A plain timestamp compare is wrong across the uint32
// wrap (24.8 days of uptime) and misses the 0 case entirely, which leaves the bot's jump time unset on a server up
// that long.
inline bool RaisesDeadline(uint32_t currentMs, uint32_t wantedMs)
{
    return currentMs == 0 || int32_t(wantedMs - currentMs) > 0;
}

// The engine is held off for the flight plus one second (the legacy knockback wait); a refresh may extend that deadline
// but never shorten it.
template <class InWater>
inline Step Observe(State& state, Sample const& s, InWater&& inWater)
{
    Step step;
    bool const flyingNow = s.effectMotion && !s.splineDone;
    if (flyingNow)
    {
        bool const start = !state.flying;
        uint32_t const until = s.nowMs + s.remainingMs + 1000;
        if (start)
        {
            state.flying = true;
            state.landed = false;
            state.apexZ = s.z;
            state.mapId = s.mapId;          // the throw's map: a sample in another map is a teleport's, not a landing's
            state.instanceId = s.instanceId;
            state.jumpUntilMs = until;
        }
        else
        {
            if (s.z > state.apexZ)
                state.apexZ = s.z;
            if (RaisesDeadline(state.jumpUntilMs, until))
                state.jumpUntilMs = until;
        }
        state.x = s.x;
        state.y = s.y;
        step.kind = start ? Kind::Start : Kind::Flying;
        step.apexZ = state.apexZ;
        step.landZ = s.z;
        step.jumpUntilMs = state.jumpUntilMs;
        return step;
    }

    if (state.flying && !state.landed)
    {
        state.flying = false;
        state.landed = true;
        step.kind = Kind::Land;
        step.apexZ = state.apexZ;
        step.landZ = s.z;
        step.jumpUntilMs = state.jumpUntilMs;
        float const dx = s.x - state.x;
        float const dy = s.y - state.y;
        bool const whereTheFlightWas = s.mapId == state.mapId && s.instanceId == state.instanceId &&
            dx * dx + dy * dy <= kLandingToleranceYd * kLandingToleranceYd;
        step.chargesFall = whereTheFlightWas && !inWater();
    }
    return step;
}
}
}
