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
    in.parkBot = 5;
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
    in.receiverInReach = true;
    CHECK(SeekParkStep(state, rules, in, 4000));
    CHECK(state.phase == SeekParkPhase::Release);
    CHECK(state.releaseMs == 4000);
    CHECK(SeekParkHeldMs(state, 4000) == 1500);
    CHECK(SeekParkLateLeadMs(SeekParkPhase::Held, true, true, 75000, 40000) == 40000);
    CHECK(SeekParkLateLeadMs(SeekParkPhase::Failed, true, true, 75000, 40000) == 75000);
    CHECK(SeekParkLateLeadMs(SeekParkPhase::None, true, true, 75000, 40000) == 75000);
    CHECK(SeekParkLateLeadMs(SeekParkPhase::Release, true, true, 75000, 40000) == 40000);
    CHECK(SeekParkLateLeadMs(SeekParkPhase::Released, true, true, 75000, 40000) == 40000);
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
    {
        SeekParkState state;
        CHECK(SeekParkStep(state, rules, ParkInput(60000), 1000));
        CHECK(state.phase == SeekParkPhase::Apply);
        CHECK(state.budgetSinceMs == 0);
        // Still walking to the source: no budget runs (the walk cap bounds it, TestParkApplyWalkCap).
        CHECK(!SeekParkStep(state, rules, ParkInput(60000), 7001));
        CHECK(state.phase == SeekParkPhase::Apply);
        CHECK(state.budgetSinceMs == 0);
    }
    {
        SeekParkState state;
        CHECK(SeekParkStep(state, rules, ParkInput(60000), 1000));
        // In reach at 2000: the budget starts there.
        SeekParkInput in = ParkInput(60000);
        in.controllerInReach = true;
        CHECK(!SeekParkStep(state, rules, in, 2000));
        CHECK(state.budgetSinceMs == 2000);
        CHECK(!SeekParkStep(state, rules, ParkInput(60000), 8000));
        CHECK(state.phase == SeekParkPhase::Apply);
        CHECK(SeekParkStep(state, rules, ParkInput(60000), 8001));
        CHECK(state.phase == SeekParkPhase::Failed);
        CHECK(std::strcmp(state.why, "budget") == 0);
    }
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

// TestParkVetoesTankTasks: Apply and Held veto every take path; Release vetoes every path but its own, whoever holds
// the source (R11: only the release's receiver touches it in its window); a latched release on a tank vetoes all but
// the holder's own drag, and one that broke onto a non-tank lets the ordinary takes back.
static void TestParkVetoesTankTasks()
{
    const char* const sources[] = {"meet", "receiver", "pickup", "pull_join", "nearest", "nearest_reach",
        "healer_clock", "victim_relief", "other", nullptr};
    for (const char* src : sources)
    {
        const bool own = src && std::strcmp(src, "park_release") == 0;
        CHECK(SeekParkVetoesTankTask(SeekParkPhase::Apply, src, false, false));
        CHECK(SeekParkVetoesTankTask(SeekParkPhase::Apply, src, true, true));
        CHECK(SeekParkVetoesTankTask(SeekParkPhase::Held, src, false, false));
        CHECK(SeekParkVetoesTankTask(SeekParkPhase::Held, src, true, true));
        CHECK(!SeekParkVetoesTankTask(SeekParkPhase::None, src, true, true));
        CHECK(!SeekParkVetoesTankTask(SeekParkPhase::Failed, src, true, true));
        // The release's window while the source is controlled: only its own take.
        CHECK(own ? !SeekParkVetoesTankTask(SeekParkPhase::Release, src, false, true)
                  : SeekParkVetoesTankTask(SeekParkPhase::Release, src, false, true));
        CHECK(own ? !SeekParkVetoesTankTask(SeekParkPhase::Release, src, true, true)
                  : SeekParkVetoesTankTask(SeekParkPhase::Release, src, true, true));
        // The control broke during the window: today's takes come back (Fix 3).
        CHECK(!SeekParkVetoesTankTask(SeekParkPhase::Release, src, false, false));
        // The latched release: every path but the own one while a tank holds the source, nothing on a non-tank.
        CHECK(own ? !SeekParkVetoesTankTask(SeekParkPhase::Released, src, false, true)
                  : !SeekParkVetoesTankTask(SeekParkPhase::Released, src, false, true));
        CHECK(own ? !SeekParkVetoesTankTask(SeekParkPhase::Released, src, true, false)
                  : SeekParkVetoesTankTask(SeekParkPhase::Released, src, true, false));
    }
    CHECK(SeekParkVetoesTankTask(SeekParkPhase::Release, "nearest", false, true));
    CHECK(!SeekParkVetoesTankTask(SeekParkPhase::Release, "park_release", false, true));
    CHECK(!SeekParkVetoesTankTask(SeekParkPhase::Released, "nearest", false, true));
    CHECK(SeekParkVetoesTankTask(SeekParkPhase::Released, "nearest", true, false));
}

// TestParkExcludesAndTarget: the body-skip rule (R11) and the release target exemption (decisions 3, 4 and 5).
static void TestParkExcludesAndTarget()
{
    // Apply and Held: the body skips the parked source for every tank.
    CHECK(SeekParkExcludesSource(SeekParkPhase::Apply, false, false, false));
    CHECK(SeekParkExcludesSource(SeekParkPhase::Apply, true, true, true));
    CHECK(SeekParkExcludesSource(SeekParkPhase::Held, true, false, true));
    // Release: every bot but the release's own skips it while the source is controlled (R11).
    CHECK(SeekParkExcludesSource(SeekParkPhase::Release, false, false, true));
    CHECK(!SeekParkExcludesSource(SeekParkPhase::Release, false, true, true));
    CHECK(SeekParkExcludesSource(SeekParkPhase::Release, true, false, true));
    CHECK(!SeekParkExcludesSource(SeekParkPhase::Release, true, true, true));
    // The control broke during the window: the source is today's again (Fix 3).
    CHECK(!SeekParkExcludesSource(SeekParkPhase::Release, false, false, false));
    CHECK(!SeekParkExcludesSource(SeekParkPhase::Release, false, true, false));
    // A latched release on a tank: every bot but the holder; on a non-tank nobody skips it.
    CHECK(SeekParkExcludesSource(SeekParkPhase::Released, true, false, false));
    CHECK(!SeekParkExcludesSource(SeekParkPhase::Released, true, true, false));
    CHECK(!SeekParkExcludesSource(SeekParkPhase::Released, false, false, false));
    CHECK(!SeekParkExcludesSource(SeekParkPhase::None, true, false, false));
    CHECK(!SeekParkExcludesSource(SeekParkPhase::Failed, true, false, true));
    // The invalid-target exemption: the receiver, its released source, while the release is live.
    CHECK(SeekParkReleaseTargetAllowed(SeekParkPhase::Release, true, false, true, true));
    CHECK(SeekParkReleaseTargetAllowed(SeekParkPhase::Release, true, false, true, false));
    CHECK(!SeekParkReleaseTargetAllowed(SeekParkPhase::Release, false, false, true, true));
    CHECK(!SeekParkReleaseTargetAllowed(SeekParkPhase::Release, true, false, false, true));
    CHECK(!SeekParkReleaseTargetAllowed(SeekParkPhase::Release, false, true, true, true));
    CHECK(SeekParkReleaseTargetAllowed(SeekParkPhase::Released, true, false, true, false));
    CHECK(!SeekParkReleaseTargetAllowed(SeekParkPhase::Released, false, false, true, true));
    CHECK(!SeekParkReleaseTargetAllowed(SeekParkPhase::Held, true, false, true, true));
    CHECK(!SeekParkReleaseTargetAllowed(SeekParkPhase::Apply, true, false, true, true));
}

