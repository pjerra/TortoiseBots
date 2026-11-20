// Standalone check of package G8 (forever/docs/SLICE8-G-NOTES.md): the revive-tolerant gather (TbRunGatherPure.h) and the crowd-control facts of the run:
//   g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/test_run_gather.cpp -o /tmp/test_run_gather && /tmp/test_run_gather
#include "forever/src/host/run/TbRunFactsPure.h"
#include "forever/src/host/run/TbRunGatherPure.h"
#include "runtime/DungeonRunPolicy.h"

#include <cstdio>

namespace DR = TortoiseBots::DungeonRun;
namespace gt = TortoiseBots::run::gather;
namespace fx = TortoiseBots::run::facts;

static int g_checks = 0, g_fail = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static std::string const kGatherPause = "regroup: the party did not gather within 60 s (a bot is 72.5 yd from the lead bot)";

static void TestGather()
{
    // The pause note the policy writes is the one the glue recognises (the text is the policy's own: a change there breaks this test, not the live run).
    CHECK(gt::IsRegroupGatherPause(kGatherPause));
    CHECK(!gt::IsRegroupGatherPause("the party did not gather within 30 s (a bot is 40 yd from the lead bot)"));   // a pull's gather: left alone
    CHECK(!gt::IsRegroupGatherPause("regroup: 2 still dead after 240 s (cmd revive <bot>, then .bot run resume)"));
    CHECK(!gt::IsRegroupGatherPause(""));

    // The first pause of a step is extended whatever the distance did (a bot that drinks first has not moved yet) ...
    gt::Extension s;
    gt::Decision d = gt::Decide(s, 56, kGatherPause, 72.5f, true);
    CHECK(d.extend && s.count == 1 && !d.why.empty());
    // ... the second needs the farthest member 8 yd closer than at the first pause: not moving, the pause stands
    d = gt::Decide(s, 56, kGatherPause, 72.0f, true);
    CHECK(!d.extend && s.count == 1);
    d = gt::Decide(s, 56, kGatherPause, 60.0f, true);       // closing in: 12.5 yd closer
    CHECK(d.extend && s.count == 2);
    d = gt::Decide(s, 56, kGatherPause, 40.0f, true);
    CHECK(d.extend && s.count == 3);
    // the cap: three extensions per step, then the policy's pause stands even for a member that is closing in
    d = gt::Decide(s, 56, kGatherPause, 10.0f, true);
    CHECK(!d.extend && s.count == 3 && !d.why.empty());
    // a new step starts the count over
    d = gt::Decide(s, 61, kGatherPause, 80.0f, true);
    CHECK(d.extend && s.count == 1 && s.step == 61);
    // a lost bot (another map, not in the world: the facts say 1000 yd) and a dead lead are no walk-back
    gt::Extension t;
    CHECK(!gt::Decide(t, 1, kGatherPause, 1000.0f, true).extend);
    CHECK(!gt::Decide(t, 1, kGatherPause, 50.0f, false).extend);
    CHECK(t.count == 0);
    // any other pause: no answer at all (the caller says nothing)
    d = gt::Decide(t, 1, "paused by .tb run pause", 50.0f, true);
    CHECK(!d.extend && d.why.empty() && t.count == 0);
}

// The crowd-control facts: a unit read as polymorphed carries the aura, its time left and its immunity into the policy's UnitFact (every list the policy reads).
static void TestUnitCcFacts()
{
    fx::EngagedObs e;
    e.handle = 9; e.entry = 11322; e.polymorphed = true; e.ccRemainMs = 31000; e.ccImmune = DR::CcBit(DR::CcFamily::Polymorph);
    DR::UnitFact u = fx::EngagedFact(e);
    CHECK(u.polymorphed && u.ccRemainMs == 31000 && (u.ccImmune & DR::CcBit(DR::CcFamily::Polymorph)));
    fx::EngagedObs plain;
    plain.handle = 10;
    u = fx::EngagedFact(plain);
    CHECK(!u.polymorphed && u.ccRemainMs == 0 && u.ccImmune == 0);
}

// With a mage that knows Polymorph and the facts of a Cultist (humanoid, not immune) in the plan's cc list, the policy's own pick is `cc <mark> <handle> Mmage`: the
// order the executor now carries out. Without the member fact (the state before G8: every cc field at its default) the policy picks nothing.
static void TestPolicyPick()
{
    ai::DungeonPlan plan;
    plan.map = 389;
    plan.ccPriority = { 11319, 11324, 11322 };
    plan.ccAlways = { 11319, 11324 };
    plan.killFirst = { 11319 };
    ai::PlanStep step;
    step.kind = ai::PlanStepKind::Pull;
    step.entry = 11321;
    step.radius = 10.0f;
    plan.steps.push_back(step);
    DR::Run run;
    run.plan = &plan;
    run.step = 0;

    DR::Facts f;
    DR::UnitFact pullUnit, cultist, shaman;
    pullUnit.guid = 1; pullUnit.entry = 11321; pullUnit.creatureType = DR::kCreatureElemental; pullUnit.distLead = 20.0f;
    cultist.guid = 2; cultist.entry = 11322; cultist.creatureType = DR::kCreatureHumanoid; cultist.distLead = 21.0f;
    shaman.guid = 3; shaman.entry = 11319; shaman.creatureType = DR::kCreatureHumanoid; shaman.distLead = 22.0f;
    f.units = { pullUnit, cultist, shaman };
    f.raidIconsRead = true;
    DR::MemberFact mage;
    mage.name = "Mmage";
    f.members.push_back(mage);

    DR::CcPick none = DR::PickCcOrderPlan(run, f, pullUnit.guid);
    CHECK(none.unit == nullptr);                                // no member knows crowd control (before G8)
    f.members[0].polymorph = true;
    DR::CcPick pick = DR::PickCcOrderPlan(run, f, pullUnit.guid);
    CHECK(pick.unit && pick.unit->guid == 2 && pick.bot == "Mmage" && pick.mark == "moon");
    CHECK(DR::CcOrderText(pick, 2) == "cc moon 2 Mmage");      // never the kill_first Shaman 11319, never the pull target
    // an immune unit and a unit the mage's family does not fit are never picked
    f.units[1].ccImmune = DR::CcBit(DR::CcFamily::Polymorph);
    CHECK(DR::PickCcOrderPlan(run, f, pullUnit.guid).unit == nullptr);
    f.units[1].ccImmune = 0;
    f.units[1].creatureType = DR::kCreatureElemental;
    CHECK(DR::PickCcOrderPlan(run, f, pullUnit.guid).unit == nullptr);
}

int main()
{
    TestGather();
    TestUnitCcFacts();
    TestPolicyPick();
    std::printf("test_run_gather: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
