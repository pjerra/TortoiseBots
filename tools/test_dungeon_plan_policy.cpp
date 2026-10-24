// Standalone check for ai/playerbot/DungeonPlanPolicy.h:
//   g++ -std=c++17 -Wall -Wextra tools/test_dungeon_plan_policy.cpp -o /tmp/test_dungeon_plan_policy && /tmp/test_dungeon_plan_policy
#include "../ai/playerbot/DungeonPlanPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using namespace ai;

static PlanStep Step(std::uint8_t leg, std::uint16_t seq, PlanStepKind kind, float x = 0, float y = 0, float z = 0)
{
    PlanStep step;
    step.leg = leg;
    step.seq = seq;
    step.kind = kind;
    step.x = x;
    step.y = y;
    step.z = z;
    return step;
}

static void TestKinds()
{
    const char* names[] = {"move", "move_direct", "pull", "boss", "loot", "open", "use_item", "wait_object",
                           "wait_clear", "rest", "ritual", "hold", "gossip"};
    for (const char* name : names)
    {
        PlanStepKind kind = ParsePlanStepKind(name);
        CHECK(kind != PlanStepKind::None);
        CHECK(std::string(PlanStepKindName(kind)) == name);
    }
    CHECK(ParsePlanStepKind("Move") == PlanStepKind::None);
    CHECK(ParsePlanStepKind("") == PlanStepKind::None);
}

static void TestEntries()
{
    std::vector<std::uint32_t> out;
    CHECK(ParsePlanEntries("", out) && out.empty());
    CHECK(ParsePlanEntries("  ", out) && out.empty());
    CHECK(ParsePlanEntries("4851", out) && out.size() == 1 && out[0] == 4851);
    CHECK(ParsePlanEntries("4851, 4852 ,7175", out) && out.size() == 3 && out[2] == 7175);
    CHECK(!ParsePlanEntries("4851,", out));
    CHECK(!ParsePlanEntries("4851,,4852", out));
    CHECK(!ParsePlanEntries("abc", out));
    CHECK(!ParsePlanEntries("0", out));
    CHECK(!ParsePlanEntries("-5", out));
    CHECK(!ParsePlanEntries("1234567890", out));
    std::string many;
    for (int i = 1; i <= 17; ++i)
        many += (i > 1 ? "," : "") + std::to_string(i);
    CHECK(!ParsePlanEntries(many, out));
}

