// Standalone check for the WC run 9 fixes in the run leader (runtime/DungeonRunPolicy.h), from live run 8
// (notes/live-wc-run8-2026-11-22.md, forever/docs/WC-R9-NOTES.md):
//   g++ -std=c++17 -Wall -Wextra tools/test_run_wc_r9_policy.cpp -o /tmp/test_run_wc_r9_policy && /tmp/test_run_wc_r9_policy
//  - the pull's healer gate (HealerOutOfReach): no pull goes out while no living healer stands within kPullHealerMaxYd of
//    the tank in its line of sight, for at most kPullHealerHoldMs, then the pull goes out with a note that says so;
//  - the walk back's pull cap (Facts::walkBackPullCap): a host that raises it keeps pulling the respawned packs on the way.
#include "../runtime/DungeonRunPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using namespace TortoiseBots::DungeonRun;
using ai::DungeonPlan;
using ai::PlanStep;
using ai::PlanStepKind;

static PlanStep Step(PlanStepKind kind, float x = 0, float y = 0, float z = 0)
{
    PlanStep s;
    s.kind = kind;
    s.x = x;
    s.y = y;
    s.z = z;
    ai::NormalizePlanStep(s);
    return s;
}

static PlanStep PullStep(float x, std::vector<std::uint32_t> entries)
{
    PlanStep s;
    s.kind = PlanStepKind::Pull;
    s.x = x;
    s.entries = entries;
    ai::NormalizePlanStep(s);
    return s;
}

static Facts Party(float x = 0)
{
    Facts f;
    f.total = 5;
    f.alive = 5;
    f.leadX = x;
    f.carrier = "Tanky";
    f.seen = true;
    return f;
}

static MemberFact Mem(const std::string& name, float yd, bool healer = false, bool sight = false)
{
    MemberFact m;
    m.name = name;
    m.yd = yd;
    m.x = yd;
    m.healer = healer;
    m.healerSight = sight;
    return m;
}

static UnitFact Unit(std::uint64_t guid, std::uint32_t entry, float dist)
{
    UnitFact u;
    u.guid = guid;
    u.entry = entry;
    u.distLead = dist;
    return u;
}

// A pull step, the party gathered (every member within kGatherYd of the tank), a healer at `healerYd`.
static Facts PullParty(bool gate, float healerYd, bool sight)
{
    Facts f = Party(60);
    f.units = {Unit(7, 5761, 40)};
    f.healerPullGate = gate;
    f.members = {Mem("Tanky", 0), Mem("Wcpri", healerYd, true, sight), Mem("Wcmag", 8), Mem("Wcrog", 9)};
    f.maxMemberYd = std::max(healerYd, 9.0f);
    if (f.maxMemberYd > kGatherYd)
        f.maxMemberYd = kGatherYd; // the ordinary gather is satisfied: only the healer gate is under test
    return f;
}

// Run 8: Wctank died first at the pull, 24 of the 77 deaths; the pull went out with the priest beyond heal range or around a
// corner. The gate holds the pull in the gather phase until a healer covers the tank.
static void TestHealerGateHoldsThePull()
{
    DungeonPlan plan;
    plan.steps = {PullStep(100, {5761})};

    // The old behaviour (no gate asked for), whatever the healer does: the pull goes out.
    {
        Run r = Start(&plan);
        Facts f = PullParty(false, 40.0f, false);
        CHECK(Tick(r, f) == "pullback 7");
    }
    // A healer 40 yd off: held, in the gather phase, the party told to form up on the tank, the note names the healer.
    {
        Run r = Start(&plan);
        Facts f = PullParty(true, 40.0f, true);
        std::string const order = Tick(r, f);
        CHECK(order.rfind("move ", 0) == 0); // the gather order
        CHECK(r.phase == Phase::Gather);
        CHECK(r.note == "pull held: the healer Wcpri is 40.0 yd from the tank (more than 30.0)");
        CHECK(Tick(r, f).empty()); // still held, no second gather order, no pull
        // The healer walks in, in sight: the pull goes out.
        f.members[1].yd = 14.0f;
        CHECK(Tick(r, f) == "pullback 7");
        CHECK(r.phase == Phase::AwaitPull);
    }
    // Close but out of sight (a corner of the cave): held for the same reason.
    {
        Run r = Start(&plan);
        Facts f = PullParty(true, 12.0f, false);
        Tick(r, f);
        CHECK(r.phase == Phase::Gather && r.note == "pull held: the healer Wcpri has no line of sight to the tank");
        f.members[1].healerSight = true;
        CHECK(Tick(r, f) == "pullback 7");
    }
    // 30 yd exactly is covered; past it is not.
    {
        Run r = Start(&plan);
        Facts f = PullParty(true, kPullHealerMaxYd, true);
        f.maxMemberYd = kGatherYd; // keep the ordinary gather out of it
        CHECK(Tick(r, f) == "pullback 7");
    }
}

