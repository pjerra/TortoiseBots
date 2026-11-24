// Standalone test of the PURE half of the run's object, loot and use tasks (forever/src/host/run/TbRunTasksPure.h; package O9, forever/docs/SLICE9-DOORS-NOTES.md):
//   g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/test_run_tasks.cpp -o /tmp/test_run_tasks && /tmp/test_run_tasks
// It runs next to the run leader's own header (runtime/DungeonRunPolicy.h) so that every answer text is checked against the policy's parsers.
#include "forever/src/host/run/TbRunOrdersPure.h"
#include "forever/src/host/run/TbRunTasksPure.h"
#include "runtime/DungeonRunPolicy.h"

#include <cstdio>

namespace od = TortoiseBots::run::orders;
namespace tk = TortoiseBots::run::tasks;
namespace DR = TortoiseBots::DungeonRun;
namespace RC = TortoiseBots::RemoteCommander;

static int g_checks = 0, g_fail = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static od::Parsed Parse(std::string const& order, bool expect = true)
{
    od::Parsed p;
    std::string code, message;
    bool const ok = od::ParseOrder(order, p, code, message);
    if (ok != expect)
        std::printf("  parse of '%s': ok=%d code=%s msg=%s\n", order.c_str(), ok, code.c_str(), message.c_str());
    CHECK(ok == expect);
    return p;
}

// The strings the policy builds for the task steps (DungeonRunPolicy.h TaskStep: "open " + EntryArg + " " + carrier; LootOrderText: "loot " + target + " " + bot + item;
// "lootmethod ffa"; the restore "lootmethod " + name; "use " + entry + " " + carrier).
static void TestGrammar()
{
    od::Parsed p = Parse("open " + DR::EntryArg(17153, -290.3f, -537.0f, 24.7f) + " Mtank");
    CHECK(p.verb == od::Verb::Open && p.task.verb == tk::Verb::Open && p.task.bot == "Mtank");
    CHECK(p.task.spec.ByEntry() && p.task.spec.entry == 17153 && p.task.spec.hasPoint && std::fabs(p.task.spec.point.x + 290.3f) < 0.06f);
    p = Parse("open " + DR::EntryArg(16398, -102.0f, -705.0f, 7.7f) + " Mtank");
    CHECK(p.task.spec.entry == 16398);
    // a boss corpse by its handle (ReadFacts' bossCorpseGuid), with and without the item
    p = Parse("loot " + DR::GuidArg(12) + " Mtank");
    CHECK(p.verb == od::Verb::Loot && !p.task.spec.ByEntry() && p.task.spec.guid == 12 && p.task.itemId == 0);
    p = Parse("loot " + DR::GuidArg(12) + " Mtank 5397");
    CHECK(p.task.itemId == 5397 && p.task.bot == "Mtank");
    p = Parse("loot " + DR::EntryArg(17155, -96.0f, -722.0f, 18.0f) + " Mtank 5397");
    CHECK(p.task.spec.entry == 17155 && p.task.itemId == 5397);
    // the free-for-all detour and its restore (the policy builds them from RemoteCommander::LootMethodName)
    p = Parse("lootmethod ffa");
    CHECK(p.verb == od::Verb::LootMethod && p.task.method == RC::kLootFfa && !p.task.hasThreshold);
    for (int m : { RC::kLootFfa, RC::kLootRoundRobin, RC::kLootMaster, RC::kLootGroup, RC::kLootNeedBeforeGreed })
    {
        p = Parse(std::string("lootmethod ") + RC::LootMethodName(m));
        CHECK(p.task.method == m);
    }
    p = Parse("lootmethod group rare");
    CHECK(p.task.hasThreshold && p.task.threshold == 3);
    p = Parse("use 5397 Mtank");
    CHECK(p.verb == od::Verb::Use && p.task.itemId == 5397 && p.task.bot == "Mtank");
    // broken ones are `invalid`, never accepted
    for (char const* bad : { "loot", "loot 12", "loot entry:0 Mtank", "loot 12 Mtank x", "open", "open 12", "open entry:1@1,2 Mtank", "lootmethod", "lootmethod sideways", "lootmethod group 9 9", "use 5397", "use abc Mtank" })
    {
        od::Parsed q;
        std::string code, message;
        CHECK(!od::ParseOrder(bad, q, code, message));
        CHECK(code == "invalid");
    }
}