static void TestNormalize()
{
    PlanStep pull = Step(0, 1, PlanStepKind::Pull);
    CHECK(NormalizePlanStep(pull) == PlanStepRefusal::MissingEntry);
    pull.entries = {4851};
    CHECK(NormalizePlanStep(pull) == PlanStepRefusal::None);
    CHECK(pull.radius == kPlanDefaultPackRadius);
    CHECK(pull.timeoutS == kPlanDefaultTimeoutS);

    PlanStep boss = Step(0, 2, PlanStepKind::Boss);
    CHECK(NormalizePlanStep(boss) == PlanStepRefusal::MissingEntry);
    boss.entry = 7228;
    CHECK(NormalizePlanStep(boss) == PlanStepRefusal::None && boss.radius == kPlanDefaultBossRadius);

    PlanStep wait = Step(0, 3, PlanStepKind::WaitObject);
    wait.entry = 124372;
    wait.param = 3;
    CHECK(NormalizePlanStep(wait) == PlanStepRefusal::BadParam);
    wait.param = 1;
    wait.timeoutS = 601;
    CHECK(NormalizePlanStep(wait) == PlanStepRefusal::BadTimeout);
    wait.timeoutS = 600;
    CHECK(NormalizePlanStep(wait) == PlanStepRefusal::None && wait.timeoutS == 600);

    PlanStep clear = Step(0, 4, PlanStepKind::WaitClear);
    CHECK(NormalizePlanStep(clear) == PlanStepRefusal::BadRadius);
    clear.radius = 101.0f;
    CHECK(NormalizePlanStep(clear) == PlanStepRefusal::BadRadius);
    clear.radius = 20.0f;
    CHECK(NormalizePlanStep(clear) == PlanStepRefusal::None);

    PlanStep move = Step(0, 5, PlanStepKind::Move, std::nanf(""), 0, 0);
    CHECK(NormalizePlanStep(move) == PlanStepRefusal::BadPoint);
    PlanStep none = Step(0, 6, PlanStepKind::None);
    CHECK(NormalizePlanStep(none) == PlanStepRefusal::UnknownKind);
    PlanStep item = Step(0, 7, PlanStepKind::UseItem);
    CHECK(NormalizePlanStep(item) == PlanStepRefusal::MissingEntry);

    // The exact flag (an exact waypoint) belongs to a plain move only: on a
    // move_direct, a pull or anything else it is a row the exporter got wrong.
    PlanStep foot = Step(0, 8, PlanStepKind::Move, -293.3f, 0.5f, -58.0f);
    foot.flags = kPlanFlagExact;
    CHECK(NormalizePlanStep(foot) == PlanStepRefusal::None && (foot.flags & kPlanFlagExact));
    PlanStep hop = Step(0, 9, PlanStepKind::MoveDirect, -287.5f, 5.0f, -63.6f);
    hop.flags = kPlanFlagExact;
    CHECK(NormalizePlanStep(hop) == PlanStepRefusal::BadParam);
    PlanStep exactPull = Step(0, 10, PlanStepKind::Pull);
    exactPull.entries = {4851};
    exactPull.flags = kPlanFlagExact;
    CHECK(NormalizePlanStep(exactPull) == PlanStepRefusal::BadParam);

    // The gone-is-done flag (256) belongs to a loot or an open row only: on any other kind it is a row the exporter got wrong.
    PlanStep graveLoot = Step(0, 11, PlanStepKind::Loot, 1858.7f, 1053.8f, 8.9f);
    graveLoot.entry = 128403;
    graveLoot.flags = kPlanFlagGoneIsDone;
    CHECK(NormalizePlanStep(graveLoot) == PlanStepRefusal::None && (graveLoot.flags & kPlanFlagGoneIsDone));
    PlanStep graveOpen = Step(0, 12, PlanStepKind::Open, 1858.7f, 1053.8f, 8.9f);
    graveOpen.entry = 128403;
    graveOpen.flags = kPlanFlagGoneIsDone;
    CHECK(NormalizePlanStep(graveOpen) == PlanStepRefusal::None);
    PlanStep goneMove = Step(0, 14, PlanStepKind::Move, 1.0f, 2.0f, 3.0f);
    goneMove.flags = kPlanFlagGoneIsDone;
    CHECK(NormalizePlanStep(goneMove) == PlanStepRefusal::BadParam);
    PlanStep gonePull = Step(0, 15, PlanStepKind::Pull);
    gonePull.entries = {7286};
    gonePull.flags = kPlanFlagGoneIsDone;
    CHECK(NormalizePlanStep(gonePull) == PlanStepRefusal::BadParam);
    PlanStep goneBoss = Step(0, 16, PlanStepKind::Boss);
    goneBoss.entry = 7271;
    goneBoss.flags = kPlanFlagGoneIsDone;
    CHECK(NormalizePlanStep(goneBoss) == PlanStepRefusal::BadParam);

    // gossip: the NPC's entry and the option the way `.bot cmd gossip` counts it (1 at the top, at most 15). A set flag
    // is refused (team flags come later), not ignored; the exact flag is refused as on any step that is not a move.
    PlanStep talk = Step(0, 13, PlanStepKind::Gossip, 1877.5f, 1199.6f, 8.9f);
    CHECK(NormalizePlanStep(talk) == PlanStepRefusal::MissingEntry);
    talk.entry = 7607;
    CHECK(NormalizePlanStep(talk) == PlanStepRefusal::BadParam); // param 0: no option
    talk.param = 16;
    CHECK(NormalizePlanStep(talk) == PlanStepRefusal::BadParam);
    talk.param = 1;
    talk.flags = kPlanFlagAlliance;
    CHECK(NormalizePlanStep(talk) == PlanStepRefusal::BadParam);
    // The one flag a gossip reads is kPlanFlagDeadNpcIsDone (512): alone it loads, with another flag it is refused.
    talk.flags = kPlanFlagDeadNpcIsDone;
    CHECK(NormalizePlanStep(talk) == PlanStepRefusal::None && (talk.flags & kPlanFlagDeadNpcIsDone));
    talk.flags = kPlanFlagDeadNpcIsDone | kPlanFlagAlliance;
    CHECK(NormalizePlanStep(talk) == PlanStepRefusal::BadParam);
    talk.flags = kPlanFlagDeadNpcIsDone | kPlanFlagGoneIsDone;
    CHECK(NormalizePlanStep(talk) == PlanStepRefusal::BadParam);
    talk.flags = 128; // reserved
    CHECK(NormalizePlanStep(talk) == PlanStepRefusal::BadParam);
    talk.flags = 0;
    CHECK(NormalizePlanStep(talk) == PlanStepRefusal::None && talk.timeoutS == kPlanDefaultTimeoutS);
    // The flag belongs to a gossip row only: on any other kind it is a row the exporter got wrong, refused, not ignored.
    CHECK(kPlanFlagDeadNpcIsDone == 512);
    PlanStep deadMove = Step(0, 20, PlanStepKind::Move, 1.0f, 2.0f, 3.0f);
    deadMove.flags = kPlanFlagDeadNpcIsDone;
    CHECK(NormalizePlanStep(deadMove) == PlanStepRefusal::BadParam);
    PlanStep deadPull = Step(0, 21, PlanStepKind::Pull);
    deadPull.entries = {7286};
    deadPull.flags = kPlanFlagDeadNpcIsDone;
    CHECK(NormalizePlanStep(deadPull) == PlanStepRefusal::BadParam);
    PlanStep deadLoot = Step(0, 22, PlanStepKind::Loot);
    deadLoot.entry = 128403;
    deadLoot.flags = kPlanFlagDeadNpcIsDone;
    CHECK(NormalizePlanStep(deadLoot) == PlanStepRefusal::BadParam);
    PlanStep deadHold = Step(0, 23, PlanStepKind::Hold);
    deadHold.entries = {7604};
    deadHold.flags = kPlanFlagDeadNpcIsDone;
    CHECK(NormalizePlanStep(deadHold) == PlanStepRefusal::BadParam);
    PlanStep deadUse = Step(0, 24, PlanStepKind::UseOn, 1650.9f, 1171.9f, 10.9f);
    deadUse.entry = 141832;
    deadUse.item = 9240;
    deadUse.flags = kPlanFlagDeadNpcIsDone;
    CHECK(NormalizePlanStep(deadUse) == PlanStepRefusal::BadParam);
    talk.param = 15;
    CHECK(NormalizePlanStep(talk) == PlanStepRefusal::None);
    talk.timeoutS = 601;
    CHECK(NormalizePlanStep(talk) == PlanStepRefusal::BadTimeout);
    DungeonPlanTable talks;
    CHECK(talks.AddPlan("zulfarrak", 209));
    PlanStep row = Step(15, 1, ParsePlanStepKind("gossip"), 1877.5f, 1199.6f, 8.9f);
    CHECK(talks.AddStep("zulfarrak", row) == PlanStepRefusal::MissingEntry);
    row.entry = 7607;
    row.param = 1;
    CHECK(talks.AddStep("zulfarrak", row) == PlanStepRefusal::None);
    CHECK(std::string(PlanStepRowKindName(row)) == "gossip");

    // use_on: item `item` used ON game object `entry` at x,y,z (Zul'Farrak's gong 141832 with the Mallet 9240). The
    // object, the item and the point are required; no flag or param is read, so a set one is refused, not ignored.
    // Appended at the end of the enum: the ids of the older kinds do not move.
    CHECK(static_cast<int>(PlanStepKind::UseOn) == static_cast<int>(PlanStepKind::Gossip) + 1);
    CHECK(ParsePlanStepKind("use_on") == PlanStepKind::UseOn && std::string(PlanStepKindName(PlanStepKind::UseOn)) == "use_on");
    PlanStep gong = Step(9, 10, PlanStepKind::UseOn, 1650.9f, 1171.9f, 10.9f);
    gong.item = 9240;
    CHECK(NormalizePlanStep(gong) == PlanStepRefusal::MissingEntry); // no object
    gong.entry = 141832;
    gong.item = 0;
    CHECK(NormalizePlanStep(gong) == PlanStepRefusal::MissingEntry); // no item
    gong.item = 9240;
    PlanStep nowhere = gong;
    nowhere.x = nowhere.y = nowhere.z = 0.0f;
    CHECK(NormalizePlanStep(nowhere) == PlanStepRefusal::BadPoint);
    PlanStep flagged = gong;
    flagged.flags = kPlanFlagGoneIsDone;
    CHECK(NormalizePlanStep(flagged) == PlanStepRefusal::BadParam);
    flagged.flags = kPlanFlagAlliance;
    CHECK(NormalizePlanStep(flagged) == PlanStepRefusal::BadParam);
    PlanStep withParam = gong;
    withParam.param = 1;
    CHECK(NormalizePlanStep(withParam) == PlanStepRefusal::BadParam);
    CHECK(NormalizePlanStep(gong) == PlanStepRefusal::None && gong.timeoutS == kPlanDefaultTimeoutS);
    CHECK(talks.AddPlan("zf2", 209));
    PlanStep gongRow = Step(9, 10, ParsePlanStepKind("use_on"), 1650.9f, 1171.9f, 10.9f);
    gongRow.entry = 141832;
    gongRow.item = 9240;
    CHECK(talks.AddStep("zf2", gongRow) == PlanStepRefusal::None);
    CHECK(std::string(PlanStepRowKindName(gongRow)) == "use_on");

    // move_hop_exact: a MoveDirect with exactLaunch, and back to its text.
    PlanStepKind kind = PlanStepKind::None;
    bool exactLaunch = false;
    bool openProof = false;
    ParsePlanStepRowKind("move_hop_exact", kind, exactLaunch, openProof);
    CHECK(kind == PlanStepKind::MoveDirect && exactLaunch);
    PlanStep landing = Step(0, 11, kind, -287.5f, 5.0f, -63.6f);
    landing.exactLaunch = exactLaunch;
    CHECK(NormalizePlanStep(landing) == PlanStepRefusal::None);
    CHECK(std::string(PlanStepRowKindName(landing)) == "move_hop_exact");
    CHECK(std::string(PlanStepKindName(landing.kind)) == "move_direct");
    ParsePlanStepRowKind("move_direct", kind, exactLaunch, openProof);
    CHECK(kind == PlanStepKind::MoveDirect && !exactLaunch);
    ParsePlanStepRowKind("move", kind, exactLaunch, openProof);
    CHECK(kind == PlanStepKind::Move && !exactLaunch);
    // exactLaunch on anything but a MoveDirect is refused.
    PlanStep wrong = Step(0, 12, PlanStepKind::Move, 1, 1, 1);
    wrong.exactLaunch = true;
    CHECK(NormalizePlanStep(wrong) == PlanStepRefusal::BadParam);
    // Fail closed: the kind-only parser (all a module before exact waypoints has)
    // does not know the text, so that loader skips the row as UnknownKind.
    CHECK(ParsePlanStepKind("move_hop_exact") == PlanStepKind::None);
    DungeonPlanTable old;
    CHECK(old.AddPlan("wailing_caverns", 43));
    PlanStep oldRow = Step(10, 4, ParsePlanStepKind("move_hop_exact"), -287.5f, 5.0f, -63.6f);
    CHECK(old.AddStep("wailing_caverns", oldRow) == PlanStepRefusal::UnknownKind);
}

