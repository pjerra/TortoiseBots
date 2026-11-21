// Dashboard round 1: see TbDashRuns.h. Built with TORTOISEBOTS_SLICE1 only (the run table is).
#include "dash/TbDashRuns.h"

#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1

#include "compat/botpch_forever.h"

#include "dash/TbDashLink.h"
#include "dash/TbDashRunsPure.h"
#include "host/TbH3.h"
#include "host/cmd/TbCmdBinding.h"
#include "host/cmd/TbCmdFeeds.h"
#include "host/run/TbRun.h"
#include "TbBotMgr.h"

#include "Config.h"
#include "GameTime.h"
#include "GitRevision.h"
#include "Group.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>

namespace TortoiseBots
{
namespace DashRuns
{
namespace
{
namespace P = ::TortoiseBots::DashRuns::pure;
namespace CT = ::TortoiseBots::CommanderTelemetry;

constexpr int ProtocolVersion = 12;           // the dashboard's model.ProtocolVersion (the roster sender writes the same)
constexpr uint32 kRoleRefreshMs = 10000;      // a member's role is re-read from its brain this often (DescribeBrain builds lines)
constexpr uint32 kCostLogMs = 60000;

struct State
{
    bool enabled = true;                      // TortoiseBots.Dashboard.Runs
    uint32 intervalMs = 2000;
    uint32 sinceMs = 0;
    std::string summaryDir;
    bool summaryDirWarned = false;
    P::CommandersGate gate;
    P::SummaryQueue summaries;
    struct Role { std::string role; uint32 atMs = 0; };
    std::map<ObjectGuid, Role> roles;
    // cost of a cycle, logged every kCostLogMs
    uint64 cycles = 0, totalUs = 0;
    uint32 maxUs = 0, sinceLogMs = 0;
};

State& Get()
{
    static State state;
    return state;
}

char const* ClassName(uint8 cls)
{
    switch (cls)
    {
        case CLASS_WARRIOR: return "warrior";
        case CLASS_PALADIN: return "paladin";
        case CLASS_HUNTER: return "hunter";
        case CLASS_ROGUE: return "rogue";
        case CLASS_PRIEST: return "priest";
        case CLASS_SHAMAN: return "shaman";
        case CLASS_MAGE: return "mage";
        case CLASS_WARLOCK: return "warlock";
        case CLASS_DRUID: return "druid";
        default: return "unknown";
    }
}

char const* PowerName(Powers power)
{
    switch (power)
    {
        case POWER_MANA: return "mana";
        case POWER_RAGE: return "rage";
        case POWER_FOCUS: return "focus";
        case POWER_ENERGY: return "energy";
        default: return "";
    }
}

Player* InWorld(ObjectGuid guid)
{
    Player* player = ObjectAccessor::FindPlayer(guid);
    return player && player->IsInWorld() ? player : nullptr;
}

// The same display values the roster uses: rage and runic power are stored in tenths.
uint32 ShownPower(Powers power, int32 value)
{
    int32 const shown = power == POWER_RAGE || power == POWER_RUNIC_POWER ? value / 10 : value;
    return shown < 0 ? 0u : uint32(shown);
}

std::string RoleOf(Player* bot, uint32 nowMs)
{
    State& s = Get();
    State::Role& cached = s.roles[bot->GetGUID()];
    if (cached.atMs == 0 || nowMs - cached.atMs >= kRoleRefreshMs)
    {
        cached.role = P::RoleOfBrainLines(BotMgr::Instance().DescribeBrain(bot->GetGUID()));
        cached.atMs = nowMs ? nowMs : 1;
    }
    return cached.role;
}

P::MemberView MemberOf(Player* player, Group* group, bool lead, uint32 nowMs)
{
    P::MemberView m;
    m.name = player->GetName();
    m.guid = uint32(player->GetGUID().GetCounter());
    m.className = ClassName(player->GetClass());
    m.role = BotMgr::Instance().IsBot(player->GetGUID()) ? RoleOf(player, nowMs) : std::string();
    Powers const power = player->GetPowerType();
    m.powerType = PowerName(power);
    m.level = player->GetLevel();
    m.hp = uint32(player->GetHealth());
    m.maxHp = uint32(player->GetMaxHealth());
    m.power = ShownPower(power, player->GetPower(power));
    m.maxPower = ShownPower(power, player->GetMaxPower(power));
    m.alive = player->IsAlive();
    m.lead = lead;
    m.map = player->GetMapId();
    m.zone = player->GetZoneId();
    m.x = player->GetPositionX();
    m.y = player->GetPositionY();
    m.z = player->GetPositionZ();
    m.subgroup = group && group->isRaidGroup() ? int(group->GetMemberGroup(player->GetGUID())) : -1;
    return m;
}

std::string CreatureName(uint32 entry)
{
    CreatureTemplate const* tmpl = sObjectMgr.GetCreatureTemplate(entry);
    return tmpl ? tmpl->Name : std::string();
}

// One run as plain data. Reads the run record, the plan it points at (loaded for the whole server life) and the leader's party.
P::RunView ViewOf(run::RunRec const& rec, int64 nowUnix, uint32 nowMs)
{
    P::RunView v;
    v.leaderName = rec.leaderName;
    v.leaderGuid = uint32(rec.leader.GetCounter());
    v.planName = rec.planName;
    ai::DungeonPlan const* plan = rec.run.plan;
    if (plan)
    {
        v.planMap = plan->map;
        for (ai::PlanStep const& step : plan->steps)
        {
            P::StepView s;
            s.leg = step.leg;
            s.kind = ai::PlanStepRowKindName(step);
            s.x = step.x;
            s.y = step.y;
            s.radius = step.radius;
            s.entry = step.entry;
            s.label = step.label;
            v.steps.push_back(std::move(s));
            v.legNames[step.leg] = plan->LegName(step.leg);
        }
        std::set<uint32> listed;
        for (ai::PlanStep const& step : plan->steps)
            if (step.kind == ai::PlanStepKind::Boss && step.entry && listed.insert(step.entry).second)
            {
                P::BossView b;
                b.entry = step.entry;
                b.name = CreatureName(step.entry);
                auto const it = rec.bossDead.find(step.entry);
                b.read = it != rec.bossDead.end();
                b.dead = b.read && it->second;
                v.bosses.push_back(std::move(b));
            }
    }
    v.state = run::StateName(rec);
    v.step = rec.run.step;
    v.phase = DungeonRun::PhaseName(rec.run.phase);
    v.note = rec.run.note;
    v.ended = run::Ended(rec);
    v.done = rec.run.state == DungeonRun::State::Done;
    v.stopped = rec.stopped;
    v.stopReason = rec.stopReason;
    v.startedMs = rec.startedMs;
    v.deaths = rec.deaths;
    v.wipes = rec.wipes;
    auto const unixOf = [&](uint32 ms) { return nowUnix - int64(nowMs >= ms ? (nowMs - ms) / 1000u : 0u); };
    v.startedUnix = unixOf(rec.startedMs);
    v.endedUnix = rec.endedMs ? unixOf(rec.endedMs) : 0;
    for (run::RunEvent const& e : rec.events)
    {
        P::EventView ev;
        ev.atUnix = v.startedUnix + int64(e.tMs / 1000u);
        ev.step = e.step;
        ev.leg = e.leg;
        ev.kind = e.kind;
        ev.why = e.why;
        v.events.push_back(std::move(ev));
    }
    v.changedUnix = v.events.empty() ? v.startedUnix : v.events.back().atUnix;

    // The party: the leader first, then the rest of its group. A leader out of the world (between maps, logged out) leaves the list empty.
    Player* leader = InWorld(rec.leader);
    if (leader)
    {
        Group* group = leader->GetGroup();
        v.groupId = group ? uint32(group->GetGUID().GetCounter()) : 0;
        v.members.push_back(MemberOf(leader, group, true, nowMs));
        if (group)
            for (Group::MemberSlot const& slot : group->GetMemberSlots())
                if (slot.guid != rec.leader)
                    if (Player* member = InWorld(slot.guid))
                        v.members.push_back(MemberOf(member, group, false, nowMs));
    }
    // The commander: the `.tb master` binding of the leader (host/TbH3.h), else the leader stands in (the pure Binding does that).
    ObjectGuid const commander = h3::CommanderGuid(rec.leader);
    if (!commander.IsEmpty())
        if (Player* cmd = InWorld(commander))
        {
            v.commanderName = cmd->GetName();
            v.commanderGuid = uint32(commander.GetCounter());
            v.commanderOnline = true;
        }
    if (v.commanderName.empty())
        v.commanderOnline = leader != nullptr;
    return v;
}

// The job is immutable before it is queued: a `.tb run start` may remove the run record before the file is written.
bool EnqueueSummary(State& s, P::RunView const& view)
{
    if (s.summaryDir.empty() || !view.ended)
        return false;
    P::SummaryMeta meta;
    meta.coreRevision = GitRevision::GetHash();
    P::SummaryJob job;
    job.leaderGuid = view.leaderGuid;
    job.startedMs = view.startedMs;
    job.stem = P::RunId(view);
    job.json = P::SummaryJson(view, meta);
    return s.summaries.Add(std::move(job));
}

bool WriteSummaryFile(State& s, P::SummaryJob const& job)
{
    std::error_code ec;
    std::filesystem::path const dir(s.summaryDir);
    std::filesystem::create_directories(dir, ec);
    if (ec)
        return false;
    std::filesystem::path const file = dir / (job.stem + ".summary.json");
    std::filesystem::path const tmp = file.parent_path() / (job.stem + ".summary.json.tmp");
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    out.write(job.json.data(), std::streamsize(job.json.size()));
    out.close();
    if (!out.good())
        return false;
    std::filesystem::rename(tmp, file, ec);     // the index never sees a half-written summary
    return !ec;
}

// One job per cycle: a good write drops the job, a failed one is retried next cycle (bounded), and only success marks the run done.
void FlushSummaries(State& s)
{
    P::SummaryJob const* job = s.summaries.Next();
    if (!job)
        return;
    uint32 const leaderGuid = job->leaderGuid;
    uint32 const startedMs = job->startedMs;
    if (WriteSummaryFile(s, *job))
    {
        s.summaries.Succeeded(leaderGuid, startedMs);
        s.summaryDirWarned = false;
        return;
    }
    std::string const stem = job->stem;
    if (!s.summaries.Failed(leaderGuid, startedMs))
        sLog.outString("TortoiseBots: dashboard run summary '%s' dropped after %d attempts (the Dungeon runs tab misses it)",
            stem.c_str(), P::SummaryQueue::kMaxAttempts);
    else if (!s.summaryDirWarned)
    {
        s.summaryDirWarned = true;
        sLog.outString("TortoiseBots: dashboard run summary not written to '%s' (retrying; the Dungeon runs tab misses it until it lands)",
            s.summaryDir.c_str());
    }
}

// The run's own end (host/run/TbRun.h Finish): its record is whole now and a new start can remove it before the next cycle.
void OnRunEnded(run::RunRec const& rec)
{
    State& s = Get();
    if (!s.enabled || s.summaryDir.empty())
        return;
    P::RunView const view = ViewOf(rec, int64(GameTime::GetGameTime()), GameTime::GetGameTimeMS());
    EnqueueSummary(s, view);
}

void Cycle(State& s)
{
    int64 const nowUnix = int64(GameTime::GetGameTime());
    uint32 const nowMs = GameTime::GetGameTimeMS();

    std::vector<CT::BindingInfo> bindings;
    std::vector<uint32> shownRunLeaders;
    for (run::RunRec* rec : run::All())
    {
        // An old finished run stays in the run table until its leader starts another: once it left the Commander tab and a summary job
        // holds it (or it is written) it costs nothing here (no party read, no role lookup).
        if (run::Ended(*rec))
        {
            uint32 const leaderGuid = uint32(rec->leader.GetCounter());
            bool const lingering = rec->endedMs && (nowMs - rec->endedMs) / 1000u < uint32(P::kLingerSecs);
            bool const summaryDue = !s.summaryDir.empty() && !s.summaries.Done(leaderGuid, rec->startedMs) && !s.summaries.Holds(leaderGuid, rec->startedMs)
                && !s.summaries.GaveUp(leaderGuid, rec->startedMs);
            if (!lingering && !summaryDue)
                continue;
        }
        P::RunView const view = ViewOf(*rec, nowUnix, nowMs);
        if (view.ended)
            EnqueueSummary(s, view);
        if (P::OnCommanderTab(view, nowUnix))
        {
            bindings.push_back(P::Binding(view));
            shownRunLeaders.push_back(uint32(rec->leader.GetCounter()));
        }
    }
    // P1/P2 (spec section 5): every standing commander binding beside the run cards, then the run cards' own order logs from the card of the same
    // commander (the run feed leaves `orders` empty).
    cmd::AppendBindings(bindings, shownRunLeaders);
    cmd::FillRunOrders(bindings);
    // P4 (spec section 5): the card's map layers - the mobs, bosses and objects of each binding's lead, and what each bot is doing.
    cmd::FillFeeds(bindings);

    FlushSummaries(s);

    P::CommandersGate::Cycle const what = s.gate.Next(bindings.size());
    if (what == P::CommandersGate::Cycle::Skip)
        return;
    uint64 const session = link::Session();
    uint64 const seq = link::NextSeq();
    int64 const tsMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    // An empty list is one datagram (BuildPayloadParts with no binding); a list over the datagram caps is split into parts by the builder.
    for (std::string const& datagram : CT::BuildPayloadParts(ProtocolVersion, session, seq, nowUnix, bindings, tsMs))
        link::Send(datagram);

    // Forget the role cache of members that no run holds any more.
    if (bindings.empty())
        s.roles.clear();
}
}

void LoadConfig()
{
    State& s = Get();
    s.enabled = sConfigMgr->GetBoolDefault("TortoiseBots.Dashboard.Runs", true);
    s.intervalMs = uint32(std::max(500, sConfigMgr->GetIntDefault("TortoiseBots.Dashboard.IntervalMs", 2000)));
    s.summaryDir = sConfigMgr->GetStringDefault("TortoiseBots.Dashboard.RunSummaryDir", "");
    s.summaryDirWarned = false;
    run::SetEndListener(&OnRunEnded);
    link::Load();
}

bool Active()
{
    return Get().enabled && link::Active() && BotMgr::Instance().Enabled();
}

void Update(uint32 diff)
{
    State& s = Get();
    if (!Active())
        return;
    s.sinceMs += diff;
    s.sinceLogMs += diff;
    if (s.sinceMs < s.intervalMs)
        return;
    s.sinceMs = 0;

    auto const started = std::chrono::steady_clock::now();
    Cycle(s);
    uint32 const us = uint32(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started).count());
    ++s.cycles;
    s.totalUs += us;
    s.maxUs = std::max(s.maxUs, us);
    if (s.sinceLogMs >= kCostLogMs)
    {
        sLog.outString("TortoiseBots: dashboard runs feed %llu cycles in the last minute, %llu us each on average, %u us at most",
            (unsigned long long)s.cycles, (unsigned long long)(s.cycles ? s.totalUs / s.cycles : 0), s.maxUs);
        s.cycles = s.totalUs = 0;
        s.maxUs = 0;
        s.sinceLogMs = 0;
    }
}
}
}

#else

namespace TortoiseBots
{
namespace DashRuns
{
void LoadConfig() {}
void Update(uint32) {}
bool Active() { return false; }
}
}

#endif
