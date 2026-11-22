// BG1 (BATTLEGROUNDS-SPEC.md, decision D3): the battleground maps' data as the core has it, loaded once at boot. The flag stands,
// the AB node banners and the two start locations are read from the core's spawn store and world-safe-locs rows, never typed in;
// a missing spawn turns that map off with one error line, so a data change shows up at boot instead of as a bot walking nowhere.
//
// Positions live here because BG3 (WSG) and BG4 (AB) move bots to them; BG1 loads them and uses the enabled flag (the queue fill
// skips a map whose data is incomplete) and prints them in `.tb bg status`. World thread only; plain data after Load().
#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace TortoiseBots
{
namespace bg
{

struct BgPoint
{
    float x = 0;
    float y = 0;
    float z = 0;
    float o = 0;
};

enum class BgMapKind : std::uint8_t
{
    None = 0,
    Wsg = 1,
    Ab = 2,
};

struct BgMapInfo
{
    BgMapKind kind = BgMapKind::None;
    std::uint32_t mapId = 0;
    std::uint32_t listId = 0;
    char const* name = "";
    bool enabled = false;
    std::string offWhy;                            // the one line Load logged, kept for `.tb bg status`; empty = on
    BgPoint start[2] = {};                         // BattlegroundTemplate::StartLocation[ALLIANCE/HORDE]
    std::array<BgPoint, 2> flags = {};             // WSG: the spawns of 179830 (Silverwing) and 179831 (Warsong)
    std::array<BgPoint, 5> nodes = {};             // AB: the spawns of the five neutral node banners, in the core's NodeData order
    std::uint32_t nodesLoaded = 0;                 // AB only
};

// Loads both maps from the core's stores (call it once the stores are loaded, OnWorldInitialized). Logs one line per map.
void LoadBgMapData();

// The map a list id or a maps id names, or nullptr. Ready after LoadBgMapData().
BgMapInfo const* BgMapByList(std::uint32_t listId);
BgMapInfo const* BgMapByMapId(std::uint32_t mapId);

// "wsg" / "ab" / "" for the log, the events and `.tb bg status`.
char const* BgMapName(std::uint32_t listId);

// True once LoadBgMapData() ran (a command before the boot load says so instead of reporting an empty world).
bool BgMapDataLoaded();

} // namespace bg
} // namespace TortoiseBots
