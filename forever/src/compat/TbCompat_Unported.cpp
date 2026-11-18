// Out-of-line part of the "not ported yet" list (TbCompat_Unported.h). Compiled with the module, WITHOUT the cmangos shim in
// front: this file only sees the core's own headers.
#define TB_UNPORTED_TABLE_ONLY 1
#include "TbCompat_Unported.h"

#include "Log.h"

#include <array>
#include <atomic>
#include <cstdio>
#include <initializer_list>
#include <sstream>

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
