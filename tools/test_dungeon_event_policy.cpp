// Standalone check for the run leader's scripted-event steps (`ritual`, `hold`;
// runtime/DungeonRunPolicy.h, ai/playerbot/DungeonPlanPolicy.h), with the
// Uldaman events as the cases: the Altar of the Keepers and Archaedas.
//   g++ -std=c++17 -Wall -Wextra tools/test_dungeon_event_policy.cpp -o /tmp/test_dungeon_event_policy && /tmp/test_dungeon_event_policy
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
using ai::PlanStepRefusal;

static bool Has(const std::string& text, const char* part)
{
    return text.find(part) != std::string::npos;
}

static PlanStep Move(float x)
{
    PlanStep s;
    s.kind = PlanStepKind::Move;
    s.x = x;
    ai::NormalizePlanStep(s);
    return s;
}

// The Altar of the Keepers (gameobject 130511, three participants).
static PlanStep KeepersRitual()
{
    PlanStep s;
    s.kind = PlanStepKind::Ritual;
    s.entry = 130511;
    s.x = 104.8f;
    s.y = 272.5f;
    s.z = -26.5f;
    CHECK(ai::NormalizePlanStep(s) == PlanStepRefusal::None);
    return s;
}

// The Stone Keepers: hold at the origin until instance data 1
// (ULDAMAN_ENCOUNTER_STONE_KEEPERS) reads 3 (DONE), an encounter.
static PlanStep KeepersHold()
{
    PlanStep s;
    s.kind = PlanStepKind::Hold;
    s.entry = 1;
    s.param = 3;
    s.flags = ai::kPlanFlagDoneData | ai::kPlanFlagEncounter;
    s.timeoutS = 600;
    s.entries = {4857};
    CHECK(ai::NormalizePlanStep(s) == PlanStepRefusal::None);
    return s;
}

static Facts Party()
{
    Facts f;
    f.total = 5;
    f.alive = 5;
    f.carrier = "Tanky";
    f.seen = true;
    const char* names[] = {"Tanky", "Rogue", "Heals", "Mage", "Lock"};
    const float yds[] = {0.0f, 12.0f, 5.0f, 3.0f, 30.0f};
    for (int i = 0; i < 5; ++i)
    {
        MemberFact m;
        m.name = names[i];
        m.yd = yds[i];
        f.members.push_back(m);
    }
    return f;
}

static UnitFact Keeper(std::uint64_t guid, bool awake)
{
    UnitFact u;
    u.guid = guid;
    u.entry = 4857;
    u.distLead = 10.0f;
    u.hostile = awake; // a dormant keeper is friendly (faction 35) until the event wakes it
    return u;
}

static void TestNormalizeEventSteps()
{
    PlanStep ritual;
    ritual.kind = PlanStepKind::Ritual;
    CHECK(ai::NormalizePlanStep(ritual) == PlanStepRefusal::MissingEntry);
    ritual.entry = 133234;
    CHECK(ai::NormalizePlanStep(ritual) == PlanStepRefusal::None && ritual.param == ai::kPlanDefaultRitualBots);

    PlanStep hold;
    hold.kind = PlanStepKind::Hold;
    // No done flag and no units: nothing could ever end it.
    CHECK(ai::NormalizePlanStep(hold) == PlanStepRefusal::MissingEntry);
    hold.entries = {4857};
    CHECK(ai::NormalizePlanStep(hold) == PlanStepRefusal::None && hold.radius == ai::kPlanDefaultHoldRadius);
    // Both done conditions, or an encounter flag without the data flag.
    hold.flags = ai::kPlanFlagDoneData | ai::kPlanFlagDoneObject;
    CHECK(ai::NormalizePlanStep(hold) == PlanStepRefusal::BadParam);
    hold.flags = ai::kPlanFlagEncounter;
    CHECK(ai::NormalizePlanStep(hold) == PlanStepRefusal::BadParam);
    // An encounter state above 4 (SPECIAL).
    hold.flags = ai::kPlanFlagDoneData | ai::kPlanFlagEncounter;
    hold.param = 5;
    CHECK(ai::NormalizePlanStep(hold) == PlanStepRefusal::BadParam);
    hold.param = 3;
    CHECK(ai::NormalizePlanStep(hold) == PlanStepRefusal::None);
    // Data index 0 is a real index (Uldaman's Ironaya door); units are optional with a done flag.
    hold.entry = 0;
    hold.entries.clear();
    CHECK(ai::NormalizePlanStep(hold) == PlanStepRefusal::None);
    // Done by object: the object and a state of 0-2.
    hold.flags = ai::kPlanFlagDoneObject;
    hold.param = 1;
    CHECK(ai::NormalizePlanStep(hold) == PlanStepRefusal::MissingEntry);
    hold.entry = 124367;
    hold.param = 3;
    CHECK(ai::NormalizePlanStep(hold) == PlanStepRefusal::BadParam);
    hold.param = 1;
    CHECK(ai::NormalizePlanStep(hold) == PlanStepRefusal::None);

    CHECK(ai::ParsePlanStepKind("hold") == PlanStepKind::Hold);
    CHECK(std::string(ai::PlanStepKindName(PlanStepKind::Hold)) == "hold");
}