// TestParkOnlyTheReceiverInRelease: F6, a second tank never takes the released Son inside its window (R11's table).
static void TestParkOnlyTheReceiverInRelease()
{
    CHECK(SeekParkExcludesSource(SeekParkPhase::Release, false, false, true));
    CHECK(!SeekParkExcludesSource(SeekParkPhase::Release, false, true, true));
    CHECK(!SeekParkExcludesSource(SeekParkPhase::Released, false, false, true));
    CHECK(SeekParkExcludesSource(SeekParkPhase::Released, true, false, true));
    CHECK(!SeekParkExcludesSource(SeekParkPhase::Released, true, true, true));
    CHECK(SeekParkExcludesSource(SeekParkPhase::Apply, true, false, true));
    CHECK(SeekParkExcludesSource(SeekParkPhase::Held, true, true, true));
    CHECK(!SeekParkExcludesSource(SeekParkPhase::None, true, true, true));
    CHECK(!SeekParkExcludesSource(SeekParkPhase::Failed, true, true, true));
    CHECK(SeekParkVetoesTankTask(SeekParkPhase::Release, "nearest", false, true));
    CHECK(!SeekParkVetoesTankTask(SeekParkPhase::Release, "park_release", false, true));
    CHECK(!SeekParkVetoesTankTask(SeekParkPhase::Released, "nearest", false, true));
    CHECK(SeekParkVetoesTankTask(SeekParkPhase::Released, "nearest", true, false));
    CHECK(!SeekParkVetoesTankTask(SeekParkPhase::Released, "park_release", true, false));
}

// TestParkBrokenControlReleasesToday (Fix 3): the receiver-only window and the 8 s hold are for a source that is still
// controlled. An awake Son on a healer gets the ordinary takes at once and today's 4 s.
static void TestParkBrokenControlReleasesToday()
{
    const SeekParkRules rules = ParkRules();
    SeekParkState state;
    SeekParkInput in = ParkInput(48001);
    in.receiverOk = true;
    CHECK(SeekParkStep(state, rules, in, 10000));
    in.dueInMs = 40000;
    in.controlled = true;
    CHECK(SeekParkStep(state, rules, in, 10000));
    CHECK(state.phase == SeekParkPhase::Release);
    // The Polymorph ran out: the Son is awake on a non-tank. Its ordinary takes and damage are not held.
    in.controlled = false;
    CHECK(!SeekParkVetoesTankTask(SeekParkPhase::Release, "nearest", false, false));
    CHECK(!SeekParkExcludesSource(SeekParkPhase::Release, false, false, false));
    CHECK(SeekParkDpsHeld(SeekParkPhase::Release, false, 10000, 13999, true, false));
    CHECK(!SeekParkDpsHeld(SeekParkPhase::Release, false, 10000, 14000, true, false));
    // Today's 4 s window, not the receiver's 8.
    CHECK(!SeekParkStep(state, rules, in, 13999));
    CHECK(state.phase == SeekParkPhase::Release);
    CHECK(SeekParkStep(state, rules, in, 14000));
    CHECK(state.phase == SeekParkPhase::Failed);
}

// TestParkFailedTakeDue (Fix 2): a park that failed while its source is still controlled still gets it broken by the
// release's receiver; with no receiver the ordinary takes answer, and an awake source uses today's takes.
static void TestParkFailedTakeDue()
{
    CHECK(SeekParkFailedTakeDue(SeekParkPhase::Failed, true, true));
    CHECK(!SeekParkFailedTakeDue(SeekParkPhase::Failed, true, false));
    CHECK(!SeekParkFailedTakeDue(SeekParkPhase::Failed, false, true));
    CHECK(!SeekParkFailedTakeDue(SeekParkPhase::Release, true, true));
    CHECK(!SeekParkFailedTakeDue(SeekParkPhase::Released, true, true));
    CHECK(!SeekParkFailedTakeDue(SeekParkPhase::Held, true, true));
    // The receiver's target exemption covers that take.
    CHECK(SeekParkReleaseTargetAllowed(SeekParkPhase::Failed, true, false, true, true));
    CHECK(!SeekParkReleaseTargetAllowed(SeekParkPhase::Failed, true, false, true, false));
    CHECK(!SeekParkReleaseTargetAllowed(SeekParkPhase::Failed, true, false, false, true));
    CHECK(!SeekParkReleaseTargetAllowed(SeekParkPhase::Failed, false, false, true, true));
    // A failed park vetoes and excludes nothing: the ordinary off-tank takes reach the source.
    CHECK(!SeekParkVetoesTankTask(SeekParkPhase::Failed, "nearest", true, true));
    CHECK(!SeekParkExcludesSource(SeekParkPhase::Failed, true, false, true));
}

// TestParkFailedParkBreaker: the named failing case. A park that failed while its Son is still controlled and found no
// receiver hands the break to the first ordinary off-tank that takes it: that taker's target is exempt from the
// invalid-target rule through the walk and the attack, and only until the control goes. A breaker that died, was
// charmed or became Hakkar's victim is dropped, and the next ordinary taker latches.
static void TestParkFailedParkBreaker()
{
    // The latch: a failed, controlled park with no receiver, on a tank whose ordinary take reaches the Son.
    CHECK(SeekParkBreakerDue(SeekParkPhase::Failed, true, false, true));
    CHECK(!SeekParkBreakerDue(SeekParkPhase::Failed, true, true, true));   // a receiver exists
    CHECK(!SeekParkBreakerDue(SeekParkPhase::Failed, false, false, true)); // the control broke
    CHECK(!SeekParkBreakerDue(SeekParkPhase::Failed, true, false, false)); // the take is on another Son
    CHECK(!SeekParkBreakerDue(SeekParkPhase::Release, true, false, true));
    CHECK(!SeekParkBreakerDue(SeekParkPhase::Held, true, false, true));
    // The breaker's own take targets the sheeped Son, through approach and attack, until the control is gone.
    CHECK(SeekParkReleaseTargetAllowed(SeekParkPhase::Failed, false, true, true, true));
    CHECK(!SeekParkReleaseTargetAllowed(SeekParkPhase::Failed, false, true, true, false));
    CHECK(!SeekParkReleaseTargetAllowed(SeekParkPhase::Failed, false, false, true, true));
    CHECK(!SeekParkReleaseTargetAllowed(SeekParkPhase::Release, false, true, true, true));
    // Kept while it qualifies; the control's loss, death, a charm and Hakkar's victim each drop it.
    CHECK(SeekParkBreakerKeeps(SeekParkPhase::Failed, true, true, false, false));
    CHECK(!SeekParkBreakerKeeps(SeekParkPhase::Failed, false, true, false, false));
    CHECK(!SeekParkBreakerKeeps(SeekParkPhase::Failed, true, false, false, false));
    CHECK(!SeekParkBreakerKeeps(SeekParkPhase::Failed, true, true, true, false));
    CHECK(!SeekParkBreakerKeeps(SeekParkPhase::Failed, true, true, false, true));
    CHECK(!SeekParkBreakerKeeps(SeekParkPhase::Release, true, true, false, false));
}


