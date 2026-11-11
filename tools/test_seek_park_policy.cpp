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
    in.cycleNo = 3;
    in.sonAlive = true;
    in.sonInCombat = true;
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
    CHECK(SeekParkLateLeadMs(SeekParkPhase::Held, true, 75000, 40000) == 40000);
    CHECK(SeekParkLateLeadMs(SeekParkPhase::Failed, true, 75000, 40000) == 75000);
    CHECK(SeekParkLateLeadMs(SeekParkPhase::None, true, 75000, 40000) == 75000);
    CHECK(SeekParkLateLeadMs(SeekParkPhase::Release, true, 75000, 40000) == 40000);
    CHECK(SeekParkLateLeadMs(SeekParkPhase::Apply, true, 75000, 40000) == 40000);
    // Key off: today's lead whatever the phase says.
    CHECK(SeekParkLateLeadMs(SeekParkPhase::Held, false, 75000, 40000) == 75000);
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

// TestParkApplyBudget: the control must be on the source within the row's window after the park started.
static void TestParkApplyBudget()
{
    const SeekParkRules rules = ParkRules();
    SeekParkState state;
    CHECK(SeekParkStep(state, rules, ParkInput(60000), 1000));
    CHECK(state.phase == SeekParkPhase::Apply);
    CHECK(!SeekParkStep(state, rules, ParkInput(60000), 7000));
    CHECK(state.phase == SeekParkPhase::Apply);
    CHECK(SeekParkStep(state, rules, ParkInput(60000), 7001));
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

// TestParkVetoesTankTasks: Apply and Held veto every take path, Release all but its own, nothing else vetoes.
static void TestParkVetoesTankTasks()
{
    const char* const sources[] = {"meet", "receiver", "pickup", "pull_join", "nearest", "nearest_reach",
        "healer_clock", "victim_relief", "other", nullptr};
    for (const char* src : sources)
    {
        CHECK(SeekParkVetoesTankTask(SeekParkPhase::Apply, src));
        CHECK(SeekParkVetoesTankTask(SeekParkPhase::Held, src));
        CHECK(!SeekParkVetoesTankTask(SeekParkPhase::None, src));
        CHECK(!SeekParkVetoesTankTask(SeekParkPhase::Failed, src));
        CHECK(src && std::strcmp(src, "park_release") == 0 ? !SeekParkVetoesTankTask(SeekParkPhase::Release, src)
                                                           : SeekParkVetoesTankTask(SeekParkPhase::Release, src));
    }
    CHECK(!SeekParkVetoesTankTask(SeekParkPhase::Release, "park_release"));
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

// TestParkReleaseNeedsReceiver: a release nobody takes in kSeekParkReleaseHoldMs fails the park.
static void TestParkReleaseNeedsReceiver()
{
    const SeekParkRules rules = ParkRules();
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
    // A receiver keeps the release for the rest of the cycle.
    SeekParkState taken;
    in.receiverOk = true;
    in.dueInMs = 48001;
    CHECK(SeekParkStep(taken, rules, in, 10000));
    in.dueInMs = 40000;
    CHECK(SeekParkStep(taken, rules, in, 10000));
    CHECK(taken.phase == SeekParkPhase::Release);
    CHECK(!SeekParkStep(taken, rules, in, 60000));
    CHECK(taken.phase == SeekParkPhase::Release);
}

// TestParkExemptsBot: only the park bot, only while it applies.
static void TestParkExemptsBot()
{
    CHECK(SeekParkExemptsBot(SeekParkPhase::Apply, 42, 42));
    CHECK(!SeekParkExemptsBot(SeekParkPhase::Held, 42, 42));
    CHECK(!SeekParkExemptsBot(SeekParkPhase::Apply, 42, 43));
    CHECK(!SeekParkExemptsBot(SeekParkPhase::Apply, 0, 0));
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
    TestParkCastDue();
    TestParkDpsHeldAfterRelease();
    TestParkGatherAtSpot();
    TestPickParkBot();
    TestParkNewCycleResets();
    TestParkReleaseNeedsReceiver();
    TestParkExemptsBot();
    TestParkPhaseWords();
    std::cout << "All seek-park policy tests passed.\n";
    return 0;
}