static void TestMethod()
{
    auto order = [](char const* text) { return Parse(text).task; };
    CHECK(tk::JudgeLootMethod(order("lootmethod ffa")).ok);
    CHECK(tk::JudgeLootMethod(order("lootmethod group")).ok);
    CHECK(tk::JudgeLootMethod(order("lootmethod rr")).ok);
    CHECK(tk::JudgeLootMethod(order("lootmethod group epic")).ok);
    CHECK(tk::JudgeLootMethod(order("lootmethod master")).ok);
    CHECK(tk::JudgeLootMethod(order("lootmethod nbg")).ok);
    CHECK(tk::ChooseLootMaster(RC::kLootMaster, true) == tk::LootMasterTarget::BoundMaster);
    CHECK(tk::ChooseLootMaster(RC::kLootMaster, false) == tk::LootMasterTarget::GroupLeader);
    for (int m : { RC::kLootFfa, RC::kLootRoundRobin, RC::kLootGroup, RC::kLootNeedBeforeGreed })
    {
        CHECK(tk::ChooseLootMaster(m, true) == tk::LootMasterTarget::Empty);
        CHECK(tk::ChooseLootMaster(m, false) == tk::LootMasterTarget::Empty);
    }
    tk::MethodVerdict v = tk::JudgeLootMethod(order("lootmethod group common"));      // the core refuses a threshold below uncommon
    CHECK(!v.ok && v.code == "invalid");
    // the core's methods as the policy reads them: PERSONAL_LOOT (5) and anything unknown are "no method"
    CHECK(tk::PolicyLootMethod(0) == RC::kLootFfa && tk::PolicyLootMethod(1) == RC::kLootRoundRobin && tk::PolicyLootMethod(2) == RC::kLootMaster);
    CHECK(tk::PolicyLootMethod(3) == RC::kLootGroup && tk::PolicyLootMethod(4) == RC::kLootNeedBeforeGreed);
    CHECK(tk::PolicyLootMethod(5) == -1 && tk::PolicyLootMethod(6) == -1 && tk::PolicyLootMethod(200) == -1);
    std::string why;
    CHECK(tk::RunLootMethodOk(RC::kLootGroup, why));
    CHECK(tk::RunLootMethodOk(RC::kLootNeedBeforeGreed, why));
    CHECK(!tk::RunLootMethodOk(RC::kLootFfa, why) && why.find("ffa") != std::string::npos);
    CHECK(!tk::RunLootMethodOk(-1, why) && why.find("personal") != std::string::npos);
    // a method changed under a running run is refused by the same rule: the run pauses and its pending task dies before any act
    CHECK(!tk::RunLootMethodOk(RC::kLootRoundRobin, why) && why.find("rr") != std::string::npos);
    CHECK(!tk::RunLootMethodOk(RC::kLootMaster, why) && why.find("master") != std::string::npos);
    CHECK(tk::StartBagsOk(12) && tk::StartBagsOk(16) && !tk::StartBagsOk(11) && !tk::StartBagsOk(0));
    CHECK(DR::kTaskCapMs > tk::kTaskCapMs);
}

static void TestLock()
{
    RC::LockFacts f;
    CHECK(tk::JudgeLock(f).ok);                                     // no lock rows: open
    f.keyItem = 5397;                                               // the cannon's lock 83: key item 5397
    tk::LockJudgement j = tk::JudgeLock(f);
    CHECK(!j.ok && j.code == "no-item");
    f.hasKey = true;
    CHECK(tk::JudgeLock(f).ok);
    RC::LockFacts skill;
    skill.skillId = 633;
    skill.reqSkill = 25;
    skill.skillValue = 1;
    j = tk::JudgeLock(skill);
    CHECK(!j.ok && j.code == "locked");
    skill.skillValue = 25;
    CHECK(tk::JudgeLock(skill).ok);
    // a door is used once: an open one (state != ready) is never used again (the core's Use toggles a door)
    CHECK(!tk::AlreadyUsed(1) && tk::AlreadyUsed(0) && tk::AlreadyUsed(2));
}