// TestParkStaleReadKeepsTheLatch (round 8): the record's receiver and breaker are computed outside its lock, so two
// bots' ticks interleave in call order here. Reader A reads the record at Release with its own receiver pick; reader
// B's tick lands first and latches the record Released with receiver R. A's write must leave R and Released intact.
// A stale receiver never lands on a record whose phase moved past the reader's, and a breaker another read latched is
// never cleared or replaced except by a read that saw that same breaker and dropped it.
static void TestParkStaleReadKeepsTheLatch()
{
    const SeekParkRules rules = ParkRules();
    SeekParkState record;
    SeekParkInput in = ParkInput(60000);
    CHECK(SeekParkStep(record, rules, in, 1000));
    in.controlled = true;
    CHECK(SeekParkStep(record, rules, in, 2000));
    in.dueInMs = 39000;
    in.receiverInReach = true;
    CHECK(SeekParkStep(record, rules, in, 3000));
    CHECK(record.phase == SeekParkPhase::Release);
    const SeekParkPhase aPhase = record.phase;
    const std::uint64_t aReceiver = 41;  // A's own pick, computed against its read
    const std::uint64_t rReceiver = 52;  // the tank B's tick latched with

    // B's tick on the same record: a tank took the source, so the release latches with B's receiver.
    SeekParkState b = record;
    SeekParkMergeRead(b, b.phase, 0, rReceiver, 0);
    SeekParkInput bIn = in;
    bIn.sonOnTank = true;
    CHECK(SeekParkStep(b, rules, bIn, 4000));
    CHECK(b.phase == SeekParkPhase::Released);
    CHECK(b.receiver == rReceiver);

    // A's tick, still carrying what its earlier read produced: the latch survives both the write and the step.
    SeekParkMergeRead(b, aPhase, 0, aReceiver, 0);
    CHECK(!SeekParkStep(b, rules, in, 5000));
    CHECK(b.phase == SeekParkPhase::Released);
    CHECK(b.receiver == rReceiver);

    // A failed, controlled park: B's read latched the ordinary off-tank as its breaker, and A's stale read (which saw
    // no breaker, or another one) neither clears nor replaces it.
    SeekParkState failed;
    CHECK(SeekParkStep(failed, rules, ParkInput(10000), 1000));
    CHECK(failed.phase == SeekParkPhase::Failed);
    SeekParkMergeRead(failed, SeekParkPhase::Failed, 0, 0, 61);
    CHECK(failed.breaker == 61);
    SeekParkMergeRead(failed, SeekParkPhase::Failed, 0, 0, 0);
    CHECK(failed.breaker == 61);
    SeekParkMergeRead(failed, SeekParkPhase::Failed, 0, 0, 41);
    CHECK(failed.breaker == 61);
    // A read that saw that breaker and no longer qualifies drops it, so the next taker can latch; the stale one after
    // it cannot undo the drop.
    SeekParkMergeRead(failed, SeekParkPhase::Failed, 61, 0, 0);
    CHECK(failed.breaker == 0);
    SeekParkMergeRead(failed, SeekParkPhase::Failed, 61, 0, 61);
    CHECK(failed.breaker == 0);
    SeekParkMergeRead(failed, SeekParkPhase::Failed, 0, 0, 61);
    CHECK(failed.breaker == 61);

    // A read that saw Release writes nothing on a record the phase moved past (the park failed with no receiver), but
    // the same read at its own phase fills the record.
    SeekParkState past = failed;
    past.receiver = 0;
    SeekParkMergeRead(past, SeekParkPhase::Release, 0, aReceiver, 0);
    CHECK(past.phase == SeekParkPhase::Failed);
    CHECK(past.receiver == 0);
    SeekParkMergeRead(past, SeekParkPhase::Failed, 61, aReceiver, 61);
    CHECK(past.receiver == aReceiver);

    // The latched release's receiver is frozen even for a read that saw Released itself.
    SeekParkState latched = b;
    SeekParkMergeRead(latched, SeekParkPhase::Released, 0, 0, 0);
    CHECK(latched.receiver == rReceiver);

    // A reader at the record's own phase fills the receiver, and the merge runs before the reader's own step, so the
    // release opens with that pick.
    SeekParkState open;
    SeekParkInput oIn = ParkInput(60000);
    CHECK(SeekParkStep(open, rules, oIn, 1000));
    oIn.controlled = true;
    CHECK(SeekParkStep(open, rules, oIn, 2000));
    CHECK(open.phase == SeekParkPhase::Held);
    SeekParkMergeRead(open, SeekParkPhase::Held, 0, aReceiver, 0);
    CHECK(open.receiver == aReceiver);
    oIn.dueInMs = 39000;
    oIn.receiverInReach = true;
    CHECK(SeekParkStep(open, rules, oIn, 3000));
    CHECK(open.phase == SeekParkPhase::Release);
    CHECK(open.receiver == aReceiver);
    // Held -> Apply (a broken control) is behind the reader, not past it: the write still lands.
    SeekParkState broken = open;
    broken.phase = SeekParkPhase::Apply;
    broken.receiver = 0;
    SeekParkMergeRead(broken, SeekParkPhase::Held, 0, aReceiver, 0);
    CHECK(broken.receiver == aReceiver);
}

// TestParkStaleTickDropsTheUpdate (round 9): the step consumes facts (control, due time, the source's victim, the
// receiver's reach) that the caller read before the lock. A tick whose record another bot committed since writes
// nothing at all: a stale Held read that saw the control cannot resurrect it after another tick recorded the break, and
// the next tick, reading the record afresh, carries on.
static void TestParkStaleTickDropsTheUpdate()
{
    const SeekParkRules rules = ParkRules();
    SeekParkRecord record;
    SeekParkInput in = ParkInput(60000);
    SeekParkCommitResult r = SeekParkCommit(record, record.revision, rules, in, SeekParkPhase::None, 0, 0, 0, 1000);
    CHECK(r.applied);
    CHECK(record.state.phase == SeekParkPhase::Apply);
    in.controlled = true;
    r = SeekParkCommit(record, record.revision, rules, in, SeekParkPhase::Apply, 0, 0, 0, 2000);
    CHECK(r.applied);
    CHECK(record.state.phase == SeekParkPhase::Held);

    // A's read: Held, the control still on the source.
    const std::uint32_t aRevision = record.revision;
    const SeekParkPhase aPhase = record.state.phase;
    const SeekParkInput aIn = in;

    // B's tick commits the break first: the control is gone, so the park re-applies.
    SeekParkInput bIn = in;
    bIn.controlled = false;
    r = SeekParkCommit(record, record.revision, rules, bIn, SeekParkPhase::Held, 0, 0, 0, 2500);
    CHECK(r.applied);
    CHECK(r.changed);
    CHECK(record.state.phase == SeekParkPhase::Apply);
    const SeekParkState broken = record.state;
    const std::uint32_t bRevision = record.revision;
    CHECK(bRevision != aRevision);

    // A's update, computed from the Held read, lands after that commit and must write nothing.
    r = SeekParkCommit(record, aRevision, rules, aIn, aPhase, 0, 0, 0, 2600);
    CHECK(!r.applied);
    CHECK(!r.changed);
    CHECK(record.revision == bRevision);
    CHECK(SeekParkSameState(record.state, broken));

    // The next tick re-reads the record and carries on from its own facts.
    r = SeekParkCommit(record, record.revision, rules, aIn, record.state.phase, 0, 0, 0, 2700);
    CHECK(r.applied);
    CHECK(record.state.phase == SeekParkPhase::Held);
}

// TestParkCommitKeepsTheLatch (round 8 through round 9's gate): a release another tick latched keeps its receiver when
// a stale read's update lands afterwards, and a tick that writes nothing leaves the revision alone so it does not drop
// the next read's update for nothing.
static void TestParkCommitKeepsTheLatch()
{
    const SeekParkRules rules = ParkRules();
    SeekParkRecord record;
    SeekParkInput in = ParkInput(60000);
    SeekParkCommitResult r = SeekParkCommit(record, record.revision, rules, in, SeekParkPhase::None, 0, 0, 0, 1000);
    CHECK(r.applied);
    in.controlled = true;
    r = SeekParkCommit(record, record.revision, rules, in, SeekParkPhase::Apply, 0, 0, 0, 2000);
    CHECK(r.applied);
    CHECK(record.state.phase == SeekParkPhase::Held);
    // A read that changed nothing leaves the revision alone.
    const std::uint32_t heldRevision = record.revision;
    r = SeekParkCommit(record, heldRevision, rules, in, SeekParkPhase::Held, 0, 0, 0, 2500);
    CHECK(r.applied);
    CHECK(!r.changed);
    CHECK(record.revision == heldRevision);

    in.dueInMs = 39000;
    in.receiverInReach = true;
    const std::uint32_t aRevision = record.revision;
    r = SeekParkCommit(record, aRevision, rules, in, SeekParkPhase::Held, 0, 0, 0, 3000);
    CHECK(r.applied);
    CHECK(record.state.phase == SeekParkPhase::Release);
    // A's read sees the open release; B's tick latches it on receiver 52 first.
    const SeekParkPhase aPhase = record.state.phase;
    const std::uint32_t releaseRevision = record.revision;
    SeekParkInput bIn = in;
    bIn.sonOnTank = true;
    r = SeekParkCommit(record, releaseRevision, rules, bIn, SeekParkPhase::Release, 0, 52, 0, 4000);
    CHECK(r.applied);
    CHECK(record.state.phase == SeekParkPhase::Released);
    CHECK(record.state.receiver == 52);
    // A's update (receiver 41), computed from the open release, is dropped whole: the latch keeps both.
    r = SeekParkCommit(record, aRevision, rules, in, aPhase, 0, 41, 0, 4100);
    CHECK(!r.applied);
    CHECK(record.state.phase == SeekParkPhase::Released);
    CHECK(record.state.receiver == 52);
}

