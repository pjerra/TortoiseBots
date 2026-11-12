#pragma once

// RegroupReleaseProbe: the diagnostic that names who starts the fight after a
// `revive|party` release at a wipe (walk 86: the plan boss evades, then about
// 2 s later takes a bot revived 430 yd away at the entrance and walks there
// through trash; no code read found the starter). Off unless
// AiPlayerbot.RaidBossDiagnostics = 1. Reads only: no behaviour change.
//
// The row layout, the window and the per-commander book are here, free of core
// headers, so they are testable without the game
// (tools/test_regroup_release_probe_policy.cpp); commands/Commander.cpp fills
// them (the release in `HandleCommanderOrder`, then `TickDungeonRun`).
//
// Rows (bot_events.csv, event RegroupReleaseProbe; subject and detail are the
// usual quoted info1/info2 fields):
//   tick=0..10|boss=<name>  entry=..|combat=..|victim=..|threat=..|top=..|evade=..|homed=..
//   tick=0..10|bot=<name>   engine=..|cur=..|atk=..|pull=..|vic=..|sel=..|combat=..|holders=..
// tick=0 is the release itself, 1..kTickRows the run ticks after it. One row
// per plan boss alive on the run's map each tick; one per revived bot each
// tick it is alive (a bot still a corpse writes none). A bot row's `holders`
// is the count of units that hold it on their threat list, then the first
// kHolderNames of their names.

#include <cstddef>
#include <cstdint>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "policy/infra/RaidBossLogPolicy.h" // RaidBossLog::Name

namespace TortoiseBots
{
namespace RegroupProbe
{

// Run ticks after the release row that carry a row each (~1 s per tick).
constexpr unsigned kTickRows = 10;
// Threat names a boss row carries, in the threat list's own order.
constexpr std::size_t kThreatNames = 5;
// Holder names a bot row carries, in the hostile reference list's order.
constexpr std::size_t kHolderNames = 5;

// The release's clock: armed by the `revive|party` release, then one row per
// run tick for kTickRows ticks, never more.
struct Window
{
    bool armed = false;
    unsigned rows = 0; // rows written after the release row

    void Arm()
    {
        armed = true;
        rows = 0;
    }

    bool Spent() const { return !armed || rows >= kTickRows; }

    // The tick number this run tick writes under (1..kTickRows), 0 when the
    // window is spent (which also disarms it).
    unsigned Next()
    {
        if (Spent())
        {
            armed = false;
            return 0;
        }
        return ++rows;
    }
};

// One run's window and the bots its release revived.
struct RunState
{
    Window window;
    std::vector<std::uint64_t> bots;
};

// The live windows, one per commander: two runs releasing and ticking
// interleaved never spend each other's window. A run's window is erased by a
// new release for the same run, when it is spent, and when the run ends.
struct Book
{
    std::map<std::uint64_t, RunState> runs;

    RunState& Arm(std::uint64_t commander, std::vector<std::uint64_t> const& bots)
    {
        RunState& state = runs[commander];
        state.window.Arm();
        state.bots = bots;
        return state;
    }

    RunState* Find(std::uint64_t commander)
    {
        auto it = runs.find(commander);
        return it == runs.end() ? nullptr : &it->second;
    }

    void End(std::uint64_t commander) { runs.erase(commander); }
    void Clear() { runs.clear(); }
};

// A field's text: the empty string reads as "-" so every key keeps its value.
inline std::string Field(std::string const& text)
{
    return text.empty() ? std::string("-") : RaidBossLog::Name(text);
}

inline std::string BossSubject(unsigned tick, std::string const& boss)
{
    std::ostringstream out;
    out << "tick=" << tick << "|boss=" << RaidBossLog::Name(boss);
    return out.str();
}

inline std::string BotSubject(unsigned tick, std::string const& bot)
{
    std::ostringstream out;
    out << "tick=" << tick << "|bot=" << RaidBossLog::Name(bot);
    return out.str();
}

// The boss row: its entry, whether it is in combat, its victim, the threat
// list's size and first kThreatNames names, the evade flag, and how far it
// stands from its spawn point (the brawl it is walking back from).
inline std::string BossDetail(std::uint32_t entry, bool inCombat, std::string const& victim,
    std::size_t threats, std::vector<std::string> const& names, bool evading, float spawnYd)
{
    std::ostringstream out;
    out << "entry=" << entry << "|combat=" << (inCombat ? 1 : 0) << "|victim=" << Field(victim)
        << "|threat=" << threats << "|top=";
    bool first = true;
    for (std::size_t i = 0; i < names.size() && i < kThreatNames; ++i)
    {
        if (!first)
            out << ",";
        out << RaidBossLog::Name(names[i]);
        first = false;
    }
    if (first)
        out << "-";
    out << "|evade=" << (evading ? 1 : 0) << "|homed=" << static_cast<unsigned>(spawnYd + 0.5f);
    return out.str();
}

// The bot row: which engine runs (combat/non-combat/dead/reaction), its
// current, attack and pull targets, the server's own victim, the unit it has
// selected (a name when the AI resolves it, the raw guid otherwise), the
// server's combat flag, and the units that hold it on their threat list (the
// count, then the first kHolderNames names).
inline std::string BotDetail(std::string const& engine, std::string const& current, std::string const& attack,
    std::string const& pull, std::string const& victim, std::string const& selection, bool inCombat,
    std::size_t holders, std::vector<std::string> const& holderNames)
{
    std::ostringstream out;
    out << "engine=" << RaidBossLog::Name(engine) << "|cur=" << Field(current) << "|atk=" << Field(attack)
        << "|pull=" << Field(pull) << "|vic=" << Field(victim) << "|sel=" << Field(selection)
        << "|combat=" << (inCombat ? 1 : 0) << "|holders=" << holders << ":";
    if (holderNames.empty())
        out << "-";
    else
        for (std::size_t i = 0; i < holderNames.size() && i < kHolderNames; ++i)
        {
            if (i)
                out << ",";
            out << RaidBossLog::Name(holderNames[i]);
        }
    return out.str();
}

} // namespace RegroupProbe
} // namespace TortoiseBots