static tk::Obs Walking(float distance)
{
    tk::Obs o;
    o.distance = distance;
    o.inRange = false;
    return o;
}

// Runs the clock `looks` times at 250 ms; returns the first step that is not a wait or a walk order.
static tk::Step Run(tk::TaskRec& r, tk::Obs const& o, int looks = 1)
{
    tk::Step s;
    for (int i = 0; i < looks; ++i)
    {
        s = tk::Advance(r, o, RC::kTaskTickMs);
        if (s.what != tk::Do::Wait && s.what != tk::Do::OrderWalk)
            return s;
    }
    return s;
}

static void TestDoor()
{
    // walk, act, effect: Deadmines leg 4's Heavy Door
    tk::TaskRec r = tk::MakeRec(tk::Kind::Open, false, false, 0, 9.0f);
    tk::Obs o = Walking(9.0f);
    tk::Step s = tk::Advance(r, o, RC::kTaskTickMs);
    CHECK(s.what == tk::Do::OrderWalk);                             // the first look sends the bot to its stand
    s = tk::Advance(r, o, RC::kTaskTickMs);
    CHECK(s.what == tk::Do::Wait);                                  // ... and waits
    o.moving = true;
    for (int i = 0; i < 20; ++i)
        CHECK(tk::Advance(r, o, RC::kTaskTickMs).what == tk::Do::Wait);   // a walking bot is not sent again
    o.moving = false;
    o.distance = 4.0f;
    o.inRange = true;
    s = tk::Advance(r, o, RC::kTaskTickMs);
    CHECK(s.what == tk::Do::BeginAct && r.state.stage == RC::TaskStage::Act);
    r.baseline = tk::PackState(0, 1);
    tk::NoteActStart(r, true, false, "", "");
    o.objectState = tk::PackState(0, 1);
    CHECK(tk::Advance(r, o, RC::kTaskTickMs).what == tk::Do::Wait);   // no reaction yet
    o.objectState = tk::PackState(1, 0);                              // the door changed state (activated, open)
    s = tk::Advance(r, o, RC::kTaskTickMs);
    CHECK(s.what == tk::Do::Succeed && s.message == "opened");
}