// TestParkSingleReaderProgresses: one bot reading and committing in turn walks the whole park, each tick capturing the
// revision its own last commit left.
static void TestParkSingleReaderProgresses()
{
    const SeekParkRules rules = ParkRules();
    SeekParkRecord record;
    SeekParkInput in = ParkInput(60000);
    SeekParkCommitResult r = SeekParkCommit(record, record.revision, rules, in, record.state.phase, 0, 0, 0, 1000);
    CHECK(r.applied);
    CHECK(record.state.phase == SeekParkPhase::Apply);
    in.controlled = true;
    r = SeekParkCommit(record, record.revision, rules, in, record.state.phase, 0, 0, 0, 2000);
    CHECK(r.applied);
    CHECK(record.state.phase == SeekParkPhase::Held);
    in.dueInMs = 39000;
    in.receiverInReach = true;
    r = SeekParkCommit(record, record.revision, rules, in, record.state.phase, 0, 0, 0, 3000);
    CHECK(r.applied);
    CHECK(record.state.phase == SeekParkPhase::Release);
    in.sonOnTank = true;
    r = SeekParkCommit(record, record.revision, rules, in, record.state.phase, 0, 52, 0, 4000);
    CHECK(r.applied);
    CHECK(record.state.phase == SeekParkPhase::Released);
    CHECK(record.state.receiver == 52);
    CHECK(record.revision == 4);
}

// TestParkRejectedTickPublishesNothing (round 9b): a tick whose commit is rejected still holds the input it captured
// before the lock (the on-tank verdict, the receiver's qualification, the control, the due time). None of it may reach
// the fight state: the values the last committed tick published stay, and the next tick re-reads.
static void TestParkRejectedTickPublishesNothing()
{
    const SeekParkRules rules = ParkRules();
    SeekParkRecord record;
    SeekParkInput in = ParkInput(60000);
    SeekParkCommitResult r = SeekParkCommit(record, record.revision, rules, in, SeekParkPhase::None, 0, 0, 0, 1000);
    CHECK(r.applied);
    in.controlled = true;
    r = SeekParkCommit(record, record.revision, rules, in, SeekParkPhase::Apply, 0, 0, 0, 2000);
    CHECK(r.applied);
    CHECK(record.state.phase == SeekParkPhase::Held);

    // What the last committed tick published, and what A's read captured before the lock.
    SeekParkPublish published;
    published.phase = SeekParkPhase::Held;
    published.son = 77;
    published.parkBot = 5;
    published.releaseMs = 2000;
    published.receiver = 52;
    published.breaker = 61;
    published.sonOnTank = true;
    published.receiverOk = true;
    published.preWalk = true;
    published.controlled = true;
    const SeekParkPublish cached = published;
    const std::uint32_t aRevision = record.revision;

    // B's tick commits the break first.
    SeekParkInput bIn = in;
    bIn.controlled = false;
    r = SeekParkCommit(record, record.revision, rules, bIn, SeekParkPhase::Held, 0, 0, 0, 2500);
    CHECK(r.applied);
    CHECK(record.state.phase == SeekParkPhase::Apply);

    // A's stale tick: rejected, and nothing of its captured facts is published.
    SeekParkInput aIn = in;  // controlled, but the Son is on no tank, the receiver does not qualify, due 60 s away
    r = SeekParkCommit(record, aRevision, rules, aIn, SeekParkPhase::Held, 0, 0, 0, 2600);
    CHECK(!r.applied);
    CHECK(!SeekParkPublishRead(published, r, aIn, rules.releaseLeadMs));
    CHECK(published.phase == cached.phase);
    CHECK(published.son == cached.son);
    CHECK(published.parkBot == cached.parkBot);
    CHECK(published.releaseMs == cached.releaseMs);
    CHECK(published.receiver == cached.receiver);
    CHECK(published.breaker == cached.breaker);
    CHECK(published.sonOnTank == cached.sonOnTank);
    CHECK(published.receiverOk == cached.receiverOk);
    CHECK(published.preWalk == cached.preWalk);
    CHECK(published.controlled == cached.controlled);

    // The next tick re-reads and publishes its own read.
    r = SeekParkCommit(record, record.revision, rules, aIn, record.state.phase, 0, 0, 0, 2700);
    CHECK(r.applied);
    CHECK(SeekParkPublishRead(published, r, aIn, rules.releaseLeadMs));
    CHECK(published.phase == SeekParkPhase::Held);
    CHECK(published.son == 77);
    CHECK(published.sonOnTank == aIn.sonOnTank);
    CHECK(published.receiverOk == aIn.receiverOk);
    CHECK(published.preWalk == SeekParkPreWalk(SeekParkPhase::Held, aIn.dueKnown, aIn.dueInMs,
                                               rules.releaseLeadMs));
    CHECK(published.controlled == aIn.controlled);
}

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

// TestParkSourceHitAllowed (round 7, the named failing case): in Release, with the Son still controlled, the
// receiver's own hit is the park's (its swing is what breaks the control), so the seek hold must not answer for it.
// The failed park's receiver and breaker are the same break; every other bot's damage on a parked source is held.
static void TestParkSourceHitAllowed()
{
    CHECK(SeekParkSourceHitAllowed(SeekParkPhase::Release, true, true, false));
    CHECK(SeekParkSourceHitAllowed(SeekParkPhase::Release, true, true, true));
    CHECK(!SeekParkSourceHitAllowed(SeekParkPhase::Release, true, false, false));
    CHECK(!SeekParkSourceHitAllowed(SeekParkPhase::Release, true, false, true));
    CHECK(SeekParkSourceHitAllowed(SeekParkPhase::Released, true, true, false));
    CHECK(!SeekParkSourceHitAllowed(SeekParkPhase::Released, true, false, true));
    CHECK(SeekParkSourceHitAllowed(SeekParkPhase::Failed, true, true, false));
    CHECK(SeekParkSourceHitAllowed(SeekParkPhase::Failed, true, false, true));
    CHECK(!SeekParkSourceHitAllowed(SeekParkPhase::Failed, true, false, false));
    CHECK(!SeekParkSourceHitAllowed(SeekParkPhase::Failed, false, true, true));
    CHECK(!SeekParkSourceHitAllowed(SeekParkPhase::Apply, true, true, true));
    CHECK(!SeekParkSourceHitAllowed(SeekParkPhase::Held, true, true, true));
    CHECK(!SeekParkSourceHitAllowed(SeekParkPhase::None, true, true, true));
}

// TestParkDpsHeldAfterRelease: the damage dealers wait for a tank, at most the release's window.
static void TestParkDpsHeldAfterRelease()
{
    CHECK(SeekParkDpsHeld(SeekParkPhase::Release, false, 10000, 13999, false, false));
    CHECK(!SeekParkDpsHeld(SeekParkPhase::Release, false, 10000, 14000, false, false));
    CHECK(SeekParkDpsHeld(SeekParkPhase::Release, false, 10000, 17999, true, true));
    CHECK(!SeekParkDpsHeld(SeekParkPhase::Release, false, 10000, 18000, true, true));
    // The receiver's longer hold needs a controlled source: awake, it is today's 4 s.
    CHECK(SeekParkDpsHeld(SeekParkPhase::Release, false, 10000, 13999, true, false));
    CHECK(!SeekParkDpsHeld(SeekParkPhase::Release, false, 10000, 14000, true, false));
    CHECK(!SeekParkDpsHeld(SeekParkPhase::Release, true, 10000, 10001, false, false));
    CHECK(!SeekParkDpsHeld(SeekParkPhase::Release, true, 10000, 10001, true, true));
    // A latched release reads the same as the open one.
    CHECK(SeekParkDpsHeld(SeekParkPhase::Released, false, 10000, 13999, false, false));
    CHECK(!SeekParkDpsHeld(SeekParkPhase::Released, false, 10000, 14000, false, false));
    CHECK(SeekParkDpsHeld(SeekParkPhase::Released, false, 10000, 17999, true, true));
    CHECK(!SeekParkDpsHeld(SeekParkPhase::Released, true, 10000, 10001, true, true));
    CHECK(!SeekParkDpsHeld(SeekParkPhase::Held, false, 10000, 10001, false, false));
    CHECK(!SeekParkDpsHeld(SeekParkPhase::Apply, false, 10000, 10001, false, false));
    CHECK(!SeekParkDpsHeld(SeekParkPhase::Release, false, 0, 1000, false, false));
}

