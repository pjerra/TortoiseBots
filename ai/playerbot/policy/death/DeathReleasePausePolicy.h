#pragma once

#include <algorithm>
#include <cstdint>

namespace TortoiseBots
{
    // Per-bot timer state for pausing a random few seconds after death before
    // releasing spirit (free bots only).
    struct DeathReleasePauseState
    {
        uint32_t deadSinceMs = 0;
        uint32_t delayMs = 0;
        bool active = false;
    };

    inline void ResetDeathReleasePause(DeathReleasePauseState& state)
    {
        state.deadSinceMs = 0;
        state.delayMs = 0;
        state.active = false;
    }

    // Returns true while the bot should keep waiting before releasing spirit.
    // On the first call for a death, latches `deadSinceMs` and picks a delay in
    // [minSec * 1000, maxSec * 1000] using `roll` (e.g. urand(0, 9999)).
    inline bool ShouldHoldDeathRelease(DeathReleasePauseState& state, uint32_t nowMs,
        uint32_t minSec, uint32_t maxSec, uint32_t roll)
    {
        if (maxSec < minSec)
            maxSec = minSec;
        if (maxSec == 0)
        {
            ResetDeathReleasePause(state);
            return false;
        }

        if (!state.active)
        {
            uint32_t const minMs = minSec * 1000u;
            uint32_t const maxMs = maxSec * 1000u;
            uint32_t const span = maxMs - minMs;
            state.deadSinceMs = nowMs;
            state.delayMs = minMs + (span > 0u ? (roll % (span + 1u)) : 0u);
            state.active = true;
        }

        return (nowMs - state.deadSinceMs) < state.delayMs;
    }
}
