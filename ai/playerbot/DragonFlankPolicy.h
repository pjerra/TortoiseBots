#pragma once

// Pure rules for the universal "dragon breath risk" trigger (raid audit
// batch 7, finding 6). The dragon is resolved from the bot's own target or,
// failing that, from the cached group "attackers" list, so healers with no
// target and bots on whelps/adds clear the breath and tail cones too.
// Donor behaviour (mod-playerbots 79bd428): Ai/Raid/Ony/OnyTriggers.cpp
// OnyxiaNearTailTrigger finds the boss whatever the bot targets, skips tanks,
// and stays quiet while Onyxia is airborne or has no victim.
// No core includes: tested in tools/test_dragon_flank_policy.cpp.

#include <cstdint>

namespace ai
{
    // Dragons with a frontal breath and a tail cone.
    inline bool IsRaidDragonEntry(std::uint32_t entry)
    {
        switch (entry)
        {
            case 10184: // Onyxia
            case 14601: // Ebonroc
            case 11981: // Flamegor
            case 11983: // Firemaw
            case 11583: // Nefarian
            case 13020: // Vaelastrasz the Corrupt
            case 60748: // Solnius (Emerald Sanctum, Acid Breath 24839)
                return true;
            default:
                return false;
        }
    }

    // Front 90-degree cone (breath/cleave) or rear 60-degree tail cone.
    // facing = dragon orientation, toBot = angle from the dragon to the bot,
    // both in radians, any range (wrapped here).
    inline bool IsInDragonCone(float facing, float toBot)
    {
        const float pi = 3.14159265358979f;
        float diff = toBot - facing;
        while (diff > pi)
            diff -= 2.0f * pi;
        while (diff < -pi)
            diff += 2.0f * pi;
        if (diff < 0.0f)
            diff = -diff;
        const float frontHalf = pi / 4.0f;
        const float rearHalf = pi / 6.0f;
        return diff < frontHalf || diff > (pi - rearHalf);
    }

    // Angle in radians wrapped to [0, 2*pi).
    inline float DragonFlankNormalize(float angle)
    {
        const float twoPi = 6.28318530718f;
        while (angle >= twoPi)
            angle -= twoPi;
        while (angle < 0.0f)
            angle += twoPi;
        return angle;
    }

    // Headings (from the dragon, radians in [0, 2*pi)) the flank move tries
    // in order: the flank on the bot's own side (side picked from the wrapped
    // facing-to-bot difference, so a bot just left of the nose across the
    // 0/2*pi seam does not run across the front to the far flank), the same
    // flank 22.5 degrees toward the head and toward the tail (still clear of
    // the 45-degree breath and 30-degree tail half-cones), then the far flank.
    // Never the nose or the tail.
    constexpr int kDragonFlankHeadingCount = 4;
    inline void DragonFlankHeadings(float facing, float toBot, float* out)
    {
        const float pi = 3.14159265358979f;
        float diff = toBot - facing;
        while (diff > pi)
            diff -= 2.0f * pi;
        while (diff < -pi)
            diff += 2.0f * pi;
        const float sign = diff >= 0.0f ? 1.0f : -1.0f;
        const float nearFlank = facing + sign * pi / 2.0f;
        out[0] = DragonFlankNormalize(nearFlank);
        out[1] = DragonFlankNormalize(nearFlank - sign * pi / 8.0f);
        out[2] = DragonFlankNormalize(nearFlank + sign * pi / 8.0f);
        out[3] = DragonFlankNormalize(facing - sign * pi / 2.0f);
    }

    // Who flanks the dragon.
    // - Dragon is the bot's own target (unchanged rule): everyone flanks
    //   except the tank holding the head (or any tank while it has no victim).
    // - Dragon found only in the group's attackers (healers, bots on
    //   whelps/adds): non-tanks flank while the dragon is grounded and tanked.
    //   Tanks on another target keep their adds where they are.
    inline bool ShouldFlankDragon(bool isTank, bool dragonIsOwnTarget,
        bool dragonHasVictim, bool botIsDragonVictim, bool dragonAirborne)
    {
        if (dragonIsOwnTarget)
            return !isTank || (dragonHasVictim && !botIsDragonVictim);
        if (isTank)
            return false;
        return dragonHasVictim && !dragonAirborne;
    }
}