// TestParkGatherAtSpot: the release gathers at the seek spot while the source is still away from it.
static void TestParkGatherAtSpot()
{
    CHECK(SeekParkGatherAtSpot(SeekParkPhase::Release, 8.1f));
    CHECK(!SeekParkGatherAtSpot(SeekParkPhase::Release, 8.0f));
    CHECK(SeekParkGatherAtSpot(SeekParkPhase::Released, 8.1f));
    CHECK(!SeekParkGatherAtSpot(SeekParkPhase::Released, 8.0f));
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

// TestParkNewCycleResets: the next cycle, another fight or a dead source starts over; another source in the same
// cycle does not (the record owns its Son, R2).
static void TestParkNewCycleResets()
{
    const SeekParkRules rules = ParkRules();
    {
        // A new cycle throws the old record away; the new one's source is judged afresh (here too late, so declined).
        SeekParkState state;
        SeekParkInput in = ParkInput(48000);
        CHECK(SeekParkStep(state, rules, in, 1000));
        CHECK(state.phase == SeekParkPhase::Failed);
        SeekParkInput next = in;
        next.cycleNo = 4;
        SeekParkStep(state, rules, next, 2000);
        CHECK(state.phase == SeekParkPhase::Failed);
        CHECK(std::strcmp(state.why, "late") == 0);
        CHECK(state.spent);
        CHECK(state.son == 77);
        CHECK(state.cycleNo == 4);
    }
    {
        // Another source with the same cycle: no change, no reset.
        SeekParkState state;
        SeekParkInput in = ParkInput(48000);
        CHECK(SeekParkStep(state, rules, in, 1000));
        CHECK(state.phase == SeekParkPhase::Failed);
        SeekParkInput other = in;
        other.son = 78;
        other.pickedSon = 78;
        CHECK(!SeekParkStep(state, rules, other, 2000));
        CHECK(state.phase == SeekParkPhase::Failed);
        CHECK(state.son == 77);
    }
    {
        // A release ends with the source's death: None, and the cycle is spent.
        SeekParkState state;
        SeekParkInput in = ParkInput(48001);
        CHECK(SeekParkStep(state, rules, in, 1000));
        in.dueInMs = 40000;
        CHECK(SeekParkStep(state, rules, in, 2000));
        CHECK(state.phase == SeekParkPhase::Release);
        in.sonAlive = false;
        CHECK(SeekParkStep(state, rules, in, 3000));
        CHECK(state.phase == SeekParkPhase::None);
        CHECK(state.spent);
        CHECK(state.son == 77);
    }
}

// TestParkReleaseNeedsReceiver: the release fails after kSeekParkReleaseHoldMs unless the source's victim is a tank,
// whatever receiver the scan found (decision 2). A tank that takes it latches the release instead.
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
        // An eligible receiver that never acquires the source is no release either; with a receiver the window is
        // kSeekParkReleaseMaxMs, not today's 4 s.
        SeekParkState state;
        SeekParkInput in = ParkInput(48001);
        CHECK(SeekParkStep(state, rules, in, 10000));
        in.dueInMs = 40000;
        in.receiverOk = true;
        in.controlled = true;
        CHECK(SeekParkStep(state, rules, in, 10000));
        CHECK(state.phase == SeekParkPhase::Release);
        CHECK(!SeekParkStep(state, rules, in, 17999));
        CHECK(state.phase == SeekParkPhase::Release);
        CHECK(SeekParkStep(state, rules, in, 18000));
        CHECK(state.phase == SeekParkPhase::Failed);
        CHECK(std::strcmp(state.why, "no_receiver") == 0);
    }
    {
        // The source on a tank at the release tick enters the release at once, receiver or not; the latch itself is
        // TestParkReleaseLatches.
        SeekParkState state;
        SeekParkInput in = ParkInput(48001);
        CHECK(SeekParkStep(state, rules, in, 10000));
        in.dueInMs = 40000;
        in.sonOnTank = true;
        CHECK(SeekParkStep(state, rules, in, 10000));
        CHECK(state.phase == SeekParkPhase::Release);
    }
}

// TestParkReleaseLatches: the named failing case. A tank that has the source inside the release's 4 s window
// completes the release for good: the source leaving the tank afterwards (threat, death, retarget) is still a
// successful release, never the after-the-fact failure the review found.
static void TestParkReleaseLatches()
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
        CHECK(state.releaseMs == 10000);
        // The tank takes it inside the window: the release is complete and terminal.
        in.sonOnTank = true;
        CHECK(SeekParkStep(state, rules, in, 11000));
        CHECK(state.phase == SeekParkPhase::Released);
        CHECK(std::strcmp(state.why, "") == 0);
        // The Son is back on a non-tank after the window: still a released park.
        in.sonOnTank = false;
        CHECK(!SeekParkStep(state, rules, in, 14000));
        CHECK(state.phase == SeekParkPhase::Released);
        CHECK(!SeekParkStep(state, rules, in, 60000));
        CHECK(state.phase == SeekParkPhase::Released);
        CHECK(std::strcmp(state.why, "") == 0);
        // The release's other answers stay live (the tank is still dragging the source to the spot).
        CHECK(SeekParkReleaseLive(state.phase));
        CHECK(!SeekParkHolds(state.phase));
        CHECK(SeekParkGatherAtSpot(state.phase, 12.0f));
        CHECK(SeekParkLateLeadMs(state.phase, true, true, 75000, 40000) == 40000);
    }
    {
        // A tank on the tick the window closes still latches: it had the source within the 4 s.
        SeekParkState state;
        SeekParkInput in = ParkInput(48001);
        CHECK(SeekParkStep(state, rules, in, 10000));
        in.dueInMs = 40000;
        CHECK(SeekParkStep(state, rules, in, 10000));
        in.sonOnTank = true;
        CHECK(SeekParkStep(state, rules, in, 14000));
        CHECK(state.phase == SeekParkPhase::Released);
    }
    {
        // Nothing takes it in the 4 s: still the failure (TestParkReleaseNeedsReceiver).
        SeekParkState state;
        SeekParkInput in = ParkInput(48001);
        CHECK(SeekParkStep(state, rules, in, 10000));
        in.dueInMs = 40000;
        CHECK(SeekParkStep(state, rules, in, 10000));
        CHECK(SeekParkStep(state, rules, in, 14000));
        CHECK(state.phase == SeekParkPhase::Failed);
        CHECK(std::strcmp(state.why, "no_receiver") == 0);
    }
    {
        // Another reader's pick never ends the latched release: the record owns its source (R2).
        SeekParkState state;
        SeekParkInput in = ParkInput(48001);
        CHECK(SeekParkStep(state, rules, in, 1000));
        in.dueInMs = 40000;
        in.sonOnTank = true;
        CHECK(SeekParkStep(state, rules, in, 2000));
        CHECK(state.phase == SeekParkPhase::Release);
        CHECK(SeekParkStep(state, rules, in, 3000));
        CHECK(state.phase == SeekParkPhase::Released);
        in.pickedSon = 78;
        CHECK(!SeekParkStep(state, rules, in, 4000));
        CHECK(state.phase == SeekParkPhase::Released);
        CHECK(state.son == 77);
    }
    {
        // The next cycle starts a fresh park from the latched release too.
        SeekParkState state;
        SeekParkInput in = ParkInput(48001);
        CHECK(SeekParkStep(state, rules, in, 1000));
        in.dueInMs = 40000;
        in.sonOnTank = true;
        CHECK(SeekParkStep(state, rules, in, 2000));
        CHECK(SeekParkStep(state, rules, in, 3000));
        CHECK(state.phase == SeekParkPhase::Released);
        in.cycleNo = 4;
        in.dueInMs = 60000;
        CHECK(SeekParkStep(state, rules, in, 4000));
        // The new cycle's record is fresh: the same Son, still fighting with room left, parks again.
        CHECK(state.phase == SeekParkPhase::Apply);
        CHECK(state.son == 77);
        CHECK(state.cycleNo == 4);
    }
}