static void TestHoldOwnsItsEventUnits()
{
    // Hops before the hold never pull the event's units; the hold's `entry`
    // (here instance data 1) is no creature.
    DungeonPlan plan;
    plan.steps = {Move(-50), KeepersHold()};
    CHECK(ai::OwnedByLaterStep(plan, 0, 4857, 10, 0, 0));
    CHECK(!ai::OwnedByLaterStep(plan, 0, 4857, 40, 0, 0));
    CHECK(!ai::OwnedByLaterStep(plan, 0, 1, 0, 0, 0));
}

static void TestRitualStartsTheEvent()
{
    DungeonPlan plan;
    plan.name = "uldaman";
    plan.steps = {KeepersRitual(), KeepersHold()};
    Run r = Start(&plan);
    Facts f = Party();
    // The lead bot first, then the two nearest (Mage 3 yd, Heals 5 yd).
    CHECK(Tick(r, f) == "ritual entry:130511@104.8,272.5,-26.5 Tanky Mage Heals");
    CHECK(r.phase == Phase::AwaitTask && Has(r.note, "starting the event"));
    // The bots walk and use it: nothing new goes out.
    f.carrierBusy = true;
    CHECK(Tick(r, f).empty());
    CHECK(Tick(r, f).empty());
    // One answer for the whole order: ACK, the hold comes next.
    f.carrierBusy = false;
    f.taskReply = 1;
    CHECK(Tick(r, f).empty());
    CHECK(r.step == 1 && r.state == State::Running && Has(r.note, "the event started"));
}

static void TestRitualFailuresPause()
{
    DungeonPlan plan;
    plan.steps = {KeepersRitual()};
    // An ERR from the module pauses with its words.
    Run r = Start(&plan);
    Facts f = Party();
    CHECK(!Tick(r, f).empty());
    f.taskReply = 2;
    f.taskText = "busy: That ritual is already in progress.";
    Tick(r, f);
    CHECK(r.state == State::Paused && Has(r.note, "ritual 130511 failed") && Has(r.note, "already in progress"));

    // Fewer living bots than the ritual needs: pause before any order.
    r = Start(&plan);
    f = Party();
    f.members.resize(2);
    CHECK(Tick(r, f).empty());
    CHECK(r.state == State::Paused && Has(r.note, "needs 3 living bots"));

    // The bots are free again but no answer came: pause, never a blind wait.
    r = Start(&plan);
    f = Party();
    CHECK(!Tick(r, f).empty());
    int ticks = 0;
    while (r.state == State::Running && ticks < 20)
    {
        Tick(r, f);
        ++ticks;
    }
    CHECK(r.state == State::Paused && Has(r.note, "no answer") && ticks <= 6);
}