// A party with no healer, a healer that is itself the lead bot, a raid, a frozen healer and a second healer that covers the
// tank are none of the gate's business.
static void TestHealerGateLeavesOtherPartiesAlone()
{
    DungeonPlan plan;
    plan.steps = {PullStep(100, {5761})};
    {
        Run r = Start(&plan);
        Facts f = PullParty(true, 12.0f, true);
        f.members.erase(f.members.begin() + 1); // no healer in the party
        CHECK(Tick(r, f) == "pullback 7");
    }
    {
        Run r = Start(&plan);
        Facts f = PullParty(true, 40.0f, false);
        f.members[0].healer = true; // the lead bot is the healer
        f.members[1].healer = false;
        CHECK(Tick(r, f) == "pullback 7");
    }
    {
        Run r = Start(&plan);
        Facts f = PullParty(true, 40.0f, false);
        f.members.push_back(Mem("Wcpal", 10.0f, true, true)); // a second healer covers the tank
        CHECK(Tick(r, f) == "pullback 7");
    }
    {
        Run r = Start(&plan);
        Facts f = PullParty(true, 40.0f, false);
        f.members[1].frozen = true;
        CHECK(Tick(r, f) == "pullback 7");
    }
    {
        std::string who;
        Facts f = PullParty(true, 40.0f, false);
        CHECK(HealerOutOfReach(f, who) && !who.empty());
        f.raid.on = true;
        CHECK(!HealerOutOfReach(f, who));
    }
}

// The hold is bounded: a healer that never gets sight must not stop an unattended run. The pull goes out after
// kPullHealerHoldMs in the gather phase, and its note says the gate was spent.
static void TestHealerGateIsBounded()
{
    DungeonPlan plan;
    plan.steps = {PullStep(100, {5761})};
    Run r = Start(&plan);
    Facts f = PullParty(true, 12.0f, false);
    std::uint32_t ms = 0;
    std::string order;
    while (ms < 3 * kPullHealerHoldMs)
    {
        order = Tick(r, f);
        ms += kTickMs;
        if (order == "pullback 7")
            break;
    }
    CHECK(order == "pullback 7");
    CHECK(ms >= kPullHealerHoldMs && ms <= kPullHealerHoldMs + 3 * kTickMs);
    CHECK(r.note.find("healer gate spent: the healer Wcpri has no line of sight to the tank") != std::string::npos);
    CHECK(r.healerHoldMs == 0); // the pull went out: the next pull gets a fresh hold

    // Codex review of the first version: the hold ran on the gather phase's clock, so an unrelated gather that had already run
    // 40 s let the pull go out at once with the healer round the corner. The gate has its own clock now.
    Run q = Start(&plan);
    EnterPhase(q, Phase::Gather);
    q.phaseMs = 40000;
    Facts g = PullParty(true, 12.0f, false);
    std::string held = Tick(q, g);
    CHECK(held != "pullback 7");
    CHECK(q.note == "pull held: the healer Wcpri has no line of sight to the tank");
    std::uint32_t held_ms = kTickMs;
    while (held_ms < 3 * kPullHealerHoldMs && Tick(q, g) != "pullback 7")
        held_ms += kTickMs;
    CHECK(held_ms >= kPullHealerHoldMs && held_ms <= kPullHealerHoldMs + 3 * kTickMs);
    // A healer that covers the tank starts the next hold afresh: 20 s uncovered, covered, 20 s uncovered again does not spend it.
    Run w = Start(&plan);
    Facts h = PullParty(true, 12.0f, false);
    for (int i = 0; i < 20; ++i)
        CHECK(Tick(w, h) != "pullback 7");
    CHECK(w.healerHoldMs == 20 * kTickMs);
    h.members[1].healerSight = true;
    h.units[0].distLead = 40;
    // (the covered tick pulls: reset by the pull; the clock is zero again)
    CHECK(Tick(w, h) == "pullback 7");
    CHECK(w.healerHoldMs == 0);
}