// TestParkReleaseIgnoresStaleTankVictim: the named failing case (F1, round 5). A creature under Polymorph keeps its
// victim, and that stale victim still targets anything. "On a tank" means a tank victim that targets the Son, with no
// breakable control: a taunt on the sleeping Son completes nothing, and neither does a tank that moved on.
static void TestParkReleaseIgnoresStaleTankVictim()
{
    CHECK(!SeekParkSonOnTank(true, true, true));
    CHECK(!SeekParkSonOnTank(true, false, false));
    CHECK(SeekParkSonOnTank(true, true, false));
    CHECK(!SeekParkSonOnTank(false, true, false));
    CHECK(!SeekParkSonOnTank(false, true, true));
    const SeekParkRules rules = ParkRules();
    SeekParkState state;
    SeekParkInput in = ParkInput(60000);
    CHECK(SeekParkStep(state, rules, in, 1000));
    CHECK(state.phase == SeekParkPhase::Apply);
    in.controlled = true;
    in.controlLeftMs = 41000;
    CHECK(SeekParkStep(state, rules, in, 2000));
    CHECK(state.phase == SeekParkPhase::Held);
    // The release tick: the control holds and the Son's stale victim is a tank.
    in.receiverInReach = true;
    in.dueInMs = 40000;
    in.sonOnTank = SeekParkSonOnTank(true, true, true);
    CHECK(!in.sonOnTank);
    CHECK(SeekParkStep(state, rules, in, 3000));
    CHECK(state.phase == SeekParkPhase::Release);
    // It does not latch while the control holds the Son.
    CHECK(!SeekParkStep(state, rules, in, 4000));
    CHECK(state.phase == SeekParkPhase::Release);
    CHECK(SeekParkReleaseTakeDue(state.phase, SeekParkSonOnTank(true, true, true), true));
    // The control is gone, but that stale tank now targets something else: still no release.
    in.controlled = false;
    in.controlLeftMs = 0;
    in.sonOnTank = SeekParkSonOnTank(true, false, in.controlled);
    CHECK(!in.sonOnTank);
    CHECK(!SeekParkStep(state, rules, in, 5000));
    CHECK(state.phase == SeekParkPhase::Release);
    // The tank engages the Son: the release completes.
    in.sonOnTank = SeekParkSonOnTank(true, true, in.controlled);
    CHECK(in.sonOnTank);
    CHECK(SeekParkStep(state, rules, in, 6000));
    CHECK(state.phase == SeekParkPhase::Released);
}

// TestParkOnePerCycle: F4's re-entries. A cycle that started a park (or declined one) is spent, for any source; the
// next cycle starts clean.
static void TestParkOnePerCycle()
{
    const SeekParkRules rules = ParkRules();
    SeekParkState state;
    SeekParkInput in = ParkInput(48001);
    CHECK(SeekParkStep(state, rules, in, 1000));
    in.dueInMs = 40000;
    in.sonOnTank = true;
    CHECK(SeekParkStep(state, rules, in, 2000));
    CHECK(SeekParkStep(state, rules, in, 3000));
    CHECK(state.phase == SeekParkPhase::Released);
    in.sonAlive = false;
    CHECK(SeekParkStep(state, rules, in, 4000));
    CHECK(state.phase == SeekParkPhase::None);
    CHECK(state.spent);
    // The next Son of the same cycle, with everything a park needs, is no park: the cycle is spent.
    SeekParkInput next = ParkInput(60000);
    next.son = 78;
    next.pickedSon = 78;
    CHECK(!SeekParkStep(state, rules, next, 5000));
    CHECK(state.phase == SeekParkPhase::None);
    // The next cycle starts clean.
    next.cycleNo = 4;
    CHECK(SeekParkStep(state, rules, next, 6000));
    CHECK(state.phase == SeekParkPhase::Apply);
    CHECK(state.son == 78);
    // A declined park spends its cycle too.
    {
        SeekParkState declined;
        SeekParkInput late = ParkInput(48000);
        CHECK(SeekParkStep(declined, rules, late, 1000));
        CHECK(declined.phase == SeekParkPhase::Failed);
        CHECK(std::strcmp(declined.why, "late") == 0);
        CHECK(declined.spent);
        CHECK(declined.son == 77);
    }
}

// TestParkReleaseWaitsForReceiver: F2. Held releases at the lead when the receiver stands in strike reach, else it
// waits until kSeekParkReceiverWaitMs past the lead; Apply (no control) releases at the lead as today.
static void TestParkReleaseWaitsForReceiver()
{
    const SeekParkRules rules = ParkRules();
    {
        SeekParkState state;
        SeekParkInput in = ParkInput(60000);
        CHECK(SeekParkStep(state, rules, in, 1000));
        in.controlled = true;
        CHECK(SeekParkStep(state, rules, in, 2000));
        CHECK(state.phase == SeekParkPhase::Held);
        in.receiverInReach = false;
        in.dueInMs = 40000;
        CHECK(!SeekParkStep(state, rules, in, 3000));
        CHECK(state.phase == SeekParkPhase::Held);
        CHECK(state.leadSinceMs == 3000);
        in.dueInMs = 32001;
        CHECK(!SeekParkStep(state, rules, in, 4000));
        CHECK(state.phase == SeekParkPhase::Held);
        CHECK(state.leadSinceMs == 3000);
        in.dueInMs = 32000;
        CHECK(SeekParkStep(state, rules, in, 5000));
        CHECK(state.phase == SeekParkPhase::Release);
        // The row's waited_ms reads from the first Held read at or under the lead.
        CHECK(state.leadSinceMs == 3000);
    }
    {
        // In reach at the lead: at once, no wait.
        SeekParkState state;
        SeekParkInput in = ParkInput(60000);
        CHECK(SeekParkStep(state, rules, in, 1000));
        in.controlled = true;
        CHECK(SeekParkStep(state, rules, in, 2000));
        in.receiverInReach = true;
        in.dueInMs = 40000;
        CHECK(SeekParkStep(state, rules, in, 3000));
        CHECK(state.phase == SeekParkPhase::Release);
    }
    {
        // Apply (no control on the Son) releases at the lead as today, without a receiver in reach.
        SeekParkState state;
        SeekParkInput in = ParkInput(60000);
        CHECK(SeekParkStep(state, rules, in, 1000));
        CHECK(state.phase == SeekParkPhase::Apply);
        in.dueInMs = 40000;
        CHECK(SeekParkStep(state, rules, in, 2000));
        CHECK(state.phase == SeekParkPhase::Release);
        CHECK(state.leadSinceMs == 0);
    }
    {
        // A break during the wait re-applies, and that Apply releases at once (as today).
        SeekParkState state;
        SeekParkInput in = ParkInput(60000);
        CHECK(SeekParkStep(state, rules, in, 1000));
        in.controlled = true;
        CHECK(SeekParkStep(state, rules, in, 2000));
        in.receiverInReach = false;
        in.dueInMs = 40000;
        CHECK(!SeekParkStep(state, rules, in, 3000));
        CHECK(state.phase == SeekParkPhase::Held);
        in.controlled = false;
        CHECK(SeekParkStep(state, rules, in, 4000));
        CHECK(state.phase == SeekParkPhase::Apply);
        CHECK(state.reapplies == 1);
        CHECK(SeekParkStep(state, rules, in, 5000));
        CHECK(state.phase == SeekParkPhase::Release);
    }
}

// TestParkReleaseWindow: with a receiver the release's window is 8 s; without one, today's 4 s (R10).
static void TestParkReleaseWindow()
{
    const SeekParkRules rules = ParkRules();
    {
        SeekParkState state;
        SeekParkInput in = ParkInput(48001);
        CHECK(SeekParkStep(state, rules, in, 10000));
        in.dueInMs = 40000;
        in.receiverOk = true;
        in.controlled = true;
        CHECK(SeekParkStep(state, rules, in, 10000));
        CHECK(state.phase == SeekParkPhase::Release);
        CHECK(!SeekParkStep(state, rules, in, 17999));
        CHECK(state.phase == SeekParkPhase::Release);
        CHECK(SeekParkStep(state, rules, in, 18000));
        CHECK(state.phase == SeekParkPhase::Failed);
        CHECK(std::strcmp(state.why, "no_receiver") == 0);
    }
    {
        SeekParkState state;
        SeekParkInput in = ParkInput(48001);
        CHECK(SeekParkStep(state, rules, in, 10000));
        in.dueInMs = 40000;
        CHECK(SeekParkStep(state, rules, in, 10000));
        CHECK(!SeekParkStep(state, rules, in, 13999));
        CHECK(state.phase == SeekParkPhase::Release);
        CHECK(SeekParkStep(state, rules, in, 14000));
        CHECK(state.phase == SeekParkPhase::Failed);
        CHECK(std::strcmp(state.why, "no_receiver") == 0);
    }
    // The damage dealers' hold follows the same two windows, the longer one only on a controlled source.
    CHECK(SeekParkDpsHeld(SeekParkPhase::Release, false, 10000, 17999, true, true));
    CHECK(!SeekParkDpsHeld(SeekParkPhase::Release, false, 10000, 18000, true, true));
    CHECK(SeekParkDpsHeld(SeekParkPhase::Release, false, 10000, 13999, false, false));
    CHECK(!SeekParkDpsHeld(SeekParkPhase::Release, false, 10000, 14000, false, false));
}