static void TestKeepersHoldFightsTheWavesToTheEnd()
{
    DungeonPlan plan;
    plan.name = "uldaman";
    plan.steps = {KeepersRitual(), Move(0), KeepersHold()};
    Run r = Start(&plan, 2);
    Facts f = Party();
    f.eventData = 1; // IN_PROGRESS: the ritual woke the first keeper
    f.units = {Keeper(1, false), Keeper(2, false), Keeper(3, false), Keeper(4, false)};
    CHECK(Tick(r, f).empty());
    CHECK(r.phase == Phase::Hold && Has(r.note, "holding the spot (0 of 4 down)"));
    CHECK(Has(StatusLine(r), "|hold|hold 0/4 down|"));
    // A keeper wakes and attacks: the bots fight, the run sends nothing, and a
    // fight of the event may last longer than an ordinary one (180 s).
    f.units[0].hostile = true;
    f.anyInCombat = true;
    for (int i = 0; i < 250; ++i)
        CHECK(Tick(r, f).empty());
    CHECK(r.state == State::Running && Has(r.note, "event: fighting (0 of 4 down, 1 awake)"));
    CHECK(Has(StatusLine(r), "|fight 0/4 down|"));
    // One keeper down, the next one awake: the wave count moves on.
    f.units = {Keeper(2, true), Keeper(3, false), Keeper(4, false)};
    f.eventDown = 1;
    Tick(r, f);
    CHECK(Has(StatusLine(r), "|fight 1/4 down|"));
    // A fight that chased the lead bot off the spot: back to it once it is over.
    f.anyInCombat = false;
    f.units = {Keeper(3, false), Keeper(4, false)};
    f.eventDown = 2;
    f.leadX = 20;
    std::string order;
    for (int i = 0; i < 3 && order.empty(); ++i)
        order = Tick(r, f);
    CHECK(order == "move 0.0 0.0 0.0");
    f.leadX = 1;
    Tick(r, f);
    CHECK(r.phase == Phase::Hold);
    // All four down: the instance says DONE (the temple door opens).
    f.units.clear();
    f.eventDown = 4;
    f.eventData = 3;
    Tick(r, f);
    CHECK(r.state == State::Done && Has(r.note, "plan done"));
}

static void TestEncounterFailGoesBackToTheStart()
{
    DungeonPlan plan;
    plan.steps = {KeepersRitual(), Move(0), KeepersHold()};
    Run r = Start(&plan, 2);
    Facts f = Party();
    f.eventData = 1;
    f.units = {Keeper(1, true)};
    Tick(r, f);
    // The party wiped, the keeper evaded: the script set FAIL and stoned them all.
    f.eventData = 2;
    f.units = {Keeper(1, false)};
    Tick(r, f);
    CHECK(r.state == State::Paused && r.step == 0);
    CHECK(Has(r.note, "event failed") && Has(r.note, "reads 2, fail") && Has(r.note, "step 1 (ritual)"));
    // Resume regroups, then the ritual goes out again.
    CHECK(Resume(r));
    CHECK(r.regroup && r.step == 0);

    // Archaedas reset and walked home: FAIL, then NOT_STARTED; a tick may see only the 0.
    r = Start(&plan, 2);
    f = Party();
    f.eventData = 1;
    Tick(r, f);
    f.eventData = 0;
    Tick(r, f);
    CHECK(r.state == State::Paused && r.step == 0 && Has(r.note, "back to 0"));

    // The ritual answered, but the event never started: bounded, then the same.
    r = Start(&plan, 2);
    f = Party();
    f.eventData = 0;
    int ticks = 0;
    while (r.state == State::Running && ticks < 100)
    {
        Tick(r, f);
        ++ticks;
    }
    CHECK(r.state == State::Paused && r.step == 0 && Has(r.note, "did not leave 0"));
    CHECK(ticks >= 30 && ticks <= 32);

    // No instance data on the map: a clear pause, not a wait.
    r = Start(&plan, 2);
    f = Party();
    f.eventData = -1;
    Tick(r, f);
    CHECK(r.state == State::Paused && Has(r.note, "no instance data"));
}

static void TestHoldByUnitsAndItsCeiling()
{
    // A hold without a done flag ends when every unit of the event is dead,
    // after one was seen awake; an awake one that does not come is pulled.
    PlanStep hold;
    hold.kind = PlanStepKind::Hold;
    hold.entries = {4857};
    hold.timeoutS = 60;
    CHECK(ai::NormalizePlanStep(hold) == PlanStepRefusal::None);
    DungeonPlan plan;
    plan.steps = {hold};
    Run r = Start(&plan);
    Facts f = Party();
    f.units = {Keeper(7, true)};
    std::string order;
    int ticks = 0;
    while (order.empty() && ticks < 20)
    {
        order = Tick(r, f);
        ++ticks;
    }
    CHECK(order == "pullback 7" && ticks >= 10 && ticks <= 12);
    f.units.clear();
    f.eventDown = 1;
    for (int i = 0; i < 30 && r.state == State::Running; ++i)
        Tick(r, f);
    CHECK(r.state == State::Done);

    // Dormant units only and nothing happens: the event's own ceiling pauses it.
    r = Start(&plan);
    f = Party();
    f.units = {Keeper(7, false)};
    ticks = 0;
    while (r.state == State::Running && ticks < 200)
    {
        Tick(r, f);
        ++ticks;
    }
    CHECK(r.state == State::Paused && Has(r.note, "event not done after 60 s") && ticks <= 62);
}