// open_proof (a use of object `entry` done by a spawned proof object, or with
// kPlanFlagProofCreature a living creature, of the one entry in `entries`
// within `radius` of x,y,z): the kind text parses to Open with openProof set,
// and NormalizePlanStep takes the row only under its own rules.
static PlanStep StatueProofStep()
{
    PlanStep s = Step(0, 100, PlanStepKind::Open, -515.553f, 95.2582f, -148.74f);
    s.entry = 148830;
    s.entries = {148883};
    s.radius = 10.0f;
    s.openProof = true;
    return s;
}

static void TestOpenProofRowParses()
{
    PlanStepKind kind = PlanStepKind::None;
    bool exact = true, proof = false;
    ParsePlanStepRowKind("open_proof", kind, exact, proof);
    CHECK(kind == PlanStepKind::Open && !exact && proof);
    ParsePlanStepRowKind("open", kind, exact, proof);
    CHECK(kind == PlanStepKind::Open && proof == false);
    PlanStep statue = StatueProofStep();
    CHECK(std::string(PlanStepRowKindName(statue)) == "open_proof");
    PlanStep plain = Step(0, 101, PlanStepKind::Open, 1.0f, 2.0f, 3.0f);
    plain.entry = 124371;
    CHECK(std::string(PlanStepRowKindName(plain)) == "open");
}