// TestPickParkReceiver: the kept guid while eligible, else in reach before not, then the nearer, then the lower guid.
static void TestPickParkReceiver()
{
    std::vector<SeekParkReceiverCand> cands;
    CHECK(PickSeekParkReceiver(cands, 0) == -1);
    cands.push_back(SeekParkReceiverCand{false, true, 1.0f, 5});
    CHECK(PickSeekParkReceiver(cands, 0) == -1);
    // The kept guid wins while eligible, even farther away, while no other eligible tank is in reach.
    cands.clear();
    cands.push_back(SeekParkReceiverCand{true, false, 9.0f, 5});
    cands.push_back(SeekParkReceiverCand{true, false, 20.0f, 6});
    CHECK(PickSeekParkReceiver(cands, 6) == 1);
    // Round 7, the named failing case: a kept receiver that cannot close (21 yd) gives way to another eligible tank
    // already in strike reach.
    cands.clear();
    cands.push_back(SeekParkReceiverCand{true, false, 21.0f, 5});
    cands.push_back(SeekParkReceiverCand{true, true, 4.0f, 6});
    CHECK(PickSeekParkReceiver(cands, 5) == 1);
    // With no other eligible tank in reach the kept one still wins, whatever its distance.
    cands.clear();
    cands.push_back(SeekParkReceiverCand{true, false, 21.0f, 5});
    cands.push_back(SeekParkReceiverCand{true, false, 9.0f, 6});
    CHECK(PickSeekParkReceiver(cands, 5) == 0);
    cands.push_back(SeekParkReceiverCand{false, true, 1.0f, 7});
    CHECK(PickSeekParkReceiver(cands, 5) == 0);
    // A kept guid that is no longer eligible is dropped.
    cands.clear();
    cands.push_back(SeekParkReceiverCand{false, true, 2.0f, 5});
    cands.push_back(SeekParkReceiverCand{true, false, 20.0f, 6});
    CHECK(PickSeekParkReceiver(cands, 5) == 1);
    // In reach beats nearer but out.
    cands.clear();
    cands.push_back(SeekParkReceiverCand{true, false, 3.0f, 5});
    cands.push_back(SeekParkReceiverCand{true, true, 9.0f, 6});
    CHECK(PickSeekParkReceiver(cands, 0) == 1);
    // Then the smaller distance.
    cands.clear();
    cands.push_back(SeekParkReceiverCand{true, false, 12.0f, 5});
    cands.push_back(SeekParkReceiverCand{true, false, 8.0f, 6});
    CHECK(PickSeekParkReceiver(cands, 0) == 1);
    // Then the lower raw guid.
    cands.clear();
    cands.push_back(SeekParkReceiverCand{true, false, 8.0f, 9});
    cands.push_back(SeekParkReceiverCand{true, false, 8.0f, 4});
    CHECK(PickSeekParkReceiver(cands, 0) == 1);
    // Nothing eligible: none.
    cands.clear();
    cands.push_back(SeekParkReceiverCand{false, true, 1.0f, 4});
    CHECK(PickSeekParkReceiver(cands, 4) == -1);
}

// TestParkPreWalk: only Held with the due time known, from kSeekParkPreWalkMs before the lead.
static void TestParkPreWalk()
{
    CHECK(SeekParkPreWalk(SeekParkPhase::Held, true, 48000, 40000));
    CHECK(!SeekParkPreWalk(SeekParkPhase::Held, true, 48001, 40000));
    CHECK(!SeekParkPreWalk(SeekParkPhase::Held, false, 40000, 40000));
    CHECK(!SeekParkPreWalk(SeekParkPhase::Apply, true, 48000, 40000));
    CHECK(!SeekParkPreWalk(SeekParkPhase::Release, true, 48000, 40000));
    CHECK(!SeekParkPreWalk(SeekParkPhase::Released, true, 48000, 40000));
    CHECK(!SeekParkPreWalk(SeekParkPhase::None, true, 48000, 40000));
    CHECK(!SeekParkPreWalk(SeekParkPhase::Failed, true, 48000, 40000));
}

// TestParkTankStandsDown: Apply and Held, a tank, the parked source its victim or target.
static void TestParkTankStandsDown()
{
    CHECK(SeekParkTankStandsDown(SeekParkPhase::Apply, true, true));
    CHECK(SeekParkTankStandsDown(SeekParkPhase::Held, true, true));
    CHECK(!SeekParkTankStandsDown(SeekParkPhase::Apply, false, true));
    CHECK(!SeekParkTankStandsDown(SeekParkPhase::Apply, true, false));
    CHECK(!SeekParkTankStandsDown(SeekParkPhase::Release, true, true));
    CHECK(!SeekParkTankStandsDown(SeekParkPhase::Released, true, true));
    CHECK(!SeekParkTankStandsDown(SeekParkPhase::Failed, true, true));
    CHECK(!SeekParkTankStandsDown(SeekParkPhase::None, true, true));
}

// TestParkApplyWalkCap: limit 2. Apply ends kSeekParkWalkCapMs after its budget whether the park bot reached the
// source or not.
static void TestParkApplyWalkCap()
{
    const SeekParkRules rules = ParkRules();
    SeekParkState state;
    SeekParkInput in = ParkInput(60000);
    CHECK(SeekParkStep(state, rules, in, 1000));
    CHECK(state.phase == SeekParkPhase::Apply);
    CHECK(!SeekParkStep(state, rules, in, 13000));
    CHECK(state.phase == SeekParkPhase::Apply);
    CHECK(SeekParkStep(state, rules, in, 13001));
    CHECK(state.phase == SeekParkPhase::Failed);
    CHECK(std::strcmp(state.why, "reach") == 0);
}

// TestParkBotSwapRestartsBudgetOnce: F4. A park bot swap re-opens the budget once (the new bot must walk); a second
// swap does not.
static void TestParkBotSwapRestartsBudgetOnce()
{
    const SeekParkRules rules = ParkRules();
    SeekParkState state;
    SeekParkInput in = ParkInput(60000);
    in.controllerInReach = true;
    CHECK(SeekParkStep(state, rules, in, 1000));
    CHECK(state.phase == SeekParkPhase::Apply);
    CHECK(state.budgetSinceMs == 1000);
    in.parkBot = 6;
    CHECK(!SeekParkStep(state, rules, in, 6000));
    CHECK(state.botSwaps == 1);
    CHECK(state.budgetSinceMs == 6000);
    // The old budget would have failed here (6001 ms).
    CHECK(!SeekParkStep(state, rules, in, 7001));
    CHECK(state.phase == SeekParkPhase::Apply);
    in.parkBot = 7;
    CHECK(!SeekParkStep(state, rules, in, 8000));
    CHECK(state.botSwaps == 1);
    CHECK(state.budgetSinceMs == 6000);
    CHECK(SeekParkStep(state, rules, in, 12001));
    CHECK(state.phase == SeekParkPhase::Failed);
    CHECK(std::strcmp(state.why, "budget") == 0);
}

// TestParkStrikeReach: the receiver's strike reach and the release's wait edge.
static void TestParkStrikeReach()
{
    CHECK(SeekParkStrikeInReach(5.0f));
    CHECK(!SeekParkStrikeInReach(5.1f));
    CHECK(SeekParkReleaseNow(false, 32000, 40000));
    CHECK(!SeekParkReleaseNow(false, 32001, 40000));
    CHECK(SeekParkReleaseNow(true, 40000, 40000));
    // A lead shorter than the wait still fires: the wait is clamped to the lead.
    CHECK(SeekParkReleaseNow(false, 0, 6000));
    CHECK(!SeekParkReleaseNow(false, 1, 6000));
    CHECK(!SeekParkReleaseNow(false, 32000, 6000));
}