static void TestHoldDoneByObject()
{
    PlanStep hold;
    hold.kind = PlanStepKind::Hold;
    hold.entry = 124367; // the Keepers' temple door
    hold.param = 1;      // open
    hold.flags = ai::kPlanFlagDoneObject;
    hold.timeoutS = 600;
    CHECK(ai::NormalizePlanStep(hold) == PlanStepRefusal::None);
    DungeonPlan plan;
    plan.steps = {hold};
    Run r = Start(&plan);
    Facts f = Party();
    f.objectState = 0;
    Tick(r, f);
    CHECK(r.state == State::Running && r.phase == Phase::Hold);
    f.objectState = 1;
    Tick(r, f);
    CHECK(r.state == State::Done);
}

static void TestFarHoldIsApproached()
{
    DungeonPlan plan;
    plan.steps = {KeepersHold()};
    Run r = Start(&plan);
    Facts f = Party();
    f.seen = false;
    f.approachKnown = true;
    f.approachX = 40;
    CHECK(Tick(r, f) == "move 40.0 0.0 0.0");
    // The event's clock starts only at the spot.
    CHECK(!r.eventOn && r.eventMs == 0);
}

static void TestFightAtTheRitualIsTheHolds()
{
    // The first Stone Keeper wakes with the ritual's cast: combat starts in the
    // same update as the ACK. That fight is the event's, bounded by the hold's
    // timeout, not the 180 s of an ordinary fight.
    DungeonPlan plan;
    plan.steps = {KeepersRitual(), KeepersHold()};
    Run r = Start(&plan);
    Facts f = Party();
    CHECK(!Tick(r, f).empty());
    f.taskReply = 1;
    f.anyInCombat = true;
    for (int i = 0; i < 250; ++i)
        CHECK(Tick(r, f).empty());
    CHECK(r.state == State::Running && r.step == 1 && r.eventOn);

    // The fight begins before the answer, and a move stands between: still the hold's.
    plan.steps = {KeepersRitual(), Move(0), KeepersHold()};
    r = Start(&plan);
    f = Party();
    CHECK(!Tick(r, f).empty());
    f.carrierBusy = true;
    f.anyInCombat = true;
    for (int i = 0; i < 250; ++i)
        Tick(r, f);
    CHECK(r.state == State::Running && r.step == 0);
    // Past the hold's own ceiling it does pause.
    for (int i = 0; i < 400 && r.state == State::Running; ++i)
        Tick(r, f);
    CHECK(r.state == State::Paused && Has(r.note, "more than 600 s"));
}