static void TestOpenProofIsNoPlainKind()
{
    // The kind-only parser (all an old module has) does not know the text, so
    // that loader refuses the row as UnknownKind instead of loading it as a
    // plain open.
    CHECK(ParsePlanStepKind("open_proof") == PlanStepKind::None);
    DungeonPlanTable old;
    CHECK(old.AddPlan("sunken_temple", 109));
    PlanStep oldRow = Step(0, 200, ParsePlanStepKind("open_proof"), -515.553f, 95.2582f, -148.74f);
    CHECK(old.AddStep("sunken_temple", oldRow) == PlanStepRefusal::UnknownKind);
}

static void TestOpenProofNormalize()
{
    PlanStep good = StatueProofStep();
    CHECK(NormalizePlanStep(good) == PlanStepRefusal::None);
    CHECK(good.timeoutS == kPlanDefaultTimeoutS);
    PlanStep noEntry = StatueProofStep();
    noEntry.entry = 0;
    CHECK(NormalizePlanStep(noEntry) == PlanStepRefusal::MissingEntry);
    PlanStep noProof = StatueProofStep();
    noProof.entries.clear();
    CHECK(NormalizePlanStep(noProof) == PlanStepRefusal::MissingEntry);
    PlanStep twoProof = StatueProofStep();
    twoProof.entries = {148883, 148884};
    CHECK(NormalizePlanStep(twoProof) == PlanStepRefusal::BadEntries);
    PlanStep flat = StatueProofStep();
    flat.radius = 0.0f;
    CHECK(NormalizePlanStep(flat) == PlanStepRefusal::BadRadius);
    PlanStep wide = StatueProofStep();
    wide.radius = kPlanMaxProofRadius + 0.5f;
    CHECK(NormalizePlanStep(wide) == PlanStepRefusal::BadRadius);
    PlanStep noPoint = StatueProofStep();
    noPoint.x = noPoint.y = noPoint.z = 0.0f;
    CHECK(NormalizePlanStep(noPoint) == PlanStepRefusal::BadPoint);
    PlanStep withParam = StatueProofStep();
    withParam.param = 1;
    CHECK(NormalizePlanStep(withParam) == PlanStepRefusal::BadParam);
    PlanStep badFlag = StatueProofStep();
    badFlag.flags = 256;
    CHECK(NormalizePlanStep(badFlag) == PlanStepRefusal::BadParam);
    PlanStep reserved = StatueProofStep();
    reserved.flags = 128;
    CHECK(NormalizePlanStep(reserved) == PlanStepRefusal::BadParam);
    PlanStep itself = StatueProofStep();
    itself.entries = {148830};
    CHECK(NormalizePlanStep(itself) == PlanStepRefusal::BadParam);
    PlanStep withTimeout = StatueProofStep();
    withTimeout.timeoutS = 30;
    CHECK(NormalizePlanStep(withTimeout) == PlanStepRefusal::BadTimeout);
    // A creature of the same number is not the used object itself.
    PlanStep creatureSame = StatueProofStep();
    creatureSame.entries = {148830};
    creatureSame.flags = kPlanFlagProofCreature;
    CHECK(NormalizePlanStep(creatureSame) == PlanStepRefusal::None);
    // openProof on anything but an open is a row the exporter got wrong.
    PlanStep wrongKind = Step(0, 201, PlanStepKind::Pull);
    wrongKind.entries = {4851};
    wrongKind.openProof = true;
    CHECK(NormalizePlanStep(wrongKind) == PlanStepRefusal::BadParam);
}

