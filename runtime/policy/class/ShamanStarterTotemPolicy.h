#pragma once

#include <array>
#include <cstdint>

namespace TortoiseBots
{

struct ShamanStarterTotemEntry
{
    uint8_t minLevel;
    uint32_t itemId;
};

inline constexpr std::array<ShamanStarterTotemEntry, 4> kShamanStarterTotems = {{
    {  4, 5175 }, // Earth Totem
    { 10, 5176 }, // Fire Totem
    { 20, 5177 }, // Water Totem
    { 30, 5178 }, // Air Totem
}};

inline uint32_t ShamanStarterTotemForSlot(uint8_t slot, uint8_t level)
{
    if (slot >= kShamanStarterTotems.size())
        return 0;
    return level >= kShamanStarterTotems[slot].minLevel ? kShamanStarterTotems[slot].itemId : 0;
}

} // namespace TortoiseBots