static void TestInterruptedReplyWhileEventRunsStillReachesTheHold()
{
    // Uldaman run 4 (19z13): the ritual answered "interrupted" as the altar reset
    // while the keepers woke. The event is in progress (data 1) and fighting: the
    // step moves on to the hold anyway.
    DungeonPlan plan;
    plan.steps = {KeepersRitual(), KeepersHold()};
    Run r = Start(&plan);
    Facts f = Party();
    CHECK(!Tick(r, f).empty());
    f.taskReply = 2;
    f.taskText = "interrupted: The ritual was cancelled (a participant stopped channeling)";
    f.anyInCombat = true;
    f.eventData = 1;
    Tick(r, f);
    CHECK(r.state == State::Running && r.step == 1 && r.eventOn);

    // A real failure in a fight with the event not running stays on the ritual (it pauses).
    r = Start(&plan);
    f = Party();
    CHECK(!Tick(r, f).empty());
    f.taskReply = 2;
    f.taskText = "in_combat: A bot is in combat and stopped.";
    f.anyInCombat = true;
    f.eventData = 0;
    Tick(r, f);
    CHECK(r.step == 0);

    // Another error text while the event reads in progress is no recovery.
    r = Start(&plan);
    f = Party();
    CHECK(!Tick(r, f).empty());
    f.taskReply = 2;
    f.taskText = "unavailable: A bot died, left the map or the party, or is teleporting.";
    f.anyInCombat = true;
    f.eventData = 1;
    Tick(r, f);
    CHECK(r.step == 0);

    // The ritual went out while the data already read in progress (the reset wait ran out): not this order's event.
    r = Start(&plan);
    f = Party();
    f.eventData = 1;
    for (int i = 0; i < 200 && r.state == State::Running && !r.orderSent; ++i)
        Tick(r, f);
    CHECK(r.orderSent && r.dataAtOrder == 1);
    f.taskReply = 2;
    f.taskText = "interrupted: The ritual was cancelled (a participant stopped channeling)";
    f.anyInCombat = true;
    Tick(r, f);
    CHECK(r.step == 0);
}

static void TestRetryWaitsForTheEncounterToReset()
{
    DungeonPlan plan;
    plan.steps = {KeepersRitual(), KeepersHold()};
    // Archaedas after a reset: FAIL while he walks home, then NOT_STARTED.
    Run r = Start(&plan);
    Facts f = Party();
    f.eventData = 2;
    for (int i = 0; i < 10; ++i)
        CHECK(Tick(r, f).empty());
    CHECK(r.state == State::Running && Has(r.note, "waiting for the encounter to reset"));
    f.eventData = 0;
    CHECK(Has(Tick(r, f), "ritual entry:130511"));

    // The Stone Keepers stay at FAIL for good: the wait is bounded, then the ritual goes out.
    r = Start(&plan);
    f = Party();
    f.eventData = 2;
    std::string order;
    int ticks = 0;
    while (order.empty() && ticks < 100)
    {
        order = Tick(r, f);
        ++ticks;
    }
    CHECK(Has(order, "ritual entry:130511") && ticks >= 60 && ticks <= 62);

    // The event is done already: no ritual.
    r = Start(&plan);
    f = Party();
    f.eventData = 3;
    CHECK(Tick(r, f).empty());
    CHECK(r.step == 1 && Has(r.note, "done already"));
}

static void TestSpentAltarAfterAFailure()
{
    DungeonPlan plan;
    plan.steps = {KeepersRitual(), KeepersHold()};
    std::string const order = "ritual entry:130511@104.8,272.5,-26.5 Tanky Mage Heals";
    std::string const busy = "busy: That ritual is already in progress.";
    // First try: the module's words.
    Run r = Start(&plan);
    OrderRefused(r, order, busy);
    CHECK(r.state == State::Paused && Has(r.note, "refused: busy") && !Has(r.note, "spent"));
    // The retry after a failed event: the altar is spent.
    r = Start(&plan, 1);
    Facts f = Party();
    f.eventData = 2;
    Tick(r, f);
    CHECK(r.state == State::Paused && r.step == 0 && r.eventRetry);
    CHECK(Resume(r));
    OrderRefused(r, order, busy);
    CHECK(r.state == State::Paused && Has(r.note, "spent") && Has(r.note, "instance reset"));
    // The same answer as the ritual's final ERR.
    CHECK(Resume(r));
    r.regroup = false;
    f = Party();
    CHECK(!Tick(r, f).empty());
    f.taskReply = 2;
    f.taskText = busy;
    Tick(r, f);
    CHECK(r.state == State::Paused && Has(r.note, "spent"));
}