// TestParkReleasedHolderKeepsTheSon: the named failing case (round 3). Tank A completes the release, then becomes
// Hakkar's victim; the scan's receiver is now tank B, but the Son still on A is A's: B gets no take. Every other
// tank's park path skips the Son while a tank holds it. Round 7: the latch ends the park's take for good, so a Son
// that leaves the tank after the latch orders none for the receiver either (the named failing case of that round).
static void TestParkReleasedHolderKeepsTheSon()
{
    const SeekParkRules rules = ParkRules();
    SeekParkState state;
    SeekParkInput in = ParkInput(48001);
    CHECK(SeekParkStep(state, rules, in, 10000));
    in.dueInMs = 40000;
    in.sonOnTank = true;
    CHECK(SeekParkStep(state, rules, in, 10000));
    CHECK(state.phase == SeekParkPhase::Release);
    CHECK(SeekParkStep(state, rules, in, 11000));
    CHECK(state.phase == SeekParkPhase::Released);
    const bool releaseLive = SeekParkReleaseLive(state.phase);
    // Tank A is Hakkar's victim, so the scan no longer offers it and tank B is the receiver; A still holds the Son.
    const bool aHolds = true;
    const bool bIsReceiver = true;
    // The holder keeps it: A is the release's tank, its own-source drag is let through, and B's take is not due.
    CHECK(SeekParkReleaseTank(releaseLive, true, aHolds, false));
    CHECK(!SeekParkReleaseTakeDue(state.phase, true, aHolds));
    CHECK(!SeekParkReleaseTank(releaseLive, true, false, bIsReceiver));
    CHECK(!SeekParkReleaseTakeDue(state.phase, true, bIsReceiver));
    CHECK(SeekParkExcludesSource(state.phase, true, false, false));
    // Round 7: the Son moves to a non-tank after the latch, and the receiver's take is still not due.
    CHECK(SeekParkReleaseTank(releaseLive, false, false, bIsReceiver));
    CHECK(!SeekParkReleaseTakeDue(state.phase, false, bIsReceiver));
    CHECK(!SeekParkExcludesSource(state.phase, false, bIsReceiver, false));
    // The open release still orders it, only while no tank has the Son.
    CHECK(SeekParkReleaseTakeDue(SeekParkPhase::Release, false, bIsReceiver));
    CHECK(!SeekParkReleaseTakeDue(SeekParkPhase::Release, false, false));
    CHECK(!SeekParkReleaseTakeDue(SeekParkPhase::Release, true, bIsReceiver));
    // No other phase orders the park's take.
    CHECK(!SeekParkReleaseTakeDue(SeekParkPhase::Held, false, true));
    CHECK(!SeekParkReleaseTakeDue(SeekParkPhase::Apply, false, true));
    CHECK(!SeekParkReleaseTakeDue(SeekParkPhase::Failed, false, true));
    CHECK(!SeekParkReleaseTakeDue(SeekParkPhase::None, false, true));
    // A non-tank victim is no holder and no receiver's Son: only the ordinary takes apply.
    CHECK(!SeekParkReleaseTank(releaseLive, false, false, false));
    // With the release not live, nothing is the release's.
    CHECK(!SeekParkReleaseTank(false, true, true, true));
}

// TestParkRecordKeepsItsSon: F5. A live record is stepped only with its own source's facts: another bot's pick, no
// pick at all, or this bot's pick moving to another Son neither resets it nor drives its phase.
static void TestParkRecordKeepsItsSon()
{
    const SeekParkRules rules = ParkRules();
    {
        SeekParkState state;
        SeekParkInput in = ParkInput(60000);
        CHECK(SeekParkStep(state, rules, in, 1000));
        in.controlled = true;
        CHECK(SeekParkStep(state, rules, in, 2000));
        CHECK(state.phase == SeekParkPhase::Held);
        const std::uint32_t heldSinceMs = state.heldSinceMs;
        // Another bot's pick.
        in.son = 78;
        CHECK(!SeekParkStep(state, rules, in, 3000));
        CHECK(state.phase == SeekParkPhase::Held);
        CHECK(state.son == 77);
        CHECK(state.heldSinceMs == heldSinceMs);
        // No pick at all.
        in.son = 0;
        CHECK(!SeekParkStep(state, rules, in, 4000));
        CHECK(state.phase == SeekParkPhase::Held);
        CHECK(state.son == 77);
        CHECK(state.heldSinceMs == heldSinceMs);
        // This bot's own pick moved to another Son: the record still holds its own.
        in.son = 77;
        in.pickedSon = 78;
        CHECK(!SeekParkStep(state, rules, in, 5000));
        CHECK(state.phase == SeekParkPhase::Held);
        CHECK(state.son == 77);
    }
    {
        // The same in the release and its latch.
        SeekParkState state;
        SeekParkInput in = ParkInput(60000);
        CHECK(SeekParkStep(state, rules, in, 1000));
        in.dueInMs = 40000;
        CHECK(SeekParkStep(state, rules, in, 2000));
        CHECK(state.phase == SeekParkPhase::Release);
        in.son = 78;
        CHECK(!SeekParkStep(state, rules, in, 3000));
        CHECK(state.phase == SeekParkPhase::Release);
        CHECK(state.son == 77);
        in.son = 77;
        in.sonOnTank = true;
        in.pickedSon = 78;
        CHECK(SeekParkStep(state, rules, in, 4000));
        CHECK(state.phase == SeekParkPhase::Released);
        in.son = 78;
        CHECK(!SeekParkStep(state, rules, in, 5000));
        CHECK(state.phase == SeekParkPhase::Released);
        CHECK(state.son == 77);
    }
}

// TestParkFightStartResets: a re-pull with the same cycle number starts over; the fight's engage clock keys the record
// with the cycle, so the new fight's record is judged afresh (this source still has room, so it parks again).
static void TestParkFightStartResets()
{
    const SeekParkRules rules = ParkRules();
    SeekParkState state;
    SeekParkInput in = ParkInput(60000);
    CHECK(SeekParkStep(state, rules, in, 1000));
    CHECK(state.phase == SeekParkPhase::Apply);
    in.fightStartMs = 900;
    SeekParkStep(state, rules, in, 2000);
    CHECK(state.phase == SeekParkPhase::Apply);
    CHECK(state.fightStartMs == 900);
    // The Apply began again at this read: the fight's clock, not the old record's.
    CHECK(state.applySinceMs == 2000);
    CHECK(state.spent);
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
    CHECK(std::strcmp(SeekParkPhaseWord(SeekParkPhase::Released), "released") == 0);
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
    TestParkSourceHitAllowed();
    TestParkDpsHeldAfterRelease();
    TestParkGatherAtSpot();
    TestPickParkBot();
    TestParkNewCycleResets();
    TestParkReleaseNeedsReceiver();
    TestParkReleaseLatches();
    TestParkReleaseIgnoresStaleTankVictim();
    TestParkOnePerCycle();
    TestParkReleaseWaitsForReceiver();
    TestParkReleaseWindow();
    TestPickParkReceiver();
    TestParkPreWalk();
    TestParkTankStandsDown();
    TestParkApplyWalkCap();
    TestParkBotSwapRestartsBudgetOnce();
    TestParkStrikeReach();
    TestParkOnlyTheReceiverInRelease();
    TestParkBrokenControlReleasesToday();
    TestParkFailedTakeDue();
    TestParkFailedParkBreaker();
    TestParkStaleReadKeepsTheLatch();
    TestParkStaleTickDropsTheUpdate();
    TestParkCommitKeepsTheLatch();
    TestParkSingleReaderProgresses();
    TestParkRejectedTickPublishesNothing();
    TestParkReleasedHolderKeepsTheSon();
    TestParkRecordKeepsItsSon();
    TestParkFightStartResets();
    TestParkExemptsBot();
    TestParkPhaseWords();
    std::cout << "All seek-park policy tests passed.\n";
    return 0;
}
