#pragma once

#include <cstdint>

namespace TortoiseBots
{

inline uint32_t HunterStarterAmmoId(bool isBullet, uint8_t level)
{
    if (isBullet)
    {
        if (level >= 51)
            return 19317; // Ice Threaded Bullet
        if (level >= 40)
            return 11284; // Accurate Slugs
        if (level >= 25)
            return 3033;  // Solid Shot
        if (level >= 10)
            return 2519;  // Heavy Shot
        return 2516;      // Light Shot
    }

    if (level >= 51)
        return 19316; // Ice Threaded Arrow
    if (level >= 40)
        return 11285; // Jagged Arrow
    if (level >= 25)
        return 3030;  // Razor Arrow
    if (level >= 10)
        return 2515;  // Sharp Arrow
    return 2512;      // Rough Arrow
}

// The ranged weapon a hunter below level 5 falls back on when its ranged slot is empty: the Worn Shortbow the
// core's new-character loadout gives (item level 2, required level 1). Above that the spawn kit and autogear own
// the slot, so a starter weapon is never forced on a levelled hunter.
inline uint32_t HunterStarterRangedItemId(uint8_t level)
{
    return level < 5 ? 2504u : 0u;
}

} // namespace TortoiseBots
