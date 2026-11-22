// The addon info readers without the core: the TBM read lines the addon polls
// (TBF #130 piece f-info). CC_ASSIGN_BEGIN/CC_ASSIGN/CC_ASSIGN_END ride the
// roster reply (TB commands/Info.cpp:501-505, addon Roster.lua:331-373);
// RUN/RUN_HERE/RAID_LAYOUT come from the run state (TB commands/Commander.cpp,
// addon BotRun.lua); FEATURE and CONFIG_BEGIN/CONFIG/CONFIG_END list the
// module's switches and settings (TB commands/Info.cpp:238-311, addon
// ServerInfo.lua). No core header and no legacy type; the live half that reads
// the live objects is host/TbAddonInfo.cpp. Tested standalone by
// forever/tools/test_addon_info.cpp.
#pragma once

#include "host/TbAddonPure.h"

#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace TortoiseBots
{
namespace addoninfo
{
// The CONFIG stream shows at most this many rows (TB's kMaxShown); the BEGIN
// line still carries the full match count.
inline constexpr std::size_t kConfigMaxRows = 200;

// A config search needs at least this many letters (TB's gate in
// HandleFeatures: shorter text answers usage).
inline constexpr std::size_t kConfigMinFilter = 2;

// TB's config values turn '|' into '/' (commands/Info.cpp SendConfigSearch);
// keys go through the normal Safe.
inline std::string SafeConfigValue(std::string_view value)
{
    std::string out(value);
    for (char& c : out)
        if (c == '|')
            c = '/';
    return out;
}

inline std::string ConfigSummaryLine(std::size_t matches, std::string_view filter)
{
    return "Config: " + std::to_string(matches) + " keys match '" + std::string(filter) + "'.";
}

inline std::string ConfigBeginLine(std::size_t matches)
{
    return "TBM:CONFIG_BEGIN|" + std::to_string(matches);
}

inline std::string ConfigLine(std::string_view key, std::string_view value)
{
    return "TBM:CONFIG|" + addon::pure::Safe(key) + "|" + SafeConfigValue(value);
}

inline std::string ConfigEndLine(std::size_t shown, std::size_t matches)
{
    return "TBM:CONFIG_END|" + std::to_string(shown) + "|" + std::to_string(matches);
}

inline std::string Lower(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (char c : text)
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return out;
}

// Case-insensitive substring (TB's FindConfigEntries); an empty filter
// matches everything, as in TB.
inline bool ConfigMatches(std::string_view filter, std::string_view key)
{
    std::string const wanted = Lower(filter);
    if (wanted.empty())
        return true;
    std::string const name = Lower(key);
    return name.find(wanted) != std::string::npos;
}

// Secret keys read back masked (TB's FindConfigEntries list).
inline bool ConfigSecret(std::string_view key)
{
    std::string const name = Lower(key);
    return name.find("apikey") != std::string::npos || name.find("llmapi") != std::string::npos ||
        name.find("password") != std::string::npos || name.find("secret") != std::string::npos ||
        name.find("token") != std::string::npos;
}

inline std::string MaskedConfigValue(std::string_view key, std::string_view value)
{
    return ConfigSecret(key) ? "***" : std::string(value);
}

// One module switch for the addon's Info window (TB's TBM:FEATURE shape).
inline std::string FeatureLine(std::string_view id, std::string_view value, std::string_view label)
{
    return "TBM:FEATURE|" + addon::pure::Safe(id) + "|" + addon::pure::Safe(value) + "|" +
        addon::pure::Safe(label);
}

// The "features" verb serves two addon sends: bare "features" lists the
// switches, "features all <text>" searches the settings (TB's HandleFeatures).
struct FeaturesRequest
{
    bool search = false;
    std::string filter;
    bool usageError = false;
};

inline FeaturesRequest ParseFeaturesArgs(std::string_view args)
{
    FeaturesRequest out;
    std::string_view text = addon::pure::Trim(args);
    if (text.empty())
        return out;
    std::size_t i = 0;
    while (i < text.size() && !std::isspace(static_cast<unsigned char>(text[i])))
        ++i;
    if (Lower(text.substr(0, i)) != "all")
    {
        out.usageError = true;
        return out;
    }
    out.search = true;
    out.filter = std::string(addon::pure::Trim(text.substr(i)));
    if (out.filter.size() < kConfigMinFilter)
        out.usageError = true;
    return out;
}

// The CC assignment stream (TB's roster trailer shape).
inline std::string CcBeginLine(std::size_t count)
{
    return "TBM:CC_ASSIGN_BEGIN|" + std::to_string(count);
}

inline std::string CcLine(std::string_view name, std::string_view mark)
{
    return "TBM:CC_ASSIGN|" + addon::pure::Safe(name) + "|" + addon::pure::Safe(Lower(mark));
}

inline constexpr std::string_view kCcEnd = "TBM:CC_ASSIGN_END";

// The marks a bot can hold as its crowd-control mark (TB's cc words; skull is
// the party's kill mark, never a CC mark).
inline char const* const kCcMarks[] = {
    "star", "circle", "diamond", "triangle", "moon", "square", "cross",
};

inline bool ValidCcMark(std::string_view mark)
{
    std::string const word = Lower(mark);
    for (char const* known : kCcMarks)
        if (word == known)
            return true;
    return false;
}

// The "run" verb's readers: "run status", "run here" and "run list" (the
// addon sends nothing else read-only; anything else is a mutation this piece
// does not own).
enum class RunReader
{
    Status,
    Here,
    List,
    Other,
};

struct RunRequest
{
    RunReader reader = RunReader::Other;
    std::string sub;   // the verb word as sent, for the error intent
};

inline RunRequest ParseRunArgs(std::string_view args)
{
    RunRequest out;
    std::string_view text = addon::pure::Trim(args);
    std::size_t i = 0;
    while (i < text.size() && !std::isspace(static_cast<unsigned char>(text[i])))
        ++i;
    out.sub = std::string(text.substr(0, i));
    std::string const word = Lower(out.sub);
    if (word == "status")
        out.reader = RunReader::Status;
    else if (word == "here")
        out.reader = RunReader::Here;
    else if (word == "list")
        out.reader = RunReader::List;
    if (out.sub.empty())
        out.sub = "unknown";
    return out;
}

// The status line when no run is going (byte-exact with TB's
// HandleDungeonRun: TBM:RUN|-|none|0/0|-|-|-|no run).
inline std::string NoRunLine()
{
    return "TBM:RUN|-|none|0/0|-|-|-|no run";
}

// TBM:RUN_HERE|<plan>|<yards> (TB's HandleDungeonRun "here" shape); the plan
// name never holds a '|' but goes through Safe like every field.
inline std::string RunHereLine(std::string_view plan, std::uint32_t yards)
{
    return "TBM:RUN_HERE|" + addon::pure::Safe(plan) + "|" + std::to_string(yards);
}

inline std::string NoRunHereLine()
{
    return "TBM:RUN_HERE|-|0";
}

// One candidate plan step for the "here" lookup; steps at 0,0,0 are not on
// the route (TB skips them when measuring).
struct PlanPoint
{
    std::string plan;
    std::uint32_t map = 0;
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

struct NearestPlan
{
    bool found = false;
    std::string plan;
    std::uint32_t yards = 0;
};

inline NearestPlan PickNearestPlan(std::uint32_t map, float x, float y, float z, std::vector<PlanPoint> const& points)
{
    NearestPlan out;
    double best = 0.0;
    for (PlanPoint const& point : points)
    {
        if (point.map != map || point.plan.empty())
            continue;
        if (point.x == 0.0f && point.y == 0.0f && point.z == 0.0f)
            continue;
        double const dx = double(point.x) - double(x);
        double const dy = double(point.y) - double(y);
        double const dz = double(point.z) - double(z);
        double const dist = dx * dx + dy * dy + dz * dz;
        if (!out.found || dist < best)
        {
            out.found = true;
            out.plan = point.plan;
            best = dist;
        }
    }
    if (out.found)
        out.yards = std::uint32_t(std::sqrt(best));
    return out;
}

// One loaded plan for the "run list" reply (TB's "Dungeon plans:" shape;
// unsupported counts the steps of kind None, as TB's Supported does).
struct PlanEntry
{
    std::string name;
    std::uint32_t map = 0;
    std::size_t steps = 0;
    std::size_t unsupported = 0;
};

inline std::string RunListLine(std::vector<PlanEntry> const& plans)
{
    if (plans.empty())
        return "Dungeon plans: none loaded";
    std::string line = "Dungeon plans: ";
    bool first = true;
    for (PlanEntry const& plan : plans)
    {
        if (!first)
            line += ", ";
        first = false;
        line += plan.name + " (map " + std::to_string(plan.map) + ", " + std::to_string(plan.steps) + " steps";
        if (plan.unsupported > 0)
            line += ", " + std::to_string(plan.unsupported) + " not supported yet";
        line += ")";
    }
    return line;
}

// TBM:RAID_LAYOUT|<bots>|<tanks>|<tankHealers>|<raidHealers>|<groups> (TB's
// raid-bind shape). TBF keeps no raid bind and no roles, so everything but
// the bot count reads "-".
inline std::string RaidLayoutLine(std::string_view bots, std::string_view tanks, std::string_view tankHealers,
    std::string_view raidHealers, std::string_view groups)
{
    return "TBM:RAID_LAYOUT|" + addon::pure::Safe(bots) + "|" + addon::pure::Safe(tanks) + "|" +
        addon::pure::Safe(tankHealers) + "|" + addon::pure::Safe(raidHealers) + "|" + addon::pure::Safe(groups);
}

inline std::string RaidLayoutWarning(std::string_view text)
{
    return "TBM:RAID_LAYOUT|warning|" + addon::pure::Safe(text);
}
} // namespace addoninfo
} // namespace TortoiseBots
