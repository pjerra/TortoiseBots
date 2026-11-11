// Standalone check for ai/playerbot/SeekParkPolicy.h:
//   g++ -std=c++17 -Wall -Wextra tools/test_seek_park_policy.cpp -o /tmp/test_seek_park_policy
//   /tmp/test_seek_park_policy
#include "../ai/playerbot/SeekParkPolicy.h"

#include <cstdlib>
#include <iostream>
#include <vector>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using namespace ai;

namespace
{
SeekParkRules ParkRules()
{
    SeekParkRules rules;
    rules.on = true;
    rules.releaseLeadMs = 40000;
    rules.applyBudgetMs = 6000;
    rules.maxReapplies = 2;
    return rules;
}

SeekParkInput ParkInput(std::uint32_t dueInMs)
{
    SeekParkInput in;
    in.son = 77;
    in.pickedSon = 77;
    in.cycleNo = 3;
    in.fightStartMs = 100;
    in.sonAlive = true;
    in.sonInCombat = true;
    in.sonIsPick = true;
    in.sonInCircle = true;
    in.parkBotOk = true;
    in.dueKnown = true;
    in.dueInMs = dueInMs;
    return in;
}
}

// TestParkHeldUntilReleaseLead: the named failing case. Apply starts while the source is due more than the release
// lead plus kSeekParkMinHoldMs away, and the release happens exactly at the lead, never a millisecond early.
static void TestParkHeldUntilReleaseLead()
{
    const SeekParkRules rules = ParkRules();
    SeekParkState state;
    SeekParkInput in = ParkInput(60000);
    CHECK(SeekParkStep(state, rules, in, 1000));
    CHECK(state.phase == SeekParkPhase::Apply);
    CHECK(state.applySinceMs == 1000);
    in.controlled = true;
    in.controlLeftMs = 41000;
    CHECK(SeekParkStep(state, rules, in, 2500));
    CHECK(state.phase == SeekParkPhase::Held);
    CHECK(state.heldSinceMs == 2500);
    in.dueInMs = 40001;
    CHECK(!SeekParkStep(state, rules, in, 3000));
    CHECK(state.phase == SeekParkPhase::Held);
    in.dueInMs = 40000;
    CHECK(SeekParkStep(state, rules, in, 4000));
    CHECK(state.phase == SeekParkPhase::Release);
    CHECK(state.releaseMs == 4000);
    CHECK(SeekParkHeldMs(state, 4000) == 1500);
    CHECK(SeekParkLateLeadMs(SeekParkPhase::Held, true, true, 75000, 40000) == 40000);
    CHECK(SeekParkLateLeadMs(SeekParkPhase::Failed, true, true, 75000, 40000) == 75000);
    CHECK(SeekParkLateLeadMs(SeekParkPhase::None, true, true, 75000, 40000) == 75000);
    CHECK(SeekParkLateLeadMs(SeekParkPhase::Release, true, true, 75000, 40000) == 40000);
    CHECK(SeekParkLateLeadMs(SeekParkPhase::Apply, true, true, 75000, 40000) == 40000);
    // Only the parked Son gets the release lead: another read's pick keeps the seek_pull lead.
    CHECK(SeekParkLateLeadMs(SeekParkPhase::Held, true, false, 75000, 40000) == 75000);
    // Key off: today's lead whatever the phase says.
    CHECK(SeekParkLateLeadMs(SeekParkPhase::Held, false, true, 75000, 40000) == 75000);
}