static void TestProofCreatureFlagOnlyOnOpenProof()
{
    PlanStep open = Step(0, 202, PlanStepKind::Open, 1.0f, 2.0f, 3.0f);
    open.entry = 124371;
    open.flags = kPlanFlagProofCreature;
    CHECK(NormalizePlanStep(open) == PlanStepRefusal::BadParam);
    PlanStep pull = Step(0, 203, PlanStepKind::Pull);
    pull.entries = {7286};
    pull.flags = kPlanFlagProofCreature;
    CHECK(NormalizePlanStep(pull) == PlanStepRefusal::BadParam);
}

static void TestPlainOpenUnchanged()
{
    // A plain open with a radius, extra entries and no param still loads: none
    // of the proof rules touches it.
    PlanStep open = Step(0, 205, PlanStepKind::Open, 1858.7f, 1053.8f, 8.9f);
    open.entry = 128403;
    open.radius = 12.0f;
    open.entries = {5};
    CHECK(NormalizePlanStep(open) == PlanStepRefusal::None);
}


static void TestTable()
{
    DungeonPlanTable table;
    CHECK(table.AddPlan("uldaman", 70));
    CHECK(!table.AddPlan("uldaman", 70));
    CHECK(!table.AddPlan("", 70));
    CHECK(!table.AddPlan("zero", 0));
    CHECK(table.AddLeg("uldaman", 1, "revelosh"));
    CHECK(table.AddLeg("uldaman", 0, "baelog"));
    CHECK(!table.AddLeg("uldaman", 0, "again"));
    CHECK(!table.AddLeg("nope", 0, "x"));

    PlanStep pull = Step(1, 2, PlanStepKind::Pull, -281.8f, 163.8f, -47.3f);
    pull.entries = {4851, 4852};
    CHECK(table.AddStep("uldaman", pull) == PlanStepRefusal::None);
    CHECK(table.AddStep("uldaman", Step(1, 1, PlanStepKind::Move, -286.7f, 143.2f, -47.2f)) == PlanStepRefusal::None);
    CHECK(table.AddStep("uldaman", Step(0, 9, PlanStepKind::Move, -236.5f, 54.9f, -46.0f)) == PlanStepRefusal::None);
    CHECK(table.AddStep("uldaman", Step(0, 9, PlanStepKind::Rest)) == PlanStepRefusal::Duplicate);
    CHECK(table.AddStep("other", Step(0, 1, PlanStepKind::Rest)) == PlanStepRefusal::UnknownPlan);
    CHECK(std::string(PlanStepRefusalName(PlanStepRefusal::BadEntries)) == "BadEntries");
    CHECK(std::string(PlanStepRefusalName(PlanStepRefusal::Duplicate)) == "Duplicate");

    // Nothing is readable before the seal.
    CHECK(table.Find("uldaman") == nullptr);
    CHECK(table.PlansOnMap(70).empty());
    table.Seal();
    CHECK(table.AddStep("uldaman", Step(2, 1, PlanStepKind::Rest)) == PlanStepRefusal::Sealed);
    CHECK(!table.AddPlan("late", 1));

    const DungeonPlan* plan = table.Find("uldaman");
    CHECK(plan && plan->map == 70 && plan->steps.size() == 3);
    // Run order: leg, then seq.
    CHECK(plan->steps[0].leg == 0 && plan->steps[0].seq == 9);
    CHECK(plan->steps[1].leg == 1 && plan->steps[1].seq == 1);
    CHECK(plan->steps[2].kind == PlanStepKind::Pull);
    CHECK(plan->LegName(0) == "baelog" && plan->LegName(1) == "revelosh" && plan->LegName(7).empty());
    CHECK(table.PlansOnMap(70).size() == 1 && table.PlansOnMap(1).empty());
    CHECK(table.StepCount() == 3 && table.PlanCount() == 1 && table.All().size() == 1);
}

