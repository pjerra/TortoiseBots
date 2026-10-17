// Standalone check for the run leader's run back (runtime/DungeonRunPolicy.h,
// RunBack; the owner's rule of 2026-10-17): a party member coming back with
// attackers keeps running to the party, and the party waits where it stands.
// The places are the Uldaman case it comes from (tests/uldaman-events-2, pull
// 58): the tank at -98 311, the revived priest fighting a scorpid at -171 316,
// 76 yd away (a complete navmesh path of 83 yd to the tank).
//   g++ -std=c++17 -Wall -Wextra tools/test_party_help_policy.cpp -o /tmp/test_party_help_policy && /tmp/test_party_help_policy
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

static PlanStep Step(PlanStepKind kind, float x, float y, float z)
{
    PlanStep s;
    s.kind = kind;
    s.x = x;
    s.y = y;
    s.z = z;
    ai::NormalizePlanStep(s);
    return s;
}

static DungeonPlan Plan()
{
    DungeonPlan plan;
    plan.name = "t";
    PlanStep pull = Step(PlanStepKind::Pull, -89.55f, 271.9f, -47.55f); // pull 58
    pull.entries = {7011};
    pull.radius = 10.0f;
    ai::NormalizePlanStep(pull);
    plan.steps = {Step(PlanStepKind::Move, -97.9f, 310.8f, -50.5f), pull};
    return plan;
}

static MemberFact Member(const std::string& name, float yd, std::uint32_t attackers = 0)
{
    MemberFact m;
    m.name = name;
    m.yd = yd;
    m.attackers = attackers;
    return m;
}

// The party at the tank, the priest coming back with attackers.
static Facts Party(float priestYd = 76.0f, std::uint32_t attackers = 2)
{
    Facts f;
    f.total = 5;
    f.alive = 5;
    f.carrier = "Tank";
    f.leadX = -98.0f;
    f.leadY = 311.0f;
    f.leadZ = -51.0f;
    f.seen = true;
    f.members = {Member("Tank", 0), Member("Rogue", 4), Member("Rogue2", 5), Member("Mage", 6),
        Member("Priest", priestYd, attackers)};
    f.maxMemberYd = priestYd;
    f.anyInCombat = attackers > 0;
    return f;
}

// A run in its regroup (after a resume), the revive already out.
static Run RegroupingRun(const DungeonPlan& plan)
{
    Run r = Start(&plan, 1);
    r.state = State::Paused;
    CHECK(Resume(r));
    Facts f = Party(76.0f, 0);
    f.dead = 1;
    f.alive = 4;
    CHECK(Tick(r, f) == "revive party"); // the revive goes out before any run back
    return r;
}

// A returning bot under attack keeps running: it is listed for the glue (a
// passive follower of the lead bot), and the party sends no order: it never
// walks out to meet it, not even the regroup's gather.
static void TestReturningBotKeepsRunningPartyStays()
{
    DungeonPlan plan = Plan();
    Run r = RegroupingRun(plan);
    for (float yd = 76.0f; yd > kRunBackArriveYd; yd -= 6.0f)
    {
        Facts f = Party(yd, 2);
        CHECK(Tick(r, f).empty());
        CHECK(r.runBack.size() == 1 && r.runBack[0] == "Priest");
        CHECK(r.note == "regroup: Priest running back with 2 attackers");
    }
    CHECK(r.runBacks == 1);
    // Arrived with its attackers: the run back ends there, and the party
    // fights them where it stands (its own group help); the run sends no move
    // and no attack order.
    Facts f = Party(8.0f, 2);
    std::string const order = Tick(r, f);
    CHECK(r.runBack.empty());
    CHECK(order.compare(0, 5, "move ") != 0);
    CHECK(order.compare(0, 7, "attack ") != 0);
}

// A bot that dies on the way is let go; nobody walks to its body.
static void TestDiesOnTheWay()
{
    DungeonPlan plan = Plan();
    Run r = RegroupingRun(plan);
    Facts f = Party(70.0f, 1);
    CHECK(Tick(r, f).empty());
    CHECK(r.note == "regroup: Priest running back with 1 attacker");
    f.members.pop_back();
    f.dead = 1;
    f.alive = 4;
    f.anyInCombat = false;
    std::string const order = Tick(r, f);
    CHECK(r.runBack.empty());
    CHECK(order.empty() || order == "revive party");
}

// An event hold (the Keepers, Archaedas) or the walk into it: no run back.
static void TestEventHoldNoRunBack()
{
    DungeonPlan plan;
    plan.name = "t";
    PlanStep hold = Step(PlanStepKind::Hold, -98, 311, -51);
    hold.radius = 30.0f;
    hold.timeoutS = 600;
    hold.entries = {4857};
    ai::NormalizePlanStep(hold);
    plan.steps = {Step(PlanStepKind::Move, -98, 311, -51), hold};
    for (std::size_t from : {std::size_t(1), std::size_t(0)})
    {
        Run r = Start(&plan, from);
        r.state = State::Paused;
        CHECK(Resume(r));
        Facts f = Party(76.0f, 2);
        for (int i = 0; i < 3; ++i)
        {
            Tick(r, f);
            CHECK(r.runBack.empty());
        }
    }
}