// TestParkNeedsRoom: a source that joined with only the lead plus a hair left does not park at all, and one nobody
// can control does not either; a source out of combat is no park yet.
static void TestParkNeedsRoom()
{
    const SeekParkRules rules = ParkRules();
    {
        SeekParkState state;
        SeekParkInput in = ParkInput(48000);
        CHECK(SeekParkStep(state, rules, in, 1000));
        CHECK(state.phase == SeekParkPhase::Failed);
        CHECK(std::strcmp(state.why, "late") == 0);
    }
    {
        SeekParkState state;
        SeekParkInput in = ParkInput(48001);
        CHECK(SeekParkStep(state, rules, in, 1000));
        CHECK(state.phase == SeekParkPhase::Apply);
    }
    {
        SeekParkState state;
        SeekParkInput in = ParkInput(60000);
        in.parkBotOk = false;
        CHECK(SeekParkStep(state, rules, in, 1000));
        CHECK(state.phase == SeekParkPhase::Failed);
        CHECK(std::strcmp(state.why, "no_bot") == 0);
    }
    {
        SeekParkState state;
        SeekParkInput in = ParkInput(60000);
        in.sonInCombat = false;
        CHECK(!SeekParkStep(state, rules, in, 1000));
        CHECK(state.phase == SeekParkPhase::None);
    }
    {
        // Not the seek's pick: the burn, kill and gather are on another Son, so this one is today's take.
        SeekParkState state;
        SeekParkInput in = ParkInput(60000);
        in.sonIsPick = false;
        CHECK(!SeekParkStep(state, rules, in, 1000));
        CHECK(state.phase == SeekParkPhase::None);
    }
    {
        // The pick outside the raid circle: the take would refuse the chase, so it is not parked.
        SeekParkState state;
        SeekParkInput in = ParkInput(60000);
        in.sonInCircle = false;
        CHECK(!SeekParkStep(state, rules, in, 1000));
        CHECK(state.phase == SeekParkPhase::None);
    }
    {
        // No due time yet: the park waits, it does not fail.
        SeekParkState state;
        SeekParkInput in = ParkInput(0);
        in.dueKnown = false;
        CHECK(!SeekParkStep(state, rules, in, 1000));
        CHECK(state.phase == SeekParkPhase::None);
    }
    {
        // Key off: the record stays None and nothing happens.
        SeekParkRules off;
        SeekParkState state;
        CHECK(!SeekParkStep(state, off, ParkInput(60000), 1000));
        CHECK(state.phase == SeekParkPhase::None);
    }
}

// TestParkApplyBudget: the control must be on the source within the row's window after the park bot first stands in
// cast reach; the walk to the source is not the control's time.
static void TestParkApplyBudget()
{
    const SeekParkRules rules = ParkRules();
    SeekParkState state;
    CHECK(SeekParkStep(state, rules, ParkInput(60000), 1000));
    CHECK(state.phase == SeekParkPhase::Apply);
    CHECK(state.budgetSinceMs == 0);
    // Still walking to the source: no budget runs, however long the walk.
    CHECK(!SeekParkStep(state, rules, ParkInput(60000), 7001));
    CHECK(state.phase == SeekParkPhase::Apply);
    CHECK(state.budgetSinceMs == 0);
    // In reach at 8000: the budget starts there.
    SeekParkInput in = ParkInput(60000);
    in.controllerInReach = true;
    CHECK(!SeekParkStep(state, rules, in, 8000));
    CHECK(state.budgetSinceMs == 8000);
    CHECK(!SeekParkStep(state, rules, ParkInput(60000), 14000));
    CHECK(state.phase == SeekParkPhase::Apply);
    CHECK(SeekParkStep(state, rules, ParkInput(60000), 14001));
    CHECK(state.phase == SeekParkPhase::Failed);
    CHECK(std::strcmp(state.why, "budget") == 0);
}

