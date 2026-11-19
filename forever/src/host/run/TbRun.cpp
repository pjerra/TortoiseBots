// Slice 5, host package H5: see TbRun.h. Built with TORTOISEBOTS_SLICE1 only.
#include "host/run/TbRun.h"

#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1

#include "ai/playerbot/DungeonPlanMgr.h"
#include "host/run/TbPlanUnits.h"

#include "Group.h"
#include "Player.h"

#include <algorithm>
#include <memory>
#include <set>

namespace TortoiseBots
{
namespace run
{
namespace
{
std::map<ObjectGuid, std::unique_ptr<RunRec>>& Table()
{
    static std::map<ObjectGuid, std::unique_ptr<RunRec>> table;
    return table;
}

// pulls = the pull steps of the plan; done = the pull steps the run has passed (an index below the current step, all of them once the run is done) that
// it did not record as skipped, deferred or given up (the Run's own lists), so a pull the run left uncleared is not counted done.
void CountPulls(RunRec const& rec, uint32& pulls, uint32& done)
{
    pulls = 0;
    done = 0;
    ai::DungeonPlan const* plan = rec.run.plan;
    if (!plan)
        return;
    size_t const passed = rec.run.state == DungeonRun::State::Done ? plan->steps.size() : std::min(rec.run.step, plan->steps.size());
    auto listed = [](std::vector<size_t> const& list, size_t index) { return std::find(list.begin(), list.end(), index) != list.end(); };
    for (size_t i = 0; i < plan->steps.size(); ++i)
    {
        if (plan->steps[i].kind != ai::PlanStepKind::Pull)
            continue;
        ++pulls;
        if (i < passed && !rec.run.notCleared.count(i) && !listed(rec.run.unreachableSkips, i) && !listed(rec.run.noPathSkips, i)
            && !listed(rec.run.deferredPulls, i) && !listed(rec.run.deferGivenUp, i))
            ++done;
    }
}
}

RunRec* Find(ObjectGuid leader)
{
    auto const it = Table().find(leader);
    return it == Table().end() ? nullptr : it->second.get();
}

RunRec* Create(ObjectGuid leader, std::string const& leaderName, ai::DungeonPlan const* plan, uint32 nowMs)
{
    if (!plan || leader.IsEmpty() || Table().count(leader))
        return nullptr;
    auto rec = std::make_unique<RunRec>();
    rec->leader = leader;
    rec->leaderName = leaderName;
    rec->planName = plan->name;
    rec->run = DungeonRun::Start(plan);
    rec->startedMs = nowMs;
    RunRec* raw = rec.get();
    Table()[leader] = std::move(rec);
    return raw;
}

bool Remove(ObjectGuid leader)
{
    return Table().erase(leader) > 0;
}

std::vector<RunRec*> All()
{
    std::vector<RunRec*> out;
    for (auto const& [guid, rec] : Table())
        out.push_back(rec.get());
    std::sort(out.begin(), out.end(), [](RunRec const* a, RunRec const* b) { return a->leaderName < b->leaderName; });
    return out;
}

std::vector<ai::DungeonPlan const*> PlansInUse()
{
    std::vector<ai::DungeonPlan const*> out;
    for (auto const& [guid, rec] : Table())
        if (rec->run.plan && std::find(out.begin(), out.end(), rec->run.plan) == out.end())
            out.push_back(rec->run.plan);
    return out;
}

uint32 AddEvent(RunRec& rec, uint32 nowMs, std::string kind, int32 step, int32 leg, int32 unit, std::string why)
{
    RunEvent event;
    event.seq = ++rec.eventSeq;
    event.tMs = nowMs >= rec.startedMs ? nowMs - rec.startedMs : 0;
    event.kind = std::move(kind);
    event.step = step;
    event.leg = leg;
    event.unit = unit;
    event.why = std::move(why);
    rec.events.push_back(std::move(event));
    while (rec.events.size() > kEventRing)
        rec.events.pop_front();
    return rec.eventSeq;
}

std::string StateName(RunRec const& rec)
{
    return rec.stopped ? "stopped" : DungeonRun::StateName(rec.run.state);
}

h3::Rec RunRecord(RunRec const& rec, uint32 nowMs)
{
    using h3::Rec;
    ai::DungeonPlan const* plan = rec.run.plan;
    size_t const steps = plan ? plan->steps.size() : 0;
    std::string const state = StateName(rec);
    Rec out("run");
    out.S("lead", rec.leaderName).S("plan", rec.planName).S("state", state);
    out.N("step", int64(std::min(rec.run.step, steps))).N("steps", int64(steps));
    // Where the run stands: the leg and kind of the step it is on (absent once it is past the last step: nothing is "current" then).
    if (plan && rec.run.step < steps)
    {
        ai::PlanStep const& step = plan->steps[rec.run.step];
        out.N("leg", step.leg).S("legname", plan->LegName(step.leg)).S("kind", ai::PlanStepKindName(step.kind));
    }
    out.S("phase", DungeonRun::PhaseName(rec.run.phase)).T("note", rec.run.note);
    uint32 pulls = 0, done = 0;
    CountPulls(rec, pulls, done);
    out.N("pull", done).N("pulls", pulls);
    std::vector<std::vector<std::string>> bosses;
    for (auto const& [entry, dead] : rec.bossDead)
        bosses.push_back({ std::to_string(entry), dead ? "dead" : "alive" });
    out.LP("boss", bosses);
    out.N("deaths", rec.deaths).N("wipes", rec.wipes).N("pauses", rec.pauses);
    uint32 const until = rec.endedMs ? rec.endedMs : nowMs;
    out.N("elapsed", until >= rec.startedMs ? (until - rec.startedMs) / 1000u : 0u);
    // why= only when the run is paused (the run's own note says why) or stopped (the reason it was given), as addendum A5 does for `ready`.
    if (state == "stopped")
        out.T("why", rec.stopReason);
    else if (state == "paused")
        out.T("why", rec.run.note);
    if (rec.eventSeq)
        out.N("ev", rec.eventSeq);
    return out;
}

h3::Rec EventRecord(RunEvent const& event)
{
    using h3::Rec;
    Rec out("event");
    out.N("seq", event.seq).N("t", event.tMs / 1000u).S("kind", event.kind);
    if (event.step >= 0)
        out.N("step", event.step);
    if (event.leg >= 0)
        out.N("leg", event.leg);
    if (event.unit >= 0)
        out.N("unit", event.unit);
    out.T("why", event.why);
    return out;
}

void LoadPlans()
{
    ai::LoadDungeonPlans();
    ai::LoadDungeonStepUnits();
}

void AnnotateStateEnd(h3::Rec& end, Player* member)
{
    if (!member)
        return;
    Group* group = member->GetGroup();
    if (!group)
        return;
    if (RunRec const* rec = Find(group->GetLeaderGUID()))
        end.S("run", rec->leaderName).S("runstate", StateName(*rec));
}

// ---- `.tb grammar selftest`: one record of each kind this package writes, built by the real writers from a made-up plan and run ----
void SelfTestRecords(h3::Reply& reply)
{
    using h3::Rec;
    ai::DungeonPlan plan;
    plan.name = "selftest_plan";
    plan.map = 389;
    plan.legs = { { 0, "tunnel" }, { 1, "trogg cave" } };
    auto addStep = [&plan](uint8 leg, uint16 seq, ai::PlanStepKind kind, uint32 entry, std::vector<uint32> entries)
    {
        ai::PlanStep step;
        step.leg = leg;
        step.seq = seq;
        step.kind = kind;
        step.entry = entry;
        step.entries = std::move(entries);
        plan.steps.push_back(std::move(step));
    };
    plan.ccPriority = { 11319, 11324 };
    plan.killFirst = { 11319 };
    std::string const source = "turtle-agent 5c7b6ca dungeons/ragefire_chasm.json";
    addStep(0, 0, ai::PlanStepKind::Move, 0, {});
    addStep(0, 1, ai::PlanStepKind::Pull, 0, { 11320 });
    addStep(0, 2, ai::PlanStepKind::Pull, 0, { 11321 });
    addStep(1, 0, ai::PlanStepKind::Move, 0, {});
    addStep(1, 1, ai::PlanStepKind::Boss, 11517, {});

    reply.Line(PlanRecord(plan, 6, &source));
    reply.Line(LegRecord(plan, 0));
    UnitCheck matched;
    matched.leg = 0; matched.seq = 1; matched.idx = 0; matched.entry = 11320; matched.role = "pack";
    matched.result = UnitResult::Matched; matched.yd = 0.42f; matched.spawnId = 125331;
    reply.Line(UnitRecord(matched, 1));
    UnitCheck optional;
    optional.leg = 0; optional.seq = 2; optional.idx = 1; optional.entry = 8996; optional.role = "pack"; optional.optional = true;
    reply.Line(UnitRecord(optional, -1));

    RunRec rec;
    rec.leaderName = "Mtank";
    rec.planName = plan.name;
    rec.run.plan = &plan;
    rec.run.step = 3;
    rec.run.state = DungeonRun::State::Paused;
    rec.run.phase = DungeonRun::Phase::Walk;
    rec.run.note = "waiting for Mdpsa (dead)";
    rec.startedMs = 1000;
    rec.pauses = 1;
    rec.deaths = 1;
    rec.bossDead[11517] = false;
    AddEvent(rec, 65000, "pull", 2, 0, 11321);
    reply.Line(RunRecord(rec, 126000));
    reply.Line(EventRecord(rec.events.back()));
}
}
}

#endif
