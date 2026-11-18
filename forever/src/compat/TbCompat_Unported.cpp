// Out-of-line part of the "not ported yet" list (TbCompat_Unported.h). Compiled with the module, WITHOUT the cmangos shim in
// front: this file only sees the core's own headers.
#define TB_UNPORTED_TABLE_ONLY 1
#include "TbCompat_Unported.h"

#include "Log.h"
#include "DB2Stores.h"
#include "MMapManager.h"

#include <array>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <initializer_list>
#include <sstream>

// TbCompat_Unported.h: the navmesh query of a map.
dtNavMeshQuery const* NavMeshQueryCompat(uint32 mapId, uint32 instanceId)
{
    return MMAP::MMapManager::instance()->GetNavMeshQuery(mapId, mapId, instanceId);
}

// TbCompat_Unported.h: the position a player reads on the zone map. T `Map2ZoneCoordinates(x, y, zone)` (DBCStores.cpp:556, the old world map area
// rectangle); F `DB2Manager::Map2ZoneCoordinates(area, x, y)` (DB2Stores.cpp:3551, the UI map position 0 to 100, which leaves x and y alone when the area
// has no UI map). DIFFERENT ANSWER, display only: the numbers of the stay position the bot tells its master.
void Map2ZoneCoordinatesCompat(float& x, float& y, uint32 zone)
{
    sDB2Manager.Map2ZoneCoordinates(zone, x, y);
}

// TbCompat_Unported.h: printf into a std::string, the way the Turtle core's PQuery formats a query.
std::string LegacyFormatCompat(char const* format, ...)
{
    va_list args;
    va_start(args, format);
    va_list copy;
    va_copy(copy, args);
    int const length = vsnprintf(nullptr, 0, format, copy);
    va_end(copy);
    std::string text;
    if (length > 0)
    {
        text.resize(static_cast<size_t>(length) + 1);
        vsnprintf(text.data(), text.size(), format, args);
        text.resize(static_cast<size_t>(length));
    }
    va_end(args);
    return text;
}

namespace tbcompat
{
namespace
{
constexpr size_t kFeatureCount = static_cast<size_t>(Feature::Count);

struct Row
{
    char const* name;
    char const* what;
};

constexpr std::array<Row, kFeatureCount> kRows = {{
#define TB_UNPORTED_ROW(id, name, what) { name, what },
    TB_UNPORTED_FEATURES(TB_UNPORTED_ROW)
#undef TB_UNPORTED_ROW
}};

std::array<std::atomic<uint64_t>, kFeatureCount> g_calls{};
}

void Unported(Feature feature)
{
    size_t const index = static_cast<size_t>(feature);
    if (index >= kFeatureCount)
        return;
    // The first call of a name is the one that moves the counter from 0 to 1: it logs, the others only count.
    if (g_calls[index].fetch_add(1, std::memory_order_relaxed) == 0)
        TC_LOG_WARN("server.loading", "TortoiseBots: not ported yet: {}", kRows[index].name);
}

void UnportedPacketHandler(std::string const& name)
{
    struct Group { Feature feature; std::initializer_list<char const*> handlers; };
    static Group const groups[] = {
        { Feature::IncomingLoot, { "loot roll", "loot response", "loot start roll", "loot vote", "loot roll won" } },
        { Feature::IncomingQuest, { "complete quest", "accept quest", "confirm quest", "quest share", "quest details",
            "quest update add kill", "quest update add item", "quest update failed", "quest update failed timer", "quest update complete" } },
        { Feature::IncomingTaxi, { "activate taxi", "taxi done" } },
        { Feature::IncomingGroup, { "group invite", "uninvite", "uninvite guid", "group set leader", "party command", "ready check",
            "summon request", "duel requested" } },
        { Feature::IncomingTradeAndItems, { "trade status", "trade status extended", "not enough money", "not enough reputation",
            "cannot equip", "inventory change failure", "item push result", "master use item" } },
        { Feature::IncomingBattleground, { "bg status" } },
        { Feature::IncomingGuildPetition, { "petition offer", "guild accept" } },
        { Feature::IncomingExperience, { "levelup", "xpgain", "honorgain" } },
        { Feature::IncomingEmotes, { "receive text emote", "receive emote" } },
        { Feature::IncomingObjectUse, { "use game object", "area trigger", "gossip hello" } },
        { Feature::IncomingDeath, { "release spirit", "revive from corpse", "resurrect request" } },
        { Feature::IncomingCastFailed, { "cast failed" } },
        { Feature::IncomingSpeedChange, { "check mount state" } },
    };
    for (Group const& group : groups)
        for (char const* handler : group.handlers)
            if (name == handler)
            {
                Unported(group.feature);
                return;
            }
    Unported(Feature::PacketHandlerTables);
}

std::vector<UnportedEntry> UnportedSummary()
{
    std::vector<UnportedEntry> table;
    table.reserve(kFeatureCount);
    for (size_t i = 0; i < kFeatureCount; ++i)
        table.push_back({ kRows[i].name, kRows[i].what, g_calls[i].load(std::memory_order_relaxed) });
    return table;
}

std::string UnportedBootLine()
{
    std::ostringstream line;
    line << "slice: " << kFeatureCount << " features not ported: ";
    for (size_t i = 0; i < kFeatureCount; ++i)
        line << (i ? ", " : "") << kRows[i].name;
    return line.str();
}

std::string UnportedReport()
{
    std::ostringstream text;
    text << "slice: " << kFeatureCount << " features not ported (calls since start)\n";
    for (size_t i = 0; i < kFeatureCount; ++i)
        text << "  " << kRows[i].name << ": " << g_calls[i].load(std::memory_order_relaxed) << "\n";
    return text.str();
}
}
