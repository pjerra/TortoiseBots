#pragma once

// The few distances and waits of BG1 that are not data: how close a ghost must stand to an area spirit healer before the core
// accepts its queue request (the core refuses beyond MAX_AREA_SPIRIT_HEALER_RANGE, Player.h:1225, so the header keeps one yard
// of margin), how far the healer is searched for, and how far a bot must move between two one-second looks to count as moving.
//
// Pure, so the host side (forever/src/host/bg/) holds no float literal at all: the wiring test (forever/tools/test_bg1_wiring.py)
// rejects one, which is how "a position never typed in" stays checkable. POSITIONS are not here: they come from the core's spawn
// data (host/bg/TbBgMapData).

#include <cstdint>

namespace TortoiseBots
{
namespace bg
{

// The core refuses a queue request beyond MAX_AREA_SPIRIT_HEALER_RANGE (20 yd, Player.h:1225); one yard of margin keeps the
// server-side check out of rounding.
inline constexpr float kSpiritHealerQueueYd = 19.0f;
inline constexpr float kSpiritHealerSearchYd = 40.0f;      // the grid radius the ghost looks in
inline constexpr float kMoveSampleYd = 0.5f;               // one-second movement sample below this is standing still
inline constexpr std::uint32_t kSpiritHealerWalkRetryMs = 3000;

} // namespace bg
} // namespace TortoiseBots