// The walk back's hop pulls wait for the healer too: a respawned pack on the way back is pulled with the healer in reach.
static void TestHealerGateHoldsTheWalkBackPull()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 200), Step(PlanStepKind::Move, 230), PullStep(400, {1})};
    Run r = Start(&plan, 2);
    r.walkBack = true;
    Facts f = PullParty(true, 40.0f, true);
    f.units.clear();
    f.hopUnits = {Unit(9, 3, 20)};
    CHECK(Tick(r, f).rfind("move ", 0) == 0); // held: the gather order, no pull
    CHECK(r.walkBackPulls == 0 && r.phase == Phase::Gather);
    f.members[1].yd = 10.0f;
    CHECK(Tick(r, f) == "pullback 9");
    CHECK(r.walkBackPulls == 1);
}

// Run 8, step 82: after a wipe the walk back from the entrance meets the packs that respawned (7200 s) and pulls them on
// the way; the policy's cap of 8 walk-back pulls a step paused the run. A host that knows its packs respawn raises it.
static void TestWalkBackPullCapIsTheHostsToRaise()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 200), Step(PlanStepKind::Move, 230), PullStep(400, {1})};
    // Default (0 = kMaxWalkBackPulls): the ninth pull pauses, as before.
    {
        Run r = Start(&plan, 2);
        r.walkBack = true;
        r.walkBackPulls = kMaxWalkBackPulls;
        Facts f = Party(0);
        f.hopUnits = {Unit(9, 3, 20)};
        CHECK(Tick(r, f).empty());
        CHECK(r.state == State::Paused);
        CHECK(r.note.find("walk back: 8 pulls on the way did not clear it") != std::string::npos);
    }
    // Raised to 40: the ninth pull goes out; the 41st pauses with the raised number in its text.
    {
        Run r = Start(&plan, 2);
        r.walkBack = true;
        r.walkBackPulls = kMaxWalkBackPulls;
        Facts f = Party(0);
        f.walkBackPullCap = 40;
        f.hopUnits = {Unit(9, 3, 20)};
        CHECK(Tick(r, f) == "pullback 9");
        CHECK(r.state == State::Running && r.walkBackPulls == kMaxWalkBackPulls + 1);

        Run q = Start(&plan, 2);
        q.walkBack = true;
        q.walkBackPulls = 40;
        Tick(q, f);
        CHECK(q.state == State::Paused);
        CHECK(q.note.find("walk back: 40 pulls on the way did not clear it") != std::string::npos);
    }
}

int main()
{
    TestHealerGateHoldsThePull();
    TestHealerGateLeavesOtherPartiesAlone();
    TestHealerGateIsBounded();
    TestHealerGateHoldsTheWalkBackPull();
    TestWalkBackPullCapIsTheHostsToRaise();
    std::cout << "ok\n";
    return 0;
}
