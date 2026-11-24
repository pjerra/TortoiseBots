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

// The flag a player carries is an aura on him (battleground_warsong_gulch.cpp:647-648, the carrier spell of the OTHER side's flag): an
// Alliance player carries the Horde's Warsong flag (23333), a Horde player the Alliance's Silverwing flag (23335).
inline constexpr std::uint32_t kWsgCarrierAuraWarsong = 23333;
inline constexpr std::uint32_t kWsgCarrierAuraSilverwing = 23335;

// Which side's player carries which of the two, as the core casts it: PickUpFlag does `player->CastSpell(player, CarrierSpell(flagTeam), true)`
// (battleground_warsong_gulch.cpp:925) and CarrierSpell answers TEAM_ALLIANCE -> SPELL_SILVERWING_FLAG, TEAM_HORDE -> SPELL_WARSONG_FLAG
// (there, :706; the ids :51-54 and :647-648): the picker-upper fights the OTHER side, so `WsgCarrierAuraOf(side)` is the aura a player
// OF `side` carries. ForeverCore b441bf74e0. Tested standalone by forever/tools/test_bg_flag_carrier.cpp.
enum class BgSide : std::uint8_t
{
    Alliance = 0,
    Horde = 1,
};

inline constexpr BgSide BgOtherSide(BgSide side)
{
    return side == BgSide::Alliance ? BgSide::Horde : BgSide::Alliance;
}

// The carrier aura a player of `carrier` holds: an Alliance player the Horde's Warsong flag, a Horde player the Alliance's Silverwing flag.
inline constexpr std::uint32_t WsgCarrierAuraOf(BgSide carrier)
{
    return carrier == BgSide::Alliance ? kWsgCarrierAuraWarsong : kWsgCarrierAuraSilverwing;
}

// The carrier aura an ENEMY of a bot on `botSide` holds (its own side's flag), and the one its own carrier holds (the enemy's flag).
inline constexpr std::uint32_t WsgEnemyCarrierAura(BgSide botSide)
{
    return WsgCarrierAuraOf(BgOtherSide(botSide));
}

inline constexpr std::uint32_t WsgOwnCarrierAura(BgSide botSide)
{
    return WsgCarrierAuraOf(botSide);
}

// Both a carrier can hold in a match (the dropped-flag auras 23334/23336 are not "carried": the flag lies on the ground then).
inline constexpr bool IsWsgCarrierAura(std::uint32_t aura)
{
    return aura == kWsgCarrierAuraWarsong || aura == kWsgCarrierAuraSilverwing;
}

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