static void TestLaterOwner()
{
    DungeonPlan plan;
    PlanStep a = Step(0, 1, PlanStepKind::Pull, 0, 0, 0);
    a.entries = {100};
    a.radius = 10;
    PlanStep b = Step(0, 2, PlanStepKind::Move, 50, 0, 0);
    PlanStep c = Step(0, 3, PlanStepKind::Pull, 60, 0, 0);
    c.entries = {100, 101};
    c.radius = 10;
    PlanStep d = Step(0, 4, PlanStepKind::Boss, 100, 0, 0);
    d.entry = 200;
    d.entries = {201};
    d.radius = 30;
    plan.steps = {a, b, c, d};
    // From step 0: a unit of 100 at the second pack belongs to step 2.
    CHECK(OwnedByLaterStep(plan, 0, 100, 62, 0, 0));
    // Its own pack is not "later".
    CHECK(!OwnedByLaterStep(plan, 0, 100, 1, 0, 0));
    // From step 2 the first pack is behind us.
    CHECK(!OwnedByLaterStep(plan, 2, 100, 1, 0, 0));
    // Boss and its listed adds.
    CHECK(OwnedByLaterStep(plan, 0, 200, 110, 0, 0));
    CHECK(OwnedByLaterStep(plan, 2, 201, 90, 0, 0));
    CHECK(!OwnedByLaterStep(plan, 0, 999, 60, 0, 0));
    CHECK(!OwnedByLaterStep(plan, 3, 200, 100, 0, 0));
    // A later step for the other team owns nothing for this party.
    plan.steps[2].flags = kPlanFlagHorde;
    CHECK(OwnedByLaterStep(plan, 0, 100, 62, 0, 0));
    CHECK(OwnedByLaterStep(plan, 0, 100, 62, 0, 0, kPlanFlagHorde));
    CHECK(!OwnedByLaterStep(plan, 0, 100, 62, 0, 0, kPlanFlagAlliance));
}