// TestParkReapplyCap: a broken control is re-applied up to the row's priority and the next break fails the park.
static void TestParkReapplyCap()
{
    const SeekParkRules rules = ParkRules();
    SeekParkState state;
    SeekParkInput in = ParkInput(60000);
    CHECK(SeekParkStep(state, rules, in, 1000));
    in.controlled = true;
    in.controlLeftMs = 40000;
    CHECK(SeekParkStep(state, rules, in, 2000));
    CHECK(state.phase == SeekParkPhase::Held);
    in.controlled = false;
    in.controlLeftMs = 0;
    CHECK(SeekParkStep(state, rules, in, 3000));
    CHECK(state.phase == SeekParkPhase::Apply);
    CHECK(state.reapplies == 1);
    CHECK(state.applySinceMs == 3000);
    in.controlled = true;
    in.controlLeftMs = 40000;
    CHECK(SeekParkStep(state, rules, in, 4000));
    CHECK(state.phase == SeekParkPhase::Held);
    in.controlled = false;
    CHECK(SeekParkStep(state, rules, in, 5000));
    CHECK(state.phase == SeekParkPhase::Apply);
    CHECK(state.reapplies == 2);
    in.controlled = true;
    CHECK(SeekParkStep(state, rules, in, 6000));
    CHECK(state.phase == SeekParkPhase::Held);
    in.controlled = false;
    CHECK(SeekParkStep(state, rules, in, 7000));
    CHECK(state.phase == SeekParkPhase::Failed);
    CHECK(std::strcmp(state.why, "breaks") == 0);
    // The Held time is kept across the breaks it survived.
    CHECK(state.heldMs == 3000);
}

// TestParkVetoesTankTasks: Apply and Held veto every take path; Release on a tank all but its own; Release on a
// non-tank nothing (the control broke onto someone, so the normal takes apply); nothing else vetoes.
static void TestParkVetoesTankTasks()
{
    const char* const sources[] = {"meet", "receiver", "pickup", "pull_join", "nearest", "nearest_reach",
        "healer_clock", "victim_relief", "other", nullptr};
    for (const char* src : sources)
    {
        CHECK(SeekParkVetoesTankTask(SeekParkPhase::Apply, src, false));
        CHECK(SeekParkVetoesTankTask(SeekParkPhase::Apply, src, true));
        CHECK(SeekParkVetoesTankTask(SeekParkPhase::Held, src, false));
        CHECK(SeekParkVetoesTankTask(SeekParkPhase::Held, src, true));
        CHECK(!SeekParkVetoesTankTask(SeekParkPhase::None, src, true));
        CHECK(!SeekParkVetoesTankTask(SeekParkPhase::Failed, src, true));
        // A released source on a non-tank: the ordinary takes may take it back.
        CHECK(!SeekParkVetoesTankTask(SeekParkPhase::Release, src, false));
        CHECK(src && std::strcmp(src, "park_release") == 0 ? !SeekParkVetoesTankTask(SeekParkPhase::Release, src, true)
                                                           : SeekParkVetoesTankTask(SeekParkPhase::Release, src, true));
    }
    CHECK(!SeekParkVetoesTankTask(SeekParkPhase::Release, "park_release", true));
}

// TestParkExcludesAndTarget: the body-skip rule and the release target exemption (decisions 3, 4 and 5).
static void TestParkExcludesAndTarget()
{
    // Apply and Held: the body skips the parked source for every tank.
    CHECK(SeekParkExcludesSource(SeekParkPhase::Apply, false, false));
    CHECK(SeekParkExcludesSource(SeekParkPhase::Apply, true, true));
    CHECK(SeekParkExcludesSource(SeekParkPhase::Held, true, false));
    // Release on a tank: every bot but the receiver skips it (the receiver's drag is the release's own).
    CHECK(SeekParkExcludesSource(SeekParkPhase::Release, true, false));
    CHECK(!SeekParkExcludesSource(SeekParkPhase::Release, true, true));
    // Release on a non-tank: nobody skips it, the ordinary takes apply.
    CHECK(!SeekParkExcludesSource(SeekParkPhase::Release, false, false));
    CHECK(!SeekParkExcludesSource(SeekParkPhase::None, true, false));
    CHECK(!SeekParkExcludesSource(SeekParkPhase::Failed, true, false));
    // The invalid-target exemption: the receiver, its released source, only in Release.
    CHECK(SeekParkReleaseTargetAllowed(SeekParkPhase::Release, true, true));
    CHECK(!SeekParkReleaseTargetAllowed(SeekParkPhase::Release, false, true));
    CHECK(!SeekParkReleaseTargetAllowed(SeekParkPhase::Release, true, false));
    CHECK(!SeekParkReleaseTargetAllowed(SeekParkPhase::Held, true, true));
    CHECK(!SeekParkReleaseTargetAllowed(SeekParkPhase::Apply, true, true));
}

