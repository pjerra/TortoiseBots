#pragma once

// RegroupReleaseProbe: the diagnostic that names who starts the fight after a
// `revive|party` release at a wipe (walk 86: the plan boss evades, then about
// 2 s later takes a bot revived 430 yd away at the entrance and walks there
// through trash; no code read found the starter). Off unless
// AiPlayerbot.RaidBossDiagnostics = 1. Reads only: no behaviour change.
//
// The row layout and the two windows are here, free of core headers, so they
// are testable without the game (tools/test_regroup_release_probe_policy.cpp);
// commands/Commander.cpp fills them (the release in `HandleCommanderOrder`,
// then `TickDungeonRun`).
//
// Rows (bot_events.csv, event RegroupReleaseProbe; subject and detail are the
// usual quoted info1/info2 fields):
//   tick=0..10|boss=<name>  entry=..|combat=..|victim=..|threat=..|top=..|evade=..|homed=..
//   tick=0..10|bot=<name>   engine=..|cur=..|atk=..|pull=..|combat=..|holders=..
// tick=0 is the release itself, 1..kTickRows the run ticks after it. One row
// per plan boss alive on the run's map each tick; one per revived bot on its
// first kBotLivingTicks living ticks (a bot still a corpse writes none).

#include <cstddef>
#include <cstdint>
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
// Living ticks each revived bot is read on (its own count, inside the window).
constexpr unsigned kBotLivingTicks = 3;
// Threat names a boss row carries, in the threat list's own order.
constexpr std::size_t kThreatNames = 5;

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

// One revived bot's budget: a row on each of its first kBotLivingTicks living
// ticks. A tick the bot spends dead neither writes nor counts.
struct BotBudget
{
    std::uint64_t guid = 0;
    unsigned living = 0;

    bool Due(bool alive)
    {
        if (!alive || living >= kBotLivingTicks)
            return false;
        ++living;
        return true;
    }
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
// current, attack and pull targets, the server's own combat flag, and how many
// units hold it on their threat list.
inline std::string BotDetail(std::string const& engine, std::string const& current, std::string const& attack,
    std::string const& pull, bool inCombat, std::size_t holders)
{
    std::ostringstream out;
    out << "engine=" << RaidBossLog::Name(engine) << "|cur=" << Field(current) << "|atk=" << Field(attack)
        << "|pull=" << Field(pull) << "|combat=" << (inCombat ? 1 : 0) << "|holders=" << holders;
    return out.str();
}

} // namespace RegroupProbe
} // namespace TortoiseBots