static void TestDoorFailures()
{
    // a fight on the way: in-combat (the policy sends the order again: TaskResendable)
    tk::TaskRec r = tk::MakeRec(tk::Kind::Open, false, false, 0, 9.0f);
    tk::Obs o = Walking(9.0f);
    o.inCombat = true;
    tk::Step s = tk::Advance(r, o, RC::kTaskTickMs);
    CHECK(s.what == tk::Do::Fail && s.code == "in-combat");
    CHECK(DR::TaskResendable(od::RefusalText(s.code, s.message)));
    // the object is gone on the way: the words GoneIsDone looks for
    r = tk::MakeRec(tk::Kind::Open, false, false, 0, 9.0f);
    o = Walking(9.0f);
    o.targetPresent = false;
    s = tk::Advance(r, o, RC::kTaskTickMs);
    CHECK(s.what == tk::Do::Fail && s.code == "invalid-target");
    ai::PlanStep row;
    row.kind = ai::PlanStepKind::Open;
    row.flags = ai::kPlanFlagGoneIsDone;
    CHECK(DR::GoneIsDone(&row, od::RefusalText(s.code, s.message)));
    // the bot stalls short of the object: unreachable after 8 s
    r = tk::MakeRec(tk::Kind::Open, false, false, 0, 9.0f);
    o = Walking(9.0f);
    s = Run(r, o, 60);
    CHECK(s.what == tk::Do::Fail && s.code == "unreachable");
    // the walk order failed in the engine
    r = tk::MakeRec(tk::Kind::Open, false, false, 0, 9.0f);
    o = Walking(9.0f);
    o.walkOrderFailed = true;
    o.walkOrderText = "fail:stay";
    s = tk::Advance(r, o, RC::kTaskTickMs);
    CHECK(s.what == tk::Do::Fail && s.code == "unreachable" && s.message.find("fail:stay") != std::string::npos);
    // the bot died: unavailable
    r = tk::MakeRec(tk::Kind::Open, false, false, 0, 9.0f);
    o = Walking(9.0f);
    o.botAvailable = false;
    s = tk::Advance(r, o, RC::kTaskTickMs);
    CHECK(s.what == tk::Do::Fail && s.code == "unavailable");
    // used, no change within 4 s: failed, never "opened"
    r = tk::MakeRec(tk::Kind::Open, false, false, 0, 2.0f);
    o = Walking(2.0f);
    o.inRange = true;
    s = tk::Advance(r, o, RC::kTaskTickMs);
    CHECK(s.what == tk::Do::BeginAct);
    r.baseline = tk::PackState(0, 1);
    tk::NoteActStart(r, true, false, "", "");
    o.objectState = tk::PackState(0, 1);
    s = Run(r, o, 30);
    CHECK(s.what == tk::Do::Fail && s.code == "failed" && s.message.find("no change") != std::string::npos);
    // the object vanished while it was used
    r = tk::MakeRec(tk::Kind::Open, false, false, 0, 2.0f);
    o = Walking(2.0f);
    o.inRange = true;
    tk::Advance(r, o, RC::kTaskTickMs);
    r.baseline = tk::PackState(0, 1);
    tk::NoteActStart(r, true, false, "", "");
    o.objectPresent = false;
    s = tk::Advance(r, o, RC::kTaskTickMs);
    CHECK(s.what == tk::Do::Fail && s.message.find("disappeared") != std::string::npos);
    // a start that is refused again and again ends with the newest refusal after RC::kActTries tries
    r = tk::MakeRec(tk::Kind::Open, false, false, 0, 2.0f);
    o = Walking(2.0f);
    o.inRange = true;
    s = tk::Advance(r, o, RC::kTaskTickMs);
    CHECK(s.what == tk::Do::BeginAct);
    int begins = 1;
    tk::NoteActStart(r, false, false, "busy", "The bot is busy casting another spell.");
    for (int i = 0; i < 10 && s.what != tk::Do::Fail; ++i)
    {
        s = tk::Advance(r, o, RC::kTaskTickMs);
        if (s.what == tk::Do::BeginAct)
        {
            ++begins;
            tk::NoteActStart(r, false, false, "busy", "The bot is busy casting another spell.");
        }
    }
    CHECK(s.what == tk::Do::Fail && s.code == "busy" && begins == int(RC::kActTries));
    // a final refusal (the lock) answers at the next look without retries
    r = tk::MakeRec(tk::Kind::Open, false, false, 0, 2.0f);
    o = Walking(2.0f);
    o.inRange = true;
    tk::Advance(r, o, RC::kTaskTickMs);
    tk::NoteActStart(r, false, true, "no-item", "The bot does not carry the key or item that opens this lock.");
    s = tk::Advance(r, o, RC::kTaskTickMs);
    CHECK(s.what == tk::Do::Fail && s.code == "no-item");
    // in a fight the act is not retried (the task fails in-combat first)
    r = tk::MakeRec(tk::Kind::Open, false, false, 0, 2.0f);
    o = Walking(2.0f);
    o.inRange = true;
    tk::Advance(r, o, RC::kTaskTickMs);
    tk::NoteActStart(r, false, false, "busy", "x");
    o.inCombat = true;
    s = tk::Advance(r, o, RC::kTaskTickMs);
    CHECK(s.what == tk::Do::Fail && s.code == "in-combat");
}

