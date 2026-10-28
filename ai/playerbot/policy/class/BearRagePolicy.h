#pragma once

#include <cstdint>

// Rage gates for the feral bear tank rows, so threat never stalls on an
// ability the bear cannot pay for.

namespace ai
{
    // Maul costs 15 rage (DB 9881): queued every swing from 15, keeping
    // the swing-slot spender live while threat ramps.
    constexpr uint32_t kBearMaulMinRage = 15;
    inline bool ShouldQueueMaul(uint32_t rage) { return rage >= kBearMaulMinRage; }

    // Savage Bite (45736, Bear/Dire Bear only, 30 rage, 6 s cooldown, "high
    // threat", trained at 58): the rage dump behind Maul, or free under the
    // Omen of Clarity proc.
    constexpr uint32_t kBearSavageBiteRage = 50;
    inline bool ShouldSavageBite(uint32_t rage, bool clearcasting)
    {
        return clearcasting || rage >= kBearSavageBiteRage;
    }
} // namespace ai
