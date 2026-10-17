// The run leader's loot step finds the boss corpse where it fell (Uldaman, Revelosh): he joins the previous
// pull and dies about 55 yd from his spawn; the boss step remembers that corpse and the loot step then loots it
// by guid. Without a remembered corpse the plan's entry@point order stays.
//
//   g++ -std=c++17 -Wall -Wextra tools/test_run_loot_corpse_policy.cpp -o /tmp/t && /tmp/t
#include "../runtime/DungeonRunPolicy.h"
#include "../runtime/RemoteCommanderPolicy.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using namespace TortoiseBots;
using namespace TortoiseBots::DungeonRun;
using ai::DungeonPlan;
using ai::PlanStep;
using ai::PlanStepKind;

static PlanStep Step(PlanStepKind kind, float x, float y, float z)
{
    PlanStep s;
    s.kind = kind;
    s.x = x;
    s.y = y;
    s.z = z;
    return s;
}

static Facts Party()
{
    Facts f;
    f.total = 5;
    f.alive = 5;
    f.carrier = "Nobble";
    f.seen = true;
    f.leadAlive = true;
    return f;
}

static DungeonPlan RevelPlan()
{
    DungeonPlan plan;
    plan.name = "t";
    PlanStep boss = Step(PlanStepKind::Boss, -225.6f, 161.2f, -44.5f);
    boss.entry = 6910;
    boss.radius = 10.0f;
    PlanStep loot = Step(PlanStepKind::Loot, -225.6f, 161.2f, -44.5f);
    loot.entry = 6910;
    loot.item = 7741;
    plan.steps = {boss, loot};
    for (PlanStep& s : plan.steps)
        ai::NormalizePlanStep(s);
    return plan;
}

static bool Parse(std::string const& order, RemoteCommander::TargetSpec& spec)
{
    // "loot <target> <bot> <item>": the target token is the second word.
    size_t const a = order.find(' ');
    size_t const b = order.find(' ', a + 1);
    return RemoteCommander::ParseTargetSpec(order.substr(a + 1, b - a - 1), spec);
}

int main()
{
    DungeonPlan const plan = RevelPlan();
    // The boss step sees him alive, the pull ends, his corpse lies 55 yd from the spot.
    Run r = Start(&plan);
    Facts f = Party();
    f.units.push_back(UnitFact());
    f.units[0].guid = 5;
    f.units[0].entry = 6910;
    f.units[0].distLead = 30.0f;
    CHECK(Tick(r, f) == "pullback 5");
    CHECK(r.bossSeenAlive);
    f.anyInCombat = true;
    Tick(r, f);
    f.anyInCombat = false;
    f.units.clear();
    f.bossDeadNearSpot = true;
    f.bossCorpseGuid = 0xF130001AFE000123ull;
    f.bossCorpseX = -284.0f;
    f.bossCorpseY = 145.0f;
    f.bossCorpseZ = -49.0f;
    Tick(r, f);
    CHECK(r.step == 1 && r.note == "boss down");
    CHECK(r.corpses.count(6910) && r.corpses[6910].guid == 0xF130001AFE000123ull);

    // The loot step sends the corpse by guid, not the entry at the spawn point (30 yd search around it).
    Facts g = Party();
    std::string order = Tick(r, g);
    CHECK(order == "loot 0xf130001afe000123 Nobble 7741");
    RemoteCommander::TargetSpec spec;
    CHECK(Parse(order, spec) && !spec.ByEntry() && spec.guid == 0xF130001AFE000123ull);
    // The corpse is 55 yd from the point the old order searched around: out of the 30 yd search.
    float const dx = -284.0f - -225.6f, dy = 145.0f - 161.2f;
    CHECK(std::sqrt(dx * dx + dy * dy) > RemoteCommander::kEntryPointRadius);

    // No corpse remembered (the plan resumed at the loot step): the entry@point order stays.
    Run n = Start(&plan, 1);
    std::string const fallback = Tick(n, Party());
    CHECK(fallback == "loot entry:6910@-225.6,161.2,-44.5 Nobble 7741");
    CHECK(Parse(fallback, spec) && spec.ByEntry() && spec.hasPoint);

    // A boss down without a corpse guid in the facts remembers nothing.
    Run m = Start(&plan);
    Facts h = Party();
    h.bossDeadNearSpot = true;
    h.units.clear();
    m.bossSeenAlive = true;
    Tick(m, h);
    CHECK(m.corpses.empty());

    std::cout << "test_run_loot_corpse_policy: all checks passed\n";
    return 0;
}