// Uldaman run 3 (19z12, tests/uldaman-events-3), Keepers attempt 1: the tank
// held "stay" on the hold spot at 102 270; a staying bot's attack actions are
// not useful, so it never fought, and a Stone Keeper killed the priest at
// 119 266, 17 yd away. In the hold the tank is now sent at the event's awake
// unit, and a mob hitting the healer within the hold's radius is picked up at
// once; out of the fight the tank goes back on the spot.
static void TestTankGuardsTheHold()
{
    DungeonPlan plan;
    plan.name = "uldaman";
    plan.steps = {KeepersRitual(), KeepersHold()};
    Run r = Start(&plan);
    Facts f = Party();
    f.leadIsTank = true;
    f.leadStaying = true;
    f.eventData = 0;
    CHECK(!Tick(r, f).empty()); // the ritual
    // The first keeper wakes with the cast, the fight starts with the ACK.
    f.eventData = 1;
    f.taskReply = 1;
    f.anyInCombat = true;
    UnitFact awake = Keeper(1, true);
    awake.engaged = true;
    awake.distLead = 6.0f;
    f.units = {awake, Keeper(2, false), Keeper(3, false), Keeper(4, false)};
    CHECK(Tick(r, f) == "attack 1 Tanky"); // the tank takes the awake keeper where it stands
    CHECK(r.step == 1 && r.holdUnstayed && Has(r.note, "the tank takes 4857"));
    // The order ended its stay: its own AI fights now, nothing more goes out.
    f.leadStaying = false;
    for (int i = 0; i < 5; ++i)
        CHECK(Tick(r, f).empty());

    // Review of #1072: the tank stays off its stay across a death (the order
    // count starts over): the next keeper is still taken, and one on the
    // healer is still picked up.
    {
        Run r2 = r;
        Facts g = f;
        g.eventDown = 1;
        UnitFact next = Keeper(5, true);
        next.engaged = true;
        next.distLead = 8.0f;
        g.units = {next, Keeper(3, false), Keeper(4, false)};
        std::string o;
        for (int i = 0; i < 3 && o.empty(); ++i)
            o = Tick(r2, g);
        CHECK(o == "attack 5 Tanky");
        for (int i = 0; i < 4; ++i)
            CHECK(Tick(r2, g).empty()); // that one it fights by itself now
        g.eventDown = 2;
        UnitFact third = Keeper(6, true);
        third.engaged = true;
        third.distLead = 17.0f;
        g.units = {third, Keeper(4, false)};
        UnitFact hits = third;
        hits.hitting = true;
        hits.victimHealer = true;
        hits.distSpot = 17.0f;
        g.partyAttackers = {hits};
        o.clear();
        for (int i = 0; i < 3 && o.empty(); ++i)
            o = Tick(r2, g);
        CHECK(o == "attack 6 Tanky" && Has(r2.note, "off the healer"));
    }

    // The replay: the next keeper on the priest 17 yd away inside the hold, the
    // tank back on stay with nothing on it (as live: it never left the spot).
    f.leadStaying = true;
    f.eventDown = 1;
    UnitFact second = Keeper(2, true);
    second.engaged = true;
    second.distLead = 17.0f;
    f.units = {second, Keeper(3, false), Keeper(4, false)};
    UnitFact onHealer = second;
    onHealer.hitting = true;
    onHealer.victimHealer = true;
    onHealer.distSpot = 17.0f;
    f.partyAttackers = {onHealer};
    std::string order;
    for (int i = 0; i < 3 && order.empty(); ++i)
        order = Tick(r, f);
    CHECK(order == "attack 2 Tanky"); // at once (a healer), within kGuardRepeatMs of the last order
    CHECK(Has(r.note, "the tank picks up 4857 off the healer (17.0 yd)"));

    // The fight is over with the tank 2 yd off the spot: back on it (on stay).
    f.anyInCombat = false;
    f.partyAttackers.clear();
    f.units = {Keeper(3, false), Keeper(4, false)};
    f.eventDown = 2;
    f.leadStaying = false;
    f.leadX = 2.0f;
    CHECK(Tick(r, f) == "move 0.0 0.0 0.0");
    CHECK(!r.holdUnstayed && Has(r.note, "back on the hold spot"));
    f.leadStaying = true;
    CHECK(Tick(r, f).empty());
    CHECK(r.phase == Phase::Hold && Has(r.note, "holding the spot"));
}

