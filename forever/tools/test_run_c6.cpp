// Standalone check of package C6's contracts with the run leader's decision header (no game types, no server). The tick in forever/src/host/run/TbRun.cpp
// calls these policy functions in this way; the test proves each call does what the tick relies on:
//   * a pull step none of whose members matched a spawn is skipped as not cleared (notCleared + NextStep): the run goes on to the next step, the step is
//     not counted as cleared, a plan that ends that way is Done;
//   * an order the executor refuses (the weak stand-in's "unsupported: ..." answer, or a late failure) pauses the run through OrderRefused, with the order
//     in the note, and `.tb run resume` (ResumeChecked) goes on from it;
//   * a leader off the plan's map pauses (PauseLeadOffMap) and the run goes on by itself once it has been back for the settle time (LeadBackWhilePaused);
//   * `.tb run pause` (PausePlayer) is never resumed by the run itself; `resume` is (ResumeChecked);
//   * a stop (EndRunEarly + RunStopNote) ends the run Done with a "stopped: ..." note; the time limit helper;
//   * the system run's join step (a real player pauses, leaves: resumes) and its flag rules.
//   cd <worktree> && g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/test_run_c6.cpp -o /tmp/test_run_c6 && /tmp/test_run_c6
#include "runtime/DungeonRunPolicy.h"
#include "runtime/SystemRunPolicy.h"

#include <cstdio>
#include <string>

using namespace TortoiseBots;
using namespace TortoiseBots::DungeonRun;