static void TestGoober()
{
    // The Defias Cannon: the goober's script answers the use and its own state does not change: delivered = used, once the settle time has passed, and only then
    tk::TaskRec r = tk::MakeRec(tk::Kind::Open, false, false, 0, 2.0f, true);
    tk::Obs o = Walking(2.0f);
    o.inRange = true;
    CHECK(tk::Advance(r, o, RC::kTaskTickMs).what == tk::Do::BeginAct);
    r.baseline = tk::PackState(0, 1);
    tk::NoteActStart(r, true, false, "", "");
    o.objectState = tk::PackState(0, 1);
    tk::Step s;
    int looks = 0;
    do
    {
        s = tk::Advance(r, o, RC::kTaskTickMs);
        ++looks;
    } while (s.what == tk::Do::Wait && looks < 40);
    CHECK(s.what == tk::Do::Succeed && s.message.compare(0, 5, "used:") == 0 && s.message != "opened");
    CHECK(looks >= int(tk::kScriptSettleMs / RC::kTaskTickMs));
    // a door is never reported "used" without its state changing
    tk::TaskRec door = tk::MakeRec(tk::Kind::Open, false, false, 0, 2.0f, false);
    tk::Obs d = Walking(2.0f);
    d.inRange = true;
    tk::Advance(door, d, RC::kTaskTickMs);
    door.baseline = tk::PackState(0, 1);
    tk::NoteActStart(door, true, false, "", "");
    d.objectState = tk::PackState(0, 1);
    s = Run(door, d, 30);
    CHECK(s.what == tk::Do::Fail && s.code == "failed");
    // a goober that vanished is no success
    tk::TaskRec g2 = tk::MakeRec(tk::Kind::Open, false, false, 0, 2.0f, true);
    tk::Obs e = Walking(2.0f);
    e.inRange = true;
    tk::Advance(g2, e, RC::kTaskTickMs);
    tk::NoteActStart(g2, true, false, "", "");
    e.objectPresent = false;
    s = tk::Advance(g2, e, RC::kTaskTickMs);
    CHECK(s.what == tk::Do::Fail);
}

static void TestLoot()
{
    // a boss corpse, the whole window: money and an item
    tk::TaskRec r = tk::MakeRec(tk::Kind::Loot, true, true, 0, 3.0f);
    tk::Obs o = Walking(3.0f);
    o.inRange = true;
    tk::Step s = tk::Advance(r, o, RC::kTaskTickMs);
    CHECK(s.what == tk::Do::BeginAct);
    tk::NoteActStart(r, true, false, "", "");
    o.watchFound = true;
    CHECK(tk::Advance(r, o, RC::kTaskTickMs).what == tk::Do::Wait);   // the window is not closed yet
    o.windowFinished = true;
    o.itemsTaken = 1;
    o.looted = "5397:1,copper:30";
    s = tk::Advance(r, o, RC::kTaskTickMs);
    CHECK(s.what == tk::Do::Succeed && s.message == "5397:1,copper:30");
    // the ordered item was not in the window
    r = tk::MakeRec(tk::Kind::Loot, true, true, 5397, 3.0f);
    o = Walking(3.0f);
    o.inRange = true;
    tk::Advance(r, o, RC::kTaskTickMs);
    tk::NoteActStart(r, true, false, "", "");
    o.watchFound = o.windowFinished = true;
    s = tk::Advance(r, o, RC::kTaskTickMs);
    CHECK(s.what == tk::Do::Fail && s.message.find("not in the loot") != std::string::npos);
    // the server refused the window
    r = tk::MakeRec(tk::Kind::Loot, true, true, 0, 3.0f);
    o = Walking(3.0f);
    o.inRange = true;
    tk::Advance(r, o, RC::kTaskTickMs);
    tk::NoteActStart(r, true, false, "", "");
    o.watchFound = o.windowFinished = o.windowRefused = true;
    s = tk::Advance(r, o, RC::kTaskTickMs);
    CHECK(s.what == tk::Do::Fail && s.message.find("refused") != std::string::npos);
    // the watch is lost: failed, not "looted"
    r = tk::MakeRec(tk::Kind::Loot, true, true, 0, 3.0f);
    o = Walking(3.0f);
    o.inRange = true;
    tk::Advance(r, o, RC::kTaskTickMs);
    tk::NoteActStart(r, true, false, "", "");
    o.watchFound = false;
    o.windowFinished = true;
    s = Run(r, o, 80);
    CHECK(s.what == tk::Do::Fail);                                    // never a success without the watch (it times out)
    // the act is refused for the loot rules: the policy's words move the order to another bot
    r = tk::MakeRec(tk::Kind::Loot, true, true, 0, 3.0f);
    o = Walking(3.0f);
    o.inRange = true;
    tk::Advance(r, o, RC::kTaskTickMs);
    tk::NoteActStart(r, false, true, "failed", std::string("This corpse has ") + tk::LootRightsText() + " under the group's loot rules.");
    s = tk::Advance(r, o, RC::kTaskTickMs);
    CHECK(s.what == tk::Do::Fail && DR::LootRefusalOpen(od::RefusalText(s.code, s.message)));
    CHECK(DR::LootRefusalOpen(std::string("failed: ") + tk::LootEmptyText() + " on this corpse."));
    // a chest: the window comes after the 5 s opening cast (the loot budget is 8 s from the cast's end)
    r = tk::MakeRec(tk::Kind::Loot, true, false, 5397, 3.0f);
    CHECK(r.state.actBudgetMs == RC::kLootWindowMs);
    // the whole task has a cap
    r = tk::MakeRec(tk::Kind::Loot, true, true, 0, 3.0f);
    o = Walking(3.0f);
    o.moving = true;
    r.state.walkBudgetMs = 1000000;
    s = Run(r, o, 400);
    CHECK(s.what == tk::Do::Fail);
}

