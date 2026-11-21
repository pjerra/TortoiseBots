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

} // namespace TortoiseBots