namespace
{
int failures = 0;
int checks = 0;

void Check(bool ok, char const* what)
{
    ++checks;
    if (!ok)
    {
        ++failures;
        std::printf("FAIL: %s\n", what);
    }
}

ai::DungeonPlan MakePlan()
{
    ai::DungeonPlan plan;
    plan.name = "c6_plan";
    plan.map = 389;
    plan.legs = { { 0, "tunnel" } };
    auto add = [&plan](ai::PlanStepKind kind, float x, std::uint32_t entry, std::vector<std::uint32_t> entries)
    {
        ai::PlanStep step;
        step.leg = 0;
        step.seq = std::uint16_t(plan.steps.size());
        step.kind = kind;
        step.entry = entry;
        step.entries = std::move(entries);
        step.x = x;
        step.radius = 8.0f;
        plan.steps.push_back(std::move(step));
    };
    add(ai::PlanStepKind::Move, 10.0f, 0, {});
    add(ai::PlanStepKind::Pull, 30.0f, 0, { 11320 });
    add(ai::PlanStepKind::Pull, 50.0f, 0, { 11321 });
    add(ai::PlanStepKind::Move, 70.0f, 0, {});
    return plan;
}

Facts PartyAt(float x)
{
    Facts f;
    f.leadAlive = true;
    f.leadX = x;
    f.total = f.alive = 1;
    f.leadPathGood = true;      // the facts glue read a walkable path toward the step point, and its first hop 10 yd on
    f.approachRead = f.approachKnown = true;
    f.approachX = x + 10.0f;
    return f;
}

void PullSkip(ai::DungeonPlan const& plan)
{
    Run r = Start(&plan);
    r.step = 1;
    ResetStep(r);
    r.notCleared.insert(1);
    NextStep(r, "pack skipped, not cleared: no member of step 2 matched a spawn of this map");
    Check(r.step == 2, "a skipped pull step moves the run to the next step");
    Check(r.notCleared.count(1) == 1, "the skipped step stays in notCleared (not counted cleared)");
    Check(r.state == State::Running, "the run goes on after a skipped pull");
    r.notCleared.insert(2);
    NextStep(r, "pack skipped");
    Check(r.step == 3 && r.state == State::Running, "two skipped pulls leave the run on the last move");
    NextStep(r, "end");
    Check(r.state == State::Done, "the plan's end is Done");
}

void RefusedOrder(ai::DungeonPlan const& plan)
{
    Run r = Start(&plan);
    std::string order;
    for (int tick = 0; tick < 40 && order.empty(); ++tick)
        order = Tick(r, PartyAt(0.0f), kTickMs);
    Check(!order.empty(), "a fresh run at a move step emits an order within 40 ticks");
    std::printf("  first order: '%s' (phase %s, note '%s')\n", order.c_str(), PhaseName(r.phase), r.note.c_str());
    if (order.empty())
        return;
    OrderRefused(r, order, "unsupported: no order executor is built into this module yet (" + order.substr(0, order.find(' ')) + ")");
    Check(r.state == State::Paused, "a refused order pauses the run");
    Check(r.note.find("unsupported") != std::string::npos, "the pause note carries the executor's answer");
    LeadSpot spot;
    spot.read = true;
    spot.alive = true;
    spot.onPlanMap = true;
    std::string refusal;
    Check(ResumeChecked(r, spot, refusal) && refusal.empty(), "ResumeChecked resumes a refused-order pause");
    Check(r.state == State::Running, "the resumed run is running");
}

void LeadOffMap(ai::DungeonPlan const& plan)
{
    Run r = Start(&plan);
    PauseLeadOffMap(r, plan.map);
    Check(r.state == State::Paused && r.leadOffMapPaused, "PauseLeadOffMap pauses and sets its flag");
    Check(!LeadBackWhilePaused(r, false, kTickMs), "no resume while the leader is away");
    bool resumed = false;
    int ticks = 0;
    while (!resumed && ticks < 30)
    {
        resumed = LeadBackWhilePaused(r, true, kTickMs);
        ++ticks;
    }
    Check(resumed && r.state == State::Running, "the run resumes by itself once the leader has been back for the settle time");
    Check(ticks > 1, "not at the first tick back (the settle time counts)");
}

void CommandPauseAndStop(ai::DungeonPlan const& plan)
{
    Run r = Start(&plan);
    PausePlayer(r, "paused by .tb run pause");
    Check(r.state == State::Paused && PausedByPlayer(r), ".tb run pause is a player's pause");
    bool resumedBySelf = false;
    for (int i = 0; i < 40; ++i)
        resumedBySelf = resumedBySelf || LeadBackWhilePaused(r, true, kTickMs) || CeilingFightOverWhilePaused(r, false, kTickMs);
    Check(!resumedBySelf && r.state == State::Paused, "a player's pause is never resumed by the run itself");
    LeadSpot spot;
    spot.read = true;
    spot.alive = true;
    spot.onPlanMap = true;
    std::string refusal;
    Check(ResumeChecked(r, spot, refusal) && r.state == State::Running, ".tb run resume resumes it");

    std::string const note = RunStopNote("by .tb run stop");
    Check(note == "stopped: by .tb run stop", "RunStopNote words");
    EndRunEarly(r, note);
    Check(r.state == State::Done && r.note.compare(0, 8, "stopped:") == 0, "a stop ends the run Done with its reason in the note");
    Check(RunTimeLimitReached(3u * 60u * 60000u, 180) && !RunTimeLimitReached(3u * 60u * 60000u - 1, 180) && !RunTimeLimitReached(999999999u, 0),
        "the time limit: reached at the minute, 0 = off");
    Check(RunTimeLimitNote(180) == "time limit reached (3h)", "the time limit note");
}

void SystemJoin(ai::DungeonPlan const& plan)
{
    Run r = Start(&plan);
    bool flag = SystemRun::JoinFlagAfterScan(r.state == State::Running, true, false);
    Check(!flag, "the flag is set only by the pause itself");
    Check(SystemRun::DecideJoinStep(true, false, false, true) == SystemRun::JoinStep::Pause, "a real player in a running run: pause");
    PausePlayer(r, SystemRun::kPlayerJoinedNote);
    flag = true;
    Check(SystemRun::DecideJoinStep(false, true, flag, true) == SystemRun::JoinStep::None, "the player is still there: stays paused");
    Check(SystemRun::DecideJoinStep(false, true, flag, false) == SystemRun::JoinStep::Resume, "the player left: resume");
    Check(Resume(r) && r.state == State::Running, "Resume goes on after the join pause");
    Check(SystemRun::JoinFlagAfterScan(true, false, true) == false, "a running run with no player holds no stale flag");
    // A pause that was not for the player (`.tb run pause`) is never resumed by the join step.
    Check(SystemRun::DecideJoinStep(false, true, false, false) == SystemRun::JoinStep::None, "a command pause is not the join's");
}
}

int main()
{
    ai::DungeonPlan const plan = MakePlan();
    PullSkip(plan);
    RefusedOrder(plan);
    LeadOffMap(plan);
    CommandPauseAndStop(plan);
    SystemJoin(plan);
    std::printf("%s: %d checks, %d failed\n", failures ? "FAIL" : "RESULT: PASS", checks, failures);
    return failures ? 1 : 0;
}