static void TestLongHops()
{
    DungeonPlan plan;
    plan.steps = {Step(0, 1, PlanStepKind::Move, 0, 0, 0), Step(0, 2, PlanStepKind::Rest),
                  Step(0, 3, PlanStepKind::Move, 50, 0, 0), Step(0, 4, PlanStepKind::MoveDirect, 130, 0, 0)};
    CHECK(CountLongHops(plan, 60.0f) == 1);
    CHECK(CountLongHops(plan, 100.0f) == 0);
}

// Issue #999 item 5: the plan-wide unit lists (ai_dungeon_plan cc_priority,
// cc_always, kill_first) and their ranks.
static void TestPlanUnits()
{
    DungeonPlanTable table;
    CHECK(table.AddPlan("zulfarrak", 209));
    CHECK(!table.SetUnits("nope", "", "", ""));
    CHECK(!table.SetUnits("zulfarrak", "5650,x", "", "")); // not a list: none kept
    CHECK(table.SetUnits("zulfarrak", "5650,7247,7246,5648", "5650,7246,9999", "8179,5650,7247"));
    table.Seal();
    CHECK(!table.SetUnits("zulfarrak", "", "", ""));
    const DungeonPlan* plan = table.Find("zulfarrak");
    CHECK(plan && plan->ccPriority.size() == 4 && plan->killFirst.size() == 3);
    // cc_always first in its own order (5650, 7246, 9999), then the
    // cc_priority entries it does not list (7247, 5648).
    CHECK(plan->CcRank(5650) == 0 && plan->CcRank(7246) == 1 && plan->CcRank(9999) == 2);
    CHECK(plan->CcRank(7247) == 3 && plan->CcRank(5648) == 4);
    CHECK(plan->CcRank(1) == -1);
    // Zul'Farrak's owner values: the Shadowhunter always first.
    DungeonPlan zf;
    zf.ccPriority = {7247, 5648, 7246};
    zf.ccAlways = {7246};
    CHECK(zf.CcRank(7246) == 0 && zf.CcRank(7247) == 1 && zf.CcRank(5648) == 2);
    CHECK(plan->KillFirstRank(8179) == 0 && plan->KillFirstRank(7247) == 2 && plan->KillFirstRank(7246) == -1);

    // No lists (the columns' default ''): nothing ranked.
    DungeonPlanTable bare;
    CHECK(bare.AddPlan("u", 70) && bare.SetUnits("u", "", "", ""));
    bare.Seal();
    CHECK(bare.Find("u")->CcRank(5650) == -1 && bare.Find("u")->KillFirstRank(8179) == -1);
}