// TestParkCastDue: Apply casts at once and then no more often than the gap; Held only near the control's end.
static void TestParkCastDue()
{
    SeekParkState state;
    SeekParkInput in = ParkInput(60000);
    state.phase = SeekParkPhase::Apply;
    CHECK(SeekParkCastDue(state, in, 1000));
    state.lastCastMs = 1000;
    CHECK(!SeekParkCastDue(state, in, 3499));
    CHECK(SeekParkCastDue(state, in, 3500));
    state.phase = SeekParkPhase::Held;
    state.lastCastMs = 0;
    in.controlLeftMs = 8001;
    CHECK(!SeekParkCastDue(state, in, 9000));
    in.controlLeftMs = 8000;
    CHECK(SeekParkCastDue(state, in, 9000));
    state.lastCastMs = 9000;
    in.controlLeftMs = 7000;
    CHECK(!SeekParkCastDue(state, in, 10000));
    state.phase = SeekParkPhase::Release;
    state.lastCastMs = 0;
    CHECK(!SeekParkCastDue(state, in, 12000));
}

// TestParkDpsHeldAfterRelease: the damage dealers wait for a tank, at most kSeekParkReleaseHoldMs.
static void TestParkDpsHeldAfterRelease()
{
    CHECK(SeekParkDpsHeld(SeekParkPhase::Release, false, 10000, 13999));
    CHECK(!SeekParkDpsHeld(SeekParkPhase::Release, false, 10000, 14000));
    CHECK(!SeekParkDpsHeld(SeekParkPhase::Release, true, 10000, 10001));
    CHECK(!SeekParkDpsHeld(SeekParkPhase::Held, false, 10000, 10001));
    CHECK(!SeekParkDpsHeld(SeekParkPhase::Apply, false, 10000, 10001));
    CHECK(!SeekParkDpsHeld(SeekParkPhase::Release, false, 0, 1000));
}

// TestParkGatherAtSpot: the release gathers at the seek spot while the source is still away from it.
static void TestParkGatherAtSpot()
{
    CHECK(SeekParkGatherAtSpot(SeekParkPhase::Release, 8.1f));
    CHECK(!SeekParkGatherAtSpot(SeekParkPhase::Release, 8.0f));
    CHECK(!SeekParkGatherAtSpot(SeekParkPhase::Held, 12.0f));
    CHECK(!SeekParkGatherAtSpot(SeekParkPhase::Apply, 12.0f));
}

// TestPickParkBot: Polymorph before Hibernate, then the nearer, then the lower guid; nothing eligible is -1.
static void TestPickParkBot()
{
    std::vector<SeekParkCand> cands;
    CHECK(PickSeekParkBot(cands) == -1);
    cands.push_back(SeekParkCand{false, 0, 1.0f, 5});
    CHECK(PickSeekParkBot(cands) == -1);
    // Hibernate near beats Polymorph far: rank first.
    cands.clear();
    cands.push_back(SeekParkCand{true, 1, 2.0f, 5});
    cands.push_back(SeekParkCand{true, 0, 20.0f, 6});
    CHECK(PickSeekParkBot(cands) == 1);
    // Same rank: the nearer wins.
    cands.clear();
    cands.push_back(SeekParkCand{true, 0, 12.0f, 5});
    cands.push_back(SeekParkCand{true, 0, 8.0f, 6});
    CHECK(PickSeekParkBot(cands) == 1);
    // Same rank and distance: the lower raw guid.
    cands.clear();
    cands.push_back(SeekParkCand{true, 0, 8.0f, 9});
    cands.push_back(SeekParkCand{true, 0, 8.0f, 4});
    CHECK(PickSeekParkBot(cands) == 1);
    // Ineligible candidates never win, whatever their rank.
    cands.clear();
    cands.push_back(SeekParkCand{false, 0, 1.0f, 1});
    cands.push_back(SeekParkCand{true, 1, 30.0f, 9});
    CHECK(PickSeekParkBot(cands) == 1);
}

