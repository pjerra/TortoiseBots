#pragma once

// Bot limit per map: how many free bots one dungeon or raid map holds before the host refuses another free bot its entry.
// A dungeon takes the 5-man cap; a raid takes the cap of its size (10, 20, 40 by the map row's player count, unknown reads
// as 40). Open-world maps, battlegrounds and arenas have no cap. 0 switches a kind off.
//
// Pure (no core types): forever/tools/test_free_map_cap_policy.cpp checks the table; BotMgr::CheckEntry fills the facts from
// the map store (MapEntry::IsDungeon/IsRaid/MaxPlayers) and counts the free bots already on the target map.

#include <cstdint>

namespace TortoiseBots
{
namespace world
{

struct FreeMapCaps
{
    std::uint32_t dungeon = 5;
    std::uint32_t raid10 = 10;
    std::uint32_t raid20 = 20;
    std::uint32_t raid40 = 40;
};

// 0 = no cap on that map (not a dungeon at all, or the kind's cap is 0).
inline std::uint32_t FreeMapCapFor(bool isDungeon, bool isRaid, std::uint32_t maxPlayers, FreeMapCaps const& caps)
{
    if (!isDungeon)
        return 0;
    if (!isRaid)
        return caps.dungeon;
    if (maxPlayers != 0 && maxPlayers <= 10)
        return caps.raid10;
    if (maxPlayers != 0 && maxPlayers <= 20)
        return caps.raid20;
    return caps.raid40;
}

} // namespace world
} // namespace TortoiseBots