static void TestBossEntries()
{
    // The entries of the plans' boss steps, per map: the run's own boss list,
    // which the telemetry ranks 'b' (five-man bosses carry creature rank 1).
    DungeonPlanTable table;
    CHECK(table.AddPlan("uldaman", 70));
    CHECK(table.AddPlan("wailing_caverns", 43));
    PlanStep baelog = Step(0, 12, PlanStepKind::Boss, -353.0f, 117.2f, -44.4f);
    baelog.entry = 6906;
    baelog.entries = {6907, 6908};  // killed first, but not the step's boss
    CHECK(table.AddStep("uldaman", baelog) == PlanStepRefusal::None);
    PlanStep pull = Step(0, 13, PlanStepKind::Pull, -281.8f, 163.8f, -47.3f);
    pull.entries = {4851, 4852};
    CHECK(table.AddStep("uldaman", pull) == PlanStepRefusal::None);
    PlanStep open = Step(0, 14, PlanStepKind::Open, -230.0f, 160.0f, -45.0f);
    open.entry = 124371;  // an object step's entry is no boss
    CHECK(table.AddStep("uldaman", open) == PlanStepRefusal::None);
    PlanStep cobrahn = Step(4, 8, PlanStepKind::Boss, -151.1f, 414.4f, -72.6f);
    cobrahn.entry = 3669;
    CHECK(table.AddStep("wailing_caverns", cobrahn) == PlanStepRefusal::None);

    CHECK(!table.IsBossEntry(70, 6906));  // nothing before the seal
    table.Seal();
    CHECK(table.IsBossEntry(70, 6906));
    CHECK(table.IsBossEntry(43, 3669));
    CHECK(!table.IsBossEntry(43, 6906));  // another map's boss
    CHECK(!table.IsBossEntry(70, 6907) && !table.IsBossEntry(70, 6908));  // a boss step's adds
    CHECK(!table.IsBossEntry(70, 4851));  // a pull's member
    CHECK(!table.IsBossEntry(70, 124371));  // another kind of step's entry
    CHECK(!table.IsBossEntry(70, 0) && !table.IsBossEntry(0, 0));
}

static void TestBossRows()
{
    // ai_dungeon_boss rows: bosses no boss step names (Kresh 3653 is skipped, Mutanus 3654 is an event), in the
    // same (map, entry) set as the boss steps' entries.
    DungeonPlanTable table;
    CHECK(table.AddPlan("wailing_caverns", 43));
    PlanStep cobrahn = Step(4, 8, PlanStepKind::Boss, -151.1f, 414.4f, -72.6f);
    cobrahn.entry = 3669;
    CHECK(table.AddStep("wailing_caverns", cobrahn) == PlanStepRefusal::None);
    CHECK(table.AddBoss(43, 3653));
    CHECK(table.AddBoss(43, 3654));
    CHECK(table.AddBoss(43, 3669));   // also a boss step: one key, no harm
    CHECK(!table.AddBoss(43, 3653));  // the primary key: first row wins
    CHECK(!table.AddBoss(0, 3653) && !table.AddBoss(43, 0));
    CHECK(!table.IsBossEntry(43, 3653));  // nothing before the seal
    table.Seal();
    CHECK(!table.AddBoss(43, 3671));  // loading only
    CHECK(table.IsBossEntry(43, 3653) && table.IsBossEntry(43, 3654) && table.IsBossEntry(43, 3669));
    CHECK(!table.IsBossEntry(43, 3671));
    CHECK(!table.IsBossEntry(70, 3653));  // another map
    CHECK(table.BossCount() == 3);
}

int main()
{
    TestOpenProofRowParses();
    TestOpenProofIsNoPlainKind();
    TestOpenProofNormalize();
    TestProofCreatureFlagOnlyOnOpenProof();
    TestPlainOpenUnchanged();
    TestPlanUnits();
    TestKinds();
    TestEntries();
    TestNormalize();
    TestTable();
    TestBossEntries();
    TestBossRows();
    TestLaterOwner();
    TestLongHops();
    std::cout << "test_dungeon_plan_policy: ok\n";
    return 0;
}
