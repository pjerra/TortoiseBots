#pragma once

// Pure break-even rule for riding a mount on a trip (issue #152).
//
// A mount cast costs the rider its cast time; the mount then gains
// `mountSpeedPercent` over the running speed. The trip must be long enough
// that the cast time is repaid. `factor` is
// AiPlayerbot.MountBreakEvenFactor: 0 disables the threshold (mount for any
// trip), larger values keep the bot on foot longer.
//
// No core includes; tested in tools/test_mount_break_even_policy.cpp.

#include <cfloat>

namespace ai
{
    // `runSpeed` in yards/s (7 in 1.12), `castSec` the mount's own cast time
    // in seconds, `mountSpeedPercent` the mount's speed bonus (60/100; the
    // spell's mirrored aura amount is one less, so callers pass what the
    // core applies). A zero or negative factor means "no threshold"; an
    // unusable mount (0 %) is never worth casting for.
    inline float MountBreakEvenDistance(float runSpeed, float castSec, unsigned mountSpeedPercent, float factor)
    {
        if (factor <= 0.0f)
            return 0.0f;
        if (mountSpeedPercent == 0)
            return FLT_MAX;
        return runSpeed * castSec / (mountSpeedPercent / 100.0f) * factor;
    }
}