// TestParkNewCycleResets: the next cycle or another source starts over; a dead source too.
static void TestParkNewCycleResets()
{
    const SeekParkRules rules = ParkRules();
    {
        SeekParkState state;
        SeekParkInput in = ParkInput(48000);
        CHECK(SeekParkStep(state, rules, in, 1000));
        CHECK(state.phase == SeekParkPhase::Failed);
        SeekParkInput next = in;
        next.cycleNo = 4;
        CHECK(SeekParkStep(state, rules, next, 2000));
        CHECK(state.phase == SeekParkPhase::None);
        CHECK(state.son == 77);
        CHECK(state.cycleNo == 4);
    }
    {
        // Another source with the same cycle.
        SeekParkState state;
        SeekParkInput in = ParkInput(48000);
        CHECK(SeekParkStep(state, rules, in, 1000));
        CHECK(state.phase == SeekParkPhase::Failed);
        SeekParkInput other = in;
        other.son = 78;
        CHECK(SeekParkStep(state, rules, other, 2000));
        CHECK(state.phase == SeekParkPhase::None);
        CHECK(state.son == 78);
    }
    {
        // A release ends with the source's death.
        SeekParkState state;
        SeekParkInput in = ParkInput(48001);
        CHECK(SeekParkStep(state, rules, in, 1000));
        in.dueInMs = 40000;
        CHECK(SeekParkStep(state, rules, in, 2000));
        CHECK(state.phase == SeekParkPhase::Release);
        in.sonAlive = false;
        CHECK(SeekParkStep(state, rules, in, 3000));
        CHECK(state.phase == SeekParkPhase::None);
    }
}

// TestParkReleaseNeedsReceiver: the release fails after kSeekParkReleaseHoldMs unless the source's victim is a tank,
// whatever receiver the scan found (decision 2). A taken source keeps the release for the rest of the cycle.
static void TestParkReleaseNeedsReceiver()
{
    const SeekParkRules rules = ParkRules();
    {
        SeekParkState state;
        SeekParkInput in = ParkInput(48001);
        CHECK(SeekParkStep(state, rules, in, 10000));
        CHECK(state.phase == SeekParkPhase::Apply);
        in.dueInMs = 40000;
        CHECK(SeekParkStep(state, rules, in, 10000));
        CHECK(state.phase == SeekParkPhase::Release);
        in.dueInMs = 35000;
        CHECK(!SeekParkStep(state, rules, in, 13999));
        CHECK(state.phase == SeekParkPhase::Release);
        CHECK(SeekParkStep(state, rules, in, 14000));
        CHECK(state.phase == SeekParkPhase::Failed);
        CHECK(std::strcmp(state.why, "no_receiver") == 0);
    }
    {
        // An eligible receiver that never acquires the source is no release either.
        SeekParkState state;
        SeekParkInput in = ParkInput(48001);
        CHECK(SeekParkStep(state, rules, in, 10000));
        in.dueInMs = 40000;
        in.receiverOk = true;
        CHECK(SeekParkStep(state, rules, in, 10000));
        CHECK(state.phase == SeekParkPhase::Release);
        CHECK(SeekParkStep(state, rules, in, 14000));
        CHECK(state.phase == SeekParkPhase::Failed);
        CHECK(std::strcmp(state.why, "no_receiver") == 0);
    }
    {
        // The source on a tank keeps the release for the rest of the cycle, receiver or not.
        SeekParkState state;
        SeekParkInput in = ParkInput(48001);
        CHECK(SeekParkStep(state, rules, in, 10000));
        in.dueInMs = 40000;
        in.sonOnTank = true;
        CHECK(SeekParkStep(state, rules, in, 10000));
        CHECK(state.phase == SeekParkPhase::Release);
        CHECK(!SeekParkStep(state, rules, in, 60000));
        CHECK(state.phase == SeekParkPhase::Release);
    }
}

