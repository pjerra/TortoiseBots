// Standalone check for runtime/DungeonRunPolicy.h (the run leader's step machine):
//   g++ -std=c++17 -Wall -Wextra tools/test_dungeon_run_policy.cpp -o /tmp/test_dungeon_run_policy && /tmp/test_dungeon_run_policy
#include "../runtime/DungeonRunPolicy.h"
#include "../runtime/RemoteCommanderPolicy.h"

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
namespace RemoteCommander = TortoiseBots::RemoteCommander;

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

static PlanStep PullStep(float x, std::vector<std::uint32_t> entries, std::uint32_t flags = 0)
{
    PlanStep s;
    s.kind = PlanStepKind::Pull;
    s.x = x;
    s.entries = entries;
    s.flags = flags;
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
    f.seen = true;  // the step's point within kSeeYd; tests of far steps clear it
    return f;
}

// A living member `yd` from the lead bot, standing on the x axis at that
// distance (the stand-still watch reads its position).
static MemberFact Mem(const std::string& name, float yd, float dz = 0.0f)
{
    MemberFact m;
    m.name = name;
    m.yd = yd;
    m.dz = dz;
    m.x = yd;
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

static void TestMove()
{
    DungeonPlan plan;
    plan.name = "t";
    plan.steps = {Step(PlanStepKind::Move, 50, 0, 0), Step(PlanStepKind::MoveDirect, 52, 0, 0)};
    Run r = Start(&plan);
    Facts f = Party(0);
    CHECK(Tick(r, f) == "move 50.0 0.0 0.0");
    CHECK(r.phase == Phase::Walk);
    // Progress: no new order.
    f.leadX = 20;
    CHECK(Tick(r, f).empty());
    // Arrived: next step, the direct move goes out at once.
    f.leadX = 48;
    CHECK(Tick(r, f).empty());
    CHECK(r.step == 1);
    CHECK(Tick(r, f) == "move 52.0 0.0 0.0 direct");
    f.leadX = 52;
    Tick(r, f);
    CHECK(r.state == State::Done);
    CHECK(StatusLine(r) == "TBM:RUN|t|done|2/2|0|move_direct|begin|plan done");
}

static void TestWalkStuckIsBounded()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 50, 0, 0)};
    Run r = Start(&plan);
    Facts f = Party(0);
    int orders = 0;
    for (int i = 0; i < 200 && r.state == State::Running; ++i)
        if (!Tick(r, f).empty())
            ++orders;
    CHECK(r.state == State::Paused);
    CHECK(orders == static_cast<int>(kWalkTries));
    CHECK(r.note.find("stuck") != std::string::npos);
}

// Live (WC run B, rows 28/30/31 and 20 more): a plan row 3-6 yd from the tank
// costs ~13 s -- `cmd move` to a point that close does not move the lead bot at
// all, and the walk waited 12 s before it counted it as arrived. 275 s of a
// 101 min run. A plain walk that gains nothing is done after 3 s now.
static void TestShortHopArrivesWithoutTheRetryWait()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 5, 0, 0), Step(PlanStepKind::Rest)};
    Run r = Start(&plan);
    Facts f = Party(0);
    CHECK(Tick(r, f) == "move 5.0 0.0 0.0");
    CHECK(r.step == 0);
    for (int i = 0; i < 4 && r.step == 0 && r.state == State::Running; ++i)
        CHECK(Tick(r, f).empty());
    CHECK(r.state == State::Running && r.step == 1 && r.note == "arrived");
    // The anchor is where the lead bot stands, not the point it could not reach.
    CHECK(r.anchorKnown && r.anchorX == 0.0f);
}

// A direct step is the plan's own line over a navmesh hole: it keeps the 12 s
// rule (and its checkpoint), so a hop it cannot finish is not cut short.
static void TestShortDirectHopKeepsTheLongWait()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::MoveDirect, 5, 0, 0), Step(PlanStepKind::Rest)};
    Run r = Start(&plan);
    Facts f = Party(0);
    CHECK(Tick(r, f) == "move 5.0 0.0 0.0 direct");
    for (int i = 0; i < 11; ++i)
    {
        CHECK(Tick(r, f).empty());
        CHECK(r.step == 0);
    }
    CHECK(Tick(r, f).empty());
    CHECK(r.step == 1 && r.note == "arrived");
}

// ... and a hop the lead bot does walk is not cut short: the walk goes on while
// it gains ground (more than 2 yd a look), and it arrives at its point.
static void TestShortHopThatMovesIsNotCut()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 20, 0, 0), Step(PlanStepKind::Rest)};
    Run r = Start(&plan);
    Facts f = Party(26); // 6 yd out: inside kStalledArriveYd, not at the point
    CHECK(Tick(r, f) == "move 20.0 0.0 0.0");
    f.leadX = 23.5f;
    CHECK(Tick(r, f).empty());
    CHECK(r.step == 0 && !r.anchorKnown);
    f.leadX = 21.0f;
    CHECK(Tick(r, f).empty());
    CHECK(r.step == 1 && r.note == "arrived");
    CHECK(r.anchorKnown && r.anchorX == 20.0f); // arrived at the plan's point, not on its own spot
}

// Live bug: a move point just off the mesh; the lead bot stopped 4.6 yd short.
static void TestStalledNearPointCountsAsArrived()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 50, 0, 0), Step(PlanStepKind::Rest)};
    Run r = Start(&plan);
    Facts f = Party(0);
    CHECK(!Tick(r, f).empty());
    f.leadX = 45.4f;
    for (int i = 0; i < 30 && r.step == 0 && r.state == State::Running; ++i)
        CHECK(Tick(r, f).empty());
    CHECK(r.state == State::Running && r.step == 1 && r.note == "arrived");
    // Still farther than kStalledArriveYd: retries, then the stuck pause.
    Run r2 = Start(&plan);
    f.leadX = 0;
    Tick(r2, f);
    f.leadX = 40;
    for (int i = 0; i < 200 && r2.state == State::Running; ++i)
        Tick(r2, f);
    CHECK(r2.state == State::Paused && r2.note.find("stuck") != std::string::npos);
}

// Live (Ragefire Chasm r6-19z24 slot 1 step 38, r6 slot 1b step 70, r2-19z19 slot 1 step 53): the stand
// stood 3-4 yd above or below the lead bot on a ledge, the whole party within 7 yd; the 3D distance was
// 6.0-6.7 yd, over kStalledArriveYd, so the walk retried three times and paused ("the lead bot is stuck
// 6.7 yd from ...") for 70 s to 4 min, and one resume fixed it. A stand within kStandZTolYd above or below
// counts by its ground distance: the stalled arrival ends the walk.
static void RunStalledWalk(float leadX, float leadZ, PlanStepKind kind, bool& arrived, bool& stuck)
{
    DungeonPlan plan;
    plan.steps = {Step(kind, 0, 0, 0), Step(PlanStepKind::Rest)};
    Run r = Start(&plan);
    Facts f = Party(leadX);
    f.leadZ = leadZ;
    Tick(r, f);
    for (int i = 0; i < 300 && r.step == 0 && r.state == State::Running; ++i)
        Tick(r, f);
    arrived = r.state == State::Running && r.step == 1 && r.note == "arrived";
    stuck = r.state == State::Paused && r.note.find("stuck") != std::string::npos;
}

static void TestStandOnALedgeCountsByGroundDistance()
{
    bool arrived = false, stuck = false;
    // r6-19z24: 6.0 yd, the stand 3 yd lower (ground 5.2 yd).
    RunStalledWalk(5.2f, 3.0f, PlanStepKind::Move, arrived, stuck);
    CHECK(arrived && !stuck);
    // r6 slot 1b: 6.7 yd, the lead 4 yd above the target (ground 5.4 yd).
    RunStalledWalk(5.4f, 4.0f, PlanStepKind::Move, arrived, stuck);
    CHECK(arrived && !stuck);
    // r2-19z19: 6.5 yd, the lead 3.6 yd above, and a stand below the lead bot.
    RunStalledWalk(5.4f, 3.6f, PlanStepKind::Move, arrived, stuck);
    CHECK(arrived && !stuck);
    RunStalledWalk(-5.4f, -3.6f, PlanStepKind::Move, arrived, stuck);
    CHECK(arrived && !stuck);
    // The edge of the tolerance counts (6.0 yd on the ground, 4.5 above).
    RunStalledWalk(5.9f, kStandZTolYd, PlanStepKind::Move, arrived, stuck);
    CHECK(arrived && !stuck);
    // A different floor (more than kStandZTolYd above or below) keeps the 3D rule
    // (beyond the follow deadband on the ground: TestStalledLeadInFollowRangeCountsAsArrived).
    RunStalledWalk(5.5f, kStandZTolYd + 0.5f, PlanStepKind::Move, arrived, stuck);
    CHECK(!arrived && stuck);
    RunStalledWalk(4.0f, 5.5f, PlanStepKind::Move, arrived, stuck); // 6.8 yd in 3D, steeper than the slope bound
    CHECK(!arrived && stuck);
    // Farther than kStalledArriveYd on the ground plane is still stuck.
    RunStalledWalk(6.5f, 3.0f, PlanStepKind::Move, arrived, stuck);
    CHECK(!arrived && stuck);
    // A direct step keeps the 3D rule.
    RunStalledWalk(5.4f, 4.0f, PlanStepKind::MoveDirect, arrived, stuck);
    CHECK(!arrived && stuck);
}

// Zul'Farrak r16-19z34 slot 2, step 37 (ZF-STEP37-2026-10-22): after a fight the lead
// bot stood on the ramp 4.84 yd from the point on the ground and 4.6 yd above it (6.6 yd
// in 3D). A `cmd move` there never moved it: the executor's "return to stay position"
// (MoveToPositionAction::isUseful) acts only beyond the dungeon follow range on the ground.
// The guard's walk back to the stand retried three times and paused the run. A stalled
// plain walk inside kFollowDeadbandYd on the point's floor is arrived.
static void TestStalledLeadInFollowRangeCountsAsArrived()
{
    bool arrived = false, stuck = false;
    // The step's own walk at the r16 numbers: arrived (it paused before).
    RunStalledWalk(4.84f, 4.6f, PlanStepKind::Move, arrived, stuck);
    CHECK(arrived && !stuck);
    RunStalledWalk(-4.84f, -4.6f, PlanStepKind::Move, arrived, stuck);
    CHECK(arrived && !stuck);
    // The edge of the deadband counts: 5.0 yd on the ground, 4.6 above.
    RunStalledWalk(kFollowDeadbandYd, 4.6f, PlanStepKind::Move, arrived, stuck);
    CHECK(arrived && !stuck);
    // ... pinned to the config default (5.0 yd) by a literal, so a smaller constant turns red.
    RunStalledWalk(5.0f, 4.6f, PlanStepKind::Move, arrived, stuck);
    CHECK(arrived && !stuck);
    RunStalledWalk(5.05f, 4.6f, PlanStepKind::Move, arrived, stuck); // 6.8 yd in 3D
    CHECK(!arrived && stuck);
    // A 45 degree ramp and a real second floor look the same to the policy without
    // navmesh evidence (5.0 yd on the ground, 5.0 above: the old ledge test's "other
    // floor"). Accepted: the walk only counts after a 3 s stall that the executor
    // itself caused.
    RunStalledWalk(5.0f, 5.0f, PlanStepKind::Move, arrived, stuck); // 7.1 yd in 3D
    CHECK(arrived && !stuck);
    // A rise a little steeper than the ground distance is still the same floor (4.0 on the
    // ground, 4.8 above: 6.2 yd in 3D, within kSharedFloorRiseYd).
    RunStalledWalk(4.0f, 4.8f, PlanStepKind::Move, arrived, stuck);
    CHECK(arrived && !stuck);
    // Just beyond kFollowDeadbandYd on the ground: stuck. The executor itself may still
    // not move the bot here: the core's GetDistance2d subtracts the bot's bounding radius
    // (Object.cpp WorldObject::GetDistance2d, SizeFactor::BoundingRadius by default) and
    // ServerFacade::GetDistance2d rounds to 0.1 yd, so its real deadband is about 5.0 +
    // the radius (roughly 5.3-6.0 yd by race); the 5.0 yd constant is conservative.
    RunStalledWalk(5.1f, 4.6f, PlanStepKind::Move, arrived, stuck); // 6.9 yd in 3D
    CHECK(!arrived && stuck);
    // A real other floor below or above: stuck.
    RunStalledWalk(2.0f, 8.0f, PlanStepKind::Move, arrived, stuck);
    CHECK(!arrived && stuck);
    RunStalledWalk(4.84f, 4.84f + kSharedFloorRiseYd + 0.2f, PlanStepKind::Move, arrived, stuck);
    CHECK(!arrived && stuck);
    // A direct step keeps the 3D rule.
    RunStalledWalk(4.84f, 4.6f, PlanStepKind::MoveDirect, arrived, stuck);
    CHECK(!arrived && stuck);

    // The r16 replay: the guard's walk back to the stay anchor (the server-snapped z 17.6).
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 1787.8f, 790.6f, 17.7f), Step(PlanStepKind::Rest)};
    Run r = Start(&plan);
    r.guardReturn = true;
    r.guardX = 1787.8f;
    r.guardY = 790.6f;
    r.guardZ = 17.6f;
    Facts f = Party(0);
    f.leadX = 1783.2f;
    f.leadY = 792.1f;
    f.leadZ = 22.2f;
    CHECK(Tick(r, f) == "move 1787.8 790.6 17.6");
    int ticks = 1;
    while (ticks < 60 && r.guardReturn && r.state == State::Running)
    {
        Tick(r, f);
        ++ticks;
    }
    CHECK(r.state == State::Running && !r.guardReturn);
    CHECK(ticks <= static_cast<int>(kShortStallMs / kTickMs) + 2);
    CHECK(r.anchorKnown && r.anchorX == f.leadX && r.anchorZ == f.leadZ);
    // The step's own walk to the plan point goes on from there and arrives.
    for (int i = 0; i < 60 && r.step == 0 && r.state == State::Running; ++i)
        Tick(r, f);
    CHECK(r.state == State::Running && r.step == 1 && r.note == "arrived");

    // A pull step's walk (an ambush walked through) keeps the stand rule: stuck at the same spot.
    DungeonPlan pull;
    pull.steps = {PullStep(0, {4850}, ai::kPlanFlagAmbush), Step(PlanStepKind::Rest)};
    Run p = Start(&pull);
    Facts g = Party(4.84f);
    g.leadZ = 4.6f;
    for (int i = 0; i < 300 && p.step == 0 && p.state == State::Running; ++i)
        Tick(p, g);
    CHECK(p.state == State::Paused && p.note.find("stuck") != std::string::npos);
}

// Live bug: a scorpid in the Sentinel pit was pulled, fought and leashed home
// over and over for minutes.
static void TestPullLoopOnOneTargetPauses()
{
    DungeonPlan plan;
    plan.steps = {PullStep(10, {7078}), Step(PlanStepKind::Rest)};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.seen = true;
    f.units = {Unit(42, 7078, 10)};
    int pulls = 0;
    for (int i = 0; i < 2000 && r.state == State::Running && r.step == 0; ++i)
    {
        f.anyInCombat = false;
        if (Tick(r, f) == "pullback 42")
        {
            ++pulls;
            // Every pull starts a short fight; the unit survives it.
            f.anyInCombat = true;
            for (int k = 0; k < 5; ++k)
                Tick(r, f);
        }
    }
    CHECK(pulls == static_cast<int>(kMaxPullsPerTarget));
    // Left as out of reach: the step ends "skipped, not cleared", the run goes on.
    CHECK(r.state == State::Running && r.step == 1 && r.note.find("skipped, not cleared") != std::string::npos);
    CHECK(r.unreachable.count(42) == 1);
}

// Live: ten Cleft Scorpids in a pit, each pull fought one that leashed home.
static void TestOutOfReachPackIsSkippedNotCleared()
{
    DungeonPlan plan;
    plan.steps = {PullStep(10, {7078}), Step(PlanStepKind::Move, 30)};
    Run r = Start(&plan);
    Facts f = Party(0);
    for (std::uint64_t g = 1; g <= 10; ++g)
        f.units.push_back(Unit(g, 7078, 10 + g));
    int pulls = 0;
    for (int i = 0; i < 3000 && r.state == State::Running && r.step == 0; ++i)
    {
        f.anyInCombat = false;
        if (Tick(r, f).compare(0, 9, "pullback ") == 0)
        {
            ++pulls;
            f.anyInCombat = true;
            for (int k = 0; k < 5; ++k)
                Tick(r, f);
        }
    }
    CHECK(pulls == static_cast<int>(kFruitlessPulls));
    CHECK(r.state == State::Running && r.step == 1);
    CHECK(r.note.find("skipped, not cleared: 10 units") != std::string::npos);
    // Never pulled again on the way (hop pulls skip them too).
    f.units.clear();
    f.hopUnits = {Unit(3, 7078, 13)};
    CHECK(Tick(r, f) == "move 30.0 0.0 0.0");
}

// Live cascade: no Shaft -> no Staff -> Keystone -> Seal wait, each failing.
static void TestMissingItemPausesOnce()
{
    DungeonPlan plan;
    PlanStep loot = Step(PlanStepKind::Loot, 1, 2, 3);
    loot.entry = 6910;
    loot.item = 7741;
    PlanStep use = Step(PlanStepKind::UseItem);
    use.entry = 7740;
    use.item = 7733;
    PlanStep open = Step(PlanStepKind::Open, 4, 5, 6);
    open.entry = 124371;
    open.item = 7733;
    PlanStep wait = Step(PlanStepKind::WaitObject, 7, 8, 9);
    wait.entry = 124372;
    wait.param = 1;
    plan.steps = {loot, use, open, wait, Step(PlanStepKind::Move, 1)};
    Run r = Start(&plan);
    Facts f = Party(0);
    std::string order = Tick(r, f);
    CHECK(order.compare(0, 5, "loot ") == 0);
    // The corpse is gone: the order is refused. One pause, naming the item.
    OrderRefused(r, order, "not-found: No spawned game object or corpse of that entry is near the point");
    CHECK(r.state == State::Paused && r.note.find("item 7741 missing") != std::string::npos);
    CHECK(Skip(r));
    CHECK(r.missing.count(7741) == 1 && r.state == State::Running && r.step == 1);
    // The medallion is there, but its use needs the Shaft: refused, no pause.
    f.carrierHasEntryItem = true;
    order = Tick(r, f);
    CHECK(order == "use 7740 Tanky");
    OrderRefused(r, order, "busy: The cast failed (spell check 40).");
    CHECK(r.state == State::Running && r.step == 2 && r.missing.count(7733) == 1);
    // No staff: the Keystone and the Seal wait are left out with notes.
    Tick(r, f);
    CHECK(r.state == State::Running && r.step == 3 && r.note.find("left out") != std::string::npos);
    Tick(r, f);
    CHECK(r.state == State::Running && r.step == 4 && r.note.find("open before it") != std::string::npos);
}

// ai::kPlanFlagGoneIsDone (Zul'Farrak's graves): a loot or open row with the flag counts the object being gone as
// done; the same row without the flag pauses as before.
static PlanStep GraveStep(PlanStepKind kind, std::uint32_t flags)
{
    PlanStep s = Step(kind, 1858.7f, 1053.8f, 8.9f);
    s.entry = 128403;
    s.flags = flags;
    ai::NormalizePlanStep(s);
    return s;
}

static void TestGoneChestWithFlagIsDone()
{
    for (PlanStepKind kind : {PlanStepKind::Loot, PlanStepKind::Open})
    {
        // The search answers `not-found` (nothing spawned of that entry near the point): done, one note, no pause.
        DungeonPlan plan;
        plan.steps = {GraveStep(kind, ai::kPlanFlagGoneIsDone), Step(PlanStepKind::Move, 9)};
        Run r = Start(&plan);
        Facts f = Party(0);
        std::string order = Tick(r, f);
        CHECK(order.compare(0, 5, kind == PlanStepKind::Loot ? "loot " : "open ") == 0);
        OrderRefused(r, order, "not-found: No spawned game object of that entry is near the point (or the bot).");
        CHECK(r.state == State::Running && r.step == 1);
        CHECK(r.note.find("object gone: counted as done") != std::string::npos);

        // The object despawned while the bot walked to it: the task fails `invalid-target`: done too.
        Run r2 = Start(&plan);
        order = Tick(r2, f);
        f.carrierBusy = true;
        f.taskReply = 2;
        f.taskText = "invalid-target: The object is gone.";
        Tick(r2, f);
        CHECK(r2.state == State::Running && r2.step == 1);
        CHECK(r2.note.find("object gone: counted as done") != std::string::npos);
    }
}

static void TestGoneChestWithoutFlagPauses()
{
    for (PlanStepKind kind : {PlanStepKind::Loot, PlanStepKind::Open})
    {
        DungeonPlan plan;
        plan.steps = {GraveStep(kind, 0), Step(PlanStepKind::Move, 9)};
        Run r = Start(&plan);
        Facts f = Party(0);
        std::string order = Tick(r, f);
        OrderRefused(r, order, "not-found: No spawned game object of that entry is near the point (or the bot).");
        CHECK(r.state == State::Paused && r.step == 0);
        Run r2 = Start(&plan);
        order = Tick(r2, f);
        f.carrierBusy = true;
        f.taskReply = 2;
        f.taskText = "invalid-target: The object is gone.";
        Tick(r2, f);
        CHECK(r2.state == State::Paused && r2.step == 0);
    }
}

// The flag settles a gone object only: any other refusal (in combat, too far, busy) still pauses, and a row that
// names an item stays a missing-item case.
static void TestGoneIsDoneOnlyForAGoneObject()
{
    DungeonPlan plan;
    plan.steps = {GraveStep(PlanStepKind::Loot, ai::kPlanFlagGoneIsDone), Step(PlanStepKind::Move, 9)};
    Run r = Start(&plan);
    Facts f = Party(0);
    std::string order = Tick(r, f);
    OrderRefused(r, order, "in-combat: The bot is in combat.");
    CHECK(r.state == State::Paused && r.step == 0);

    DungeonPlan withItem;
    PlanStep s = GraveStep(PlanStepKind::Loot, ai::kPlanFlagGoneIsDone);
    s.item = 7741;
    withItem.steps = {s, Step(PlanStepKind::Move, 9)};
    Run r2 = Start(&withItem);
    order = Tick(r2, f);
    OrderRefused(r2, order, "not-found: No spawned game object or corpse of that entry is near the point");
    CHECK(r2.state == State::Paused && r2.step == 0);
}

// After a wipe: resume revives the party and gathers it before the step.
static void TestResumeRegroups()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 40)};
    Run r = Start(&plan);
    r.autoRegroups = kAutoRegroupsPerStep; // the regroups by itself are used up: it pauses
    Facts f = Party(0);
    f.dead = 3;
    f.alive = 2;
    Tick(r, f);
    CHECK(r.state == State::Paused);
    CHECK(Resume(r));
    CHECK(r.autoRegroups == 0);
    CHECK(Tick(r, f) == "revive party");
    CHECK(r.state == State::Running && r.note.find("regroup") == 0);
    CHECK(Tick(r, f).empty());
    // Up again but spread out: gather on the lead bot, then the step.
    f.dead = 0;
    f.alive = 5;
    f.maxMemberYd = 80;
    CHECK(Tick(r, f) == "move 0.0 0.0 0.0");
    f.maxMemberYd = 5;
    CHECK(Tick(r, f) == "move 40.0 0.0 0.0");
    // Revive that never works is bounded.
    Run r2 = Start(&plan);
    r2.autoRegroups = kAutoRegroupsPerStep;
    Facts g = Party(0);
    g.dead = 3;
    g.alive = 2;
    Tick(r2, g);
    Resume(r2);
    for (int i = 0; i < 400 && r2.state == State::Running; ++i)
        Tick(r2, g);
    CHECK(r2.state == State::Paused && r2.note.find("still dead") != std::string::npos);
}

// A far move point (the lead came back from the entrance): navmesh hops.
static void TestFarMoveWalksBackInHops()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 200)};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.seen = false;
    f.approachKnown = true;
    f.approachX = 40;
    CHECK(Tick(r, f) == "move 40.0 0.0 0.0");
    f.leadX = 40;
    CHECK(Tick(r, f).empty());
    f.seen = true;
    CHECK(Tick(r, f) == "move 200.0 0.0 0.0");
}

// The Sentinel ramp: a direct step gathers the party first.
static void TestDirectStepGathersFirst()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::MoveDirect, 5)};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.maxMemberYd = 30;
    CHECK(Tick(r, f) == "move 0.0 0.0 0.0");
    CHECK(Tick(r, f).empty());
    f.maxMemberYd = 4;
    CHECK(Tick(r, f) == "move 5.0 0.0 0.0 direct");
}

// r12-19z29 Uldaman leg 5 step 41 (2026-10-22): the re-gather before the direct step passed on the ground distance
// (Jinkla within kGatherYd) while she stood on the ledge above the ramp's floor (z -45.4 vs -52.3); the glue's own
// test (no navmesh path of at most 40 yd onto the lead bot's spot, not within 5 yd of it on its floor in its sight)
// refused the step too-far. The gate asks the glue's test (MemberFact::withLead) instead.
static Facts LedgeScene(float jinklaYd, bool withLead)
{
    Facts f = Party(0);
    f.directJoinRead = true;
    MemberFact jinkla = Mem("Jinkla", jinklaYd, 6.9f);
    jinkla.withLead = withLead;
    f.members = {Mem("Tanky", 0), Mem("Nathil", 3), jinkla};
    f.maxMemberYd = jinklaYd;
    return f;
}

static void TestDirectGateAsksTheGluesWithLeadTest()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::MoveDirect, 5)};
    Run r = Start(&plan);
    CHECK(DirectJoinWanted(r));
    Facts f = LedgeScene(19.5f, false); // inside kGatherYd on the ground, not with the lead bot
    CHECK(Tick(r, f) == "move 0.0 0.0 0.0");
    CHECK(r.phase == Phase::Gather && r.note == "gathering before a direct step");
    for (int i = 0; i < 5; ++i)
        CHECK(Tick(r, f).empty()); // never the direct step while she is not with the lead bot
    // The summon brought her: with the lead bot, the step goes out.
    f = LedgeScene(1.0f, true);
    CHECK(Tick(r, f) == "move 5.0 0.0 0.0 direct");
    // Facts the glue did not read (no direct step under it): the ground rule as before.
    Run g = Start(&plan);
    Facts old = LedgeScene(19.5f, false);
    old.directJoinRead = false;
    CHECK(Tick(g, old) == "move 5.0 0.0 0.0 direct");
    // Not for other steps: the glue reads no paths for them.
    DungeonPlan move;
    move.steps = {Step(PlanStepKind::Move, 5)};
    Run m = Start(&move);
    CHECK(!DirectJoinWanted(m));
}

// The gather's ceiling before a direct step: the straggler not with the lead bot was summoned already this step
// (the revive's summon) more than kDirectResummonMs ago: it is summoned again instead of the pause.
static void TestDirectGatherCeilingSummonsAgain()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::MoveDirect, 5)};
    Run r = Start(&plan);
    MarkSummoned(r, "Jinkla"); // the mid-step revive's summon (SummonRevived), at runMs 0
    r.runMs = 90000;           // 980.7 -> 1070.7 in the trace
    Facts f = LedgeScene(19.5f, false);
    CHECK(Tick(r, f) == "move 0.0 0.0 0.0");
    std::string order;
    for (std::uint32_t i = 0; i <= kGatherCapMs / kTickMs && order.empty(); ++i)
        order = Tick(r, f);
    CHECK(order == "summon Jinkla");
    CHECK(r.state == State::Running && r.stepSummons["Jinkla"].count == 2);
}

static std::string const kLedgeStep = "move 5.0 0.0 0.0 direct";
static std::string const kLedgeRefusal = "too-far: Jinkla is not with Tankkinz (no navmesh path of at most 40 yd to "
    "its spot, and not within 5 yd of it on its floor in its sight). A direct step takes the lead bot's line: gather "
    "the party on it first.";

// Ticks until an order comes (at most `ms`); the ms waited in `waited`.
static std::string TickUntilOrder(Run& r, const Facts& f, std::uint32_t ms, std::uint32_t& waited)
{
    waited = 0;
    std::string order;
    while (order.empty() && waited <= ms && r.state == State::Running)
    {
        order = Tick(r, f);
        waited += kTickMs;
    }
    return order;
}

// The refused direct step (r12 1070.7): the straggler was summoned this step already (the revive's summon), so the
// once-per-step summon was skipped and the refusal paused the run, which stopped the guard: two more deaths. Now:
// summoned again after kDirectResummonMs (or after it died and came back), at most kDirectSummonCap times a step;
// no pause before kDirectPauseSummons summons and kDirectPauseMs since the first refusal; the pause text as before.
static void TestRefusedDirectStepSummonsAgainAndPausesLate()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::MoveDirect, 5)};
    Run r = Start(&plan);
    MarkSummoned(r, "Jinkla");
    r.summoned.insert("Jinkla");
    r.runMs = 90000;
    Facts with = Party(0);
    with.members = {Mem("Tanky", 0), Mem("Jinkla", 1.0f)};
    with.maxMemberYd = 1.0f;
    CHECK(Tick(r, with) == kLedgeStep);
    OrderRefused(r, kLedgeStep, kLedgeRefusal);
    CHECK(r.state == State::Running); // the summon-once rule paused here
    CHECK(Tick(r, with) == "summon Jinkla");
    CHECK(r.stepSummons["Jinkla"].count == 2);
    // She arrives, the step goes out and is refused again at once: no third summon yet (kDirectResummonMs), no
    // pause (60 s have not passed since the first refusal): the run waits for her, and does not resend every tick.
    std::uint32_t waited = 0;
    CHECK(TickUntilOrder(r, with, kSummonWaitMs, waited) == kLedgeStep);
    OrderRefused(r, kLedgeStep, kLedgeRefusal);
    CHECK(r.state == State::Running);
    CHECK(r.note.find("waiting for it to come") != std::string::npos);
    std::string order = TickUntilOrder(r, with, kDirectRetryMs + kTickMs, waited);
    CHECK(order == kLedgeStep && waited >= kDirectRetryMs);
    // Refused again until kDirectResummonMs since her last summon: then the third summon.
    for (int i = 0; i < 10 && order != "summon Jinkla"; ++i)
    {
        if (order == kLedgeStep)
            OrderRefused(r, kLedgeStep, kLedgeRefusal);
        CHECK(r.state == State::Running);
        order = TickUntilOrder(r, with, kDirectRetryMs + kSummonWaitMs + kTickMs, waited);
    }
    CHECK(order == "summon Jinkla" && r.stepSummons["Jinkla"].count == kDirectSummonCap);
    // The cap: never a fourth summon; once 60 s passed since the first refusal, the pause, with the old text.
    for (int i = 0; i < 20 && r.state == State::Running; ++i)
    {
        order = TickUntilOrder(r, with, kDirectResummonMs + kDirectRetryMs + kSummonWaitMs, waited);
        CHECK(order != "summon Jinkla");
        if (order == kLedgeStep)
            OrderRefused(r, kLedgeStep, kLedgeRefusal);
    }
    CHECK(r.state == State::Paused);
    CHECK(r.stepSummons["Jinkla"].count == kDirectSummonCap);
    CHECK(r.runMs - r.directRefusedMs >= kDirectPauseMs);
    CHECK(r.note.find("cmd " + kLedgeStep + " refused: " + kLedgeRefusal) == 0);
}

// No pause for a refused direct step before kDirectPauseSummons summons, however long: a bot summoned once is
// summoned a second time once kDirectResummonMs passed, before any pause.
static void TestRefusedDirectStepNeedsTwoSummonsBeforeThePause()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::MoveDirect, 5)};
    Run r = Start(&plan);
    Facts with = Party(0);
    with.members = {Mem("Tanky", 0), Mem("Jinkla", 1.0f)};
    with.maxMemberYd = 1.0f;
    CHECK(Tick(r, with) == kLedgeStep);
    OrderRefused(r, kLedgeStep, kLedgeRefusal);
    CHECK(Tick(r, with) == "summon Jinkla"); // the first: never summoned this step
    std::uint32_t waited = 0;
    CHECK(TickUntilOrder(r, with, kSummonWaitMs, waited) == kLedgeStep);
    r.runMs += kDirectPauseMs; // long past the 60 s: still only one summon, so no pause
    r.stepSummons["Jinkla"].lastMs = r.runMs - 1000; // ... and the last one 1 s ago: no summon now either
    OrderRefused(r, kLedgeStep, kLedgeRefusal);
    CHECK(r.state == State::Running && r.stepSummons["Jinkla"].count == 1);
    CHECK(r.note.find("waiting for it to come") != std::string::npos);
    CHECK(Tick(r, with).empty()); // the facts do not say she is with the lead bot: the wait goes on
    // The glue's test finds her with the lead bot: the step goes out at once, not after kDirectRetryMs.
    with.directJoinRead = true;
    CHECK(Tick(r, with) == kLedgeStep);
    // The time bound: 2 x kDirectPauseMs since the first refusal pauses whatever the summons, with the old text.
    r.runMs = r.directRefusedMs + 2 * kDirectPauseMs - kTickMs;
    r.stepSummons["Jinkla"].lastMs = r.runMs - 1000;
    OrderRefused(r, kLedgeStep, kLedgeRefusal);
    CHECK(r.state == State::Running && r.stepSummons["Jinkla"].count == 1);
    r.directWaitFor.clear();
    r.runMs = r.directRefusedMs + 2 * kDirectPauseMs;
    r.stepSummons["Jinkla"].lastMs = r.runMs - 1000;
    OrderRefused(r, kLedgeStep, kLedgeRefusal);
    CHECK(r.state == State::Paused && r.note.find("cmd " + kLedgeStep + " refused: " + kLedgeRefusal) == 0);
}

// The cold review of d158546e: a straggler held in a fight is never summoned (`cmd summon` refuses it), so the
// count rule alone never paused the direct gather: past 2 x kDirectPauseMs it pauses, and not before.
static void TestCombatLockedDirectStragglerPausesOnTime()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::MoveDirect, 5)};
    Run r = Start(&plan);
    MarkSummoned(r, "Jinkla");
    Facts f = LedgeScene(19.5f, false);
    f.members[2].inCombat = true;
    f.members[2].attackers = 1;
    CHECK(Tick(r, f) == "move 0.0 0.0 0.0");
    std::uint64_t pausedAt = 0;
    for (int i = 0; i < 400 && r.state == State::Running; ++i)
    {
        CHECK(Tick(r, f).find("summon") == std::string::npos);
        if (r.state == State::Paused)
            pausedAt = r.runMs;
    }
    CHECK(r.state == State::Paused && r.stepSummons["Jinkla"].count == 1);
    CHECK(r.directRefusedOn && pausedAt - r.directRefusedMs >= 2ull * kDirectPauseMs);
    CHECK(pausedAt - r.directRefusedMs < 2ull * kDirectPauseMs + kGatherCapMs + kTickMs);
    CHECK(r.note.find("the party did not gather within") == 0);
}

// The module's glue as the policy sees it: the test is read on the ticks DirectJoinWanted asked for (with the run
// as the last tick left it); then Jinkla on the ledge is not with the lead bot.
static std::string GlueTick(Run& r, Facts f)
{
    f.directJoinSupported = true;
    f.directJoinRead = DirectJoinWanted(r);
    for (MemberFact& m : f.members)
        if (m.name == "Jinkla")
            m.withLead = !f.directJoinRead;
    return Tick(r, f);
}

// The cold review of d158546e (MED): a walk back passes a pull in the tick it reaches it, so the facts read for
// the pull's index were not the direct step's, and the gate fell back to the ground rule on that tick. The read
// follows WalkBack's own skip (NextWalkBackIdx); a ledge bot never lets the walk back's direct order out.
static void TestWalkBackReadsTheDirectStepPastAPull()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 0), PullStep(50, {1}), Step(PlanStepKind::MoveDirect, 5),
        Step(PlanStepKind::Move, 100)};
    Run r = Start(&plan, 3);
    r.walkBack = true;
    r.walkBackIdx = r.walkBackStart = 0;
    Facts f = LedgeScene(19.5f, false);
    bool sawPull = false;
    for (int i = 0; i < 60; ++i)
    {
        if (r.walkBack && r.walkBackIdx == 1)
        {
            sawPull = true;
            CHECK(DirectJoinWanted(r)); // the point the next tick reaches is the direct step
        }
        std::string const order = GlueTick(r, f);
        CHECK(order.find(" direct") == std::string::npos);
        CHECK(r.state == State::Running);
    }
    CHECK(sawPull && r.walkBack && r.walkBackIdx == 2 && r.phase == Phase::Gather);
    // Down from the ledge (with the lead bot): the direct order goes out.
    f = LedgeScene(1.0f, true);
    f.directJoinSupported = true;
    f.directJoinRead = true;
    CHECK(Tick(r, f) == "move 5.0 0.0 0.0 direct");
}

// The belt: a glue that reads the test did not read it on the tick the gate passed: no order that tick (the note
// as it was), then the read tick sends it; a bounded hold (kDirectJoinHolds), never forever.
static void TestDirectOrderWaitsForTheRead()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::MoveDirect, 5)};
    Run r = Start(&plan);
    Facts f = LedgeScene(1.0f, true);
    f.directJoinSupported = true;
    f.directJoinRead = false;
    CHECK(Tick(r, f).empty() && r.directJoinHolds == 1);
    f.directJoinRead = true;
    CHECK(Tick(r, f) == "move 5.0 0.0 0.0 direct" && r.directJoinHolds == 0);
    Run g = Start(&plan);
    f.directJoinRead = false;
    std::uint32_t held = 0;
    std::string order;
    while (order.empty() && held <= kDirectJoinHolds)
    {
        order = Tick(g, f);
        held += order.empty() ? 1 : 0;
    }
    CHECK(order == "move 5.0 0.0 0.0 direct" && held == kDirectJoinHolds);
    // No read while a walk is under way (the gate is not asked then): the glue saves the paths.
    g.phase = Phase::Walk;
    CHECK(!DirectJoinWanted(g));
    g.phase = Phase::Begin;
    CHECK(DirectJoinWanted(g));
}

// The cold review of d158546e (LOW): the refusal clock belongs to the point; a walk back gone on to another direct
// point starts it over, so a pause due at the first point does not fire at the next one.
static void TestDirectRefusalClockStartsOverAtTheNextPoint()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::MoveDirect, 5), Step(PlanStepKind::MoveDirect, 10), Step(PlanStepKind::Move, 100)};
    Run r = Start(&plan, 2);
    r.walkBack = true;
    r.walkBackIdx = r.walkBackStart = 0;
    r.walkBackDirect = true;
    r.stepSummons["Jinkla"].count = kDirectSummonCap;
    OrderRefused(r, kLedgeStep, kLedgeRefusal);
    CHECK(r.state == State::Running && r.directRefusedIdx == 0);
    NextWalkBackPoint(r);
    r.directWaitFor.clear();
    r.runMs += kDirectPauseMs + 10000;
    OrderRefused(r, "move 10.0 0.0 0.0 direct", kLedgeRefusal);
    CHECK(r.state == State::Running && r.directRefusedIdx == 1 && r.directRefusedMs == r.runMs);
}

// A bot that died and came back since its last summon is a new case: summoned again at once (the cap still holds).
static void TestDirectSummonAfterADeathIsANewCase()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::MoveDirect, 5)};
    Run r = Start(&plan);
    r.runMs = 10000;
    MarkSummoned(r, "Jinkla");
    CHECK(!DirectSummonAllowed(r, "Jinkla"));
    Facts dead = Party(0);
    dead.deadNames = {"Jinkla"};
    Tick(r, dead); // every running tick notes the dead (NoteDeadSeen)
    CHECK(r.deadSeenMs["Jinkla"] == 10000 + kTickMs);
    r.runMs = 15000;
    CHECK(DirectSummonAllowed(r, "Jinkla"));
    r.stepSummons["Jinkla"].count = kDirectSummonCap;
    CHECK(!DirectSummonAllowed(r, "Jinkla"));
    // The step's record starts over with the next step.
    ResetStep(r);
    CHECK(r.stepSummons.empty() && !r.directRefusedOn && r.directWaitFor.empty());
}

// `.bot run skip` while running goes on running.
static void TestSkipWhileRunning()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 40), Step(PlanStepKind::Move, 10)};
    Run r = Start(&plan);
    Facts f = Party(0);
    Tick(r, f);
    CHECK(Skip(r));
    CHECK(r.state == State::Running && r.step == 1 && !r.regroup);
    CHECK(Tick(r, f) == "move 10.0 0.0 0.0");
}

// Live: `cmd move -159.5 281.6 -49.1` refused unreachable (a point up a
// ledge). The glue finds a reachable point near it; the step walks there.
static void TestUnreachableMoveDetours()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 50, 0, 0), Step(PlanStepKind::Rest)};
    Run r = Start(&plan);
    Facts f = Party(0);
    std::string order = Tick(r, f);
    CHECK(order == "move 50.0 0.0 0.0");
    // Too far from the plan's point: not a stand-in.
    CHECK(!UseDetour(r, 30, 0, 0));
    CHECK(UseDetour(r, 44, 2, 0));
    CHECK(r.state == State::Running && r.detour);
    // Only once per step.
    CHECK(!UseDetour(r, 45, 0, 0));
    CHECK(Tick(r, f) == "move 44.0 2.0 0.0");
    f.leadX = 44;
    f.leadY = 2;
    Tick(r, f);
    CHECK(r.step == 1 && r.note.find("nearest reachable point") != std::string::npos);
    // The detour is per step.
    CHECK(!r.detour);
    // Not for a step that is not a move.
    CHECK(!UseDetour(r, 1, 1, 1));
}

static void TestPullGatherAndFight()
{
    DungeonPlan plan;
    plan.steps = {PullStep(40, {4851, 4852})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.seen = true;
    f.units = {Unit(11, 4851, 45), Unit(12, 4852, 38)};
    // Scattered party: gather first (the lead holds where it stands).
    f.maxMemberYd = 35;
    CHECK(Tick(r, f) == "move 0.0 0.0 0.0");
    CHECK(r.phase == Phase::Gather);
    CHECK(Tick(r, f).empty());
    f.maxMemberYd = 5;
    // Nearest first.
    CHECK(Tick(r, f) == "pullback 12");
    CHECK(r.phase == Phase::AwaitPull);
    // The fight: no orders while it lasts.
    f.anyInCombat = true;
    for (int i = 0; i < 30; ++i)
        CHECK(Tick(r, f).empty());
    CHECK(r.note == "fighting");
    // Both died; low mana afterwards: rest, then the step is done.
    f.anyInCombat = false;
    f.units.clear();
    f.minHealerManaPct = 20;
    CHECK(Tick(r, f) == "rest");
    CHECK(r.phase == Phase::Rest);
    f.minHealerManaPct = 85;
    Tick(r, f);
    CHECK(r.state == State::Done || r.step == 1);
}

// RFC r3-19z20 step 22: the lead bot holds "stay" while the run rests (an
// attack order had ended its stay, and its idle formation walk took it into
// the lava basin while the healer drank). Only in the rest phase of a running
// run, only for a living lead out of combat that is off "stay".
static void TestLeadHoldsStayWhileResting()
{
    DungeonPlan plan;
    plan.steps = {PullStep(40, {4851})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.units = {Unit(11, 4851, 38)};
    f.maxMemberYd = 5;
    // Before the pull and during the fight: no hold from here.
    CHECK(Tick(r, f) == "pullback 11");
    CHECK(!LeadStayDueWhileResting(r, true, false, false, false));
    f.anyInCombat = true;
    Tick(r, f);
    CHECK(!LeadStayDueWhileResting(r, true, true, false, false));
    // The fight is over and the healer is dry: the run rests, the lead holds.
    f.anyInCombat = false;
    f.units.clear();
    f.minHealerManaPct = 20;
    CHECK(Tick(r, f) == "rest");
    CHECK(r.phase == Phase::Rest);
    CHECK(LeadStayDueWhileResting(r, true, false, false, false));
    // Sent once: a lead already on "stay" is left alone.
    CHECK(!LeadStayDueWhileResting(r, true, false, true, false));
    // A lead standing in magma is left free to walk out of it.
    CHECK(!LeadStayDueWhileResting(r, true, false, false, true));
    // A dead lead or one pulled into a fight again is not put on "stay".
    CHECK(!LeadStayDueWhileResting(r, false, false, false, false));
    CHECK(!LeadStayDueWhileResting(r, true, true, false, false));
    // Still resting a tick later (the formation walk would start any time).
    Tick(r, f);
    CHECK(r.phase == Phase::Rest);
    CHECK(LeadStayDueWhileResting(r, true, false, false, false));
    // A paused run gives no orders.
    Run paused = r;
    paused.state = State::Paused;
    CHECK(!LeadStayDueWhileResting(paused, true, false, false, false));
    // Rested: the step goes on and the hold is the next order's business.
    f.minHealerManaPct = 85;
    Tick(r, f);
    CHECK(r.phase != Phase::Rest);
    CHECK(!LeadStayDueWhileResting(r, true, false, false, false));
}

static void TestUnseenPackIsApproachedNotCleared()
{
    DungeonPlan plan;
    plan.steps = {PullStep(100, {4851})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.seen = false;
    // The glue gives the next hop on the navmesh path; one move per hop.
    f.approachKnown = true;
    f.approachX = 35;
    CHECK(Tick(r, f) == "move 35.0 0.0 0.0");
    f.leadX = 10;
    CHECK(Tick(r, f).empty());
    CHECK(r.step == 0 && r.state == State::Running);
    // At the hop and still not in sight: the next hop.
    f.leadX = 34;
    CHECK(Tick(r, f).empty());
    f.approachX = 70;
    CHECK(Tick(r, f) == "move 70.0 0.0 0.0");
    f.leadX = 70;
    f.seen = true;
    Tick(r, f);
    CHECK(r.state == State::Done);
    // No path toward it at all: pause, never a straight-line guess.
    Run r2 = Start(&plan);
    Facts g = Party(0);
    g.seen = false;
    Tick(r2, g);
    CHECK(r2.state == State::Paused && r2.note.find("no walkable path") == 0);
    // A hop within kArriveYd of the lead bot goes nowhere: no path at once
    // (it used to "arrive" 8 times first; r3-19z20 B-FIX-2). The hop cap's
    // own test is TestApproachHopCapCountsHopsWithoutProgress.
    Run r3 = Start(&plan);
    g.approachKnown = true;
    g.approachX = 1;
    Tick(r3, g);
    CHECK(r3.state == State::Paused && r3.note.find("no walkable path") == 0);
}

// Ragefire 19z13, trogg cave pull 13: the pack is 70 yd down a ramp. The hop
// toward it was broken off by the pull's gather as soon as the pack came in
// sight, while the anchor was still the previous stand 11 yd higher (more than
// kStragglerDzYd): the gather "went back" to it, the next hop came forward
// again, eight times, then the hop cap. The hop is walked to its end first, and
// its point becomes the anchor.
static void TestFarPackHopIsNotUndoneByTheGatherBack()
{
    DungeonPlan plan;
    plan.steps = {PullStep(-238.0f, {11318})};
    plan.steps[0].y = -36.0f;
    plan.steps[0].z = -56.4f;
    Run r = Start(&plan);
    r.restDue = false;
    r.anchorKnown = true; // the previous step's stand, at the top of the ramp
    r.anchorX = -169.1f; r.anchorY = -34.9f; r.anchorZ = -40.9f;
    Facts f = Party(-169.1f);
    f.leadY = -34.9f; f.leadZ = -40.9f;
    f.seen = false;
    f.approachKnown = true;
    f.approachX = -205; f.approachY = -34; f.approachZ = -52;
    CHECK(Tick(r, f) == "move -205.0 -34.0 -52.0");
    // Half way down the pack comes in sight (60 yd): the hop goes on.
    f.leadX = -200.7f; f.leadY = -35.6f; f.leadZ = -48.5f;
    f.seen = true;
    f.units = {Unit(1, 11318, 38)};
    CHECK(Tick(r, f).empty());
    CHECK(r.phase == Phase::Walk && r.note == "approaching the pack");
    // At the hop's point: it is the anchor and the pull goes out, no gather back.
    f.leadX = -205; f.leadY = -34; f.leadZ = -52;
    f.units = {Unit(1, 11318, 34)};
    CHECK(Tick(r, f) == "pullback 1");
    CHECK(r.anchorX == -205.0f && r.anchorZ == -52.0f);
    CHECK(r.note.find("gathering back") == std::string::npos);
    // A unit of the pack within kHopClearYd, the followers far behind: the hop
    // is over and the pull goes out from here. Neither a gather beside the pack
    // nor a walk back to the old anchor, however many ticks it takes.
    Run q = Start(&plan);
    q.restDue = false;
    q.anchorKnown = true;
    q.anchorX = -169.1f; q.anchorY = -34.9f; q.anchorZ = -40.9f;
    Facts g = Party(-169.1f);
    g.leadY = -34.9f; g.leadZ = -40.9f;
    g.seen = false;
    g.approachKnown = true;
    g.approachX = -205; g.approachY = -34; g.approachZ = -52;
    Tick(q, g);
    g.leadX = -222; g.leadY = -34; g.leadZ = -54;
    g.seen = true;
    g.maxMemberYd = 40;
    g.units = {Unit(1, 11318, 17)};
    bool pulled = false;
    for (int i = 0; i < 40 && q.state == State::Running; ++i)
    {
        std::string const order = Tick(q, g);
        CHECK(order.compare(0, 5, "move ") != 0);
        CHECK(q.note.find("gathering") == std::string::npos);
        if (order == "pullback 1")
        {
            pulled = true;
            break;
        }
    }
    CHECK(pulled && q.state == State::Running && q.pulls == 1 && q.anchorX == -169.1f);
    // The fight starts and ends: the next pull is an ordinary one again.
    g.anyInCombat = true;
    Tick(q, g);
    CHECK(q.state == State::Running && q.fighting);
    // A player's move next to the visible pack, the party behind: the anchor stays too.
    Run m = Start(&plan);
    m.restDue = false;
    m.anchorKnown = true;
    m.anchorX = -169.1f; m.anchorY = -34.9f; m.anchorZ = -40.9f;
    Facts h = g;
    h.units = {Unit(1, 11318, 15)};
    Tick(m, h);
    CHECK(m.anchorX == -169.1f && m.anchorZ == -40.9f);
}

// A hop loop that really gets nowhere still pauses after kMaxApproachHops, with
// how near it got; a long way walked in hops that do get nearer is not capped.
static void TestApproachHopCapCountsHopsWithoutProgress()
{
    DungeonPlan plan;
    plan.steps = {PullStep(1000, {1})};
    Run r = Start(&plan);
    r.restDue = false;
    Facts f = Party(0);
    f.seen = false;
    f.approachKnown = true;
    for (int hop = 1; hop <= 3 * static_cast<int>(kMaxApproachHops); ++hop)
    {
        f.approachX = f.leadX + 40;
        CHECK(Tick(r, f) == "move " + Num(f.approachX) + " 0.0 0.0");
        f.leadX = f.approachX; // the hop is walked, 40 yd nearer
        CHECK(Tick(r, f).empty());
        CHECK(r.state == State::Running);
    }
    // The lead bot is pushed back each time: no progress, 8 hops, the pause.
    Run s = Start(&plan);
    s.restDue = false;
    Facts g = Party(0);
    g.seen = false;
    g.approachKnown = true;
    for (int i = 0; i < 100 && s.state == State::Running; ++i)
    {
        g.approachX = 40;
        Tick(s, g);
        g.leadX = 40;
        Tick(s, g);
        g.leadX = 0; // dragged back
    }
    CHECK(s.state == State::Paused);
    CHECK(s.note.find("8 approach hops did not bring the party within 60.0 yd of the pack") == 0);
    CHECK(s.note.find("nearest it got: 1000.0 yd") != std::string::npos);
}

// Uldaman 19z13, Revelosh loot step 20: the lead bot may not loot the corpse
// (quest item 7741, another member's turn). The step is done once any party bot
// carries the item; else the bot whose turn it is, then each living bot, is
// tried before the run pauses.
static void TestLootStepIsDoneWhenAnyPartyBotHasTheItem()
{
    DungeonPlan plan;
    PlanStep loot = Step(PlanStepKind::Loot, 1, 2, 3);
    loot.entry = 6910;
    loot.item = 7741;
    plan.steps = {loot, Step(PlanStepKind::Move, 9)};
    // Before the order.
    Run r = Start(&plan);
    Facts f = Party(0);
    f.members = {Mem("Tanky", 0), Mem("Healy", 5)};
    f.partyHasItem = true;
    f.itemHolder = "Healy";
    CHECK(Tick(r, f).empty());
    CHECK(r.step == 1 && r.note.find("Healy already carries item 7741") != std::string::npos);
    // After a refusal (Healy took the item while the order was out).
    Run q = Start(&plan);
    Facts g = Party(0);
    g.members = f.members;
    std::string const order = Tick(q, g);
    CHECK(order.compare(0, 5, "loot ") == 0);
    OrderRefused(q, order, "failed: This corpse has nothing this bot may loot under the group's loot rules");
    CHECK(q.state == State::Running && q.step == 0);
    g.partyHasItem = true;
    g.itemHolder = "Healy";
    CHECK(Tick(q, g).empty());
    CHECK(q.step == 1 && q.state == State::Running);
}

static void TestLootRefusalTriesTheTurnBotThenEachBot()
{
    DungeonPlan plan;
    PlanStep loot = Step(PlanStepKind::Loot, 1, 2, 3);
    loot.entry = 6910;
    loot.item = 7741;
    plan.steps = {loot, Step(PlanStepKind::Move, 9)};
    char const* rights = "failed: This corpse has nothing this bot may loot under the group's loot rules (another "
        "member's turn, already looted, or not tapped by the group)";
    Run r = Start(&plan);
    Facts f = Party(0);
    f.members = {Mem("Tanky", 0), Mem("Healy", 5), Mem("Stabby", 6)};
    f.corpseChecked = true;
    f.corpseHasItem = true; // still on the corpse
    std::string order = Tick(r, f);
    CHECK(order.find(" Tanky 7741") != std::string::npos);
    OrderRefused(r, order, rights);
    CHECK(r.state == State::Running);
    // The corpse's round-robin turn is Stabby's: he is tried first.
    f.lootTurn = "Stabby";
    order = Tick(r, f);
    CHECK(order.compare(0, 5, "loot ") == 0 && order.find(" Stabby 7741") != std::string::npos);
    CHECK(r.state == State::Running && r.taskBot == "Stabby");
    OrderRefused(r, order, rights);
    // Then each bot not tried yet.
    order = Tick(r, f);
    CHECK(order.find(" Healy 7741") != std::string::npos);
    // His task is accepted and the item lands in his bags: done.
    f.taskReply = 1;
    f.partyHasItem = true;
    f.itemHolder = "Healy";
    Tick(r, f);
    CHECK(r.state == State::Running && r.step == 1);

    // Everybody refused: one pause naming who was tried and the skip hint.
    Run p = Start(&plan);
    Facts g = f;
    g.partyHasItem = false;
    g.taskReply = 0;
    g.lootTurn.clear();
    for (int i = 0; i < 3; ++i)
    {
        order = Tick(p, g);
        CHECK(order.compare(0, 5, "loot ") == 0);
        OrderRefused(p, order, rights);
    }
    Tick(p, g);
    CHECK(p.state == State::Paused);
    CHECK(p.note.find("(tried Tanky Healy Stabby)") != std::string::npos);
    CHECK(p.note.find("item 7741 missing") != std::string::npos);

    // The item is gone from the corpse and in no bag: no point in trying the
    // rest; the pause says so plainly.
    Run e = Start(&plan);
    Facts h = f;
    h.partyHasItem = false;
    h.taskReply = 0;
    h.corpseHasItem = false;
    order = Tick(e, h);
    OrderRefused(e, order, rights);
    Tick(e, h);
    CHECK(e.state == State::Paused);
    CHECK(e.note.find("item 7741 not on corpse and not in any bag") != std::string::npos);
    CHECK(e.note.find("item 7741 missing") != std::string::npos);
    // A refusal that is not about the loot rules still pauses at once.
    Run n = Start(&plan);
    order = Tick(n, f);
    OrderRefused(n, order, "not-found: No corpse of that entry is near the point");
    CHECK(n.state == State::Paused && n.note.find("not-found") != std::string::npos);
    // A bot off the lead bot's map (yd 1000) is never tried, not even on its round-robin turn.
    Run m = Start(&plan);
    Facts k = f;
    k.taskReply = 0;
    k.partyHasItem = false;
    k.members = {Mem("Tanky", 0), Mem("Healy", 5), Mem("Stabby", 1000)};
    k.lootTurn = "Stabby";
    order = Tick(m, k);
    OrderRefused(m, order, rights);
    order = Tick(m, k);
    CHECK(order.find(" Healy 7741") != std::string::npos && m.taskBot == "Healy");
}

// ---- loot step: free-for-all detour (AiPlayerbot.DungeonRunLootFfa) ----

static char const* const kFfaRights = "failed: This corpse has nothing this bot may loot under the group's loot rules "
    "(another member's turn, already looted, or not tapped by the group)";

static DungeonPlan FfaPlan()
{
    DungeonPlan plan;
    PlanStep loot = Step(PlanStepKind::Loot, 1, 2, 3);
    loot.entry = 6910;
    loot.item = 7741;
    plan.steps = {loot, Step(PlanStepKind::Move, 9)};
    return plan;
}

// The facts of a party under group loot (3) whose lead bot leads it, the key on.
static Facts FfaParty()
{
    Facts f = Party(0);
    f.members = {Mem("Tanky", 0), Mem("Healy", 5), Mem("Stabby", 6)};
    f.lootFfaOn = true;
    f.lootMethod = RemoteCommander::kLootGroup;
    f.leadLeadsGroup = true;
    f.corpseChecked = true;
    f.corpseHasItem = true;
    return f;
}

// Order sequence: loot refused on the rules -> lootmethod ffa -> (its ACK) loot by the carrier -> the task's
// answer -> lootmethod group -> the step ends.
static void TestLootFfaOrderSequence()
{
    DungeonPlan plan = FfaPlan();
    Run r = Start(&plan);
    Facts f = FfaParty();
    f.lootMethod = RemoteCommander::kLootGroup;
    std::string order = Tick(r, f);
    CHECK(order.compare(0, 5, "loot ") == 0 && order.find(" Tanky 7741") != std::string::npos);
    OrderRefused(r, order, kFfaRights);
    // Before NextLootBot: the method is changed first, once.
    CHECK(Tick(r, f) == "lootmethod ffa");
    CHECK(r.lootFfaLive && r.lootRestore == RemoteCommander::kLootGroup && r.lootFfa == LootFfa::Ordered);
    // The order was accepted: the next tick reads ffa and sends the loot to the carrier (not another bot).
    f.lootMethod = RemoteCommander::kLootFfa;
    order = Tick(r, f);
    CHECK(order.compare(0, 5, "loot ") == 0 && order.find(" Tanky 7741") != std::string::npos);
    CHECK(r.orderSent && r.lootFfa == LootFfa::Looting && r.step == 0);
    // The task runs: nothing is sent, the method is not touched.
    f.carrierBusy = true;
    for (int i = 0; i < 3; ++i)
        CHECK(Tick(r, f).empty() && r.lootFfa == LootFfa::Looting);
    // The answer: the method goes back first, then the answer is read.
    f.taskReply = 1;
    CHECK(Tick(r, f) == "lootmethod group");
    CHECK(r.lootFfa == LootFfa::Restoring && r.step == 0);
    f.lootMethod = RemoteCommander::kLootGroup;
    f.carrierBusy = false;
    f.carrierHasItem = true;
    f.partyHasItem = true;
    f.itemHolder = "Tanky";
    Tick(r, f);
    CHECK(r.step == 1 && r.state == State::Running && r.lootFfa == LootFfa::Idle);
    CHECK(r.note.find("loot done") != std::string::npos);
    // The next running tick reads the method back and lets the flag go, with no second order.
    CHECK(Tick(r, f).compare(0, 11, "lootmethod ") != 0);
    CHECK(!r.lootFfaLive && r.lootRestoreTries == 0);
}

// An ERR answer restores too, then the old path goes on (the next bot), without a second ffa.
static void TestLootFfaRestoresOnAnErrAnswerAndNeverTwice()
{
    DungeonPlan plan = FfaPlan();
    Run r = Start(&plan);
    Facts f = FfaParty();
    f.lootMethod = RemoteCommander::kLootRoundRobin;
    OrderRefused(r, Tick(r, f), kFfaRights);
    CHECK(Tick(r, f) == "lootmethod ffa");
    f.lootMethod = RemoteCommander::kLootFfa;
    CHECK(Tick(r, f).compare(0, 5, "loot ") == 0);
    f.carrierBusy = true;
    f.taskReply = 2;
    f.taskText = kFfaRights;
    CHECK(Tick(r, f) == "lootmethod rr"); // the saved method, by the name the order parses
    f.lootMethod = RemoteCommander::kLootRoundRobin;
    f.carrierBusy = false;
    std::string const order = Tick(r, f); // the answer is read: refused on the rules -> the next bot
    CHECK(r.lootFfa == LootFfa::Idle);
    CHECK(order.empty() || order.compare(0, 11, "lootmethod ") != 0);
    for (int i = 0; i < 3; ++i)
    {
        std::string const next = Tick(r, f);
        CHECK(next.compare(0, 11, "lootmethod ") != 0);
        if (next.compare(0, 5, "loot ") == 0)
            OrderRefused(r, next, kFfaRights);
    }
    CHECK(r.lootFfaUsed);
}

// The restore is what the glue sends when the run pauses or stops while the method is ffa: the flag and the
// saved method stay set through a pause (fail-if-removed: lootFfaLive / lootRestore set at the ffa order).
static void TestLootFfaStaysLiveThroughAPause()
{
    DungeonPlan plan = FfaPlan();
    Run r = Start(&plan);
    Facts f = FfaParty();
    f.lootMethod = RemoteCommander::kLootNeedBeforeGreed;
    OrderRefused(r, Tick(r, f), kFfaRights);
    CHECK(Tick(r, f) == "lootmethod ffa");
    f.lootMethod = RemoteCommander::kLootFfa;
    CHECK(Tick(r, f).compare(0, 5, "loot ") == 0);
    // The run pauses (the cap, a player's order) with the loot order out and the method on ffa.
    Pause(r, "something stopped the run");
    CHECK(r.state == State::Paused && r.lootFfaLive && r.lootRestore == RemoteCommander::kLootNeedBeforeGreed);
    CHECK(std::string(RemoteCommander::LootMethodName(r.lootRestore)) == "nbg");
    // A resume starts the step afresh but keeps the flag until the glue has put the method back
    // (a resume can come before its next tick).
    CHECK(Resume(r));
    CHECK(r.lootFfa == LootFfa::Idle && !r.lootFfaUsed && r.lootFfaLive);
}

// The loot task ends with no ACK or ERR line seen (not busy any more, reply 0): the method goes back all the same.
static void TestLootFfaRestoresWhenTheTaskEndsUnanswered()
{
    DungeonPlan plan = FfaPlan();
    Run r = Start(&plan);
    Facts f = FfaParty();
    OrderRefused(r, Tick(r, f), kFfaRights);
    CHECK(Tick(r, f) == "lootmethod ffa");
    f.lootMethod = RemoteCommander::kLootFfa;
    CHECK(Tick(r, f).compare(0, 5, "loot ") == 0);
    f.carrierBusy = true;
    CHECK(Tick(r, f).empty());
    f.carrierBusy = false;
    CHECK(Tick(r, f) == "lootmethod group");
}

// A refused ffa order: the old path (NextLootBot) goes on, the method was never changed.
static void TestLootFfaRefusedFallsBackToTheOldPath()
{
    DungeonPlan plan = FfaPlan();
    Run r = Start(&plan);
    Facts f = FfaParty();
    OrderRefused(r, Tick(r, f), kFfaRights);
    CHECK(Tick(r, f) == "lootmethod ffa");
    OrderRefused(r, "lootmethod ffa", "lead-unavailable: The lead bot does not lead the group.");
    CHECK(r.state == State::Running && r.lootFfa == LootFfa::Idle && !r.lootFfaLive);
    f.lootTurn = "Stabby";
    std::string const order = Tick(r, f);
    CHECK(order.compare(0, 5, "loot ") == 0 && order.find(" Stabby 7741") != std::string::npos);
    CHECK(r.taskBot == "Stabby");
    // An ACK that did not take (the method still reads group): the same.
    Run q = Start(&plan);
    Facts g = FfaParty();
    OrderRefused(q, Tick(q, g), kFfaRights);
    CHECK(Tick(q, g) == "lootmethod ffa");
    std::string const next = Tick(q, g); // g.lootMethod is still group
    CHECK(next.compare(0, 11, "lootmethod ") != 0 && q.lootFfa == LootFfa::Idle);
    CHECK(q.state == State::Running);
}

// A regroup (a wipe, a lead death, a fight cap) resets the step between the ffa ACK and the restore and keeps the
// run Running: the very next tick orders the method back, whatever the step; the tick after reads it and is done.
static void TestLootFfaRegroupBetweenAckAndRestoreRestores()
{
    DungeonPlan plan = FfaPlan();
    Run r = Start(&plan);
    Facts f = FfaParty();
    OrderRefused(r, Tick(r, f), kFfaRights);
    CHECK(Tick(r, f) == "lootmethod ffa");
    f.lootMethod = RemoteCommander::kLootFfa;
    CHECK(Tick(r, f).compare(0, 5, "loot ") == 0);
    f.carrierBusy = true;
    CHECK(Tick(r, f).empty());
    ResetStep(r); // the regroup redoes the step
    CHECK(r.lootFfa == LootFfa::Idle && r.lootFfaLive && r.state == State::Running);
    CHECK(Tick(r, f) == "lootmethod group");
    CHECK(r.lootRestoreTries == 1);
    // It took: the facts read the saved method; no second order, the flag is gone.
    f.lootMethod = RemoteCommander::kLootGroup;
    CHECK(Tick(r, f).compare(0, 11, "lootmethod ") != 0);
    CHECK(!r.lootFfaLive && r.lootRestoreTries == 0 && r.state == State::Running);
    // The redone step may go through the detour once more: the method is not ffa any more.
    CHECK(r.lootFfaUsed == false);
}

// The restore is refused or does not take: one order a tick, three in all, then the run pauses naming the key.
static void TestLootFfaRestoreRefusedRetriesThenPauses()
{
    DungeonPlan plan = FfaPlan();
    Run r = Start(&plan);
    Facts f = FfaParty();
    f.lootMethod = RemoteCommander::kLootFfa;
    r.lootFfaLive = true;
    r.lootRestore = RemoteCommander::kLootNeedBeforeGreed;
    for (int i = 1; i <= 3; ++i)
    {
        std::string const order = Tick(r, f);
        CHECK(order == "lootmethod nbg");
        OrderRefused(r, order, "lead-unavailable: The lead bot does not lead the group.");
        CHECK(r.state == State::Running && r.lootRestoreTries == static_cast<std::uint32_t>(i));
    }
    CHECK(Tick(r, f).empty());
    CHECK(r.state == State::Paused && r.lootFfaLive); // the glue tries once more as the run pauses, and clears the flag
    CHECK(r.note.find("still free-for-all") != std::string::npos);
    CHECK(r.note.find("the lead bot's group") != std::string::npos);
    CHECK(r.note.find("AiPlayerbot.DungeonRunLootFfa") != std::string::npos);
    CHECK(r.note.find("lootmethod nbg") != std::string::npos);
}

// Only "no group" or a method that is not ffa ends the flag; the key switched off meanwhile does not.
static void TestLootFfaFlagEndsOnlyWhenTheMethodIsNotFfa()
{
    DungeonPlan plan = FfaPlan();
    // The key was switched off by a reload while the detour was live: the restore still goes out.
    {
        Run r = Start(&plan);
        Facts f = FfaParty();
        f.lootFfaOn = false;
        f.lootMethod = RemoteCommander::kLootFfa;
        r.lootFfaLive = true;
        r.lootRestore = RemoteCommander::kLootRoundRobin;
        CHECK(Tick(r, f) == "lootmethod rr" && r.lootFfaLive);
    }
    // ... and in the Ordered tick (the key off there, the method ffa): the detour goes on, the flag stays.
    {
        Run r = Start(&plan);
        Facts f = FfaParty();
        OrderRefused(r, Tick(r, f), kFfaRights);
        CHECK(Tick(r, f) == "lootmethod ffa");
        f.lootFfaOn = false;
        f.lootMethod = RemoteCommander::kLootFfa;
        CHECK(Tick(r, f).compare(0, 5, "loot ") == 0 && r.lootFfaLive);
    }
    // No group (-1): nothing to put back.
    {
        Run r = Start(&plan);
        Facts f = FfaParty();
        f.lootMethod = -1;
        r.lootFfaLive = true;
        r.lootRestore = RemoteCommander::kLootGroup;
        CHECK(Tick(r, f).compare(0, 11, "lootmethod ") != 0 && !r.lootFfaLive);
    }
    // A player set another method meanwhile (Ordered tick reads nbg, not ffa, not the saved one): no order, flag gone.
    {
        Run r = Start(&plan);
        Facts f = FfaParty();
        OrderRefused(r, Tick(r, f), kFfaRights);
        CHECK(Tick(r, f) == "lootmethod ffa");
        f.lootMethod = RemoteCommander::kLootNeedBeforeGreed;
        CHECK(Tick(r, f).compare(0, 11, "lootmethod ") != 0);
        CHECK(!r.lootFfaLive && r.lootFfa == LootFfa::Idle);
    }
}

// A bot dies while the detour waits for a busy carrier; the WaitForFallen regroup does not reset the step: the
// detour ends there and the method is ordered back on the next tick (not after the whole regroup).
static void TestLootFfaOrderedThenFallenRegroupRestores()
{
    DungeonPlan plan = FfaPlan();
    Run r = Start(&plan);
    Facts f = FfaParty();
    OrderRefused(r, Tick(r, f), kFfaRights);
    CHECK(Tick(r, f) == "lootmethod ffa");
    f.lootMethod = RemoteCommander::kLootFfa;
    f.carrierBusy = true;
    CHECK(Tick(r, f).empty() && r.lootFfa == LootFfa::Ordered);
    f.dead = 1;
    f.alive = 4;
    f.deadNames = {"Stabby"};
    f.members = {Mem("Tanky", 0), Mem("Healy", 5)};
    Tick(r, f);
    CHECK(r.regroup && r.state == State::Running && r.lootFfa == LootFfa::Idle && r.lootFfaLive);
    // The regroup's ticks: the restore is the first order out.
    CHECK(Tick(r, f) == "lootmethod group");
}

// A carrier that stays busy: the ffa wait is bounded; on the cap the method goes back and the step goes the old way.
static void TestLootFfaOrderedWaitIsBounded()
{
    DungeonPlan plan = FfaPlan();
    Run r = Start(&plan);
    Facts f = FfaParty();
    OrderRefused(r, Tick(r, f), kFfaRights);
    CHECK(Tick(r, f) == "lootmethod ffa");
    f.lootMethod = RemoteCommander::kLootFfa;
    f.carrierBusy = true;
    int ticks = 0;
    while (r.lootFfa == LootFfa::Ordered && ticks < 60)
    {
        CHECK(Tick(r, f).empty());
        ++ticks;
    }
    CHECK(ticks >= 19 && ticks <= 22 && r.lootFfa == LootFfa::Idle && r.state == State::Running);
    CHECK(r.lootFfaLive);
    CHECK(Tick(r, f) == "lootmethod group");
}

// No ffa without the item, without the key, without the lead bot leading the group, for a method the order cannot
// restore, or when the item is gone from the corpse; the shortcuts come first.
static void TestLootFfaConditions()
{
    DungeonPlan plan = FfaPlan();
    // No item on the step: the old path (the next bot).
    DungeonPlan bare = FfaPlan();
    bare.steps[0].item = 0;
    {
        Run r = Start(&bare);
        Facts f = FfaParty();
        OrderRefused(r, Tick(r, f), kFfaRights);
        std::string const order = Tick(r, f);
        CHECK(order.compare(0, 11, "lootmethod ") != 0 && r.lootFfa == LootFfa::Idle);
    }
    // Key off (the facts' default), not the leader, already ffa, no group: the old path.
    for (int variant = 0; variant < 4; ++variant)
    {
        Run r = Start(&plan);
        Facts f = FfaParty();
        if (variant == 0)
            f.lootFfaOn = false;
        else if (variant == 1)
            f.leadLeadsGroup = false;
        else if (variant == 2)
            f.lootMethod = RemoteCommander::kLootFfa;
        else
            f.lootMethod = -1;
        OrderRefused(r, Tick(r, f), kFfaRights);
        for (int i = 0; i < 2; ++i)
            CHECK(Tick(r, f).compare(0, 11, "lootmethod ") != 0);
        CHECK(!r.lootFfaLive && r.lootFfa == LootFfa::Idle);
    }
    // The item is not on the corpse and in no bag: no point (the usual pause).
    {
        Run r = Start(&plan);
        Facts f = FfaParty();
        OrderRefused(r, Tick(r, f), kFfaRights);
        f.corpseHasItem = false;
        Tick(r, f);
        CHECK(r.state == State::Paused && r.lootFfa == LootFfa::Idle);
    }
    // 'Already carries' and partyHasItem come first: no order at all.
    {
        Run r = Start(&plan);
        Facts f = FfaParty();
        OrderRefused(r, Tick(r, f), kFfaRights);
        f.partyHasItem = true;
        f.itemHolder = "Healy";
        CHECK(Tick(r, f).empty());
        CHECK(r.step == 1 && r.lootFfa == LootFfa::Idle && !r.lootFfaLive);
    }
    {
        Run r = Start(&plan);
        Facts f = FfaParty();
        f.carrierHasItem = true;
        CHECK(Tick(r, f).empty() && r.step == 1 && !r.lootFfaLive);
    }
    // A refusal that is not about the loot rules pauses at once, no ffa.
    {
        Run r = Start(&plan);
        Facts f = FfaParty();
        OrderRefused(r, Tick(r, f), "not-found: No corpse of that entry is near the point");
        CHECK(r.state == State::Paused && !r.lootFfaLive);
    }
}

// The key off: the facts' defaults (what the glue leaves when the key is 0) give byte-identical orders to the old path.
static void TestLootFfaKeyOffIsTheOldPath()
{
    DungeonPlan plan = FfaPlan();
    Run r = Start(&plan);
    Facts f = Party(0);
    f.members = {Mem("Tanky", 0), Mem("Healy", 5), Mem("Stabby", 6)};
    f.corpseChecked = true;
    f.corpseHasItem = true;
    CHECK(!f.lootFfaOn && f.lootMethod == -1 && !f.leadLeadsGroup);
    std::string order = Tick(r, f);
    CHECK(order == "loot entry:6910@1.0,2.0,3.0 Tanky 7741");
    OrderRefused(r, order, kFfaRights);
    f.lootTurn = "Stabby";
    CHECK(Tick(r, f) == "loot entry:6910@1.0,2.0,3.0 Stabby 7741");
    CHECK(!r.lootFfaUsed && !r.lootFfaLive);
}

// open / use_item go to the bot that carries every input of the step: its item
// (or the item to use) and `param`. Loot another bot keeps for other reasons
// does not count.
static void TestItemCarrierIsPickedFromTheStepsOwnInputs()
{
    std::vector<BagFact> bags(3);
    bags[0].name = "Tanky";
    bags[1].name = "Healy";
    bags[2].name = "Stabby";
    std::string split;
    // The Medallion use: 7740, and the Shaft 7741 in `param`.
    std::vector<std::uint32_t> inputs = {7740, 7741};
    bags[0].held = {7740};
    bags[1].held = {7740, 7741};
    CHECK(PickItemCarrier(bags, inputs, "Tanky", &split) == "Healy");
    // The lead bot carries all: it keeps the step.
    bags[0].held = {7740, 7741};
    CHECK(PickItemCarrier(bags, inputs, "Tanky", &split) == "Tanky");
    // The Keystone: only its key matters; two bots carry unrelated loot (the
    // glue lists only the step's own inputs, so those do not show up at all).
    std::vector<std::uint32_t> key = {7733};
    bags[0].held.clear();
    bags[1].held.clear();
    bags[2].held = {7733};
    CHECK(PickItemCarrier(bags, key, "Tanky", &split) == "Stabby");
    CHECK(split.empty());
    // Split: the lead bot stays the carrier and the note says who holds what.
    bags[0].held = {7740};
    bags[1].held = {7741};
    bags[2].held.clear();
    CHECK(PickItemCarrier(bags, inputs, "Tanky", &split) == "Tanky");
    CHECK(split == "Tanky holds 7740; Healy holds 7741");
    // The only carrier is dead or off the map: never chosen; the note says where the item is.
    bags[0].held = {7740};
    bags[1].held = {7740, 7741};
    bags[1].away = "dead";
    split.clear();
    CHECK(PickItemCarrier(bags, inputs, "Tanky", &split) == "Tanky");
    CHECK(split == "Tanky holds 7740; item 7740, 7741 is with Healy, who is dead");
    bags[1].away = "not in the instance";
    split.clear();
    CHECK(PickItemCarrier(bags, inputs, "Tanky", &split) == "Tanky");
    CHECK(split.find("who is not in the instance") != std::string::npos);
    // ... and another available bot that carries everything is preferred over the unavailable one.
    bags[2].held = {7740, 7741};
    CHECK(PickItemCarrier(bags, inputs, "Tanky", &split) == "Stabby");
    bags[1].away.clear();
    bags[1].held = {7741};
    bags[2].held.clear();
    // Nobody has the first input: no split note (the missing-item rules apply).
    split.clear();
    bags[0].held.clear();
    CHECK(PickItemCarrier(bags, inputs, "Tanky", &split) == "Tanky" && split.empty());
}

// While an open / use_item order is out, its bot stays the carrier. Uldaman:
// Healy (not the lead bot) used the Medallion 7740 with the Shaft 7741; both
// are consumed and the Staff 7733 is in Healy's bags, so no bot carries the
// inputs and PickItemCarrier alone would fall back to the lead bot.
static void TestOutstandingCarrierKeepsTheOrdersBot()
{
    std::vector<BagFact> bags(3);
    bags[0].name = "Tanky";
    bags[0].guid = 11;
    bags[1].name = "Healy";
    bags[1].guid = 12;
    bags[2].name = "Stabby";
    bags[2].guid = 13;
    std::vector<std::uint32_t> inputs = {7740, 7741};
    CHECK(PickItemCarrier(bags, inputs, "Tanky") == "Tanky"); // what the bags alone say
    // Still present: pinned, the order's bot keeps it.
    std::string name;
    CHECK(OutstandingCarrierVerdict(true, 12, bags, &name) == CarrierVerdict::Pinned && name == "Healy");
    // No order out yet (or a retried step: ResetStep cleared orderSent), or a new order (carrierRaw 0).
    CHECK(OutstandingCarrierVerdict(false, 12, bags) == CarrierVerdict::NoOrder);
    CHECK(OutstandingCarrierVerdict(true, 0, bags) == CarrierVerdict::NoOrder);
    // That bot left the party, died or went off the map: the order is retired, never re-picked.
    CHECK(OutstandingCarrierVerdict(true, 14, bags) == CarrierVerdict::Retire);
    bags[1].away = "dead";
    CHECK(OutstandingCarrierVerdict(true, 12, bags) == CarrierVerdict::Retire);
    bags[1].away = "not in the instance";
    CHECK(OutstandingCarrierVerdict(true, 12, bags) == CarrierVerdict::Retire);
}

// A retired order (its bot gone) goes out again to the fresh pick; the old
// task is not read as finished from the replacement's bags.
static void TestRetiredUseItemOrderGoesOutAgain()
{
    DungeonPlan plan;
    PlanStep use = Step(PlanStepKind::UseItem);
    use.entry = 7740;
    use.item = 7733;
    use.param = 7741;
    plan.steps = {use, Step(PlanStepKind::Move, 9)};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.carrier = "Healy";
    f.carrierHasEntryItem = true;
    CHECK(Tick(r, f) == "use 7740 Healy");
    CHECK(r.orderSent && r.phase == Phase::AwaitTask);
    // Healy died with the order out; Tanky now carries both inputs.
    RetireOrder(r);
    CHECK(!r.orderSent && r.phase == Phase::Begin && r.state == State::Running);
    Facts g = Party(0);
    g.carrierHasEntryItem = true;
    CHECK(Tick(r, g) == "use 7740 Tanky");
    CHECK(r.state == State::Running && r.step == 0);
    // Without the retire the replacement's bags would end the task: the pause the fix prevents.
    Run q = Start(&plan);
    CHECK(Tick(q, f) == "use 7740 Healy");
    Facts h = Party(0);
    CHECK(Tick(q, h).empty());
    CHECK(q.state == State::Paused && q.note.find("is not in Tanky's bags") != std::string::npos);
}

// The use_item pauses, naming who holds what, when the inputs are split.
static void TestUseItemPausesOnSplitInputs()
{
    DungeonPlan plan;
    PlanStep use = Step(PlanStepKind::UseItem);
    use.entry = 7740;
    use.item = 7733;
    use.param = 7741;
    plan.steps = {use};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.carrierHasEntryItem = true;
    f.itemsSplit = "Tanky holds 7740; Healy holds 7741";
    Tick(r, f);
    CHECK(r.state == State::Paused);
    CHECK(r.note.find("no bot carries every item it needs: Tanky holds 7740; Healy holds 7741") != std::string::npos);
    // Healy carries both: the glue names her the carrier and the use goes to her.
    Run q = Start(&plan);
    Facts g = Party(0);
    g.carrier = "Healy";
    g.carrierHasEntryItem = true;
    CHECK(Tick(q, g) == "use 7740 Healy");
}

// Uldaman run 4, archaedas wait_object 135/135: Archaedas was dead and the
// vault door 124369 active, yet "a fight lasted more than 180 s" paused the run.
// The hold's long fight had left fightMs over the plain 180 s ceiling, and the
// combat flag stood a few seconds after the last kill.
static void TestWaitObjectDoneWinsOverAStaleFightTimer()
{
    DungeonPlan plan;
    PlanStep wait = Step(PlanStepKind::WaitObject, 1, 2, 3);
    wait.entry = 124369;
    wait.param = 1;
    plan.steps = {wait, Step(PlanStepKind::Move, 9)};
    // The door is in the wanted state and only the combat flag stands: done.
    Run r = Start(&plan);
    r.fighting = true;
    r.fightMs = kFightCapMs + 5000;
    Facts f = Party(0);
    f.anyInCombat = true;
    f.staleCombat = true;
    f.objectState = 1;
    // One stale snapshot proves nothing: the wait step is finished once the
    // flag has stood unbroken for kStaleConfirmMs.
    Tick(r, f);
    CHECK(r.step == 0 && r.state == State::Running);
    for (std::uint32_t i = 0; i < kStaleConfirmMs / kTickMs && r.step == 0; ++i)
        Tick(r, f);
    CHECK(r.state == State::Running && r.step == 1);
    // A break (one engaged tick) starts the confirmation over.
    Run b = Start(&plan);
    b.fighting = true;
    Facts e = f;
    Tick(b, e);
    Tick(b, e);
    e.staleCombat = false;
    Tick(b, e);
    e.staleCombat = true;
    Tick(b, e);
    CHECK(b.step == 0 && b.staleRunMs == kTickMs);
    // A real fight (a hostile engaged) with the door open keeps its handling
    // and its ceiling: the step does not advance.
    Run q = Start(&plan);
    q.fighting = true;
    q.fightMs = kFightCapMs + 5000;
    Facts g = Party(0);
    g.anyInCombat = true;
    g.objectState = 1;
    Tick(q, g);
    CHECK(q.state == State::Paused && q.step == 0 &&
        q.note.find("a fight lasted more than 180 s") != std::string::npos);
    // A stale flag with the door shut is waited out for the grace period (the
    // timer does not run), then the ceiling runs again: it ends in a pause.
    Run p = Start(&plan);
    p.fighting = true;
    p.fightMs = kFightCapMs + 5000;
    Facts h = g;
    h.objectState = 0;
    h.staleCombat = true;
    Tick(p, h);
    CHECK(p.state == State::Running && p.fightMs == kFightCapMs + 5000);
    for (std::uint32_t i = 0; i < kStaleCombatGraceMs / kTickMs + 2 && p.state == State::Running; ++i)
        Tick(p, h);
    CHECK(p.state == State::Paused && p.note.find("a fight lasted more than 180 s") != std::string::npos);
    // The grace is spent once per fight: alternating 19 stale ticks with one
    // engaged tick does not renew it, the ceiling still fires.
    Run a = Start(&plan);
    a.fighting = true;
    a.fightMs = kFightCapMs + 5000;
    Facts k = h;
    for (int i = 0; i < 200 && a.state == State::Running; ++i)
    {
        k.staleCombat = (i % 20) != 19;
        Tick(a, k);
    }
    CHECK(a.state == State::Paused && a.note.find("a fight lasted more than 180 s") != std::string::npos);
    // Two satisfied wait_object steps over one uninterrupted stale flag: the
    // second does not get a fresh grace.
    DungeonPlan two;
    two.steps = {plan.steps[0], plan.steps[0]};
    Run w = Start(&two);
    w.fighting = true;
    Facts z = f;
    for (int i = 0; i < 10 && w.step == 0; ++i)
        Tick(w, z);
    CHECK(w.step == 1 && w.staleMs >= kStaleConfirmMs);
    // The flag has stood this long already; the next step's confirmation is
    // unbroken too, so it finishes at once, the grace is not renewed.
    std::uint32_t const spent = w.staleMs;
    Tick(w, z);
    CHECK(w.staleMs > spent);
    // Stale combat, then a wipe regroup by itself when it clears: the next
    // combat gets a fresh grace and a full confirmation.
    Run g2 = Start(&plan);
    g2.fighting = true;
    g2.staleMs = kStaleCombatGraceMs;
    g2.staleRunMs = kStaleConfirmMs;
    Facts wipe = Party(0);
    wipe.dead = kWipeDead;
    wipe.alive = 2;
    wipe.anyInCombat = false;
    Tick(g2, wipe);
    CHECK(g2.regroup && g2.staleMs == 0 && g2.staleRunMs == 0);
    // The flag clearing resets the grace.
    Run c = Start(&plan);
    c.fighting = true;
    Tick(c, h);
    CHECK(c.staleMs > 0);
    h.anyInCombat = false;
    Tick(c, h);
    CHECK(c.staleMs == 0);
    // A fight that goes on into the next step starts that step's ceiling over.
    Run n = Start(&plan);
    n.fighting = true;
    n.fightMs = kFightCapMs + 5000;
    NextStep(n, "hold done");
    CHECK(n.fightMs == 0 && n.fighting);
}

static void TestPullThatStartsNoFightIsPassed()
{
    DungeonPlan plan;
    plan.steps = {PullStep(10, {4851})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.seen = true;
    f.units = {Unit(7, 4851, 10)};
    int pulls = 0;
    for (int i = 0; i < 120 && r.state == State::Running && r.step == 0; ++i)
    {
        std::string order = Tick(r, f);
        if (order == "pullback 7")
            ++pulls;
    }
    CHECK(pulls == static_cast<int>(kPullTriesPerUnit));
    // The unit is passed, so the step ends (the unit is alive: it is not "cleared", it is passed).
    CHECK(r.state == State::Done);
}

static void TestAmbushWalksThrough()
{
    DungeonPlan plan;
    plan.steps = {PullStep(20, {4850}, ai::kPlanFlagAmbush)};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.seen = true;
    CHECK(Tick(r, f) == "move 20.0 0.0 0.0");
    f.leadX = 19;
    Tick(r, f);
    CHECK(r.state == State::Done);
}

static void TestBossKillOrderAndTeamGate()
{
    DungeonPlan plan;
    PlanStep boss = Step(PlanStepKind::Boss, 10);
    boss.entry = 6906;
    boss.entries = {6907, 6908};
    ai::NormalizePlanStep(boss);
    plan.steps = {boss, Step(PlanStepKind::Rest)};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.seen = true;
    f.units = {Unit(1, 6906, 5), Unit(2, 6908, 9), Unit(3, 6907, 12)};
    // First listed add (6907) before the nearer boss.
    CHECK(Tick(r, f) == "pullback 3");
    // Not hostile (an Alliance party and the Lost Dwarves): left alone, step done.
    Run r2 = Start(&plan);
    for (UnitFact& u : f.units)
        u.hostile = false;
    Tick(r2, f);
    CHECK(r2.step == 1 && r2.note.find("friendly") != std::string::npos);
}

static void TestHopPullsNearUnitsFirst()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 40)};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.hopUnits = {Unit(9, 4852, 35)};
    CHECK(Tick(r, f) == "pullback 9");
    f.hopUnits.clear();
    f.anyInCombat = true;
    Tick(r, f);
    f.anyInCombat = false;
    CHECK(Tick(r, f) == "move 40.0 0.0 0.0");
}

// Uldaman rerun pull 58 (build 19z11): the fight ended with the priest and the
// mage dead (fewer than kWipeDead) and the next pullback went out at once. Now
// the dead are revived and the party gathers before the pull.
static void TestFewDeadReviveBeforePull()
{
    DungeonPlan plan;
    plan.steps = {PullStep(40, {7011})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.units = {Unit(21, 7011, 30)};
    f.dead = 2;
    f.alive = 3;
    // No pull while two are dead: the regroup's revive goes out instead.
    CHECK(Tick(r, f).empty());
    CHECK(r.regroup && r.note.find("waiting for 2 dead: reviving and gathering") == 0);
    CHECK(Tick(r, f) == "revive party");
    CHECK(Tick(r, f).empty());
    // Up again and gathered: the pull follows.
    f.dead = 0;
    f.alive = 5;
    std::string order;
    for (int i = 0; i < 5 && order.empty(); ++i)
        order = Tick(r, f);
    CHECK(order == "pullback 21");
    // In a fight with one dead nothing changes: the fight is the bots' own.
    Run r2 = Start(&plan);
    f.dead = 1;
    f.alive = 4;
    f.anyInCombat = true;
    CHECK(Tick(r2, f).empty());
    CHECK(!r2.regroup && r2.note.find("fighting") == 0);
}

// Review of #1055: the 1-2 dead rule stays out of scripted events (a quiet
// gap between waves is not the end of the fight).
static void TestFewDeadRuleSkipsEvents()
{
    DungeonPlan plan;
    plan.steps = {PullStep(40, {7011})};
    Run r = Start(&plan);
    r.eventOn = true;
    Facts f = Party(0);
    f.units = {Unit(21, 7011, 30)};
    f.dead = 1;
    f.alive = 4;
    Tick(r, f);
    CHECK(!r.regroup);
    // A move leading into a hold counts as the event too.
    DungeonPlan holdPlan;
    holdPlan.steps = {Step(PlanStepKind::Move, 40), Step(PlanStepKind::Hold, 40)};
    Run h = Start(&holdPlan);
    Tick(h, f);
    CHECK(!h.regroup);
}

// Review of #1055: a fight during a regroup (a pack met on the corpse run)
// gets the combat block: fight note, fight ceiling, and a third death reaches
// the regroup by itself.
static void TestRegroupFightsFirst()
{
    DungeonPlan plan;
    plan.steps = {PullStep(40, {7011})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.dead = 1;
    f.alive = 4;
    BeginRegroup(r);
    f.anyInCombat = true;
    CHECK(Tick(r, f).empty());
    CHECK(r.note == "fighting" && r.fighting);
    // The fight ceiling still holds inside the regroup.
    for (std::uint32_t ms = 0; ms <= kFightCapMs && r.state == State::Running; ms += kTickMs)
        Tick(r, f);
    CHECK(r.state == State::Paused && r.note.find("a fight lasted") == 0);
    // Three dead in the regroup's fight: the regroup by itself follows the fight.
    Run w = Start(&plan);
    BeginRegroup(w);
    f.dead = 1;
    f.alive = 4;
    CHECK(Tick(w, f).empty());
    f.dead = 3;
    f.alive = 2;
    CHECK(Tick(w, f).empty());
    CHECK(w.note.find("3 dead: fighting on") == 0);
    f.anyInCombat = false;
    Tick(w, f);
    CHECK(w.regroup && w.autoRegroups == 1);
}

// Review of #1055: a living rezzer gets kPartyRezWaitMs to raise the dead
// before `revive party` releases them; without one the revive goes out at once.
static void TestRegroupWaitsForPartyRez()
{
    DungeonPlan plan;
    plan.steps = {PullStep(40, {7011})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.units = {Unit(21, 7011, 30)};
    f.dead = 1;
    f.alive = 4;
    f.rezzer = true;
    CHECK(Tick(r, f).empty()); // the regroup starts
    std::uint32_t waited = 0;
    std::string order;
    while (order.empty() && waited < 60000)
    {
        order = Tick(r, f);
        waited += kTickMs;
        if (order.empty())
            CHECK(r.note.find("a resurrection in the party first") != std::string::npos);
    }
    CHECK(order == "revive party" && waited >= kPartyRezWaitMs && waited <= kPartyRezWaitMs + 2 * kTickMs);
    // Raised in time: no revive at all, the pull follows.
    Run q = Start(&plan);
    Tick(q, f);
    Tick(q, f);
    f.dead = 0;
    f.alive = 5;
    order.clear();
    for (int i = 0; i < 5 && order.empty(); ++i)
        order = Tick(q, f);
    CHECK(order == "pullback 21");
    // No rezzer: the revive goes out on the regroup's first tick.
    Run n = Start(&plan);
    f.dead = 1;
    f.alive = 4;
    f.rezzer = false;
    Tick(n, f);
    CHECK(Tick(n, f) == "revive party");
}

// The rez hold (AiPlayerbot.DungeonRunRezHold, RezHoldPolicy.h). The roster: a tank, a priest
// healer and three more; the mage (index 2) is dead when `dead` is set.
static std::vector<TortoiseBots::RezHold::Member> RezRoster(bool mageDead, std::uint32_t priestMana, float corpseYd = -1.0f)
{
    using namespace TortoiseBots::RezHold;
    std::vector<Member> m(5);
    m[0].tank = true;
    m[1].rez = RezClass::Priest;
    m[1].healer = true;
    m[1].mana = priestMana;
    m[1].rezCost = 160;
    if (mageDead)
    {
        m[2].dead = true;
        m[2].corpseYd = corpseYd;
    }
    return m;
}

// Ticks a regroup with one dead until a revive goes out; the seconds it took (0: none in 200 s).
static std::uint32_t SecondsToRevive(const Facts& f, std::string* note = nullptr)
{
    DungeonPlan plan;
    plan.steps = {PullStep(40, {7011})};
    Run r = Start(&plan);
    BeginRegroup(r);
    for (std::uint32_t ms = kTickMs; ms <= 200000; ms += kTickMs)
    {
        std::string const order = Tick(r, f);
        if (note)
            *note = r.note;
        if (order == "revive party")
            return ms / 1000;
    }
    return 0;
}

// Key off (the default): the rez hold's facts are never looked at. A run fed a roster that would
// change the verdict (rezHold false) ticks exactly as one fed none, order and note, tick by tick,
// and the old fixed wait is still the one that decides.
static void TestRezHoldOffIsUnchanged()
{
    DungeonPlan plan;
    plan.steps = {PullStep(40, {7011})};
    Facts plain = Party(0);
    plain.units = {Unit(21, 7011, 30)};
    plain.dead = 1;
    plain.alive = 4;
    plain.rezzer = true;
    Facts withRoster = plain;
    withRoster.rezHold = false;
    withRoster.rezMembers = RezRoster(true, 0, 500.0f); // a priest with no mana and a far body: would send at once
    Run a = Start(&plan);
    Run b = Start(&plan);
    std::uint32_t firstOrder = 0;
    for (std::uint32_t ms = kTickMs; ms <= 300000; ms += kTickMs)
    {
        std::string const oa = Tick(a, plain);
        std::string const ob = Tick(b, withRoster);
        CHECK(oa == ob);
        CHECK(a.note == b.note);
        CHECK(a.regroupMs == b.regroupMs && a.reviveSent == b.reviveSent && a.state == b.state);
        if (!firstOrder && oa == "revive party")
            firstOrder = ms;
    }
    CHECK(firstOrder >= kPartyRezWaitMs && firstOrder <= kPartyRezWaitMs + 3 * kTickMs);
    // No rezzer in the plain facts: the old rule revives at once, with or without the roster.
    plain.rezzer = false;
    withRoster.rezzer = false;
    CHECK(SecondsToRevive(plain) == SecondsToRevive(withRoster));
    CHECK(SecondsToRevive(plain) <= 3);
}

// Key on: the hold waits for the priest well past the old 15 s, waits for its mana, and sends the
// revive when a rezzer is missing, the body is far or the budget is spent.
static void TestRezHoldOn()
{
    using namespace TortoiseBots::RezHold;
    Facts f = Party(0);
    f.dead = 1;
    f.alive = 4;
    f.rezHold = true;
    f.rezzer = true; // the old flag is not what decides any more
    std::string note;
    // A priest with mana: held for kHoldMs (not 15 s), then the revive.
    f.rezMembers = RezRoster(true, 500);
    std::uint32_t s = SecondsToRevive(f, &note);
    CHECK(s >= kHoldMs / 1000 && s <= kHoldMs / 1000 + 3);
    f.rezMembers = RezRoster(true, 500);
    {
        DungeonPlan plan;
        plan.steps = {PullStep(40, {7011})};
        Run r = Start(&plan);
        BeginRegroup(r);
        for (int i = 0; i < 30; ++i)
            CHECK(Tick(r, f).empty());
        CHECK(r.note.find("a resurrection in the party first") != std::string::npos);
        CHECK(r.note.find("waiting for a resurrection") != std::string::npos);
    }
    // The priest is short of mana: waited for up to kManaWaitMs.
    f.rezMembers = RezRoster(true, 20);
    s = SecondsToRevive(f, &note);
    CHECK(s >= kManaWaitMs / 1000 && s <= kManaWaitMs / 1000 + 3);
    {
        DungeonPlan plan;
        plan.steps = {PullStep(40, {7011})};
        Run r = Start(&plan);
        BeginRegroup(r);
        for (int i = 0; i < 20; ++i)
            CHECK(Tick(r, f).empty());
        CHECK(r.note.find("the resurrector drinks first") != std::string::npos);
    }
    // No resurrector at all: the revive goes out on the first ticks.
    f.rezMembers = RezRoster(true, 500);
    f.rezMembers[1].rez = RezClass::None;
    CHECK(SecondsToRevive(f) <= 3);
    // The body lies too far: no wait either.
    f.rezMembers = RezRoster(true, 500, kCorpseMaxYd + 10.0f);
    CHECK(SecondsToRevive(f) <= 3);
    // Raised in time: no revive, the pull follows.
    {
        DungeonPlan plan;
        plan.steps = {PullStep(40, {7011})};
        Run q = Start(&plan);
        f.units = {Unit(21, 7011, 30)};
        f.rezMembers = RezRoster(true, 500);
        BeginRegroup(q);
        for (int i = 0; i < 20; ++i)
            CHECK(Tick(q, f).empty());
        f.dead = 0;
        f.alive = 5;
        f.rezMembers = RezRoster(false, 500);
        std::string order;
        for (int i = 0; i < 6 && order.empty(); ++i)
            order = Tick(q, f);
        CHECK(order == "pullback 21");
    }
    // A revive that went out is not taken back by a resurrector turning up after it (the priest
    // revived at the spirit healer is one): the regroup goes on reviving, it does not hold again.
    {
        DungeonPlan plan;
        plan.steps = {PullStep(40, {7011})};
        Run r = Start(&plan);
        BeginRegroup(r);
        f.units.clear();
        f.dead = 2;
        f.alive = 3;
        f.rezMembers = RezRoster(true, 500);
        f.rezMembers[1].rez = RezClass::None;
        f.rezMembers[3].dead = true;
        CHECK(Tick(r, f) == "revive party");
        f.rezMembers[1].rez = RezClass::Priest;
        Tick(r, f);
        CHECK(r.reviveSent && r.note.find("reviving") != std::string::npos);
        CHECK(r.note.find("a resurrection in the party first") == std::string::npos);
        f.dead = 1;
        f.alive = 4;
    }
    // Once the revive went out it is not held back again: the old resend every 60 s still runs.
    {
        DungeonPlan plan;
        plan.steps = {PullStep(40, {7011})};
        Run r = Start(&plan);
        BeginRegroup(r);
        f.units.clear();
        f.dead = 1;
        f.alive = 4;
        f.rezMembers = RezRoster(true, 500);
        int revives = 0;
        for (std::uint32_t ms = 0; ms < 200000; ms += kTickMs)
            if (Tick(r, f) == "revive party")
                ++revives;
        CHECK(revives == 3); // at ~60 s, then every 60 s: ~120 s, ~180 s
    }
}

// The rez hold's nudge: a hold that went kRezNudgeMs with a rezzer able and no cast under way sends the lead
// bot onto the body, once; the note gives the rezzer's distance to the body and its sight of it.
// The roster: the priest (1) stands `rezzerYd` from the body of the dead mage (2), which lies `bodyYd` from
// the lead bot (at x 0) on the x axis.
static std::vector<TortoiseBots::RezHold::Member> NudgeRoster(float bodyYd, float rezzerYd, bool sees)
{
    std::vector<TortoiseBots::RezHold::Member> m = RezRoster(true, 500);
    m[1].placed = true;
    m[1].x = bodyYd + rezzerYd;
    m[1].seesBody = sees;
    m[2].placed = true;
    m[2].bodyX = bodyYd;
    m[2].bodyY = 0.0f;
    m[2].bodyZ = 0.0f;
    return m;
}

// Ticks a regroup for `ms`; every non-empty order, in order.
static std::vector<std::string> NudgeOrders(Run& r, const Facts& f, std::uint32_t ms)
{
    std::vector<std::string> orders;
    for (std::uint32_t t = kTickMs; t <= ms; t += kTickMs)
    {
        std::string const o = Tick(r, f);
        if (!o.empty())
            orders.push_back(o);
    }
    return orders;
}

static void TestRezHoldNudge()
{
    using namespace TortoiseBots::RezHold;
    DungeonPlan plan;
    plan.steps = {PullStep(40, {7011})};
    Facts f = Party(0);
    f.dead = 1;
    f.alive = 4;
    f.rezHold = true;
    f.leadAlive = true;
    std::string const move = MoveOrder(20.0f, 0.0f, 0.0f, false);
    // A body 20 yd away: nothing before kRezNudgeMs, then one move onto it, and no second one.
    {
        Run r = Start(&plan);
        BeginRegroup(r);
        f.rezMembers = NudgeRoster(20.0f, 18.0f, false);
        CHECK(NudgeOrders(r, f, kRezNudgeMs - 2 * kTickMs).empty());
        CHECK(!r.rezNudged);
        std::vector<std::string> const orders = NudgeOrders(r, f, 3 * kTickMs);
        CHECK(orders.size() == 1 && orders[0] == move);
        CHECK(r.rezNudged);
        CHECK(r.note.find("waiting for a resurrection") != std::string::npos);
        CHECK(NudgeOrders(r, f, 30000).empty()); // none a second time
        // The next regroup may nudge again.
        BeginRegroup(r);
        CHECK(!r.rezNudged && r.rezHoldMs == 0);
    }
    // The note names the rezzer's distance to the body and its sight of it.
    {
        Run r = Start(&plan);
        BeginRegroup(r);
        f.rezMembers = NudgeRoster(20.0f, 18.0f, false);
        f.rezMembers[1].name = "Lyrieluna";
        f.rezMembers[2].name = "Beardkinz";
        Tick(r, f);
        CHECK(r.note.find("Lyrieluna 18.0 yd from the body, sight no") != std::string::npos);
        f.rezMembers[1].seesBody = true;
        Tick(r, f);
        CHECK(r.note.find("Lyrieluna 18.0 yd from the body, sight yes") != std::string::npos);
    }
    // A body 5 yd from the lead bot: no move at all.
    {
        Run r = Start(&plan);
        BeginRegroup(r);
        f.rezMembers = NudgeRoster(5.0f, 3.0f, true);
        CHECK(NudgeOrders(r, f, 40000).empty());
        CHECK(!r.rezNudged);
    }
    // A resurrection under way (a cast, or a request not yet accepted): no move.
    {
        Run r = Start(&plan);
        BeginRegroup(r);
        f.rezMembers = NudgeRoster(20.0f, 18.0f, false);
        f.rezInProgress = true;
        CHECK(NudgeOrders(r, f, 40000).empty());
        CHECK(!r.rezNudged);
        // The cast ends without a rez: the 12 s run again from there.
        f.rezInProgress = false;
        CHECK(NudgeOrders(r, f, kRezNudgeMs - 2 * kTickMs).empty());
        CHECK(NudgeOrders(r, f, 3 * kTickMs).size() == 1);
    }
    // The resurrector drinks (short of mana): no move, and the clock starts over when it is back.
    {
        Run r = Start(&plan);
        BeginRegroup(r);
        f.rezMembers = NudgeRoster(20.0f, 18.0f, false);
        f.rezMembers[1].mana = 20;
        CHECK(NudgeOrders(r, f, 30000).empty());
        f.rezMembers[1].mana = 500;
        CHECK(NudgeOrders(r, f, kRezNudgeMs - 2 * kTickMs).empty());
        CHECK(NudgeOrders(r, f, 3 * kTickMs).size() == 1);
    }
    // Able 8 s, drinking, able again: the 12 s start over from the drink (the same restart as the hold budget's).
    {
        Run r = Start(&plan);
        BeginRegroup(r);
        f.rezMembers = NudgeRoster(20.0f, 18.0f, false);
        CHECK(NudgeOrders(r, f, 8000).empty());
        f.rezMembers[1].mana = 20;
        CHECK(NudgeOrders(r, f, 4 * kTickMs).empty());
        f.rezMembers[1].mana = 500;
        CHECK(NudgeOrders(r, f, kRezNudgeMs - 2 * kTickMs).empty());
        CHECK(NudgeOrders(r, f, 3 * kTickMs).size() == 1);
    }
    // A revived straggler whose summon falls due on the very tick the 12 s are reached: the summon goes out
    // (the nudge never replaces an order SummonRevived set), and the nudge follows on a later tick, once.
    {
        Run r = Start(&plan);
        BeginRegroup(r);
        r.waitFor.insert("Quelinator"); // died earlier in the run, revived far away
        f.rezMembers = NudgeRoster(20.0f, 18.0f, false);
        f.members = {Mem("Tanky", 0), Mem("Quelinator", 270)};
        f.members[1].inCombat = true; // a summon is not sent into a fight: not due yet
        CHECK(NudgeOrders(r, f, kRezNudgeMs - kTickMs).empty());
        CHECK(r.rezHoldMs == kRezNudgeMs - kTickMs && !r.rezNudged);
        f.members[1].inCombat = false; // due now: this tick reaches 12 s
        CHECK(Tick(r, f) == "summon Quelinator");
        CHECK(r.rezHoldMs >= kRezNudgeMs && !r.rezNudged);
        CHECK(Tick(r, f) == move); // the next tick: the nudge, once
        CHECK(r.rezNudged);
        CHECK(NudgeOrders(r, f, 5000).empty());
        f.members.clear();
    }
    // A body the glue did not place, or a dead lead bot: no move.
    {
        Run r = Start(&plan);
        BeginRegroup(r);
        f.rezMembers = NudgeRoster(20.0f, 18.0f, false);
        f.rezMembers[2].placed = false;
        CHECK(NudgeOrders(r, f, 40000).empty());
        Run q = Start(&plan);
        BeginRegroup(q);
        f.rezMembers = NudgeRoster(20.0f, 18.0f, false);
        f.leadAlive = false;
        CHECK(NudgeOrders(q, f, 40000).empty());
        f.leadAlive = true;
    }
    // The key off: the same facts send no move, ever, and the old wait is unchanged.
    {
        Run r = Start(&plan);
        BeginRegroup(r);
        f.rezHold = false;
        f.rezzer = true;
        f.rezMembers = NudgeRoster(20.0f, 18.0f, false);
        for (std::string const& o : NudgeOrders(r, f, 60000))
            CHECK(o == "revive party");
        CHECK(!r.rezNudged && r.rezHoldMs == 0);
        f.rezHold = true;
        f.rezzer = false;
    }
    // The revive going out ends the nudge for good.
    {
        Run r = Start(&plan);
        BeginRegroup(r);
        f.rezMembers = NudgeRoster(20.0f, 18.0f, false);
        std::vector<std::string> const orders = NudgeOrders(r, f, 70000);
        CHECK(orders.size() == 2 && orders[0] == move && orders[1] == "revive party");
        CHECK(r.rezHoldMs == 0 && !r.rezNudged);
    }
}

// T4 (F3): the rez nudge's move refused as unreachable (the Uldaman scorpid pit: the ledge the body lies on is
// not on the navmesh) ends the hold at once and goes to the revive; any other refusal keeps the hold.
static void TestRezNudgeUnreachable()
{
    using namespace TortoiseBots::RezHold;
    DungeonPlan plan;
    plan.steps = {PullStep(40, {7011})};
    Facts f = Party(0);
    f.dead = 1;
    f.alive = 4;
    f.rezHold = true;
    f.leadAlive = true;
    f.rezMembers = NudgeRoster(20.0f, 18.0f, false);
    std::string const move = MoveOrder(20.0f, 0.0f, 0.0f, false);
    std::string const noPath = "unreachable: The lead bot has no walkable path to that point.";
    // Reaches the nudge and checks the move order went out.
    auto nudged = [&](Run& r)
    {
        BeginRegroup(r);
        // Ticks up to the nudge itself (the 15 s party-rezzer wait has not run out then).
        std::string o;
        for (std::uint32_t t = 0; t < 2 * kRezNudgeMs && o.empty(); t += kTickMs)
            o = Tick(r, f);
        CHECK(o == move);
        CHECK(r.rezNudged && r.rezNudgeOrder == move && !r.rezBodyUnreachable);
    };
    // No walkable path: not paused, the very next tick revives.
    {
        Run r = Start(&plan);
        nudged(r);
        OrderRefused(r, move, noPath);
        CHECK(r.state != State::Paused);
        CHECK(r.rezBodyUnreachable && r.rezNudgeOrder.empty());
        CHECK(r.note.find("cannot walk to the body") != std::string::npos);
        CHECK(r.note.find("reviving instead") != std::string::npos);
        CHECK(Tick(r, f) == "revive party");
        CHECK(r.state != State::Paused);
        CHECK(r.note.find("reviving") != std::string::npos);
        CHECK(!r.rezBodyUnreachable); // the revive going out resets it
    }
    // Every path refusal code counts, and a living party rezzer does not bring the 15 s wait back.
    for (char const* code : {"off-mesh: x", "no-ground: x", "no-los: x"})
    {
        Run r = Start(&plan);
        nudged(r);
        f.rezzer = true;
        OrderRefused(r, move, code);
        CHECK(r.state != State::Paused && r.rezBodyUnreachable);
        CHECK(Tick(r, f) == "revive party");
        f.rezzer = false;
    }
    // A refusal for the lead bot's state of the moment: not paused, no revive, the hold kept, no second nudge.
    {
        Run r = Start(&plan);
        nudged(r);
        OrderRefused(r, move, "busy: The lead bot is casting.");
        CHECK(r.state != State::Paused);
        CHECK(!r.rezBodyUnreachable && r.rezNudgeOrder.empty());
        CHECK(r.note.find("refused") != std::string::npos);
        for (std::string const& o : NudgeOrders(r, f, 20000))
            CHECK(o != "revive party" && o != move);
        CHECK(r.state != State::Paused);
    }
    // A refusal of some other order is not taken for the nudge's.
    {
        Run r = Start(&plan);
        nudged(r);
        OrderRefused(r, MoveOrder(99.0f, 0.0f, 0.0f, false), noPath);
        CHECK(!r.rezBodyUnreachable);
    }
    // The next regroup starts clean.
    {
        Run r = Start(&plan);
        nudged(r);
        OrderRefused(r, move, noPath);
        BeginRegroup(r);
        CHECK(!r.rezBodyUnreachable && r.rezNudgeOrder.empty() && !r.rezNudged);
    }
}

// The rez hold's clocks (review of b228d8e8). A priest short of mana for 70 s and then back up keeps
// the whole hold budget from there (before, one shared clock released it the same tick); a cast under
// way finishes past the budget; the 240 s corpse-run budget starts when the hold gave up; the status
// note names who is waited for.
static void TestRezHoldClocks()
{
    using namespace TortoiseBots::RezHold;
    DungeonPlan plan;
    plan.steps = {PullStep(40, {7011})};
    Facts f = Party(0);
    f.dead = 1;
    f.alive = 4;
    f.rezHold = true;
    // Mana back at 70 s: held until 70 s + kHoldMs, then the revive.
    {
        Run r = Start(&plan);
        BeginRegroup(r);
        std::uint32_t revivedAt = 0;
        for (std::uint32_t ms = kTickMs; ms <= 300000 && !revivedAt; ms += kTickMs)
        {
            f.rezMembers = RezRoster(true, ms < 70000 ? 20 : 500);
            if (Tick(r, f) == "revive party")
                revivedAt = ms;
        }
        CHECK(revivedAt >= 70000 + kHoldMs && revivedAt <= 70000 + kHoldMs + 3 * kTickMs);
    }
    // A cast under way when the budget is spent: kCastGraceMs more, once.
    {
        Run r = Start(&plan);
        BeginRegroup(r);
        f.rezMembers = RezRoster(true, 500);
        std::uint32_t revivedAt = 0;
        for (std::uint32_t ms = kTickMs; ms <= 200000 && !revivedAt; ms += kTickMs)
        {
            f.rezInProgress = ms >= 50000;
            if (Tick(r, f) == "revive party")
                revivedAt = ms;
        }
        CHECK(revivedAt >= kHoldMs + kCastGraceMs && revivedAt <= kHoldMs + kCastGraceMs + 3 * kTickMs);
        f.rezInProgress = false;
    }
    // The corpse run keeps its 240 s: the pause comes kRegroupReviveMs after the revive, not after the regroup began.
    {
        Run r = Start(&plan);
        BeginRegroup(r);
        f.rezMembers = RezRoster(true, 500);
        std::uint32_t revivedAt = 0, pausedAt = 0;
        for (std::uint32_t ms = kTickMs; ms <= 400000 && !pausedAt; ms += kTickMs)
        {
            if (Tick(r, f) == "revive party" && !revivedAt)
                revivedAt = ms;
            if (r.state == State::Paused)
                pausedAt = ms;
        }
        CHECK(revivedAt >= kHoldMs && pausedAt >= revivedAt + kRegroupReviveMs &&
              pausedAt <= revivedAt + kRegroupReviveMs + 3 * kTickMs);
        // With the key off the pause stays at kRegroupReviveMs from the regroup's start.
        Run o = Start(&plan);
        BeginRegroup(o);
        Facts off = f;
        off.rezHold = false;
        off.rezMembers.clear();
        off.rezzer = true; // the old 15 s wait first: the budget still runs from the regroup's start
        std::uint32_t offPaused = 0;
        for (std::uint32_t ms = kTickMs; ms <= 400000 && !offPaused; ms += kTickMs)
        {
            Tick(o, off);
            if (o.state == State::Paused)
                offPaused = ms;
        }
        CHECK(offPaused >= kRegroupReviveMs && offPaused <= kRegroupReviveMs + 3 * kTickMs);
    }
    // A rezzer that drinks again after it acted gets the budget again (the clock restarts while it drinks).
    {
        Run r = Start(&plan);
        BeginRegroup(r);
        std::uint32_t revivedAt = 0;
        for (std::uint32_t ms = kTickMs; ms <= 300000 && !revivedAt; ms += kTickMs)
        {
            // able 0-40 s, drinking 40-60 s, able again from 60 s
            f.rezMembers = RezRoster(true, ms >= 40000 && ms < 60000 ? 20 : 500);
            if (Tick(r, f) == "revive party")
                revivedAt = ms;
        }
        CHECK(revivedAt >= 60000 + kHoldMs && revivedAt <= 60000 + kHoldMs + 3 * kTickMs);
    }
    // A new regroup starts the budget over.
    {
        Run r = Start(&plan);
        BeginRegroup(r);
        f.rezMembers = RezRoster(true, 500);
        for (int i = 0; i < 40; ++i)
            CHECK(Tick(r, f).empty());
        BeginRegroup(r);
        std::uint32_t revivedAt = 0;
        for (std::uint32_t ms = kTickMs; ms <= 200000 && !revivedAt; ms += kTickMs)
            if (Tick(r, f) == "revive party")
                revivedAt = ms;
        CHECK(revivedAt >= kHoldMs && revivedAt <= kHoldMs + 3 * kTickMs);
    }
    // The gather's restart of the regroup clock takes the hold's share of the revive budget with it: the
    // dead gone and one dead again while the gather waits, the pause is kRegroupReviveMs from the gather.
    {
        Run r = Start(&plan);
        BeginRegroup(r);
        f.rezMembers = RezRoster(true, 500);
        for (int i = 0; i < 70; ++i)
            Tick(r, f); // the hold gives up at ~60 s, the revive is out
        CHECK(r.reviveSent && r.reviveBaseMs > 0);
        Facts g = f;
        g.dead = 0;
        g.alive = 5;
        g.rezMembers = RezRoster(false, 500);
        g.maxMemberYd = 50.0f;
        g.members = {Mem("Far", 50.0f)};
        Tick(r, g); // the gather goes out and restarts the clock
        CHECK(r.regroupGatherSent && r.reviveBaseMs == 0);
        std::uint32_t pausedAt = 0;
        for (std::uint32_t ms = kTickMs; ms <= 400000 && !pausedAt; ms += kTickMs)
        {
            Tick(r, f); // dead again
            if (r.state == State::Paused)
                pausedAt = ms;
        }
        CHECK(pausedAt >= kRegroupReviveMs && pausedAt <= kRegroupReviveMs + 3 * kTickMs);
    }
    // The note names who is waited for and whom.
    {
        Run r = Start(&plan);
        BeginRegroup(r);
        f.rezMembers = RezRoster(true, 500);
        f.rezMembers[1].name = "Holysis";
        f.rezMembers[2].name = "Magnus";
        Tick(r, f);
        Tick(r, f);
        CHECK(r.note.find("Holysis for Magnus") != std::string::npos);
    }
}

// The regroup's revive is bounded: still dead after kRegroupReviveMs pauses.
static void TestRegroupRevivePauses()
{
    DungeonPlan plan;
    plan.steps = {PullStep(40, {7011})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.dead = 2;
    f.alive = 3;
    BeginRegroup(r);
    int revives = 0;
    for (std::uint32_t ms = 0; ms <= kRegroupReviveMs + kTickMs && r.state == State::Running; ms += kTickMs)
        if (Tick(r, f) == "revive party")
            ++revives;
    CHECK(r.state == State::Paused && r.note.find("2 still dead after 240 s") != std::string::npos);
    CHECK(revives >= 4); // the first, then again every 60 s
}

// Review of #1055 (owner decision): a bot eating or drinking after the fight
// is resting. Uldaman pulls 17 and 61 (19z11): the tank sat eating at 82 %
// (above the 80 % start line) and the pullback found it seated.
static void TestMealWaitBeforePull()
{
    DungeonPlan plan;
    plan.steps = {PullStep(40, {4852})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.units = {Unit(31, 4852, 54.7f)};
    f.minHealthPct = 82;
    f.eating = 1;
    CHECK(Tick(r, f).empty());
    CHECK(r.phase == Phase::Rest && r.note.find("1 eating") != std::string::npos);
    for (int i = 0; i < 10; ++i)
        CHECK(Tick(r, f).find("pullback") == std::string::npos);
    // The meal ends (the tank stood up full): the pull goes out.
    f.eating = 0;
    f.minHealthPct = 100;
    std::string order;
    for (int i = 0; i < 5 && order.empty(); ++i)
        order = Tick(r, f);
    CHECK(order == "pullback 31");
    // Eating on and on: kRestCapMs at most, then the pull; once per fight.
    Run c = Start(&plan);
    f.eating = 1;
    f.minHealthPct = 82;
    order.clear();
    std::uint32_t ms = 0;
    for (; ms <= kRestCapMs + 5 * kTickMs && order.find("pullback") == std::string::npos; ms += kTickMs)
        order = Tick(c, f);
    CHECK(order == "pullback 31" && ms >= kRestCapMs);
    // Eating starts a tick after the fight ends (bots sit on their next AI
    // tick), as at pull 17: "pack cleared" on one tick, the next step's pull on
    // the next. The wait still holds that pull.
    DungeonPlan two;
    two.steps = {PullStep(40, {4851}), PullStep(40, {4852})};
    Run late = Start(&two);
    f.eating = 0;
    f.minHealthPct = 100;
    f.units = {Unit(30, 4851, 20)};
    CHECK(Tick(late, f) == "pullback 30");
    f.anyInCombat = true;
    Tick(late, f);
    f.anyInCombat = false;
    f.units.clear();
    f.minHealthPct = 82;
    std::string first;
    for (int i = 0; i < 3 && late.step == 0; ++i)
        first = Tick(late, f);
    CHECK(late.step == 1 && first.find("pullback") == std::string::npos);
    f.units = {Unit(31, 4852, 54.7f)};
    f.eating = 1;
    CHECK(Tick(late, f).empty());
    CHECK(late.phase == Phase::Rest && late.restForEating);
    // The same step: the fight ends with the party scattered (a gather goes
    // out, nobody eats yet), and the tank sits down on the next tick.
    Run same = Start(&plan);
    f.eating = 0;
    f.minHealthPct = 100;
    f.units = {Unit(30, 4852, 20), Unit(31, 4852, 54.7f)};
    CHECK(Tick(same, f) == "pullback 30");
    f.anyInCombat = true;
    Tick(same, f);
    f.anyInCombat = false;
    f.units = {Unit(31, 4852, 54.7f)};
    f.minHealthPct = 82;
    f.maxMemberYd = 35;
    CHECK(Tick(same, f) == "move 0.0 0.0 0.0");
    f.maxMemberYd = 5;
    f.eating = 1;
    CHECK(Tick(same, f).empty());
    CHECK(same.phase == Phase::Rest && same.restForEating);
    // One budget per fight: after kRestCapMs of meal wait the pull goes out
    // even if the bot sits down again.
    order.clear();
    for (std::uint32_t t = 0; t <= kRestCapMs + 5 * kTickMs && order.find("pullback") == std::string::npos; t += kTickMs)
        order = Tick(late, f);
    CHECK(order == "pullback 31" && late.eatWaitMs >= kRestCapMs);
    // At the end lines already: no wait.
    Run e = Start(&plan);
    f.minHealthPct = 95;
    CHECK(Tick(e, f) == "pullback 31");
    // Not inside an event.
    Run v = Start(&plan);
    v.eventOn = true;
    f.minHealthPct = 82;
    Tick(v, f);
    CHECK(v.phase != Phase::Rest);
}

static void TestWipeAndLeadDeathPause()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 40)};
    Run r = Start(&plan);
    r.autoRegroups = kAutoRegroupsPerStep;
    Facts f = Party(0);
    f.dead = 3;
    f.alive = 2;
    CHECK(Tick(r, f).empty());
    CHECK(r.state == State::Paused && r.note.find("3 dead") == 0);
    // Resume after the regroup: the step starts over.
    f.dead = 0;
    f.alive = 5;
    CHECK(Resume(r));
    CHECK(Tick(r, f) == "move 40.0 0.0 0.0");
    // The lead bot dead (the owner's wait rule): the party waits for it, a
    // regroup the run counts; past kAutoRegroupsPerStep it pauses.
    f.leadAlive = false;
    f.dead = 1;
    f.alive = 4;
    Tick(r, f);
    CHECK(r.state == State::Running && r.regroup && r.walkBackDue);
    CHECK(r.note.find("waiting for 1 dead: the lead bot is dead") == 0);
    r.regroup = false; // as if the regroup ended and the lead bot fell again
    r.autoRegroups = kAutoRegroupsPerStep;
    Tick(r, f);
    CHECK(r.state == State::Paused && r.note.find("lead bot is dead") != std::string::npos);
}

static void TestFightCeiling()
{
    DungeonPlan plan;
    plan.steps = {PullStep(10, {1})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.anyInCombat = true;
    for (std::uint32_t i = 0; i <= kFightCapMs / kTickMs + 1 && r.state == State::Running; ++i)
        Tick(r, f);
    CHECK(r.state == State::Paused && r.note.find("fight lasted") != std::string::npos);
}

static void TestTaskSteps()
{
    DungeonPlan plan;
    PlanStep loot = Step(PlanStepKind::Loot, 1, 2, 3);
    loot.entry = 123329;
    loot.item = 7740;
    PlanStep use = Step(PlanStepKind::UseItem);
    use.entry = 7740;
    use.item = 7733;
    PlanStep open = Step(PlanStepKind::Open, 4, 5, 6);
    open.entry = 124371;
    open.item = 7733;
    PlanStep wait = Step(PlanStepKind::WaitObject, 7, 8, 9);
    wait.entry = 124372;
    wait.param = 1;
    wait.timeoutS = 60;
    plan.steps = {loot, use, open, wait};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.minHealthPct = 100;
    CHECK(Tick(r, f) == "loot entry:123329@1.0,2.0,3.0 Tanky 7740");
    f.carrierBusy = true;
    CHECK(Tick(r, f).empty());
    // Task over without the item: pause, naming it.
    f.carrierBusy = false;
    Tick(r, f);
    CHECK(r.state == State::Paused && r.note.find("7740") != std::string::npos);
    // With the item it goes on.
    CHECK(Resume(r));
    f.carrierHasItem = true;
    Tick(r, f);
    CHECK(r.step == 1);
    // use_item: the staff is not there yet; the medallion is.
    f.carrierHasItem = false;
    f.carrierHasEntryItem = true;
    CHECK(Tick(r, f) == "use 7740 Tanky");
    f.carrierHasItem = true;
    Tick(r, f);
    CHECK(r.step == 2);
    CHECK(Tick(r, f) == "open entry:124371@4.0,5.0,6.0 Tanky");
    Tick(r, f);
    CHECK(r.step == 3);
    // The seal opens after a while; then the plan is done.
    f.objectState = 0;
    for (int i = 0; i < 20; ++i)
        Tick(r, f);
    CHECK(r.step == 3 && r.state == State::Running);
    f.objectState = 1;
    Tick(r, f);
    CHECK(r.state == State::Done);
}

static void TestOpenWithoutKeyAndTaskError()
{
    DungeonPlan plan;
    PlanStep open = Step(PlanStepKind::Open, 4, 5, 6);
    open.entry = 124371;
    open.item = 7733;
    plan.steps = {open};
    Run r = Start(&plan);
    Facts f = Party(0);
    Tick(r, f);
    CHECK(r.state == State::Paused && r.note.find("lock needs it") != std::string::npos);
    CHECK(Resume(r));
    f.carrierHasItem = true;
    CHECK(!Tick(r, f).empty());
    f.carrierBusy = true;
    f.taskReply = 2;
    f.taskText = "locked: needs key";
    Tick(r, f);
    CHECK(r.state == State::Paused && r.note.find("locked") != std::string::npos);
}

// ---- task resend (F1): an interrupted cast or an in-combat answer sends the same order again ----

static const char* const kInterruptedText =
    "failed: The opening cast was interrupted after 2.5 s (it did not move and is not in combat).";

// One AwaitTask round: the order is out (carrier busy), then the ERR answer is read.
static std::string AnswerErr(Run& r, Facts& f, const std::string& text)
{
    f.carrierBusy = true;
    f.taskReply = 2;
    f.taskText = text;
    std::string order = Tick(r, f);
    f.carrierBusy = false; // the module's task ended with the answer
    f.taskReply = 0;
    f.taskText.clear();
    return order;
}

static void TestTaskResendAfterInterruptedCast()
{
    for (PlanStepKind kind : {PlanStepKind::Loot, PlanStepKind::Open})
    {
        DungeonPlan plan;
        plan.steps = {GraveStep(kind, 0), Step(PlanStepKind::Move, 9)};
        Run r = Start(&plan);
        Facts f = Party(0);
        std::string const first = Tick(r, f);
        CHECK(first.compare(0, 5, kind == PlanStepKind::Loot ? "loot " : "open ") == 0);
        CHECK(r.orderSent && r.phase == Phase::AwaitTask);

        // 1st failure: running, back at Begin, the note says so, the next tick sends the same order.
        AnswerErr(r, f, kInterruptedText);
        CHECK(r.state == State::Running && r.step == 0 && r.phase == Phase::Begin && !r.orderSent);
        CHECK(r.note.find("sending it again (1 of 2)") != std::string::npos);
        CHECK(r.note.find("The opening cast was interrupted after 2.5 s (it did not move and is not in combat): sending") != std::string::npos);
        CHECK(Tick(r, f) == first);
        CHECK(r.orderSent && r.phase == Phase::AwaitTask);

        // 2nd failure: again.
        AnswerErr(r, f, kInterruptedText);
        CHECK(r.state == State::Running && r.step == 0 && !r.orderSent);
        CHECK(r.note.find("sending it again (2 of 2)") != std::string::npos);
        CHECK(Tick(r, f) == first);

        // 3rd failure: the old pause.
        AnswerErr(r, f, kInterruptedText);
        CHECK(r.state == State::Paused && r.step == 0);
        CHECK(r.note.find("failed: " + std::string(kInterruptedText)) != std::string::npos);
    }
}

static void TestTaskResendInCombatAndBusy()
{
    for (const char* text : {"in-combat: The bot is in combat and stopped.", "busy: The chest is in use."})
    {
        DungeonPlan plan;
        plan.steps = {GraveStep(PlanStepKind::Loot, 0), Step(PlanStepKind::Move, 9)};
        Run r = Start(&plan);
        Facts f = Party(0);
        std::string const first = Tick(r, f);
        AnswerErr(r, f, text);
        CHECK(r.state == State::Running && r.phase == Phase::Begin && !r.orderSent);
        CHECK(r.note.find("sending it again (1 of 2)") != std::string::npos);
        CHECK(Tick(r, f) == first);
    }
}

// The counter is per step: the second step gets its own two re-sends; a resume resets it too.
static void TestTaskResendCounterResetsPerStep()
{
    DungeonPlan plan;
    plan.steps = {GraveStep(PlanStepKind::Open, 0), GraveStep(PlanStepKind::Open, 0)};
    Run r = Start(&plan);
    Facts f = Party(0);
    Tick(r, f);
    AnswerErr(r, f, kInterruptedText);
    Tick(r, f);
    AnswerErr(r, f, kInterruptedText);
    Tick(r, f);
    Tick(r, f); // the 3rd try ends without an error: step 2
    CHECK(r.step == 1);
    CHECK(r.taskResends == 0);
    for (int i = 0; i < 3 && !r.orderSent; ++i)
        Tick(r, f);
    CHECK(r.orderSent);
    AnswerErr(r, f, kInterruptedText);
    CHECK(r.state == State::Running && r.note.find("sending it again (1 of 2)") != std::string::npos);
}

// A loot step sends to the same bot again: taskBot and the tried list are left as they were.
static void TestTaskResendKeepsTheLootBot()
{
    DungeonPlan plan;
    plan.steps = {GraveStep(PlanStepKind::Loot, 0), Step(PlanStepKind::Move, 9)};
    Run r = Start(&plan);
    Facts f = Party(0);
    Tick(r, f);
    std::string const bot = r.taskBot;
    std::size_t const tried = r.lootTried.size();
    AnswerErr(r, f, kInterruptedText);
    CHECK(r.taskBot == bot);
    CHECK(r.lootTried.size() == tried);
    CHECK(r.lootRefusal.empty());
}

// Other failures still pause at once.
static void TestTaskResendNotForOtherFailures()
{
    for (const char* text : {"locked: needs key", "failed: The bot was interrupted by a spell.", "too-far: x"})
    {
        DungeonPlan plan;
        plan.steps = {GraveStep(PlanStepKind::Open, 0), Step(PlanStepKind::Move, 9)};
        Run r = Start(&plan);
        Facts f = Party(0);
        Tick(r, f);
        AnswerErr(r, f, text);
        CHECK(r.state == State::Paused && r.step == 0);
        CHECK(r.note.find("sending it again") == std::string::npos);
    }
}

// ---- gossip step (talk to an NPC: `cmd gossip <entry> <lead bot> <option>`) ----

// Weegli Blastfuse (7607): option 1, the top of his menu, at x.
static PlanStep GossipRow(float x, std::uint32_t option = 1, std::uint32_t entry = 7607)
{
    PlanStep s;
    s.kind = PlanStepKind::Gossip;
    s.entry = entry;
    s.x = x;
    s.param = option;
    CHECK(ai::NormalizePlanStep(s) == ai::PlanStepRefusal::None);
    return s;
}

// The one order goes out once, the busy task is waited for, the ACK ends the step.
static void TestGossipOrderOnceThenAck()
{
    DungeonPlan plan;
    plan.steps = {GossipRow(10), Step(PlanStepKind::Rest)};
    Run r = Start(&plan);
    Facts f = Party(0);
    CHECK(Tick(r, f) == "gossip 7607 Tanky 1");
    CHECK(r.orderSent && r.phase == Phase::AwaitTask);
    // The lead bot's task is under way: no second order, the step stays.
    f.carrierBusy = true;
    for (int i = 0; i < 5; ++i)
        CHECK(Tick(r, f).empty() && r.step == 0 && r.state == State::Running);
    // The ACK.
    f.taskReply = 1;
    f.taskText = "1|Will you blow up that door now?";
    Tick(r, f);
    CHECK(r.step == 1 && r.state == State::Running && r.note == "gossip 7607 option 1 done");
}

// An ERR answer pauses with the module's own words; .bot run resume sends the order again.
static void TestGossipErrPauses()
{
    DungeonPlan plan;
    plan.steps = {GossipRow(10, 3)};
    Run r = Start(&plan);
    Facts f = Party(0);
    CHECK(Tick(r, f) == "gossip 7607 Tanky 3");
    f.taskReply = 2;
    f.taskText = "no-option: No option 3. The menu offers: 1 \"Will you blow up that door now?\".";
    Tick(r, f);
    CHECK(r.state == State::Paused && r.step == 0);
    CHECK(r.note.find("gossip 7607 option 3 failed") != std::string::npos);
    CHECK(r.note.find("no-option: No option 3.") != std::string::npos);
    CHECK(Resume(r));
    f.taskReply = 0;
    f.taskText.clear();
    for (int i = 0; i < 400 && !r.orderSent; ++i) // the resume's regroup first
    {
        std::string order = Tick(r, f);
        if (order.compare(0, 7, "gossip ") == 0)
            break;
    }
    CHECK(r.orderSent);
}

// The order's immediate ERR: an NPC that AiPlayerbot.BotGossipNpcs does not list pauses naming the key.
static void TestGossipNotAllowedNamesTheConfigKey()
{
    DungeonPlan plan;
    plan.steps = {GossipRow(10)};
    Run r = Start(&plan);
    Facts f = Party(0);
    std::string const order = Tick(r, f);
    CHECK(order == "gossip 7607 Tanky 1");
    OrderRefused(r, order, "not-allowed: That NPC is not in AiPlayerbot.BotGossipNpcs, so bots may not talk to it.");
    CHECK(r.state == State::Paused);
    CHECK(r.note.find("cmd gossip 7607 Tanky 1 refused") != std::string::npos);
    CHECK(r.note.find("AiPlayerbot.BotGossipNpcs") != std::string::npos);
}

// Plan ZF-EVENT-HOLD T7: the order's immediate ERR for an NPC that lies dead (r12-19z29 step 220: Weegli
// Blastfuse 7607 dead at the door): the first refusal pauses plainly, says a resume will not help and
// names skip and stop; no retry is asked for. The words say what the module found (none alive within
// 40 yd, a dead one nearby), not that the NPC is dead everywhere.
static void TestGossipDeadNpcPausesPlainly()
{
    DungeonPlan plan;
    plan.steps = {GossipRow(10)};
    Run r = Start(&plan);
    Facts f = Party(0);
    std::string const order = Tick(r, f);
    CHECK(order == "gossip 7607 Tanky 1");
    OrderRefused(r, order,
        "no-npc: No living NPC of that entry is within 40 yd of the bot: a dead one lies nearby: Weegli Blastfuse (7607) "
        "lies dead 3 yd away and respawns in 6 d 23 h (604800 s spawn timer).");
    CHECK(r.state == State::Paused && r.step == 0);
    CHECK(r.note.find("gossip 7607 option 1 cannot be done") != std::string::npos);
    CHECK(r.note.find("none alive within 40 yd, a dead one lies nearby (Weegli Blastfuse (7607), respawns in 6 d 23 h)") !=
        std::string::npos);
    CHECK(r.note.find("resume will not help") != std::string::npos);
    CHECK(r.note.find(".bot run skip") != std::string::npos && r.note.find(".bot run stop") != std::string::npos);
    CHECK(r.note.find("refused") == std::string::npos);
    // A scripted NPC with no respawn timer.
    Run q = Start(&plan);
    Tick(q, f);
    OrderRefused(q, order,
        "no-npc: No living NPC of that entry is within 40 yd of the bot: a dead one lies nearby: Weegli Blastfuse (7607) "
        "lies dead 3 yd away (it has no respawn timer).");
    CHECK(q.state == State::Paused && q.note.find("(Weegli Blastfuse (7607), it never respawns)") != std::string::npos);
    CHECK(q.note.find("respawns in") == std::string::npos);
}

// T8: the plain no-npc text (nothing dead there either) keeps its text.
static void TestGossipNoNpcKeepsItsText()
{
    DungeonPlan plan;
    plan.steps = {GossipRow(10)};
    Run r = Start(&plan);
    Facts f = Party(0);
    std::string const order = Tick(r, f);
    OrderRefused(r, order, "no-npc: No living NPC of that entry is within 40 yd of the bot.");
    CHECK(r.state == State::Paused);
    CHECK(r.note.find("cmd gossip 7607 Tanky 1 refused: no-npc: No living NPC of that entry is within 40 yd of the bot.") !=
        std::string::npos);
    CHECK(r.note.find("cannot be done") == std::string::npos);
    // The mark outside a gossip step's own order is not read either.
    DungeonPlan other;
    other.steps = {Step(PlanStepKind::Rest)};
    Run o = Start(&other);
    OrderRefused(o, "attack 5 Tanky", "invalid-target: it lies dead there");
    CHECK(o.note.find("cannot be done") == std::string::npos);
}

// T9: the later failure (the NPC died before the bot reached it, the module's own words) gives the same plain note.
static void TestGossipDeadNpcTaskReply()
{
    DungeonPlan plan;
    plan.steps = {GossipRow(10)};
    Run r = Start(&plan);
    Facts f = Party(0);
    CHECK(Tick(r, f) == "gossip 7607 Tanky 1");
    f.taskReply = 2;
    f.taskText = "no-npc: The NPC died or despawned before the bot reached it: Weegli Blastfuse (7607) lies dead 3 yd away "
                 "and respawns in 6 d 23 h (604800 s spawn timer).";
    Tick(r, f);
    CHECK(r.state == State::Paused && r.step == 0);
    CHECK(r.note.find("gossip 7607 option 1 cannot be done") != std::string::npos);
    CHECK(r.note.find("(Weegli Blastfuse (7607), respawns in 6 d 23 h)") != std::string::npos);
    CHECK(r.note.find("resume will not help") != std::string::npos);
    // A late failure without the mark keeps today's note.
    Run q = Start(&plan);
    Tick(q, f);
    f.taskText = "no-npc: The NPC died or despawned before the bot reached it.";
    Tick(q, f);
    CHECK(q.note.find("gossip 7607 option 1 failed: no-npc: The NPC died or despawned") != std::string::npos);
}

// use_on: Zul'Farrak's gong 141832, rung with the Mallet of Zul'Farrak 9240.
static PlanStep GongRow()
{
    PlanStep s;
    s.kind = PlanStepKind::UseOn;
    s.entry = 141832;
    s.item = 9240;
    s.x = 1650.9f;
    s.y = 1171.9f;
    s.z = 10.9f;
    CHECK(ai::NormalizePlanStep(s) == ai::PlanStepRefusal::None);
    return s;
}

// One targeted use order by the carrier that holds the item; the busy task is waited for; only the ACK ends the step
// (a free carrier with no answer yet is no answer).
static void TestUseOnOneTargetedOrderThenAck()
{
    CHECK(Supported(PlanStepKind::UseOn));
    DungeonPlan plan;
    plan.steps = {GongRow(), Step(PlanStepKind::Rest)};
    Run r = Start(&plan);
    Facts f = Party(1650.9f);
    f.carrierHasItem = true;
    f.carrierBusy = true; // an earlier order of the carrier: wait, send nothing
    CHECK(Tick(r, f).empty() && !r.orderSent && r.note.find("waiting for Tanky") != std::string::npos);
    f.carrierBusy = false;
    CHECK(Tick(r, f) == "use 9240 Tanky entry:141832@1650.9,1171.9,10.9");
    CHECK(r.orderSent && r.phase == Phase::AwaitTask && r.note == "use_on 141832 with item 9240 by Tanky");
    f.carrierBusy = true;
    for (int i = 0; i < 5; ++i)
        CHECK(Tick(r, f).empty() && r.step == 0);
    f.carrierBusy = false; // free again, but no ACK read yet
    CHECK(Tick(r, f).empty() && r.step == 0 && r.state == State::Running);
    f.taskReply = 1;
    f.taskText = "1|spell 11434";
    f.objectReacted = true; // the gong's state changed under the use (ObjectEffectOf at the ACK)
    Tick(r, f);
    CHECK(r.step == 1 && r.note == "use_on 141832 with item 9240 done: 1|spell 11434");
    CHECK(r.useOnRang.count(0) == 1);
}

// The ACK alone proves nothing: the step ends only once the object reacted or the boss row's boss is in sight
// (alive or dead); else, after kUseOnProofMs, an honest pause that says the ACK came but nothing happened.
static void TestUseOnAckNeedsProof()
{
    PlanStep boss = Step(PlanStepKind::Boss, 1702.6f, 1224.2f, 8.9f);
    boss.entry = 7273;
    DungeonPlan plan;
    plan.steps = {GongRow(), boss};
    Run r = Start(&plan);
    Facts f = Party(1650.9f);
    f.carrierHasItem = true;
    CHECK(!Tick(r, f).empty());
    f.taskReply = 1;
    f.taskText = "1|spell 11434";
    Tick(r, f);
    CHECK(r.step == 0 && r.state == State::Running && r.useOnRang.count(0) == 1);
    CHECK(r.note.find("ACKed") != std::string::npos);
    // The boss appears: done.
    Run b = r;
    Facts g = f;
    g.summonSeen = true;
    Tick(b, g);
    CHECK(b.step == 1 && b.note.find("boss 7273 is here") != std::string::npos);
    // Nothing shows within the proof window: an honest pause, not the boss row's search note.
    for (std::uint32_t ms = 0; ms <= kUseOnProofMs && r.state == State::Running; ms += kTickMs)
        Tick(r, f);
    CHECK(r.state == State::Paused && r.step == 0);
    CHECK(r.note.find("the order was ACKed but object 141832 showed no change and boss 7273 did not show") != std::string::npos);
}

// Never a second use once the act started or the item took: an in-combat answer after the cast began (the walking use
// task fails in a fight after its act started) pauses instead of sending again; a boss already here is no reason to use it.
static void TestUseOnNoResendOnceTheActStarted()
{
    DungeonPlan plan;
    plan.steps = {GongRow()};
    Run r = Start(&plan);
    Facts f = Party(1650.9f);
    f.carrierHasItem = true;
    CHECK(!Tick(r, f).empty());
    f.taskReply = 2;
    f.taskText = "in-combat: The bot is in combat and stopped.";
    f.taskActStarted = true;
    Tick(r, f);
    CHECK(r.state == State::Paused && r.taskResends == 0 && r.note.find("the cast had started") != std::string::npos);
    // In combat before the act, but the boss row's boss is already in sight: no second use either.
    PlanStep boss = Step(PlanStepKind::Boss, 1702.6f, 1224.2f, 8.9f);
    boss.entry = 7273;
    DungeonPlan withBoss;
    withBoss.steps = {GongRow(), boss};
    Run s = Start(&withBoss);
    Facts g = Party(1650.9f);
    g.carrierHasItem = true;
    CHECK(!Tick(s, g).empty());
    g.taskReply = 2;
    g.taskText = "in-combat: The bot is in combat and stopped.";
    g.summonSeen = true;
    Tick(s, g);
    CHECK(s.taskResends == 0 && s.state == State::Running && s.step == 1);
    // A boss already here before any order: the step ends without a use.
    Run t = Start(&withBoss);
    Facts h = Party(1650.9f);
    h.carrierHasItem = true;
    h.summonSeen = true;
    CHECK(Tick(t, h).empty() && t.step == 1);
}

// A wipe after the ACK (ResetStep clears orderSent and the resends) never sends the use again: the per-run latch holds,
// and a run with no boss in sight pauses, naming why.
static void TestUseOnResetAfterAckDoesNotUseAgain()
{
    PlanStep boss = Step(PlanStepKind::Boss, 1702.6f, 1224.2f, 8.9f);
    boss.entry = 7273;
    DungeonPlan plan;
    plan.steps = {GongRow(), boss};
    Run r = Start(&plan);
    Facts f = Party(1650.9f);
    f.carrierHasItem = true;
    CHECK(!Tick(r, f).empty());
    f.taskReply = 1;
    Tick(r, f);
    CHECK(r.useOnRang.count(0) == 1);
    ResetStep(r); // a wipe or the lead bot's death
    f.taskReply = 0;
    f.taskText.clear();
    CHECK(Tick(r, f).empty() && !r.orderSent);
    CHECK(r.state == State::Paused && r.note.find("used once already this run") != std::string::npos);
    CHECK(Resume(r));
    for (int i = 0; i < 400 && r.state == State::Running; ++i)
        CHECK(Tick(r, f).compare(0, 4, "use ") != 0);
}

// The item is in a dead bot's bags: the pause names that bot (revive it), not a second item.
static void TestUseOnNamesADeadHolder()
{
    DungeonPlan plan;
    plan.steps = {GongRow()};
    Run r = Start(&plan);
    Facts f = Party(1650.9f);
    f.carrierHasItem = false;
    f.itemsSplit = "item 9240 is with Tanky, who is dead";
    CHECK(Tick(r, f).empty());
    CHECK(r.state == State::Paused && r.note.find("item 9240 is with Tanky, who is dead") != std::string::npos);
    CHECK(r.note.find(".additem") == std::string::npos);
}

// No bot of the party carries the item: a pause that says so (the item is the operator's to give), no order.
static void TestUseOnWithoutTheItemPauses()
{
    DungeonPlan plan;
    plan.steps = {GongRow()};
    Run r = Start(&plan);
    Facts f = Party(1650.9f);
    f.carrierHasItem = false;
    CHECK(Tick(r, f).empty() && !r.orderSent);
    CHECK(r.state == State::Paused && r.note.find("no bot of the party carries item 9240") != std::string::npos);
    CHECK(r.note.find(".additem 9240") != std::string::npos);
}

// An answer that says the cast did not happen (in combat, busy, an interrupted cast) is sent again ONCE: a second cast
// that took would run the object's event twice (a second Gahz'rilla dies at once and lies invisible at the pit). Any other ERR pauses at once,
// naming the module's text.
static void TestUseOnResendsOnceThenPauses()
{
    DungeonPlan plan;
    plan.steps = {GongRow()};
    Run r = Start(&plan);
    Facts f = Party(1650.9f);
    f.carrierHasItem = true;
    std::string const order = "use 9240 Tanky entry:141832@1650.9,1171.9,10.9";
    CHECK(Tick(r, f) == order);
    f.taskReply = 2;
    f.taskText = "in-combat: Tanky is in combat.";
    Tick(r, f);
    CHECK(r.state == State::Running && !r.orderSent && r.taskResends == 1);
    f.taskReply = 0;
    f.taskText.clear();
    CHECK(Tick(r, f) == order);
    f.taskReply = 2;
    f.taskText = "in-combat: Tanky is in combat.";
    Tick(r, f);
    CHECK(r.state == State::Paused && r.note.find("use_on 141832 with item 9240 failed: in-combat: Tanky is in combat.") !=
        std::string::npos);

    Run h = Start(&plan);
    Facts g = Party(1650.9f);
    g.carrierHasItem = true;
    CHECK(Tick(h, g) == order);
    g.taskReply = 2;
    g.taskText = "invalid-target: spell check 10";
    Tick(h, g);
    CHECK(h.state == State::Paused && h.taskResends == 0 &&
        h.note.find("failed: invalid-target: spell check 10") != std::string::npos);
}

// No answer within the task cap pauses (the resume note warns that a resume rings again).
static void TestUseOnNoAnswerPauses()
{
    DungeonPlan plan;
    plan.steps = {GongRow()};
    Run r = Start(&plan);
    Facts f = Party(1650.9f);
    f.carrierHasItem = true;
    CHECK(!Tick(r, f).empty());
    for (std::uint32_t ms = 0; ms <= kTaskCapMs && r.state == State::Running; ms += kTickMs)
        Tick(r, f);
    CHECK(r.state == State::Paused && r.note.find("no answer") != std::string::npos);
    CHECK(r.useOnRang.count(0) == 1); // it may have taken: never sent again this run
}

// A use_on right before a hold starts that event, as open and use_item do.
static void TestUseOnStartsAnEvent()
{
    DungeonPlan plan;
    PlanStep hold = Step(PlanStepKind::Hold, 1, 2, 3);
    hold.entries = {7273};
    plan.steps = {GongRow(), hold};
    Run r = Start(&plan);
    CHECK(EventHoldAhead(r) != nullptr && EventStartStep(plan, 1) == 0);
    // A failed event goes back to the use_on.
    Run h = Start(&plan, 1);
    EventFailed(h, "the event did not start");
    CHECK(h.state == State::Paused && h.step == 0 && h.note.find("(use_on)") != std::string::npos);
}

// No ACK and no ERR within the step's timeout (default 60 s): the run pauses.
static void TestGossipNoAnswerPauses()
{
    DungeonPlan plan;
    PlanStep row = GossipRow(10);
    CHECK(row.timeoutS == ai::kPlanDefaultTimeoutS);
    plan.steps = {row, Step(PlanStepKind::Rest)};
    Run r = Start(&plan);
    Facts f = Party(0);
    CHECK(Tick(r, f) == "gossip 7607 Tanky 1");
    f.carrierBusy = true;
    std::uint32_t ticks = 0;
    while (r.state == State::Running && ticks < 200)
    {
        CHECK(Tick(r, f).empty());
        ++ticks;
    }
    CHECK(r.state == State::Paused && r.step == 0 && ticks >= 59 && ticks <= 61);
    CHECK(r.note.find("no answer in 60 s") != std::string::npos);
    // The step's own timeout counts: 20 s.
    DungeonPlan quick;
    PlanStep fast = GossipRow(10);
    fast.timeoutS = 20;
    quick.steps = {fast};
    Run q = Start(&quick);
    Facts g = Party(0);
    Tick(q, g);
    g.carrierBusy = true;
    ticks = 0;
    while (q.state == State::Running && ticks < 200)
    {
        Tick(q, g);
        ++ticks;
    }
    CHECK(q.state == State::Paused && ticks >= 19 && ticks <= 21 && q.note.find("no answer in 20 s") != std::string::npos);
}

// A lead bot busy with an earlier task holds the order back.
static void TestGossipWaitsForABusyLeadBot()
{
    DungeonPlan plan;
    plan.steps = {GossipRow(10)};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.carrierBusy = true;
    CHECK(Tick(r, f).empty() && !r.orderSent);
    CHECK(r.note.find("waiting for Tanky to finish an earlier order") != std::string::npos);
    f.carrierBusy = false;
    CHECK(Tick(r, f) == "gossip 7607 Tanky 1");
}

// Farther than 30 yd from the point the lead bot walks there first (a plain walk in sight, navmesh hops out of
// sight); the order goes out once it is within reach, and a walk once begun is finished.
static void TestGossipWalksToFarPointFirst()
{
    DungeonPlan plan;
    plan.steps = {GossipRow(45)};
    Run r = Start(&plan);
    Facts f = Party(0);
    CHECK(Tick(r, f) == "move 45.0 0.0 0.0");
    // 20 yd out is within the NPC search, but the walk was begun: it goes on to the point.
    f.leadX = 25;
    CHECK(Tick(r, f).empty() && !r.orderSent);
    f.leadX = 44;
    CHECK(Tick(r, f) == "gossip 7607 Tanky 1");

    // Out of sight of the point: a navmesh hop first.
    DungeonPlan far;
    far.steps = {GossipRow(200)};
    Run q = Start(&far);
    Facts g = Party(0);
    g.seen = false;
    g.approachKnown = true;
    g.approachX = 40;
    CHECK(Tick(q, g) == "move 40.0 0.0 0.0");
    g.leadX = 40;
    CHECK(Tick(q, g).empty());
    g.seen = true;
    CHECK(Tick(q, g) == "move 200.0 0.0 0.0");
    g.leadX = 200;
    CHECK(Tick(q, g) == "gossip 7607 Tanky 1");

    // Within 30 yd there is no walk at all.
    DungeonPlan near;
    near.steps = {GossipRow(29)};
    Run n = Start(&near);
    Facts h = Party(0);
    CHECK(Tick(n, h) == "gossip 7607 Tanky 1");
}

// A gossip right before a hold starts that event (like open / use_item): the run leader treats the walk and the
// step as inside the event, and a failed event goes back to it.
static void TestGossipStartsAnEvent()
{
    DungeonPlan plan;
    plan.map = 209;
    plan.steps = {Step(PlanStepKind::Move, 1, 2, 3), GossipRow(10), Step(PlanStepKind::Move, 1, 2, 3),
                  Step(PlanStepKind::Hold, 1, 2, 3)};
    plan.steps[3].entry = 1;
    plan.steps[3].flags = ai::kPlanFlagDoneData;
    plan.steps[3].entries = {5};
    Run r = Start(&plan, 1);
    CHECK(Current(r)->kind == PlanStepKind::Gossip && EventHoldAhead(r) != nullptr && InEvent(r));
    // A start on the hold or the move leading into it goes back to the gossip.
    std::size_t step = 99;
    bool back = false;
    CHECK(ResolveStartStep(plan, StartFrom::Step, "4", step, &back) == FromResult::Ok && step == 1 && back);
    CHECK(ResolveStartStep(plan, StartFrom::Step, "3", step, &back) == FromResult::Ok && step == 1 && back);
    CHECK(ResolveStartStep(plan, StartFrom::Step, "2", step, &back) == FromResult::Ok && step == 1 && !back);
    CHECK(EventStartStep(plan, 3) == 1);
    // The failed event returns to the gossip step.
    Run h = Start(&plan, 3);
    EventFailed(h, "data 1 did not leave 0");
    CHECK(h.state == State::Paused && h.step == 1);
    CHECK(h.note.find("starts it again at step 2 (gossip)") != std::string::npos);

    // Before a wait_object it is no event start: nothing to return to, no hold ahead.
    DungeonPlan door;
    door.steps = {GossipRow(10), Step(PlanStepKind::WaitObject, 1, 2, 3)};
    door.steps[1].entry = 146084;
    Run d = Start(&door);
    CHECK(EventHoldAhead(d) == nullptr && !InEvent(d));
}

// The ZF shape: hold until the pyramid data reads 8, the gossip, then the door wait.
static void TestGossipAfterAHoldThenTheDoor()
{
    DungeonPlan plan;
    PlanStep hold = Step(PlanStepKind::Hold, 1877.5f, 1199.6f, 8.9f);
    hold.entry = 1;
    hold.param = 8;
    hold.flags = ai::kPlanFlagDoneData;
    hold.entries = {7787};
    hold.timeoutS = 600;
    ai::NormalizePlanStep(hold);
    PlanStep door = Step(PlanStepKind::WaitObject, 1854.5f, 1142.9f, 16.1f);
    door.entry = 146084;
    door.param = 1;
    ai::NormalizePlanStep(door);
    PlanStep talk = GossipRow(1877.5f, 1);
    talk.y = 1199.6f;
    talk.z = 8.9f;
    plan.steps = {hold, talk, door};
    Run r = Start(&plan);
    Facts f = Party(1877.5f);
    f.leadY = 1199.6f;
    f.leadZ = 8.9f;
    f.eventData = 8; // the hold's data already reads 8: it ends at once
    // The hold ends on the first look and the gossip step goes out in the same tick (it is in sight, the lead bot on its spot).
    CHECK(Tick(r, f) == "gossip 7607 Tanky 1");
    CHECK(r.step == 1 && r.orderSent);
    f.carrierBusy = true;
    f.taskReply = 1;
    Tick(r, f);
    CHECK(r.step == 2 && Current(r)->kind == PlanStepKind::WaitObject);
}

// Zul'Farrak's Sergeant Bly (turtle-agent feat/zf-gahz-bly): his gossip, then a hold done by its units (no done flag) with Bly
// first. Bly and his crew stay friendly for 10 s after the option; a boss row would leave them alone at once, the hold waits:
// the friendly first unit counts (EventUnitCounts), no friendly unit is pulled, the event starts when one turns hostile, and
// the hold ends only when none of the four is left alive within its radius.
static void TestUnitsHoldWaitsForAFriendlyBossToTurn()
{
    PlanStep hold = Step(PlanStepKind::Hold);
    hold.radius = 25.0f;
    hold.timeoutS = 300;
    hold.entries = {7604, 7608, 7606, 7605};
    CHECK(ai::NormalizePlanStep(hold) == ai::PlanStepRefusal::None);
    DungeonPlan plan;
    plan.map = 209;
    plan.steps = {GossipRow(0, 1, 7604), hold, Step(PlanStepKind::Move, 500)};
    Run r = Start(&plan);
    Facts f = Party(0);
    CHECK(Tick(r, f) == "gossip 7604 Tanky 1");
    f.carrierBusy = true;
    f.taskReply = 1;
    Tick(r, f);
    CHECK(r.step == 1 && Current(r)->kind == PlanStepKind::Hold);
    f.carrierBusy = false;
    f.taskReply = 0;
    std::vector<UnitFact> crew;
    for (std::uint32_t e : {7604u, 7608u, 7606u, 7605u})
    {
        UnitFact u = Unit(e, e, 11.0f);
        u.hostile = false;
        crew.push_back(u);
    }
    f.units = crew;
    // The 10 s before the script turns them: held, nothing pulled, nothing done.
    for (int i = 0; i < 12; ++i)
    {
        std::string const order = Tick(r, f);
        CHECK(order.find("attack") == std::string::npos && order.find("pull") == std::string::npos);
    }
    CHECK(r.state == State::Running && r.step == 1 && !r.eventStarted && r.eventTotal == 1);
    // Hostile: the event is on; it is not done while one of them lives.
    for (UnitFact& u : f.units)
        u.hostile = true;
    Tick(r, f);
    CHECK(r.state == State::Running && r.step == 1 && r.eventStarted && r.eventTotal == 4);
    f.units = {f.units[0]};
    f.eventDown = 3;
    Tick(r, f);
    CHECK(r.state == State::Running && r.step == 1);
    f.units.clear();
    f.eventDown = 4;
    Tick(r, f);
    CHECK(r.state == State::Running && r.step == 2 && r.note.find("every unit of it is dead") != std::string::npos);

    // The contrast: a boss row on the friendly Bly ends at once and leaves him alone.
    DungeonPlan bossPlan;
    PlanStep boss = Step(PlanStepKind::Boss);
    boss.entry = 7604;
    boss.entries = {7608, 7606, 7605};
    CHECK(ai::NormalizePlanStep(boss) == ai::PlanStepRefusal::None);
    bossPlan.steps = {boss, Step(PlanStepKind::Move, 500)};
    Run b = Start(&bossPlan);
    Facts g = Party(0);
    g.units = crew;
    Tick(b, g);
    CHECK(b.step == 1 && b.note.find("boss is friendly to this party: left alone") != std::string::npos);
}

// ---- r15-19z33 Zul'Farrak: Bly's crew dead before the Bly leg (kPlanFlagDeadNpcIsDone, the units hold with nothing alive) ----

static const char* const kBlyDeadAnswer =
    "no-npc: No living NPC of that entry is within 40 yd of the bot: a dead one lies nearby: Sergeant Bly (7604) "
    "lies dead 3 yd away and respawns in 6 d 23 h (604800 s spawn timer).";
static const char* const kBlyDoneNote =
    "gossip 7604: Sergeant Bly lies dead, nothing to talk to: the step is counted done (the plan marks a dead NPC as done)";

// Bly's gossip row as the migration writes it: flag 512.
static PlanStep BlyGossip(bool flag)
{
    PlanStep s;
    s.kind = PlanStepKind::Gossip;
    s.entry = 7604;
    s.param = 1;
    s.flags = flag ? ai::kPlanFlagDeadNpcIsDone : 0u;
    CHECK(ai::NormalizePlanStep(s) == ai::PlanStepRefusal::None);
    return s;
}

// Bly's crew hold: no done flag, radius 25, 300 s, the four entries.
static PlanStep CrewHold()
{
    PlanStep hold = Step(PlanStepKind::Hold);
    hold.radius = 25.0f;
    hold.timeoutS = 300;
    hold.entries = {7604, 7608, 7606, 7605};
    CHECK(ai::NormalizePlanStep(hold) == ai::PlanStepRefusal::None);
    return hold;
}

// Ticks the run `n` times, returns how many ticks it stayed on `step` (stops when it left or stopped running).
static int TicksOnStep(Run& r, const Facts& f, std::size_t step, int n)
{
    int stayed = 0;
    for (int i = 0; i < n && r.state == State::Running && r.step == step; ++i)
    {
        Tick(r, f);
        if (r.state == State::Running && r.step == step)
            ++stayed;
    }
    return stayed;
}

// Step 223 of r15: the order refused with the dead-NPC wording. With the flag the step is done and the next one runs; without it
// the run pauses plainly as before (Weegli 7607 stays a pause: the End Door cannot be opened without him).
static void TestGossipDeadIsDoneWithFlag()
{
    DungeonPlan plan;
    plan.steps = {BlyGossip(true), Step(PlanStepKind::Rest)};
    Run r = Start(&plan);
    Facts f = Party(0);
    std::string const order = Tick(r, f);
    CHECK(order == "gossip 7604 Tanky 1");
    OrderRefused(r, order, kBlyDeadAnswer);
    CHECK(r.state == State::Running && r.step == 1);
    CHECK(r.note == kBlyDoneNote);
    CHECK(!r.orderSent);

    // The later failure (the NPC died on the bot's way, the task's ERR) is the same.
    Run q = Start(&plan);
    CHECK(Tick(q, f) == "gossip 7604 Tanky 1");
    f.taskReply = 2;
    f.taskText = "no-npc: The NPC died or despawned before the bot reached it: Sergeant Bly (7604) lies dead 3 yd away "
                 "and respawns in 6 d 23 h (604800 s spawn timer).";
    Tick(q, f);
    CHECK(q.state == State::Running && q.step == 1);
    CHECK(q.note == kBlyDoneNote);
    f.taskReply = 0;

    // The same refusal without the flag pauses with the plain dead-NPC text.
    DungeonPlan plain;
    plain.steps = {BlyGossip(false), Step(PlanStepKind::Rest)};
    Run p = Start(&plain);
    f = Party(0);
    OrderRefused(p, Tick(p, f), kBlyDeadAnswer);
    CHECK(p.state == State::Paused && p.step == 0);
    CHECK(p.note.find("gossip 7604 option 1 cannot be done") != std::string::npos);
    CHECK(p.note.find("counted done") == std::string::npos);
    Run pt = Start(&plain);
    Tick(pt, f);
    f.taskReply = 2;
    f.taskText = "no-npc: The NPC died or despawned before the bot reached it: Sergeant Bly (7604) lies dead 3 yd away.";
    Tick(pt, f);
    CHECK(pt.state == State::Paused && pt.step == 0 && pt.note.find("cannot be done") != std::string::npos);
}

// The flag counts a DEAD NPC as done, nothing else: every other refusal or failure of the order still pauses.
static void TestGossipDeadIsDoneOnlyForADeadNpc()
{
    DungeonPlan plan;
    plan.steps = {BlyGossip(true), Step(PlanStepKind::Rest)};
    for (const char* answer : {"no-npc: No living NPC of that entry is within 40 yd of the bot.",
             "not-allowed: That NPC is not in AiPlayerbot.BotGossipNpcs, so bots may not talk to it.",
             "busy: the bot is busy", "no-option: No option 1."})
    {
        Run r = Start(&plan);
        Facts f = Party(0);
        OrderRefused(r, Tick(r, f), answer);
        CHECK(r.state == State::Paused && r.step == 0);
        Run t = Start(&plan);
        Tick(t, f);
        f.taskReply = 2;
        f.taskText = answer;
        Tick(t, f);
        CHECK(t.state == State::Paused && t.step == 0);
    }
    // A flagged gossip step's flag is not read on another order's refusal (a move on the way is a plain refusal).
    Run m = Start(&plan);
    Facts f = Party(0);
    Tick(m, f);
    OrderRefused(m, "move 5 5 5", kBlyDeadAnswer);
    CHECK(m.step == 0);
}

// A units hold whose four units lay dead before the hold (r15: "4 of 4 down" for 300 s): it ends once the spot has been in sight for
// kHoldAllDeadMs, and not before.
static void TestUnitsHoldAllDeadOnEntryEndsAfterTenSeconds()
{
    CHECK(kHoldAllDeadMs == 10000);
    DungeonPlan plan;
    plan.steps = {CrewHold(), Step(PlanStepKind::Move, 500)};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.eventDown = 4; // the corpses are in sight, nothing alive
    // The first tick is the first look; the clock runs from the next one: 9 s later it still holds, at 10 s it is done.
    int ticks = 1;
    Tick(r, f);
    CHECK(r.state == State::Running && r.step == 0 && r.eventTotal == 4 && !r.eventStarted);
    while (r.state == State::Running && r.step == 0 && ticks < 40)
    {
        Tick(r, f);
        ++ticks;
    }
    CHECK(r.state == State::Running && r.step == 1);
    CHECK(ticks == static_cast<int>(kHoldAllDeadMs / kTickMs) + 1);
    CHECK(r.note.find("event done: none of its units is alive (they died before the hold)") != std::string::npos);
    CHECK(r.note.find("4 of 4 down") != std::string::npos);
}

// The whole r15 step 223-224 replay: the crew is dead, the gossip flagged: no pause at the gossip, the hold ends by itself.
static void TestBlyCrewDeadBeforeTheLegRunsUnattended()
{
    DungeonPlan plan;
    plan.steps = {BlyGossip(true), CrewHold(), Step(PlanStepKind::Move, 500)};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.eventDown = 4;
    std::string const order = Tick(r, f);
    CHECK(order == "gossip 7604 Tanky 1");
    OrderRefused(r, order, kBlyDeadAnswer);
    CHECK(r.state == State::Running && r.step == 1 && Current(r)->kind == PlanStepKind::Hold);
    int ticks = 0;
    while (r.state == State::Running && r.step == 1 && ticks < 60)
    {
        Tick(r, f);
        ++ticks;
    }
    CHECK(r.state == State::Running && r.step == 2 && ticks <= 15);
    CHECK(r.note.find("none of its units is alive") != std::string::npos);
}

// What the all-dead end must not do: end a hold early where the units are merely not there yet, or alive, or were alive, or where the
// event already started; and the ceiling stays for every other case.
static void TestUnitsHoldAllDeadDoesNotEndEarly()
{
    DungeonPlan plan;
    plan.steps = {CrewHold(), Step(PlanStepKind::Move, 500)};
    // The spot is not in sight: the hold approaches it, never counts the dead.
    {
        Run r = Start(&plan);
        Facts f = Party(0);
        f.seen = false;
        f.leadX = 80.0f;
        f.approachKnown = true;
        f.approachX = 40.0f;
        f.eventDown = 4;
        CHECK(Tick(r, f).find("move") == 0); // a hop toward the spot
        f.leadX = 40.0f;
        Tick(r, f);
        CHECK(r.state == State::Running && r.step == 0 && r.note.find("none of its units") == std::string::npos);
        CHECK(!r.eventOn && r.eventMs == 0); // the event's clock has not started: no look yet
        // In sight at last: the clock starts then, not before.
        f.seen = true;
        f.leadX = 0.0f;
        Tick(r, f);
        CHECK(r.step == 0 && r.state == State::Running);
        CHECK(TicksOnStep(r, f, 0, 8) == 8);
    }
    // Nothing alive and nothing dead in sight (the units are not spawned or not visible yet): no end, the old 300 s ceiling.
    {
        Run r = Start(&plan);
        Facts f = Party(0);
        f.eventDown = 0;
        CHECK(TicksOnStep(r, f, 0, 120) == 120);
        for (int i = 0; i < 400 && r.state == State::Running; ++i)
            Tick(r, f);
        CHECK(r.state == State::Paused && r.note.find("event not done after 300 s") != std::string::npos);
    }
    // A unit alive in the radius (friendly, it has not turned): the hold waits, up to the ceiling.
    {
        Run r = Start(&plan);
        Facts f = Party(0);
        UnitFact bly = Unit(7604, 7604, 11.0f);
        bly.hostile = false;
        f.units = {bly};
        f.eventDown = 3;
        CHECK(TicksOnStep(r, f, 0, 60) == 60);
        for (int i = 0; i < 400 && r.state == State::Running; ++i)
            Tick(r, f);
        CHECK(r.state == State::Paused && r.note.find("event not done after 300 s") != std::string::npos);
    }
    // A unit was alive in this hold and then died while the event never started (a friendly one killed by something else): not the
    // all-dead end, which asks for none ever counted alive.
    {
        Run r = Start(&plan);
        Facts f = Party(0);
        UnitFact bly = Unit(7604, 7604, 11.0f);
        bly.hostile = false;
        f.units = {bly};
        Tick(r, f);
        Tick(r, f);
        f.units.clear();
        f.eventDown = 4;
        CHECK(TicksOnStep(r, f, 0, 30) == 30);
    }
    // The event started (a unit seen awake): the old rule, every unit dead, ends it at once.
    {
        Run r = Start(&plan);
        Facts f = Party(0);
        UnitFact bly = Unit(7604, 7604, 11.0f);
        f.units = {bly}; // hostile
        Tick(r, f);
        CHECK(r.eventStarted && r.step == 0);
        f.units.clear();
        f.eventDown = 4;
        Tick(r, f);
        CHECK(r.step == 1 && r.note.find("every unit of it is dead") != std::string::npos);
    }
}

// holdUnitSeen belongs to one hold: ResetStep clears it. A leftover true (a step before it, or the same hold before a pause and a resume)
// would keep a later all-dead hold waiting its whole 300 s.
static void TestHoldUnitSeenDoesNotLeakAcrossSteps()
{
    // Two units holds in a row: the first sees a unit alive (and awake), ends when they are dead; the second finds only corpses.
    {
        DungeonPlan plan;
        plan.steps = {CrewHold(), CrewHold(), Step(PlanStepKind::Move, 500)};
        Run r = Start(&plan);
        Facts f = Party(0);
        f.units = {Unit(7604, 7604, 11.0f)};
        Tick(r, f);
        CHECK(r.step == 0 && r.holdUnitSeen);
        f.units.clear();
        f.eventDown = 4;
        Tick(r, f);
        CHECK(r.step == 1 && !r.holdUnitSeen); // the first hold ended ("every unit of it is dead"); nothing leaks into the second
        int ticks = 0;
        while (r.state == State::Running && r.step == 1 && ticks < 40)
        {
            Tick(r, f);
            ++ticks;
        }
        CHECK(r.state == State::Running && r.step == 2 && ticks <= 15);
        CHECK(r.note.find("none of its units is alive") != std::string::npos);
    }
    // Pause and resume of one units hold: the unit seen alive before the pause does not count after the resume.
    {
        DungeonPlan plan;
        plan.steps = {CrewHold(), Step(PlanStepKind::Move, 500)};
        Run r = Start(&plan);
        Facts f = Party(0);
        UnitFact bly = Unit(7604, 7604, 11.0f);
        bly.hostile = false;
        f.units = {bly};
        Tick(r, f);
        CHECK(r.holdUnitSeen && r.step == 0);
        Pause(r, "test pause");
        CHECK(Resume(r));
        CHECK(!r.holdUnitSeen);
        f.units.clear();
        f.eventDown = 4;
        int ticks = 0;
        while (r.state == State::Running && r.step == 0 && ticks < 80)
        {
            Tick(r, f);
            ++ticks;
        }
        CHECK(r.state == State::Running && r.step == 1);
        CHECK(r.note.find("none of its units is alive") != std::string::npos);
    }
}

// Not for a data, object or encounter hold: they end by their own condition, units or no units.
static void TestAllDeadEndIsForAUnitsHoldOnly()
{
    for (std::uint32_t flags : {ai::kPlanFlagDoneData, ai::kPlanFlagDoneObject,
             ai::kPlanFlagDoneData | ai::kPlanFlagEncounter})
    {
        PlanStep hold = Step(PlanStepKind::Hold);
        hold.radius = 25.0f;
        hold.timeoutS = 300;
        hold.entry = 1;
        hold.param = (flags & ai::kPlanFlagDoneObject) ? 1 : 3;
        hold.flags = flags;
        hold.entries = {7604, 7608};
        CHECK(ai::NormalizePlanStep(hold) == ai::PlanStepRefusal::None);
        DungeonPlan plan;
        plan.steps = {hold, Step(PlanStepKind::Move, 500)};
        Run r = Start(&plan);
        Facts f = Party(0);
        f.eventDown = 2;
        f.eventData = 1; // in progress: not done, not failed
        f.objectState = 0;
        for (int i = 0; i < 30 && r.state == State::Running; ++i)
            Tick(r, f);
        CHECK(r.step == 0);
        CHECK(r.note.find("none of its units") == std::string::npos);
    }
}

static void TestWaitObjectTimeoutIsBounded()
{
    DungeonPlan plan;
    PlanStep wait = Step(PlanStepKind::WaitObject);
    wait.entry = 5;
    wait.timeoutS = 600;
    plan.steps = {wait};
    Run r = Start(&plan);
    Facts f = Party(0);
    std::uint32_t ticks = 0;
    while (r.state == State::Running && ticks < 2000)
    {
        Tick(r, f);
        ++ticks;
    }
    // The wait's own timeout ends it, not the generic step ceiling (300 s).
    CHECK(r.state == State::Paused && ticks >= 600 && ticks <= 602);
    CHECK(r.note.find("not found") != std::string::npos);
}

static void TestUnsupportedPausesAndSkip()
{
    DungeonPlan plan;
    // A kind the run does not know (the loader never lets one through; the
    // machine still pauses on it rather than guess).
    PlanStep unknown;
    unknown.kind = PlanStepKind::None;
    plan.steps = {unknown, Step(PlanStepKind::Move, 10)};
    Run r = Start(&plan);
    Facts f = Party(0);
    Tick(r, f);
    CHECK(r.state == State::Paused && r.note.find("not supported yet") != std::string::npos);
    CHECK(!Supported(PlanStepKind::None) && Supported(PlanStepKind::Move));
    CHECK(Supported(PlanStepKind::Ritual) && Supported(PlanStepKind::Hold) && Supported(PlanStepKind::Gossip));
    // Skip from a pause goes on running (after the regroup).
    CHECK(Skip(r));
    CHECK(r.state == State::Running && r.step == 1 && r.regroup);
    CHECK(!Resume(r));
    CHECK(Tick(r, f) == "move 10.0 0.0 0.0");
}

static void TestPlayerOrderAndRefusalPause()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 10)};
    Run r = Start(&plan);
    CHECK(PauseForPlayer(r, "cmd attack"));
    CHECK(!PauseForPlayer(r, "cmd attack"));
    CHECK(r.state == State::Paused && r.note.find("cmd attack") != std::string::npos);
    Facts f = Party(0);
    CHECK(Tick(r, f).empty());
    CHECK(Resume(r));
    OrderRefused(r, "rest", "failed: No bot of the group accepted the order.");
    CHECK(r.state == State::Running && r.note.find("rest refused") == 0);
    OrderRefused(r, "move 10.0 0.0 0.0", "no-path: The lead bot has no walkable path to that point.");
    CHECK(r.state == State::Paused);
    // The status line keeps the protocol's field count.
    r.note = "a|b";
    std::string line = StatusLine(r);
    int bars = 0;
    for (char c : line)
        bars += c == '|';
    CHECK(bars == 7);
}

// Issue #999: a gather refused (a live ZF run: "cmd move 1420.7 801.1 11.0
// refused unreachable" with the tank on that very spot) is sent once more,
// then the pull goes on without it instead of pausing the run.
static void TestRefusedGatherIsRetriedOnceThenLeftOut()
{
    DungeonPlan plan;
    plan.steps = {PullStep(10, {1})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.maxMemberYd = 30;
    f.units = {Unit(7, 1, 20), Unit(8, 1, 25)};
    std::string const gather = "move 0.0 0.0 0.0";
    CHECK(Tick(r, f) == gather);
    OrderRefused(r, gather, "unreachable: The lead bot has no walkable path to that point.");
    CHECK(r.state == State::Running && r.note.find("once more") != std::string::npos);
    CHECK(Tick(r, f) == gather);
    OrderRefused(r, gather, "unreachable: The lead bot has no walkable path to that point.");
    CHECK(r.state == State::Running && r.note.find("without it") != std::string::npos);
    CHECK(Tick(r, f) == "pullback 7");
    // The next pull gathers again: the waiver held for one pull only.
    f.anyInCombat = true;
    Tick(r, f);
    f.anyInCombat = false;
    f.units = {Unit(8, 1, 25)};
    CHECK(Tick(r, f) == gather);
    CHECK(r.state == State::Running);

    // A direct step: refused twice, the direct walk goes out ungathered.
    DungeonPlan direct;
    direct.steps = {Step(PlanStepKind::MoveDirect, 5)};
    Run d = Start(&direct);
    Facts g = Party(0);
    g.maxMemberYd = 30;
    CHECK(Tick(d, g) == gather);
    OrderRefused(d, gather, "unreachable");
    CHECK(Tick(d, g) == gather);
    OrderRefused(d, gather, "unreachable");
    CHECK(d.state == State::Running && Tick(d, g) == "move 5.0 0.0 0.0 direct");

    // Other refusals still pause, also a gather's text sent on an earlier tick.
    Run o = Start(&plan);
    OrderRefused(o, gather, "unreachable");
    CHECK(o.state == State::Paused);
}

// Issue #999: a wipe regroups by itself once the fight is over (revive,
// gather), then walks the plan's own move points back from the entrance to
// the step; it counts the wipe; the third at one step pauses as before.
static void TestWipeRegroupsAndWalksBack()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 10), Step(PlanStepKind::Move, 50), Step(PlanStepKind::Move, 90),
        PullStep(130, {1})};
    Run r = Start(&plan, 3);
    Facts f = Party(130);
    f.dead = 5;
    f.alive = 0;
    f.leadAlive = false;
    CHECK(Tick(r, f).empty());
    CHECK(r.state == State::Running && r.regroup && r.wipes == 1 && r.autoRegroups == 1);
    CHECK(r.note.find("(1 wipe this run)") != std::string::npos);
    CHECK(Tick(r, f) == "revive party");
    for (int i = 0; i < 30; ++i)
        Tick(r, f); // the corpse run: still one wipe
    CHECK(r.wipes == 1 && r.state == State::Running);
    // Alive at the entrance, spread: gather, then the walk back.
    f = Party(0);
    f.maxMemberYd = 40;
    CHECK(Tick(r, f) == "move 0.0 0.0 0.0");
    f.maxMemberYd = 5;
    CHECK(Tick(r, f).empty()); // regrouped; the walk back starts with the facts of its point
    // 19z5: straight to the last point before the step (90), in navmesh hops.
    CHECK(r.walkBack && r.walkBackDirect && r.walkBackIdx == 2 && FactStep(r) == &plan.steps[2] && OwnerFrom(r) == 2);
    f.seen = false; // FactStep's point, 90 yd away
    f.approachKnown = true;
    f.approachX = 40;
    CHECK(Tick(r, f) == "move 40.0 0.0 0.0");
    CHECK(r.note.find("walk back: approaching") == 0);
    f.leadX = 40;
    CHECK(Tick(r, f).empty()); // at the hop
    // A pack that came back by the next hop is pulled before the move.
    f.hopUnits = {Unit(9, 4, 30)};
    CHECK(Tick(r, f) == "pullback 9");
    CHECK(r.walkBack && r.note.find("walk back: pulling") == 0);
    f.hopUnits.clear();
    f.anyInCombat = true;
    Tick(r, f);
    f.anyInCombat = false;
    // The hops fail (no path from here): the plan's points are replayed from
    // the nearest one (50, from 40).
    f.approachKnown = false;
    Tick(r, f);
    CHECK(r.walkBack && !r.walkBackDirect && r.walkBackIdx == 1 && r.note.find("replaying") != std::string::npos);
    f.seen = true;
    CHECK(Tick(r, f) == "move 50.0 0.0 0.0");
    // A point the navmesh refuses now is passed for the next one.
    OrderRefused(r, "move 50.0 0.0 0.0", "unreachable");
    CHECK(r.state == State::Running);
    CHECK(Tick(r, f) == "move 90.0 0.0 0.0");
    f.leadX = 90;
    CHECK(Tick(r, f).empty());
    CHECK(Tick(r, f).empty() && !r.walkBack && r.note.find("walked back") == 0);
    CHECK(FactStep(r) == &plan.steps[3] && OwnerFrom(r) == 3);
    f.units = {Unit(7, 1, 40)};
    CHECK(Tick(r, f) == "pullback 7");
    CHECK(r.step == 3);

    // Second wipe at the step: regroups once more; the third pauses.
    f = Party(130);
    f.dead = 5;
    f.alive = 0;
    f.leadAlive = false;
    Tick(r, f);
    CHECK(r.state == State::Running && r.wipes == 2 && r.autoRegroups == 2);
    f = Party(130); // revived where it stands, near the step: no walk back
    f.units = {Unit(7, 1, 40)};
    CHECK(Tick(r, f) == "pullback 7");
    CHECK(!r.regroup && !r.walkBack);
    f.dead = 5;
    f.alive = 0;
    f.leadAlive = false;
    Tick(r, f);
    CHECK(r.state == State::Paused && r.wipes == 3 && r.note.find("did not hold") != std::string::npos);
    CHECK(r.note.find("(cmd revive party)") != std::string::npos);
    // A resume gives the step its regroups back; a new step too.
    CHECK(Resume(r) && r.autoRegroups == 0);
}

// Three dead while the other two still fight: the fight goes on (bounded by
// its ceiling), the regroup starts when it ends; no wipe is counted.
static void TestThreeDeadRegroupAfterTheFight()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 40)};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.dead = 3;
    f.alive = 2;
    f.leadAlive = false;
    f.anyInCombat = true;
    CHECK(Tick(r, f).empty());
    CHECK(r.state == State::Running && !r.regroup && r.note.find("regroup follows") != std::string::npos);
    f.anyInCombat = false;
    Tick(r, f);
    CHECK(r.state == State::Running && r.regroup && r.wipes == 0 && r.nearWipes == 1);
    CHECK(r.note.find("(1 near wipe this run)") != std::string::npos);
    CHECK(Tick(r, f) == "revive party");
    // No earlier move point: nothing to walk back, the step goes on.
    f = Party(0);
    CHECK(Tick(r, f) == "move 40.0 0.0 0.0");
    CHECK(!r.walkBack);
    // The done note carries the wipes.
    r.wipes = 2;
    f.leadX = 40;
    Tick(r, f);
    CHECK(r.state == State::Done && r.note == "plan done (2 wipes, 1 near wipe)");
}

// Issue #999 (third observe run, step 35): the healer stood 82 yd behind and
// did not come; the gather paused the run again and again. At the gather's
// ceiling the farthest bot over 40 yd that came less than 5 yd closer since
// the last sample is summoned to the lead bot, once; one on its way is not.
static void TestGatherSummonsAStalledStraggler()
{
    DungeonPlan plan;
    plan.steps = {PullStep(10, {1})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.maxMemberYd = 90;
    f.members = {Mem("Walky", 90), Mem("Healy", 82), Mem("Nearby", 15), Mem("Stuck", 50)};
    f.units = {Unit(7, 1, 20)};
    CHECK(Tick(r, f) == "move 0.0 0.0 0.0"); // the gather: first sample
    // By the ceiling Walky came 30 yd closer; Healy and Stuck did not.
    f.members = {Mem("Walky", 60), Mem("Healy", 81), Mem("Nearby", 15), Mem("Stuck", 50)};
    f.maxMemberYd = 81;
    std::string order;
    for (std::uint32_t i = 0; i < kGatherCapMs / kTickMs + 2 && order.empty(); ++i)
    {
        f.members[0].x += (i % 2 ? 3.0f : -3.0f); // Walky keeps walking (not standing still)
        order = Tick(r, f);
    }
    CHECK(order == "summon Healy"); // the farthest stalled one
    CHECK(r.state == State::Running && r.phase == Phase::Gather && r.note.find("Healy") != std::string::npos);
    // A refused summon does not pause: the gather's ceiling does.
    OrderRefused(r, order, "lead-unavailable");
    CHECK(r.state == State::Running);
    // Next ceiling: Walky still comes (no summon), Stuck stalled since the last sample: summoned.
    f.members = {Mem("Walky", 50), Mem("Healy", 81), Mem("Nearby", 15), Mem("Stuck", 49)};
    order.clear();
    for (std::uint32_t i = 0; i < kGatherCapMs / kTickMs + 2 && order.empty(); ++i)
    {
        f.members[0].x += (i % 2 ? 3.0f : -3.0f); // Walky keeps walking (not standing still)
        order = Tick(r, f);
    }
    CHECK(order == "summon Stuck");
    // Nobody left to summon (Walky still makes progress): the next ceiling pauses.
    f.members = {Mem("Walky", 44), Mem("Healy", 81), Mem("Nearby", 15), Mem("Stuck", 49)};
    for (std::uint32_t i = 0; i < kGatherCapMs / kTickMs + 2 && r.state == State::Running; ++i)
    {
        f.members[0].x += (i % 2 ? 3.0f : -3.0f);
        CHECK(Tick(r, f).find("summon") == std::string::npos);
    }
    CHECK(r.state == State::Paused && r.note.find("did not gather") != std::string::npos);

    // The summoned bot arrives: the pull goes out.
    Run g = Start(&plan);
    Facts h = Party(0);
    h.maxMemberYd = 82;
    h.members = {Mem("Healy", 82)};
    h.units = {Unit(7, 1, 20)};
    Tick(g, h);
    std::string got;
    for (std::uint32_t i = 0; i < kGatherCapMs / kTickMs + 2 && got.empty(); ++i)
        got = Tick(g, h);
    CHECK(got == "summon Healy");
    h.maxMemberYd = 5;
    h.members.clear();
    CHECK(Tick(g, h) == "pullback 7");
}

// Live (events-5 00:55:59 and again in the regroup 00:57:35, grimlok pull 93):
// a bot stuck 34.9 yd out (a navmesh pocket at the pit) was never summoned --
// the gather (kGatherYd 20) waits for it, the summon's ceiling (kSummonYd 40)
// skips it -- so the run paused "the party did not gather within 30 s (a bot is
// 34.9 yd ...)" until a person summoned it.
static void TestStillBotInsideSummonRangeIsSummoned()
{
    DungeonPlan plan;
    plan.steps = {PullStep(100, {1})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.maxMemberYd = 34.9f;
    f.members = {Mem("Tanky", 0), Mem("Stoutster", 34.9f)};
    f.units = {Unit(7, 1, 50)};
    CHECK(Tick(r, f) == "move 0.0 0.0 0.0"); // the gather
    std::string order;
    for (std::uint32_t i = 0; i < kGatherCapMs / kTickMs + 2 && order.empty(); ++i)
        order = Tick(r, f);
    CHECK(order == "summon Stoutster");
    CHECK(r.state == State::Running && r.phase == Phase::Gather);
    CHECK(r.note.find("Stoutster") != std::string::npos && r.note.find("has not moved") != std::string::npos);
}

// ... and a busy bot is not stuck: one in a fight, or with something on it
// (units attacking it), is left alone, exactly as in a regroup
// (SummonStuckInRegroup) and as the revived bot's summon does -- `cmd summon`
// refuses a bot in combat, so the order would only be refused. The gather's
// ceiling then pauses as before.
static void TestStuckBotInCombatIsNotSummoned()
{
    DungeonPlan plan;
    plan.steps = {PullStep(100, {1})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.maxMemberYd = 34.9f;
    f.members = {Mem("Tanky", 0), Mem("Stoutster", 34.9f)};
    f.members[1].inCombat = true;
    f.units = {Unit(7, 1, 50)};
    CHECK(Tick(r, f) == "move 0.0 0.0 0.0"); // the gather
    std::string order;
    for (std::uint32_t i = 0; i < kGatherCapMs / kTickMs + 2 && r.state == State::Running; ++i)
    {
        order = Tick(r, f);
        CHECK(order.find("summon") == std::string::npos);
    }
    CHECK(r.state == State::Paused && r.note.find("did not gather") != std::string::npos);
}

static void TestStuckBotWithAttackersIsNotSummoned()
{
    DungeonPlan plan;
    plan.steps = {PullStep(100, {1})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.maxMemberYd = 34.9f;
    f.members = {Mem("Tanky", 0), Mem("Stoutster", 34.9f)};
    f.members[1].attackers = 1; // a mob on it: it runs, it does not stand (RunBack)
    f.units = {Unit(7, 1, 50)};
    CHECK(Tick(r, f) == "move 0.0 0.0 0.0"); // the gather
    std::string order;
    for (std::uint32_t i = 0; i < kGatherCapMs / kTickMs + 2 && r.state == State::Running; ++i)
    {
        order = Tick(r, f);
        CHECK(order.find("summon") == std::string::npos);
    }
    CHECK(r.state == State::Paused && r.note.find("did not gather") != std::string::npos);
}

// Live r4-19z21 slot 2 step 102 (13:57:42): the gather paused "the party did not
// gather within 30 s (a bot is 33.7 yd from the lead bot)" while that bot was
// coming at about 1 yd/s (54.9 -> 28.7 yd in the trace). A bot that keeps
// coming is waited for, within kGatherComingMaxMs (T5). A walking bot that
// does not come closer (it wobbles about 34 yd out) is summoned at the first
// ceiling, once; if it is still not here by the next, the run pauses.
static void TestMovingBotThatDoesNotGainIsSummoned()
{
    DungeonPlan plan;
    plan.steps = {PullStep(100, {1})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.maxMemberYd = 34.9f;
    f.members = {Mem("Tanky", 0), Mem("Stoutster", 34.9f)};
    f.units = {Unit(7, 1, 50)};
    CHECK(Tick(r, f) == "move 0.0 0.0 0.0");
    std::string order;
    std::uint32_t ticks = 0;
    for (; ticks < 2 * kGatherCapMs / kTickMs + 6 && order.empty(); ++ticks)
    {
        float const yd = (ticks % 2) ? 33.7f : 35.0f; // walking, but not coming
        f.members[1] = Mem("Stoutster", yd);
        f.members[1].x += (ticks % 2 ? 3.0f : -3.0f);
        f.maxMemberYd = yd;
        order = Tick(r, f);
        CHECK(r.state == State::Running);
    }
    CHECK(order == "summon Stoutster");
    CHECK(ticks <= kGatherCapMs / kTickMs + 2); // at the first ceiling, not at 90 s
    CHECK(r.phase == Phase::Gather);
    // Summoned and still not here at the next ceiling: pause as before.
    for (std::uint32_t i = 0; i < kGatherCapMs / kTickMs + 2 && r.state == State::Running; ++i)
    {
        float const yd = (i % 2) ? 33.7f : 35.0f;
        f.members[1] = Mem("Stoutster", yd);
        f.members[1].x += (i % 2 ? 3.0f : -3.0f);
        f.maxMemberYd = yd;
        CHECK(Tick(r, f).find("summon") == std::string::npos);
    }
    CHECK(r.state == State::Paused && r.note.find("did not gather") != std::string::npos);
}

// T5: the trace's straggler, 54.9 yd out and walking in at 0.6 yd/s, is waited
// for past the 30 s ceiling; once within kGatherYd the pull goes out.
static void TestComingStragglerIsWaitedFor()
{
    DungeonPlan plan;
    plan.steps = {PullStep(100, {1})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.maxMemberYd = 54.9f;
    f.members = {Mem("Tanky", 0), Mem("Walky", 54.9f)};
    f.units = {Unit(7, 1, 50)};
    CHECK(Tick(r, f) == "move 0.0 0.0 0.0");
    bool sawComing = false;
    std::string order;
    std::uint32_t ticks = 1;
    for (; ticks < 120 && order.empty(); ++ticks)
    {
        float const yd = 54.9f - 0.6f * static_cast<float>(ticks);
        f.members[1] = Mem("Walky", yd);
        f.maxMemberYd = yd;
        order = Tick(r, f);
        CHECK(r.state == State::Running);
        CHECK(order.find("summon") == std::string::npos);
        if (r.note.find("Walky is coming") != std::string::npos)
            sawComing = true;
    }
    CHECK(sawComing);
    CHECK(order == "pullback 7");
    CHECK(ticks > kGatherCapMs / kTickMs); // it went past the 30 s ceiling
}

// ... also at a direct step's gather and at the walk back's direct gather.
static void TestComingStragglerIsWaitedForAtDirectGathers()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::MoveDirect, 5), PullStep(100, {1})};
    for (int walkBack = 0; walkBack < 2; ++walkBack)
    {
        Run r = Start(&plan, walkBack ? 1 : 0);
        if (walkBack)
        {
            r.walkBack = true;
            r.walkBackIdx = r.walkBackStart = 0;
            r.walkBackDirect = true;
        }
        Facts f = Party(0);
        f.maxMemberYd = 54.9f;
        f.members = {Mem("Tanky", 0), Mem("Walky", 54.9f)};
        CHECK(Tick(r, f) == "move 0.0 0.0 0.0");
        CHECK(r.phase == Phase::Gather);
        for (std::uint32_t ticks = 1; ticks < kGatherCapMs / kTickMs + 10; ++ticks)
        {
            float const yd = 54.9f - 0.6f * static_cast<float>(ticks);
            f.members[1] = Mem("Walky", yd);
            f.maxMemberYd = yd;
            std::string const order = Tick(r, f);
            CHECK(r.state == State::Running && r.phase == Phase::Gather);
            CHECK(order.find("summon") == std::string::npos);
        }
        CHECK(r.note.find("Walky is coming") != std::string::npos);
        // The same bot standing at 34 yd: no gain, summoned at the next ceiling.
        for (std::uint32_t i = 0; i < 2 * kGatherCapMs / kTickMs; ++i)
        {
            f.members[1] = Mem("Walky", 34.0f);
            f.members[1].x += (i % 2 ? 3.0f : -3.0f);
            f.maxMemberYd = 34.0f;
            std::string const order = Tick(r, f);
            if (order == "summon Walky")
                break;
            CHECK(r.state == State::Running);
        }
        CHECK(r.state == State::Running && r.summoned.count("Walky") == 1);
    }
}

// T5: a bot that comes, but too slowly, is waited for until kGatherComingMaxMs
// and then summoned once (it is far and gaining, which SummonStraggler leaves
// alone); the run pauses only if the summon does not bring it.
static void TestComingStragglerIsSummonedAfterTheComingLimit()
{
    DungeonPlan plan;
    plan.steps = {PullStep(100, {1})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.maxMemberYd = 150;
    f.members = {Mem("Tanky", 0), Mem("Slowpoke", 150)};
    f.units = {Unit(7, 1, 50)};
    CHECK(Tick(r, f) == "move 0.0 0.0 0.0");
    std::string order;
    std::uint32_t ticks = 1;
    for (; ticks < 200 && order.empty(); ++ticks)
    {
        float const yd = 150.0f - 0.5f * static_cast<float>(ticks);
        f.members[1] = Mem("Slowpoke", yd);
        f.maxMemberYd = yd;
        order = Tick(r, f);
        CHECK(r.state == State::Running);
    }
    CHECK(order == "summon Slowpoke");
    CHECK(ticks >= kGatherComingMaxMs / kTickMs - 5 && ticks <= kGatherComingMaxMs / kTickMs + 8);
    CHECK(r.phase == Phase::Gather && r.note.find("Slowpoke") != std::string::npos);
    // Once only: it is still far at the next ceiling (the summon was refused or did not bring it).
    for (std::uint32_t i = 0; i < kGatherCapMs / kTickMs + 2 && r.state == State::Running; ++i)
    {
        float const yd = 100.0f - 0.5f * static_cast<float>(i);
        f.members[1] = Mem("Slowpoke", yd);
        f.maxMemberYd = yd;
        CHECK(Tick(r, f).find("summon") == std::string::npos);
    }
    CHECK(r.state == State::Paused && r.note.find("did not gather") != std::string::npos);
}

// A bot that is not walking in is not waited for even if it is nearer than at
// the last look: one that stands still in a fight (busy, `cmd summon` refuses
// it) pauses the run as before.
static void TestStillBotInAFightIsNotWaitedFor()
{
    DungeonPlan plan;
    plan.steps = {PullStep(100, {1})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.maxMemberYd = 45;
    f.members = {Mem("Tanky", 0), Mem("Stoutster", 45)};
    f.members[1].inCombat = true;
    f.units = {Unit(7, 1, 50)};
    CHECK(Tick(r, f) == "move 0.0 0.0 0.0");
    f.members[1] = Mem("Stoutster", 38); // nearer by 7 yd, then it stops
    f.members[1].inCombat = true;
    f.maxMemberYd = 38;
    for (std::uint32_t i = 0; i < kGatherCapMs / kTickMs + 2 && r.state == State::Running; ++i)
        CHECK(Tick(r, f).find("summon") == std::string::npos);
    CHECK(r.state == State::Paused && r.note.find("did not gather") != std::string::npos);
}

// A bot far from the lead bot (past kSummonYd, or on another floor) that is in
// a fight, or has attackers on it, is never summoned by a gather's ceiling:
// the summon would drag the encounter to the lead bot or leave the bot alone
// in it, and `cmd summon` refuses a bot in combat. Walking or standing, it is
// not waited for as stuck either: the run pauses as before.
static void TestFarBusyBotIsNeverSummonedAtTheCeiling()
{
    for (int variant = 0; variant < 4; ++variant)
    {
        bool const inCombat = variant % 2 == 1; // attackers only, or in combat
        bool const otherFloor = variant >= 2;   // 60 yd out, or 10 yd away but 12 yd up
        DungeonPlan plan;
        plan.steps = {PullStep(100, {1})};
        Run r = Start(&plan);
        Facts f = Party(0);
        float const yd = otherFloor ? 25.0f : 60.0f;
        f.maxMemberYd = yd;
        f.members = {Mem("Tanky", 0), Mem("Busy", yd, otherFloor ? 12.0f : 0.0f)};
        f.units = {Unit(7, 1, 50)};
        CHECK(Tick(r, f) == "move 0.0 0.0 0.0");
        for (std::uint32_t i = 0; i < kGatherCapMs / kTickMs + 2 && r.state == State::Running; ++i)
        {
            f.members[1] = Mem("Busy", yd, otherFloor ? 12.0f : 0.0f);
            if (i % 2 == 0)
                f.members[1].x += 3.0f; // some ticks walking, then standing still for 6 s
            f.members[1].inCombat = inCombat;
            f.members[1].attackers = inCombat ? 0 : 2;
            CHECK(Tick(r, f).find("summon") == std::string::npos);
        }
        CHECK(r.state == State::Paused && r.note.find("did not gather") != std::string::npos);
        CHECK(r.summoned.empty());
    }
    // The same bot, free of the fight, is summoned (the exclusion is the fight).
    DungeonPlan plan;
    plan.steps = {PullStep(100, {1})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.maxMemberYd = 60;
    f.members = {Mem("Tanky", 0), Mem("Busy", 60)};
    f.units = {Unit(7, 1, 50)};
    CHECK(Tick(r, f) == "move 0.0 0.0 0.0");
    std::string order;
    for (std::uint32_t i = 0; i < kGatherCapMs / kTickMs + 2 && order.empty(); ++i)
        order = Tick(r, f);
    CHECK(order == "summon Busy");
}

// Live (events-5 01:48:43 -> 01:50:42): after the wipe the regroup's gather
// waited its whole ceiling (60 s, a bot 363 yd out) for a bot that had not moved
// at all; it is summoned after kStuckSummonMs instead.
static void TestRegroupSummonsAStuckFarBotEarly()
{
    DungeonPlan plan;
    plan.steps = {PullStep(100, {1})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.maxMemberYd = 120;
    f.members = {Mem("Tanky", 0), Mem("Stoutster", 120)};
    Pause(r, "test");
    CHECK(Resume(r) && r.regroup);
    CHECK(Tick(r, f) == "move 0.0 0.0 0.0"); // the regroup's gather
    std::string order;
    std::uint32_t ticks = 1;
    for (; ticks < 30 && order.empty(); ++ticks)
        order = Tick(r, f);
    CHECK(order == "summon Stoutster");
    CHECK(ticks >= kStuckSummonMs / kTickMs && ticks <= kStuckSummonMs / kTickMs + 2);
    CHECK(r.state == State::Running && r.regroup);
    CHECK(r.note.find("has not moved for 15 s") != std::string::npos && r.note.find("summoned") != std::string::npos);
}

// The 15 s are the gather's own clock. The stand-still watch runs from before
// the gather (it keeps counting while the dead are revived), so without this a
// bot that stood still 120 yd away during the revives was summoned one tick
// after the gather went out -- before anyone could have seen it walk.
static void TestStuckSummonCountsFromTheGather()
{
    DungeonPlan plan;
    plan.steps = {PullStep(100, {1})};
    Run r = Start(&plan);
    BeginRegroup(r);
    Facts f = Party(0);
    f.total = 5;
    f.alive = 4;
    f.dead = 1;
    f.deadNames = {"Quelinator"};
    f.maxMemberYd = 120;
    f.members = {Mem("Tanky", 0), Mem("Stoutster", 120)};
    // The corpse run: 20 ticks of revive before the dead are up. The stand-still
    // watch of Stoutster runs all through them.
    for (int i = 0; i < 20; ++i)
        Tick(r, f);
    CHECK(r.still["Stoutster"].ms >= kStuckSummonMs);
    f.dead = 0;
    f.alive = 5;
    f.deadNames.clear();
    CHECK(Tick(r, f) == "move 0.0 0.0 0.0"); // the regroup's gather: its clock starts
    CHECK(Tick(r, f).empty());               // NOT summoned yet
    std::string order;
    std::uint32_t ticks = 0;
    for (; ticks < 30 && order.empty(); ++ticks)
        order = Tick(r, f);
    CHECK(order == "summon Stoutster");
    CHECK(ticks >= kStuckSummonMs / kTickMs - 1 && ticks <= kStuckSummonMs / kTickMs + 1);
    CHECK(r.note.find("has not moved for 15 s") != std::string::npos);
}

// Codex on #1000: a walk-back point refused for the lead bot's state of the
// moment is sent again (3 times, then pause); only a path refusal passes it.
static void TestWalkBackRefusalKinds()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 10), Step(PlanStepKind::Move, 50), PullStep(130, {1})};
    Run r = Start(&plan, 2);
    r.walkBack = true;
    r.walkBackIdx = 0;
    r.gatherWaived = true;
    r.gatherRefusals = 2;
    Facts f = Party(0);
    CHECK(Tick(r, f) == "move 10.0 0.0 0.0");
    for (std::uint32_t i = 1; i <= kWalkBackRetries; ++i)
    {
        OrderRefused(r, "move 10.0 0.0 0.0", "lead-unavailable: The lead bot is dead, on a taxi or teleporting.");
        CHECK(r.state == State::Running && r.walkBackIdx == 0 && r.note.find("again") != std::string::npos);
        CHECK(Tick(r, f) == "move 10.0 0.0 0.0");
    }
    OrderRefused(r, "move 10.0 0.0 0.0", "busy: a task runs");
    CHECK(r.state == State::Paused && r.walkBackIdx == 0);

    Run p = Start(&plan, 2);
    p.walkBack = true;
    p.gatherWaived = true;
    p.gatherRefusals = 2;
    CHECK(Tick(p, f) == "move 10.0 0.0 0.0");
    OrderRefused(p, "move 10.0 0.0 0.0", "lead-unavailable: teleporting");
    OrderRefused(p, "move 10.0 0.0 0.0", "off-mesh: The point is not on walkable ground.");
    // Passed: the next point, its own refusal count and gather waiver fresh.
    CHECK(p.state == State::Running && p.walkBackIdx == 1 && p.walkBackRefusals == 0);
    CHECK(!p.gatherWaived && p.gatherRefusals == 0);
    CHECK(Tick(p, f) == "move 50.0 0.0 0.0");
}

// Cold review on #1000: a pullback drags the boss to the party, so his
// corpse 30 yd from his spot is normal (radius + kSeeYd); and a boss seen
// alive in this step and now dead near the lead bot is down wherever he fell.
static void TestBossKilledAwayFromHisSpot()
{
    DungeonPlan plan;
    PlanStep boss = Step(PlanStepKind::Boss, 30);
    boss.entry = 6910;             // Uldaman: Revelosh, his adds first
    boss.entries = {7076, 7077};
    ai::NormalizePlanStep(boss);
    plan.steps = {boss, Step(PlanStepKind::Move, 60)};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.units = {Unit(5, 6910, 30)};
    CHECK(Tick(r, f) == "pullback 5");
    CHECK(r.bossSeenAlive);
    f.anyInCombat = true;
    Tick(r, f);
    f.anyInCombat = false;
    f.units.clear();
    f.bossDeadNearSpot = true; // the glue: his corpse 30 yd from the spot, within radius + kSeeYd
    Tick(r, f);
    CHECK(r.step == 1 && r.note == "boss down");

    // Fell far from his spot but near the lead bot: down, because he was seen alive.
    Run n = Start(&plan);
    Facts g = Party(0);
    g.units = {Unit(5, 6910, 30)};
    CHECK(Tick(n, g) == "pullback 5");
    g.anyInCombat = true;
    Tick(n, g);
    g.anyInCombat = false;
    g.units.clear();
    g.bossDeadNearLead = true;
    Tick(n, g);
    CHECK(n.step == 1 && n.note == "boss down");
    // Never seen alive this step: a corpse near the lead bot alone is not enough.
    Run m = Start(&plan);
    Facts h = Party(0);
    h.bossDeadNearLead = true;
    Tick(m, h);
    CHECK(m.step == 0 && m.note.find("not at his spot") != std::string::npos);
}

// Cold review on #1000: after a wipe the party stands at the entrance and the
// nearest walk-back point is more than ReactDistance away: it is approached in
// navmesh hops (as a resume does), not sent as one refused move; with no path
// toward it at all it is passed. Walk-back pulls have their own count.
static void TestWalkBackFarPointIsApproached()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 200), Step(PlanStepKind::Move, 230), PullStep(400, {1})};
    Run r = Start(&plan, 2);
    r.walkBack = true;
    Facts f = Party(0);
    f.seen = false; // FactStep: the walk-back point, 200 yd away
    f.approachKnown = true;
    f.approachX = 40;
    CHECK(Tick(r, f) == "move 40.0 0.0 0.0");
    CHECK(r.note.find("walk back: approaching") == 0);
    f.leadX = 40;
    CHECK(Tick(r, f).empty()); // at the hop: the next one next tick
    f.approachX = 80;
    CHECK(Tick(r, f) == "move 80.0 0.0 0.0");
    f.leadX = 80;
    Tick(r, f);
    f.seen = true;
    CHECK(Tick(r, f) == "move 200.0 0.0 0.0");
    // A hostile on the way: pulled, counted apart from the step's pulls.
    f.hopUnits = {Unit(9, 3, 20)};
    CHECK(Tick(r, f) == "pullback 9");
    CHECK(r.pulls == 0 && r.walkBackPulls == 1);

    // No path toward a far point: passed for the next one.
    Run p = Start(&plan, 2);
    p.walkBack = true;
    Facts g = Party(0);
    g.seen = false;
    g.approachKnown = false;
    CHECK(Tick(p, g).empty());
    CHECK(p.state == State::Running && p.walkBackIdx == 1 && p.note.find("no walkable path") != std::string::npos);
}

// Codex on #1000 / 19z5 step 57: only an invalid-target refusal of a pullback
// leaves the unit out (for the run, no pause); a busy tank or an unavailable
// lead bot pauses as before.
static void TestRefusedPullOnlyForInvalidTarget()
{
    DungeonPlan plan;
    plan.steps = {PullStep(10, {1})};
    Run r = Start(&plan);
    OrderRefused(r, "pullback 7", "busy: the tank is pulling");
    CHECK(r.state == State::Paused && !r.unreachable.count(7));
    Run l = Start(&plan);
    OrderRefused(l, "pullback 7", "lead-unavailable: The lead bot is dead");
    CHECK(l.state == State::Paused && !l.unreachable.count(7));
    Run i = Start(&plan);
    OrderRefused(i, "pullback 7", "invalid-target: The group cannot attack that target.");
    CHECK(i.state == State::Running && i.unreachable.count(7) && i.note.find("left out") != std::string::npos);
}

// Codex on #1001: the dead lead bot already released to the graveyard map;
// the glue lets the tick run (its map guard pauses only a living lead bot off
// the map), and the wipe starts the regroup.
static void TestWipeWithTheLeadOnTheGraveyardMap()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 10), PullStep(130, {1})};
    Run r = Start(&plan, 1);
    Facts f = Party(-9000); // the lead's ghost far away on another map
    f.dead = 5;
    f.alive = 0;
    f.leadAlive = false;
    f.maxMemberYd = 0;
    f.seen = false;
    CHECK(Tick(r, f).empty());
    CHECK(r.state == State::Running && r.regroup && r.wipes == 1);
    CHECK(Tick(r, f) == "revive party");
}

// Codex on #1000: a near wipe that becomes a wipe, then the revive one by one
// (4 dead, 3 dead with someone up again), is one near wipe and one wipe.
static void TestNearWipeLatchHoldsThroughTheWipe()
{
    DungeonPlan plan;
    plan.steps = {PullStep(10, {1})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.anyInCombat = true;
    f.dead = 3;
    f.alive = 2;
    Tick(r, f);
    CHECK(r.nearWipes == 1 && r.wipes == 0);
    f.anyInCombat = false;
    f.dead = 5;
    f.alive = 0;
    f.leadAlive = false;
    Tick(r, f);
    CHECK(r.nearWipes == 1 && r.wipes == 1);
    for (std::uint32_t dead : {4u, 3u})
    {
        f.dead = dead;
        f.alive = 5 - dead;
        Tick(r, f);
        CHECK(r.nearWipes == 1 && r.wipes == 1);
    }
    // All up again: a later near wipe counts anew.
    f = Party(0);
    f.units = {Unit(7, 1, 20)};
    Tick(r, f);
    CHECK(r.state == State::Running);
    f.anyInCombat = true;
    f.dead = 3;
    f.alive = 2;
    Tick(r, f);
    CHECK(r.nearWipes == 2 && r.wipes == 1);
}

// 19z5 steps 27 and 81: the lead bot died first; the run paused "the lead bot
// is dead" and the rest died while it waited. Now the others fight on; when
// they fall too it is a wipe and the run regroups by itself. A dead lead bot
// alone after the fight still pauses.
static void TestLeadDiesFirstThenTheWipeRegroups()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 10), PullStep(130, {1})};
    Run r = Start(&plan, 1);
    Facts f = Party(130);
    f.units = {Unit(7, 1, 10)};
    f.anyInCombat = true;
    f.leadAlive = false;
    f.dead = 1;
    f.alive = 4;
    Tick(r, f);
    CHECK(r.state == State::Running && r.note.find("the others fight on") != std::string::npos);
    f.dead = 5;
    f.alive = 0;
    f.anyInCombat = false;
    Tick(r, f);
    CHECK(r.state == State::Running && r.regroup && r.wipes == 1);

    Run p = Start(&plan, 1);
    Facts g = Party(130);
    g.units = {Unit(7, 1, 10)};
    g.leadAlive = false;
    g.dead = 1;
    g.alive = 4;
    Tick(p, g);
    CHECK(p.state == State::Running && p.regroup && p.note.find("the lead bot is dead") != std::string::npos);
}

// 19z5 step 80 (15:43): 4 dead and the tank alone past the fight ceiling
// paused as "a fight lasted more than 180 s". It is a wipe: the run regroups
// by itself, within the step's budget.
static void TestFightCeilingWithFourDeadRegroups()
{
    DungeonPlan plan;
    plan.steps = {PullStep(10, {1})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.anyInCombat = true;
    f.dead = 4;
    f.alive = 1;
    for (std::uint32_t i = 0; i <= kFightCapMs / kTickMs + 1 && !r.regroup; ++i)
        Tick(r, f);
    CHECK(r.state == State::Running && r.regroup && r.autoRegroups == 1 && r.note.find("went past") != std::string::npos);
    // Over budget: the ceiling pauses as before.
    Run p = Start(&plan);
    p.autoRegroups = kAutoRegroupsPerStep;
    for (std::uint32_t i = 0; i <= kFightCapMs / kTickMs + 1 && p.state == State::Running; ++i)
        Tick(p, f);
    CHECK(p.state == State::Paused);
}

// 19z5 step 83: Draezur 29 yd away but on another floor (z 21 against 43);
// the gather paused twice. A bot more than 8 yd up or down that comes no
// closer is a straggler too.
static void TestStragglerOnAnotherFloorIsSummoned()
{
    DungeonPlan plan;
    plan.steps = {PullStep(10, {1})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.maxMemberYd = 29.1f;
    MemberFact healer{"Draezur", 29.1f};
    healer.dz = 22;
    MemberFact near{"Nearby", 25};
    near.dz = 2;
    f.members = {near, healer};
    f.units = {Unit(7, 1, 20)};
    CHECK(Tick(r, f) == "move 0.0 0.0 0.0");
    std::string order;
    for (std::uint32_t i = 0; i < kGatherCapMs / kTickMs + 2 && order.empty(); ++i)
        order = Tick(r, f);
    CHECK(order == "summon Draezur");
}

// 19z5 step 27 and 81: after a revive by hand and `.bot run resume` the party
// stood at the entrance and the step's approach paused (no path / 8 hops).
// A resume walks back too: straight to the last plan point, in hops.
static void TestResumeFromTheEntranceWalksBack()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 10), Step(PlanStepKind::Move, 300), PullStep(330, {1})};
    Run r = Start(&plan, 2);
    Pause(r, "the lead bot is dead: revive it, then .bot run resume");
    CHECK(Resume(r));
    Facts f = Party(0);
    CHECK(Tick(r, f).empty()); // regrouped
    CHECK(r.walkBack && r.walkBackDirect && r.walkBackIdx == 1);
    f.seen = false;
    f.approachKnown = true;
    f.approachX = 40;
    CHECK(Tick(r, f) == "move 40.0 0.0 0.0");
}

// 19z5 steps 83-84: the approach hop left the party at about 1622 1051 z 18;
// the tank chased over a ridge to 1625 1075 z 44 in the fight, and the next
// gather (on the lead's own spot) pinned the party on the hilltop, where no
// path led down. A gather now goes back to the run's anchor (the last point
// the run walked to) when the lead bot is off it, and gathers there.
static void TestGatherGoesBackToTheAnchorAfterARidgeChase()
{
    DungeonPlan plan;
    plan.steps = {PullStep(1639.4f, {5649, 5650})};
    plan.steps[0].y = 1106.8f;
    plan.steps[0].z = 9.0f;
    Run r = Start(&plan);
    Facts f = Party(0);
    f.leadX = 1584; f.leadY = 1082; f.leadZ = 10;
    f.seen = false;
    f.approachKnown = true;
    f.approachX = 1622; f.approachY = 1051; f.approachZ = 18;
    CHECK(Tick(r, f) == "move 1622.0 1051.0 18.0");
    f.leadX = 1622; f.leadY = 1051; f.leadZ = 18;
    Tick(r, f); // at the hop: the anchor
    CHECK(r.anchorKnown && r.anchorZ == 18.0f);
    // In sight; the fight drags the tank up the ridge.
    f.seen = true;
    f.units = {Unit(1, 5649, 25)};
    CHECK(Tick(r, f) == "pullback 1");
    f.anyInCombat = true;
    f.leadX = 1625; f.leadY = 1075; f.leadZ = 44;
    Tick(r, f);
    f.anyInCombat = false;
    f.units = {Unit(2, 5650, 30)};
    f.maxMemberYd = 29.1f;
    // The gather walks the lead bot back down to the anchor, not up the hill.
    CHECK(Tick(r, f) == "move 1622.0 1051.0 18.0");
    CHECK(r.phase == Phase::Gather && r.note.find("gathering back at 1622.0 1051.0 18.0") == 0);
    // Even with the party close, no pull while the lead bot is still up there.
    f.maxMemberYd = 5;
    CHECK(Tick(r, f).empty());
    // Back at the anchor and gathered: the pull goes out.
    f.leadX = 1622; f.leadY = 1052; f.leadZ = 18;
    CHECK(Tick(r, f) == "pullback 2");

    // Same floor but no navmesh path back (a pocket): back to the anchor too.
    Run p = Start(&plan);
    p.anchorKnown = true;
    p.anchorX = 1622; p.anchorY = 1051; p.anchorZ = 18;
    Facts g = Party(0);
    g.leadX = 1627; g.leadY = 1078; g.leadZ = 22;
    g.units = {Unit(2, 5650, 30)};
    // The path back is read lazily: asked for on the first tick, no pull yet.
    CHECK(Tick(p, g).empty() && p.anchorPathWanted && p.pulls == 0 && p.fruitlessPulls == 0);
    g.anchorPathRead = true;
    g.anchorReachable = false;
    g.units = {Unit(2, 5650, 30)};
    CHECK(Tick(p, g) == "move 1622.0 1051.0 18.0");
    // On the anchor's floor with a path back: the gather stays on the lead's spot.
    Run q = Start(&plan);
    q.anchorKnown = true;
    q.anchorX = 1622; q.anchorY = 1051; q.anchorZ = 18;
    Facts h = Party(0);
    h.leadX = 1630; h.leadY = 1060; h.leadZ = 20;
    h.anchorPathRead = true;
    h.maxMemberYd = 30;
    h.units = {Unit(2, 5650, 30)};
    CHECK(Tick(q, h) == "move 1630.0 1060.0 20.0");
}

// Codex on #1000: the shortest navmesh path back ignores packs. A direct walk
// back whose path passes within a not-yet-cleared pull group's radius + 10 yd
// drops the shortcut and replays the plan's points; no move enters the pack.
static void TestDirectWalkBackAvoidsUnclearedPacks()
{
    DungeonPlan plan;
    PlanStep later = PullStep(60, {9});
    later.y = 20;   // a later pack, 20 yd off the straight line
    ai::NormalizePlanStep(later);
    plan.steps = {Step(PlanStepKind::Move, 30), Step(PlanStepKind::Move, 120), PullStep(150, {1}), later};
    // Geometry: the straight path from 0 to 120 passes 20 yd from the later
    // pack (radius 15 + 10); a path that bends away does not.
    std::vector<PathPoint> straight = {{0, 0, 0}, {120, 0, 0}};
    std::vector<PathPoint> around = {{0, 0, 0}, {60, -30, 0}, {120, 0, 0}};
    CHECK(PathCrossesUnclearedPack(plan, 2, straight, 0));
    CHECK(!PathCrossesUnclearedPack(plan, 2, around, 0));
    CHECK(!PathCrossesUnclearedPack(plan, 4, straight, 0)); // that pack is cleared (an earlier step)

    Run r = Start(&plan, 2);
    Facts f = Party(0);
    r.walkBack = true;
    r.walkBackDirect = true;
    r.walkBackIdx = 1;
    f.seen = false;
    f.approachKnown = true;
    f.approachX = 40;
    f.approachY = 0;
    f.directPathCrossesPack = true;
    std::string order = Tick(r, f);
    CHECK(order.empty() && !r.walkBackDirect && r.walkBack && r.walkBackIdx == 0);
    CHECK(r.note.find("passes an uncleared pack") != std::string::npos);
    // The replay walks the plan's first point (30 0 0), not the hop toward the pack.
    f.seen = true;
    f.directPathCrossesPack = false;
    CHECK(Tick(r, f) == "move 30.0 0.0 0.0");
}

// Cold review on #1000: a stalled arrival short of the point (off the mesh)
// anchors where the lead bot stands, not at the unreachable point; skipped
// packs count in the walk back's pack check, the path's last leg too.
static void TestStalledAnchorAndSkippedPacks()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 40)};
    Run r = Start(&plan);
    Facts f = Party(0);
    CHECK(Tick(r, f) == "move 40.0 0.0 0.0");
    f.leadX = 35; // 5 yd short, stuck
    for (std::uint32_t i = 0; i <= kWalkRetryMs / kTickMs + 1 && r.step == 0; ++i)
        Tick(r, f);
    CHECK(r.step == 1 && r.anchorKnown && r.anchorX == 35.0f);

    DungeonPlan p;
    PlanStep skipped = PullStep(60, {9});
    skipped.y = 20;
    ai::NormalizePlanStep(skipped);
    PlanStep atEnd = PullStep(120, {8});
    ai::NormalizePlanStep(atEnd);
    p.steps = {skipped, Step(PlanStepKind::Move, 120), atEnd, Step(PlanStepKind::Move, 200)};
    std::vector<PathPoint> straight = {{0, 0, 0}, {119, 0, 0}};
    std::unordered_set<std::size_t> notCleared;
    CHECK(!PathCrossesUnclearedPack(p, 3, straight, 0, notCleared)); // both groups before step 3, cleared
    notCleared.insert(0);
    CHECK(PathCrossesUnclearedPack(p, 3, straight, 0, notCleared)); // the skipped one counts
    CHECK(PathCrossesUnclearedPack(p, 2, straight, 0));             // a group by the path's end counts too
}

// Issue #999 item 5 (Zul'Farrak): the Sandfury Witch Doctor 5650 heals its
// pack and drops a Greater Healing Ward 8179. Before the pull the mage
// polymorphs the best-ranked cc unit (never the pull target); the pull goes
// on a kill_first unit; in the fight the kill_first units are focused, the
// ward first as soon as it is up.
static PlanStep ZfPack()
{
    return PullStep(10, {5650, 5648, 7247});
}

static DungeonPlan ZfPlan()
{
    DungeonPlan plan;
    plan.steps = {ZfPack()};
    plan.ccPriority = {5650, 7247, 7246, 5648};
    plan.ccAlways = {5650, 7246};
    plan.killFirst = {8179, 5650, 7247};
    return plan;
}

static UnitFact Engaged(UnitFact u)
{
    u.engaged = true;
    return u;
}

// A Greater Healing Ward 8179 of `owner`; ownerEngaged: its owner fights the party.
static UnitFact Ward(std::uint64_t guid, std::uint64_t owner, bool ownerEngaged, float dist = 18)
{
    UnitFact u = Unit(guid, 8179, dist);
    u.owner = owner;
    u.ownerEngaged = ownerEngaged;
    return u;
}

// The gather left out for a pull after a hop was abandoned for a close unit
// holds for that pull only: combat that starts during the pre-pull crowd
// control ends it, and the next pull gathers (and goes back to the anchor).
static void TestHopWaiverEndsWhenCombatStartsDuringCrowdControl()
{
    DungeonPlan plan = ZfPlan();
    Run r = Start(&plan);
    r.restDue = false;
    r.anchorKnown = true; // the stand before, 20 yd below the lead bot
    r.anchorX = 0; r.anchorY = 0; r.anchorZ = -20;
    EnterPhase(r, Phase::Walk); // an approach hop the pack came in sight during
    r.hopWalk = true;
    r.hopX = r.walkX = 5; r.hopY = r.walkY = 0; r.hopZ = r.walkZ = 0;
    Facts f = Party(0);
    f.members = {MemberFact{"Magey", 5, true, true}};
    f.maxMemberYd = 40; // the followers are behind
    f.units = {Unit(1, 5648, 10), Unit(2, 5650, 20), Unit(3, 7247, 25)};
    // The hop is over, no gather: the crowd control goes first.
    CHECK(Tick(r, f) == "cc moon 2 Magey");
    CHECK(r.gatherWaived && !r.hopWaiveUnits.empty());
    // Combat starts before the pullback.
    f.anyInCombat = true;
    Tick(r, f);
    CHECK(!r.gatherWaived && r.hopWaiveUnits.empty());
    // The fight is over; the next pull gathers first, back at the anchor.
    f.anyInCombat = false;
    std::string order;
    for (int i = 0; i < 40 && order.empty() && r.state == State::Running; ++i)
        order = Tick(r, f);
    CHECK(order.compare(0, 5, "move ") == 0);
    CHECK(r.note.find("gathering") == 0);
}

// Uldaman's cc lists (20261021174717_world.sql): cc_priority 4853,4848,7321,7030,7012,7397. Grimlok's boss row
// (4854, adds 4855,4853,4863) and Galgann's (7291, add 7030) name listed units as the adds their kill order takes
// first: the run never crowd-controls the boss or one of those adds; another listed unit in the fight still can
// be, and a pull step (the cascades) keeps its crowd control.
static void TestBossStepNeverCrowdControlsItsAdds()
{
    auto boss = [](std::uint32_t entry, std::vector<std::uint32_t> adds) {
        PlanStep s;
        s.kind = PlanStepKind::Boss;
        s.entry = entry;
        s.entries = adds;
        ai::NormalizePlanStep(s);
        return s;
    };
    DungeonPlan plan;
    plan.ccPriority = {4853, 4848, 7321, 7030, 7012, 7397};
    plan.ccAlways = {4848, 7321};
    plan.steps = {boss(4854, {4855, 4853, 4863}), boss(7291, {7030}), PullStep(10, {4853, 4855})};
    Facts f = Party(0);
    f.members = {MemberFact{"Magey", 5, true, true}};
    // Grimlok: the Geomancer 4853 is ranked first, but it is his add.
    f.units = {Unit(1, 4854, 12), Unit(2, 4853, 15), Unit(3, 4855, 10), Unit(4, 4863, 14)};
    Run g = Start(&plan, 0);
    CHECK(PickCc(g, f, 0) == nullptr);
    CHECK(PickCc(g, f, 3) == nullptr);
    CHECK(BossStepUnit(Current(g), 4853) && BossStepUnit(Current(g), 4854) && !BossStepUnit(Current(g), 7321));
    // A listed unit that is not his (a wanderer that joined) is still taken.
    f.units.push_back(Unit(5, 7321, 20));
    const UnitFact* cc = PickCc(g, f, 0);
    CHECK(cc && cc->guid == 5);
    // Galgann: his 7030 is never sheeped.
    Run a = Start(&plan, 1);
    f.units = {Unit(1, 7291, 12), Unit(2, 7030, 15)};
    CHECK(PickCc(a, f, 0) == nullptr);
    // A pull step with the same units keeps its crowd control.
    Run p = Start(&plan, 2);
    f.units = {Unit(1, 4855, 10), Unit(2, 4853, 15)};
    cc = PickCc(p, f, 1);
    CHECK(cc && cc->guid == 2);
    CHECK(!BossStepUnit(Current(p), 4853) && !BossStepUnit(nullptr, 4853));
}

static void TestPullCrowdControlsTheHealer()
{
    DungeonPlan plan = ZfPlan();
    Run r = Start(&plan);
    Facts f = Party(0);
    f.members = {MemberFact{"Magey", 5, true, true}};
    f.units = {Unit(1, 5648, 10), Unit(2, 5650, 20), Unit(3, 7247, 25)};
    // The witch doctor is the best cc rank: it is sheeped, not pulled.
    CHECK(Tick(r, f) == "cc moon 2 Magey");
    CHECK(r.note.find("crowd control on 5650") == 0 && r.state == State::Running);
    // The pull goes on the soul eater (kill_first) rather than the nearest.
    CHECK(Tick(r, f) == "pullback 3");
    CHECK(r.pulls == 1 && r.fruitlessPulls == 0);
    // In the fight: the soul eater is focused; the sheeped witch doctor is not.
    f.anyInCombat = true;
    f.killFirst = {Engaged(Unit(2, 5650, 20)), Engaged(Unit(3, 7247, 5))};
    CHECK(Tick(r, f) == "focus 3");
    CHECK(Tick(r, f).empty()); // only on a change
    // A ward comes up: it is focused first.
    f.killFirst.push_back(Ward(9, 3, true, 12)); // a pull step: within kKillFirstMaxYd
    CHECK(Tick(r, f) == "focus 9");
    // The ward dies: back on the soul eater.
    f.killFirst.pop_back();
    CHECK(Tick(r, f) == "focus 3");
    // After the fight: the sheeped witch doctor is left while another can be
    // pulled, and no second cc goes out while it lives.
    f.anyInCombat = false;
    f.killFirst.clear();
    f.units = {Unit(1, 5648, 10), Unit(2, 5650, 20)};
    CHECK(Tick(r, f) == "pullback 1");
    f.anyInCombat = true;
    Tick(r, f);
    f.anyInCombat = false;
    f.units = {Unit(2, 5650, 20)};
    CHECK(Tick(r, f) == "pullback 2"); // the last one, sheeped or not
    // A refused cc or focus does not pause.
    OrderRefused(r, "cc moon 2 Magey", "no-bot");
    OrderRefused(r, "focus 3", "invalid-target");
    CHECK(r.state == State::Running);
}

static void TestNoCrowdControlWithoutAMageOrPlanList()
{
    DungeonPlan plan = ZfPlan();
    Run r = Start(&plan);
    Facts f = Party(0);
    f.units = {Unit(1, 5648, 10), Unit(2, 5650, 20)};
    // No mage: no cc; the pull still prefers kill_first.
    CHECK(Tick(r, f) == "pullback 2");

    DungeonPlan bare;
    bare.steps = {ZfPack()};
    Run b = Start(&bare);
    Facts g = Party(0);
    g.members = {MemberFact{"Magey", 5, true, true}};
    g.units = {Unit(1, 5648, 10), Unit(2, 5650, 20)};
    CHECK(Tick(b, g) == "pullback 1"); // the nearest, as before
    g.anyInCombat = true;
    g.killFirst = {Engaged(Unit(2, 5650, 20))};
    CHECK(Tick(b, g).empty());

    // A lone cc unit is pulled, not sheeped.
    Run c = Start(&plan);
    Facts h = Party(0);
    h.members = {MemberFact{"Magey", 5, true, true}};
    h.units = {Unit(2, 5650, 20)};
    CHECK(Tick(c, h) == "pullback 2");
}

// Codex on #1001: the Polymorph is pending until its aura shows on that unit
// in the fight (the mage's cc strategy is a combat one): not within
// kCcLandMs, the mark once more; still not, noted, and the fight goes on.
static void TestCrowdControlMustLand()
{
    DungeonPlan plan = ZfPlan();
    plan.killFirst.clear();
    Run r = Start(&plan);
    Facts f = Party(0);
    f.members = {MemberFact{"Tanky", 0, false}, MemberFact{"Magey", 5, true, true}};
    f.units = {Unit(1, 5648, 10), Unit(2, 5650, 20)};
    CHECK(Tick(r, f) == "cc moon 2 Magey");
    CHECK(r.ccPending == 2);
    CHECK(Tick(r, f) == "pullback 1"); // the mark is out; the pull starts the fight
    f.anyInCombat = true;
    CHECK(Tick(r, f).empty() && r.ccPending == 2);
    f.units[1].polymorphed = true; // landed
    CHECK(Tick(r, f).empty());
    CHECK(r.ccPending == 0 && r.note.find("polymorphed") != std::string::npos);

    // Never lands: once more after kCcLandMs, then noted.
    Run n = Start(&plan);
    Facts g = Party(0);
    g.members = {MemberFact{"Magey", 5, true, true}};
    g.units = {Unit(1, 5648, 10), Unit(2, 5650, 20)};
    CHECK(Tick(n, g) == "cc moon 2 Magey");
    CHECK(Tick(n, g) == "pullback 1");
    g.anyInCombat = true;
    std::string order;
    for (std::uint32_t i = 0; i <= kCcLandMs / kTickMs && order.empty(); ++i)
        order = Tick(n, g);
    CHECK(order == "cc moon 2 Magey" && n.ccResent && n.note.find("once more") != std::string::npos);
    order.clear();
    for (std::uint32_t i = 0; i <= kCcLandMs / kTickMs && n.ccPending; ++i)
        order += Tick(n, g);
    CHECK(order.empty() && n.ccPending == 0 && n.note.find("cc did not land on 5650") != std::string::npos);
    CHECK(n.state == State::Running);

    // A mage that does not know Polymorph is not the crowd control.
    Run m = Start(&plan);
    Facts h = Party(0);
    h.members = {MemberFact{"Lowmage", 5, false}};
    h.units = {Unit(1, 5648, 10), Unit(2, 5650, 20)};
    CHECK(Tick(m, h) == "pullback 1" && m.ccSent.empty());
}

// Codex on #1001: kill_first is this fight's only. A ward whose owner fights
// the party is focused; a ward of an unrelated witch doctor nearby is not,
// nor a kill_first unit that is not engaged.
static void TestKillFirstIsThisFightsOnly()
{
    DungeonPlan plan = ZfPlan();
    Run r = Start(&plan);
    Facts f = Party(0);
    f.units = {Unit(1, 5648, 10)};
    CHECK(Tick(r, f) == "pullback 1");
    f.anyInCombat = true;
    f.killFirst = {Ward(20, 30, false), Unit(30, 5650, 25)}; // unrelated ward and its idle owner
    CHECK(Tick(r, f).empty());
    f.killFirst.push_back(Ward(21, 1, true, 12)); // the ward of a unit in this fight (a pull step: within kKillFirstMaxYd)
    CHECK(Tick(r, f) == "focus 21");
}

// A mob hitting the healer `dist` from the lead bot (Hitter is defined further down).
static UnitFact ZfHitter(std::uint64_t guid, std::uint32_t entry, float dist)
{
    UnitFact u = Unit(guid, entry, dist);
    u.hitting = true;
    u.victimHealer = true;
    return u;
}

// ZF r5-19z23 step 27, wipe 1 (15:10:58): the tank stood 20 yd from the
// antusul camp's 5650 when two witch doctors aggroed on it; "kill 5650 first"
// sent the tank and the party into the camp, next to the north group, and
// 13 mobs joined at once. A kill_first unit more than kKillFirstMaxYd from the
// lead bot is not focused (it is left to come to the tank, as in r3); within
// it, it is. The fight goes on with the bots' own picks in between.
static void TestKillFirstFarIsNotFocusedWipe1Replay()
{
    DungeonPlan plan = ZfPlan();
    Run r = Start(&plan);
    Facts f = Party(0);
    f.leadIsTank = true;
    f.leadStaying = true; // on firing stand 1
    f.units = {Unit(1, 5648, 24.5f)};
    CHECK(Tick(r, f) == "pullback 1");
    f.anyInCombat = true;
    // The camp's two witch doctors, engaged with the tank from 20 and 23 yd.
    f.killFirst = {Engaged(Unit(30, 5650, 20)), Engaged(Unit(31, 5650, 23))};
    for (int i = 0; i < 20; ++i)
        CHECK(Tick(r, f).empty()); // no focus: the tank stays on its stand
    CHECK(r.focusGuid == 0 && r.state == State::Running);
    // One walks in to the tank: focused now.
    f.killFirst[0].distLead = kKillFirstMaxYd - 1.0f;
    CHECK(Tick(r, f) == "focus 30");
    // At exactly the bound it counts as near; just past it, not.
    Run b = Start(&plan);
    Facts g = Party(0);
    g.units = {Unit(1, 5648, 10)};
    CHECK(Tick(b, g) == "pullback 1");
    g.anyInCombat = true;
    g.killFirst = {Engaged(Unit(40, 5650, kKillFirstMaxYd + 0.5f))};
    CHECK(Tick(b, g).empty());
    g.killFirst[0].distLead = kKillFirstMaxYd;
    CHECK(Tick(b, g) == "focus 40");
}

// The bound picks a near unit over a far better-ranked one, and a focus once
// sent stays while its unit lives: a better-ranked unit that walks back and
// forth across kKillFirstMaxYd must not flip the focus every tick.
static void TestKillFirstBoundDoesNotFlipFlop()
{
    DungeonPlan plan = ZfPlan(); // kill_first 8179, 5650, 7247
    Run r = Start(&plan);
    Facts f = Party(0);
    f.units = {Unit(1, 5648, 10)};
    CHECK(Tick(r, f) == "pullback 1");
    f.anyInCombat = true;
    // A 5650 (rank 1) far, a 7247 (rank 2) near: the near one.
    f.killFirst = {Engaged(Unit(30, 5650, 30)), Engaged(Unit(3, 7247, 5))};
    CHECK(Tick(r, f) == "focus 3");
    int focuses = 0;
    for (int i = 0; i < 40; ++i)
    {
        // The 5650 drifts in and out across the bound.
        f.killFirst[0].distLead = (i % 2) ? kKillFirstMaxYd - 2.0f : kKillFirstMaxYd + 2.0f;
        if (Tick(r, f).compare(0, 6, "focus ") == 0)
            ++focuses;
    }
    // One switch to the better rank once it came in, then it holds.
    CHECK(focuses == 1 && r.focusGuid == 30);
    // The focused 5650 steps out of the bound again: still the focus.
    f.killFirst[0].distLead = 30;
    CHECK(Tick(r, f).empty() && r.focusGuid == 30);
}

// A focus is never dropped for distance: no order takes it back (the skull and
// every bot's attack stay), so the policy must not think it over while the
// party still chases. A focused unit that walks back toward its camp keeps
// the focus (the known risk) and the guard keeps watching; it ends only when
// the unit is gone: dead (out of the list) or crowd-controlled.
static void TestHeldFocusEndsOnlyWhenItsUnitIsGone()
{
    DungeonPlan plan = ZfPlan();
    Run r = Start(&plan);
    Facts f = Party(0);
    f.leadIsTank = true;
    f.units = {Unit(1, 5648, 10)};
    CHECK(Tick(r, f) == "pullback 1");
    f.anyInCombat = true;
    f.killFirst = {Engaged(Unit(30, 5650, 12))};
    CHECK(Tick(r, f) == "focus 30");
    f.leadStaying = false;
    f.killFirst[0].distLead = 40; // back toward its camp
    for (int i = 0; i < 10; ++i)
        CHECK(Tick(r, f).empty() && r.focusGuid == 30);
    // Alive but not pickable (evading): still held, the guard still watches.
    f.killFirst[0].evading = true;
    CHECK(Tick(r, f).empty() && r.focusGuid == 30);
    f.partyAttackers = {ZfHitter(50, 5649, 8)};
    CHECK(Tick(r, f) == "attack 50 Tanky" && r.guardOrders == 1);
    // Pick-ups stay available under the cap and the spacing.
    f.partyAttackers = {ZfHitter(51, 5649, 6)};
    int more = 0;
    for (int i = 0; i < 100; ++i)
        if (Tick(r, f).compare(0, 7, "attack ") == 0)
            ++more;
    CHECK(more == static_cast<int>(kGuardOrdersPerFight) - 1);
    // Crowd-controlled by the run: gone, the focus ends.
    Run c = Start(&plan);
    Facts g = Party(0);
    g.units = {Unit(1, 5648, 10)};
    CHECK(Tick(c, g) == "pullback 1");
    g.anyInCombat = true;
    g.killFirst = {Engaged(Unit(30, 5650, 12))};
    CHECK(Tick(c, g) == "focus 30");
    c.ccSent.insert(30);
    CHECK(Tick(c, g).empty() && c.focusGuid == 0);
}

// A boss step keeps the old rule: the plan's kill_first unit is focused
// wherever it stands (Zum'rah's Ward 7785 first while the tank holds his adds;
// the witch doctors, the 8179 ward and Anacondra are casters that keep ~25 yd).
static void TestKillFirstOnABossStepIsNotBounded()
{
    DungeonPlan plan = ZfPlan();
    PlanStep boss = Step(PlanStepKind::Boss, 10);
    boss.entry = 7271;
    ai::NormalizePlanStep(boss);
    plan.steps = {boss, Step(PlanStepKind::Rest)};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.units = {Unit(1, 7271, 10)};
    f.anyInCombat = true;
    f.killFirst = {Ward(9, 1, true, 20)};
    CHECK(Tick(r, f) == "focus 9");
    CHECK(KillFirstBounded(r) == false);
    // Far off and back: no drop on a boss step.
    f.killFirst[0].distLead = 38;
    CHECK(Tick(r, f).empty() && r.focusGuid == 9);
}

// The guard watches for a focus only while it is on: the focused unit dies
// (no kill_first unit left), the focus is cleared, and a mob on the healer
// with the tank off its stay and no pick-up or engage yet this fight is left
// to the tank's own AI, as before the focus rule.
static void TestGuardStopsWatchingWhenTheFocusEnds()
{
    DungeonPlan plan = ZfPlan();
    Run r = Start(&plan);
    Facts f = Party(0);
    f.leadIsTank = true;
    f.units = {Unit(1, 5648, 10)};
    CHECK(Tick(r, f) == "pullback 1");
    f.anyInCombat = true;
    f.killFirst = {Engaged(Unit(30, 5650, 8))};
    CHECK(Tick(r, f) == "focus 30");
    f.leadStaying = false;
    f.killFirst.clear(); // it died
    CHECK(Tick(r, f).empty() && r.focusGuid == 0);
    f.partyAttackers = {ZfHitter(50, 5649, 8)};
    for (int i = 0; i < 30; ++i)
        CHECK(Tick(r, f).empty());
    CHECK(r.guardOrders == 0);
}

// ZF r5-19z23 step 27, wipe 2 (15:14:23): "kill 5650 first" took every bot off
// "stay" (CommanderFocus -> RelaxTacticalMovement), the lead included, with
// guardOrders 0 and engageGuid 0, so GuardTarget never watched: 0 pick-ups the
// whole fight while mobs hit the priest. A focus keeps the guard watching.
static void TestGuardWatchesAfterAFocusWipe2Replay()
{
    DungeonPlan plan = ZfPlan();
    Run r = Start(&plan);
    Facts f = Party(0);
    f.leadIsTank = true;
    f.leadStaying = true;
    f.units = {Unit(1, 7247, 24.5f)};
    CHECK(Tick(r, f) == "pullback 1");
    f.anyInCombat = true;
    f.killFirst = {Engaged(Unit(30, 5650, 8))};
    CHECK(Tick(r, f) == "focus 30");
    // The focus ended the tank's stay; nothing has been picked up or engaged.
    f.leadStaying = false;
    CHECK(r.guardOrders == 0 && r.engageGuid == 0 && r.focusGuid == 30);
    // A 5649 hits the priest 8 yd from the tank: picked up at once (healer).
    f.partyAttackers = {ZfHitter(50, 5649, 8)};
    CHECK(Tick(r, f) == "attack 50 Tanky");
    CHECK(r.guardOrders == 1);
    CHECK(r.note.find("picks up 5649 off the healer") != std::string::npos);
    // No focus is re-sent: the pick-up does not change the kill_first pick.
    CHECK(Tick(r, f).empty() && r.focusGuid == 30);
}

// The order rate in the new state (focus on, the tank off its stay): healer
// attackers whose nearest changes every tick give at most kGuardOrdersPerFight
// pick-ups, each kGuardRepeatMs after the last order, and the focus is never
// re-sent. The same ten units hit the healer from the first tick: none joins
// later, so the budget is not refilled (CheckGuard; the refill has its own
// tests, TestGuardRefill*).
static void TestGuardAfterAFocusIsRateBounded()
{
    DungeonPlan plan = ZfPlan();
    Run r = Start(&plan);
    Facts f = Party(0);
    f.leadIsTank = true;
    f.units = {Unit(1, 5648, 10)};
    CHECK(Tick(r, f) == "pullback 1");
    f.anyInCombat = true;
    f.killFirst = {Engaged(Unit(30, 5650, 6))};
    CHECK(Tick(r, f) == "focus 30");
    f.leadStaying = false;
    int pickups = 0, focuses = 0, other = 0;
    std::uint32_t sinceLast = 0, minGap = ~0u;
    bool any = false;
    for (int i = 0; i < 300; ++i) // 30 s at kTickMs
    {
        f.partyAttackers.clear();
        for (int k = 0; k < 7; ++k)
            f.partyAttackers.push_back(ZfHitter(100 + k, 5649, 5.0f + ((i + k) % 7)));
        for (int k = 0; k < 3; ++k)
            f.partyAttackers.push_back(ZfHitter(200 + k, 5648, 4.5f + ((i + k) % 3)));
        std::string const o = Tick(r, f);
        sinceLast += kTickMs;
        if (o.compare(0, 7, "attack ") == 0)
        {
            ++pickups;
            if (any && sinceLast < minGap)
                minGap = sinceLast;
            any = true;
            sinceLast = 0;
        }
        else if (o.compare(0, 6, "focus ") == 0)
            ++focuses;
        else if (!o.empty())
            ++other;
    }
    CHECK(pickups == static_cast<int>(kGuardOrdersPerFight));
    CHECK(minGap >= kGuardRepeatMs);
    CHECK(focuses == 0 && other == 0);
}

// Deadmines r12-19z29 step 118, t 2207.9-2237: the kill_first 622 3569 was
// focused, turned to flee, and `focus <guid>` came back "No bot of the group
// accepted the focus order" (every bot refused an evading target). The focus
// stayed on it with no second order while it lived. Now the refusal clears
// the focus; the next tick focuses the next kill_first unit (622 356d), the
// refused one is passed over kFocusRefusedIgnoreMs and may be focused again
// after it; with no other unit the focus is none (the bots' own picks), and
// the guard keeps watching (the focus took every bot off its stay).
static void TestRefusedFocusIsPickedAgainDeadminesS118()
{
    std::uint64_t const fled = 0xf13000026e013569ull, next = 0xf13000026e01356dull;
    std::string const refusal = "failed: No bot of the group accepted the focus order.";
    DungeonPlan plan;
    plan.steps = {PullStep(10, {1731})};
    plan.killFirst = {622};
    {
        Run r = Start(&plan);
        Facts f = Party(0);
        f.leadIsTank = true;
        f.units = {Unit(1, 1731, 10)};
        CHECK(Tick(r, f) == "pullback 1");
        f.anyInCombat = true;
        f.killFirst = {Engaged(Unit(fled, 622, 4))};
        std::string const focusFled = "focus " + std::to_string(fled);
        CHECK(Tick(r, f) == focusFled && r.focusGuid == fled);
        OrderRefused(r, focusFled, refusal);
        CHECK(r.state == State::Running && r.focusGuid == 0);
        CHECK(r.note.find("the focus is picked again") != std::string::npos);
        // 356d (2218) joins: it is the focus now; the fled one is passed over.
        f.killFirst = {Engaged(Unit(fled, 622, 4)), Engaged(Unit(next, 622, 9))};
        CHECK(Tick(r, f) == "focus " + std::to_string(next) && r.focusGuid == next);
        // 356d dies; the fled one is still passed over: no focus, nothing held.
        f.killFirst = {Engaged(Unit(fled, 622, 4))};
        std::string o;
        int ticks = 1;
        for (; ticks < static_cast<int>(kFocusRefusedIgnoreMs / kTickMs) - 1; ++ticks)
            o += Tick(r, f);
        CHECK(o.empty() && r.focusGuid == 0);
        // After the wait: focused again (the retry).
        std::string again;
        for (int i = 0; i < 3 && again.empty(); ++i)
            again = Tick(r, f);
        CHECK(again == focusFled && r.focusGuid == fled);
    }
    {
        // No other kill_first unit: the focus is none, and the guard still
        // picks up a mob on the healer for the tank off its stay.
        Run r = Start(&plan);
        Facts f = Party(0);
        f.leadIsTank = true;
        f.units = {Unit(1, 1731, 10)};
        CHECK(Tick(r, f) == "pullback 1");
        f.anyInCombat = true;
        f.killFirst = {Engaged(Unit(fled, 622, 4))};
        std::string const focusFled = "focus " + std::to_string(fled);
        CHECK(Tick(r, f) == focusFled);
        OrderRefused(r, focusFled, refusal);
        CHECK(Tick(r, f).empty() && r.focusGuid == 0);
        f.partyAttackers = {ZfHitter(0xf1300006c301358full, 1731, 1)};
        CHECK(Tick(r, f) == "attack " + std::to_string(0xf1300006c301358full) + " Tanky");
        // A refusal of a focus that is no longer the current one leaves it.
        f.partyAttackers.clear();
        f.killFirst = {Engaged(Unit(next, 622, 6))};
        CHECK(Tick(r, f) == "focus " + std::to_string(next));
        OrderRefused(r, focusFled, refusal);
        CHECK(r.focusGuid == next);
        // The next fight starts with an empty list.
        f.anyInCombat = false;
        f.killFirst.clear();
        Tick(r, f);
        CHECK(r.focusRefused.empty() && !r.focusUnstayed);
    }
}

// 19z5: the mage enters the fight seconds after the pull (it waits for the
// tank's threat, then walks into range); its Polymorphs landed 7-8 s into its
// own fight. The window counts only the mage's fight time.
static void TestCcWindowCountsTheMagesFightTime()
{
    DungeonPlan plan = ZfPlan();
    plan.killFirst.clear();
    Run r = Start(&plan);
    Facts f = Party(0);
    MemberFact mage{"Magey", 5, true, false};
    f.members = {mage};
    f.units = {Unit(1, 5648, 10), Unit(2, 5650, 20)};
    CHECK(Tick(r, f) == "cc moon 2 Magey");
    CHECK(Tick(r, f) == "pullback 1");
    f.anyInCombat = true;
    for (int i = 0; i < 20; ++i)
        CHECK(Tick(r, f).empty()); // the mage not in the fight yet: not counted
    CHECK(r.ccPending == 2 && !r.ccResent);
    f.members[0].inCombat = true;
    std::string order;
    std::uint32_t ticks = 0;
    for (; ticks <= kCcLandMs / kTickMs && order.empty(); ++ticks)
        order = Tick(r, f);
    CHECK(order == "cc moon 2 Magey" && ticks == kCcLandMs / kTickMs);
}

static void TestStepIdleCeiling()
{
    DungeonPlan plan;
    PlanStep clear = Step(PlanStepKind::WaitClear, 0, 0, 0);
    clear.radius = 20;
    ai::NormalizePlanStep(clear);
    plan.steps = {clear};
    CHECK(StepIdleCapMs(plan.steps[0]) == kStepIdleCapMs);
    Run r = Start(&plan);
    Facts f = Party(0);
    f.seen = true;
    f.hostilesNear = 2;
    for (int i = 0; i < 100 && r.state == State::Running; ++i)
        Tick(r, f);
    CHECK(r.state == State::Paused && r.note.find("hostiles still near") != std::string::npos);
}

static void TestTeamFlags()
{
    DungeonPlan plan;
    plan.steps = {PullStep(10, {4851}, ai::kPlanFlagHorde), Step(PlanStepKind::Rest)};
    Facts f = Party(0);
    f.team = ai::kPlanFlagAlliance;
    f.seen = false;
    // Not seen yet: a pack for the other team is skipped without a walk.
    Run r = Start(&plan);
    CHECK(Tick(r, f).empty());
    CHECK(r.step == 1 && r.note.find("other team") != std::string::npos);
    // Seen and nothing hostile there: skipped too.
    Run r2 = Start(&plan);
    f.seen = true;
    f.units = {Unit(5, 4851, 10)};
    f.units[0].hostile = false;
    Tick(r2, f);
    CHECK(r2.step == 1);
    // Seen and hostile anyway: fought like any pack.
    Run r3 = Start(&plan);
    f.units[0].hostile = true;
    CHECK(Tick(r3, f) == "pullback 5");
    // The party's own team, or a step for both: normal.
    Run r4 = Start(&plan);
    f.team = ai::kPlanFlagHorde;
    CHECK(Tick(r4, f) == "pullback 5");
    CHECK(ai::StepForTeam(plan.steps[0], 0) && ai::StepForTeam(plan.steps[1], ai::kPlanFlagAlliance));
}

// Live bug (first bots-only Uldaman run): the Lost Dwarves (boss flagged
// horde) were skipped for an Alliance party before anybody looked at them.
static void TestOtherTeamBossIsLookedAtAndFoughtUnlessFriendly()
{
    DungeonPlan plan;
    PlanStep boss = Step(PlanStepKind::Boss, 100);
    boss.entry = 6906;
    boss.entries = {6907, 6908};
    boss.flags = ai::kPlanFlagHorde;
    ai::NormalizePlanStep(boss);
    plan.steps = {boss, Step(PlanStepKind::Rest)};
    Facts f = Party(0);
    f.team = ai::kPlanFlagAlliance;
    f.seen = false;
    // Not in sight: walk to it, never skip.
    Run r = Start(&plan);
    f.approachKnown = true;
    f.approachX = 40;
    CHECK(Tick(r, f) == "move 40.0 0.0 0.0");
    CHECK(r.step == 0);
    // In sight, the dwarves neutral or hostile: fought, Eric first.
    f.seen = true;
    f.leadX = 70;
    f.units = {Unit(1, 6906, 30), Unit(3, 6907, 35)};
    Run r2 = Start(&plan);
    CHECK(Tick(r2, f) == "pullback 3");
    // In sight and all friendly: skipped, saying why.
    for (UnitFact& u : f.units)
        u.hostile = false;
    Run r3 = Start(&plan);
    Tick(r3, f);
    CHECK(r3.step == 1 && r3.note.find("friendly") != std::string::npos);
    // In sight and nobody there: skipped (not "boss down": it was never fought).
    f.units.clear();
    Run r4 = Start(&plan);
    Tick(r4, f);
    CHECK(r4.step == 1 && r4.note == "boss is for the other team and not here: skipped");
    // The party's own boss in sight and dead: down.
    f.team = ai::kPlanFlagHorde;
    f.bossDeadNearSpot = true;
    Run r5 = Start(&plan);
    Tick(r5, f);
    CHECK(r5.step == 1 && r5.note == "boss down");
}

// Fourth observe run (Theka, ZF steps 54-56): "boss down" 2 s after arriving,
// with no fight; Theka patrols. A boss neither alive nor dead in sight is
// waited for at his spot, then the run pauses; never counted down.
static void TestBossDownNeedsASeenDeath()
{
    DungeonPlan plan;
    PlanStep boss = Step(PlanStepKind::Boss, 30);
    boss.entry = 7272;
    ai::NormalizePlanStep(boss);
    plan.steps = {boss, Step(PlanStepKind::Move, 60)};
    Run r = Start(&plan);
    Facts f = Party(0);
    CHECK(Tick(r, f).empty());
    CHECK(r.step == 0 && r.state == State::Running && r.note.find("not at his spot") != std::string::npos);
    // He walks in on his patrol: pulled.
    for (int i = 0; i < 20; ++i)
        Tick(r, f);
    f.units = {Unit(5, 7272, 40)};
    CHECK(Tick(r, f) == "pullback 5");
    f.anyInCombat = true;
    Tick(r, f);
    f.anyInCombat = false;
    // Killed: his corpse is seen.
    f.units.clear();
    f.bossDeadNearSpot = true;
    Tick(r, f);
    CHECK(r.step == 1 && r.note == "boss down");

    // Never there: paused after kBossSearchMs, "not found".
    Run p = Start(&plan);
    Facts g = Party(0);
    for (std::uint32_t i = 0; i <= kBossSearchMs / kTickMs + 1 && p.state == State::Running; ++i)
        Tick(p, g);
    CHECK(p.state == State::Paused && p.step == 0 && p.note.find("boss 7272 not found") == 0);
}

// Fourth observe run (step 43 -> 45) and 19z5 step 57 (Sul'lithuz Hatchlings
// 8130): a pullback refused invalid-target. The unit is left out for the
// run without a pause; the pack is skipped as out of reach, and a later step
// that sees the same unit pulls the other one. A skip clears the pull state.
static void TestSkipDropsTheRefusedPullTarget()
{
    DungeonPlan plan;
    plan.steps = {PullStep(10, {1}), Step(PlanStepKind::Move, 20), PullStep(30, {1, 2})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.units = {Unit(7, 1, 10)};
    CHECK(Tick(r, f) == "pullback 7");
    OrderRefused(r, "pullback 7", "invalid-target: The group cannot attack that target.");
    CHECK(r.state == State::Running && r.pullGuid == 0);
    Tick(r, f);
    CHECK(r.step == 1 && r.note.find("out of reach") != std::string::npos);
    f = Party(20);
    Tick(r, f);
    CHECK(r.step == 2);
    f.units = {Unit(7, 1, 5), Unit(8, 2, 15)};
    CHECK(Tick(r, f) == "pullback 8");
    // A skip clears the step's pull state.
    CHECK(Skip(r));
    CHECK(r.pullGuid == 0 && r.pulls == 0 && r.pullTries.empty());
}

static void TestResumeWithLeadDead()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 40)};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.dead = 1;
    f.alive = 4;
    f.leadAlive = false;
    Tick(r, f);
    // The owner's wait rule: no pause, the regroup waits for the lead bot.
    CHECK(r.state == State::Running && r.regroup);
    Pause(r, "test: the old pause");
    CHECK(Resume(r));
    CHECK(r.regroup);
    CHECK(Tick(r, f) == "revive party");
    // The ghost runs back: no pause while the regroup's ceiling holds.
    for (int i = 0; i < 100; ++i)
        Tick(r, f);
    CHECK(r.state == State::Running && r.regroup);
    // Alive again, gathered: the step goes on.
    f.dead = 0;
    f.alive = 5;
    f.leadAlive = true;
    CHECK(Tick(r, f) == "move 40.0 0.0 0.0");
    CHECK(!r.regroup);
    // A lead bot that never comes back is bounded by kRegroupReviveMs.
    Run r2 = Start(&plan);
    Facts g = Party(0);
    g.dead = 1;
    g.alive = 4;
    g.leadAlive = false;
    Tick(r2, g);
    Resume(r2);
    for (std::uint32_t i = 0; i <= kRegroupReviveMs / kTickMs + 2 && r2.state == State::Running; ++i)
        Tick(r2, g);
    CHECK(r2.state == State::Paused && r2.note.find("still dead") != std::string::npos);
}

static UnitFact Hitter(std::uint64_t guid, std::uint32_t entry, float dist, bool healer = false)
{
    UnitFact u = Unit(guid, entry, dist);
    u.hitting = true;
    u.victimHealer = healer;
    return u;
}

// Uldaman rerun 2026-10-17, the walk to Galgann at -10 337: the tank held
// "stay" with nothing on it while the party fought 13-31 yd away. With mobs
// hitting the party for kGuardWaitMs the tank picks the nearest one up
// (`attack <guid> <tank>`), and after the fight the run walks it back to the
// stand point.
static void TestTankGuardsThePartyFromItsStand()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 0), PullStep(40, {4855, 4853})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.leadIsTank = true;
    f.leadStaying = true;
    Tick(r, f); // on the stand point: the anchor, next step
    CHECK(r.step == 1 && r.anchorKnown);
    f.units = {Unit(31, 4853, 40)};
    CHECK(Tick(r, f) == "pullback 31");
    f.anyInCombat = true;
    // The pullback's mobs aggroed on the healer and still run in: not hitting yet.
    UnitFact running = Unit(31, 4853, 30);
    running.victimHealer = true;
    f.partyAttackers = {running};
    for (int i = 0; i < 8; ++i)
        CHECK(Tick(r, f).empty());
    // They arrive and hit the party; nothing is on the tank.
    f.partyAttackers = {Hitter(41, 7320, 35), Hitter(51, 7320, 45), Hitter(31, 4853, 30)};
    for (std::uint32_t ms = kTickMs; ms < kGuardWaitMs; ms += kTickMs)
        CHECK(Tick(r, f).empty());
    CHECK(Tick(r, f) == "attack 31 Tanky"); // the nearest one within kGuardYd
    CHECK(r.guardReturn);
    // The order ended the stay; the tank holds that mob now.
    f.leadStaying = false;
    f.leadHit = true;
    f.leadX = 28;
    f.partyAttackers = {Hitter(41, 7320, 12)};
    for (int i = 0; i < 10; ++i)
        CHECK(Tick(r, f).empty()); // a mob on a DPS while the tank holds its own: the tank's own AI
    // The fight is over: back to the stand point first, then the step goes on.
    f.anyInCombat = false;
    f.leadHit = false;
    f.partyAttackers.clear();
    f.units.clear();
    CHECK(Tick(r, f) == "move 0.0 0.0 0.0");
    f.leadX = 1;
    f.leadStaying = true;
    CHECK(Tick(r, f).empty()); // arrived: the next tick reads fresh facts
    CHECK(!r.guardReturn);
    Tick(r, f);
    CHECK(r.state == State::Done);
}

// Uldaman run 3 (19z12, tests/uldaman-events-3), baelog: the leg's pulls have
// no move step, so the run's anchor stayed the leg's start point; after every
// guarded fight the tank walked 90 yd back there, and on arrival the step
// paused at once on "no walkable path toward the pack" (the arrival tick's
// facts had no approach query: the glue skips it while the run walks). The
// tank walks back to its own spot at the first pick-up, the arrival ends the
// tick, and the next tick approaches the unseen pack.
static void TestTankGuardReturnsToItsOwnSpot()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 0), PullStep(150, {4851})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.leadIsTank = true;
    f.leadStaying = true;
    Tick(r, f); // the leg's start point: the anchor
    CHECK(r.step == 1 && r.anchorKnown && r.anchorX == 0.0f);
    // The pull goes out from 90 yd on (the pullback stand, no walk of the run's).
    f.leadX = 90;
    f.anchorPathRead = true;
    f.units = {Unit(31, 4851, 60), Unit(32, 4851, 62)};
    CHECK(Tick(r, f) == "pullback 31");
    f.anyInCombat = true;
    f.partyAttackers = {Hitter(31, 4851, 12, true)};
    CHECK(Tick(r, f) == "attack 31 Tanky");
    CHECK(r.guardReturn && r.guardX == 90.0f);
    // The tank chased its mob 15 yd; the fight is over, one of the pack is left.
    f.leadStaying = false;
    f.leadX = 105;
    f.anyInCombat = false;
    f.partyAttackers.clear();
    f.units = {Unit(32, 4851, 60)};
    CHECK(Tick(r, f) == "move 90.0 0.0 0.0"); // its own spot, not the leg's start
    // Arrived; the pack is out of sight and this tick's facts have no approach.
    f.leadX = 90;
    f.leadStaying = true;
    f.seen = false;
    f.approachKnown = false;
    CHECK(Tick(r, f).empty());
    CHECK(r.state == State::Running && !r.guardReturn);
    f.approachKnown = true;
    f.approachX = 125;
    std::string order;
    for (int i = 0; i < 5 && order.empty() && r.state == State::Running; ++i)
        order = Tick(r, f);
    CHECK(order == "move 125.0 0.0 0.0");
    CHECK(r.state == State::Running);

    // A spot the fight took the tank far from is left: it stays near the fight.
    Run r2 = Start(&plan);
    Facts g = Party(0);
    g.leadIsTank = true;
    g.leadStaying = true;
    Tick(r2, g);
    g.leadX = 90;
    g.anchorPathRead = true;
    g.units = {Unit(31, 4851, 60)};
    CHECK(Tick(r2, g) == "pullback 31");
    g.anyInCombat = true;
    g.partyAttackers = {Hitter(31, 4851, 12, true)};
    CHECK(Tick(r2, g) == "attack 31 Tanky");
    g.leadStaying = false;
    g.leadX = 90 + kGuardReturnMaxYd + 5;
    g.anyInCombat = false;
    g.partyAttackers.clear();
    std::string const next = Tick(r2, g);
    CHECK(next != "move 90.0 0.0 0.0" && next != "move 0.0 0.0 0.0");
    CHECK(!r2.guardReturn);
    // That spot is the party's anchor now (a later gather does not walk back).
    CHECK(r2.anchorKnown && r2.anchorX == g.leadX);

    // Uldaman run 3, pull 2: the tank on stay was still dragging the pullback
    // in (18 yd short of its stand) at the first pick-up. The walk back goes
    // to its stay point, not to where the pick-up found it.
    Run r3 = Start(&plan);
    Facts h = Party(0);
    h.leadIsTank = true;
    h.leadStaying = true;
    Tick(r3, h);
    h.leadX = 90;
    h.anchorPathRead = true;
    h.units = {Unit(31, 4851, 60)};
    CHECK(Tick(r3, h) == "pullback 31");
    h.anyInCombat = true;
    h.leadX = 108; // mid-drag
    h.leadStayKnown = true;
    h.leadStayX = 90;
    h.partyAttackers = {Hitter(31, 4851, 12, true)};
    CHECK(Tick(r3, h) == "attack 31 Tanky");
    CHECK(r3.guardX == 90.0f);
    h.leadStaying = false;
    h.leadStayKnown = false;
    h.anyInCombat = false;
    h.partyAttackers.clear();
    h.units = {Unit(32, 4851, 60)};
    CHECK(Tick(r3, h) == "move 90.0 0.0 0.0");
}

// Replay of keepers door pull 100 (trace r-...9bbf): a Brawler on the tank in
// melee reach, a Geomancer casting at the mage from 30 yd, then two Maulers on
// the mage and the healer. The tank keeps its Brawler while only the mage is
// hit; a mob hitting the healer is picked up (it needs no stay once the guard
// took the tank off it), the next one after a fresh wait, and the cap is
// reached in one fight.
static void TestTankGuardPull100Replay()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 0), PullStep(40, {4855, 4853})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.leadIsTank = true;
    f.leadStaying = true;
    Tick(r, f);
    f.units = {Unit(97, 4855, 40)};
    CHECK(Tick(r, f) == "pullback 97");
    f.anyInCombat = true;
    f.leadHit = true; // the Brawler, 1 yd
    f.partyAttackers = {Hitter(98, 4853, 30)}; // the Geomancer on the mage
    for (int i = 0; i < 8; ++i)
        CHECK(Tick(r, f).empty());
    // A Mauler reaches the healer at T (trace: 7478); she is dead at T+2-3 s
    // (botwatch 73 % at 03:07:22, 0 % at 03:07:26): the order goes out on the
    // first tick she is seen hit.
    f.partyAttackers = {Hitter(98, 4853, 30), Hitter(90, 7320, 14, true)};
    CHECK(Tick(r, f) == "attack 90 Tanky");
    // Off its stay now; the second Mauler turns on the healer: picked up once
    // kGuardRepeatMs have passed since the first, and the cap is reached in
    // one fight.
    f.leadStaying = false;
    f.partyAttackers = {Hitter(98, 4853, 30), Hitter(91, 7320, 20, true)};
    CHECK(Tick(r, f).empty());
    CHECK(Tick(r, f) == "attack 91 Tanky");
    int orders = 2;
    for (int i = 0; i < 20; ++i)
        if (Tick(r, f).compare(0, 7, "attack ") == 0)
            ++orders;
    // The first order was an engage on arrival (a healer's attacker on the
    // staying tank), which the cap does not count: it and the cap's pick-ups.
    // Nothing joined, so no refill and no per-unit cap: the second Mauler
    // takes all 3 pick-ups (literals: the Deadmines refill must not change it).
    CHECK(orders == 4);
    CHECK(r.guardOrders == 3 && r.guardUnitOrders[91] == 3 && r.guardRefills == 0);
    CHECK(r.state == State::Running);
}

// Cold review 2026-10-22 of the Deadmines refill: pull 100 with one Mauler on
// the healer and nothing joining. No refill, so no per-unit cap: the engage on
// arrival and all 3 pick-ups go to that Mauler, as before the refill. Then a
// unit joins far off: the refill's orders pass the Mauler over (it had 4, the
// per-unit cap after a refill is 2) and go to the joiner, 2 at most.
static void TestTankGuardOneMaulerAndTheCapAfterARefill()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 0), PullStep(40, {4855, 4853})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.leadIsTank = true;
    f.leadStaying = true;
    Tick(r, f);
    f.units = {Unit(97, 4855, 40)};
    CHECK(Tick(r, f) == "pullback 97");
    f.anyInCombat = true;
    f.leadHit = true; // the Brawler, 1 yd
    f.partyAttackers = {Hitter(90, 7320, 14, true)};
    CHECK(Tick(r, f) == "attack 90 Tanky"); // engage on arrival
    f.leadStaying = false;
    int orders = 1;
    for (int i = 0; i < 30; ++i)
    {
        std::string const o = Tick(r, f);
        if (!o.empty())
        {
            CHECK(o == "attack 90 Tanky");
            ++orders;
        }
    }
    CHECK(orders == 4 && r.guardOrders == 3 && r.guardUnitOrders[90] == 4 && r.guardRefills == 0);
    // A joiner on the healer, 30 yd off: the refill; the Mauler is capped now.
    f.partyAttackers = {Hitter(90, 7320, 14, true), Hitter(92, 2520, 30, true)};
    std::vector<std::string> after;
    for (int i = 0; i < 30; ++i)
    {
        std::string const o = Tick(r, f);
        if (!o.empty())
            after.push_back(o);
    }
    CHECK(r.guardRefills == 1);
    CHECK(after.size() == 2 && after[0] == "attack 92 Tanky" && after[1] == "attack 92 Tanky");
    CHECK(r.guardUnitOrders[90] == 4 && r.guardUnitOrders[92] == 2);
}

// The guard stays out of the way: no tank, a tank off its stay the guard did
// not take off, a polymorphed, evading, too far or not yet hitting mob, a
// scripted event; a refused pick-up or walk back never pauses the run.
static void TestTankGuardLimits()
{
    auto guardRun = [](DungeonPlan& plan)
    {
        plan.steps = {Step(PlanStepKind::Move, 0), PullStep(40, {4855})};
        Run r = Start(&plan);
        Facts f = Party(0);
        Tick(r, f);
        f.units = {Unit(31, 4855, 40)};
        Tick(r, f); // pullback 31
        return r;
    };
    auto fight = [](bool tank, bool staying, bool hit)
    {
        Facts f = Party(0);
        f.anyInCombat = true;
        f.leadIsTank = tank;
        f.leadStaying = staying;
        f.leadHit = hit;
        f.partyAttackers = {Hitter(31, 4855, 20)};
        return f;
    };
    auto ordersIn = [](Run& r, const Facts& f, int ticks)
    {
        int n = 0;
        for (int i = 0; i < ticks; ++i)
            if (Tick(r, f).compare(0, 7, "attack ") == 0)
                ++n;
        return n;
    };
    {
        DungeonPlan plan;
        Run r = guardRun(plan);
        CHECK(ordersIn(r, fight(true, true, true), 8) == 0);   // the tank holds a mob; the victim is no healer
        CHECK(ordersIn(r, fight(false, true, false), 8) == 0); // the lead bot is no tank
        CHECK(ordersIn(r, fight(true, false, false), 8) == 0); // off its stay by itself: its own AI goes already
        Facts f = fight(true, true, false);
        f.partyAttackers[0].polymorphed = true;
        CHECK(ordersIn(r, f, 8) == 0);
        f.partyAttackers[0].polymorphed = false;
        f.partyAttackers[0].evading = true;
        CHECK(ordersIn(r, f, 8) == 0);
        f.partyAttackers[0].evading = false;
        f.partyAttackers[0].hitting = false;
        CHECK(ordersIn(r, f, 8) == 0);
        f.partyAttackers[0].hitting = true;
        f.partyAttackers[0].distLead = kGuardYd + 1;
        CHECK(ordersIn(r, f, 8) == 0);
        f.partyAttackers[0].distLead = 20;
        CHECK(ordersIn(r, f, 5) == 1);
        OrderRefused(r, "attack 31 Tanky", "failed: That bot did not accept the attack.");
        CHECK(r.state == State::Running);
    }
    {
        // Inside a scripted event but not in its hold (a step leading into it):
        // nothing (the hold's own guard: tools/test_dungeon_event_policy.cpp).
        DungeonPlan plan;
        Run r = guardRun(plan);
        r.eventOn = true;
        CHECK(ordersIn(r, fight(true, true, false), 8) == 0);
    }
    {
        // A refused walk back to the stand point goes on from where it stands.
        DungeonPlan plan;
        Run r = guardRun(plan);
        CHECK(ordersIn(r, fight(true, true, false), 5) == 1);
        Facts after = Party(25);
        CHECK(Tick(r, after) == "move 0.0 0.0 0.0");
        OrderRefused(r, "move 0.0 0.0 0.0", "unreachable: The lead bot has no walkable path to that point.");
        CHECK(r.state == State::Running && !r.guardReturn);
    }
}

// Uldaman Pause 2 (scorpid_cave step 38, 2026-10-20): a Scorpid had a pet as its
// victim 15 yd from the tank and never came; the guard saw only mobs on bots,
// so the tank stood on "stay" and the fight ran past the 180 s ceiling. A mob
// on a party pet is now picked up too, but after every mob on a bot, never while
// the tank holds a mob of its own, and never from beyond kGuardYd of the tank
// (a pet's attacker beside a sleeping pack must not pull the tank across).
static UnitFact PetHitter(std::uint64_t guid, std::uint32_t entry, float dist)
{
    UnitFact u = Hitter(guid, entry, dist);
    u.victimPet = true;
    return u;
}

static void TestTankGuardPicksUpMobsOnPets()
{
    auto guardRun = [](DungeonPlan& plan)
    {
        plan.steps = {Step(PlanStepKind::Move, 0), PullStep(40, {4855})};
        Run r = Start(&plan);
        Facts f = Party(0);
        Tick(r, f);
        f.units = {Unit(31, 4855, 40)};
        Tick(r, f); // pullback 31
        return r;
    };
    auto fight = [](std::vector<UnitFact> const& attackers, bool hit = false)
    {
        Facts f = Party(0);
        f.anyInCombat = true;
        f.leadIsTank = true;
        f.leadStaying = true;
        f.leadHit = hit;
        f.partyAttackers = attackers;
        return f;
    };
    std::string orderNote; // the run's note on the tick that sent the order
    auto ordersIn = [&orderNote](Run& r, const Facts& f, int ticks)
    {
        std::string last;
        int n = 0;
        for (int i = 0; i < ticks; ++i)
        {
            std::string const o = Tick(r, f);
            if (o.compare(0, 7, "attack ") == 0)
            {
                ++n;
                last = o;
                orderNote = r.note;
            }
        }
        return std::make_pair(n, last);
    };
    {
        // The Pause 2 case: only a pet has a mob on it, 15 yd away. After the
        // guard's wait the tank is sent at it, and the note says "off a pet".
        DungeonPlan plan;
        Run r = guardRun(plan);
        auto const res = ordersIn(r, fight({PetHitter(31, 4863, 15)}), 5);
        CHECK(res.first == 1 && res.second == "attack 31 Tanky");
        CHECK(orderNote.find("off a pet") != std::string::npos);
        CHECK(r.guardReturn);
    }
    {
        // Bounded by kGuardYd from the tank: a pet's attacker at the edge of a
        // sleeping pack, beyond the bound, draws nothing; just inside it does.
        DungeonPlan plan;
        Run r = guardRun(plan);
        CHECK(ordersIn(r, fight({PetHitter(31, 4863, kGuardYd + 1)}), 10).first == 0);
        CHECK(ordersIn(r, fight({PetHitter(31, 4863, kGuardYd - 1)}), 5).first == 1);
    }
    {
        // A mob on a bot goes first, though the pet's attacker is nearer; a pet's
        // attacker only when no mob on a bot qualifies (here: one is not hitting yet).
        DungeonPlan plan;
        Run r = guardRun(plan);
        auto const res = ordersIn(r, fight({PetHitter(31, 4863, 8), Hitter(41, 7320, 30)}), 5);
        CHECK(res.first == 1 && res.second == "attack 41 Tanky");
        CHECK(orderNote.find("off a pet") == std::string::npos);
        DungeonPlan plan2;
        Run r2 = guardRun(plan2);
        UnitFact running = Unit(41, 7320, 30);
        auto const res2 = ordersIn(r2, fight({PetHitter(31, 4863, 8), running}), 5);
        CHECK(res2.first == 1 && res2.second == "attack 31 Tanky");
    }
    {
        // The tank holds a mob of its own: it leaves a pet's attacker to its AI.
        DungeonPlan plan;
        Run r = guardRun(plan);
        CHECK(ordersIn(r, fight({PetHitter(31, 4863, 15)}, true), 10).first == 0);
    }
    {
        // Not hitting (still running in), evading or polymorphed: no pick-up.
        DungeonPlan plan;
        Run r = guardRun(plan);
        UnitFact u = PetHitter(31, 4863, 15);
        u.hitting = false;
        CHECK(ordersIn(r, fight({u}), 10).first == 0);
        u.hitting = true;
        u.evading = true;
        CHECK(ordersIn(r, fight({u}), 10).first == 0);
        u.evading = false;
        u.polymorphed = true;
        CHECK(ordersIn(r, fight({u}), 10).first == 0);
    }
}

// Engage on arrival (PLAN-THREAT-265 M1a). The tank on "stay" at a stand fights
// nothing (an attack action is a movement action): RFC 19z19 step 25 had it idle
// 21 s with a Trogg on the mage and the rogue 2-4 yd away, Uldaman 19z19 step 85
// three mobs on it with no target for 14 s. So a mob on the tank in melee reach
// (Facts::leadAttacker), or one hitting the healer, sends it at once
// (`attack <guid> <tank>`, which ends the stay), without the guard's wait and
// without using up a pick-up of the fight (kGuardOrdersPerFight).
static Run EngageRun(DungeonPlan& plan)
{
    plan.steps = {Step(PlanStepKind::Move, 0), PullStep(40, {4855})};
    Run r = Start(&plan);
    Facts f = Party(0);
    Tick(r, f); // on the stand point: the anchor, next step
    f.units = {Unit(31, 4855, 40)};
    Tick(r, f); // pullback 31
    return r;
}

static Facts EngageFight(bool staying = true)
{
    Facts f = Party(0);
    f.anyInCombat = true;
    f.leadIsTank = true;
    f.leadStaying = staying;
    return f;
}

// A living mob on the lead bot in melee reach (what the glue reads for leadHit).
static void OnLead(Facts& f, std::uint64_t guid, std::uint32_t entry, float dist)
{
    f.leadHit = true;
    f.leadAttacker = Hitter(guid, entry, dist);
    f.leadHasAttacker = true;
}

static int OrdersIn(Run& r, const Facts& f, int ticks)
{
    int n = 0;
    for (int i = 0; i < ticks; ++i)
        if (Tick(r, f).compare(0, 7, "attack ") == 0)
            ++n;
    return n;
}

// T1: the first tick sends the tank at the mob on it; no pick-up is used.
static void TestEngageT1()
{
    DungeonPlan plan;
    Run r = EngageRun(plan);
    Facts f = EngageFight();
    OnLead(f, 60, 11517, 2.0f);
    CHECK(Tick(r, f) == "attack 60 Tanky");
    CHECK(r.guardOrders == 0 && r.engageGuid == 60);
    CHECK(r.note.find("the tank engages 11517 on arrival") != std::string::npos);
    CHECK(r.guardReturn);
}

// T2: the same mob gets no second order, however long its fight lasts. In a
// live run the order ends the stay at once; the stay stays on only when the bot
// holds the attack (stunned, feared: HoldsAttackUntilControl), and then the
// order must not be repeated every tick.
static void TestEngageT2()
{
    DungeonPlan plan;
    Run r = EngageRun(plan);
    Facts f = EngageFight();
    OnLead(f, 60, 11517, 2.0f);
    CHECK(Tick(r, f) == "attack 60 Tanky");
    CHECK(OrdersIn(r, f, 12) == 0);
    CHECK(r.guardOrders == 0);
}

// T3: a polymorphed or evading mob is not engaged.
static void TestEngageT3()
{
    DungeonPlan plan;
    Run r = EngageRun(plan);
    Facts f = EngageFight();
    OnLead(f, 60, 11517, 2.0f);
    f.leadAttacker.polymorphed = true;
    CHECK(OrdersIn(r, f, 6) == 0);
    f.leadAttacker.polymorphed = false;
    f.leadAttacker.evading = true;
    CHECK(OrdersIn(r, f, 6) == 0);
    CHECK(r.engageGuid == 0);
    f.leadAttacker.evading = false;
    CHECK(Tick(r, f) == "attack 60 Tanky"); // the same mob once it is neither
}

// T3b: the same for the healer's attacker.
static void TestEngageT3b()
{
    DungeonPlan plan;
    Run r = EngageRun(plan);
    Facts f = EngageFight();
    f.partyAttackers = {Hitter(41, 7320, 12, true)};
    f.partyAttackers[0].polymorphed = true;
    CHECK(OrdersIn(r, f, 3) == 0 && r.engageGuid == 0);
}

// T4: the healer's attacker first, then the mob on the tank.
static void TestEngageT4()
{
    DungeonPlan plan;
    Run r = EngageRun(plan);
    Facts f = EngageFight();
    OnLead(f, 60, 11517, 2.0f);
    f.partyAttackers = {Hitter(41, 7320, 20, true), Hitter(42, 7320, 12, true)};
    CHECK(Tick(r, f) == "attack 42 Tanky"); // the nearer of the healer's
    CHECK(r.guardOrders == 0 && r.engageGuid == 42);
// Off its stay now; the guard keeps watching (guardOrders is still 0):
// the other mob on the healer is picked up, a guard order this time.
    f.leadStaying = false;
    f.partyAttackers = {Hitter(41, 7320, 20, true)};
    CHECK(Tick(r, f).empty()); // kGuardRepeatMs after the engage, as after a pick-up
    CHECK(Tick(r, f) == "attack 41 Tanky");
    CHECK(r.guardOrders == 1);
}

// T5: only a DPS's attacker: no engage; the guard's wait rule fires as before.
static void TestEngageT5()
{
    DungeonPlan plan;
    Run r = EngageRun(plan);
    Facts f = EngageFight();
    f.partyAttackers = {Hitter(31, 4855, 20)};
    for (std::uint32_t ms = kTickMs; ms < kGuardWaitMs; ms += kTickMs)
        CHECK(Tick(r, f).empty());
    CHECK(r.engageGuid == 0 && r.guardOrders == 0);
    CHECK(Tick(r, f) == "attack 31 Tanky");
    CHECK(r.guardOrders == 1 && r.engageGuid == 0);
// ... and with a mob on the tank as well, the tank takes that one and
// not the DPS's.
    DungeonPlan plan2;
    Run r2 = EngageRun(plan2);
    Facts g = EngageFight();
    OnLead(g, 60, 11517, 2.0f);
    g.partyAttackers = {Hitter(31, 4855, 20)};
    CHECK(Tick(r2, g) == "attack 60 Tanky");
}

// T6: a tank that is not staying (already engaged) gets no order; nor does a
// lead bot that is no tank, or a dead one.
static void TestEngageT6()
{
    DungeonPlan plan;
    Run r = EngageRun(plan);
    Facts f = EngageFight(false);
    OnLead(f, 60, 11517, 2.0f);
    f.partyAttackers = {Hitter(41, 7320, 20, true)};
    CHECK(OrdersIn(r, f, 8) == 0);
    Facts g = EngageFight();
    OnLead(g, 60, 11517, 2.0f);
    g.leadIsTank = false;
    CHECK(OrdersIn(r, g, 8) == 0);
    Facts h = EngageFight();
    OnLead(h, 60, 11517, 2.0f);
    h.leadAlive = false;
    CHECK(OrdersIn(r, h, 8) == 0);
    CHECK(r.engageGuid == 0);
}

// T7: inside an event (a hold, its ritual, the moves into it): no engage.
// The hold's own engage (HoldEngageTarget) is the event policy's.
static void TestEngageT7()
{
    DungeonPlan plan;
    Run r = EngageRun(plan);
    Facts f = EngageFight();
    OnLead(f, 60, 11517, 2.0f);
    CHECK(EngageTarget(r, f) != nullptr);
    r.eventOn = true;
    CHECK(EngageTarget(r, f) == nullptr);
    CHECK(OrdersIn(r, f, 8) == 0);
    CHECK(r.engageGuid == 0);
}

// T8: after the fight the guard return walks the tank back to its stand.
static void TestEngageT8()
{
    DungeonPlan plan;
    Run r = EngageRun(plan);
    Facts f = EngageFight();
    f.leadX = 6; // dragging the pullback in, short of its stand
    f.leadStayKnown = true;
    f.leadStayX = 0;
    OnLead(f, 60, 11517, 2.0f);
    CHECK(Tick(r, f) == "attack 60 Tanky");
    CHECK(r.guardReturn && r.guardX == 0.0f);
    f.leadStaying = false;
    f.leadStayKnown = false;
    f.leadX = 14; // chased it
    CHECK(OrdersIn(r, f, 5) == 0);
    f.anyInCombat = false;
    f.leadHit = false;
    f.leadHasAttacker = false;
    f.units.clear();
    CHECK(Tick(r, f) == "move 0.0 0.0 0.0");
    f.leadX = 1;
    f.leadStaying = true;
    CHECK(Tick(r, f).empty());
    CHECK(!r.guardReturn);
// The next fight starts over: the same mob guid is engaged again.
    CHECK(r.engageGuid == 0);
}

// T9: the attack is held (the stay stays on) and the nearest mob changes every
// tick: the order is not sent every tick. The fight's first engage is at once,
// a further one only kGuardRepeatMs after the last order, and three at most.
static void TestEngageT9()
{
    DungeonPlan plan;
    Run r = EngageRun(plan);
    std::vector<int> at;
    for (int i = 0; i < 16; ++i)
    {
        Facts f = EngageFight();
        OnLead(f, i % 2 == 0 ? 60 : 61, 11517, 2.0f);
        std::string const order = Tick(r, f);
        if (order.compare(0, 7, "attack ") == 0)
            at.push_back(i);
    }
    CHECK(at.size() == kGuardOrdersPerFight);
    CHECK(at[0] == 0);
    for (std::size_t i = 1; i < at.size(); ++i)
        CHECK(at[i] - at[i - 1] >= static_cast<int>(kGuardRepeatMs / kTickMs));
    CHECK(r.guardOrders == 0 && r.engageOrders == kGuardOrdersPerFight);
}

// T10: the end of the fight starts the engage over: the next fight, with the
// tank off its stay, is not watched by the guard on account of the old engage
// (no pick-up of a DPS's attacker, no spacing), and its first engage is at once
// again, the cap spent or not.
static void TestEngageT10()
{
    DungeonPlan plan;
    Run r = EngageRun(plan);
    for (int i = 0; i < 8; ++i)
    {
        Facts f = EngageFight();
        OnLead(f, 60 + i % 2, 11517, 2.0f);
        Tick(r, f);
    }
    CHECK(r.engageOrders == kGuardOrdersPerFight && r.engageGuid != 0);
    // The fight is over: back to the stand, the step goes on.
    Facts over = EngageFight(false);
    over.anyInCombat = false;
    over.leadX = 14; // chased it
    CHECK(Tick(r, over) == "move 0.0 0.0 0.0");
    over.leadX = 1;
    over.leadStaying = true;
    Tick(r, over);
    CHECK(r.engageGuid == 0 && r.engageOrders == 0 && !r.guardReturn);
    // A new fight, the tank off its stay with a DPS being hit: its own AI; the
    // guard does not keep watching for the old engage.
    Facts next = EngageFight(false);
    next.partyAttackers = {Hitter(31, 4855, 20)};
    CHECK(OrdersIn(r, next, 8) == 0);
    // A new fight with the tank on its stay: the engage is at once again.
    Run r2 = r;
    Facts stay = EngageFight();
    OnLead(stay, 70, 11517, 2.0f);
    CHECK(Tick(r2, stay) == "attack 70 Tanky");
}

// T11: one clock for both kinds of order. After a pick-up (attack held, the
// stay still on) an engage waits kGuardRepeatMs too, and after an engage a
// pick-up candidate inside kGuardRepeatMs waits (the existing repeat rule).
static void TestEngageT11()
{
    {
        DungeonPlan plan;
        Run r = EngageRun(plan);
        Facts f = EngageFight();
        f.partyAttackers = {Hitter(31, 4855, 20)};
        for (std::uint32_t ms = kTickMs; ms < kGuardWaitMs; ms += kTickMs)
            CHECK(Tick(r, f).empty());
        CHECK(Tick(r, f) == "attack 31 Tanky"); // a pick-up; the attack is held, the stay stays on
        CHECK(r.guardOrders == 1 && r.engageOrders == 0);
        Facts g = EngageFight();
        OnLead(g, 60, 11517, 2.0f); // an engage target appears on the next tick
        CHECK(Tick(r, g).empty());  // 1 s after the pick-up
        CHECK(Tick(r, g) == "attack 60 Tanky"); // kGuardRepeatMs after it, exactly one
        CHECK(r.guardOrders == 1 && r.engageOrders == 1);
        CHECK(OrdersIn(r, g, 6) == 0);
    }
    {
        // The mirror: an engage, then a pick-up candidate (a mob on the healer)
        // inside kGuardRepeatMs; the stay is off (the order took effect).
        DungeonPlan plan;
        Run r = EngageRun(plan);
        Facts f = EngageFight();
        OnLead(f, 60, 11517, 2.0f);
        CHECK(Tick(r, f) == "attack 60 Tanky");
        f.leadStaying = false;
        f.partyAttackers = {Hitter(41, 7320, 12, true)};
        CHECK(Tick(r, f).empty()); // 1 s after the engage
        CHECK(Tick(r, f) == "attack 41 Tanky");
        CHECK(r.guardOrders == 1 && r.engageOrders == 1);
    }
    {
        // ... and with the stay still on (held attack), the same candidate is
        // an engage target: it waits the same 2 s, then one order.
        DungeonPlan plan;
        Run r = EngageRun(plan);
        Facts f = EngageFight();
        OnLead(f, 60, 11517, 2.0f);
        CHECK(Tick(r, f) == "attack 60 Tanky");
        f.partyAttackers = {Hitter(41, 7320, 12, true)};
        CHECK(Tick(r, f).empty());
        CHECK(Tick(r, f) == "attack 41 Tanky");
        CHECK(r.guardOrders == 0 && r.engageOrders == 2);
    }
}

// The guard's budget refill (Deadmines r12-19z29, DEADMINES-PACKS-2026-10-22
// Q1/Q3). Every tick's orders of a fight with the party attackers `att` (the
// tank off its stay unless `staying`), as (tick, order).
static std::string gGuardOrderNote; // the run's note on the tick of GuardOrders' last order
static std::vector<std::pair<int, std::string>> GuardOrders(Run& r, const std::vector<UnitFact>& att, int ticks,
    int start, bool staying = false)
{
    std::vector<std::pair<int, std::string>> out;
    for (int i = 0; i < ticks; ++i)
    {
        Facts f = EngageFight(staying);
        f.partyAttackers = att;
        std::string const o = Tick(r, f);
        if (!o.empty())
        {
            out.emplace_back(start + i, o);
            gGuardOrderNote = r.note;
        }
    }
    return out;
}

static std::string AttackOn(std::uint64_t guid)
{
    return "attack " + std::to_string(guid) + " Tanky";
}

// Mob guids of the trace (0xF130 creature, the entry in the high part) and the
// Remote-Controlled Golem summons (0xF140 pet guids).
constexpr std::uint64_t kDm622a = 0xf13000026e01356cull, kDm622b = 0xf13000026e013569ull;
constexpr std::uint64_t kDm1731 = 0xf1300006c301358full, kDmRcgA = 0xf14000035700001aull,
                        kDmRcgB = 0xf14000035700001bull;

// s118, t 2138-2188: 622 356c on the healer at the walk (engaged on arrival),
// 622 3569 and the golem summons 001a/001b join; the pick-ups are spent in the
// first seconds. At 2161 1731 358f joins and sits on the healer with the
// golems for 26 s, the tank 1 yd away sending nothing; she died at 2188.7.
// Now the joiner restores the budget: an order within kGuardRepeatMs.
static void TestGuardRefillDeadminesS118()
{
    DungeonPlan plan;
    Run r = EngageRun(plan);
    std::vector<UnitFact> att = {Hitter(kDm622a, 622, 3, true)};
    auto o = GuardOrders(r, att, 1, 2138, true);
    CHECK(o.size() == 1 && o[0].second == AttackOn(kDm622a)); // engage on arrival
    att = {Hitter(kDm622a, 622, 3, true), Hitter(kDm622b, 622, 6, true), Hitter(kDmRcgA, 2520, 4, true),
        Hitter(kDmRcgB, 2520, 5, true)};
    o = GuardOrders(r, att, 22, 2139);
    CHECK(o.size() == 3); // the pick-ups, spent by 2151
    CHECK(r.guardOrders == 3);
    // The old silence: the same units, no order however long.
    CHECK(GuardOrders(r, att, 10, 2161).empty());
    // 1731 358f joins on the healer, 1 yd from the tank.
    att.push_back(Hitter(kDm1731, 1731, 1, true));
    o = GuardOrders(r, att, 2, 2171);
    CHECK(o.size() == 1 && o[0].second == AttackOn(kDm1731));
    CHECK(gGuardOrderNote.find("picks up 1731 off the healer") != std::string::npos &&
        gGuardOrderNote.find("a new unit joined") != std::string::npos);
    // Before the refill the nearest 622 356c took the engage and all 3
    // pick-ups (no per-unit cap without a refill). The refilled budget goes on
    // to the units still on her, never to 356c (4 orders, over the cap of 2
    // after a refill), spaced as before, and then the fight is silent again.
    CHECK(r.guardUnitOrders[kDm622a] == 4 && r.guardRefills == 1);
    o = GuardOrders(r, att, 30, 2173);
    CHECK(o.size() == 2);
    for (std::size_t i = 1; i < o.size(); ++i)
        CHECK(o[i].first - o[i - 1].first >= 2);
    for (auto const& order : o)
        CHECK(order.second != AttackOn(kDm622a));
    CHECK(r.guardUnitOrders[kDm622a] == 4 && r.guardUnitOrders[kDm1731] <= 2);
    CHECK(GuardOrders(r, att, 20, 2203).empty());
}

// s90, t 1707-1777: the pull's 622s, a golem 61962 and the Taskmaster 4417
// spend the budget by 1718; then the Overseer 634 (1724), the Harvester 61963
// (1742) and golem summons 0016/0017 (1743) join one after another. Each
// joiner gets the tank again, within kGuardRepeatMs of its arrival.
static void TestGuardRefillDeadminesS90()
{
    DungeonPlan plan;
    Run r = EngageRun(plan);
    std::vector<UnitFact> att = {Hitter(0xf13000026e016a03ull, 622, 12, true),
        Hitter(0xf13000f20a016998ull, 61962, 23, true), Hitter(0xf130001141016a21ull, 4417, 9, true)};
    auto o = GuardOrders(r, att, 1, 1709, true);
    CHECK(o.size() == 1);
    o = GuardOrders(r, att, 14, 1710);
    CHECK(o.size() == 3 && r.guardOrders == 3);
    struct Join
    {
        int t;
        UnitFact u;
    };
    std::vector<Join> const joins = {{1724, Hitter(0xf13000027a016a12ull, 634, 8, true)},
        {1742, Hitter(0xf13000f20b01698cull, 61963, 6, true)},
        {1752, Hitter(0xf140000357000016ull, 2520, 4, true)},
        {1762, Hitter(0xf140000357000017ull, 2520, 3, true)}};
    int t = 1724;
    for (Join const& j : joins)
    {
        // Silent until the joiner: the budget is spent again.
        CHECK(GuardOrders(r, att, j.t - t, t).size() <= 2);
        att.push_back(j.u);
        bool const capped = r.fightOrders >= 12;
        o = GuardOrders(r, att, 2, j.t);
        // Each refill's first order goes to its joiner (on the healer, the
        // nearest); the rest of it to the units still on her. The fight's
        // 12 ends it: here the fourth joiner, after 12.
        if (!capped)
            CHECK(!o.empty() && o[0].second == AttackOn(j.u.guid));
        else
            CHECK(o.empty());
        t = j.t + 2;
    }
    CHECK(r.fightOrders == 12);
    CHECK(GuardOrders(r, att, 20, t).empty());
}

// s125, t 2556-2619: the boss camp's 1731s and 622s spend the budget; the
// golem summon 0021 lands at 2572 (the refused pick-up at 2576.1 was its
// order) and Gilnid 1763 joins at 2595: an order at each.
static void TestGuardRefillDeadminesS125()
{
    DungeonPlan plan;
    Run r = EngageRun(plan);
    std::vector<UnitFact> att = {Hitter(0xf1300006c3013564ull, 1731, 2, true),
        Hitter(0xf1300006c3013561ull, 1731, 9, true), Hitter(0xf13000026e01355cull, 622, 18, true),
        Hitter(0xf1300006c301355dull, 1731, 10, true)};
    CHECK(GuardOrders(r, att, 1, 2563, true).size() == 1);
    CHECK(GuardOrders(r, att, 9, 2564).size() == 3);
    att.push_back(Hitter(0xf14000035f000021ull, 2520, 5, true));
    auto o = GuardOrders(r, att, 2, 2573);
    CHECK(!o.empty() && o[0].second == AttackOn(0xf14000035f000021ull));
    GuardOrders(r, att, 20, 2575);
    att.push_back(Hitter(0xf1300006e3013566ull, 1763, 4, true)); // Gilnid on the healer
    o = GuardOrders(r, att, 2, 2595);
    CHECK(!o.empty() && o[0].second == AttackOn(0xf1300006e3013566ull));
}

// The bounds of the refill: a new unit every tick for 2 minutes (a stream of
// summons) never gives more than 12 orders a fight, never
// two within kGuardRepeatMs, and after a refill never a third at one unit. A joiner while the budget is not spent does not refill it (TestEngageT9
// is the alternating pair).
static void TestGuardRefillIsBounded()
{
    DungeonPlan plan;
    Run r = EngageRun(plan);
    std::vector<UnitFact> att;
    std::vector<int> at;
    for (int i = 0; i < 120; ++i)
    {
        att.push_back(Hitter(1000 + i, 2520, 1.0f + (i % 9), true));
        Facts f = EngageFight(i == 0);
        f.partyAttackers = att;
        auto const before = r.guardUnitOrders;
        bool const refilledBefore = r.guardRefills > 0;
        std::string const o = Tick(r, f);
        if (o.compare(0, 7, "attack ") == 0)
        {
            at.push_back(i);
            // Once a refill happened (on this tick or before), an order goes
            // only to a unit with fewer than 2 orders this fight.
            if (refilledBefore || r.guardRefills > 0)
            {
                std::uint64_t const guid = std::strtoull(o.c_str() + 7, nullptr, 10);
                auto const it = before.find(guid);
                CHECK(it == before.end() || it->second < 2);
            }
        }
    }
    CHECK(at.size() == 12 && r.fightOrders == 12 && r.guardRefills >= 1);
    for (std::size_t i = 1; i < at.size(); ++i)
        CHECK(at[i] - at[i - 1] >= 2);
    // The next fight starts over.
    Facts over = EngageFight(true);
    over.anyInCombat = false;
    Tick(r, over);
    CHECK(r.fightOrders == 0 && r.guardSeen.empty() && r.guardUnitOrders.empty());

    // A joiner while pick-ups are left: no refill (the budget serves it).
    DungeonPlan plan2;
    Run b = EngageRun(plan2);
    std::vector<UnitFact> two = {Hitter(1, 622, 3, true)};
    CHECK(GuardOrders(b, two, 1, 0, true).size() == 1);
    CHECK(GuardOrders(b, two, 2, 1).size() == 1 && b.guardOrders == 1); // a pick-up of the same unit
    two.push_back(Hitter(2, 622, 2, true));
    GuardOrders(b, two, 1, 3);
    CHECK(b.guardOrders >= 1 && !b.guardRefilled);
}

// A refill keeps the spacing and the watch. (a) The engage budget spent on
// the alternating pair of TestEngageT9 (the attack held, the stay still on),
// a third unit joins 1 s after the last order: it is engaged kGuardRepeatMs
// after that order, not at once. (b) Only pick-ups (mobs on a DPS, no engage,
// no focus), the tank off its stay: after the refill the budgets read 0, and
// the guard still watches (fightOrders), so the joiner is picked up.
static void TestGuardRefillKeepsSpacingAndWatch()
{
    {
        DungeonPlan plan;
        Run r = EngageRun(plan);
        std::vector<int> at;
        int i = 0;
        for (; i < 16 && r.engageOrders < 3; ++i)
        {
            Facts f = EngageFight();
            OnLead(f, i % 2 == 0 ? 60 : 61, 11517, 2.0f);
            if (Tick(r, f).compare(0, 7, "attack ") == 0)
                at.push_back(i);
        }
        CHECK(at.size() == 3 && at.back() == i - 1);
        Facts f = EngageFight();
        OnLead(f, i % 2 == 0 ? 60 : 61, 11517, 2.0f);
        f.partyAttackers = {Hitter(62, 2520, 3, true)}; // a summon on the healer, 1 s after the last order
        CHECK(Tick(r, f).empty());
        CHECK(Tick(r, f) == "attack 62 Tanky"); // kGuardRepeatMs after the last order
    }
    {
        DungeonPlan plan;
        Run r = EngageRun(plan);
        std::vector<UnitFact> att = {Hitter(31, 4855, 20), Hitter(32, 4855, 22)};
        std::size_t orders = GuardOrders(r, att, 4, 0, true).size();
        orders += GuardOrders(r, att, 20, 10).size();
        CHECK(orders == 3 && r.engageGuid == 0 && r.focusGuid == 0);
        CHECK(r.guardOrders == 3);
        att.push_back(Hitter(33, 2520, 5));
        auto const o = GuardOrders(r, att, 2, 30);
        CHECK(o.size() == 1 && o[0].second == "attack 33 Tanky");
    }
}

// R1: Ragefire Chasm 19z19, Oggleflint pull (step 25), t=862-875. Oggleflint is
// on Tanky from 862 while he stands on "stay" with no target; a Ragefire Trogg
// (be05) hits the mage Gera 4 yd away and later the rogue. The old guard sent
// nothing until 885 (a mob on the healer, and the tank held one in reach, so the
// DPS's Trogg was left alone). Now the tank is sent at Oggleflint on the first
// tick, and it keeps its mob: the Trogg on the mage draws no second order.
static void TestEngageReplayRfcOggleflint()
{
    DungeonPlan plan;
    Run r = EngageRun(plan);
    Facts f = EngageFight();
    OnLead(f, 161, 11517, 1.5f);                  // Oggleflint on Tanky
    f.partyAttackers = {Hitter(48645, 11318, 4.0f)}; // the Trogg (be05) on Gera
    CHECK(Tick(r, f) == "attack 161 Tanky");
    CHECK(r.guardOrders == 0);
    f.leadStaying = false; // the order ended the stay; 863-875 go on like that
    for (int i = 0; i < 13; ++i)
        CHECK(Tick(r, f).empty());
    CHECK(r.guardOrders == 0 && r.state == State::Running);
}

// R2: Uldaman 19z19, step 85 (the firing stand for 4853), t=2613-2632. Three
// mobs are on Cedrias (staying, no target from 2616 to 2630); Katrien's Rain of
// Fire wakes more. The tank is sent at the nearest mob on it on the first tick
// (old: no order until 2632, for the healer), and from 2632 the guard still
// picks the healer's attacker up (a pick-up, not an engage, off the stay).
static void TestEngageReplayUldamanStep85()
{
    DungeonPlan plan;
    Run r = EngageRun(plan);
    Facts f = EngageFight();
    OnLead(f, 8201, 4853, 2.4f); // the nearest of the three; the glue reports one
    f.partyAttackers = {Hitter(8101, 4855, 9.0f), Hitter(8103, 4855, 11.0f)}; // on Katrien and the hunter
    CHECK(Tick(r, f) == "attack 8201 Tanky");
    CHECK(r.guardOrders == 0 && r.engageGuid == 8201);
    f.leadStaying = false;
    for (int i = 0; i < 18; ++i) // 2614-2631
        CHECK(Tick(r, f).empty());
    f.partyAttackers.push_back(Hitter(8204, 4853, 16.0f, true)); // 2632: 4853 on the priest
    CHECK(Tick(r, f) == "attack 8204 Tanky");
    CHECK(r.guardOrders == 1);
}

// Uldaman run 3 (19z12, 10:45-10:54): the priest and the mage revived at the
// entrance and walked ~270 yd back through the Shrike Bats (4 extra deaths);
// the regroup paused at 60 s with a bot 273.7 yd out. A revived bot far from
// the lead bot is summoned to it on its first tick alive and out of combat,
// before any gather walks it, once per revive.
static Run DeathsInAWalk(const DungeonPlan& plan, Facts& f, std::vector<std::string> dead)
{
    Run r = Start(&plan);
    f = Party(0);
    CHECK(Tick(r, f) == "move 40.0 0.0 0.0");
    f.dead = static_cast<std::uint32_t>(dead.size());
    f.alive = 5 - f.dead;
    f.deadNames = dead;
    f.members = {Mem("Tanky", 0)};
    CHECK(Tick(r, f).empty()); // the wait rule starts the regroup
    CHECK(r.regroup);
    CHECK(Tick(r, f) == "revive party");
    return r;
}

static void TestRevivedBotSummonedBeforeWalking()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 40), PullStep(60, {1})};
    Facts f;
    Run r = DeathsInAWalk(plan, f, {"Quelinator", "Tobbert"});
    // The priest is up at the entrance, the mage still a ghost: she is summoned now.
    f.dead = 1;
    f.alive = 4;
    f.deadNames = {"Tobbert"};
    f.members = {Mem("Tanky", 0), Mem("Quelinator", 270)};
    f.maxMemberYd = 270;
    CHECK(Tick(r, f) == "summon Quelinator");
    CHECK(r.note.find("summoned to it instead of walking back") != std::string::npos);
    // The mage up too: summoned on his first tick alive.
    f.dead = 0;
    f.alive = 5;
    f.deadNames.clear();
    f.members = {Mem("Tanky", 0), Mem("Quelinator", 270), Mem("Tobbert", 268)};
    CHECK(Tick(r, f) == "summon Tobbert");
    // No gather walks them while their summons are on the way.
    for (int i = 0; i < 3; ++i)
        CHECK(Tick(r, f).empty());
    // Both at the lead bot: the regroup ends, no walk back through the dungeon.
    f.members = {Mem("Tanky", 0), Mem("Quelinator", 3), Mem("Tobbert", 4)};
    f.maxMemberYd = 4;
    CHECK(Tick(r, f) == "move 40.0 0.0 0.0");
    CHECK(!r.regroup && r.waitFor.empty() && r.reviveSummons.empty());
    // A bot revived near the lead bot (a resurrection in the party) needs none.
    Run n = DeathsInAWalk(plan, f, {"Quelinator"});
    f.dead = 0;
    f.alive = 5;
    f.deadNames.clear();
    f.members = {Mem("Tanky", 0), Mem("Quelinator", 30)};
    f.maxMemberYd = 30;
    CHECK(Tick(n, f) == "move 0.0 0.0 0.0"); // the gather, no summon
}

static void TestNoReviveSummonInCombat()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 40), PullStep(60, {1})};
    Facts f;
    Run r = DeathsInAWalk(plan, f, {"Quelinator"});
    f.dead = 0;
    f.alive = 5;
    f.deadNames.clear();
    f.members = {Mem("Tanky", 0), Mem("Quelinator", 200)};
    f.maxMemberYd = 200;
    // In combat on the way (the bats): never summoned (`cmd summon` refuses it).
    f.members[1].inCombat = true;
    f.anyInCombat = true;
    for (int i = 0; i < 5; ++i)
        CHECK(Tick(r, f).find("summon") == std::string::npos);
    // A member flagged in combat while the party reads out of combat: not either.
    f.anyInCombat = false;
    for (int i = 0; i < 3; ++i)
        CHECK(Tick(r, f).find("summon") == std::string::npos);
    // With attackers it runs back to the party (RunBack, #1054), no summon.
    f.members[1].inCombat = false;
    f.members[1].attackers = 1;
    f.members[1].yd = 100;
    f.maxMemberYd = 100;
    CHECK(Tick(r, f).find("summon") == std::string::npos);
    CHECK(!r.runBack.empty());
    // Its attackers gone, the run back goes on until it ends (here: it stalls);
    // then, out of combat and far, it is summoned.
    f.members[1].attackers = 0;
    std::string order;
    for (std::uint32_t ms = 0; ms < kWalkRetryMs + 3000 && order.empty(); ms += kTickMs)
    {
        bool const running = !r.runBack.empty();
        order = Tick(r, f);
        CHECK(!running || order.empty() || !r.runBack.empty() || order == "summon Quelinator");
    }
    CHECK(order == "summon Quelinator" && r.runBack.empty());
}

static void TestOneReviveSummonPerRevive()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 40), PullStep(60, {1})};
    Facts f;
    Run r = DeathsInAWalk(plan, f, {"Quelinator"});
    f.dead = 0;
    f.alive = 5;
    f.deadNames.clear();
    f.members = {Mem("Tanky", 0), Mem("Quelinator", 270)};
    f.maxMemberYd = 270;
    int summons = 0;
    for (int i = 0; i < 30; ++i)
    {
        f.members[1].x += (i % 2 ? 3.0f : -3.0f); // walking: no stand-still summon either
        if (Tick(r, f) == "summon Quelinator")
            ++summons;
    }
    CHECK(summons == 1);
    // She dies again on the way and is revived: her new revive gets its summon.
    f.dead = 1;
    f.alive = 4;
    f.deadNames = {"Quelinator"};
    f.members = {Mem("Tanky", 0)};
    f.maxMemberYd = 0;
    std::string order;
    for (int i = 0; i < 3 && order != "revive party"; ++i)
        order = Tick(r, f);
    f.dead = 0;
    f.alive = 5;
    f.deadNames.clear();
    f.members = {Mem("Tanky", 0), Mem("Quelinator", 270)};
    f.maxMemberYd = 270;
    order.clear();
    for (int i = 0; i < 3 && order.empty(); ++i)
        order = Tick(r, f);
    CHECK(order == "summon Quelinator");
}

static void TestFailedReviveSummonWalks()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 40), PullStep(60, {1})};
    Facts f;
    Run r = DeathsInAWalk(plan, f, {"Quelinator"});
    f.dead = 0;
    f.alive = 5;
    f.deadNames.clear();
    f.members = {Mem("Tanky", 0), Mem("Quelinator", 270)};
    f.maxMemberYd = 270;
    CHECK(Tick(r, f) == "summon Quelinator");
    // Refused (in combat by the time it arrived): she walks with the gather.
    OrderRefused(r, "summon Quelinator", "unavailable: That bot is dead, in combat, on a taxi or teleporting.");
    CHECK(r.state == State::Running && r.reviveSummons.empty() && r.note.find("it walks back") != std::string::npos);
    CHECK(Tick(r, f) == "move 0.0 0.0 0.0");
    // Acknowledged but she never came: after kReviveSummonWaitMs she walks.
    Run t = DeathsInAWalk(plan, f, {"Quelinator"});
    f.dead = 0;
    f.alive = 5;
    f.deadNames.clear();
    f.members = {Mem("Tanky", 0), Mem("Quelinator", 270)};
    f.maxMemberYd = 270;
    CHECK(Tick(t, f) == "summon Quelinator");
    std::string order;
    std::uint32_t ms = 0;
    for (; ms <= kReviveSummonWaitMs + kTickMs && order.empty(); ms += kTickMs)
        order = Tick(t, f);
    CHECK(order == "move 0.0 0.0 0.0" && ms >= kReviveSummonWaitMs && t.reviveSummons.empty());
}

static void TestRegroupGatherCapScales()
{
    CHECK(RegroupGatherCap(Party(0)) == kRegroupGatherMs);
    Facts far = Party(0);
    far.maxMemberYd = 273.7f;
    CHECK(RegroupGatherCap(far) > kRegroupGatherMs && RegroupGatherCap(far) == 91233u);
    far.maxMemberYd = 2000.0f;
    CHECK(RegroupGatherCap(far) == kRegroupGatherMaxMs);
    // A walker 273.7 yd out (its summon failed) is not paused on at 60 s.
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 40), PullStep(60, {1})};
    Facts f;
    Run r = DeathsInAWalk(plan, f, {"Quelinator"});
    f.dead = 0;
    f.alive = 5;
    f.deadNames.clear();
    f.members = {Mem("Tanky", 0), Mem("Quelinator", 273.7f)};
    f.maxMemberYd = 273.7f;
    CHECK(Tick(r, f) == "summon Quelinator");
    OrderRefused(r, "summon Quelinator", "unavailable: in combat");
    CHECK(Tick(r, f) == "move 0.0 0.0 0.0");
    std::uint32_t ms = 0;
    for (; r.state == State::Running && ms < kRegroupGatherMaxMs; ms += kTickMs)
    {
        f.members[1].x += (ms / kTickMs % 2 ? 3.0f : -3.0f);
        CHECK(Tick(r, f).find("summon") == std::string::npos);
    }
    CHECK(r.state == State::Paused && ms > kRegroupGatherMs + 20000 && r.note.find("within 91 s") != std::string::npos);
    // A bot whose summon is on its way does not count against the ceiling.
    Run p = DeathsInAWalk(plan, f, {"Quelinator"});
    f.dead = 0;
    f.alive = 5;
    f.deadNames.clear();
    f.members = {Mem("Tanky", 0), Mem("Quelinator", 50)};
    f.maxMemberYd = 50;
    p.regroupGatherSent = true;
    p.regroupMs = kRegroupGatherMs + 1000;
    CHECK(Tick(p, f) == "summon Quelinator");
    for (int i = 0; i < 5; ++i)
        CHECK(Tick(p, f).empty());
    CHECK(p.state == State::Running);
}

static void TestEmptyPlan()
{
    DungeonPlan plan;
    Run r = Start(&plan);
    CHECK(r.state == State::Done);
    Facts f = Party(0);
    CHECK(Tick(r, f).empty());
}

// (a) Uldaman 19z11 01:59-02:04: the revived priest walked part of the way
// (more than kStragglerGainYd since the gather began), then stood at the
// scorpid pit's navmesh hole (-163.5 298.7). The regroup's ceiling skipped
// her and paused. A bot that stands still now is summoned, once per regroup;
// then one more gather window, then the pause.
static void TestStuckStragglerThatMovedIsSummoned()
{
    DungeonPlan plan;
    plan.steps = {PullStep(10, {1})};
    Run r = Start(&plan);
    BeginRegroup(r);
    Facts f = Party(0);
    f.maxMemberYd = 110;
    f.members = {Mem("Tanky", 0), Mem("Fyran", 110)};
    CHECK(Tick(r, f) == "move 0.0 0.0 0.0"); // the regroup's gather: first sample at 110 yd
    // She walks 30 yd, then stands at the hole, 80 yd out.
    f.members = {Mem("Tanky", 0), Mem("Fyran", 80)};
    f.maxMemberYd = 80;
    std::string order;
    for (std::uint32_t i = 0; i < kRegroupGatherMs / kTickMs + 2 && order.empty(); ++i)
        order = Tick(r, f);
    CHECK(order == "summon Fyran");
    CHECK(r.state == State::Running && r.regroup && r.note.find("has not moved") != std::string::npos);
    // Bounded: one summon per bot per regroup; the next ceiling pauses.
    order.clear();
    for (std::uint32_t i = 0; i < kRegroupGatherMs / kTickMs + 2 && r.state == State::Running; ++i)
        CHECK(Tick(r, f).find("summon") == std::string::npos);
    CHECK(r.state == State::Paused && r.note.find("did not gather") != std::string::npos);
    // A bot still on its way (moving every tick) is not summoned.
    Run w = Start(&plan);
    BeginRegroup(w);
    Facts g = Party(0);
    g.maxMemberYd = 110;
    g.members = {Mem("Tanky", 0), Mem("Walky", 110)};
    Tick(w, g);
    g.members[1].yd = 80;
    for (std::uint32_t i = 0; i < kRegroupGatherMs / kTickMs + 2 && w.state == State::Running; ++i)
    {
        g.members[1].x += (i % 2 ? 3.0f : -3.0f);
        CHECK(Tick(w, g).find("summon") == std::string::npos);
    }
    CHECK(w.state == State::Paused);
}

// Zul'Farrak r3-19z20 (PLAN-ZF-ZUMRAH-WALKBACK B-FIX-1/2): after the wipe the
// walk back pulled a Shadowhunter 7246 that stood on a ~3 x 3 yd navmesh island
// at 1588-1591 1044-1047; the tank chased it onto the island and from there
// every walk-back point was refused (no path) or an approach hop of zero length:
// 23 points passed in 13 minutes, 8 hops at the step, a pause, and a resume that
// did the same again. The slice: steps 63-67 of the plan's route and the
// Zum'rah step after them.
static DungeonPlan ZfIslandPlan()
{
    DungeonPlan plan;
    PlanStep pull = PullStep(1903, {7271});
    pull.y = 1014;
    pull.z = 11.8f;
    plan.steps = {Step(PlanStepKind::Move, 1603, 1043, 9.8f), Step(PlanStepKind::Move, 1584, 1050, 9.0f),
        Step(PlanStepKind::Move, 1588, 1080, 9.1f), Step(PlanStepKind::Move, 1584, 1088, 9.1f),
        Step(PlanStepKind::Move, 1633.6f, 1130.7f, 9.9f), pull};
    return plan;
}

// The lead bot on the island: the glue's path toward the point is partial and
// ends 1.1 yd from it (its approach hop with it).
static Facts OnIsland()
{
    Facts f = Party(0);
    f.leadX = 1589.6f;
    f.leadY = 1046.9f;
    f.leadZ = 8.9f;
    f.seen = false;
    f.approachKnown = true;
    f.approachX = 1590.7f;
    f.approachY = 1046.7f;
    f.approachZ = 8.9f;
    f.leadIsland = true;
    f.goodSpotCut = true; // the second sign: the main mesh a few yards off cannot be reached
    return f;
}

static void TestLeadOnIslandReading()
{
    CHECK(LeadOnIsland(false, true, 1.1f, 315.1f));             // partial, ends at the lead bot
    CHECK(LeadOnIsland(false, true, 3.05f, 315.1f));            // ... or across the island (B5: step 65, 1591.2 1044.3)
    CHECK(LeadOnIsland(false, true, kGoodPathMinYd, 315.1f));
    CHECK(!LeadOnIsland(false, true, kGoodPathMinYd + 0.1f, 315.1f)); // partial and gets somewhere: the length cap
    CHECK(!LeadOnIsland(false, false, 1.1f, 315.1f));           // complete
    CHECK(!LeadOnIsland(false, true, -1.0f, 315.1f));           // not measured
    CHECK(!LeadOnIsland(false, true, 1.1f, kArriveYd));         // at the point already
    CHECK(!LeadOnIsland(false, true, 6.0f, 9.0f));              // the path ends at the point: not short of it
    // No path (r9-19z26 Uldaman: a partial path inside the smooth-path slop comes back NOPATH, its end the
    // point itself, 59.4 yd off for step 127 and 9.9 yd for step 124): one reading, however far its end.
    CHECK(LeadOnIsland(true, false, 59.4f, 59.4f));
    CHECK(LeadOnIsland(true, false, 9.9f, 9.9f));
    CHECK(LeadOnIsland(true, false, -1.0f, kArriveYd + 0.1f));
    CHECK(!LeadOnIsland(true, false, kArriveYd, kArriveYd));     // at the point already
    CHECK(LeadPathGood(false, false, 0.5f));
    CHECK(LeadPathGood(false, true, kGoodPathMinYd + 0.1f));
    CHECK(!LeadPathGood(false, true, kGoodPathMinYd));
    CHECK(!LeadPathGood(false, true, 3.05f));            // the 3.05 yd path across the island is no good path
    CHECK(!LeadPathGood(true, false, 50.0f));
    CHECK(kGoodPathMinYd > kIslandSizeYd + kArriveYd);
    // The ring search: the path must lead on from the spot AND from the lead bot
    // (a spot off the mesh snaps onto the island: its path ends at the lead bot).
    CHECK(RingSpotLeadsOn(false, true, kGoodPathMinYd + 1, kGoodPathMinYd + 1));
    CHECK(!RingSpotLeadsOn(false, true, kGoodPathMinYd + 1, 2.0f));
    CHECK(!RingSpotLeadsOn(false, true, 2.0f, kGoodPathMinYd + 1));
    CHECK(!RingSpotLeadsOn(true, false, 50.0f, 50.0f));
    // A complete path cannot start from a spot that snapped onto the island:
    // accepted, its end 5 yd away (the point is near) or not.
    CHECK(RingSpotLeadsOn(false, false, 5.0f, 5.0f));
    // The last good spot is probed for the second sign only off the lead bot, near, on its level.
    CHECK(GoodSpotProbeWanted(6.0f, 0.0f) && GoodSpotProbeWanted(kGoodSpotProbeYd, -kEscapeMaxVertical));
    CHECK(!GoodSpotProbeWanted(kArriveYd, 0.0f) && !GoodSpotProbeWanted(kGoodSpotProbeYd + 1, 0.0f) &&
        !GoodSpotProbeWanted(6.0f, kEscapeMaxVertical + 1));
    // The last good spot as the hop's end: not on the lead bot, within reach.
    CHECK(GoodSpotFits(1589.6f, 1046.9f, 8.9f, 1584.8f, 1040.2f, 8.9f));
    CHECK(!GoodSpotFits(1589.6f, 1046.9f, 8.9f, 1590.6f, 1046.9f, 8.9f)); // 1 yd: the island itself
    CHECK(!GoodSpotFits(0, 0, 0, kEscapeMaxYd + 0.5f, 0, 0));
    CHECK(!GoodSpotFits(0, 0, 0, 5, 0, kEscapeMaxVertical + 0.5f));
}

static void TestWalkBackEscapesNavmeshIsland()
{
    DungeonPlan plan = ZfIslandPlan();
    Run r = Start(&plan, 5);
    r.walkBack = true;
    r.walkBackIdx = r.walkBackStart = 4;
    // On the main mesh (where the forward run's tank stood): the hop toward
    // step 5's point is a real one, and this spot is the last good one.
    Facts f = Party(0);
    f.leadX = 1584.8f;
    f.leadY = 1040.2f;
    f.leadZ = 8.9f;
    f.seen = false;
    f.approachKnown = true;
    f.approachX = 1600;
    f.approachY = 1080;
    f.approachZ = 9;
    f.leadPathGood = true;
    CHECK(Tick(r, f) == "move 1600.0 1080.0 9.0");
    CHECK(r.goodLeadKnown && r.goodLeadX == 1584.8f && r.goodLeadY == 1040.2f && r.goodLeadZ == 8.9f);
    // A fight on the way took the tank onto the island.
    f.anyInCombat = true;
    Tick(r, f);
    Facts g = OnIsland();
    CHECK(Tick(r, g) == "escape from 1589.6 1046.9 8.9 to 1584.8 1040.2 8.9 toward 1633.6 1130.7 9.9");
    CHECK(r.note.find("navmesh island at 1589.6 1046.9 8.9") != std::string::npos);
    CHECK(r.note.find("(1 of 2)") != std::string::npos && r.note.find("on to the next") == std::string::npos);
    CHECK(r.state == State::Running && r.walkBackIdx == 4 && r.approachHops == 1); // no hop counted for it
    // The hop is under way: nothing else goes out.
    g.leadMoving = true;
    CHECK(Tick(r, g).empty());
    CHECK(r.note.find("hops off the navmesh island") != std::string::npos);
    // At the good spot, standing: the hop is over; the walk back goes on.
    f.anyInCombat = false;
    CHECK(Tick(r, f).empty());
    CHECK(!r.escapeOn && r.islandEscapes == 0);
    CHECK(Tick(r, f) == "move 1600.0 1080.0 9.0");
    CHECK(r.note == "walk back: approaching the point of step 5" && r.walkBackIdx == 4);
}

static void TestIslandPausesAfterTwoEscapes()
{
    DungeonPlan plan = ZfIslandPlan();
    Run r = Start(&plan, 5);
    r.walkBack = true;
    r.walkBackIdx = r.walkBackStart = 4;
    // A good spot 19.6 yd away is not hopped to straight: the glue looks around the lead bot.
    r.goodLeadKnown = true;
    r.goodLeadX = 1570;
    r.goodLeadY = 1046.9f;
    r.goodLeadZ = 8.9f;
    Facts g = OnIsland();
    CHECK(Tick(r, g) == "escape from 1589.6 1046.9 8.9 toward 1633.6 1130.7 9.9");
    CHECK(Tick(r, g).empty());
    CHECK(Tick(r, g).empty()); // the lead bot stands still: the hop is over
    // Still on the island: the second hop.
    CHECK(Tick(r, g) == "escape from 1589.6 1046.9 8.9 toward 1633.6 1130.7 9.9");
    CHECK(r.note.find("(2 of 2)") != std::string::npos);
    CHECK(Tick(r, g).empty());
    CHECK(Tick(r, g).empty());
    CHECK(Tick(r, g).empty());
    CHECK(r.state == State::Paused);
    CHECK(r.note.find("the lead bot stands on a navmesh island at 1589.6 1046.9 8.9: no path toward the point of step 5") == 0);
    CHECK(r.walkBackIdx == 4); // no point passed
}

// Zul'Farrak r6-19z24 (PLAN-ZF-ISLAND-R6): going FORWARD, step 64's hop pull of
// the Shadowhunter 7246 at 1591.0 1047.7 left the tank on the island
// (1591.2 1044.3); 'back to the stand point refused (unreachable)', then
// `cmd move` to step 64's point (6-9 yd away, in sight) refused = pause 1, and
// step 65's the same = pause 2. The island escape covered only the walk back
// and the approach hop of a point out of sight.
static void TestForwardMoveEscapesNavmeshIsland()
{
    DungeonPlan plan = ZfIslandPlan();
    Run r = Start(&plan, 1); // step 64: move 1584 1050 9.0, in sight
    // At the stand of the pull before it (main mesh): the path to the point is good.
    Facts f = Party(0);
    f.leadX = 1583.7f;
    f.leadY = 1040.3f;
    f.leadZ = 8.9f;
    f.leadPathGood = true;
    f.anyInCombat = true; // the hop pull's fight
    Tick(r, f);
    CHECK(r.goodLeadKnown && r.goodLeadX == 1583.7f && r.goodLeadY == 1040.3f);
    // The fight took the tank onto the island; its walk back to the stand was refused.
    r.guardReturn = true;
    r.guardX = 1583.7f;
    r.guardY = 1040.3f;
    r.guardZ = 8.9f;
    OrderRefused(r, MoveOrder(1583.7f, 1040.3f, 8.9f, false),
        "unreachable: The lead bot has no walkable path to that point (no navmesh route reaches it).");
    CHECK(r.state == State::Running && r.note.find("back to the stand point refused") == 0);
    Facts g = Party(0);
    g.leadX = 1591.2f;
    g.leadY = 1044.3f;
    g.leadZ = 8.9f;
    g.leadIsland = true;  // the path toward the step's point ends on the island
    g.goodSpotCut = true; // ... and so does the one toward the last good spot (the second sign)
    CHECK(Tick(r, g) == "escape from 1591.2 1044.3 8.9 to 1583.7 1040.3 8.9 toward 1584.0 1050.0 9.0");
    CHECK(r.note.find("navmesh island at 1591.2 1044.3 8.9 (no path toward the point of step 2)") != std::string::npos);
    CHECK(r.state == State::Running && r.step == 1);
    g.leadMoving = true;
    CHECK(Tick(r, g).empty()); // the hop under way
    // At the good spot, standing: the hop is over; the step's move goes out.
    f.anyInCombat = false;
    CHECK(Tick(r, f).empty());
    CHECK(!r.escapeOn && r.islandEscapes == 0);
    CHECK(Tick(r, f) == "move 1584.0 1050.0 9.0");
}

// Uldaman r16-19z34 slot 5, step 39 (UNREACHABLE-ULDAMAN-S39-2026-10-22): the
// guard return walked a partial path to its end and left the tank on the dead-end
// tip of the plateau's ramp at -159.7 287.2 -50.2, 2.4 yd above the floor and not
// linked to it. Its walk back to the stand point (the step's own point, the stay
// anchor) was refused unreachable ('going on from here'), then the step's own move
// to the same point was refused with no ring point either: a pause, and a resume
// paused again. The one reading said island; no second sign came, so the island
// escape never ran. The slice: step 38's stand, step 39's point, a point after it.
static DungeonPlan UldamanS39Plan()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, -174.5f, 308.5f, -53.4f), Step(PlanStepKind::Move, -163.5f, 298.7f, -52.8f),
        Step(PlanStepKind::Move, -150.0f, 280.0f, -46.0f)};
    return plan;
}

// The tank on the ramp's tip: the path toward step 39's point is partial and ends
// 0.03 yd from it, 12.4 yd short of the point (one sign); no fresh good spot.
static Facts OnRampTip()
{
    Facts g = Party(0);
    g.leadX = -159.7f;
    g.leadY = 287.2f;
    g.leadZ = -50.2f;
    g.leadIsland = LeadOnIsland(false, true, 0.03f, 12.4f);
    return g;
}

// CommanderMove's answer for the r16 refusals: the path was incomplete and ended 0.03 yd from the lead bot
// (RemoteCommander::NoProgressFromLead), so the glue marks it (kLeadNoProgressMark).
static const std::string kNoWalkablePath =
    std::string("unreachable: The lead bot has no walkable path to that point (no navmesh route reaches it). ") +
    kLeadNoProgressMark + ".";
// The same code without that evidence: a NOPATH (no polygon at the point), or an incomplete path that made real
// progress toward a point behind a closed door or on a patch of mesh it does not reach.
static const char* const kBareUnreachable =
    "unreachable: The lead bot has no walkable path to that point (no navmesh route reaches it).";
static const char* const kNoRingPointEither = " (no reachable point within 15 yd of it either)";

static void SetGuardReturn(Run& r, float x, float y, float z)
{
    r.guardReturn = true;
    r.guardX = x;
    r.guardY = y;
    r.guardZ = z;
}

static void TestR16StandRefusalHopsOffIsland()
{
    DungeonPlan plan = UldamanS39Plan();
    Facts g = OnRampTip();
    CHECK(g.leadIsland && !g.goodSpotCut);
    Run r = Start(&plan, 1);
    SetGuardReturn(r, -163.5f, 298.7f, -52.8f); // the stay anchor: step 39's own point
    OrderRefused(r, MoveOrder(-163.5f, 298.7f, -52.8f, false), kNoWalkablePath);
    CHECK(r.state == State::Running && !r.guardReturn && r.islandSuspect && r.islandRefusals == 1);
    CHECK(r.note.find("back to the stand point refused (unreachable") == 0);
    CHECK(r.note.find("navmesh island") != std::string::npos && r.note.find("going on from here") == std::string::npos);
    // The next tick hops the tank off the tip instead of sending the same move.
    CHECK(Tick(r, g) == "escape from -159.7 287.2 -50.2 toward -163.5 298.7 -52.8");
    CHECK(r.escapeFromMove && r.stepEscapes == 1 && r.note.find("(1 of 2)") != std::string::npos);
}

static void TestR16StepMoveRefusalHopsOffIsland()
{
    DungeonPlan plan = UldamanS39Plan();
    Facts g = OnRampTip();
    Run r = Start(&plan, 1);
    // One sign: the move goes out (r16 at 1134.3 s) and is refused; the glue's detour search found no ring point.
    std::string const move = Tick(r, g);
    CHECK(move == "move -163.5 298.7 -52.8");
    CHECK(r.islandEvidence && !r.escapeOn);
    CHECK(RefusedFromIsland(r, move, kNoWalkablePath));
    CHECK(r.state == State::Running && r.islandSuspect && r.islandRefusals == 1 && r.phase == Phase::Begin);
    CHECK(r.note.find("cmd move -163.5 298.7 -52.8 refused (unreachable") == 0);
    CHECK(r.note.find("navmesh island") != std::string::npos);
    std::string const escape = Tick(r, g);
    CHECK(escape == "escape from -159.7 287.2 -50.2 toward -163.5 298.7 -52.8");
    // The hop finds no spot (the 2.4 yd drop off the tip may fail the line check): the move goes out once more
    // from that spot, and its refusal pauses with the old text: no second hop, no loop.
    OrderRefused(r, escape, "no-spot: no spot within 10 yd in the lead bot's sight has a floor, a clear line and a way on");
    CHECK(r.noSpotKnown && r.state == State::Running);
    CHECK(Tick(r, g) == move);
    CHECK(!RefusedFromIsland(r, move, kNoWalkablePath));
    OrderRefused(r, move, std::string(kNoWalkablePath) + kNoRingPointEither);
    CHECK(r.state == State::Paused);
    CHECK(r.note.find(std::string("cmd move -163.5 298.7 -52.8 refused: ") + kNoWalkablePath + kNoRingPointEither) == 0);
    // The open risk, not proven either way offline: the 3 yd ring spot west of the tip, on the floor at
    // -162.7 287.2 -52.7 (its path to the point is complete), passes the line check only if the floor read at the
    // first sample (1.5 yd out) is within kSegmentRiseYd of the tank's -50.2: the floor's -52.3 there is 2.1 yd down.
    // The one-sided limit: that spot leads on (RingSpotLeadsOn), so the 2.1 yd step down passes (kSegmentDropYd);
    // the last good spot's line (no leads-on read) keeps kSegmentRiseYd both ways.
    std::vector<SegmentSample> drop = EscapeSegmentSamples(-159.7f, 287.2f, -50.2f, -162.7f, 287.2f, -52.7f);
    CHECK(drop.size() == 2);
    for (SegmentSample& sample : drop)
    {
        sample.ground = true;
        sample.groundZ = sample.x > -162.0f ? -52.3f : -52.7f;
    }
    CHECK(RingSpotLeadsOn(false, false, 0.0f, 0.0f)); // its path to the point is complete
    CHECK(CheckEscapeSegment(-50.2f, drop, false, true) == SegmentVerdict::Ok);
    CHECK(CheckEscapeSegment(-50.2f, drop, false, false) == SegmentVerdict::Step);
    drop[0].groundZ = -51.5f; // the ramp's own slope under the first sample
    CHECK(CheckEscapeSegment(-50.2f, drop, false, false) == SegmentVerdict::Ok);
    // The walk back to the stand refused at a no-spot spot: 'going on from here', as before.
    Run n = Start(&plan, 1);
    Tick(n, g);
    n.noSpotKnown = true;
    n.noSpotX = g.leadX;
    n.noSpotY = g.leadY;
    n.noSpotZ = g.leadZ;
    SetGuardReturn(n, -163.5f, 298.7f, -52.8f);
    OrderRefused(n, move, kNoWalkablePath);
    CHECK(!n.islandSuspect && n.note.find("going on from here") != std::string::npos);
}

// After the caps (kIslandEscapes, kStepEscapes, and kStepEscapes refusals taken as
// the second sign in one step) a path refusal pauses with the existing text.
static void TestR16RefusalAfterTheCapsPauses()
{
    DungeonPlan plan = UldamanS39Plan();
    Facts g = OnRampTip();
    std::string const move = "move -163.5 298.7 -52.8";
    std::string const paused = std::string("cmd ") + move + " refused: " + kNoWalkablePath + kNoRingPointEither;
    for (int cap = 0; cap < 2; ++cap)
    {
        Run c = Start(&plan, 1);
        CHECK(Tick(c, g) == move);
        if (cap == 0)
            c.islandEscapes = kIslandEscapes;
        else
            c.stepEscapes = kStepEscapes;
        CHECK(!RefusedFromIsland(c, move, kNoWalkablePath) && !c.islandSuspect);
        OrderRefused(c, move, std::string(kNoWalkablePath) + kNoRingPointEither);
        CHECK(c.state == State::Paused && c.note.find(paused) == 0);
        // The walk back to the stand: 'going on from here', as before.
        Run s = Start(&plan, 1);
        s.islandEscapes = c.islandEscapes;
        s.stepEscapes = c.stepEscapes;
        SetGuardReturn(s, -163.5f, 298.7f, -52.8f);
        OrderRefused(s, move, kNoWalkablePath);
        CHECK(!s.islandSuspect && s.note.find("going on from here") != std::string::npos);
    }
    // Hops that are refused "wait" (a fight, the lead bot on the move) are no tries and give the escape caps back:
    // the refusals taken as the second sign are capped on their own.
    Run w = Start(&plan, 1);
    for (std::uint32_t i = 0; i < kStepEscapes; ++i)
    {
        CHECK(Tick(w, g) == move);
        CHECK(RefusedFromIsland(w, move, kNoWalkablePath));
        std::string const escape = Tick(w, g);
        CHECK(escape.compare(0, 12, "escape from ") == 0);
        OrderRefused(w, escape, "wait: a party bot is in combat");
        CHECK(w.stepEscapes == 0 && w.islandEscapes == 0);
    }
    CHECK(Tick(w, g) == move);
    CHECK(!RefusedFromIsland(w, move, kNoWalkablePath));
    OrderRefused(w, move, std::string(kNoWalkablePath) + kNoRingPointEither);
    CHECK(w.state == State::Paused && w.note.find(paused) == 0);
    // The next step has fresh refusals.
    Run x = Start(&plan, 1);
    x.islandRefusals = kStepEscapes;
    NextStep(x, "arrived");
    CHECK(x.islandRefusals == 0);
}

// Any other refusal is unchanged: not a path refusal of the lead bot's (lead-unavailable, off-mesh: the point is off
// the walkable ground), a direct step, a walk back, another step kind, or a path just read good.
static void TestNonPathRefusalUnchanged()
{
    DungeonPlan plan = UldamanS39Plan();
    Facts g = OnRampTip();
    std::string const move = "move -163.5 298.7 -52.8";
    for (const char* answer : {"lead-unavailable: The lead bot is dead, on a taxi or teleporting.",
             "off-mesh: The point is not on walkable ground (the path ends more than 5 yd from it).", "busy: a task"})
    {
        Run r = Start(&plan, 1);
        CHECK(Tick(r, g) == move);
        CHECK(!RefusedFromIsland(r, move, answer) && !r.islandSuspect && r.islandRefusals == 0);
        Run s = Start(&plan, 1);
        SetGuardReturn(s, -163.5f, 298.7f, -52.8f);
        OrderRefused(s, move, answer);
        CHECK(s.state == State::Running && !s.islandSuspect);
        CHECK(s.note == std::string("back to the stand point refused (") + answer + "): going on from here");
        CHECK(Tick(s, g) == move);
    }
    // Another order than the step's own move (a gather, an approach hop) is not the glue's to take.
    Run o = Start(&plan, 1);
    CHECK(!RefusedFromIsland(o, "move -160.0 290.0 -50.0", kNoWalkablePath) && !o.islandSuspect);
    // A direct step: its line crosses a navmesh hole; no island reading.
    DungeonPlan direct = plan;
    direct.steps[1].kind = PlanStepKind::MoveDirect;
    Run d = Start(&direct, 1);
    CHECK(!RefusedFromIsland(d, MoveOrder(-163.5f, 298.7f, -52.8f, true), kNoWalkablePath) && !d.islandSuspect);
    Run dg = Start(&direct, 1);
    SetGuardReturn(dg, -163.5f, 298.7f, -52.8f);
    OrderRefused(dg, move, kNoWalkablePath);
    CHECK(!dg.islandSuspect && dg.note.find("going on from here") != std::string::npos);
    // A walk back: its own rules (PassWalkBackPoint).
    Run b = Start(&plan, 2);
    b.walkBack = true;
    b.walkBackIdx = b.walkBackStart = 1;
    CHECK(!RefusedFromIsland(b, MoveOrder(-150.0f, 280.0f, -46.0f, false), kNoWalkablePath) && !b.islandSuspect);
    // A pull step's guard return: going on from here.
    DungeonPlan pull = plan;
    pull.steps[1] = PullStep(-163.5f, {7022});
    Run p = Start(&pull, 1);
    SetGuardReturn(p, -163.5f, 298.7f, -52.8f);
    OrderRefused(p, move, kNoWalkablePath);
    CHECK(!p.islandSuspect && p.note.find("going on from here") != std::string::npos);
    // The last read path got somewhere: not an island.
    Run k = Start(&plan, 1);
    k.leadPathGoodNow = true;
    CHECK(!RefusedFromIsland(k, move, kNoWalkablePath) && !k.islandSuspect);
    SetGuardReturn(k, -163.5f, 298.7f, -52.8f);
    OrderRefused(k, move, kNoWalkablePath);
    CHECK(!k.islandSuspect && k.note.find("going on from here") != std::string::npos);
    // A paused run takes nothing.
    Run q = Start(&plan, 1);
    q.state = State::Paused;
    CHECK(!RefusedFromIsland(q, move, kNoWalkablePath));
}

// Codex review of 51fc904f: a bare `unreachable` is no source-side evidence. From a lead bot on the main floor the
// same code comes back for a point with no polygon (NOPATH), behind a closed door, or on a patch the path got well
// toward: no hop, the old pause and 'going on from here', also after a resume. The r16 refusal carries the mark.
static void TestBareUnreachableIsNoIslandSign()
{
    // Where the glue tells them apart (CommanderMove marks only the no-progress one).
    RemoteCommander::MoveFacts m;
    m.distanceFromLead = 12.4f;
    m.maxDistance = 100.0f;
    m.fromRunLeader = true;
    m.pathType = RemoteCommander::kPathIncomplete;
    m.endFromLead = 0.03f; // r16: the path ends at the lead bot
    CHECK(RemoteCommander::CheckMove(m) == RemoteCommander::MoveVerdict::NoPath && RemoteCommander::NoProgressFromLead(m));
    m.pathType = RemoteCommander::kPathNoPath; // no polygon at the point
    CHECK(RemoteCommander::CheckMove(m) == RemoteCommander::MoveVerdict::NoPath && !RemoteCommander::NoProgressFromLead(m));
    m.pathType = RemoteCommander::kPathIncomplete; // a closed door: real progress, then the end
    m.endFromLead = 9.0f;
    m.endOffset = 6.0f;
    m.fromRunLeader = false; // (the run leader's own move with progress is accepted: no refusal at all)
    CHECK(RemoteCommander::CheckMove(m) == RemoteCommander::MoveVerdict::NoPath && !RemoteCommander::NoProgressFromLead(m));
    m.endFromLead = -1.0f; // not measured
    CHECK(!RemoteCommander::NoProgressFromLead(m));
    CHECK(LeadNoProgressRefusal(kNoWalkablePath) && !LeadNoProgressRefusal(kBareUnreachable));
    CHECK(!LeadNoProgressRefusal(std::string("off-mesh: x. ") + kLeadNoProgressMark));

    DungeonPlan plan = UldamanS39Plan();
    std::string const move = "move -163.5 298.7 -52.8";
    // The lead bot on the main floor near step 38's stand; its one reading toward the point said nothing.
    Facts f = Party(0);
    f.leadX = -170.0f;
    f.leadY = 305.0f;
    f.leadZ = -53.0f;
    // The step's move refused bare: the old pause; resumed (the walk back to step 38's stand, then the move), the
    // same refusal pauses again, and no tick in between sends a hop.
    Run r = Start(&plan, 1);
    CHECK(Tick(r, f) == move);
    CHECK(!RefusedFromIsland(r, move, kBareUnreachable) && !r.islandSuspect);
    OrderRefused(r, move, std::string(kBareUnreachable) + kNoRingPointEither);
    CHECK(r.state == State::Paused && r.note.find("cmd " + move + " refused: " + kBareUnreachable) == 0);
    CHECK(Resume(r));
    Facts atStand = f;
    atStand.leadX = -174.5f;
    atStand.leadY = 308.5f;
    atStand.leadZ = -53.4f;
    std::string sent;
    for (int i = 0; i < 40 && sent != move && r.state == State::Running; ++i)
    {
        sent = Tick(r, i == 0 ? f : atStand);
        CHECK(sent.find("escape") == std::string::npos);
    }
    CHECK(sent == move && !r.islandSuspect);
    CHECK(!RefusedFromIsland(r, move, kBareUnreachable));
    OrderRefused(r, move, std::string(kBareUnreachable) + kNoRingPointEither);
    CHECK(r.state == State::Paused && r.note.find("cmd " + move + " refused: " + kBareUnreachable) == 0);
    // The walk back to the stand refused bare: 'going on from here', and the next tick sends the move, not a hop.
    Run s = Start(&plan, 1);
    CHECK(Tick(s, f) == move);
    SetGuardReturn(s, -163.5f, 298.7f, -52.8f);
    OrderRefused(s, move, kBareUnreachable);
    CHECK(!s.islandSuspect && s.note.find("going on from here") != std::string::npos);
    CHECK(Tick(s, f) == move);
    // Even on the ramp's tip (one reading said island) a bare `unreachable` adds no sign.
    Run t = Start(&plan, 1);
    CHECK(Tick(t, OnRampTip()) == move);
    CHECK(!RefusedFromIsland(t, move, kBareUnreachable) && !t.islandSuspect);
    OrderRefused(t, move, std::string(kBareUnreachable) + kNoRingPointEither);
    CHECK(t.state == State::Paused);
}

// The glue's order for a refused own-step move (BotCommands.cpp, !accepted): the detour first, then
// RefusedFromIsland, then OrderRefused. `detour` is what NearestReachablePoint and UseDetour gave.
static void GlueRefused(Run& r, const std::string& order, const std::string& answer, bool detour)
{
    const PlanStep* step = Current(r);
    bool const ownMove = step && (step->kind == PlanStepKind::Move || step->kind == PlanStepKind::MoveDirect) &&
        order == MoveOrder(step->x, step->y, step->z, step->kind == PlanStepKind::MoveDirect);
    bool const unreachable = ownMove && (answer.compare(0, 11, "unreachable") == 0 || answer.compare(0, 8, "off-mesh") == 0);
    if (!(unreachable && step && !r.detour && detour) && !(unreachable && RefusedFromIsland(r, order, answer)))
        OrderRefused(r, order, answer + (unreachable ? " (no reachable point within 15 yd of it either)" : ""));
}

// Opus review of 51fc904f: the guard's walk back to the stand point is the step's own point (the stay anchor, the
// plan's z): the glue hands it to RefusedFromIsland before OrderRefused, so its guard branch never runs. The walk
// back must not go out again on the next tick (Tick walks a guard return before MoveStep): the hop does.
static void TestGuardReturnThroughTheGlue()
{
    DungeonPlan plan = UldamanS39Plan();
    Facts g = OnRampTip();
    Run r = Start(&plan, 1);
    SetGuardReturn(r, -163.5f, 298.7f, -52.8f); // plan z == guard z
    std::string const back = MoveOrder(r.guardX, r.guardY, r.guardZ, false);
    CHECK(back == MoveOrder(plan.steps[1].x, plan.steps[1].y, plan.steps[1].z, false));
    GlueRefused(r, back, kNoWalkablePath, false);
    CHECK(r.state == State::Running && r.islandSuspect && !r.guardReturn);
    CHECK(r.note.find("cmd " + back + " refused (unreachable") == 0);
    CHECK(Tick(r, g) == "escape from -159.7 287.2 -50.2 toward -163.5 298.7 -52.8");
    // A detour found: the glue's detour, no sign taken (the guard branch is not this path's).
    Run d = Start(&plan, 1);
    SetGuardReturn(d, -163.5f, 298.7f, -52.8f);
    d.detour = false;
    GlueRefused(d, back, kNoWalkablePath, true);
    CHECK(!d.islandSuspect && d.state == State::Running);
    // A bare `unreachable` through the glue: the guard branch, 'going on from here', as before.
    Run b = Start(&plan, 1);
    SetGuardReturn(b, -163.5f, 298.7f, -52.8f);
    GlueRefused(b, back, kBareUnreachable, false);
    CHECK(!b.islandSuspect && !b.guardReturn && b.state == State::Running);
    CHECK(b.note.find("going on from here") != std::string::npos);
}

// Opus review of 51fc904f: the sign taken, a fight moves the tank off the tip; the new spot's path reads good:
// the suspicion is gone and the move goes out, no hop.
static void TestGoodPathClearsTheRefusalSign()
{
    DungeonPlan plan = UldamanS39Plan();
    Facts g = OnRampTip();
    Run r = Start(&plan, 1);
    std::string const move = Tick(r, g);
    CHECK(RefusedFromIsland(r, move, kNoWalkablePath) && r.islandSuspect);
    Facts f = Party(0);
    f.leadX = -162.1f;
    f.leadY = 287.0f;
    f.leadZ = -52.7f;
    f.leadPathGood = true;
    f.anyInCombat = true;
    Tick(r, f);
    CHECK(!r.islandSuspect);
    f.anyInCombat = false;
    CHECK(Tick(r, f) == move);
    CHECK(!r.escapeOn && r.stepEscapes == 0);
}

// The detour's candidate check (NearestReachablePoint). r6-19z24: from the
// island at 1591.2 1044.3 the 3 yd ring point of step 64, 1587.0 1050.0, had a
// partial path ending at 1588.4 1046.5 (3.8 yd from it, 3.6 yd from the lead
// bot), and step 65's 10 yd ring point 1594.3 1048.0 one ending at 1590.9 1045.3
// (4.3 yd from it, 1.0 yd from the lead bot): both were taken as reachable (end
// within 5 yd of the point) and both `cmd move`s were refused, each a pause.
static void TestDetourPointReachable();

// A forward run on the main mesh: the lead bot 8 yd short of step 2's point,
// its path partial and ending 5 yd on (within 6 yd of the point: a ledge, a
// rock), and the last good spot read just before (goodSpotCut: e.g. a mob's
// body in the way). The forward check sends one hop; the glue finds no spot
// (no-spot); from that spot the move goes out as before: no second hop, no pause.
static Facts MainMeshPartial()
{
    Facts g = Party(0);
    g.leadX = 1584.0f;
    g.leadY = 1042.0f;
    g.leadZ = 9.0f;
    g.leadIsland = true;
    g.goodSpotCut = true;
    return g;
}

static void RecordGoodSpot(Run& r, float x, float y, float z)
{
    Facts f = Party(0);
    f.leadX = x;
    f.leadY = y;
    f.leadZ = z;
    f.leadPathGood = true;
    f.anyInCombat = true; // a fight tick: nothing goes out for the step
    Tick(r, f);
    CHECK(r.goodLeadKnown && r.goodLeadX == x && r.goodLeadY == y && r.goodLeadZ == z);
    CHECK(r.goodLeadMs == r.runMs && r.runMs > 0);
}

static void TestForwardEscapeNoSpotFallsBack()
{
    DungeonPlan plan = ZfIslandPlan();
    Run r = Start(&plan, 1);
    RecordGoodSpot(r, 1584.0f, 1036.0f, 9.0f);
    Facts g = MainMeshPartial();
    std::string const escape = Tick(r, g);
    CHECK(escape == "escape from 1584.0 1042.0 9.0 to 1584.0 1036.0 9.0 toward 1584.0 1050.0 9.0");
    CHECK(r.escapeFromMove);
    OrderRefused(r, escape, "no-spot: no spot within 10 yd of the lead bot leads on");
    CHECK(r.noSpotKnown && !r.escapeFromMove && r.state == State::Running);
    CHECK(Tick(r, g) == "move 1584.0 1050.0 9.0");
    CHECK(r.state == State::Running && r.islandEscapes == 1);
    // A wait refusal (a fight) is no try and leaves no mark: the hop goes out again.
    Run w = Start(&plan, 1);
    RecordGoodSpot(w, 1584.0f, 1036.0f, 9.0f);
    std::string const again = Tick(w, g);
    OrderRefused(w, again, "wait: a party bot is in combat");
    CHECK(!w.noSpotKnown && Tick(w, g) == again);
    // Any other hop off an island (the approach hop's, a walk back's) is not the
    // move check's; nor is one in the next step.
    std::string other;
    IslandEscape(w, g, 1603, 1043, 9.8f, "the next point", other);
    CHECK(!other.empty() && !w.escapeFromMove);
    w.escapeFromMove = true;
    ResetStep(w);
    CHECK(!w.escapeFromMove);
}

// A fall in a fight (a one-way drop within kGoodSpotFreshMs): the good spot is
// 6 yd above the lead bot, the path back up partial. The hop to it is sent; the
// glue's line check refuses it (a rise of more than kSegmentRiseYd per sample):
// no-spot, and the move goes out as before.
static void TestForwardEscapeAfterAFallFallsBack()
{
    DungeonPlan plan = ZfIslandPlan();
    Run r = Start(&plan, 1);
    RecordGoodSpot(r, 1584.0f, 1038.0f, 15.0f);
    Facts g = MainMeshPartial();
    std::string const escape = Tick(r, g);
    CHECK(escape == "escape from 1584.0 1042.0 9.0 to 1584.0 1038.0 15.0 toward 1584.0 1050.0 9.0");
    // What the glue's line check reads for it: the floor steps up 6 yd at once.
    std::vector<SegmentSample> line = EscapeSegmentSamples(1584.0f, 1042.0f, 9.0f, 1584.0f, 1038.0f, 15.0f);
    for (SegmentSample& sample : line)
    {
        sample.ground = true;
        sample.groundZ = sample.y <= 1040.0f ? 15.0f : 9.0f;
        sample.z = sample.groundZ;
    }
    CHECK(CheckEscapeSegment(9.0f, line, false) == SegmentVerdict::Step);
    OrderRefused(r, escape, "no-spot: no spot within 10 yd of the lead bot leads on");
    CHECK(Tick(r, g) == "move 1584.0 1050.0 9.0");
    CHECK(r.state == State::Running);
}

// Sign (a) for the forward check counts only for a good spot read within
// kGoodSpotFreshMs of run time.
static void TestForwardEscapeNeedsAFreshGoodSpot()
{
    DungeonPlan plan = ZfIslandPlan();
    Run r = Start(&plan, 1);
    RecordGoodSpot(r, 1584.0f, 1036.0f, 9.0f);
    r.runMs = r.goodLeadMs + kGoodSpotFreshMs + 1; // the spot is older than that
    CHECK(!GoodSpotFresh(r));
    Facts g = MainMeshPartial();
    CHECK(Tick(r, g) == "move 1584.0 1050.0 9.0"); // one sign only: the evidence is kept
    CHECK(r.islandEvidence && !r.escapeOn);
    Run e = Start(&plan, 1);
    RecordGoodSpot(e, 1584.0f, 1036.0f, 9.0f);
    e.runMs = e.goodLeadMs + kGoodSpotFreshMs - kTickMs; // the tick brings it to exactly the bound
    CHECK(Tick(e, g).compare(0, 7, "escape ") == 0);
    CHECK(GoodSpotFresh(e) && e.runMs - e.goodLeadMs == kGoodSpotFreshMs);
    Run n;
    CHECK(!GoodSpotFresh(n)); // none read
    // The run's own clock runs with every running tick.
    Run c = Start(&plan, 1);
    Tick(c, g, 700);
    CHECK(c.runMs == 700);
}

static void TestForwardMoveProbeGateAndCache()
{
    DungeonPlan plan = ZfIslandPlan();
    Run r = Start(&plan, 1);
    CHECK(r.phase == Phase::Begin);
    CHECK(ForwardMoveProbeWanted(r, true, true, false, 8.0f));
    CHECK(!ForwardMoveProbeWanted(r, false, true, false, 8.0f)); // not a move step
    CHECK(!ForwardMoveProbeWanted(r, true, false, false, 8.0f)); // out of sight: the approach hop reads
    CHECK(!ForwardMoveProbeWanted(r, true, true, true, 8.0f));   // a fight
    CHECK(!ForwardMoveProbeWanted(r, true, true, false, kArriveYd)); // at the point
    Run w = r;
    w.phase = Phase::Walk;
    CHECK(!ForwardMoveProbeWanted(w, true, true, false, 8.0f));
    w.phase = Phase::Rest;
    CHECK(!ForwardMoveProbeWanted(w, true, true, false, 8.0f));
    w.phase = Phase::AwaitPull;
    CHECK(!ForwardMoveProbeWanted(w, true, true, false, 8.0f));
    w = r;
    w.regroup = true;
    CHECK(!ForwardMoveProbeWanted(w, true, true, false, 8.0f));
    w = r;
    w.walkBack = true; // the walk back reads its own point, ungated as before
    CHECK(!ForwardMoveProbeWanted(w, true, true, false, 8.0f));
    w = r;
    w.state = State::Paused;
    CHECK(!ForwardMoveProbeWanted(w, true, true, false, 8.0f));

    MoveProbeCache c;
    CHECK(!MoveProbeHit(c, 1, 1591.2f, 1044.3f, 1584, 1050, 9, 0));
    CHECK(!MoveProbeHit(c, 0, 0.5f, 0.5f, 0, 0, 0, 100)); // nothing read: no hit, whatever matches the zeros
    StoreMoveProbe(c, 1, 1591.2f, 1044.3f, 1584, 1050, 9, 10000, true, false);
    CHECK(c.island && !c.good);
    CHECK(MoveProbeHit(c, 1, 1591.2f, 1044.3f, 1584, 1050, 9, 10000));
    CHECK(MoveProbeHit(c, 1, 1590.1f, 1045.9f, 1584, 1050, 9, 10000 + kMoveProbeCacheMs - 1)); // same 2 yd cell
    CHECK(!MoveProbeHit(c, 1, 1591.2f, 1044.3f, 1584, 1050, 9, 10000 + kMoveProbeCacheMs)); // 5 s on
    CHECK(!MoveProbeHit(c, 1, 1592.1f, 1044.3f, 1584, 1050, 9, 10001)); // another cell (x)
    CHECK(!MoveProbeHit(c, 1, 1591.2f, 1046.1f, 1584, 1050, 9, 10001)); // another cell (y)
    CHECK(!MoveProbeHit(c, 2, 1591.2f, 1044.3f, 1584, 1050, 9, 10001)); // another step
    CHECK(!MoveProbeHit(c, 1, 1591.2f, 1044.3f, 1603, 1050, 9, 10001)); // another point (x)
    CHECK(!MoveProbeHit(c, 1, 1591.2f, 1044.3f, 1584, 1043, 9, 10001)); // (y)
    CHECK(!MoveProbeHit(c, 1, 1591.2f, 1044.3f, 1584, 1050, 9.8f, 10001)); // (z)
    CHECK(!MoveProbeHit(c, 1, 1591.2f, 1044.3f, 1584, 1050, 9, 9999)); // a clock before it (a new run)
}

static void TestDetourPointReachable()
{
    CHECK(!DetourPointReachable(false, true, 3.8f, 3.6f, 20.0f));     // r6 step 64's detour 1587 1050
    CHECK(!DetourPointReachable(false, true, 4.3f, 1.0f, 20.0f));     // r6 step 65's detour 1594.3 1048
    CHECK(!DetourPointReachable(false, true, 0.5f, 2.0f, 20.0f)); // CheckMove's refusal (RemoteCommander::kNoProgressYd)
    CHECK(!DetourPointReachable(false, true, 4.0f, kGoodPathMinYd, 20.0f)); // partial and gets nowhere past the island
    CHECK(DetourPointReachable(false, true, 4.0f, kGoodPathMinYd + 0.5f, 20.0f)); // partial, leads on, ends near the point
    CHECK(!DetourPointReachable(false, true, kDetourEndYd + 0.5f, 30.0f, 20.0f)); // ... but short of it
    CHECK(DetourPointReachable(false, false, 0.3f, 1.0f, 20.0f));     // complete: accepted, however short
    CHECK(DetourPointReachable(false, false, kDetourEndYd, 1.0f, 20.0f));
    CHECK(!DetourPointReachable(false, false, kDetourEndYd + 0.5f, 20.0f, 20.0f)); // complete but ends off the point
    CHECK(!DetourPointReachable(true, false, 0.0f, 20.0f, 20.0f));    // no path
    // r9-19z26 step 124: the ring point 136 298.7 on the tank's own island, 1.3 yd from it, its path complete.
    CHECK(!DetourPointReachable(false, false, 0.0f, 1.3f, 1.3f));
    CHECK(!DetourPointReachable(false, false, 0.0f, 20.0f, kArriveYd));
    CHECK(DetourPointReachable(false, false, 0.0f, 20.0f, kArriveYd + 0.1f));
}

// Uldaman r13-19z30 slot 5/slot 1 (T3): the tank fell into the 6 x 6 yd pit at
// x 39.7-46.1, y 370.7-376.8, z -38.9 (no navmesh exit). The detour of step 96
// (50.8 379.3) took the pit's own ring point 42.1 374.3 as 'the nearest reachable
// point': the lead's path to it is complete (same polygon group), but the point's own
// path toward 50.8 379.3 is partial and ends 1.1 yd from it, in the pit.
static void TestDetourCandidateLeadsOn()
{
    float const fromPoint = 10.0f; // 42.1 374.3 is 10.0 yd from 50.8 379.3
    // The pit point: lead at 45.3 374.4 (3.2 yd away), the candidate's probe partial,
    // ending 1.1 yd from it and 1.1 + 3.2 yd from the lead.
    CHECK(DetourPointReachable(false, false, 0.0f, 20.0f, 3.2f));
    CHECK(!DetourCandidateLeadsOn(false, true, 1.1f, 4.3f, fromPoint));
    // Same pit point, lead at 42.5 376.3 (2.0 yd): refused as before by the arrive rule.
    CHECK(!DetourPointReachable(false, false, 0.0f, 20.0f, 2.0f));
    // A pit point whose probe ends in the pit at more than 10 yd from the lead and from itself would lead on
    // (the same rule as a ring spot's): the lead's own end distance stays the second gate.
    CHECK(!DetourCandidateLeadsOn(false, true, kGoodPathMinYd + 1, 3.0f, fromPoint)); // ends near the lead
    CHECK(!DetourCandidateLeadsOn(false, true, 3.0f, kGoodPathMinYd + 1, fromPoint)); // ends near the candidate
    // A main-mesh candidate outside the pit: its probe is complete -> accepted.
    CHECK(DetourCandidateLeadsOn(false, false, 0.0f, 12.0f, fromPoint));
    // ... or partial but leading on well past both.
    CHECK(DetourCandidateLeadsOn(false, true, kGoodPathMinYd + 1, kGoodPathMinYd + 1, fromPoint));
    // No path at all from the candidate: refused.
    CHECK(!DetourCandidateLeadsOn(true, false, 0.0f, 12.0f, fromPoint));
    // The plan's own point (or one within kArriveYd of it) has nothing to lead on to: not asked.
    CHECK(DetourCandidateLeadsOn(true, false, 0.0f, 0.0f, 0.0f));
    CHECK(DetourCandidateLeadsOn(false, true, 0.0f, 0.0f, kArriveYd));
    CHECK(!DetourCandidateLeadsOn(false, true, 0.0f, 0.0f, kArriveYd + 0.1f));
    // The lead itself within kArriveYd of the candidate: still refused before the probe is read.
    CHECK(!DetourPointReachable(false, false, 0.0f, 0.0f, kArriveYd));
}

// One reading is no island (a ledge, a closed door): the forward move goes out
// as before; a direct step's point never escapes; out of sight the approach hop decides.
static void TestForwardMoveIslandNeedsSecondSign()
{
    DungeonPlan plan = ZfIslandPlan();
    Run r = Start(&plan, 1);
    RecordGoodSpot(r, 1583.7f, 1040.3f, 8.9f); // fresh: sign (a) would count
    Facts g = Party(0);
    g.leadX = 1591.2f;
    g.leadY = 1044.3f;
    g.leadZ = 8.9f;
    g.leadIsland = true;
    CHECK(Tick(r, g) == "move 1584.0 1050.0 9.0");
    CHECK(r.islandEvidence && !r.escapeOn);
    // The walk under way is not cut short by a reading (the glue reads none while it walks).
    g.goodSpotCut = true;
    CHECK(r.phase == Phase::Walk && Tick(r, g).find("escape") == std::string::npos && !r.escapeOn);

    DungeonPlan direct = ZfIslandPlan();
    direct.steps[1].kind = PlanStepKind::MoveDirect;
    Run d = Start(&direct, 1);
    RecordGoodSpot(d, 1583.7f, 1040.3f, 8.9f);
    CHECK(Tick(d, g).find("escape") == std::string::npos);
    CHECK(!d.escapeOn);

    // At the point already: nothing to escape toward.
    Run a = Start(&plan, 1);
    RecordGoodSpot(a, 1583.7f, 1040.3f, 8.9f);
    Facts h = g;
    h.leadX = 1584.5f;
    h.leadY = 1049.0f;
    h.leadZ = 9.0f;
    CHECK(Tick(a, h).find("escape") == std::string::npos);
}

// Uldaman r9-19z26 (descent leg): the fight of step 124 left the tank on a patch of
// mesh at 132-139 / 296-304, z -23.8, above the -26.5 floor. From 137.3 298.7 -23.3
// every path came back NOPATH (a partial one ending 0.0-0.2 yd off, inside the smooth
// path's slop): step 124's move (141 290, 9.9 yd) and step 127's (158.8 243.8, 59.4 yd)
// were refused unreachable, the second one 'no reachable point within 15 yd of it
// either' = pause, twice. The reading said no island, so the escape never ran.
static void TestForwardMoveNoPathLedgeEscapes()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 121, 280.5f, -26.3f), Step(PlanStepKind::Move, 141, 290, -26.4f),
        Step(PlanStepKind::Move, 158.8f, 243.8f, -30.7f)};
    float const lx = 137.3f, ly = 298.7f, lz = -23.3f;
    auto onLedge = [&](float tx, float ty, float tz) {
        Facts g = Party(0);
        g.leadX = lx;
        g.leadY = ly;
        g.leadZ = lz;
        float const to = ai::PlanDistance(lx, ly, lz, tx, ty, tz);
        g.leadIsland = LeadOnIsland(true, false, to, to); // NOPATH: the end is the point itself
        g.leadPathGood = LeadPathGood(true, false, to);
        return g;
    };
    // Step 127 (here step 3): the last good spot was read at the via point 24.6 yd off,
    // 94 s before; the path from the ledge to it is partial (the second sign).
    Run r = Start(&plan, 2);
    RecordGoodSpot(r, 121, 280.5f, -26.3f);
    Facts g = onLedge(158.8f, 243.8f, -30.7f);
    CHECK(g.leadIsland && !g.leadPathGood);
    g.goodSpotCut = true;
    CHECK(Tick(r, g) == "escape from 137.3 298.7 -23.3 toward 158.8 243.8 -30.7"); // the good spot is beyond kEscapeMaxYd
    CHECK(r.note.find("navmesh island at 137.3 298.7 -23.3 (no path toward the point of step 3)") != std::string::npos);
    CHECK(r.state == State::Running && r.escapeFromMove && r.goodLeadX == 121);
    // Step 124 (here step 2) after the rest, 9.9 yd from its point: the same.
    Run s = Start(&plan, 1);
    RecordGoodSpot(s, 121, 280.5f, -26.3f);
    Facts h = onLedge(141, 290, -26.4f);
    h.goodSpotCut = true;
    CHECK(Tick(s, h) == "escape from 137.3 298.7 -23.3 toward 141.0 290.0 -26.4");
    // One sign alone (no fresh good spot, no second reading): the move goes out as before.
    Run o = Start(&plan, 2);
    Facts k = onLedge(158.8f, 243.8f, -30.7f);
    CHECK(Tick(o, k) == "move 158.8 243.8 -30.7");
    CHECK(o.islandEvidence && !o.escapeOn);
    // The documented limit: the good spot is beyond the straight hop, and a ring spot 3 yd off on the -26.5 floor
    // fails the line check (3.2 yd down at the first sample) unless the rubble's own slope steps down 2 yd or less.
    CHECK(!GoodSpotFits(lx, ly, lz, 121, 280.5f, -26.3f));
    std::vector<SegmentSample> line = EscapeSegmentSamples(lx, ly, lz, lx + 3, ly, -26.5f);
    for (SegmentSample& sample : line)
    {
        sample.ground = true;
        sample.groundZ = -26.5f;
    }
    CHECK(CheckEscapeSegment(lz, line, false) == SegmentVerdict::Step);
    line.front().groundZ = -24.6f; // a slope like the one the tank walked up in r9 (-25.8 to -24.2 over 3.5 yd)
    CHECK(CheckEscapeSegment(lz, line, false) == SegmentVerdict::Ok);
}

static void TestResumeEscapesFirst()
{
    DungeonPlan plan = ZfIslandPlan();
    Run r = Start(&plan, 5);
    r.walkBack = true;
    r.walkBackIdx = r.walkBackStart = 4;
    Facts g = OnIsland();
    for (int i = 0; i < 20 && r.state == State::Running; ++i)
        Tick(r, g);
    CHECK(r.state == State::Paused && r.note.find("navmesh island") != std::string::npos);
    // `.bot run resume`: the regroup on the lead bot, the walk back from the
    // same spot, and its first order is the hop off the island, not a hop.
    CHECK(Resume(r));
    g.members = {Mem("Tanky", 0), Mem("Healy", 4)};
    g.maxMemberYd = 4;
    CHECK(Tick(r, g).empty());
    CHECK(r.walkBack && r.walkBackIdx == 4 && !r.regroup);
    CHECK(Tick(r, g) == "escape from 1589.6 1046.9 8.9 toward 1633.6 1130.7 9.9");
    CHECK(r.islandEscapes == 1 && r.state == State::Running);
}

static void TestWalkBackPointInSightOnIsland()
{
    DungeonPlan plan = ZfIslandPlan();
    // Step 3's point is in sight: no hop is read, the glue reads the lead bot's path to it.
    Run r = Start(&plan, 5);
    r.walkBack = true;
    r.walkBackIdx = 2;
    Facts g = OnIsland();
    g.seen = true;
    g.approachKnown = false;
    CHECK(Tick(r, g) == "escape from 1589.6 1046.9 8.9 toward 1588.0 1080.0 9.1");

    // No island read (the backstop): two points refused from one spot with no
    // path there that got anywhere; the second is not passed, the lead bot is hopped off first.
    Run q = Start(&plan, 5);
    q.walkBack = true;
    q.walkBackIdx = 1;
    Facts h = g;
    h.leadIsland = false;
    std::string const unreachable =
        "unreachable: The lead bot has no walkable path to that point (no navmesh route reaches it).";
    CHECK(Tick(q, h) == "move 1584.0 1050.0 9.0");
    OrderRefused(q, "move 1584.0 1050.0 9.0", unreachable);
    CHECK(q.walkBackIdx == 2 && q.note.find("on to the next") != std::string::npos);
    CHECK(Tick(q, h) == "move 1588.0 1080.0 9.1");
    OrderRefused(q, "move 1588.0 1080.0 9.1", unreachable);
    CHECK(q.state == State::Running && q.walkBackIdx == 2 && q.islandSuspect);
    CHECK(Tick(q, h) == "escape from 1589.6 1046.9 8.9 toward 1588.0 1080.0 9.1");

    // A spot whose path read got somewhere: refused points are passed as before.
    Run o = Start(&plan, 5);
    o.walkBack = true;
    o.walkBackIdx = 1;
    Facts k = h;
    k.leadPathGood = true;
    CHECK(Tick(o, k) == "move 1584.0 1050.0 9.0");
    OrderRefused(o, "move 1584.0 1050.0 9.0", "off-mesh: The point is not on walkable ground.");
    CHECK(Tick(o, k) == "move 1588.0 1080.0 9.1");
    OrderRefused(o, "move 1588.0 1080.0 9.1", "off-mesh: The point is not on walkable ground.");
    CHECK(o.walkBackIdx == 3 && !o.islandSuspect && o.note.find("on to the next") != std::string::npos);

    // Out of sight, a hop that ends where the lead bot stands (no island read):
    // no path toward the point at once, not a hop counted (r3-19z20: 8 per point).
    Run w = Start(&plan, 5);
    w.walkBack = true;
    w.walkBackIdx = w.walkBackStart = 4;
    w.walkBackRestarted = true;
    Facts z = OnIsland();
    z.leadIsland = false;
    CHECK(Tick(w, z).empty());
    CHECK(w.approachHops == 0 && w.walkBackIdx == 5);
    CHECK(w.note == "walk back: no walkable path toward the point of step 5: on to the next");
}

// The step's own approach (r3-19z20 step 102: 8 hops of zero length, then the
// pause 315.1 yd out): on the island it hops off first; a hop that ends where
// the lead bot stands is no path at once, not 8 hops.
static void TestApproachHopIslandAndZeroHop()
{
    DungeonPlan plan = ZfIslandPlan();
    Run r = Start(&plan, 5);
    r.restDue = false;
    Facts g = OnIsland();
    CHECK(Tick(r, g) == "escape from 1589.6 1046.9 8.9 toward 1903.0 1014.0 11.8");
    CHECK(r.approachHops == 0 && r.note.find("no path toward the pack") != std::string::npos);

    Run z = Start(&plan, 5);
    z.restDue = false;
    Facts h = OnIsland();
    h.leadIsland = false;
    CHECK(Tick(z, h).empty());
    CHECK(z.state == State::Paused && z.note.find("no walkable path toward the pack") == 0);
}


// Cold review on bcc23124 (Opus M1): from the frozen spot the path to step 65
// ended at 1591.2 1044.3, 3.05 yd from the lead bot: over the old 2 yd island
// line and the old 3 yd "good path" line, so the island spot became the last
// good one and each good read reset the escape count (hop, escape, hop ...).
// With the real numbers it now reads as island and resets nothing.
static void TestIslandGapIsNotAGoodPath()
{
    float const lx = 1589.6f, ly = 1046.9f, lz = 8.9f;
    float const end = ai::PlanDistance(lx, ly, lz, 1591.2f, 1044.3f, 8.9f);
    float const toStep65 = ai::PlanDistance(lx, ly, lz, 1588, 1080, 9.1f);
    CHECK(end > 3.0f && end < 3.1f);
    DungeonPlan plan = ZfIslandPlan();
    Run r = Start(&plan, 5);
    r.walkBack = true;
    r.walkBackIdx = r.walkBackStart = 4;
    r.goodLeadKnown = true;
    r.goodLeadX = 1584.8f;
    r.goodLeadY = 1040.2f;
    r.goodLeadZ = 8.9f;
    Facts g = OnIsland();
    g.approachX = 1591.2f;
    g.approachY = 1044.3f;
    g.leadIsland = LeadOnIsland(false, true, end, toStep65);
    g.leadPathGood = LeadPathGood(false, true, end);
    CHECK(g.leadIsland && !g.leadPathGood);
    CHECK(Tick(r, g) == "escape from 1589.6 1046.9 8.9 to 1584.8 1040.2 8.9 toward 1633.6 1130.7 9.9");
    for (int i = 0; i < 2; ++i)
        CHECK(Tick(r, g).empty());
    CHECK(r.islandEscapes == 1 && r.goodLeadX == 1584.8f && r.goodLeadY == 1040.2f); // not overwritten, not reset
    CHECK(Tick(r, g).compare(0, 7, "escape ") == 0);
    CHECK(r.islandEscapes == 2);
    for (int i = 0; i < 3; ++i)
        Tick(r, g);
    CHECK(r.state == State::Paused && r.note.find("navmesh island at 1589.6 1046.9 8.9") != std::string::npos);
    // A last good spot on the lead bot itself (within kArriveYd) is not hopped to.
    Run n = Start(&plan, 5);
    n.walkBack = true;
    n.walkBackIdx = n.walkBackStart = 4;
    n.goodLeadKnown = true;
    n.goodLeadX = 1590.6f;
    n.goodLeadY = 1046.9f;
    n.goodLeadZ = 8.9f;
    CHECK(Tick(n, OnIsland()) == "escape from 1589.6 1046.9 8.9 toward 1633.6 1130.7 9.9");
}

// Codex on bcc23124: a hop refused because a fight started or the lead bot
// moved since the read ("wait: ...") is not a try; "no-spot" is.
static void TestEscapeWaitIsNotATry()
{
    DungeonPlan plan = ZfIslandPlan();
    Run r = Start(&plan, 5);
    r.walkBack = true;
    r.walkBackIdx = r.walkBackStart = 4;
    Facts g = OnIsland();
    std::string const order = Tick(r, g);
    CHECK(order.compare(0, 7, "escape ") == 0 && r.islandEscapes == 1);
    OrderRefused(r, order, "wait: a party bot is in combat");
    CHECK(r.islandEscapes == 0 && r.stepEscapes == 0 && !r.escapeOn && r.state == State::Running &&
        r.note.find("not counted") != std::string::npos);
    CHECK(Tick(r, g) == order);
    OrderRefused(r, order, "no-spot: no spot within 10 yd");
    CHECK(r.islandEscapes == 1 && r.note.find("not counted") == std::string::npos);
}

// Codex on bcc23124: the straight line is checked before the lead bot moves.
static void TestEscapeSegmentCheck()
{
    std::vector<SegmentSample> s = EscapeSegmentSamples(0, 0, 0, 10, 0, 0);
    CHECK(s.size() == 5 && s.back().x == 10.0f && s.front().x == 2.0f);
    CHECK(EscapeSegmentSamples(0, 0, 0, 30, 0, 0).size() == kSegmentSamples);
    CHECK(EscapeSegmentSamples(0, 0, 0, 0.5f, 0, 0).size() == 1);
    auto flat = [&]()
    {
        std::vector<SegmentSample> v = EscapeSegmentSamples(0, 0, 0, 10, 0, 0);
        for (SegmentSample& x : v)
        {
            x.ground = true;
            x.groundZ = 0.0f;
        }
        return v;
    };
    CHECK(CheckEscapeSegment(0, flat(), false) == SegmentVerdict::Ok);
    CHECK(CheckEscapeSegment(0, flat(), true) == SegmentVerdict::Hostile);
    CHECK(CheckEscapeSegment(0, {}, false) == SegmentVerdict::NoGround);
    std::vector<SegmentSample> v = flat();
    v[2].ground = false;
    CHECK(CheckEscapeSegment(0, v, false) == SegmentVerdict::NoGround);
    v = flat();
    v[2].groundZ = -(kSegmentGroundYd + 0.5f); // a floor far under the line (a drop)
    CHECK(CheckEscapeSegment(0, v, false) == SegmentVerdict::NoGround);
    v = flat();
    v[2].groundZ = kSegmentRiseYd + 0.5f;
    CHECK(CheckEscapeSegment(0, v, false) == SegmentVerdict::Step);
    v = flat();
    v[0].groundZ = kSegmentRiseYd + 0.5f; // the first sample against the lead bot's own height
    CHECK(CheckEscapeSegment(0, v, false) == SegmentVerdict::Step);
    v = flat();
    v[3].liquid = true;
    CHECK(CheckEscapeSegment(0, v, false) == SegmentVerdict::Liquid);
    // One-sided steps (Uldaman r16-19z34): a rise over kSegmentRiseYd is refused even where the spot leads on ...
    v = flat();
    v[0].groundZ = kSegmentRiseYd + 0.1f;
    CHECK(CheckEscapeSegment(0, v, false, true) == SegmentVerdict::Step);
    // ... a step down of 2.1-4.0 yd passes only to a spot whose path leads on ...
    for (float down : {kSegmentRiseYd + 0.1f, 3.0f, kSegmentDropYd})
    {
        v = flat();
        for (SegmentSample& x : v)
        {
            x.groundZ = -down;
            x.z = -down;
        }
        CHECK(CheckEscapeSegment(0, v, false, true) == SegmentVerdict::Ok);
        CHECK(CheckEscapeSegment(0, v, false, false) == SegmentVerdict::Step);
    }
    // ... and one over kSegmentDropYd never.
    v = flat();
    for (SegmentSample& x : v)
    {
        x.groundZ = -(kSegmentDropYd + 0.1f);
        x.z = x.groundZ;
    }
    CHECK(CheckEscapeSegment(0, v, false, true) == SegmentVerdict::Step);
    // The step down counts from sample to sample: two 3 yd steps down (6 yd in all, within kEscapeMaxVertical) pass.
    v = flat();
    for (std::size_t i = 0; i < v.size(); ++i)
    {
        v[i].groundZ = i == 0 ? -3.0f : -6.0f;
        v[i].z = v[i].groundZ;
    }
    CHECK(CheckEscapeSegment(0, v, false, true) == SegmentVerdict::Ok);
    // Uldaman r13-19z30's closed pit stays refused: 2.6 yd below the crest, its spot's own path ends 1.1 yd from it
    // (no leads-on), so the glue never asks the line with the larger drop, and the plain line check refuses it.
    CHECK(!RingSpotLeadsOn(false, true, 1.1f, 4.3f));
    std::vector<SegmentSample> pit = EscapeSegmentSamples(42.7f, 376.5f, -36.3f, 42.7f, 373.5f, -38.9f);
    for (SegmentSample& x : pit)
    {
        x.ground = true;
        x.groundZ = -38.9f;
    }
    CHECK(CheckEscapeSegment(-36.3f, pit, false, RingSpotLeadsOn(false, true, 1.1f, 4.3f)) == SegmentVerdict::Step);
    // A ramp of 1.9 yd per sample up a line to 9.5 yd: no step, too far up in all.
    v = EscapeSegmentSamples(0, 0, 0, 10, 0, 9.5f);
    for (SegmentSample& x : v)
    {
        x.ground = true;
        x.groundZ = x.z;
    }
    CHECK(CheckEscapeSegment(0, v, false) == SegmentVerdict::Climb);
    v.pop_back();
    CHECK(CheckEscapeSegment(0, v, false) == SegmentVerdict::Ok);
    CHECK(std::string(SegmentVerdictName(SegmentVerdict::Liquid)) == "liquid on the line");
}

// Opus notes on bcc23124: the island never hops a lead bot off a direct step's
// point (it may stand at its launch spot), a direct point passed clears the
// suspicion, and so does a point reached.
static void TestIslandLeavesDirectStepsAlone()
{
    DungeonPlan plan;
    PlanStep pull = PullStep(500, {1});
    plan.steps = {Step(PlanStepKind::Move, 10), Step(PlanStepKind::MoveDirect, 200), Step(PlanStepKind::Move, 220), pull};
    Run r = Start(&plan, 3);
    r.walkBack = true;
    r.walkBackIdx = r.walkBackStart = 1;
    r.islandSuspect = true;
    Facts f = Party(0);
    f.seen = false;
    f.approachKnown = true;
    f.approachX = 40;
    CHECK(Tick(r, f) == "move 40.0 0.0 0.0");
    // A crossed direct point refused: passed, and the suspicion with it.
    Run d = Start(&plan, 3);
    d.walkBack = true;
    d.walkBackIdx = 1;
    d.islandSuspect = true;
    d.routeKnown = true;
    d.checkpoint = 1;
    OrderRefused(d, "move 200.0 0.0 0.0 direct", "no-los: no line of sight");
    CHECK(d.walkBackIdx == 2 && !d.islandSuspect);
    // A point reached clears it too (the lead bot stands on it: no hop off it).
    Run q = Start(&plan, 3);
    q.walkBack = true;
    q.walkBackIdx = 0;
    q.islandSuspect = true;
    Facts a = Party(10);
    CHECK(Tick(q, a).empty());
    CHECK(q.walkBackIdx == 1 && !q.islandSuspect);
}


// Codex/Opus on 269503e0: one partial path toward one point does not prove an
// island (an unreachable ledge, a closed door, a point off the mesh). From the
// main mesh with no second sign, the walk back and the approach hop do what
// they did before (2012dfee): no escape order.
static void TestSingleIslandReadingChangesNothing()
{
    DungeonPlan plan;
    PlanStep pull = PullStep(300, {1});
    plan.steps = {Step(PlanStepKind::Move, 40), Step(PlanStepKind::Move, 200), pull};
    // A ledge point in sight: the path toward it ends 5 yd out, short of it.
    Run r = Start(&plan, 2);
    r.walkBack = true;
    r.walkBackIdx = 0;
    Facts f = Party(0);
    f.leadIsland = LeadOnIsland(false, true, 5.0f, 40.0f);
    CHECK(f.leadIsland && !f.goodSpotCut);
    CHECK(Tick(r, f) == "move 40.0 0.0 0.0");
    OrderRefused(r, "move 40.0 0.0 0.0", "unreachable: no navmesh route reaches it");
    CHECK(r.walkBackIdx == 1 && r.note.find("on to the next") != std::string::npos);
    // The next point out of sight: the hop is walked (5 yd), as before.
    Facts g = Party(0);
    g.seen = false;
    g.approachKnown = true;
    g.approachX = 5;
    g.leadIsland = LeadOnIsland(false, true, 5.0f, 200.0f);
    // ... read toward a point 160 yd from the first one from the same spot: that
    // is the second sign (b), so this check uses another spot (another cell).
    g.leadX = f.leadX = 20;
    g.approachX = 25;
    Run w = Start(&plan, 2);
    w.walkBack = true;
    w.walkBackIdx = 1;
    CHECK(Tick(w, g) == "move 25.0 0.0 0.0");
    // The approach hop of a pull step: the hop, not an escape.
    Run a = Start(&plan, 2);
    a.restDue = false;
    Facts h = Party(0);
    h.seen = false;
    h.approachKnown = true;
    h.approachX = 5;
    h.leadIsland = LeadOnIsland(false, true, 5.0f, 300.0f);
    CHECK(Tick(a, h) == "move 5.0 0.0 0.0");
    // ... and a zero-length hop with one reading only: no path, the pause (B-FIX-2), not an escape.
    Run z = Start(&plan, 2);
    z.restDue = false;
    h.approachX = 1;
    h.leadIsland = LeadOnIsland(false, true, 1.0f, 300.0f);
    CHECK(Tick(z, h).empty());
    CHECK(z.state == State::Paused && z.note.find("no walkable path toward the pack") == 0);
}

// The second sign (b) with the trace's numbers: from 1589.6 1046.9 the path to
// step 65 (1588 1080) ended 3.05 yd away, and the one to step 70 (1633.6 1130.7)
// came back partial too. The first reading changes nothing (the move goes out,
// is refused and passed); the second, toward a point 58 yd from the first, hops
// the lead bot off. Without the good spot probe (no good spot known yet).
static void TestSecondReadingConfirmsTheIsland()
{
    DungeonPlan plan;
    PlanStep pull = PullStep(1903, {7271});
    pull.y = 1014;
    pull.z = 11.8f;
    plan.steps = {Step(PlanStepKind::Move, 1603, 1043, 9.8f), Step(PlanStepKind::Move, 1588, 1080, 9.1f),
        Step(PlanStepKind::Move, 1633.6f, 1130.7f, 9.9f), pull};
    Run r = Start(&plan, 3);
    r.walkBack = true;
    r.walkBackIdx = 1;
    Facts g = OnIsland();
    g.goodSpotCut = false;
    g.seen = true;
    g.approachKnown = false;
    g.leadIsland = LeadOnIsland(false, true, 3.05f, ai::PlanDistance(1589.6f, 1046.9f, 8.9f, 1588, 1080, 9.1f));
    CHECK(Tick(r, g) == "move 1588.0 1080.0 9.1");
    CHECK(r.islandEvidence);
    OrderRefused(r, "move 1588.0 1080.0 9.1", "unreachable: no navmesh route reaches it");
    CHECK(r.walkBackIdx == 2);
    Facts h = OnIsland();
    h.goodSpotCut = false;
    h.approachX = 1591.2f;
    h.approachY = 1044.3f;
    h.leadIsland = LeadOnIsland(false, true, 3.05f, ai::PlanDistance(1589.6f, 1046.9f, 8.9f, 1633.6f, 1130.7f, 9.9f));
    CHECK(Tick(r, h) == "escape from 1589.6 1046.9 8.9 toward 1633.6 1130.7 9.9");
    // The same point read again is no second sign.
    Run s = Start(&plan, 3);
    s.walkBack = true;
    s.walkBackIdx = 2;
    s.walkBackStart = 2;
    s.walkBackRestarted = true;
    Facts k = h;
    k.approachX = 1600; // a hop that goes somewhere
    k.approachY = 1060;
    CHECK(Tick(s, k) == "move 1600.0 1060.0 8.9");
    EnterPhase(s, Phase::Begin); // the hop did not move it: the same point is read again
    CHECK(Tick(s, k) == "move 1600.0 1060.0 8.9");
    CHECK(s.islandEvidence && s.state == State::Running);
    // The evidence goes with the spot: the lead bot moved out of the cell, and a
    // reading there toward the other point is a first one again.
    Run m = Start(&plan, 3);
    m.walkBack = true;
    m.walkBackIdx = 1;
    CHECK(Tick(m, g) == "move 1588.0 1080.0 9.1");
    OrderRefused(m, "move 1588.0 1080.0 9.1", "unreachable: no navmesh route reaches it");
    Facts moved = h;
    moved.leadX = 1580.0f; // 9.6 yd west: another cell
    moved.approachX = 1590;
    moved.approachY = 1060;
    CHECK(Tick(m, moved) == "move 1590.0 1060.0 8.9");
    CHECK(m.evidenceX == 1580.0f);
    Facts away = moved; // no reading on this tick: the old cell's evidence is gone all the same
    away.leadIsland = false;
    away.leadX = 1570.0f;
    Run e = Start(&plan, 3);
    e.walkBack = true;
    e.walkBackIdx = 1;
    CHECK(Tick(e, g) == "move 1588.0 1080.0 9.1");
    CHECK(e.islandEvidence);
    EnterPhase(e, Phase::Begin);
    Tick(e, away);
    CHECK(!e.islandEvidence);
    // In the same cell but 2.7 yd from the first reading's spot: no second sign.
    Run n = Start(&plan, 3);
    n.walkBack = true;
    n.walkBackIdx = 1;
    CHECK(Tick(n, g) == "move 1588.0 1080.0 9.1");
    OrderRefused(n, "move 1588.0 1080.0 9.1", "unreachable: no navmesh route reaches it");
    Facts near = h;
    near.leadY = 1044.2f;
    near.approachX = 1600;
    near.approachY = 1060;
    CHECK(IslandCell(near.leadY) == IslandCell(1046.9f));
    CHECK(Tick(n, near) == "move 1600.0 1060.0 8.9");
    // That reading from 2.7 yd off does not replace the first: back on the first
    // spot, the reading toward the other point is the second sign.
    CHECK(n.evidenceY == 1046.9f);
    EnterPhase(n, Phase::Begin);
    Facts back = h;
    back.approachX = 1600;
    back.approachY = 1060;
    n.walkBackIdx = 2;
    CHECK(Tick(n, back).compare(0, 7, "escape ") == 0);
    // ... and a good read clears it.
    Run c = Start(&plan, 3);
    c.walkBack = true;
    c.walkBackIdx = 1;
    CHECK(Tick(c, g) == "move 1588.0 1080.0 9.1");
    Facts good = g;
    good.leadIsland = false;
    good.leadPathGood = true;
    Tick(c, good);
    CHECK(!c.islandEvidence);
}

// Opus on 269503e0: a walk back's hop that found no spot does not pause the
// run from an unchanged spot: the walk back goes on as before (the point passed).
static void TestWalkBackNoSpotPassesThePoint()
{
    DungeonPlan plan = ZfIslandPlan();
    Run r = Start(&plan, 5);
    r.walkBack = true;
    r.walkBackIdx = r.walkBackStart = 4;
    r.walkBackRestarted = true;
    Facts g = OnIsland();
    std::string const order = Tick(r, g);
    CHECK(order.compare(0, 7, "escape ") == 0);
    OrderRefused(r, order, "no-spot: no spot within 10 yd");
    CHECK(r.noSpotKnown && r.state == State::Running);
    // Same spot: no second hop; the zero hop is no path and the point is passed.
    CHECK(Tick(r, g).empty());
    CHECK(r.state == State::Running && r.walkBackIdx == 5 && r.note.find("on to the next") != std::string::npos);
    CHECK(r.islandEscapes == 1);
    // Off that spot (another island read 5 yd away): the mark is gone, the hop goes out again.
    Run q = Start(&plan, 5);
    q.walkBack = true;
    q.walkBackIdx = q.walkBackStart = 4;
    q.walkBackRestarted = true;
    std::string const first = Tick(q, g);
    OrderRefused(q, first, "no-spot: no spot within 10 yd");
    Facts there = g;
    there.leadX += 5.0f;
    CHECK(Tick(q, there).compare(0, 7, "escape ") == 0);
    // The mark goes once the lead bot leaves the spot, even when it comes back there.
    Run b = Start(&plan, 5);
    b.walkBack = true;
    b.walkBackIdx = b.walkBackStart = 4;
    b.walkBackRestarted = true;
    std::string const hop = Tick(b, g);
    OrderRefused(b, hop, "no-spot: no spot within 10 yd");
    Facts off = g;
    off.leadIsland = false;
    off.leadX += 5.0f;
    off.approachX = off.leadX + 20; // a hop elsewhere, not walked
    Tick(b, off);
    CHECK(!b.noSpotKnown);
    EnterPhase(b, Phase::Begin);
    CHECK(Tick(b, g).compare(0, 7, "escape ") == 0);
}

// A good read between the hops gives fresh tries, but one step sends at most kStepEscapes.
static void TestStepEscapeCap()
{
    DungeonPlan plan = ZfIslandPlan();
    Run r = Start(&plan, 5);
    r.walkBack = true;
    r.walkBackIdx = r.walkBackStart = 4;
    Facts g = OnIsland();
    Facts good = g;
    good.leadIsland = false;
    good.leadPathGood = true;
    good.approachX = 1600;
    good.approachY = 1080;
    std::uint32_t escapes = 0;
    for (int i = 0; i < 40 && r.state == State::Running; ++i)
    {
        std::string const order = Tick(r, g);
        if (order.compare(0, 7, "escape ") == 0)
        {
            ++escapes;
            Tick(r, g);
            Tick(r, g);   // the hop settles
            Tick(r, good); // a good read in between: fresh tries ...
            EnterPhase(r, Phase::Begin);
        }
    }
    CHECK(escapes == kStepEscapes);
    CHECK(r.state == State::Paused && r.note.find("navmesh island") != std::string::npos);
}


// Final Opus check on d7e94c3b: the probe. After a hop that found no spot, the
// walk back from that spot looped: a point refused from it set the island
// suspicion instead of being passed, the no-spot mark cleared it, and the move
// went out again every tick (3000 ticks at walk-back index 1). The driver here
// answers every hop "no-spot" and every move "unreachable", as from the island.
static void TestNoSpotWalkBackNeverSpins()
{
    DungeonPlan plan;
    PlanStep pull = PullStep(1903, {7271});
    pull.y = 1014;
    pull.z = 11.8f;
    plan.steps = {Step(PlanStepKind::Move, 1603, 1043, 9.8f), Step(PlanStepKind::Move, 1588, 1080, 9.1f),
        Step(PlanStepKind::Move, 1584, 1088, 9.1f), Step(PlanStepKind::Move, 1633.6f, 1130.7f, 9.9f), pull};
    Run r = Start(&plan, 4);
    r.walkBack = true;
    r.walkBackIdx = r.walkBackStart = 0;
    r.walkBackRestarted = true;
    std::uint32_t sameOrder = 0, longestRepeat = 0, moves = 0;
    std::string last;
    std::size_t lastIdx = 0;
    int endedAt = -1;
    for (int t = 0; t < 3000 && r.state == State::Running; ++t)
    {
        Facts f = OnIsland();
        const PlanStep* fs = FactStep(r);
        float const to = ai::PlanDistance(f.leadX, f.leadY, f.leadZ, fs->x, fs->y, fs->z);
        f.seen = to <= kSeeYd;
        f.approachKnown = !f.seen;
        f.leadIsland = LeadOnIsland(false, true, 3.05f, to);
        f.goodSpotCut = false;
        std::string const order = Tick(r, f);
        if (!r.walkBack && endedAt < 0)
            endedAt = t;
        if (order.empty())
            continue;
        if (order == last && r.walkBackIdx == lastIdx)
            longestRepeat = std::max(longestRepeat, ++sameOrder);
        else
            sameOrder = 1;
        last = order;
        lastIdx = r.walkBackIdx;
        if (order.compare(0, 7, "escape ") == 0)
            OrderRefused(r, order, "no-spot: no spot within 10 yd");
        else if (order.compare(0, 5, "move ") == 0)
        {
            ++moves;
            OrderRefused(r, order, "unreachable: no navmesh route reaches it");
        }
    }
    CHECK(endedAt >= 0 && endedAt < 200); // every point passed, the walk back is over
    CHECK(longestRepeat <= 1); // each point refused once and passed (as on 2012dfee), never sent again
    CHECK(moves <= 3);
    CHECK(r.state == State::Paused); // the step's own approach then pauses (bounded)
}

// Sign (b) from the main mesh (two unreachable points read from one spot): one
// hop off per spot, at most kStepEscapes in the step, and the run ends bounded.
static void TestSignBFromTheMainMeshIsBounded()
{
    DungeonPlan plan;
    PlanStep pull = PullStep(600, {1});
    plan.steps = {Step(PlanStepKind::Move, 40), Step(PlanStepKind::Move, 40, 30), Step(PlanStepKind::Move, 40, 60),
        Step(PlanStepKind::Move, 40, 90), pull};
    Run r = Start(&plan, 4);
    r.walkBack = true;
    r.walkBackRestarted = true;
    float leadX = 0.0f;
    std::uint32_t escapes = 0;
    std::vector<float> escapedFrom;
    for (int t = 0; t < 3000 && r.state == State::Running; ++t)
    {
        Facts f = Party(leadX);
        const PlanStep* fs = FactStep(r);
        float const to = ai::PlanDistance(f.leadX, f.leadY, f.leadZ, fs->x, fs->y, fs->z);
        f.seen = to <= kSeeYd;
        f.approachKnown = !f.seen;
        f.approachX = leadX + 5;
        f.leadIsland = LeadOnIsland(false, true, 5.0f, to);
        std::string const order = Tick(r, f);
        if (order.compare(0, 7, "escape ") == 0)
        {
            ++escapes;
            for (float x : escapedFrom)
                CHECK(std::fabs(x - leadX) > kArriveYd); // one hop off per spot
            escapedFrom.push_back(leadX);
            leadX -= 6.0f; // the glue moved it 6 yd: another spot, another cell
        }
        else if (order.compare(0, 5, "move ") == 0 && f.seen)
            OrderRefused(r, order, "unreachable: no navmesh route reaches it");
    }
    CHECK(escapes >= 1 && escapes <= kStepEscapes);
    CHECK(r.state == State::Paused);
}


// The defensive bound on its own: a walk-back point tried more than
// kWalkBackResends times from one spot is passed, whatever brought it back
// (here a refusal handled elsewhere that left the point standing), in sight
// (its move) or out of sight (its way found missing).
static void TestWalkBackResendBound()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 40), Step(PlanStepKind::Move, 200), PullStep(400, {1})};
    Run r = Start(&plan, 2);
    r.walkBack = true;
    Facts f = Party(0);
    for (std::uint32_t i = 0; i < kWalkBackResends; ++i)
    {
        CHECK(Tick(r, f) == "move 40.0 0.0 0.0");
        EnterPhase(r, Phase::Begin);
    }
    CHECK(Tick(r, f).empty());
    CHECK(r.walkBackIdx == 1 && r.note.find("tried 6 times from this spot: on to the next") != std::string::npos);
    // Out of sight, the way found missing each time without passing (the
    // suspicion kept standing): the point is passed after kWalkBackResends.
    Run o = Start(&plan, 2);
    o.walkBack = true;
    o.walkBackIdx = 1;
    o.walkBackStart = 1;
    o.walkBackRestarted = true;
    Facts g = Party(0);
    g.seen = false;
    g.approachKnown = false;
    for (std::uint32_t i = 0; i < kWalkBackResends; ++i)
    {
        o.passSpotKnown = true; // a point given up here before, no good path: the suspicion
        o.passX = o.passY = o.passZ = 0.0f;
        o.noSpotKnown = true;   // ... and a no-spot mark that keeps it from a hop
        o.noSpotX = 50.0f;      // (not on this spot: PassWalkBackPoint does not pass)
        Tick(o, g);
        CHECK(o.walkBackIdx == 1 && o.state == State::Running);
        o.islandSuspect = false;
    }
    Tick(o, g);
    CHECK(o.walkBackIdx == 2);
}

// (b) Uldaman 19z11 02:50: the approach paused after 8 hops while the priest
// stood still on a navmesh pocket (43 371). Before the pause a far bot that
// stands still is summoned, once; a walker is not; then the pause (the lead
// bot's own way is the reason).
static void TestApproachHopSummonsBeforePause()
{
    DungeonPlan plan;
    plan.steps = {PullStep(300, {1})};
    Run r = Start(&plan);
    r.restDue = false;
    Facts f = Party(0);
    f.seen = false;
    f.approachKnown = true;
    f.approachX = 40;
    f.maxMemberYd = 15;
    f.members = {Mem("Tanky", 0), Mem("Fyran", 150), Mem("Walky", 90)};
    f.members[2].yd = 10; // close enough for the pull's gather
    f.maxMemberYd = 150;
    r.approachHops = kMaxApproachHops;
    Run::StillState st;
    st.x = 150;
    st.ms = kStillMs;
    r.still["Fyran"] = st;
    std::string const order = Tick(r, f);
    CHECK(order == "summon Fyran");
    CHECK(r.state == State::Running && r.note.find("approach: Fyran") == 0);
    CHECK(Tick(r, f).find("summon") == std::string::npos); // Walky moves: not summoned
    CHECK(r.state == State::Paused && r.note.find("approach hops") != std::string::npos);
}

// (c) Uldaman 19z11 02:48: only the lead bot died in the fight; its own
// release put it at the entrance 300 yd away while the others won. It is the
// wipe recovery: regroup, then the walk back over the plan's points. Bounded
// by kAutoRegroupsPerStep. A lead bot raised where it fell needs nothing.
static void TestLeadOnlyDeathRecovers()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 10), Step(PlanStepKind::Move, 200), PullStep(220, {1})};
    Run r = Start(&plan, 2);
    Facts f = Party(210);
    f.anyInCombat = true;
    Tick(r, f); // fighting, the lead bot alive at 210
    f.leadAlive = false;
    f.dead = 1;
    f.alive = 4;
    f.deadNames = {"Tanky"};
    Tick(r, f);
    CHECK(r.leadDied && r.state == State::Running);
    // Back on its feet at the entrance, the fight won.
    f.leadAlive = true;
    f.dead = 0;
    f.alive = 5;
    f.deadNames.clear();
    f.anyInCombat = false;
    f.leadX = -90;
    f.maxMemberYd = 300;
    f.members = {Mem("Tanky", 0), Mem("Fyran", 305)};
    f.members[1].x = 215; // where the party won the fight, 5 yd from where the lead bot fell
    Tick(r, f);
    CHECK(r.regroup && r.walkBackDue && r.autoRegroups == 1 && r.state == State::Running);
    CHECK(r.note.find("the lead bot died and came back") == 0);
    // The living party holds; the lead bot walks back to it, nobody is
    // gathered on (or summoned to) the entrance.
    CHECK(r.hold.size() == 1 && r.hold[0] == "Fyran");
    f.seen = false;
    f.approachKnown = true;
    f.approachX = -50;
    std::string order;
    for (int i = 0; i < 3; ++i)
    {
        order = Tick(r, f);
        CHECK(order.find("summon") != 0 && order != "move -90.0 0.0 0.0");
    }
    CHECK(!r.regroup && r.walkBack && r.hold.size() == 1);
    // Back with it: the hold ends.
    f.leadX = 200;
    f.members[1].yd = 15;
    Tick(r, f);
    CHECK(r.hold.empty());
    // Bounded: past kAutoRegroupsPerStep it pauses.
    Run p = Start(&plan, 2);
    p.autoRegroups = kAutoRegroupsPerStep;
    Facts g = Party(210);
    g.anyInCombat = true;
    Tick(p, g);
    g.leadAlive = false;
    g.dead = 1;
    g.alive = 4;
    Tick(p, g);
    g.leadAlive = true;
    g.dead = 0;
    g.alive = 5;
    g.anyInCombat = false;
    g.leadX = -90;
    Tick(p, g);
    CHECK(p.state == State::Paused && p.note.find("came back") != std::string::npos);
    // Raised where it fell (a resurrection in the party): no regroup of its own.
    Run q = Start(&plan, 2);
    Facts h = Party(210);
    h.anyInCombat = true;
    Tick(q, h);
    h.leadAlive = false;
    h.dead = 1;
    h.alive = 4;
    Tick(q, h);
    h.leadAlive = true;
    h.dead = 0;
    h.alive = 5;
    h.anyInCombat = false;
    Tick(q, h);
    CHECK(!q.regroup && q.autoRegroups == 0 && !q.leadDied);
}

// The owner's rule (2026-10-17): "Party should wait if a party member dies
// until it is back." A death in a walk holds the walk; the bot alive again but
// far is waited for (running back); within kGatherYd the step goes on.
static void TestDeathWhileWalkingBlocksTheStep()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 40), PullStep(60, {1})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.seen = true;
    CHECK(Tick(r, f) == "move 40.0 0.0 0.0");
    f.dead = 1;
    f.alive = 4;
    f.deadNames = {"Fyran"};
    CHECK(Tick(r, f).empty());
    CHECK(r.regroup && r.note.find("waiting for Fyran (dead)") == 0);
    // Released, ran back, alive at the entrance: still waited for.
    f.dead = 0;
    f.alive = 5;
    f.deadNames.clear();
    f.maxMemberYd = 90;
    f.members = {Mem("Tanky", 0), Mem("Fyran", 90)};
    std::string order;
    for (int i = 0; i < 20; ++i)
    {
        order = Tick(r, f);
        CHECK(order.find("move 40") != 0);
    }
    CHECK(r.regroup && r.note.find("Fyran (running back)") != std::string::npos);
    // Back with the party: the walk goes on.
    f.maxMemberYd = 5;
    f.members = {Mem("Tanky", 0), Mem("Fyran", 5)};
    for (int i = 0; i < 5 && order.find("move 40") != 0; ++i)
        order = Tick(r, f);
    CHECK(order == "move 40.0 0.0 0.0" && !r.regroup && r.waitFor.empty());
}

// The lead bot dead out of a fight: the party waits for it (no pause).
static void TestLeadDeathBlocks()
{
    DungeonPlan plan;
    plan.steps = {PullStep(10, {1})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.units = {Unit(7, 1, 10)};
    f.leadAlive = false;
    f.dead = 1;
    f.alive = 4;
    f.deadNames = {"Tanky"};
    for (int i = 0; i < 30; ++i)
        CHECK(Tick(r, f).find("pullback") != 0);
    CHECK(r.state == State::Running && r.regroup && r.note.find("Tanky (dead)") != std::string::npos);
}

// Events cannot be left mid-way: the wait comes after the event.
static void TestWaitRuleEventException()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 40), Step(PlanStepKind::Move, 80)};
    Run r = Start(&plan);
    r.eventOn = true;
    Facts f = Party(0);
    f.dead = 1;
    f.alive = 4;
    f.deadNames = {"Fyran"};
    CHECK(Tick(r, f) == "move 40.0 0.0 0.0");
    CHECK(!r.regroup && r.waitFor.count("Fyran"));
    r.eventOn = false; // the event is over
    CHECK(Tick(r, f).empty());
    CHECK(r.regroup && r.note.find("waiting for Fyran (dead)") == 0);
}

// N1 (Uldaman 19z11: 17 of 63 pulls with a member dead): the pull itself
// holds while anyone is dead, outside an event.
static void TestNoPullWithADeadMember()
{
    DungeonPlan plan;
    plan.steps = {PullStep(10, {1})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.units = {Unit(7, 1, 10)};
    f.dead = 1;
    f.alive = 4;
    f.deadNames = {"Fyran"};
    std::string order;
    PullUnit(r, f, f.units[0], order);
    CHECK(order.empty() && r.note.find("no pull while a party member is dead") != std::string::npos);
    for (int i = 0; i < 40; ++i)
        CHECK(Tick(r, f).find("pullback") != 0);
    // Inside an event the pull goes out (it cannot wait).
    Run e = Start(&plan);
    e.eventOn = true;
    order.clear();
    PullUnit(e, f, f.units[0], order);
    CHECK(order == "pullback 7");
}

// N3 (Uldaman 01:43, 02:31): three dead, the two rogues still fighting. The
// regroup neither revives nor gathers nor walks while a living bot fights:
// the fight first, then the regroup.
static void TestRegroupWaitsForTheFight()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 10), PullStep(100, {1})};
    Run r = Start(&plan, 1);
    Facts f = Party(100);
    f.dead = 3;
    f.alive = 2;
    f.deadNames = {"Tanky", "Fyran", "Iront"};
    f.anyInCombat = true;
    for (int i = 0; i < 30; ++i)
    {
        std::string const order = Tick(r, f);
        CHECK(order.empty() || order.find("focus") == 0 || order.find("cc ") == 0);
    }
    CHECK(r.state == State::Running && r.note.find("fighting on") != std::string::npos);
    // A regroup already going (a resume) holds in a fight too.
    Run g = Start(&plan, 1);
    BeginRegroup(g);
    Facts h = Party(100);
    h.dead = 1;
    h.alive = 4;
    h.anyInCombat = true;
    for (int i = 0; i < 30; ++i)
        CHECK(Tick(g, h).find("revive") != 0);
    h.anyInCombat = false;
    CHECK(Tick(g, h) == "revive party");
}

// A death during the regroup's gather: back to reviving.
static void TestDeathDuringRegroup()
{
    DungeonPlan plan;
    plan.steps = {PullStep(10, {1})};
    Run r = Start(&plan);
    BeginRegroup(r);
    Facts f = Party(0);
    f.maxMemberYd = 60;
    f.members = {Mem("Tanky", 0), Mem("Fyran", 60)};
    CHECK(Tick(r, f) == "move 0.0 0.0 0.0");
    f.dead = 1;
    f.alive = 4;
    f.deadNames = {"Fyran"};
    f.members = {Mem("Tanky", 0)};
    f.maxMemberYd = 0;
    CHECK(Tick(r, f) == "revive party");
    CHECK(r.regroup && r.note.find("Fyran (dead)") != std::string::npos);
}

// Review of #1060: the common case, the fight ends while the lead bot is
// still a ghost. The regroup revives it; once it is up at the entrance the
// living party deep inside holds (no gather to the entrance, no summon there).
static void TestLeadDeadAfterTheFightHolds()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 10), Step(PlanStepKind::Move, 200), PullStep(220, {1})};
    Run r = Start(&plan, 2);
    Facts f = Party(210);
    f.anyInCombat = true;
    Tick(r, f); // fighting, the lead bot alive at 210
    f.leadAlive = false;
    f.dead = 1;
    f.alive = 4;
    f.deadNames = {"Tanky"};
    Tick(r, f); // it falls in the fight
    // The fight is over, the lead bot still dead: the regroup, its hold armed.
    f.anyInCombat = false;
    f.members = {Mem("Fyran", 5)};
    f.members[0].x = 215;
    f.maxMemberYd = 0;
    Tick(r, f);
    CHECK(r.regroup && r.holdDue && r.walkBackDue && r.autoRegroups == 1 && r.hold.empty());
    CHECK(Tick(r, f) == "revive party");
    // Up at the entrance, 300 yd from where it fell.
    f.leadAlive = true;
    f.dead = 0;
    f.alive = 5;
    f.deadNames.clear();
    f.leadX = -90;
    f.members = {Mem("Tanky", 0), Mem("Fyran", 305)};
    f.members[1].x = 215;
    f.maxMemberYd = 305;
    CHECK(Tick(r, f).empty());
    CHECK(r.hold.size() == 1 && r.hold[0] == "Fyran" && !r.holdDue);
    f.seen = false;
    f.approachKnown = true;
    f.approachX = -50;
    for (int i = 0; i < 20; ++i)
    {
        std::string const order = Tick(r, f);
        CHECK(order.find("summon") != 0 && order != "move -90.0 0.0 0.0");
    }
    CHECK(r.state == State::Running && !r.regroup && r.walkBack && r.hold.size() == 1);
    // A player's order pauses the run: the hold ends (the glue lets them go).
    CHECK(PauseForPlayer(r, "follow"));
    CHECK(r.hold.empty() && !r.holdDue);
    // Raised where it fell (a resurrection in the party): no hold.
    Run q = Start(&plan, 2);
    Facts h = Party(210);
    h.anyInCombat = true;
    Tick(q, h);
    h.leadAlive = false;
    h.dead = 1;
    h.alive = 4;
    Tick(q, h);
    h.anyInCombat = false;
    h.members = {Mem("Fyran", 30)};
    h.members[0].x = 240;
    Tick(q, h);
    CHECK(q.holdDue);
    h.leadAlive = true;
    h.dead = 0;
    h.alive = 5;
    Tick(q, h);
    CHECK(q.hold.empty() && !q.holdDue);
}

// The run's own time limit and the stop note: the 3 h cap used to read as a plain stop.
static void TestRunTimeLimitAndStopNote()
{
    CHECK(kRunMaxMinutesDefault == 180);
    CHECK(!RunTimeLimitReached(0, 180));
    CHECK(!RunTimeLimitReached(180ull * 60000ull - 1, 180));
    CHECK(RunTimeLimitReached(180ull * 60000ull, 180));
    CHECK(RunTimeLimitReached(4000ull * 3600000ull, 180));
    CHECK(!RunTimeLimitReached(4000ull * 3600000ull, 0)); // 0 = no limit
    CHECK(RunTimeLimitNote(180) == "time limit reached (3h)");
    CHECK(RunTimeLimitNote(60) == "time limit reached (1h)");
    CHECK(RunTimeLimitNote(90) == "time limit reached (90 min)");

    CHECK(RunStopNote("") == "stopped by .bot run stop");
    CHECK(RunStopNote("   ") == "stopped by .bot run stop");
    CHECK(RunStopNote("time limit reached (3h)") == "stopped: time limit reached (3h)");
    CHECK(RunStopNote("a|b  c") == "stopped: a b c"); // no protocol separator, spaces squeezed
    CHECK(RunStopNote(std::string(300, 'x')).size() == std::string("stopped: ").size() + kRunStopReasonMax);
}

// `.bot run start [plan] [from ...]`.
static DungeonPlan TwoLegPlan()
{
    DungeonPlan plan;
    plan.name = "test";
    plan.map = 70;
    plan.legs.push_back(ai::PlanLeg{0, "entrance"});
    plan.legs.push_back(ai::PlanLeg{3, "Trogg_West"});
    for (std::uint8_t leg : {0, 0, 3, 3, 3})
    {
        PlanStep st = Step(PlanStepKind::Move, 1, 2, 3);
        st.leg = leg;
        plan.steps.push_back(st);
    }
    return plan;
}

static void TestStartFromParsing()
{
    StartArgs a = ParseStartArgs("");
    CHECK(a.ok && a.plan.empty() && a.from == StartFrom::Beginning);
    a = ParseStartArgs("uldaman");
    CHECK(a.ok && a.plan == "uldaman" && a.from == StartFrom::Beginning);
    a = ParseStartArgs("uldaman from 4");
    CHECK(a.ok && a.plan == "uldaman" && a.from == StartFrom::Leg && a.value == "4");
    a = ParseStartArgs("uldaman from leg trogg_west");
    CHECK(a.ok && a.from == StartFrom::Leg && a.value == "trogg_west");
    a = ParseStartArgs("uldaman from step 57");
    CHECK(a.ok && a.from == StartFrom::Step && a.value == "57");
    a = ParseStartArgs("from step 2"); // the lead bot's map names the plan
    CHECK(a.ok && a.plan.empty() && a.from == StartFrom::Step && a.value == "2");
    a = ParseStartArgs("  uldaman   from   leg   baelog ");
    CHECK(a.ok && a.plan == "uldaman" && a.value == "baelog");
    CHECK(!ParseStartArgs("uldaman from").ok);
    CHECK(!ParseStartArgs("uldaman from step").ok);
    CHECK(!ParseStartArgs("uldaman from 3 4").ok);
    CHECK(!ParseStartArgs("uldaman two from 3").ok);
    CHECK(!ParseStartArgs("uldaman fromm 3").ok);
}

static void TestStartFromResolve()
{
    DungeonPlan plan = TwoLegPlan();
    std::size_t step = 99;
    CHECK(ResolveStartStep(plan, StartFrom::Beginning, "", step) == FromResult::Ok && step == 0);
    CHECK(ResolveStartStep(plan, StartFrom::Leg, "3", step) == FromResult::Ok && step == 2); // first step of leg 3
    CHECK(ResolveStartStep(plan, StartFrom::Leg, "0", step) == FromResult::Ok && step == 0);
    CHECK(ResolveStartStep(plan, StartFrom::Leg, "trogg_west", step) == FromResult::Ok && step == 2); // name, any case
    CHECK(ResolveStartStep(plan, StartFrom::Leg, "7", step) == FromResult::NoSuchLeg);
    CHECK(ResolveStartStep(plan, StartFrom::Leg, "nowhere", step) == FromResult::NoSuchLeg);
    CHECK(ResolveStartStep(plan, StartFrom::Step, "1", step) == FromResult::Ok && step == 0);
    CHECK(ResolveStartStep(plan, StartFrom::Step, "5", step) == FromResult::Ok && step == 4);
    CHECK(ResolveStartStep(plan, StartFrom::Step, "6", step) == FromResult::NoSuchStep);
    CHECK(ResolveStartStep(plan, StartFrom::Step, "0", step) == FromResult::NoSuchStep);
    CHECK(ResolveStartStep(plan, StartFrom::Step, "x", step) == FromResult::NoSuchStep);

    // The run begins on that step, running, with the usual first-step state.
    Run r = Start(&plan, 2);
    CHECK(r.step == 2 && r.state == State::Running);
}

// The Uldaman Keepers: the ritual ends leg 10, the hold opens leg 11. A start on the hold goes back
// to the ritual, or the event never starts.
static DungeonPlan EventPlan()
{
    DungeonPlan plan;
    plan.name = "event";
    plan.map = 70;
    plan.legs.push_back(ai::PlanLeg{10, "keepers"});
    plan.legs.push_back(ai::PlanLeg{11, "archaedas"});
    struct Row { std::uint8_t leg; PlanStepKind kind; };
    for (Row row : {Row{10, PlanStepKind::Move}, Row{10, PlanStepKind::Move}, Row{10, PlanStepKind::Ritual},
                    Row{11, PlanStepKind::Hold}, Row{11, PlanStepKind::Move}, Row{11, PlanStepKind::Pull},
                    Row{11, PlanStepKind::Move}, Row{11, PlanStepKind::Open}, Row{11, PlanStepKind::Move},
                    Row{11, PlanStepKind::Hold}})
    {
        PlanStep st = Step(row.kind, 1, 2, 3);
        st.leg = row.leg;
        plan.steps.push_back(st);
    }
    return plan;
}

static void TestStartInsideEventGoesBack()
{
    DungeonPlan plan = EventPlan();
    std::size_t step = 99;
    bool back = false;
    // Leg 11 begins on the hold (step index 3): back to the ritual (index 2).
    CHECK(ResolveStartStep(plan, StartFrom::Leg, "11", step, &back) == FromResult::Ok && step == 2 && back);
    CHECK(ResolveStartStep(plan, StartFrom::Leg, "archaedas", step, &back) == FromResult::Ok && step == 2 && back);
    // A step number on the hold (step 4), and on the moves before the ritual (steps 1, 2) leading into... the ritual.
    CHECK(ResolveStartStep(plan, StartFrom::Step, "4", step, &back) == FromResult::Ok && step == 2 && back);
    // The ritual itself and the leg before it stay as asked.
    CHECK(ResolveStartStep(plan, StartFrom::Step, "3", step, &back) == FromResult::Ok && step == 2 && !back);
    CHECK(ResolveStartStep(plan, StartFrom::Leg, "10", step, &back) == FromResult::Ok && step == 0 && !back);
    // A move that leads into the second hold (index 8) goes back to the open step (index 7); the hold itself too.
    CHECK(ResolveStartStep(plan, StartFrom::Step, "9", step, &back) == FromResult::Ok && step == 7 && back);
    CHECK(ResolveStartStep(plan, StartFrom::Step, "10", step, &back) == FromResult::Ok && step == 7 && back);
    // A pull in the middle of the leg is not an event start.
    CHECK(ResolveStartStep(plan, StartFrom::Step, "6", step, &back) == FromResult::Ok && step == 5 && !back);
    // A move that leads into no hold stays put.
    CHECK(ResolveStartStep(plan, StartFrom::Step, "7", step, &back) == FromResult::Ok && step == 6 && !back);
    // A hold with no ritual/open/use_item before it (only moves, then the plan's start) stays put.
    DungeonPlan bare;
    bare.map = 70;
    for (PlanStepKind k : {PlanStepKind::Move, PlanStepKind::Hold})
        bare.steps.push_back(Step(k, 1, 2, 3));
    CHECK(EventStartStep(bare, 1) == 1 && EventStartStep(bare, 0) == 0);
    // The run that starts there begins with the ritual, not the hold.
    Run r = Start(&plan, 2);
    CHECK(Current(r)->kind == PlanStepKind::Ritual && EventHoldAhead(r) != nullptr);
}

static void TestPartialStartGuard()
{
    CHECK(CheckPartialStart(true, 5, 5) == PartialStartVerdict::Ok);
    CHECK(CheckPartialStart(false, 5, 5) == PartialStartVerdict::NotInstance);
    CHECK(CheckPartialStart(true, 5, 4) == PartialStartVerdict::PartyScattered);
    CHECK(CheckPartialStart(true, 0, 0) == PartialStartVerdict::Ok);
}


// ---- Exact waypoints (ai::kPlanFlagExact, ExactWalkTo) ----

static PlanStep ExactStep(float x, float y, float z)
{
    PlanStep s;
    s.kind = PlanStepKind::Move;
    s.x = x;
    s.y = y;
    s.z = z;
    s.flags = ai::kPlanFlagExact;
    CHECK(ai::NormalizePlanStep(s) == ai::PlanStepRefusal::None);
    return s;
}

// The hop after an exact point: a move_hop_exact row (exactLaunch).
static PlanStep DirectExact(float x, float y, float z)
{
    PlanStep s = Step(PlanStepKind::MoveDirect, x, y, z);
    s.exactLaunch = true;
    return s;
}

// The Wailing Caverns leg 10 rows (export of 2026-10-18): three path rows, the
// exact foot 2.25 yd from the row before it, the direct hop down to the
// Serpentis ramp (47 yd drop under it), then a rest.
static DungeonPlan SerpentisPlan()
{
    DungeonPlan plan;
    plan.name = "wailing_caverns";
    plan.steps = {Step(PlanStepKind::Move, -308.6239f, -32.13346f, -60.944225f),
        Step(PlanStepKind::Move, -292.80005f, -3.4667964f, -58.160522f),
        Step(PlanStepKind::Move, -292.00006f, -1.3334637f, -57.860523f),
        ExactStep(-293.3f, 0.5f, -58.0f),
        DirectExact(-287.5f, 5.0f, -63.6f),
        Step(PlanStepKind::Rest)};
    return plan;
}

static bool IsDirectOrder(const std::string& order)
{
    return order.find("direct") != std::string::npos;
}

// The Serpentis case: the lead bot stands 2.3 yd from the foot (within the plain
// move's 3 yd). Before the exact rule the foot counted as reached at once and the
// direct hop went out from there. Now: no direct order until the proof; the lead
// bot (too near for a move onto the point) walks back to the first plan point at
// least 6.5 yd behind, comes onto the foot, stands 2 s, and only then the hop.
static void TestExactSerpentisFootIsNotSkipped()
{
    DungeonPlan plan = SerpentisPlan();
    Run r = Start(&plan, 3);
    Facts f = Party(0);
    f.leadX = -293.3f;
    f.leadY = 0.5f - 2.3f;
    f.leadZ = -58.0f;
    std::string back;
    for (int i = 0; i < 6 && back.empty(); ++i)
    {
        std::string const order = Tick(r, f);
        CHECK(!IsDirectOrder(order));
        CHECK(r.step == 3 && r.state == State::Running);
        back = order;
    }
    // The rows 1 and 2 are within 6.5 yd of the foot on the ground plane: row 0.
    CHECK(back == "move -308.6 -32.1 -60.9");
    // Walking there: no order over the move.
    f.leadMoving = true;
    f.leadX = -300.0f;
    f.leadY = -15.0f;
    CHECK(Tick(r, f).empty());
    // Arrived and standing: after 3 s, onto the foot.
    f.leadMoving = false;
    f.leadX = -308.6f;
    f.leadY = -32.1f;
    f.leadZ = -60.9f;
    std::string onto;
    for (int i = 0; i < 5 && onto.empty(); ++i)
    {
        onto = Tick(r, f);
        CHECK(!IsDirectOrder(onto) && r.step == 3);
    }
    CHECK(onto == "move -293.3 0.5 -58.0");
    // On the foot but still moving: no proof.
    f.leadX = -293.2f;
    f.leadY = 0.4f;
    f.leadZ = -58.4f;
    f.leadMoving = true;
    for (int i = 0; i < 4; ++i)
    {
        CHECK(Tick(r, f).empty());
        CHECK(r.step == 3);
    }
    // Standing: three looks on one spot (2 s), then the step is done ...
    f.leadMoving = false;
    CHECK(Tick(r, f).empty() && r.step == 3);
    CHECK(Tick(r, f).empty() && r.step == 3);
    CHECK(Tick(r, f).empty());
    CHECK(r.step == 4 && r.exactProved == 3 && r.note.find("exact point") != std::string::npos);
    // ... and the direct hop goes out from the foot.
    CHECK(Tick(r, f) == "move -287.5 5.0 -63.6 direct");
}

// A lead bot that never comes onto the point: bounded, and a pause with a clear
// note; the direct step never goes out.
static void TestExactNeverProvedPauses()
{
    DungeonPlan plan = SerpentisPlan();
    Run r = Start(&plan, 3);
    Facts f = Party(0);
    f.leadX = -293.3f;
    f.leadY = 0.5f - 2.3f;
    f.leadZ = -58.0f;
    int ticks = 0;
    for (; ticks < 200 && r.state == State::Running; ++ticks)
        CHECK(!IsDirectOrder(Tick(r, f)));
    CHECK(r.state == State::Paused && r.step == 3);
    CHECK(ticks <= static_cast<int>(kExactCapMs / kTickMs) + 2);
    CHECK(r.note.find("exact point of step 4") != std::string::npos);
    CHECK(r.note.find("not proved") != std::string::npos);
    CHECK(r.note.find(".bot run resume") != std::string::npos);
}

// 1.5-3 yd away with no plan point behind it to walk back to: the lead bot is
// sent onto the point again (a re-issued move), never counted as arrived by the
// 3 yd rule, nor by the stalled arrival within 6 yd.
static void TestExactReissuesTheMoveAndIgnoresTheStallRule()
{
    DungeonPlan plan;
    plan.steps = {ExactStep(10, 0, 0), Step(PlanStepKind::MoveDirect, 15, 0, 0)};
    Run r = Start(&plan);
    Facts f = Party(12.3f);
    std::vector<std::string> orders;
    for (int i = 0; i < 8; ++i)
    {
        std::string const order = Tick(r, f);
        CHECK(!IsDirectOrder(order) && r.step == 0);
        if (!order.empty())
            orders.push_back(order);
    }
    CHECK(!orders.empty() && orders[0] == "move 10.0 0.0 0.0");
    // A stall 4.5 yd short (the plain walk's 3 s / 6 yd arrival): not done either.
    Run r2 = Start(&plan);
    Facts g = Party(0);
    CHECK(Tick(r2, g) == "move 10.0 0.0 0.0");
    g.leadX = 5.5f;
    for (int i = 0; i < 10; ++i)
    {
        CHECK(!IsDirectOrder(Tick(r2, g)));
        CHECK(r2.step == 0);
    }
}

// A direct step launched from an exact point checks the proof itself: started
// (or resumed) at the direct step with the lead bot 2.3 yd off the foot, the hop
// waits for the tighten; on the foot and still, it goes out.
static void TestDirectFromExactPointWaitsForTheProof()
{
    DungeonPlan plan = SerpentisPlan();
    Run r = Start(&plan, 4);
    Facts f = Party(0);
    f.leadX = -293.3f;
    f.leadY = 0.5f - 2.3f;
    f.leadZ = -58.0f;
    for (int i = 0; i < 6; ++i)
        CHECK(!IsDirectOrder(Tick(r, f)));
    Run r2 = Start(&plan, 4);
    f.leadY = 0.4f;
    std::string order;
    for (int i = 0; i < 6 && order.empty(); ++i)
        order = Tick(r2, f);
    CHECK(order == "move -287.5 5.0 -63.6 direct");
    // A stand-in point (detour) is never taken for the exact point nor its hop.
    Run r3 = Start(&plan, 3);
    f.leadX = -320.0f;
    f.leadY = -40.0f;
    Tick(r3, f);
    CHECK(!UseDetour(r3, -295.0f, 0.5f, -58.0f));
}

// A walk back after a regroup replays the exact point as its step does: the
// foot is not passed by the 3 yd rule, and the hop waits for its proof.
static void TestWalkBackReplaysTheExactPoint()
{
    DungeonPlan plan = SerpentisPlan();
    Run r = Start(&plan, 5);
    r.walkBack = true;
    r.walkBackIdx = r.walkBackStart = 3;
    Facts f = Party(0);
    f.leadX = -293.3f;
    f.leadY = 0.5f - 2.3f;
    f.leadZ = -58.0f;
    std::string back;
    for (int i = 0; i < 7 && (back.empty() || back == "rest"); ++i) // the rest step's gate asks for a rest first
    {
        back = Tick(r, f);
        CHECK(!IsDirectOrder(back) && r.walkBackIdx == 3 && r.walkBack);
    }
    CHECK(back == "move -308.6 -32.1 -60.9");
    // Skipping the foot's replay is caught by the hop's own check.
    Run r2 = Start(&plan, 5);
    r2.walkBack = true;
    r2.walkBackIdx = r2.walkBackStart = 4;
    for (int i = 0; i < 7; ++i)
        CHECK(!IsDirectOrder(Tick(r2, f)));
    // On the foot and still: the hop is replayed.
    f.leadY = 0.4f;
    Run r3 = Start(&plan, 5);
    r3.walkBack = true;
    r3.walkBackIdx = r3.walkBackStart = 3;
    std::string order;
    for (int i = 0; i < 8 && !IsDirectOrder(order); ++i)
        order = Tick(r3, f);
    CHECK(order == "move -287.5 5.0 -63.6 direct" && r3.walkBackIdx == 4);
}

// Brings a Serpentis run to the hop (step 4) with the foot proved; the lead
// bot stands on the foot.
static Run ProvedAtTheFoot(const DungeonPlan& plan, Facts& f)
{
    Run r = Start(&plan, 3);
    f = Party(0);
    f.leadX = -293.2f;
    f.leadY = 0.4f;
    f.leadZ = -58.4f;
    for (int i = 0; i < 6 && r.step == 3; ++i)
        CHECK(Tick(r, f).empty());
    CHECK(r.step == 4 && r.exactProved == 3);
    return r;
}

// Proved, then the lead bot is 3 yd off the foot before the hop goes out (a
// knock, a player's move): no direct order; the tighten runs again.
static void TestHopRechecksTheLeadOnTheFoot()
{
    DungeonPlan plan = SerpentisPlan();
    Facts f;
    Run r = ProvedAtTheFoot(plan, f);
    f.leadY = 0.5f - 3.0f;
    f.leadX = -293.3f;
    f.leadZ = -58.0f;
    for (int i = 0; i < 4; ++i)
        CHECK(!IsDirectOrder(Tick(r, f)));
    CHECK(r.step == 4 && r.exactProved != 3);
}

// A lead bot that drifts 1 yd a tick while staying within 1.5 yd of the point,
// with the moving fact false, is never proved.
static void TestDriftWithinTheRadiusIsNoProof()
{
    DungeonPlan plan = SerpentisPlan();
    Run r = Start(&plan, 3);
    Facts f = Party(0);
    f.leadX = -293.3f;
    f.leadZ = -58.0f;
    for (int i = 0; i < 10; ++i)
    {
        f.leadY = (i % 2 == 0) ? 0.0f : 1.0f; // 0.5 yd either side of the foot, 1 yd per tick
        CHECK(!IsDirectOrder(Tick(r, f)));
        CHECK(r.step == 3);
    }
    // ... nor one that swings 0.9 yd a tick about one spot (each look within
    // 0.5 yd of it: only the tick-to-tick move shows it) ...
    Run r2 = Start(&plan, 3);
    for (int i = 0; i < 10; ++i)
    {
        f.leadY = 0.5f + ((i == 0) ? 0.0f : (i % 2 ? 0.45f : -0.45f));
        CHECK(!IsDirectOrder(Tick(r2, f)));
        CHECK(r2.step == 3);
    }
    // ... nor one that creeps 0.4 yd a tick across the point (each tick under
    // 0.5 yd: only the distance from the first spot shows it).
    Run r3 = Start(&plan, 3);
    for (int i = 0; i < 7; ++i)
    {
        f.leadY = 0.5f - 1.2f + 0.4f * static_cast<float>(i);
        CHECK(!IsDirectOrder(Tick(r3, f)));
        CHECK(r3.step == 3);
    }
}

// After a fight, and after a pause and resume, the lead bot still on the foot:
// the proof is taken again (three looks) before the hop goes out.
static void TestProofIsRetakenAfterAFightAndAResume()
{
    DungeonPlan plan = SerpentisPlan();
    Facts f;
    Run r = ProvedAtTheFoot(plan, f);
    f.anyInCombat = true;
    Tick(r, f);
    f.anyInCombat = false;
    int quiet = 0;
    std::string order;
    for (int i = 0; i < 8 && !IsDirectOrder(order); ++i)
    {
        order = Tick(r, f);
        if (!IsDirectOrder(order))
            ++quiet;
    }
    CHECK(order == "move -287.5 5.0 -63.6 direct");
    CHECK(quiet >= 3);

    Run r2 = ProvedAtTheFoot(plan, f);
    Pause(r2, "by hand");
    CHECK(Resume(r2));
    quiet = 0;
    order.clear();
    for (int i = 0; i < 12 && !IsDirectOrder(order); ++i)
    {
        order = Tick(r2, f);
        if (!IsDirectOrder(order))
            ++quiet;
    }
    CHECK(order == "move -287.5 5.0 -63.6 direct");
    CHECK(quiet >= 3);
}

// The walk back to a point behind an exact point never goes back over a direct
// step: with a hole behind, the lead bot is sent onto the point instead.
static void TestBackPointStopsAtADirectStep()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, -30, 0, 0), Step(PlanStepKind::MoveDirect, -8, 0, 0),
        Step(PlanStepKind::Move, -2, 0, 0), ExactStep(0, 0, 0), DirectExact(5, 5, -5)};
    for (int which = 0; which < 2; ++which)
    {
        plan.steps[1].exactLaunch = false;
        if (which == 1)
        {
            // ... a move_hop_exact behind too (with its own exact point before it).
            plan.steps[0].flags = ai::kPlanFlagExact;
            plan.steps[1].exactLaunch = true;
        }
        Run r = Start(&plan, 3);
        Facts f = Party(0);
        f.leadX = 2.3f;
        std::string first;
        for (int i = 0; i < 6 && first.empty(); ++i)
            first = Tick(r, f);
        CHECK(first == "move 0.0 0.0 0.0");
    }
}

// A move_hop_exact with no exact move right before it is not sent: the run
// pauses (at the step and on a walk back). Then a narrow check of the rows an
// older module would load: with the hop's row skipped (the parser test in
// test_dungeon_plan_policy.cpp shows the old kind parser returns None for it)
// and the flag ignored, the plan holds no direct step, and THIS runtime run on
// those rows emits no direct order. It does not model the old runtime, and it
// says nothing about where its plain move to seq 5 ends: that deploy state is
// unsupported (the migration and the module ship together).
static void TestBrokenHopPausesAndSkippedHopRowHasNoDirectOrder()
{
    DungeonPlan plan = SerpentisPlan();
    plan.steps[3].flags = 0; // the foot without its flag
    Run r = Start(&plan, 4);
    Facts f = Party(0);
    f.leadX = -293.2f;
    f.leadY = 0.4f;
    f.leadZ = -58.4f;
    CHECK(!IsDirectOrder(Tick(r, f)));
    CHECK(r.state == State::Paused && r.note.find("move_hop_exact") != std::string::npos);
    // ... on a walk back too.
    Run w = Start(&plan, 5);
    w.walkBack = true;
    w.walkBackIdx = w.walkBackStart = 4;
    for (int i = 0; i < 4 && w.state == State::Running; ++i)
        CHECK(!IsDirectOrder(Tick(w, f)));
    CHECK(w.state == State::Paused && w.note.find("walk back") != std::string::npos &&
        w.note.find("move_hop_exact") != std::string::npos);

    DungeonPlan old = SerpentisPlan();
    old.steps[3].flags = 0;                  // the flag dropped, as an older module ignores it
    old.steps.erase(old.steps.begin() + 4);  // the hop's row skipped, as an older loader does (UnknownKind)
    old.steps.insert(old.steps.begin() + 4, Step(PlanStepKind::Move, -270.9f, 24.3f, -60.0f)); // seq 5, on the ramp
    Run o = Start(&old, 0);
    Facts g = Party(0);
    g.leadX = -320.0f;
    g.leadY = -40.0f;
    g.leadZ = -61.0f;
    for (int i = 0; i < 120 && o.state == State::Running; ++i)
        CHECK(!IsDirectOrder(Tick(o, g)));
}

// AiPlayerbot.DungeonRunAggroEstimate: a pull expected to bring more joiners
// than the ceiling waits once, on the rest wait, for the party to be rested.
static UnitFact Joiners(std::uint64_t guid, std::uint32_t entry, float dist, std::uint32_t joiners, bool patrol = false)
{
    UnitFact u = Unit(guid, entry, dist);
    u.joiners = joiners;
    u.joinersPatrol = patrol;
    return u;
}

static void TestAggroEstimateOffChangesNothing()
{
    DungeonPlan plan;
    plan.steps = {PullStep(40, {4852}), PullStep(40, {4851})};
    Run plain = Start(&plan);
    Run off = Start(&plan);
    Facts a = Party(0);
    a.units = {Unit(31, 4852, 54.7f)};
    a.minHealthPct = 85;
    Facts b = a;
    b.units = {Joiners(31, 4852, 54.7f, 40, true)}; // the estimate is read, the ceiling is off
    CHECK(b.joinerCeiling == 0);
    for (int i = 0; i < 40; ++i)
    {
        std::string const oa = Tick(plain, a);
        std::string const ob = Tick(off, b);
        CHECK(oa == ob);
        CHECK(plain.note == off.note && plain.phase == off.phase && plain.step == off.step);
        CHECK(StatusLine(plain) == StatusLine(off));
        CHECK(!off.joinerRest && off.joinerHeld.empty());
        if (i == 3)
        {
            a.anyInCombat = b.anyInCombat = true;
            a.units.clear();
            b.units.clear();
        }
        if (i == 5)
            a.anyInCombat = b.anyInCombat = false;
    }
}

static void TestAggroEstimateHoldsOnceForARest()
{
    DungeonPlan plan;
    plan.steps = {PullStep(40, {4852})};
    Facts f = Party(0);
    f.joinerCeiling = 6;
    f.units = {Joiners(31, 4852, 54.7f, 7)};
    f.minHealthPct = 85; // above the start lines, under the end lines
    // Over the ceiling: a note, no order; then the rest wait; then the pull.
    Run r = Start(&plan);
    CHECK(Tick(r, f).empty());
    CHECK(r.note == "7 joiners expected" && r.joinerHeld.count(31));
    CHECK(r.phase != Phase::AwaitPull && r.pulls == 0);
    CHECK(Tick(r, f) == "rest");
    CHECK(r.phase == Phase::Rest && r.note == "resting: 7 joiners expected");
    for (int i = 0; i < 5; ++i)
        CHECK(Tick(r, f).find("pullback") == std::string::npos);
    f.minHealthPct = 100;
    std::string order;
    for (int i = 0; i < 5 && order.find("pullback") == std::string::npos; ++i)
        order = Tick(r, f);
    CHECK(order == "pullback 31");
    // Only once per unit: after a fruitless pull the same unit goes out again at once.
    CHECK(r.joinerHeld.size() == 1 && !r.joinerRest);

    // Already rested: the note, then the pull, no rest order.
    Run e = Start(&plan);
    CHECK(Tick(e, f).empty());
    CHECK(e.note == "7 joiners expected");
    CHECK(Tick(e, f) == "pullback 31");
    CHECK(e.phase == Phase::AwaitPull);

    // At the ceiling is not over it, and 0 expected joiners never waits.
    f.minHealthPct = 85;
    f.units = {Joiners(31, 4852, 54.7f, 6)};
    Run at = Start(&plan);
    CHECK(Tick(at, f) == "pullback 31");
    f.units = {Joiners(31, 4852, 54.7f, 0)};
    Run none = Start(&plan);
    CHECK(Tick(none, f) == "pullback 31");

    // A patrol being the whole reason is said.
    f.units = {Joiners(31, 4852, 54.7f, 8, true)};
    Run patrol = Start(&plan);
    CHECK(Tick(patrol, f).empty());
    CHECK(patrol.note == "8 joiners expected (a patrol is the cause)");

    // Inside a scripted event the rest wait is not entered.
    f.units = {Joiners(31, 4852, 54.7f, 8)};
    Run ev = Start(&plan);
    ev.eventOn = true;
    CHECK(Tick(ev, f).empty());
    CHECK(Tick(ev, f) == "pullback 31");
    CHECK(ev.phase != Phase::Rest);

    // A fight that begins before the rest is due does not start one.
    Run fight = Start(&plan);
    CHECK(Tick(fight, f).empty());
    CHECK(fight.joinerRest);
    f.anyInCombat = true;
    CHECK(Tick(fight, f).empty());
    CHECK(!fight.joinerRest && fight.phase != Phase::Rest && fight.fighting);

    // A normal rest after a joiner rest counts as idle time again.
    {
        f.anyInCombat = false;
        f.units = {Joiners(31, 4852, 54.7f, 7)};
        f.minHealthPct = 85;
        Run x = Start(&plan);
        Tick(x, f);
        CHECK(Tick(x, f) == "rest" && x.restForJoiners);
        f.minHealthPct = 100;
        for (int i = 0; i < 3 && x.phase == Phase::Rest; ++i)
            Tick(x, f);
        CHECK(x.phase != Phase::Rest);
        f.minHealthPct = 60;
        x.restDue = true;
        x.phase = Phase::Begin;
        CHECK(Tick(x, f) == "rest" && x.phase == Phase::Rest && !x.restForJoiners);
        f.minHealthPct = 85;
        // ... and so does a meal wait.
        Run y = Start(&plan);
        Tick(y, f);
        Tick(y, f);
        CHECK(y.restForJoiners && y.phase == Phase::Rest);
        y.phase = Phase::Begin;
        f.eating = 1;
        Tick(y, f);
        CHECK(y.phase == Phase::Rest && y.restForEating && !y.restForJoiners);
        f.eating = 0;
    }

    // A step's reset (also a resume) forgets the hold.
    Run reset = Start(&plan);
    f.anyInCombat = false;
    CHECK(Tick(reset, f).empty() && reset.joinerRest && !reset.joinerHeld.empty());
    ResetStep(reset);
    reset.joinerRests = 2;
    ResetStep(reset);
    CHECK(!reset.joinerRest && reset.joinerHeld.empty() && reset.joinerNote.empty());
    CHECK(reset.joinerRests == 0 && !reset.restForJoiners);

    // The nearest unit is the one judged: a quiet pick goes out, and the
    // loud one is held when it is picked.
    f.anyInCombat = false;
    f.units = {Joiners(31, 4852, 54.7f, 9), Joiners(32, 4852, 30.0f, 1)};
    Run two = Start(&plan);
    CHECK(Tick(two, f) == "pullback 32");
}


// Six loud packs in one pull step: the joiner rests (60 s each when the party
// stays under the end lines) are not step idle time and are three a step, so
// the run neither pauses on the 300 s idle ceiling nor rests a fourth time.
static void TestJoinerRestsAreBudgetedAndNotIdleTime()
{
    DungeonPlan plan;
    plan.steps = {PullStep(40, {4852})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.joinerCeiling = 6;
    f.minHealthPct = 85; // above the start lines, under the end lines: it never gets rested
    std::vector<UnitFact> alive;
    for (std::uint64_t g = 31; g <= 36; ++g)
        alive.push_back(Joiners(g, 4852, 40.0f + static_cast<float>(g), 9));
    int rests = 0;
    std::vector<std::string> notes;
    for (std::size_t unit = 0; unit < 6; ++unit)
    {
        f.units = std::vector<UnitFact>(alive.begin() + static_cast<long>(unit), alive.end());
        f.anyInCombat = false;
        std::string order;
        Phase prev = r.phase;
        for (int i = 0; i < 400 && order.compare(0, 9, "pullback ") != 0; ++i)
        {
            order = Tick(r, f);
            if (r.phase == Phase::Rest && prev != Phase::Rest)
                ++rests;
            prev = r.phase;
            CHECK(r.state == State::Running);
        }
        CHECK(order == "pullback " + std::to_string(31 + unit));
        notes.push_back(r.note);
        f.anyInCombat = true;
        for (int i = 0; i < 3; ++i)
            Tick(r, f);
        CHECK(r.state == State::Running);
    }
    CHECK(rests == 3);
    CHECK(r.joinerRests == 3);
    CHECK(notes[3] == "9 joiners expected, rest budget used");
    CHECK(notes[5] == "9 joiners expected, rest budget used");
    CHECK(notes[0] != notes[3]);
    // 180 s of joiner rest are not idle time.
    CHECK(r.stepIdleMs < kStepIdleCapMs / 2);
}

// A loud pick the party is already rested for spends no rest slot: the three
// slots go to the rests that really happen later in the step.
static void TestJoinerRestSlotsAreSpentByRealRestsOnly()
{
    DungeonPlan plan;
    plan.steps = {PullStep(40, {4852})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.joinerCeiling = 6;
    std::vector<UnitFact> alive;
    for (std::uint64_t g = 31; g <= 36; ++g)
        alive.push_back(Joiners(g, 4852, 40.0f + static_cast<float>(g), 9));
    int rests = 0;
    std::vector<std::string> notes;
    for (std::size_t unit = 0; unit < 6; ++unit)
    {
        // The first two picks find the party full, the rest find it at 85 %.
        f.minHealthPct = unit < 2 ? 100 : 85;
        f.units = std::vector<UnitFact>(alive.begin() + static_cast<long>(unit), alive.end());
        f.anyInCombat = false;
        std::string order;
        Phase prev = r.phase;
        for (int i = 0; i < 400 && order.compare(0, 9, "pullback ") != 0; ++i)
        {
            order = Tick(r, f);
            if (r.phase == Phase::Rest && prev != Phase::Rest)
                ++rests;
            prev = r.phase;
            CHECK(r.state == State::Running);
        }
        CHECK(order == "pullback " + std::to_string(31 + unit));
        notes.push_back(r.note);
        if (unit == 1)
            CHECK(r.joinerRests == 0 && rests == 0);
        f.anyInCombat = true;
        for (int i = 0; i < 3; ++i)
            Tick(r, f);
    }
    CHECK(rests == 3 && r.joinerRests == 3);
    CHECK(notes[5] == "9 joiners expected, rest budget used");
    CHECK(notes[4] != notes[5]);
}

// Both keys on (AiPlayerbot.DungeonRunAggroEstimate and DungeonRunRezHold): a loud pull, a death in its
// fight, then the regroup, then the next loud pick. The joiner rest is a pull-step thing (cleared by
// ResetStep, kept by BeginRegroup, which a single death reaches without a ResetStep); the rez hold's clocks
// are a regroup thing (restarted by BeginRegroup, never by a step reset). Neither may reset the other.
static void TestAggroEstimateAndRezHoldDoNotCrossTheirResets()
{
    using namespace TortoiseBots::RezHold;
    DungeonPlan plan;
    plan.steps = {PullStep(40, {4852})};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.joinerCeiling = 6;
    f.rezHold = true;
    f.minHealthPct = 85; // under the end lines: the joiner rest really starts
    f.units = {Joiners(21, 4852, 40.0f, 9), Joiners(22, 4852, 45.0f, 9)};
    // 1. Loud pick 21: the joiner rest starts (not idle time), then the pull goes out.
    for (int i = 0; i < 4 && r.phase != Phase::Rest; ++i)
        Tick(r, f);
    CHECK(r.phase == Phase::Rest && r.restForJoiners && r.joinerRests == 1 && r.joinerHeld.count(21));
    f.minHealthPct = 100;
    std::string order;
    for (int i = 0; i < 20 && order.compare(0, 9, "pullback ") != 0; ++i)
        order = Tick(r, f);
    CHECK(order == "pullback 21");
    // 2. The fight: the mage (index 2) falls, 21 dies; 22 is still up. Out of combat the single death
    // starts the regroup with no ResetStep.
    f.anyInCombat = true;
    for (int i = 0; i < 3; ++i)
        Tick(r, f);
    f.dead = 1;
    f.alive = 4;
    f.units = {Joiners(22, 4852, 45.0f, 9)};
    f.rezMembers = RezRoster(true, 500);
    Tick(r, f);
    f.anyInCombat = false;
    for (int i = 0; i < 4 && !r.regroup; ++i)
        Tick(r, f);
    CHECK(r.regroup && r.regroupMs < 3 * kTickMs && r.rezAbleMs == 0 && r.reviveBaseMs == 0);
    // The regroup's restart left the step's joiner state alone: 21 was rested for, one slot is spent.
    CHECK(r.joinerRests == 1 && r.joinerHeld.count(21) == 1);
    // 3. The rez hold decides the regroup (well past the old 15 s) and the joiner rest does not move.
    std::uint32_t revivedAt = 0;
    std::uint32_t const idleBefore = r.stepIdleMs;
    for (std::uint32_t ms = kTickMs; ms <= 200000 && !revivedAt; ms += kTickMs)
    {
        if (Tick(r, f) == "revive party")
            revivedAt = ms;
        CHECK(r.joinerRests == 1 && r.joinerHeld.count(21) == 1 && !r.joinerRest);
    }
    CHECK(revivedAt >= kHoldMs && revivedAt <= kHoldMs + 4 * kTickMs);
    CHECK(r.stepIdleMs == idleBefore); // a regroup is not step idle time
    // The invariant in both directions: a step reset leaves the regroup clocks, a regroup start leaves
    // the joiner state.
    {
        Run c = r;
        c.rezAbleMs = 7000;
        c.reviveBaseMs = 9000;
        c.regroupMs = 11000;
        ResetStep(c);
        CHECK(c.rezAbleMs == 7000 && c.reviveBaseMs == 9000 && c.regroupMs == 11000);
        CHECK(c.joinerRests == 0 && c.joinerHeld.empty() && !c.restForJoiners);
        Run d = r;
        d.restForJoiners = true;
        BeginRegroup(d);
        CHECK(d.joinerRests == 1 && d.joinerHeld.count(21) == 1 && d.restForJoiners);
    }
    // 4. The party is raised: the regroup ends, the step goes on with 22. It is rested for (a second
    // slot, 21's kept), the pull goes out, and the whole of it was no step idle time.
    f.dead = 0;
    f.alive = 5;
    f.rezMembers = RezRoster(false, 500);
    f.minHealthPct = 85;
    for (int i = 0; i < 12 && r.regroup; ++i)
        Tick(r, f);
    CHECK(!r.regroup);
    for (int i = 0; i < 6 && r.phase != Phase::Rest; ++i)
        Tick(r, f);
    CHECK(r.phase == Phase::Rest && r.restForJoiners && r.joinerRests == 2);
    CHECK(r.joinerHeld.count(21) == 1 && r.joinerHeld.count(22) == 1);
    CHECK(r.note.find("9 joiners expected") != std::string::npos);
    f.minHealthPct = 100;
    order.clear();
    for (int i = 0; i < 20 && order.compare(0, 9, "pullback ") != 0; ++i)
        order = Tick(r, f);
    CHECK(order == "pullback 22");
    CHECK(r.state == State::Running && r.stepIdleMs < kStepIdleCapMs / 2);
    // 5. A wipe in a later step reset (ResetStep, then BeginRegroup): fresh joiner budget and fresh clocks.
    ResetStep(r);
    BeginRegroup(r);
    CHECK(r.joinerRests == 0 && r.joinerHeld.empty() && r.regroupMs == 0 && r.rezAbleMs == 0);
}

// Both keys off: a run fed the estimate's joiners and the rez roster (the glue reads neither with the keys
// off, but a stray value must change nothing) ticks exactly like one fed neither, through a loud pull, a
// death in its fight and the regroup, tick by tick.
static void TestBothKeysOffChangeNothing()
{
    DungeonPlan plan;
    plan.steps = {PullStep(40, {4852})};
    Run plain = Start(&plan);
    Run noisy = Start(&plan);
    Facts a = Party(0);
    a.minHealthPct = 85;
    a.units = {Unit(21, 4852, 40.0f), Unit(22, 4852, 45.0f)};
    a.rezzer = true;
    Facts b = a;
    b.units = {Joiners(21, 4852, 40.0f, 9, true), Joiners(22, 4852, 45.0f, 9)};
    b.rezMembers = RezRoster(true, 0, 500.0f);
    CHECK(b.joinerCeiling == 0 && !b.rezHold);
    for (int i = 0; i < 300; ++i)
    {
        if (i == 20)
        {
            a.minHealthPct = b.minHealthPct = 100;
        }
        if (i == 30)
            a.anyInCombat = b.anyInCombat = true;
        if (i == 40)
        {
            a.anyInCombat = b.anyInCombat = false;
            a.dead = b.dead = 1;
            a.alive = b.alive = 4;
            a.units = {Unit(22, 4852, 45.0f)};
            b.units = {Joiners(22, 4852, 45.0f, 9)};
        }
        if (i == 150)
        {
            a.dead = b.dead = 0;
            a.alive = b.alive = 5;
        }
        std::string const oa = Tick(plain, a);
        std::string const ob = Tick(noisy, b);
        CHECK(oa == ob);
        CHECK(plain.note == noisy.note && plain.phase == noisy.phase && plain.regroup == noisy.regroup);
        CHECK(plain.regroupMs == noisy.regroupMs && plain.reviveSent == noisy.reviveSent);
        CHECK(StatusLine(plain) == StatusLine(noisy));
        CHECK(!noisy.joinerRest && noisy.joinerHeld.empty() && noisy.joinerRests == 0 && noisy.rezAbleMs == 0 &&
              noisy.reviveBaseMs == 0);
    }
}

// The pull stand (AiPlayerbot.DungeonRunPullStand, HoldForShot). Shoot Bow: the
// module's GetRange 29.5, so the tank shoots from where it stands within 24.5 yd.
static Facts StandParty()
{
    Facts f = Party(0);
    f.pullStand = true;
    f.leadShotYd = 29.5f;
    return f;
}

static UnitFact Seen(std::uint64_t guid, std::uint32_t entry, float dist, bool los = true)
{
    UnitFact u = Unit(guid, entry, dist);
    u.losLead = los;
    return u;
}

static bool Has(const std::string& text, const std::string& part)
{
    return text.find(part) != std::string::npos;
}

// The glue's read of a stand for the unit the policy asked for (Run::shotStandFor).
static void TestPullNoteCarriesTheJoinerCount()
{
    DungeonPlan plan;
    plan.steps = {PullStep(40, {4852})};
    Facts f = Party(0);
    f.joinerCeiling = 6;
    std::string order;

    // Ceiling 6, the glue read 3 joiners for the unit: the count rides on the note.
    {
        Run r = Start(&plan);
        UnitFact u = Joiners(31, 4852, 40.0f, 3);
        u.joinersRead = true;
        f.units = {u};
        order.clear();
        PullUnit(r, f, f.units[0], order);
        CHECK(order == "pullback 31" && r.note == "pulling 4852; 3 joiners expected");
    }
    // Ceiling 6, the unit was held or out of the glue's index, so no count this step: the note says so,
    // and it never claims the stale count below.
    {
        Run r = Start(&plan);
        UnitFact u = Unit(31, 4852, 40.0f);
        u.joiners = 5;   // a stale read kept, but marked not read
        u.joinersPatrol = true;
        f.units = {u};
        order.clear();
        PullUnit(r, f, f.units[0], order);
        CHECK(order == "pullback 31" && r.note == "pulling 4852; joiners not read");
    }
    // Ceiling on, the pull stand on: the count goes after the stand text.
    {
        Run r = Start(&plan);
        Facts s = StandParty();
        s.joinerCeiling = 6;
        UnitFact u = Joiners(11, 4851, 24.5f, 3);
        u.joinersRead = true;
        u.losLead = true;
        s.units = {u};
        CHECK(Tick(r, s) == "pullback 11");
        CHECK(r.note ==
            "pulling 4851 0x000000000000000b (order 1; shot from where it stands); 3 joiners expected");
    }

    // Ceiling off: a stray count and stray read in the facts change nothing, both stand variants.
    f.joinerCeiling = 0;
    {
        Run r = Start(&plan);
        UnitFact u = Joiners(31, 4852, 40.0f, 9);
        u.joinersRead = true;
        f.units = {u};
        order.clear();
        PullUnit(r, f, f.units[0], order);
        CHECK(order == "pullback 31" && r.note == "pulling 4852");
    }
    {
        Run r = Start(&plan);
        Facts s = StandParty();
        UnitFact u = Seen(11, 4851, 24.5f);
        u.joiners = 9;
        u.joinersRead = true;
        u.joinersPatrol = true;
        s.units = {u};
        CHECK(Tick(r, s) == "pullback 11");
        CHECK(r.note == "pulling 4851 0x000000000000000b (order 1; shot from where it stands)");
    }
}

// The glue's read of a stand for the unit the policy asked for (Run::shotStandFor).
static void GiveStand(Facts& f, const Run& r, float x, float yd)
{
    f.shotStandGuid = r.shotStandFor;
    f.shotStandKnown = true;
    f.shotStandX = x;
    f.shotStandY = 0.0f;
    f.shotStandZ = 0.0f;
    f.shotStandYd = yd;
}

static void ClearStand(Facts& f)
{
    f.shotStandGuid = 0;
    f.shotStandKnown = false;
}

// In sight and within 24.5 yd (the edge included), standing still: the order on the first tick, its note
// carrying the guid, the order count and how the shot stood.
static void TestPullShootsFromStand()
{
    DungeonPlan plan;
    plan.steps = {PullStep(40, {4851})};
    Run r = Start(&plan);
    Facts f = StandParty();
    f.units = {Seen(11, 4851, 24.5f)};
    CHECK(Tick(r, f) == "pullback 11");
    CHECK(r.phase == Phase::AwaitPull);
    CHECK(r.note == "pulling 4851 0x000000000000000b (order 1; shot from where it stands)");
    CHECK(r.shotStandTries[11] == 0 && r.shotStandFor == 0);
    // No fight: the re-order after kPullNoFightMs has a note of its own (the trace keeps only changes).
    std::string order;
    for (int i = 0; i < 40 && order.empty(); ++i)
        order = Tick(r, f);
    CHECK(order == "pullback 11");
    CHECK(r.note == "pulling 4851 0x000000000000000b (order 2; shot from where it stands)");
}

// A moving tank holds the order up to kShotStillWaitMs, then it goes out anyway; one that stops goes at once.
static void TestPullWaitsForStillTank()
{
    DungeonPlan plan;
    plan.steps = {PullStep(40, {4851})};
    {
        Run r = Start(&plan);
        Facts f = StandParty();
        f.units = {Seen(11, 4851, 20.0f)};
        f.leadMoving = true;
        for (std::uint32_t ms = 0; ms < kShotStillWaitMs; ms += kTickMs)
        {
            CHECK(Tick(r, f).empty());
            CHECK(r.note == "waiting for the tank to stand still before pulling 4851 0x000000000000000b");
        }
        CHECK(Tick(r, f) == "pullback 11");
        CHECK(Has(r.note, "(order 1; shot from where it stands, still moving)"));
    }
    {
        Run r = Start(&plan);
        Facts f = StandParty();
        f.units = {Seen(11, 4851, 20.0f)};
        f.leadMoving = true;
        CHECK(Tick(r, f).empty());
        f.leadMoving = false;
        CHECK(Tick(r, f) == "pullback 11");
    }
}

// 27 yd (inside the shot's 29.5, outside its 24.5 with the margin): the stand is read, the party walks to
// it, and the order goes out from there.
static void TestPullWalksToStandOutOfRange()
{
    DungeonPlan plan;
    plan.steps = {PullStep(40, {4851})};
    Run r = Start(&plan);
    Facts f = StandParty();
    f.units = {Seen(11, 4851, 27.0f)};
    CHECK(Tick(r, f).empty());
    CHECK(r.shotStandFor == 11);
    CHECK(r.note == "reading a firing stand for 4851 0x000000000000000b");
    GiveStand(f, r, 5.0f, 23.0f);
    CHECK(Tick(r, f) == "move 5.0 0.0 0.0");
    CHECK(r.phase == Phase::Walk && r.shotStandTries[11] == 1 && r.shotStandFor == 0);
    CHECK(r.note == "to firing stand 1 for 4851 0x000000000000000b (23.0 yd from it; was 27.0 yd, los yes)");
    ClearStand(f);
    f.leadX = 1.0f;
    f.leadMoving = true;
    f.units[0].distLead = 26.0f;
    CHECK(Tick(r, f).empty());
    CHECK(Has(r.note, "to firing stand 1 for 4851"));
    f.leadX = 5.0f;
    f.leadMoving = false;
    f.units[0].distLead = 22.0f;
    CHECK(Tick(r, f) == "pullback 11");
    CHECK(r.note == "pulling 4851 0x000000000000000b (order 1; shot from firing stand 1)");
    // The anchor is the stand: the pullback's return leg and the next gather are there.
    CHECK(r.anchorKnown && r.anchorX == 5.0f);
}

// ... and a firing stand 3 yd below the tank: the stand walk ends by the same rule, the pull goes out from
// the tank's spot instead of giving the stand up as unreachable.
static void TestFiringStandOnALedgeCountsByGroundDistance()
{
    DungeonPlan plan;
    plan.steps = {PullStep(40, {4851})};
    Run r = Start(&plan);
    Facts f = StandParty();
    f.units = {Seen(11, 4851, 27.0f)};
    CHECK(Tick(r, f).empty());
    GiveStand(f, r, 0.0f, 23.0f);
    f.leadX = 5.4f;
    f.leadZ = 3.0f; // 6.2 yd from the stand in 3D, 5.4 on the ground
    CHECK(Tick(r, f) == "move 0.0 0.0 0.0");
    ClearStand(f);
    f.units[0].distLead = 22.0f;
    std::string order;
    for (int i = 0; i < 300 && order.compare(0, 9, "pullback ") != 0 && r.state == State::Running; ++i)
        order = Tick(r, f);
    CHECK(r.state == State::Running);
    CHECK(order == "pullback 11");
    CHECK(Has(r.note, "shot from firing stand 1"));
}

// 22 yd, in range but out of sight: a stand too; a tank still moving on arrival is waited for.
static void TestPullWalksToStandNoLos()
{
    DungeonPlan plan;
    plan.steps = {PullStep(40, {4851})};
    Run r = Start(&plan);
    Facts f = StandParty();
    f.units = {Seen(11, 4851, 22.0f, false)};
    CHECK(Tick(r, f).empty());
    CHECK(r.shotStandFor == 11);
    GiveStand(f, r, -6.0f, 20.0f);
    CHECK(Tick(r, f) == "move -6.0 0.0 0.0");
    CHECK(Has(r.note, "was 22.0 yd, los no"));
    ClearStand(f);
    f.leadX = -6.0f;
    f.leadMoving = true;
    f.units[0] = Seen(11, 4851, 20.0f, true);
    CHECK(Tick(r, f).empty());
    CHECK(Has(r.note, "waiting for the tank to stand still"));
    f.leadMoving = false;
    CHECK(Tick(r, f) == "pullback 11");
    CHECK(Has(r.note, "shot from firing stand 1"));
}

// At most kShotStandTries stands per unit; then, and with no stand found, the order as before.
static void TestPullStandTriesBounded()
{
    DungeonPlan plan;
    plan.steps = {PullStep(40, {4851})};
    {
        Run r = Start(&plan);
        Facts f = StandParty();
        f.units = {Seen(11, 4851, 40.0f, false)};
        f.anchorPathRead = true; // the glue's answer to the way back (the stand before)
        std::vector<std::string> orders;
        for (int i = 0; i < 30 && (orders.empty() || orders.back().compare(0, 9, "pullback ") != 0); ++i)
        {
            if (r.shotStandFor)
                GiveStand(f, r, f.leadX + 6.0f, 34.0f);
            else
                ClearStand(f);
            std::string const o = Tick(r, f);
            if (!o.empty())
                orders.push_back(o);
            if (r.phase == Phase::Walk)
                f.leadX = r.walkX; // arrives on the next tick; still not in sight
        }
        CHECK(orders.size() == 3);
        CHECK(orders[0] == "move 6.0 0.0 0.0" && orders[1] == "move 12.0 0.0 0.0" && orders[2] == "pullback 11");
        CHECK(r.shotStandTries[11] == kShotStandTries);
        CHECK(r.note == "pulling 4851 0x000000000000000b (order 1; firing stands used up (2))");
    }
    {
        Run r = Start(&plan);
        Facts f = StandParty();
        f.units = {Seen(11, 4851, 40.0f, false)};
        CHECK(Tick(r, f).empty());
        f.shotStandGuid = 11; // read, nothing found
        f.shotStandKnown = false;
        CHECK(Tick(r, f) == "pullback 11");
        CHECK(Has(r.note, "(order 1; no firing stand found)"));
    }
}

// A body-pull tank (leadShotYd 0) with the key on: the same orders as with the key off, tick by tick
// (only the order's note is longer). The key off with stray stand facts: everything the same, notes too.
static void TestPullNoShotDataKeepsOldOrder()
{
    DungeonPlan plan;
    plan.steps = {PullStep(40, {4851, 4852})};
    for (int variant = 0; variant < 2; ++variant)
    {
        Run plain = Start(&plan);
        Run other = Start(&plan);
        Facts a = Party(0);
        // 12 is shootable from where the tank stands, 11 nearer and out of sight: F2 would pick 12.
        a.units = {Unit(11, 4851, 18), Unit(12, 4852, 23)};
        a.maxMemberYd = 35;
        Facts b = a;
        if (variant == 0)
        {
            b.pullStand = true; // key on, no ranged pull
            b.leadShotYd = 0.0f;
        }
        else
        {
            b.pullStand = false; // key off, stray facts
            b.leadShotYd = 29.5f;
            b.shotStandGuid = 12;
            b.shotStandKnown = true;
            b.shotStandX = 9.0f;
        }
        b.units[0].losLead = false;
        b.units[1].losLead = true;
        b.leadMoving = a.leadMoving = true;
        bool sawFirst = false;
        for (int i = 0; i < 120; ++i)
        {
            if (i == 2)
                a.maxMemberYd = b.maxMemberYd = 5;
            if (i == 60)
                a.anyInCombat = b.anyInCombat = true;
            if (i == 70)
            {
                a.anyInCombat = b.anyInCombat = false;
                a.units = {Unit(11, 4851, 18)};
                b.units = {a.units[0]};
            }
            std::string const oa = Tick(plain, a);
            std::string const ob = Tick(other, b);
            CHECK(oa == ob);
            CHECK(oa != "pullback 12" || i >= 70 || sawFirst); // the nearest (11) goes first
            sawFirst = sawFirst || oa == "pullback 11";
            CHECK(plain.phase == other.phase && plain.step == other.step && plain.state == other.state);
            CHECK(other.shotStandFor == 0 && !other.shotStandWalk && !other.shotStandPrep);
            if (variant == 1)
            {
                CHECK(plain.note == other.note);
                CHECK(StatusLine(plain) == StatusLine(other));
            }
            else if (oa.compare(0, 9, "pullback ") == 0)
                CHECK(Has(other.note, "no ranged weapon pull: stand rule off"));
        }
        CHECK(sawFirst);
    }
}

// (review 1) A shot range at or under the margin (4.5, 5.0): the glue reads no stand for it, so the rule
// is off and the order goes out on the first tick (it used to read "reading a firing stand" for 300 s).
static void TestPullStandNeedsMoreRangeThanTheMargin()
{
    DungeonPlan plan;
    plan.steps = {PullStep(40, {4851})};
    for (float shot : {4.5f, 5.0f})
    {
        Run r = Start(&plan);
        Facts f = StandParty();
        f.leadShotYd = shot;
        f.units = {Seen(11, 4851, 20.0f, false)};
        CHECK(Tick(r, f) == "pullback 11");
        CHECK(Has(r.note, "no ranged weapon pull: stand rule off") && r.shotStandFor == 0);
    }
}

// (review 2, 6) A stand request lives one tick: the next tick answers or drops it, whether the gate runs
// (a still-wait) or not (a fight); the hold's prep, walk and wait end with any tick the gate did not run on.
static void TestStandRequestIsReadOnce()
{
    DungeonPlan plan;
    plan.steps = {PullStep(40, {4851})};
    {
        Run r = Start(&plan);
        Facts f = StandParty();
        f.units = {Seen(11, 4851, 40.0f, false)};
        CHECK(Tick(r, f).empty());
        CHECK(r.shotStandFor == 11 && r.shotStandPrep);
        f.anyInCombat = true; // a fight comes before the read is used
        Tick(r, f);
        CHECK(r.shotStandFor == 0 && !r.shotStandPrep);
        for (int i = 0; i < 5; ++i)
            Tick(r, f);
        CHECK(r.shotStandFor == 0 && !r.shotStandPrep && !r.shotStandWalk && r.shotStillMs == 0);
    }
    {
        Run r = Start(&plan);
        Facts f = StandParty();
        f.units = {Seen(11, 4851, 40.0f, false)};
        CHECK(Tick(r, f).empty());
        GiveStand(f, r, 10.0f, 30.0f);
        f.leadMoving = true; // the read tick goes to the still-wait: the read is dropped, not asked again
        CHECK(Tick(r, f).empty());
        CHECK(r.shotStandFor == 0 && Has(r.note, "waiting for the tank to stand still"));
        CHECK(Tick(r, f).empty());
        CHECK(r.shotStandFor == 0);
    }
    {
        // A gather breaks the stand walk off: the hold ends (prep and walk off), the stand stays counted.
        Run r = Start(&plan);
        Facts f = StandParty();
        f.units = {Seen(11, 4851, 40.0f, false)};
        Tick(r, f);
        GiveStand(f, r, 10.0f, 30.0f);
        CHECK(Tick(r, f) == "move 10.0 0.0 0.0");
        ClearStand(f);
        f.maxMemberYd = 30.0f;
        CHECK(Tick(r, f) == "move 0.0 0.0 0.0"); // the gather on the lead bot's spot
        CHECK(!r.shotStandPrep && !r.shotStandWalk && r.shotStandTries[11] == 1);
    }
}

// (review 4) A stand walk that gains nothing for kWalkRetryMs, or runs past kShotWalkCapMs, gives the stand
// up and orders from where the tank is; the run never pauses for it.
static void TestStandWalkStuckOrdersFromHere()
{
    DungeonPlan plan;
    plan.steps = {PullStep(40, {4851})};
    for (int longWalk = 0; longWalk < 2; ++longWalk)
    {
        Run r = Start(&plan);
        Facts f = StandParty();
        f.units = {Seen(11, 4851, 120.0f, false)};
        Tick(r, f);
        GiveStand(f, r, 100.0f, 24.0f);
        CHECK(Tick(r, f) == "move 100.0 0.0 0.0");
        ClearStand(f);
        f.anchorPathRead = true;
        std::string order;
        std::uint32_t ms = 0;
        for (; ms < 60000 && order.empty(); ms += kTickMs)
        {
            if (longWalk)
                f.leadX += 2.1f; // headway every tick, never there in time
            order = Tick(r, f);
            CHECK(r.state == State::Running);
        }
        CHECK(order == "pullback 11");
        CHECK(Has(r.note, "(order 1; firing stand unreachable: ordering from here)"));
        // The give-up tick holds once more (the way back is checked on the next): the order a tick later.
        CHECK(longWalk ? ms == kShotWalkCapMs + kTickMs : ms == kWalkRetryMs + kTickMs);
        CHECK(r.shotStandTries[11] == kShotStandTries);
    }
}

// (review 4) The stand walk is driven on every tick while the way back to the old anchor is read on every
// other one (the glue reads the path the tick after it is asked for).
static void TestStandWalkIsDrivenEveryTick()
{
    DungeonPlan plan;
    plan.steps = {PullStep(40, {4851})};
    Run r = Start(&plan);
    Facts f = StandParty();
    f.units = {Seen(11, 4851, 40.0f, false)};
    Tick(r, f);
    GiveStand(f, r, 20.0f, 20.0f);
    CHECK(Tick(r, f) == "move 20.0 0.0 0.0");
    ClearStand(f);
    r.anchorKnown = true; // the anchor the walk leaves: 2 yd behind the tank
    r.anchorX = -2.0f;
    for (int i = 0; i < 5; ++i)
    {
        f.anchorPathRead = r.anchorPathWanted;
        f.leadX += 3.0f;
        f.leadMoving = true;
        CHECK(Tick(r, f).empty());
        CHECK(Has(r.note, "to firing stand 1 for 4851"));
    }
}

// (review round 2) The stuck give-up holds its own tick and drops the hold's pass on the way back, so the
// next tick checks the anchor from where the tank got stuck: more than kStragglerDzYd below it (another
// floor) the party gathers back there first; the same floor with a way back, the order goes out.
static void TestStuckStandChecksTheWayBackFirst()
{
    DungeonPlan plan;
    plan.steps = {PullStep(40, {4851})};
    for (int otherFloor = 0; otherFloor < 2; ++otherFloor)
    {
        Run r = Start(&plan);
        Facts f = StandParty();
        f.units = {Seen(11, 4851, 120.0f, false)};
        r.anchorKnown = true; // the anchor: (0, 0, 0)
        Tick(r, f);
        GiveStand(f, r, 100.0f, 24.0f);
        CHECK(Tick(r, f) == "move 100.0 0.0 0.0");
        ClearStand(f);
        f.leadX = 30.0f; // stuck there
        f.leadZ = otherFloor ? -12.0f : 0.0f;
        std::string order;
        int ticks = 0;
        for (; ticks < 30 && order.empty() && !Has(r.note, "firing stand unreachable"); ++ticks)
            order = Tick(r, f);
        CHECK(order.empty()); // the give-up tick sends nothing
        CHECK(r.note == "firing stand unreachable for 4851 0x000000000000000b: checking the way back before the order");
        CHECK(!r.shotStandPrep && r.shotStandTries[11] == kShotStandTries && r.state == State::Running);
        if (otherFloor)
        {
            CHECK(Tick(r, f) == "move 0.0 0.0 0.0"); // the gather back to the anchor
            CHECK(r.phase == Phase::Gather);
        }
        else
        {
            f.anchorPathRead = true; // the glue's read: the way back is there
            CHECK(Tick(r, f) == "pullback 11");
            CHECK(Has(r.note, "(order 1; firing stand unreachable: ordering from here)"));
        }
    }
}

// (review round 2) The pick switching to another unit in the middle of a hold: the new unit's first tick
// checks the way back (the hold's pass belonged to the old unit).
static void TestPickSwitchChecksTheWayBack()
{
    DungeonPlan plan;
    plan.steps = {PullStep(40, {4851})};
    Run r = Start(&plan);
    Facts f = StandParty();
    f.units = {Seen(11, 4851, 40.0f, false)};
    r.anchorKnown = true;
    r.anchorX = -10.0f; // the lead bot is off it: the way back has to be read
    f.anchorPathRead = true;
    CHECK(Tick(r, f).empty());
    CHECK(r.shotStandPrep && r.shotHoldGuid == 11);
    f.anchorPathRead = false;
    f.units = {Seen(11, 4851, 40.0f, false), Seen(12, 4851, 30.0f, false)}; // 12 is nearer now
    CHECK(Tick(r, f).empty());
    CHECK(Has(r.note, "checking the way back"));
    CHECK(!r.shotStandPrep);
    // A move step's hop pull (PullUnit alone, no FightStep): the same.
    DungeonPlan move;
    move.steps = {Step(PlanStepKind::Move, 50, 0, 0)};
    Run m = Start(&move);
    Facts g = StandParty();
    g.hopUnits = {Seen(11, 4851, 40.0f, false)};
    m.anchorKnown = true;
    m.anchorX = -10.0f;
    g.anchorPathRead = true;
    CHECK(Tick(m, g).empty());
    CHECK(m.shotStandPrep && m.shotHoldGuid == 11);
    g.anchorPathRead = false;
    g.hopUnits = {Seen(11, 4851, 40.0f, false), Seen(12, 4851, 30.0f, false)};
    CHECK(Tick(m, g).empty());
    CHECK(Has(m.note, "checking the way back") && !m.shotStandPrep);
}

// F2: among units of the same kill_first rank one the tank can shoot from where it stands goes before a
// nearer one it cannot; the rank still comes first; the key off picks the nearest.
static void TestPickPackPullPrefersShootable()
{
    DungeonPlan plan;
    plan.steps = {PullStep(40, {4851, 4852})};
    {
        Run r = Start(&plan);
        Facts f = StandParty();
        f.units = {Seen(11, 4851, 18.0f, false), Seen(12, 4852, 23.0f, true)};
        CHECK(Tick(r, f) == "pullback 12");
    }
    {
        Run r = Start(&plan);
        Facts f = StandParty();
        f.pullStand = false;
        f.units = {Seen(11, 4851, 18.0f, false), Seen(12, 4852, 23.0f, true)};
        CHECK(Tick(r, f) == "pullback 11");
    }
    {
        DungeonPlan ranked = plan;
        ranked.killFirst = {4851};
        Run r = Start(&ranked);
        Facts f = StandParty();
        f.units = {Seen(11, 4851, 18.0f, false), Seen(12, 4852, 23.0f, true)};
        CHECK(Tick(r, f).empty()); // the kill_first unit, out of sight: its stand is read
        CHECK(r.shotStandFor == 11);
    }
}

// The ticks a stand rule holds a pull are one pull: a pack with a fruitless pull behind it is not skipped
// while the tank walks to its stand (FightStep's kFruitlessPulls).
static void TestStandTicksAreNotFruitlessPulls()
{
    DungeonPlan plan;
    plan.steps = {PullStep(40, {4851})};
    Run r = Start(&plan);
    Facts f = StandParty();
    f.units = {Seen(11, 4851, 20.0f), Seen(12, 4851, 40.0f)};
    CHECK(Tick(r, f) == "pullback 11");
    // 11 never comes (no fight); it is ordered again, then passed, and 12 needs a stand.
    f.units[0].losLead = false;
    f.units[0].distLead = 30.0f;
    f.anchorPathRead = true;
    std::string order;
    for (int i = 0; i < 200 && r.step == 0 && order != "pullback 12"; ++i)
    {
        if (r.shotStandFor)
            GiveStand(f, r, f.leadX + 3.5f + static_cast<float>(i % 3), 24.0f);
        else
            ClearStand(f);
        order = Tick(r, f);
        if (r.phase == Phase::Walk)
        {
            // Two ticks on the way, then there.
            f.leadMoving = true;
            Tick(r, f);
            Tick(r, f);
            f.leadMoving = false;
            f.leadX = r.walkX;
        }
    }
    CHECK(r.step == 0 && r.state == State::Running);
    CHECK(order == "pullback 12");
}

// The stand helper: beyond range, the ranged reach's own point; in range but out of sight, the first path
// point nearer the unit that sees it; nothing in sight, no stand.
static void TestShotStandOnPath()
{
    using ai::ApproachPoint;
    auto P = [](float x, float y) { ApproachPoint p; p.x = x; p.y = y; return p; };
    ApproachPoint const unit = P(0, 0);
    auto seesAll = [](const ApproachPoint&) { return true; };
    {
        std::vector<ApproachPoint> path = {P(40, 0), P(30, 0), P(20, 0), P(0, 0)};
        ShotStand const s = ShotStandOnPath(path, unit, 24.5f, seesAll);
        CHECK(s.known && std::fabs(s.x - 24.5f) < 0.01f);
    }
    {
        // 22 yd from it, the pillar hides it until the corner at (10, 10).
        std::vector<ApproachPoint> path = {P(22, 0), P(18, 6), P(10, 10), P(0, 0)};
        auto corner = [](const ApproachPoint& p) { return p.x <= 10.0f; };
        ShotStand const s = ShotStandOnPath(path, unit, 24.5f, corner);
        CHECK(s.known && s.x == 10.0f && s.y == 10.0f);
        auto none = [](const ApproachPoint&) { return false; };
        CHECK(!ShotStandOnPath(path, unit, 24.5f, none).known);
        CHECK(!ShotStandOnPath(path, unit, 0.0f, seesAll).known);
    }
}

// Uldaman run 4, step 38: a fight past the 180 s ceiling paused the run, the
// fight ended 15 s later, and the run stayed paused for 470 s. A ceiling pause
// goes on by itself once the party is out of combat kCeilingQuietMs, once per
// step; a player's pause and every other pause never do.
static void TestCeilingPauseResumesByItself()
{
    DungeonPlan plan;
    PlanStep wait = Step(PlanStepKind::WaitObject, 1, 2, 3);
    wait.entry = 124369;
    wait.param = 1;
    plan.steps = {wait, Step(PlanStepKind::Move, 9)};
    auto ceilingPause = [&](Run& r)
    {
        r.fighting = true;
        r.fightMs = kFightCapMs + 1000;
        Facts g = Party(0);
        g.anyInCombat = true;
        Tick(r, g);
    };
    // The fight past the ceiling pauses; ticks with the party in combat stay paused.
    Run r = Start(&plan);
    ceilingPause(r);
    CHECK(r.state == State::Paused && r.ceilingPaused &&
        r.note.find("a fight lasted more than 180 s") != std::string::npos);
    for (int i = 0; i < 10; ++i)
        CHECK(!CeilingFightOverWhilePaused(r, true, 1000) && r.state == State::Paused);
    // A quiet stretch broken by one combat tick starts over.
    CHECK(!CeilingFightOverWhilePaused(r, false, 4000));
    CHECK(!CeilingFightOverWhilePaused(r, true, 1000) && r.ceilingQuietMs == 0);
    CHECK(!CeilingFightOverWhilePaused(r, false, 4000) && r.state == State::Paused);
    // kCeilingQuietMs out of combat: it goes on by itself, regrouping.
    CHECK(CeilingFightOverWhilePaused(r, false, 1000));
    CHECK(r.state == State::Running && !r.ceilingPaused && r.step == 0);
    CHECK(r.note.find("going on by itself") != std::string::npos);
    // A second ceiling on the same step stays paused for good.
    ceilingPause(r);
    CHECK(r.state == State::Paused && r.ceilingPaused);
    CHECK(!CeilingFightOverWhilePaused(r, false, kCeilingQuietMs * 2) && r.state == State::Paused);
    // A player's pause never self-resumes, and clears the ceiling mark.
    Run p = Start(&plan);
    ceilingPause(p);
    PausePlayer(p, "player order (stay): .bot run resume to go on");
    CHECK(!p.ceilingPaused);
    CHECK(!CeilingFightOverWhilePaused(p, false, kCeilingQuietMs * 2) && p.state == State::Paused);
    // ... and the guard holds on its own even if the mark were left set.
    p.ceilingPaused = true;
    CHECK(!CeilingFightOverWhilePaused(p, false, kCeilingQuietMs * 2) && p.state == State::Paused);
    // Any other pause after the ceiling's clears it.
    Run o = Start(&plan);
    ceilingPause(o);
    Pause(o, "the lead bot is not on the plan's map (70)");
    CHECK(!o.ceilingPaused);
    CHECK(!CeilingFightOverWhilePaused(o, false, kCeilingQuietMs * 2) && o.state == State::Paused);
    // A manual resume and a skip clear it as well.
    Run m = Start(&plan);
    ceilingPause(m);
    CHECK(Resume(m) && !m.ceilingPaused);
    CHECK(!CeilingFightOverWhilePaused(m, false, kCeilingQuietMs * 2));
    Run k = Start(&plan);
    ceilingPause(k);
    CHECK(Skip(k) && !k.ceilingPaused && k.step == 1);
    // A running run is untouched.
    Run u = Start(&plan);
    CHECK(!CeilingFightOverWhilePaused(u, false, kCeilingQuietMs * 2) && u.state == State::Running);
}

// Zul'Farrak r3-19z20 / r6-19z24 (PLAN-ZF-NOFIGHT): the island Shadowhunter 7246
// (spawn 81610 at 1591.0 1047.7) stood 7.4 yd from step 64's move point and was
// hop-pulled there; it drew no fight. Passed was cleared every step, so r3 pulled
// it 6 times over 3 steps, and the walk back pulled it again. A hop unit that
// drew no fight from one pullback is passed for the run: step 65, the walk back
// over step 64 and a resume do not pull it again.
static void TestHopUnitThatNeverCameIsNotPulledAgain()
{
    std::uint64_t const island = 0xF130001C4E013ECAull;
    std::string const pull = "pullback " + std::to_string(island);
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 1584.0f, 1050.0f, 8.9f), Step(PlanStepKind::Move, 1594.3f, 1048.0f, 9.4f),
        PullStep(1700, {5649})};
    Run r = Start(&plan);
    Facts f = Party(1585.1f); // row 63's pull point
    f.leadY = 1035.5f;
    f.leadZ = 9.0f;
    f.hopUnits = {Unit(island, 7246, 7.4f)};
    CHECK(Tick(r, f) == pull);
    int pulls = 1;
    std::string order;
    for (int i = 0; i < 40 && order.compare(0, 5, "move ") != 0; ++i)
    {
        order = Tick(r, f);
        if (order == pull)
            ++pulls;
    }
    CHECK(pulls == 1);
    CHECK(order == "move 1584.0 1050.0 8.9");
    CHECK(r.neverCame.count(island));
    // Step 65: the same unit within 15 yd of its point (12.9 yd): no pull.
    f.leadX = 1584.0f;
    f.leadY = 1050.0f;
    f.leadZ = 8.9f;
    Tick(r, f);
    CHECK(r.step == 1);
    for (int i = 0; i < 3 && order.compare(0, 5, "move ") != 0; ++i)
        CHECK((order = Tick(r, f)) != pull);
    order = Tick(r, f);
    CHECK(order == "move 1594.3 1048.0 9.4");
    // The walk back over step 64: no pull on the way either.
    r.walkBack = true;
    r.walkBackIdx = 0;
    r.gatherWaived = true;
    r.gatherRefusals = 2;
    f.leadX = 1594.3f;
    f.leadY = 1048.0f;
    f.leadZ = 9.4f;
    order = Tick(r, f);
    CHECK(order == "move 1584.0 1050.0 8.9");
    CHECK(r.walkBackPulls == 0);
    // A resume regroups and judges the hop units afresh (the set is cleared):
    // with the key off the unit gets its one pullback again.
    Pause(r, "test");
    CHECK(Resume(r));
    CHECK(r.neverCame.empty());
    r.walkBack = false;
    pulls = 0;
    for (int i = 0; i < 40 && pulls == 0; ++i)
        if (Tick(r, f) == pull)
            ++pulls;
    CHECK(pulls == 1);
}

// Review of 8dee2ee1: the run-wide set holds hop units only. A pull row's own
// unit is passed for its step only (a later row that lists it pulls it again),
// a boss that never comes is pulled again after a resume (the pause's
// "not found ... skip if already dead" keeps working), and a hold's idle pull
// (no hop) never enters the set.
static void TestStepUnitsNeverEnterTheRunWideSet()
{
    {
        DungeonPlan plan;
        plan.steps = {PullStep(10, {4851}), PullStep(12, {4851})};
        Run r = Start(&plan);
        Facts f = Party(0);
        f.units = {Unit(7, 4851, 10)};
        int first = 0, second = 0;
        for (int i = 0; i < 200 && r.state == State::Running; ++i)
        {
            std::size_t const step = r.step;
            if (Tick(r, f) == "pullback 7")
                ++(step == 0 ? first : second);
        }
        CHECK(first == static_cast<int>(kPullTriesPerUnit));
        CHECK(second == static_cast<int>(kPullTriesPerUnit));
        CHECK(r.neverCame.empty());
    }
    {
        DungeonPlan plan;
        PlanStep boss = Step(PlanStepKind::Boss, 10);
        boss.entry = 4854;
        ai::NormalizePlanStep(boss);
        plan.steps = {boss};
        Run r = Start(&plan);
        Facts f = Party(0);
        f.units = {Unit(77, 4854, 10)};
        int before = 0, after = 0;
        for (int i = 0; i < 400 && r.state == State::Running; ++i)
            if (Tick(r, f) == "pullback 77")
                ++before;
        CHECK(before == static_cast<int>(kPullTriesPerUnit));
        CHECK(r.state == State::Paused && r.neverCame.empty());
        CHECK(Resume(r));
        for (int i = 0; i < 400 && r.state == State::Running; ++i)
            if (Tick(r, f) == "pullback 77")
                ++after;
        CHECK(after == static_cast<int>(kPullTriesPerUnit));
        CHECK(r.neverCame.empty());
    }
    {
        // A step pull's no-fight timeout (the hold's idle pull is one): no run-wide entry.
        DungeonPlan plan;
        plan.steps = {PullStep(10, {4851})};
        Run r = Start(&plan);
        r.pullGuid = 5;
        r.pullTries[5] = 1;
        r.pullHop = false;
        EnterPhase(r, Phase::AwaitPull);
        r.phaseMs = kPullNoFightMs;
        AwaitPull(r);
        CHECK(r.phase == Phase::Begin && r.neverCame.empty());
    }
}

// The 25 s window: a hop unit whose fight starts at 24 s is not marked; one
// with no fight is marked on the tick its pull is 25 s old, not before. A
// marked unit that turns up as an attacker of the party leaves the set at once.
static void TestHopNeverCameWindowAndLateAttacker()
{
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 50, 0, 0)};
    {
        Run r = Start(&plan);
        Facts f = Party(0);
        f.hopUnits = {Unit(9, 4852, 10)};
        CHECK(Tick(r, f) == "pullback 9");
        for (int i = 0; i < 23; ++i)
            CHECK(Tick(r, f).empty());
        CHECK(r.phase == Phase::AwaitPull && r.phaseMs == 23000);
        f.anyInCombat = true; // the fight starts at 24 s
        f.leadInCombat = true;
        Tick(r, f);
        f.hopUnits.clear(); // killed
        for (int i = 0; i < 5; ++i)
            Tick(r, f);
        f.anyInCombat = false;
        f.leadInCombat = false;
        for (int i = 0; i < 30; ++i)
            Tick(r, f);
        CHECK(!r.neverCame.count(9));
    }
    {
        Run r = Start(&plan);
        Facts f = Party(0);
        f.hopUnits = {Unit(9, 4852, 10)};
        CHECK(Tick(r, f) == "pullback 9");
        for (int i = 0; i < 24; ++i)
            Tick(r, f);
        CHECK(r.phase == Phase::AwaitPull && !r.neverCame.count(9)); // 24 s: still waiting
        Tick(r, f);
        CHECK(r.neverCame.count(9)); // 25 s
        // It came after all: on a party bot.
        f.anyInCombat = true;
        f.partyAttackers = {Unit(9, 4852, 6)};
        Tick(r, f);
        CHECK(!r.neverCame.count(9));
        // Or on the lead bot itself.
        r.neverCame.insert(9);
        f.partyAttackers.clear();
        f.leadHasAttacker = true;
        f.leadAttacker = Unit(9, 4852, 2);
        Tick(r, f);
        CHECK(!r.neverCame.count(9));
    }
}

// r6-19z24 step 64 (17:28:12): "pulling 7246 (order 1; no firing stand found)",
// the tank walked at it, engaged it on arrival and followed it onto the island.
// With the pull stand on, a hop unit the tank cannot shoot from where it stands,
// once the stand rule gave up, is not pulled at all: it is passed for the run and
// the move goes on. A shootable hop unit is pulled as before, and so is every hop
// unit with the key off (no sight is read then) and a pull row's own unit.
static void TestHopPullWithoutAShotSendsNoOrder()
{
    std::uint64_t const island = 0xF130001C4E013ECAull;
    std::string const pull = "pullback " + std::to_string(island);
    DungeonPlan plan;
    plan.steps = {Step(PlanStepKind::Move, 50, 0, 0)};
    {
        Run r = Start(&plan);
        Facts f = StandParty();
        f.hopUnits = {Seen(island, 7246, 7.4f, false)};
        CHECK(Tick(r, f).empty());
        CHECK(r.shotStandFor == island);
        f.shotStandGuid = island; // read, nothing found
        f.shotStandKnown = false;
        CHECK(Tick(r, f).empty());
        CHECK(r.neverCame.count(island) && !r.passed.count(island));
        CHECK(Has(r.note, "passed: the tank cannot shoot it from here (no firing stand found)"));
        f.shotStandGuid = 0;
        CHECK(Tick(r, f) == "move 50.0 0.0 0.0");
        // A regroup (a wipe recovery, a resume) clears the set; the same rule
        // leaves the still unshootable unit out again, with no pull.
        BeginRegroup(r);
        CHECK(r.neverCame.empty());
        r.regroup = false;
        EnterPhase(r, Phase::Begin);
        for (int i = 0; i < 6 && !r.neverCame.count(island); ++i)
        {
            if (r.shotStandFor == island)
            {
                f.shotStandGuid = island;
                f.shotStandKnown = false;
            }
            else
                f.shotStandGuid = 0;
            CHECK(Tick(r, f) != pull);
        }
        CHECK(r.neverCame.count(island));
    }
    {
        // The walk back's hop pull: the same.
        DungeonPlan two;
        two.steps = {Step(PlanStepKind::Move, 50, 0, 0), Step(PlanStepKind::Move, 90, 0, 0)};
        Run r = Start(&two, 1);
        r.walkBack = true;
        r.walkBackIdx = 0;
        r.gatherWaived = true;
        r.gatherRefusals = 2;
        Facts f = StandParty();
        f.hopUnits = {Seen(island, 7246, 7.4f, false)};
        CHECK(Tick(r, f).empty());
        f.shotStandGuid = island;
        f.shotStandKnown = false;
        CHECK(Tick(r, f).empty());
        CHECK(r.neverCame.count(island) && r.walkBackPulls == 0);
        f.shotStandGuid = 0;
        CHECK(Tick(r, f) == "move 50.0 0.0 0.0");
    }
    {
        // In sight and in range: pulled.
        Run r = Start(&plan);
        Facts f = StandParty();
        f.hopUnits = {Seen(island, 7246, 20.0f, true)};
        CHECK(Tick(r, f) == pull);
    }
    {
        // The key off: pulled as before.
        Run r = Start(&plan);
        Facts f = Party(0);
        f.hopUnits = {Seen(island, 7246, 7.4f, false)};
        CHECK(Tick(r, f) == pull);
    }
    {
        // A pull row's own unit keeps the old order.
        DungeonPlan pack;
        pack.steps = {PullStep(40, {7246})};
        Run r = Start(&pack);
        Facts f = StandParty();
        f.units = {Seen(island, 7246, 40.0f, false)};
        CHECK(Tick(r, f).empty());
        f.shotStandGuid = island;
        f.shotStandKnown = false;
        CHECK(Tick(r, f) == pull);
        CHECK(!r.neverCame.count(island));
    }
}

// --- Uldaman r10-19z29 step 66: the Stone Steward 4860 (guid 0x6b51) walks its lane north, away from the
// step's 10 yd radius at (-39.6, 272.8), while the party stands at the south end. The step said "pack cleared"
// in 0 s in r8, r9 and r10; the Steward then came into the Keeper's fight at step 68.

static UnitFact Patroller(std::uint64_t guid, std::uint32_t entry, float distLead, float distSpot)
{
    UnitFact u = Unit(guid, entry, distLead);
    u.distSpot = distSpot;
    u.x = -42.0f; // on its lane, south of the step's radius
    u.y = 250.0f;
    u.z = -48.3f;
    return u;
}

static DungeonPlan StewardPlan(std::uint32_t timeoutS = 60)
{
    DungeonPlan plan;
    PlanStep pull = PullStep(-39.6f, {4860});
    pull.y = 272.8f;
    pull.radius = 10.0f;
    pull.timeoutS = timeoutS;
    plan.steps = {pull, Step(PlanStepKind::Move, 100, 0, 0)};
    return plan;
}

static void TestPatrollerOutsideTheRadiusIsWaitedForThenPulled()
{
    DungeonPlan plan = StewardPlan();
    Run r = Start(&plan);
    Facts f = Party(0);
    f.patrollers = {Patroller(0x6b51, 4860, 34.0f, 22.0f)};
    // On the north half of its lane: no "pack cleared", the step waits for it.
    for (int i = 0; i < 10; ++i)
    {
        CHECK(Tick(r, f).empty());
        CHECK(r.step == 0 && r.state == State::Running);
        CHECK(r.note == "waiting for the patroller 4860 0x0000000000006b51 (outside the 10.0 yd radius; up to 60 s)");
    }
    // Close and in sight, still outside the radius: no pull out there, the step waits on.
    f.patrollers[0].distLead = 12.0f;
    f.patrollers[0].losLead = true;
    CHECK(Tick(r, f).empty());
    CHECK(Has(r.note, "waiting for the patroller 4860"));
    // It walks into the radius: one of the step's units now, pulled by the usual pick.
    f.patrollers.clear();
    f.units = {Unit(0x6b51, 4860, 28.0f)};
    CHECK(Tick(r, f) == "pullback 27473");
    CHECK(r.step == 0 && r.phase == Phase::AwaitPull);
    // Killed: nothing of the step is left (the glue lists no corpse), the step is cleared.
    f.units.clear();
    r.phase = Phase::Begin;
    Tick(r, f);
    CHECK(r.step == 1 && r.note == "pack cleared");
}

// The wait cap: kPatrolWaitMs (60 s), or the row's timeout_s when that is shorter.
static void WaitOut(std::uint32_t timeoutS, std::uint32_t expectS)
{
    DungeonPlan plan = StewardPlan(timeoutS);
    Run r = Start(&plan);
    Facts f = Party(0);
    f.patrollers = {Patroller(0x6b51, 4860, 34.0f, 22.0f)};
    for (std::uint32_t i = 0; i < expectS; ++i)
    {
        Tick(r, f);
        CHECK(r.step == 0);
    }
    Tick(r, f);
    CHECK(r.step == 1 && r.state == State::Running);
    CHECK(r.note == "pack cleared; the patroller 4860 0x0000000000006b51 did not come within " +
        std::to_string(expectS) + " s");
}

static void TestPatrollerWaitIsBounded()
{
    WaitOut(60, 60);  // NormalizePlanStep's default
    WaitOut(600, 60); // a 600 s row never waits 600 s
    WaitOut(20, 20);  // a shorter row caps it
    WaitOut(0, 60);   // no timeout at all
    // The loader's path: a row with no timeout_s is normalized to 60 s.
    DungeonPlan plan;
    PlanStep pull = PullStep(-39.6f, {4860});
    CHECK(pull.timeoutS == ai::kPlanDefaultTimeoutS);
    plan.steps = {pull, Step(PlanStepKind::Move, 100, 0, 0)};
    Run r = Start(&plan);
    Facts f = Party(0);
    f.patrollers = {Patroller(0x6b51, 4860, 34.0f, 22.0f)};
    CHECK(Tick(r, f).empty() && Has(r.note, "up to 60 s"));
}

// A patroller given up on (its wait ran out, or its pulls drew no fight) is not waited for by a later step that
// lists the same entry; a regroup (a resume, a wipe recovery) judges it afresh.
static void TestPatrollerGiveUpIsRunWide()
{
    DungeonPlan plan = StewardPlan(5);
    PlanStep again = plan.steps[0];
    again.x = -20.0f;
    plan.steps = {plan.steps[0], again, Step(PlanStepKind::Move, 100, 0, 0)};
    // The wait ran out at step 1.
    {
        Run r = Start(&plan);
        Facts f = Party(0);
        f.patrollers = {Patroller(0x6b51, 4860, 34.0f, 22.0f)};
        for (int i = 0; i < 6; ++i)
            Tick(r, f);
        CHECK(r.step == 1 && Has(r.note, "did not come within 5 s"));
        CHECK(r.patrolGaveUp.count(0x6b51));
        Tick(r, f); // step 2 lists 4860 too: no second wait for the same guid
        CHECK(r.step == 2 && r.note == "pack cleared");
        // A regroup clears it: the patroller is waited for again.
        Run q = Start(&plan, 1);
        q.patrolGaveUp.insert(0x6b51);
        BeginRegroup(q);
        CHECK(q.patrolGaveUp.empty());
    }
    // Pulled when it walked into the radius, but two pulls drew no fight: passed for the step and given up run-wide.
    {
        Run r = Start(&plan);
        Facts f = Party(0);
        f.patrollers = {Patroller(0x6b51, 4860, 34.0f, 22.0f)};
        CHECK(Tick(r, f).empty());
        f.patrollers.clear();
        f.units = {Unit(0x6b51, 4860, 28.0f)};
        std::string order;
        int pulls = 0;
        for (int i = 0; i < 80 && !r.patrolGaveUp.count(0x6b51); ++i)
            if (Tick(r, f) == "pullback 27473")
                ++pulls;
        CHECK(pulls == 2 && r.patrolGaveUp.count(0x6b51));
        // It walked out again: no step waits for it any more (this one, nor the next that lists 4860 too).
        f.units.clear();
        f.patrollers = {Patroller(0x6b51, 4860, 34.0f, 22.0f)};
        for (int i = 0; i < 3 && r.step < 2; ++i)
        {
            Tick(r, f);
            CHECK(!Has(r.note, "waiting for the patroller"));
        }
        CHECK(r.step == 2 && r.note == "pack cleared");
    }
}

static void TestNoPatrollerWaitWithoutALivingHostilePatroller()
{
    DungeonPlan plan = StewardPlan();
    // Not seen at all (dead, out of sight): "pack cleared" at once, as before.
    {
        Run r = Start(&plan);
        Facts f = Party(0);
        Tick(r, f);
        CHECK(r.step == 1 && r.note == "pack cleared");
    }
    // Friendly to the party: no wait.
    {
        Run r = Start(&plan);
        Facts f = Party(0);
        f.patrollers = {Patroller(0x6b51, 4860, 34.0f, 22.0f)};
        f.patrollers[0].hostile = false;
        Tick(r, f);
        CHECK(r.step == 1 && r.note == "pack cleared");
    }
    // Given up on this run (out of reach): no wait.
    {
        Run r = Start(&plan);
        r.unreachable.insert(0x6b51);
        Facts f = Party(0);
        f.patrollers = {Patroller(0x6b51, 4860, 34.0f, 22.0f)};
        Tick(r, f);
        CHECK(r.step == 1 && r.note == "pack cleared");
    }
    // The party in a fight: the step logic does not wait for it (called directly; Tick runs the fight gate).
    {
        Run r = Start(&plan);
        Facts f = Party(0);
        f.anyInCombat = true;
        f.patrollers = {Patroller(0x6b51, 4860, 34.0f, 22.0f)};
        std::string order;
        FightStep(r, f, plan.steps[0], {}, kTickMs, order);
        CHECK(r.step == 1 && r.note == "pack cleared");
    }
    // A boss step is not a pull step: its own search rule stands.
    {
        DungeonPlan boss;
        PlanStep b = Step(PlanStepKind::Boss, 40, 0, 0);
        b.entry = 4860;
        boss.steps = {b, Step(PlanStepKind::Move, 100, 0, 0)};
        Run r = Start(&boss);
        Facts f = Party(0);
        f.patrollers = {Patroller(0x6b51, 4860, 34.0f, 22.0f)};
        Tick(r, f);
        CHECK(Has(r.note, "not at his spot: waiting for him"));
    }
}

// A later pull step lists the same entry, and this patroller stands in that step's radius (within kPatrolSeeYd of
// this step's radius): it is that step's unit; this step neither waits for it nor pulls it.
static void TestPatrollerOwnedByALaterStepIsLeftToIt()
{
    DungeonPlan plan = StewardPlan();
    PlanStep later = plan.steps[0];
    later.x = -42.0f;
    later.y = 250.0f;
    later.z = -48.3f;
    later.radius = 8.0f;
    plan.steps = {plan.steps[0], Step(PlanStepKind::Move, -45, 245, -48), later};
    Run r = Start(&plan);
    Facts f = Party(0);
    UnitFact u = Patroller(0x6b52, 4860, 20.0f, 23.0f); // at (-42, 250): in the later step's radius
    u.losLead = true;
    f.patrollers = {u};
    CHECK(Tick(r, f).empty());
    CHECK(r.step == 1 && r.note == "pack cleared");
    CHECK(!r.patrolGaveUp.count(0x6b52));
}

// --- Uldaman r10-19z29 step 79: pull 7405 x10 (9 joiners expected) from a firing stand 10 yd from the pack,
// up a ramp. The healer (Vist) and the mage (Dizzy) were 27-30 yd behind when the lead reached the stand.

static Facts ScorpidParty(float leadX)
{
    Facts f = StandParty();
    f.joinerCeiling = 6;
    f.leadX = leadX;
    f.members = {Mem("Tanky", 0.0f), Mem("Vist", 4.0f), Mem("Dizzy", 5.0f)};
    for (MemberFact& m : f.members)
        m.x = leadX - m.yd; // behind the lead on the x axis
    f.maxMemberYd = 5.0f;
    return f;
}

static void SetBehind(Facts& f, const std::string& name, float yd)
{
    for (MemberFact& m : f.members)
        if (m.name == name)
        {
            m.yd = yd;
            m.x = f.leadX - yd;
        }
    f.maxMemberYd = 0.0f;
    for (const MemberFact& m : f.members)
        f.maxMemberYd = std::max(f.maxMemberYd, m.yd);
}

// Starts the r10 pull: the joiner rest (rested: no wait), then the stand read and the walk to (24.4, 0, 0).
// Returns with the walk's move sent; the unit is held from here, so the glue sends no count for it any more.
static void StartScorpidWalk(Run& r, Facts& f, std::uint32_t joiners)
{
    UnitFact u = Joiners(0x6b55, 7405, 34.3f, joiners);
    u.joinersRead = true;
    f.units = {u};
    CHECK(Tick(r, f).empty()); // over the ceiling: the joiner rest's pick (the party is rested)
    f.units[0].joinersRead = false; // held: ReadPullJoiners skips it from now on
    f.units[0].joiners = 0;
    std::string order;
    for (int i = 0; i < 4 && r.shotStandFor == 0; ++i)
        order = Tick(r, f);
    CHECK(r.shotStandFor == 0x6b55);
    GiveStand(f, r, 24.4f, 10.2f);
    CHECK(Tick(r, f) == "move 24.4 0.0 0.0");
    ClearStand(f);
    CHECK(r.phase == Phase::Walk && r.shotStandWalk);
}

static void TestBigPackStandWalkWaitsForTheParty()
{
    DungeonPlan plan;
    plan.steps = {PullStep(40, {7405}), Step(PlanStepKind::Move, 100, 0, 0)};
    Run r = Start(&plan);
    Facts f = ScorpidParty(0.0f);
    StartScorpidWalk(r, f, 9);
    CHECK(r.bigWalk && r.bigJoiners == 9);
    // Up the ramp: the lead at 12, the healer 28 yd behind (40 yd from the stand). The lead holds where it
    // stands (a gather on its own spot), with the reason on the note.
    f.leadX = 12.0f;
    f.leadMoving = true;
    f.units[0].distLead = 22.3f;
    SetBehind(f, "Vist", 28.0f);
    CHECK(Tick(r, f) == "move 12.0 0.0 0.0");
    CHECK(IsGatherOrder(r, "move 12.0 0.0 0.0"));
    CHECK(r.note == "big pack (9 joiners expected, 8 or more): the firing stand walk waits for the party (Vist 28.0 yd behind)");
    // Held while the healer comes up the ramp; the stand walk stays the pull stand's.
    f.leadMoving = false;
    SetBehind(f, "Vist", 24.0f);
    for (int i = 0; i < 5; ++i)
    {
        CHECK(Tick(r, f).empty());
        CHECK(Has(r.note, "waits for the party") && r.shotStandWalk && r.phase == Phase::Walk);
    }
    // A second unit of the pack (another 9-joiner read: its joiner rest's pick) on the next tick does not
    // let the walk run on unwatched: the r10 leak.
    UnitFact other = Joiners(0x6b54, 7405, 21.0f, 9);
    other.joinersRead = true;
    f.units.push_back(other);
    CHECK(Tick(r, f).empty());
    CHECK(Has(r.note, "waits for the party") && r.shotStandWalk);
    f.units.pop_back();
    // The healer is within kGatherYd of the lead: the walk goes on to the same stand (no new stand read).
    SetBehind(f, "Vist", 8.0f);
    CHECK(Tick(r, f) == "move 24.4 0.0 0.0");
    CHECK(r.note == "big pack (9 joiners expected, 8 or more): the party gathered, on to the firing stand");
    CHECK(r.shotStandTries[0x6b55] == 1);
    // At the stand with the party together: the pull goes out from there, and the gate is over.
    f.leadX = 24.4f;
    SetBehind(f, "Vist", 6.0f);
    f.units[0].distLead = 10.2f;
    f.units[0].losLead = true;
    std::string order;
    for (int i = 0; i < 5 && order.compare(0, 9, "pullback ") != 0; ++i)
        order = Tick(r, f);
    CHECK(order == "pullback 27477");
    CHECK(Has(r.note, "shot from firing stand 1"));
    CHECK(!r.bigWalk);
}

// The r10 tick itself: the healer falls back on the very tick another unit's joiner rest takes the pick (before
// PullUnit's own gather could see it). Without the gate no order goes out and the lead walks on to the stand.
static void TestBigPackGateHoldsOnAJoinerRestTick()
{
    DungeonPlan plan;
    plan.steps = {PullStep(40, {7405}), Step(PlanStepKind::Move, 100, 0, 0)};
    Run r = Start(&plan);
    Facts f = ScorpidParty(0.0f);
    StartScorpidWalk(r, f, 9);
    f.leadX = 12.0f;
    f.leadMoving = true;
    f.units[0].distLead = 22.3f;
    UnitFact other = Joiners(0x6b54, 7405, 21.0f, 9);
    other.joinersRead = true;
    f.units.push_back(other);
    SetBehind(f, "Vist", 28.0f);
    CHECK(Tick(r, f) == "move 12.0 0.0 0.0");
    CHECK(r.bigHold && r.shotStandWalk);
}

// A bot near the stand already (ahead of the lead) is no reason to hold.
static void TestBigPackGateCountsTheStandPoint()
{
    DungeonPlan plan;
    plan.steps = {PullStep(40, {7405}), Step(PlanStepKind::Move, 100, 0, 0)};
    Run r = Start(&plan);
    Facts f = ScorpidParty(0.0f);
    StartScorpidWalk(r, f, 9);
    f.leadX = 2.0f;
    for (MemberFact& m : f.members)
        if (m.name == "Dizzy")
        {
            m.yd = 21.0f;
            m.x = 23.0f; // 1.4 yd from the stand
        }
    f.maxMemberYd = 21.0f;
    Tick(r, f);
    CHECK(!r.bigHold && !Has(r.note, "big pack"));
}

static void TestBigPackGateGivesUpAtTheGatherCeiling()
{
    DungeonPlan plan;
    plan.steps = {PullStep(40, {7405}), Step(PlanStepKind::Move, 100, 0, 0)};
    Run r = Start(&plan);
    Facts f = ScorpidParty(0.0f);
    StartScorpidWalk(r, f, 9);
    f.leadX = 12.0f;
    SetBehind(f, "Vist", 28.0f);
    CHECK(Tick(r, f) == "move 12.0 0.0 0.0");
    std::string order;
    std::uint32_t held = 0;
    for (; held < kGatherCapMs + 5 * kTickMs; held += kTickMs)
    {
        order = Tick(r, f);
        if (!order.empty())
            break;
    }
    CHECK(held + kTickMs == kGatherCapMs); // the ceiling, not forever
    CHECK(order == "move 24.4 0.0 0.0");
    CHECK(r.note == "big pack (9 joiners expected, 8 or more): the party did not gather within 30 s (Vist 28.0 yd "
                    "behind): proceeding anyway");
    // Spent for this walk: the gate does not hold again on the way.
    Tick(r, f);
    CHECK(!r.bigHold && !Has(r.note, "waits for the party"));
}

// kBigPackJoiners is a boundary: exactly 8 joiners expected gates the walk, 7 does not.
static void TestBigPackBoundaryIsEight()
{
    DungeonPlan plan;
    plan.steps = {PullStep(40, {7405}), Step(PlanStepKind::Move, 100, 0, 0)};
    for (std::uint32_t joiners : {8u, 7u})
    {
        Run r = Start(&plan);
        Facts f = ScorpidParty(0.0f);
        StartScorpidWalk(r, f, joiners);
        CHECK(r.bigWalk == (joiners == 8));
        f.leadX = 12.0f;
        f.units[0].distLead = 22.3f;
        SetBehind(f, "Vist", 28.0f);
        Tick(r, f);
        CHECK(r.bigHold == (joiners == 8));
        CHECK(Has(r.note, "big pack (8 joiners expected, 8 or more)") == (joiners == 8));
    }
}

static void TestSmallPackStandWalkIsNotGated()
{
    DungeonPlan plan;
    plan.steps = {PullStep(40, {7405}), Step(PlanStepKind::Move, 100, 0, 0)};
    Run r = Start(&plan);
    Facts f = ScorpidParty(0.0f);
    StartScorpidWalk(r, f, 7); // one under kBigPackJoiners
    CHECK(!r.bigWalk);
    f.leadX = 12.0f;
    SetBehind(f, "Vist", 28.0f);
    Tick(r, f);
    CHECK(!r.bigHold && !Has(r.note, "big pack"));
    CHECK(r.note == "gathering before a pull"); // the old gate, as before
}

int main()
{
    TestPatrollerOutsideTheRadiusIsWaitedForThenPulled();
    TestPatrollerWaitIsBounded();
    TestPatrollerGiveUpIsRunWide();
    TestPatrollerOwnedByALaterStepIsLeftToIt();
    TestNoPatrollerWaitWithoutALivingHostilePatroller();
    TestBigPackStandWalkWaitsForTheParty();
    TestBigPackGateHoldsOnAJoinerRestTick();
    TestBigPackGateCountsTheStandPoint();
    TestBigPackGateGivesUpAtTheGatherCeiling();
    TestSmallPackStandWalkIsNotGated();
    TestBigPackBoundaryIsEight();
    TestHopUnitThatNeverCameIsNotPulledAgain();
    TestStepUnitsNeverEnterTheRunWideSet();
    TestHopNeverCameWindowAndLateAttacker();
    TestHopPullWithoutAShotSendsNoOrder();
    TestGoneChestWithFlagIsDone();
    TestGoneChestWithoutFlagPauses();
    TestGoneIsDoneOnlyForAGoneObject();
    TestHopRechecksTheLeadOnTheFoot();
    TestDriftWithinTheRadiusIsNoProof();
    TestProofIsRetakenAfterAFightAndAResume();
    TestBackPointStopsAtADirectStep();
    TestBrokenHopPausesAndSkippedHopRowHasNoDirectOrder();
    TestWalkBackReplaysTheExactPoint();
    TestExactSerpentisFootIsNotSkipped();
    TestExactNeverProvedPauses();
    TestExactReissuesTheMoveAndIgnoresTheStallRule();
    TestDirectFromExactPointWaitsForTheProof();
    TestRunTimeLimitAndStopNote();
    TestStartFromParsing();
    TestStartFromResolve();
    TestStartInsideEventGoesBack();
    TestPartialStartGuard();
    TestMove();
    TestWalkStuckIsBounded();
    TestShortHopArrivesWithoutTheRetryWait();
    TestShortDirectHopKeepsTheLongWait();
    TestShortHopThatMovesIsNotCut();
    TestStalledNearPointCountsAsArrived();
    TestStandOnALedgeCountsByGroundDistance();
    TestStalledLeadInFollowRangeCountsAsArrived();
    TestPullLoopOnOneTargetPauses();
    TestOutOfReachPackIsSkippedNotCleared();
    TestMissingItemPausesOnce();
    TestResumeRegroups();
    TestResumeWithLeadDead();
    TestFarMoveWalksBackInHops();
    TestDirectStepGathersFirst();
    TestDirectGateAsksTheGluesWithLeadTest();
    TestDirectGatherCeilingSummonsAgain();
    TestRefusedDirectStepSummonsAgainAndPausesLate();
    TestRefusedDirectStepNeedsTwoSummonsBeforeThePause();
    TestDirectSummonAfterADeathIsANewCase();
    TestCombatLockedDirectStragglerPausesOnTime();
    TestWalkBackReadsTheDirectStepPastAPull();
    TestDirectOrderWaitsForTheRead();
    TestDirectRefusalClockStartsOverAtTheNextPoint();
    TestSkipWhileRunning();
    TestUnreachableMoveDetours();
    TestPullGatherAndFight();
    TestLeadHoldsStayWhileResting();
    TestUnseenPackIsApproachedNotCleared();
    TestFarPackHopIsNotUndoneByTheGatherBack();
    TestApproachHopCapCountsHopsWithoutProgress();
    TestItemCarrierIsPickedFromTheStepsOwnInputs();
    TestUseItemPausesOnSplitInputs();
    TestLootStepIsDoneWhenAnyPartyBotHasTheItem();
    TestLootRefusalTriesTheTurnBotThenEachBot();
    TestLootFfaOrderSequence();
    TestLootFfaRestoresOnAnErrAnswerAndNeverTwice();
    TestLootFfaStaysLiveThroughAPause();
    TestLootFfaRestoresWhenTheTaskEndsUnanswered();
    TestLootFfaRefusedFallsBackToTheOldPath();
    TestLootFfaConditions();
    TestLootFfaRegroupBetweenAckAndRestoreRestores();
    TestLootFfaRestoreRefusedRetriesThenPauses();
    TestLootFfaFlagEndsOnlyWhenTheMethodIsNotFfa();
    TestLootFfaOrderedWaitIsBounded();
    TestLootFfaOrderedThenFallenRegroupRestores();
    TestLootFfaKeyOffIsTheOldPath();
    TestOutstandingCarrierKeepsTheOrdersBot();
    TestRetiredUseItemOrderGoesOutAgain();
    TestWaitObjectDoneWinsOverAStaleFightTimer();
    TestPullThatStartsNoFightIsPassed();
    TestAmbushWalksThrough();
    TestBossKillOrderAndTeamGate();
    TestHopPullsNearUnitsFirst();
    TestWipeAndLeadDeathPause();
    TestFewDeadReviveBeforePull();
    TestFewDeadRuleSkipsEvents();
    TestRegroupFightsFirst();
    TestRegroupWaitsForPartyRez();
    TestRegroupRevivePauses();
    TestMealWaitBeforePull();
    TestFightCeiling();
    TestTaskSteps();
    TestOpenWithoutKeyAndTaskError();
    TestTaskResendAfterInterruptedCast();
    TestTaskResendInCombatAndBusy();
    TestTaskResendCounterResetsPerStep();
    TestTaskResendKeepsTheLootBot();
    TestTaskResendNotForOtherFailures();
    TestWaitObjectTimeoutIsBounded();
    TestUnsupportedPausesAndSkip();
    TestPlayerOrderAndRefusalPause();
    TestStepIdleCeiling();
    TestTeamFlags();
    TestOtherTeamBossIsLookedAtAndFoughtUnlessFriendly();
    TestRefusedGatherIsRetriedOnceThenLeftOut();
    TestWipeRegroupsAndWalksBack();
    TestThreeDeadRegroupAfterTheFight();
    TestGatherSummonsAStalledStraggler();
    TestStillBotInsideSummonRangeIsSummoned();
    TestStuckBotInCombatIsNotSummoned();
    TestStuckBotWithAttackersIsNotSummoned();
    TestMovingBotThatDoesNotGainIsSummoned();
    TestComingStragglerIsWaitedFor();
    TestComingStragglerIsWaitedForAtDirectGathers();
    TestComingStragglerIsSummonedAfterTheComingLimit();
    TestStillBotInAFightIsNotWaitedFor();
    TestFarBusyBotIsNeverSummonedAtTheCeiling();
    TestRegroupSummonsAStuckFarBotEarly();
    TestStuckSummonCountsFromTheGather();
    TestWalkBackRefusalKinds();
    TestBossDownNeedsASeenDeath();
    TestSkipDropsTheRefusedPullTarget();
    TestBossKilledAwayFromHisSpot();
    TestWalkBackFarPointIsApproached();
    TestRefusedPullOnlyForInvalidTarget();
    TestWipeWithTheLeadOnTheGraveyardMap();
    TestNearWipeLatchHoldsThroughTheWipe();
    TestLeadDiesFirstThenTheWipeRegroups();
    TestFightCeilingWithFourDeadRegroups();
    TestStragglerOnAnotherFloorIsSummoned();
    TestResumeFromTheEntranceWalksBack();
    TestGatherGoesBackToTheAnchorAfterARidgeChase();
    TestDirectWalkBackAvoidsUnclearedPacks();
    TestStalledAnchorAndSkippedPacks();
    TestHopWaiverEndsWhenCombatStartsDuringCrowdControl();
    TestPullCrowdControlsTheHealer();
    TestBossStepNeverCrowdControlsItsAdds();
    TestNoCrowdControlWithoutAMageOrPlanList();
    TestCrowdControlMustLand();
    TestKillFirstIsThisFightsOnly();
    TestKillFirstFarIsNotFocusedWipe1Replay();
    TestKillFirstBoundDoesNotFlipFlop();
    TestHeldFocusEndsOnlyWhenItsUnitIsGone();
    TestKillFirstOnABossStepIsNotBounded();
    TestGuardStopsWatchingWhenTheFocusEnds();
    TestGuardWatchesAfterAFocusWipe2Replay();
    TestGuardAfterAFocusIsRateBounded();
    TestRefusedFocusIsPickedAgainDeadminesS118();
    TestCcWindowCountsTheMagesFightTime();
    TestTankGuardsThePartyFromItsStand();
    TestTankGuardReturnsToItsOwnSpot();
    TestTankGuardPull100Replay();
    TestTankGuardOneMaulerAndTheCapAfterARefill();
    TestTankGuardLimits();
    TestTankGuardPicksUpMobsOnPets();
    TestEngageT1();
    TestEngageT2();
    TestEngageT3();
    TestEngageT3b();
    TestEngageT4();
    TestEngageT5();
    TestEngageT6();
    TestEngageT7();
    TestEngageT8();
    TestEngageT9();
    TestEngageT10();
    TestEngageT11();
    TestGuardRefillDeadminesS118();
    TestGuardRefillDeadminesS90();
    TestGuardRefillDeadminesS125();
    TestGuardRefillIsBounded();
    TestGuardRefillKeepsSpacingAndWatch();
    TestEngageReplayRfcOggleflint();
    TestEngageReplayUldamanStep85();
    TestEmptyPlan();
    TestStuckStragglerThatMovedIsSummoned();
    TestApproachHopSummonsBeforePause();
    TestLeadOnlyDeathRecovers();
    TestDeathWhileWalkingBlocksTheStep();
    TestLeadDeathBlocks();
    TestWaitRuleEventException();
    TestNoPullWithADeadMember();
    TestRegroupWaitsForTheFight();
    TestDeathDuringRegroup();
    TestLeadDeadAfterTheFightHolds();
    TestRevivedBotSummonedBeforeWalking();
    TestNoReviveSummonInCombat();
    TestOneReviveSummonPerRevive();
    TestFailedReviveSummonWalks();
    TestRegroupGatherCapScales();
    TestAggroEstimateOffChangesNothing();
    TestAggroEstimateHoldsOnceForARest();
    TestJoinerRestsAreBudgetedAndNotIdleTime();
    TestJoinerRestSlotsAreSpentByRealRestsOnly();
    TestRezHoldOffIsUnchanged();
    TestRezHoldOn();
    TestRezHoldClocks();
    TestRezHoldNudge();
    TestRezNudgeUnreachable();
    TestAggroEstimateAndRezHoldDoNotCrossTheirResets();
    TestBothKeysOffChangeNothing();
    TestPullNoteCarriesTheJoinerCount();
    TestPullShootsFromStand();
    TestPullWaitsForStillTank();
    TestPullWalksToStandOutOfRange();
    TestPullWalksToStandNoLos();
    TestFiringStandOnALedgeCountsByGroundDistance();
    TestPullStandTriesBounded();
    TestPullNoShotDataKeepsOldOrder();
    TestPickPackPullPrefersShootable();
    TestStandTicksAreNotFruitlessPulls();
    TestShotStandOnPath();
    TestPullStandNeedsMoreRangeThanTheMargin();
    TestStandRequestIsReadOnce();
    TestStandWalkStuckOrdersFromHere();
    TestStandWalkIsDrivenEveryTick();
    TestStuckStandChecksTheWayBackFirst();
    TestPickSwitchChecksTheWayBack();
    TestGossipOrderOnceThenAck();
    TestGossipErrPauses();
    TestGossipNotAllowedNamesTheConfigKey();
    TestGossipDeadNpcPausesPlainly();
    TestGossipNoNpcKeepsItsText();
    TestGossipDeadNpcTaskReply();
    TestGossipNoAnswerPauses();
    TestGossipWaitsForABusyLeadBot();
    TestGossipWalksToFarPointFirst();
    TestGossipStartsAnEvent();
    TestGossipAfterAHoldThenTheDoor();
    TestUnitsHoldWaitsForAFriendlyBossToTurn();
    TestGossipDeadIsDoneWithFlag();
    TestGossipDeadIsDoneOnlyForADeadNpc();
    TestUnitsHoldAllDeadOnEntryEndsAfterTenSeconds();
    TestBlyCrewDeadBeforeTheLegRunsUnattended();
    TestUnitsHoldAllDeadDoesNotEndEarly();
    TestAllDeadEndIsForAUnitsHoldOnly();
    TestHoldUnitSeenDoesNotLeakAcrossSteps();
    TestUseOnOneTargetedOrderThenAck();
    TestUseOnWithoutTheItemPauses();
    TestUseOnResendsOnceThenPauses();
    TestUseOnNoAnswerPauses();
    TestUseOnStartsAnEvent();
    TestUseOnAckNeedsProof();
    TestUseOnNoResendOnceTheActStarted();
    TestUseOnResetAfterAckDoesNotUseAgain();
    TestUseOnNamesADeadHolder();
    TestLeadOnIslandReading();
    TestWalkBackEscapesNavmeshIsland();
    TestForwardMoveEscapesNavmeshIsland();
    TestR16StandRefusalHopsOffIsland();
    TestR16StepMoveRefusalHopsOffIsland();
    TestR16RefusalAfterTheCapsPauses();
    TestNonPathRefusalUnchanged();
    TestBareUnreachableIsNoIslandSign();
    TestGuardReturnThroughTheGlue();
    TestGoodPathClearsTheRefusalSign();
    TestForwardMoveIslandNeedsSecondSign();
    TestForwardMoveNoPathLedgeEscapes();
    TestDetourPointReachable();
    TestDetourCandidateLeadsOn();
    TestForwardEscapeNoSpotFallsBack();
    TestForwardEscapeAfterAFallFallsBack();
    TestForwardEscapeNeedsAFreshGoodSpot();
    TestForwardMoveProbeGateAndCache();
    TestIslandPausesAfterTwoEscapes();
    TestResumeEscapesFirst();
    TestWalkBackPointInSightOnIsland();
    TestApproachHopIslandAndZeroHop();
    TestIslandGapIsNotAGoodPath();
    TestEscapeWaitIsNotATry();
    TestEscapeSegmentCheck();
    TestIslandLeavesDirectStepsAlone();
    TestSingleIslandReadingChangesNothing();
    TestSecondReadingConfirmsTheIsland();
    TestWalkBackNoSpotPassesThePoint();
    TestStepEscapeCap();
    TestNoSpotWalkBackNeverSpins();
    TestSignBFromTheMainMeshIsBounded();
    TestWalkBackResendBound();
    TestCeilingPauseResumesByItself();
    std::cout << "test_dungeon_run_policy: ok\n";
    return 0;
}
