// Dashboard round 1 (FOREVER-DASH-RUNS-SPEC.md): what the Commander and Dungeon runs tabs show of a ForeverCore run, as plain data.
//
// No core header and no database: a RunView is filled once per cycle by TbDashRuns.cpp from the run table (host/run/TbRun.h) and the
// leader's party; everything below turns it into the dashboard's own formats. The COMMANDERS datagram is built by the Turtle builder
// (runtime/CommanderTelemetry.h, standard library only), so the format is the Turtle one by construction. Tested standalone by
// forever/tools/dash_runs_check.cpp (g++ -std=c++17 -I. forever/tools/dash_runs_check.cpp).
#pragma once

#include "runtime/CommanderTelemetry.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace TortoiseBots
{
namespace DashRuns
{
namespace pure
{
namespace CT = ::TortoiseBots::CommanderTelemetry;

// A finished run stays on the Commander tab this long (so the card shows "done" or "stopped" before it goes), then drops out.
constexpr int64_t kLingerSecs = 30;

struct StepView
{
    int leg = 0;
    std::string kind;       // the plan row's kind (move, pull, boss ...)
    float x = 0.0f, y = 0.0f, radius = 0.0f;
    uint32_t entry = 0;
    std::string label;      // the row's comment
};

struct EventView
{
    int64_t atUnix = 0;
    int step = -1;          // 0-based plan step, -1 = none
    int leg = -1;           // leg id, -1 = none
    std::string kind;       // start | step | pull | fight-end | rest | death | wipe | revive | pause | resume | done | stop
    std::string why;
};

struct MemberView
{
    std::string name, className, role, powerType;
    uint32_t guid = 0;      // the character's guid counter (what the roster calls "guid")
    uint32_t level = 0, hp = 0, maxHp = 0, power = 0, maxPower = 0;
    bool alive = true, lead = false;
    uint32_t map = 0, zone = 0;
    float x = 0.0f, y = 0.0f, z = 0.0f;
    int subgroup = -1;      // raid subgroup, -1 outside a raid group
};

struct BossView
{
    uint32_t entry = 0;
    std::string name;
    bool read = false;      // the run read this boss dead or alive at least once (RunRec::bossDead has the entry)
    bool dead = false;
};

struct RunView
{
    std::string leaderName;
    uint32_t leaderGuid = 0;
    std::string commanderName;      // empty = no `.tb master` commander: the leader stands in
    uint32_t commanderGuid = 0;
    bool commanderOnline = false;
    uint32_t groupId = 0;

    std::string planName;
    uint32_t planMap = 0;
    std::vector<StepView> steps;
    std::map<int, std::string> legNames;

    std::string state;              // running | paused | done | stopped (run::StateName)
    size_t step = 0;                // 0-based, == steps.size() once past the last step
    std::string phase, note;
    bool ended = false;             // run::Ended
    bool done = false;              // the policy's own state is Done
    bool stopped = false;
    std::string stopReason;
    uint32_t startedMs = 0;         // the run's own clock, only the (leader, startedMs) key of the summary ledger
    int64_t startedUnix = 0, endedUnix = 0;
    int64_t changedUnix = 0;
    uint32_t deaths = 0, wipes = 0;

    std::vector<EventView> events;  // oldest first (the whole ring)
    std::vector<MemberView> members; // the leader first
    std::vector<BossView> bosses;   // the plan's boss steps, in plan order, one row per entry
};

inline std::string Lower(std::string s)
{
    for (char& c : s)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// A stop reason that names a wipe ("party wiped", "wipe limit"). The run's stop text is free text, so this is a word match.
inline bool StopIsWipe(std::string const& reason)
{
    return Lower(reason).find("wipe") != std::string::npos;
}

// running | cleared | stopped | wiped_out | partial. cleared needs a Done run in which EVERY plan boss was read and read dead: a boss the run
// never read is not a boss it killed (a "done" run that skipped one is partial).
inline std::string Outcome(RunView const& run)
{
    if (!run.ended)
        return "running";
    if (run.stopped)
        return StopIsWipe(run.stopReason) ? "wiped_out" : "stopped";
    if (!run.done)
        return "partial";
    for (BossView const& boss : run.bosses)
        if (!boss.read || !boss.dead)
            return "partial";
    return "cleared";
}

inline size_t CurrentStep(RunView const& run)
{
    return std::min(run.step, run.steps.size());
}

// The leg the run stands in: the current step's, else the last step's (a finished run), else none.
inline int CurrentLeg(RunView const& run)
{
    if (run.steps.empty())
        return -1;
    return run.steps[std::min(run.step, run.steps.size() - 1)].leg;
}

inline std::string LegName(RunView const& run, int leg)
{
    auto it = run.legNames.find(leg);
    return it == run.legNames.end() ? std::string() : it->second;
}

// The plan steps listed on the card: the current leg's, windowed by the Turtle rule (CT::RunStepWindow), at most CT::kMaxRunSteps.
// Steps are in (leg, seq) order, so a leg is one run of rows.
inline std::vector<CT::RunStepInfo> StepWindow(RunView const& run)
{
    std::vector<CT::RunStepInfo> out;
    if (run.steps.empty())
        return out;
    size_t const current = std::min(run.step, run.steps.size() - 1);
    int const leg = run.steps[current].leg;
    size_t first = current, end = current + 1;
    while (first > 0 && run.steps[first - 1].leg == leg)
        --first;
    while (end < run.steps.size() && run.steps[end].leg == leg)
        ++end;
    auto const window = CT::RunStepWindow(first, end, current);
    for (size_t i = window.first; i < window.second; ++i)
    {
        StepView const& s = run.steps[i];
        CT::RunStepInfo info;
        info.index = uint32_t(i + 1);
        info.kind = s.kind;
        info.x = s.x;
        info.y = s.y;
        info.radius = s.radius;
        info.entry = s.entry;
        info.label = s.label;
        out.push_back(std::move(info));
    }
    return out;
}

inline char const* NoteState(std::string const& eventKind)
{
    if (eventKind == "pause")
        return "paused";
    if (eventKind == "done")
        return "done";
    if (eventKind == "stop")
        return "stopped";
    return "running";
}

// The last CT::kMaxRunNotes events, oldest first, as the card's status lines: step = the 1-based step the event names (0 when none),
// kind = that step's kind, phase = the event's own kind, note = its reason.
inline std::vector<CT::RunNoteInfo> Notes(RunView const& run)
{
    std::vector<CT::RunNoteInfo> out;
    size_t const from = run.events.size() > CT::kMaxRunNotes ? run.events.size() - CT::kMaxRunNotes : 0;
    for (size_t i = from; i < run.events.size(); ++i)
    {
        EventView const& e = run.events[i];
        CT::RunNoteInfo n;
        n.at = e.atUnix;
        n.step = e.step >= 0 ? uint32_t(e.step + 1) : 0;
        n.leg = e.leg >= 0 ? LegName(run, e.leg) : std::string();
        n.kind = e.step >= 0 && size_t(e.step) < run.steps.size() ? run.steps[size_t(e.step)].kind : std::string();
        n.phase = e.kind;
        n.state = NoteState(e.kind);
        n.note = e.why;
        out.push_back(std::move(n));
    }
    return out;
}

// One binding per run. orders stay empty and mobs/objects/bosses are not reported (the card greys those layers out).
inline CT::BindingInfo Binding(RunView const& run)
{
    CT::BindingInfo b;
    b.commander = run.commanderName.empty() ? run.leaderName : run.commanderName;
    b.commanderGuid = run.commanderName.empty() ? run.leaderGuid : run.commanderGuid;
    b.commanderOnline = run.commanderOnline;
    b.groupId = run.groupId;
    b.lead = run.leaderName;
    b.boundAt = run.startedUnix;
    for (MemberView const& m : run.members)
    {
        CT::BotInfo bot;
        bot.name = m.name;
        bot.guid = m.guid;
        bot.className = m.className;
        bot.role = m.role;
        bot.level = m.level;
        bot.hp = m.hp;
        bot.maxHp = m.maxHp;
        bot.power = m.power;
        bot.maxPower = m.maxPower;
        bot.powerType = m.powerType;
        bot.alive = m.alive;
        bot.lead = m.lead;
        bot.mapId = m.map;
        bot.zoneId = m.zone;
        bot.x = m.x;
        bot.y = m.y;
        bot.z = m.z;
        bot.subgroup = m.subgroup;
        b.bots.push_back(std::move(bot));
    }
    CT::RunInfo& r = b.run;
    r.present = true;
    r.plan = run.planName;
    r.state = run.state;
    r.steps = uint32_t(run.steps.size());
    r.step = run.steps.empty() ? 0u : uint32_t(std::min(run.step + 1, run.steps.size()));
    if (!run.steps.empty())
    {
        StepView const& s = run.steps[std::min(run.step, run.steps.size() - 1)];
        r.leg = LegName(run, s.leg);
        r.kind = s.kind;
        r.label = s.label;
    }
    r.phase = run.phase;
    r.note = run.note;
    r.changedAt = run.changedUnix ? run.changedUnix : run.startedUnix;
    r.stepList = StepWindow(run);
    r.notes = Notes(run);
    return b;
}

// Should a run be on the Commander tab at `nowUnix`: while it goes on, and for kLingerSecs after it ended.
inline bool OnCommanderTab(RunView const& run, int64_t nowUnix)
{
    if (!run.ended)
        return true;
    return run.endedUnix > 0 && nowUnix - run.endedUnix < kLingerSecs;
}

// The COMMANDERS datagram sequence: while any binding stands, one list per cycle; after the last one is gone, ONE empty list (the dashboard
// drops its cards on it at once instead of waiting for its stale timer), then nothing until a binding stands again.
class CommandersGate
{
public:
    enum class Cycle { Skip, Send, SendEmpty };
    Cycle Next(size_t bindings)
    {
        if (bindings > 0)
        {
            sentNonEmpty_ = true;
            return Cycle::Send;
        }
        if (sentNonEmpty_)
        {
            sentNonEmpty_ = false;
            return Cycle::SendEmpty;
        }
        return Cycle::Skip;
    }

private:
    bool sentNonEmpty_ = false;
};

// One summary file per (leader, startedMs): the key of a run that ended. True the first time a key is seen.
class SummaryLedger
{
public:
    bool MarkIfNew(uint32_t leaderGuid, uint32_t startedMs)
    {
        return seen_.insert({leaderGuid, startedMs}).second;
    }
    bool Contains(uint32_t leaderGuid, uint32_t startedMs) const
    {
        return seen_.count({leaderGuid, startedMs}) != 0;
    }

private:
    std::set<std::pair<uint32_t, uint32_t>> seen_;
};

// The file stem and run id: plan, start second and leader, only [A-Za-z0-9_.-] (the dashboard indexes "<run>.summary.json" by name).
inline std::string RunId(RunView const& run)
{
    std::string id = run.planName + "-" + std::to_string(run.startedUnix) + "-" + std::to_string(run.leaderGuid);
    for (char& c : id)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.' || c == '-'))
            c = '_';
    return id;
}

inline std::string SummaryFileName(RunView const& run)
{
    return RunId(run) + ".summary.json";
}

struct SummaryMeta
{
    std::string moduleVersion = "forever-port";
    std::string coreRevision;
    std::string driver = "human";
};

// The run summary the Dungeon runs tab indexes (internal/dungeons/run.go): outcome, counts, the plan's bosses (killed_at_s is null: the run
// keeps no kill time yet, `killed` says what it read), the party at the end, driver and build. Wipes and deaths are counts (no positions yet).
inline std::string SummaryJson(RunView const& run, SummaryMeta const& meta)
{
    using CT::JsonEscape;
    std::string const status = run.stopped ? "stopped" : (run.done ? "done" : "ended");
    int64_t const ended = run.endedUnix ? run.endedUnix : run.startedUnix;
    int64_t const durationMs = ended > run.startedUnix ? (ended - run.startedUnix) * 1000 : 0;
    std::string out;
    out += "{\"run\":\"" + JsonEscape(RunId(run)) + "\"";
    out += ",\"mode\":\"forever-run\"";
    out += ",\"plan\":\"" + JsonEscape(run.planName) + "\"";
    out += ",\"map\":" + std::to_string(run.planMap);
    out += ",\"dungeon\":{\"map\":" + std::to_string(run.planMap) + "}";
    out += ",\"build\":{\"module\":\"" + JsonEscape(meta.moduleVersion) + "\"";
    if (!meta.coreRevision.empty())
        out += ",\"core\":\"" + JsonEscape(meta.coreRevision) + "\"";
    out += "}";
    out += ",\"driver\":\"" + JsonEscape(meta.driver) + "\"";
    out += ",\"started\":" + std::to_string(run.startedUnix);
    out += ",\"ended\":" + std::to_string(ended);
    out += ",\"duration_ms\":" + std::to_string(durationMs);
    out += ",\"status\":\"" + status + "\"";
    out += ",\"outcome\":\"" + Outcome(run) + "\"";
    if (!run.stopReason.empty())
        out += ",\"stop_reason\":\"" + JsonEscape(run.stopReason) + "\"";
    out += ",\"bosses\":[";
    for (size_t i = 0; i < run.bosses.size(); ++i)
    {
        BossView const& b = run.bosses[i];
        out += std::string(i ? "," : "") + "{\"entry\":" + std::to_string(b.entry) + ",\"name\":\"" + JsonEscape(b.name) +
            "\",\"killed_at_s\":null,\"killed\":" + (b.read && b.dead ? "true" : "false") + "}";
    }
    out += "],\"wipes\":" + std::to_string(run.wipes);
    out += ",\"deaths\":" + std::to_string(run.deaths);
    out += ",\"party\":[";
    for (size_t i = 0; i < run.members.size(); ++i)
    {
        MemberView const& m = run.members[i];
        out += std::string(i ? "," : "") + "{\"class\":\"" + JsonEscape(m.className) + "\",\"role\":\"" + JsonEscape(m.role) +
            "\",\"level\":" + std::to_string(m.level) + "}";
    }
    out += "]}";
    return out;
}

// The role of a host bot from the lines `.tb where` prints (BotMgr::DescribeBrain), as the roster does: the engine's role strategies
// (AiFactory): tanks get "tank assist" / "tank face" / "tank feral", healers "holy" / "discipline" / "restoration". Empty while the
// engine is off. Whole strategy names only.
inline std::string RoleOfBrainLines(std::vector<std::string> const& lines)
{
    std::string const prefix = "combat strategies: ";
    std::string const first = lines.empty() ? std::string() : lines.front();
    if (first.compare(0, 13, "brain: engine") != 0)
        return "";
    std::string combat;
    for (std::string const& line : lines)
        if (line.compare(0, prefix.size(), prefix) == 0)
            combat = line.substr(prefix.size());
    auto const has = [&](char const* name) { return (", " + combat + ",").find(std::string(", ") + name + ",") != std::string::npos; };
    if (has("tank assist") || has("tank face") || has("tank feral"))
        return "tank";
    if (has("holy") || has("discipline") || has("restoration"))
        return "healer";
    return "dps";
}

} // namespace pure
} // namespace DashRuns
} // namespace TortoiseBots
