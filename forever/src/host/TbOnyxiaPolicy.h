#pragma once

#include <cstdint>

namespace TortoiseBots::onyxia
{
enum class Phase { Idle, GroundOne, Air, GroundThree, Dead };

inline char const* PhaseName(Phase phase)
{
    switch (phase)
    {
        case Phase::GroundOne: return "ground-one";
        case Phase::Air: return "air";
        case Phase::GroundThree: return "ground-three";
        case Phase::Dead: return "dead";
        default: return "idle";
    }
}

struct Observation
{
    bool present = false;
    bool alive = false;
    bool combat = false;
    bool flying = false;
    float healthPct = 100;
    std::uint32_t map = 0;
    std::uint32_t instance = 0;
    std::uint32_t entry = 0;
};

struct Encounter
{
    Phase phase = Phase::Idle;
    std::uint32_t instance = 0;
    std::uint64_t elapsedMs = 0;
    bool sawAir = false;

    void Clear() { *this = {}; }

    bool Update(Observation const& observation, std::uint32_t diff)
    {
        Phase const before = phase;
        if (!observation.present || observation.map != 249 || observation.entry != 10184)
        {
            Clear();
            return before != phase;
        }
        if (instance != observation.instance)
            Clear();
        instance = observation.instance;
        if (!observation.alive)
            phase = Phase::Dead;
        else if (!observation.combat)
        {
            Clear();
            instance = observation.instance;
        }
        else
        {
            elapsedMs += diff;
            sawAir = sawAir || observation.flying;
            phase = observation.flying ? Phase::Air :
                (sawAir || observation.healthPct < 40 ? Phase::GroundThree : Phase::GroundOne);
        }
        return before != phase;
    }
};
}