static void TestChestCastBudget()
{
    // A locked chest's Opening cast runs inside the act stage: its 5 s are added on top of the 8 s window wait, so the stage does not time out while
    // the cast itself is still running (the Turtle BeginLootAct and ActBudgetAfterCast).
    tk::TaskRec r = tk::MakeRec(tk::Kind::Loot, true, false, 5397, 3.0f);
    r.state.stage = RC::TaskStage::Act;
    r.state.stageMs = 0;
    r.actStarted = true;
    tk::NoteActCast(r, 5000);
    CHECK(r.state.actBudgetMs == RC::kLootWindowMs + 5000);
    tk::Obs o;
    o.watchFound = true;
    o.windowFinished = false;
    tk::Step s;
    for (int i = 0; i < 9; ++i)
        s = tk::Advance(r, o, 1000);
    CHECK(s.what == tk::Do::Wait);                       // 9 s in: the 5 s cast and the window wait are not over, and the act is not answered as a timeout
    o.windowFinished = true;
    o.itemsTaken = 1;
    o.looted = "5397:1";
    s = tk::Advance(r, o, 250);
    CHECK(s.what == tk::Do::Succeed);
    // an instant act (no cast) keeps its budget
    tk::TaskRec door = tk::MakeRec(tk::Kind::Open, false, false, 0, 0.0f);
    uint32_t const before = door.state.actBudgetMs;
    tk::NoteActCast(door, 0);
    CHECK(door.state.actBudgetMs == before);
}

static void TestTargets()
{
    uint64_t key = 0;
    CHECK(!tk::PickEntryTarget({}, key));
    CHECK(!tk::PickEntryTarget({ { 7, 5.5f } }, key));                       // beyond 5 yd of the row: not it, never another object of the entry
    CHECK(tk::PickEntryTarget({ { 7, 4.9f }, { 8, 0.4f }, { 9, 2.0f } }, key) && key == 8);
    CHECK(tk::PickEntryTarget({ { 9, 1.0f }, { 4, 1.0f } }, key) && key == 4);   // equal distance: the lowest key
    CHECK(tk::NotFoundText(17153).find("17153") != std::string::npos);
}

static void TestExecutorTexts()
{
    // The answers the executor builds: "<code>: <text>", and an accepted task order names its bot.
    CHECK(od::RefusalText("unsupported", tk::UseUnsupportedText()).compare(0, 12, "unsupported:") == 0);
    CHECK(od::AckText("bot:Mtank", 1) == "bot:Mtank 1");
    // the stand: 1.5 yd short of the object on the bot's side
    RC::Point3 const p = RC::StandingPoint(0.0f, 10.0f, 0.0f, 0.0f, 3.0f);
    CHECK(std::fabs(p.y - 1.5f) < 0.001f && std::fabs(p.z - 3.0f) < 0.001f);
}

