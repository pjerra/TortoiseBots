#pragma once

// Pure policy: did a commanded ranged pull get its walk to the firing
// position started?
//
// The order runs the pull's two steps in one AI tick and then judges. The walk
// is a point move: the core registers the movement generator at once, but the
// unit only reads as moving after the next movement tick. Judging on "is the
// unit moving" alone therefore aborted pulls whose walk had just been launched
// ("The tank could not start moving to a firing position for the pull").
// A launched motion counts as started.
//
// Core-free, so tools/test_pull_start_policy.cpp includes it directly.

namespace ai
{
    struct PullStartFacts
    {
        bool hasTarget = true;       // the pull target is in the world
        bool moving = false;         // the unit already reads as moving
        bool motionLaunched = false; // a point or chase movement generator is set
        float distance = 0.0f;       // tank to the pull target
        float range = 0.0f;          // the pull's shooting range
    };

    inline bool PullWalkStarted(PullStartFacts const& facts)
    {
        return !facts.hasTarget || facts.moving || facts.motionLaunched ||
            facts.distance <= facts.range;
    }
}
