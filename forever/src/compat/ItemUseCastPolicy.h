#pragma once
#include <cstdint>

namespace TortoiseBots
{
    enum class ItemCastTargetKind : uint8_t
    {
        Self = 0,
        Unit = 1,
        Item = 2
    };

    inline ItemCastTargetKind DecideItemCastTargetKind(bool hasItem, bool hasUnit)
    {
        if (hasItem)
            return ItemCastTargetKind::Item;
        if (hasUnit)
            return ItemCastTargetKind::Unit;
        return ItemCastTargetKind::Self;
    }

    inline bool ShouldStopMovingForItemCast(bool isFoodOrDrink, int32_t castTimeMs, bool isChanneled)
    {
        return isFoodOrDrink || castTimeMs > 0 || isChanneled;
    }

    inline uint32_t ItemCastDurationMs(bool isFoodOrDrink, int32_t castTimeMs, bool isChanneled, int32_t spellDurationMs)
    {
        if (isFoodOrDrink)
            return 24000u;
        if (isChanneled)
            return spellDurationMs > 0 ? static_cast<uint32_t>(spellDurationMs) : 8000u;
        if (castTimeMs > 0)
            return static_cast<uint32_t>(castTimeMs);
        return 0u;
    }
}