// TestParkOnlyThePick: the park is the seek's own pick; a pick that moves away while Held or Release ends the record.
static void TestParkOnlyThePick()
{
    const SeekParkRules rules = ParkRules();
    SeekParkState state;
    SeekParkInput in = ParkInput(60000);
    CHECK(SeekParkStep(state, rules, in, 1000));
    CHECK(state.phase == SeekParkPhase::Apply);
    in.controlled = true;
    CHECK(SeekParkStep(state, rules, in, 2000));
    CHECK(state.phase == SeekParkPhase::Held);
    CHECK(state.son == in.pickedSon);
    // The pick moved to another Son while this one is held: the record ends at None.
    in.pickedSon = 78;
    CHECK(SeekParkStep(state, rules, in, 3000));
    CHECK(state.phase == SeekParkPhase::None);
    CHECK(state.son == in.son);
    // A release is the pick too.
    SeekParkState released;
    SeekParkInput out = ParkInput(48001);
    CHECK(SeekParkStep(released, rules, out, 1000));
    out.dueInMs = 40000;
    out.sonOnTank = true;
    CHECK(SeekParkStep(released, rules, out, 2000));
    CHECK(released.phase == SeekParkPhase::Release);
    out.pickedSon = 78;
    CHECK(SeekParkStep(released, rules, out, 3000));
    CHECK(released.phase == SeekParkPhase::None);
}

// TestParkFightStartResets: a re-pull with the same cycle number starts over; the fight's engage clock keys the record
// with the cycle.
static void TestParkFightStartResets()
{
    const SeekParkRules rules = ParkRules();
    SeekParkState state;
    SeekParkInput in = ParkInput(60000);
    CHECK(SeekParkStep(state, rules, in, 1000));
    CHECK(state.phase == SeekParkPhase::Apply);
    in.fightStartMs = 900;
    CHECK(SeekParkStep(state, rules, in, 2000));
    CHECK(state.phase == SeekParkPhase::None);
    CHECK(state.fightStartMs == 900);
}

// TestParkExemptsBot: only the park bot, while it applies or holds (a held park bot is not walked back to its slot).
static void TestParkExemptsBot()
{
    CHECK(SeekParkExemptsBot(SeekParkPhase::Apply, 42, 42));
    CHECK(SeekParkExemptsBot(SeekParkPhase::Held, 42, 42));
    CHECK(!SeekParkExemptsBot(SeekParkPhase::Apply, 42, 43));
    CHECK(!SeekParkExemptsBot(SeekParkPhase::Apply, 0, 0));
    CHECK(!SeekParkExemptsBot(SeekParkPhase::Release, 42, 42));
}

// The phase words the log row writes.
static void TestParkPhaseWords()
{
    CHECK(std::strcmp(SeekParkPhaseWord(SeekParkPhase::None), "none") == 0);
    CHECK(std::strcmp(SeekParkPhaseWord(SeekParkPhase::Apply), "apply") == 0);
    CHECK(std::strcmp(SeekParkPhaseWord(SeekParkPhase::Held), "held") == 0);
    CHECK(std::strcmp(SeekParkPhaseWord(SeekParkPhase::Release), "release") == 0);
    CHECK(std::strcmp(SeekParkPhaseWord(SeekParkPhase::Failed), "failed") == 0);
}

int main()
{
    std::cout << "Starting TortoiseBots seek-park policy tests...\n";
    TestParkHeldUntilReleaseLead();
    TestParkNeedsRoom();
    TestParkApplyBudget();
    TestParkReapplyCap();
    TestParkVetoesTankTasks();
    TestParkExcludesAndTarget();
    TestParkCastDue();
    TestParkDpsHeldAfterRelease();
    TestParkGatherAtSpot();
    TestPickParkBot();
    TestParkNewCycleResets();
    TestParkReleaseNeedsReceiver();
    TestParkOnlyThePick();
    TestParkFightStartResets();
    TestParkExemptsBot();
    TestParkPhaseWords();
    std::cout << "All seek-park policy tests passed.\n";
    return 0;
}