// WC: the gossip task of the Naralex event. The act is over at once (the core runs the choice inside the select handler), the payload is the menu line
// the matcher picked, and the two refusal texts are GossipOrder's own.
static void TestGossip()
{
    tk::TaskRec rec = tk::MakeRec(tk::Kind::Gossip, false, false, 0, 3.0f);
    CHECK(rec.state.kind == RC::TaskKind::Gossip);
    tk::Obs o;
    o.botAvailable = true;
    o.inRange = true;
    o.distance = 3.0f;
    tk::Step begin = tk::Advance(rec, o, 500);
    CHECK(begin.what == tk::Do::BeginAct);              // in range: the gossip act starts at once
    tk::NoteActStart(rec, true, false, "", "");
    rec.gossipPicked = "Let the event begin!";
    tk::Step step = tk::Advance(rec, o, 500);
    CHECK(step.what == tk::Do::Succeed && step.message == "Let the event begin!");
    // not started yet: the walk stage keeps it waiting, the act stage answers once it started
    tk::TaskRec walk = tk::MakeRec(tk::Kind::Gossip, false, false, 0, 12.0f);
    tk::Obs far;
    far.distance = 12.0f;
    tk::Step s2 = tk::Advance(walk, far, 500);
    CHECK(s2.what != tk::Do::Succeed);
    // the grammar the policy sends: "gossip <entry> <bot> <index>"
    od::Parsed p = Parse("gossip 3678 Mtank 1");
    CHECK(p.verb == od::Verb::Gossip && p.task.verb == tk::Verb::Gossip && p.task.bot == "Mtank");
    CHECK(p.task.gossip.entry == 3678 && p.task.gossip.byIndex && p.task.gossip.index == 1);
    // the target is the NPC by entry, never a handle of an earlier read (Shadowfang Keep, 2026-11-23: refused invalid-target)
    CHECK(p.task.spec.ByEntry() && p.task.spec.entry == 3678 && !p.task.spec.hasPoint);
    Parse("gossip 3678 Mtank 0", false);
    Parse("gossip 3678 Mtank 16", false);
    Parse("gossip 0 Mtank 1", false);
    Parse("gossip 3678 1", false);
    p = Parse("gossip 3678 Mtank \"Let the event begin!\"");
    CHECK(!p.task.gossip.byIndex && p.task.gossip.text == "Let the event begin!");
    // the matcher: by index (1-based), and a service line is never picked
    std::vector<TortoiseBots::GossipOrder::Option> menu(2);
    menu[0].text = "Let the event begin!";
    menu[0].sender = 202;
    menu[1].text = "Let me browse your goods.";
    menu[1].icon = 1;             // GOSSIP_OPTION_VENDOR
    TortoiseBots::GossipOrder::Request byIndex;
    byIndex.byIndex = true;
    byIndex.index = 1;
    TortoiseBots::GossipOrder::Match m = TortoiseBots::GossipOrder::MatchOption(menu, byIndex);
    CHECK(m.ok && m.listIndex == 0);
    byIndex.index = 2;
    m = TortoiseBots::GossipOrder::MatchOption(menu, byIndex);
    CHECK(!m.ok && m.code == std::string(TortoiseBots::GossipOrder::kCodeNoOption));
    TortoiseBots::GossipOrder::Request byText;
    byText.text = "begin";
    m = TortoiseBots::GossipOrder::MatchOption(menu, byText);
    CHECK(m.ok && m.listIndex == 0);
}

