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
    // threat", trained at 58): the 1.12 bear rage dump behind Maul, or free
    // under the Omen of Clarity proc.
    constexpr uint32_t kBearSavageBiteRage = 50;
    inline bool ShouldSavageBite(uint32_t rage, bool clearcasting)
    {
        return clearcasting || rage >= kBearSavageBiteRage;
    }

    // Primal Bite (407995, 20 rage, 6 s cooldown, high threat) is the same
    // rage dump on the Forever data.
    constexpr uint32_t kBearPrimalBiteRage = 20;
    inline bool ShouldPrimalBite(uint32_t rage, bool clearcasting)
    {
        return clearcasting || rage >= kBearPrimalBiteRage;
    }

    // Lacerate (1328617, 15 rage, 15 s bleed, stacks to five): build the
    // stack up, then only refresh it near expiry so a refresh never eats a
    // Maul swing.
    constexpr uint32_t kBearLacerateMinRage = 15;
    constexpr uint8_t kBearLacerateMaxStacks = 5;
    constexpr uint32_t kBearLacerateRefreshMs = 4000;
    inline bool ShouldLacerate(uint32_t rage, uint8_t stacks, uint32_t remainingMs)
    {
        if (rage < kBearLacerateMinRage)
            return false;
        if (stacks >= kBearLacerateMaxStacks)
            return remainingMs <= kBearLacerateRefreshMs;
        return true;
    }
} // namespace ai