// The hold's guard stays inside the hold: never a mob outside its radius of
// the spot, a dormant statue, a tank outside the hold area, or before the
// hold (the ritual); the order caps start over with each unit that dies.
static void TestHoldGuardLimits()
{
    auto holdRun = [](DungeonPlan& plan)
    {
        plan.steps = {KeepersRitual(), KeepersHold()};
        Run r = Start(&plan);
        Facts f = Party();
        f.eventData = 0;
        Tick(r, f); // the ritual
        f.eventData = 1;
        f.taskReply = 1;
        f.anyInCombat = true;
        Tick(r, f); // ACK: the hold's fight
        CHECK(r.step == 1 && r.eventOn);
        return r;
    };
    auto fight = []()
    {
        Facts f = Party();
        f.eventData = 1;
        f.anyInCombat = true;
        f.leadIsTank = true;
        f.leadStaying = true;
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
    UnitFact far = Keeper(7, true);
    far.distLead = 20.0f;
    far.hitting = true;
    far.victimHealer = true;
    {
        // Outside the hold's radius of the spot (30): left alone.
        DungeonPlan plan;
        Run r = holdRun(plan);
        Facts f = fight();
        UnitFact u = far;
        u.distSpot = 31.0f;
        f.partyAttackers = {u};
        CHECK(ordersIn(r, f, 8) == 0);
        u.distSpot = -1.0f; // not read: left alone too
        f.partyAttackers = {u};
        CHECK(ordersIn(r, f, 8) == 0);
        u.distSpot = 25.0f;
        f.partyAttackers = {u};
        CHECK(ordersIn(r, f, 1) == 1);
    }
    {
        // Dormant statues (friendly) and an awake keeper not yet in the fight: no engage.
        DungeonPlan plan;
        Run r = holdRun(plan);
        Facts f = fight();
        UnitFact idle = Keeper(2, true);
        f.units = {Keeper(1, false), idle};
        CHECK(ordersIn(r, f, 8) == 0);
    }
    {
        // The tank outside the hold area: nothing.
        DungeonPlan plan;
        Run r = holdRun(plan);
        Facts f = fight();
        f.leadX = 31.0f;
        UnitFact u = far;
        u.distSpot = 10.0f;
        f.partyAttackers = {u};
        UnitFact awake = Keeper(1, true);
        awake.engaged = true;
        f.units = {awake};
        CHECK(ordersIn(r, f, 8) == 0);
    }
    {
        // Before the hold (the ritual's bots still busy): nothing.
        DungeonPlan plan;
        plan.steps = {KeepersRitual(), KeepersHold()};
        Run r = Start(&plan);
        Facts f = fight();
        f.anyInCombat = false;
        f.eventData = 0;
        Tick(r, f);
        f = fight();
        f.carrierBusy = true;
        UnitFact awake = Keeper(1, true);
        awake.engaged = true;
        f.units = {awake};
        CHECK(ordersIn(r, f, 8) == 0 && r.step == 0);
    }
    {
        // The caps: kGuardOrdersPerFight per unit of the event, then again
        // once one dies (a refused attack keeps the tank on stay).
        DungeonPlan plan;
        Run r = holdRun(plan);
        Facts f = fight();
        UnitFact awake = Keeper(1, true);
        awake.engaged = true;
        f.units = {awake};
        CHECK(ordersIn(r, f, 20) == static_cast<int>(kGuardOrdersPerFight));
        OrderRefused(r, "attack 1 Tanky", "failed: That bot did not accept the attack.");
        CHECK(r.state == State::Running);
        f.eventDown = 1;
        awake.guid = 2;
        f.units = {awake};
        CHECK(ordersIn(r, f, 2) == 1);
    }
}

// Review of #1072, round 2: off its stay the tank was sent at any awake unit
// but the last one it was sent at, so with two in the fight (Archaedas and a
// statue waking every 10 s) it went A, B, A every 2 s, 3 times per death,
// losing threat with each switch. Now it keeps what it holds: never a unit
// already on it, and while it holds one only a unit hitting the healer.
static void TestHoldEngageKeepsItsTarget()
{
    auto holdRun = [](DungeonPlan& plan)
    {
        plan.steps = {KeepersRitual(), KeepersHold()};
        Run r = Start(&plan);
        Facts f = Party();
        f.eventData = 0;
        Tick(r, f); // the ritual
        f.eventData = 1;
        f.taskReply = 1;
        f.anyInCombat = true;
        Tick(r, f); // ACK: the hold's fight
        CHECK(r.step == 1 && r.eventOn);
        return r;
    };
    auto fight = []()
    {
        Facts f = Party();
        f.eventData = 1;
        f.anyInCombat = true;
        f.leadIsTank = true;
        f.leadStaying = true;
        return f;
    };
    auto unit = [](std::uint64_t guid, float yd, bool onTank, bool onHealer)
    {
        UnitFact u = Keeper(guid, true);
        u.engaged = true;
        u.distLead = yd;
        u.victimLead = onTank;
        u.victimHealer = onHealer;
        return u;
    };
    auto orders = [](Run& r, const Facts& f, int ticks, std::vector<std::string>& sent)
    {
        for (int i = 0; i < ticks; ++i)
        {
            std::string const o = Tick(r, f);
            if (o.compare(0, 7, "attack ") == 0)
                sent.push_back(o);
        }
    };
    {
        // One on the tank (nearer), one on the mage: one order, for the mage's.
        DungeonPlan plan;
        Run r = holdRun(plan);
        Facts f = fight();
        f.units = {unit(1, 3.0f, true, false), unit(2, 12.0f, false, false)};
        std::vector<std::string> sent;
        orders(r, f, 1, sent);
        CHECK(sent.size() == 1 && sent[0] == "attack 2 Tanky");
        // Off its stay, the mage's attacker not yet turned: no flip-flop back.
        f.leadStaying = false;
        orders(r, f, 20, sent);
        CHECK(sent.size() == 1);
        // Both on the tank now: still nothing.
        f.units = {unit(1, 3.0f, true, false), unit(2, 4.0f, true, false)};
        orders(r, f, 20, sent);
        CHECK(sent.size() == 1);
        // A death resets the count; the next one wakes onto the mage while
        // the tank holds the other: it keeps that one.
        f.eventDown = 1;
        f.units = {unit(2, 4.0f, true, false), unit(3, 14.0f, false, false)};
        orders(r, f, 20, sent);
        CHECK(sent.size() == 1);
        // That one turns on the healer: taken, once, and not back again.
        f.units = {unit(2, 4.0f, true, false), unit(3, 14.0f, false, true)};
        orders(r, f, 20, sent);
        CHECK(sent.size() == 2 && sent[1] == "attack 3 Tanky");
        // The healer's attacker first, on stay too, over a nearer one on the mage.
        Run r2 = holdRun(plan);
        Facts g = fight();
        g.units = {unit(4, 3.0f, false, false), unit(5, 15.0f, false, true)};
        std::vector<std::string> sent2;
        orders(r2, g, 1, sent2);
        CHECK(sent2.size() == 1 && sent2[0] == "attack 5 Tanky");
    }
    {
        // Two units both on the tank off its stay: no orders at all, also
        // across a death.
        DungeonPlan plan;
        Run r = holdRun(plan);
        r.holdUnstayed = true; // an earlier hold order took it off its stay
        Facts f = fight();
        f.leadStaying = false;
        f.units = {unit(1, 3.0f, true, false), unit(2, 5.0f, true, false)};
        std::vector<std::string> sent;
        orders(r, f, 20, sent);
        f.eventDown = 1;
        orders(r, f, 20, sent);
        CHECK(sent.empty());
        // On stay the tank does not swing at all: one order for the nearest,
        // then its own AI fights both.
        Run r2 = holdRun(plan);
        Facts g = fight();
        g.units = f.units;
        orders(r2, g, 1, sent);
        g.leadStaying = false;
        orders(r2, g, 20, sent);
        CHECK(sent.size() == 1 && sent[0] == "attack 1 Tanky");
    }
}

int main()
{
    TestNormalizeEventSteps();
    TestHoldOwnsItsEventUnits();
    TestRitualStartsTheEvent();
    TestRitualFailuresPause();
    TestKeepersHoldFightsTheWavesToTheEnd();
    TestEncounterFailGoesBackToTheStart();
    TestHoldByUnitsAndItsCeiling();
    TestHoldDoneByObject();
    TestFarHoldIsApproached();
    TestFightAtTheRitualIsTheHolds();
    TestInterruptedReplyWhileEventRunsStillReachesTheHold();
    TestRetryWaitsForTheEncounterToReset();
    TestSpentAltarAfterAFailure();
    TestTankGuardsTheHold();
    TestHoldGuardLimits();
    TestHoldEngageKeepsItsTarget();
    std::cout << "test_dungeon_event_policy: ok\n";
    return 0;
}