static void TestGossipAndCommanderP6()
{
    namespace GO = ::TortoiseBots::GossipOrder;
    // Plain gossip option vs service option (vendor = 3) vs coded option:
    std::vector<GO::Option> menu = {
        tk::MakeGossipOption("Open the gate", 0, false),
        tk::MakeGossipOption("Browse your goods.", 3, false),
        tk::MakeGossipOption("Enter secret code", 0, true),
    };
    GO::Request req;
    req.entry = 7607;
    req.bot = "Mtank";
    // Valid plain option by 1-based index:
    req.byIndex = true;
    req.index = 1;
    GO::Match m = GO::MatchOption(menu, req);
    CHECK(m.ok && m.listIndex == 0);
    // Service option refused:
    req.index = 2;
    m = GO::MatchOption(menu, req);
    CHECK(!m.ok && m.code == GO::kCodeNoOption);
    // Coded option refused:
    req.index = 3;
    m = GO::MatchOption(menu, req);
    CHECK(!m.ok && m.code == GO::kCodeNoOption);
    // Out-of-bounds option id refused (never selected):
    req.index = 99;
    m = GO::MatchOption(menu, req);
    CHECK(!m.ok && m.code == GO::kCodeNoOption);
    // By text:
    req.byIndex = false;
    req.text = "open the gate";
    m = GO::MatchOption(menu, req);
    CHECK(m.ok && m.listIndex == 0);

    // Gossip task walk -> BeginAct -> final refusal (invalid option) or timeout:
    tk::TaskRec g = tk::MakeRec(tk::Kind::Gossip, false, false, 0, 12.0f);
    tk::Obs o = Walking(12.0f);
    CHECK(tk::Advance(g, o, RC::kTaskTickMs).what == tk::Do::OrderWalk);
    o.inRange = true;
    o.distance = 2.0f;
    CHECK(tk::Advance(g, o, RC::kTaskTickMs).what == tk::Do::BeginAct);
    tk::NoteActStart(g, false, true, GO::kCodeNoOption, "No option 99.");
    tk::Step s = tk::Advance(g, o, RC::kTaskTickMs);
    CHECK(s.what == tk::Do::Fail && s.code == GO::kCodeNoOption);

    // Already-looted corpse / empty loot -> ERR (never ACK):
    tk::TaskRec emptyLoot = tk::MakeRec(tk::Kind::Loot, true, true, 0, 2.0f);
    tk::Obs lo = Walking(2.0f);
    lo.inRange = true;
    CHECK(tk::Advance(emptyLoot, lo, RC::kTaskTickMs).what == tk::Do::BeginAct);
    tk::NoteActStart(emptyLoot, false, true, RC::kCodeFailed, std::string(tk::LootEmptyText()) + " on this corpse.");
    s = tk::Advance(emptyLoot, lo, RC::kTaskTickMs);
    CHECK(s.what == tk::Do::Fail && s.code == RC::kCodeFailed && s.message.find(tk::LootEmptyText()) != std::string::npos);

    // The core's database options carry sender = menu id and action = order index, so the option's TYPE is what tells a service line:
    // a binder (inn), auctioneer, battlemaster and stablemaster line are all refused, a plain line beside them is picked.
    std::vector<GO::Option> db = {
        tk::MakeGossipOption("Make this inn your home", 5, false),     // GossipOptionNpc::Binder
        tk::MakeGossipOption("I want to browse your goods", 10, false), // Auctioneer
        tk::MakeGossipOption("I wish to fight in the battlegrounds", 9, false),
        tk::MakeGossipOption("I want to stable my pet", 12, false),
        tk::MakeGossipOption("Let the event begin!", 0, false),
    };
    GO::Request svc;
    svc.entry = 3678;
    svc.bot = "Mtank";
    svc.byIndex = true;
    for (std::uint32_t i = 1; i <= 4; ++i)
    {
        svc.index = i;
        GO::Match const sm = GO::MatchOption(db, svc);
        CHECK(!sm.ok && sm.code == GO::kCodeNoOption);
    }
    svc.index = 5;
    GO::Match const okm = GO::MatchOption(db, svc);
    CHECK(okm.ok && okm.listIndex == 4);

    // Verb names:
    CHECK(std::string(tk::VerbName(tk::Verb::Gossip)) == "gossip");
    CHECK(std::string(tk::VerbName(tk::Verb::Use)) == "use");
}

int main()
{
    TestGrammar();
    TestGossip();
    TestMethod();
    TestLock();
    TestDoor();
    TestDoorFailures();
    TestGoober();
    TestLoot();
    TestChestCastBudget();
    TestTargets();
    TestExecutorTexts();
    TestGossip();
    TestGossipAndCommanderP6();
    std::printf("test_run_tasks: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