// Only a bot with attackers, more than kGatherYd and at most kRunBackMaxYd out,
// never the lead bot, and only while the run waits for its party.
static void TestWhoRunsBack()
{
    DungeonPlan plan = Plan();
    // No attackers: an ordinary straggler (the gather handles it).
    Run r1 = RegroupingRun(plan);
    Facts a = Party(76.0f, 0);
    Tick(r1, a);
    CHECK(r1.runBack.empty());
    // Within kGatherYd: already with the party.
    Run r2 = RegroupingRun(plan);
    Facts b = Party(kGatherYd - 1.0f, 2);
    Tick(r2, b);
    CHECK(r2.runBack.empty());
    // Too far.
    Run r3 = RegroupingRun(plan);
    Facts c = Party(kRunBackMaxYd + 1.0f, 2);
    Tick(r3, c);
    CHECK(r3.runBack.empty());
    // The lead bot itself (the carrier) is never listed.
    Run r4 = RegroupingRun(plan);
    Facts d = Party(76.0f, 0);
    d.members[0].attackers = 3;
    d.members[0].yd = 50.0f;
    Tick(r4, d);
    CHECK(r4.runBack.empty());
    // A step walking on (no regroup, no gather, no walk back): no run back.
    Run r5 = Start(&plan, 0);
    Facts e = Party(76.0f, 2);
    Tick(r5, e);
    CHECK(r5.runBack.empty());
}

// Bounded: a bot that gains no ground (the scorpid pit floor has no navmesh
// path out) is let go after kWalkRetryMs, one past kRunBackCapMs too, and not
// listed again in that wait. A pause clears the list.
static void TestBounds()
{
    DungeonPlan plan = Plan();
    Run r = RegroupingRun(plan);
    Facts f = Party(66.0f, 2);
    Tick(r, f);
    CHECK(r.runBack.size() == 1);
    for (std::uint32_t ms = 0; ms < kWalkRetryMs; ms += kTickMs)
        Tick(r, f);
    CHECK(r.runBack.empty() && r.runBackDone.count("Priest"));
    Tick(r, f);
    CHECK(r.runBack.empty());
    // The cap: steady progress, but too slow to arrive.
    Run r2 = RegroupingRun(plan);
    float yd = 119.0f;
    Facts g = Party(yd, 2);
    Tick(r2, g);
    std::uint32_t ms = 0;
    for (; ms <= kRunBackCapMs && !r2.runBack.empty(); ms += kTickMs)
    {
        yd -= 0.6f; // 6 yd per 10 s: never stalled
        g = Party(yd, 2);
        Tick(r2, g);
    }
    CHECK(r2.runBack.empty() && ms + kTickMs >= kRunBackCapMs);
    // A pause (a player's order) ends every run back.
    Run r3 = RegroupingRun(plan);
    Facts h = Party(76.0f, 2);
    Tick(r3, h);
    CHECK(!r3.runBack.empty());
    CHECK(PauseForPlayer(r3, "cmd attack 1"));
    CHECK(r3.runBack.empty());
}

// The party in a fight of its own (the lead bot fighting a patrol): a healer
// standing back 25 yd with one attacker is not called in, it keeps healing.
static void TestNoNewRunBackWhileTheLeadFights()
{
    DungeonPlan plan = Plan();
    Run r = RegroupingRun(plan);
    Facts f = Party(25.0f, 1);
    f.leadInCombat = true;
    f.anyInCombat = true;
    for (int i = 0; i < 3; ++i)
    {
        Tick(r, f);
        CHECK(r.runBack.empty());
    }
}

// While a bot is listed, the run's fight handling still runs: the plan's
// kill_first unit is focused, and the fight ceiling still bounds the fight.
static void TestFightHandlingRunsWhileListed()
{
    DungeonPlan plan = Plan();
    plan.killFirst = {7012};
    Run r = RegroupingRun(plan);
    Facts f = Party(60.0f, 2);
    CHECK(Tick(r, f).empty());
    CHECK(r.runBack.size() == 1);
    // The bot closes in (not stalled) and a kill_first unit is in the fight.
    UnitFact k;
    k.guid = 555;
    k.entry = 7012;
    k.distLead = 10.0f;
    k.engaged = true;
    f = Party(50.0f, 2);
    f.killFirst = {k};
    CHECK(Tick(r, f) == "focus 555");
    CHECK(r.runBack.size() == 1);
    CHECK(r.note == "regroup: Priest running back with 2 attackers" || r.note.find("kill 7012 first") != std::string::npos);
    // The fight ceiling still counts while it is listed.
    CHECK(r.fighting && r.fightMs > 0);
}

int main()
{
    TestNoNewRunBackWhileTheLeadFights();
    TestFightHandlingRunsWhileListed();
    TestReturningBotKeepsRunningPartyStays();
    TestDiesOnTheWay();
    TestEventHoldNoRunBack();
    TestWhoRunsBack();
    TestBounds();
    std::cout << "test_party_help_policy: ok\n";
    return 0;
}
