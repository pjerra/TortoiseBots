#pragma once

// Battleground data ids (protocol and world data, never positions): the maps, the queues, the game object entries and the world
// state ids the module reads. Every number carries the pinned core file it was read from; the wiring test
// (forever/tools/test_bg1_wiring.py) greps the pinned core text for the same number, so a core change breaks the test instead of
// a live run. Positions are NOT here: they come from the core's spawn data at boot (host/bg/TbBgMapData).
//
// Core paths are under src/server/ on the pin in forever/PINNED_CORE.

#include <cstdint>

namespace TortoiseBots
{
namespace bg
{

// ---- the two maps this round plays (D9: WSG and AB only) ----
// battleground_warsong_gulch.cpp:614 `MapId = 489`
inline constexpr std::uint32_t kWsgMapId = 489;
// battleground_arathi_basin.cpp: `MapId = 529`
inline constexpr std::uint32_t kAbMapId = 529;

// BattlemasterList ids, which are also the BattlegroundTypeId values and the queue's BattlemasterListId (SharedDefines.h:6948-6949).
inline constexpr std::uint32_t kWsgListId = 2;
inline constexpr std::uint32_t kAbListId = 3;

// ---- Warsong Gulch, map 489 (battleground_warsong_gulch.cpp:638-668) ----
inline constexpr std::uint32_t kWsgSilverwingFlag = 179830;   // Alliance's stand, taken by the Horde
inline constexpr std::uint32_t kWsgWarsongFlag = 179831;      // Horde's stand, taken by the Alliance
inline constexpr std::uint32_t kWsgSilverwingFlagDropped = 179785;
inline constexpr std::uint32_t kWsgWarsongFlagDropped = 179786;

inline constexpr std::int32_t kWsgFlagTakenAlliance = 1545;   // 0 at base, 1 carried, -1 on the ground
inline constexpr std::int32_t kWsgFlagTakenHorde = 1546;
inline constexpr std::int32_t kWsgCapturesAlliance = 1581;
inline constexpr std::int32_t kWsgCapturesHorde = 1582;

// ---- Arathi Basin, map 529 (battleground_arathi_basin.cpp:706-723) ----
// Node order is the core's NodeData order: 0 Stables, 1 Blacksmith, 2 Farm, 3 Lumber Mill, 4 Gold Mine.
inline constexpr std::uint32_t kAbNodeBannerCount = 5;
inline constexpr std::uint32_t kAbNodeBanners[kAbNodeBannerCount] =
    { 180087, 180088, 180089, 180090, 180091 };
// NodeData[i].StateWorldState; the state is the id + 0 alliance, +1 horde, +2 alliance contested, +3 horde contested.
inline constexpr std::int32_t kAbNodeStateWorldStates[kAbNodeBannerCount] =
    { 1767, 1782, 1772, 1792, 1787 };
inline constexpr std::int32_t kAbResourcesAlliance = 1776;
inline constexpr std::int32_t kAbResourcesHorde = 1777;

// The bracket table has one row per level range per map (PVPDifficulty.db2 via DB2Manager::GetBattlegroundBracketByLevel); the
// module never keeps a table of its own (D9).
inline constexpr std::uint32_t kMaxBrackets = 16;

} // namespace bg
} // namespace TortoiseBots
