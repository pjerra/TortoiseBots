// Standalone tests for runtime/StandWindowMarks.h: the countdown of a boss's stand window per unit. Two instance copies
// of a map can hand a creature the same raw guid, so the key carries the map and the instance; a window that disappears
// without a closing observation (the unit leaves the report) is pruned after 30 s, a binding's teardown forgets the keys
// it reported even when they are fresh, and a mark that outlived a prune never times the next encounter.
//
// Standard library only, no game header.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra -Werror=return-type tools/test_stand_window_marks.cpp -o /tmp/test_stand_window_marks
//   /tmp/test_stand_window_marks

#include "../runtime/StandWindowMarks.h"

#include <cstdio>
#include <cstdlib>

using namespace TortoiseBots::StandWindow;

namespace {

int g_failures = 0;

void Check(bool ok, char const* what)
{
    if (!ok)
    {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++g_failures;
    }
}

// A creature raw guid as both copies of an instance see it.
constexpr uint64_t kGuid = 0x0000000100000F00ull;
constexpr uint32_t kTotal = 5000;

Key Make(uint32_t map, uint32_t instance, uint64_t guid = kGuid)
{
    return Key{map, instance, guid};
}

// Two instance copies of the same map report the same raw guid: each copy has its own countdown, and opening a window in
// the second copy does not inherit the first copy's start.
void TestInstanceCopiesDoNotCollide()
{
    Marks marks;
    Key const first = Make(249, 1);
    Key const second = Make(249, 2);
    Check(first != second, "two copies of one map are different keys");
    Check(marks.CastLeft(first, 1000, kTotal) == 5000, "the first copy starts its own countdown");
    Check(marks.CastLeft(first, 2000, kTotal) == 4000, "the first copy counts down");
    Check(marks.Size() == 1, "the first copy holds one mark");
    Check(marks.CastLeft(second, 2000, kTotal) == 5000,
        "the second copy's window at the same raw guid starts fresh, not 4000 ms in");
    Check(marks.Size() == 2, "each copy holds its own mark");
    Check(marks.CastLeft(second, 3000, kTotal) == 4000, "the second copy counts down from its own start");
    Check(marks.CastLeft(first, 3000, kTotal) == 3000, "the first copy kept its own start");
    // A different map at the same instance id and guid is not the same unit.
    Check(marks.CastLeft(Make(70, 1), 3000, kTotal) == 5000, "a map is part of the key");
    Check(marks.CastLeft(Make(249, 1, kGuid + 1), 3000, kTotal) == 5000, "a guid is part of the key");
}

// A window disappears without a closing observation: nothing forgets it, so a mark not seen for 30 s is pruned by the
// snapshot cycle. Until then it stays (a window's own countdown is shorter than the prune age).
void TestAnUnobservedWindowIsPruned()
{
    Marks marks;
    Key const key = Make(249, 1);
    Check(marks.CastLeft(key, 0, kTotal) == 5000, "the window opens");
    marks.Prune(kStaleMs);
    Check(marks.Size() == 1, "a mark seen at 0 survives a prune at exactly 30 s");
    marks.Prune(kStaleMs + 1);
    Check(marks.Size() == 0, "a mark not observed for more than 30 s is pruned");
    // The pruned window is gone: a later observation is a new window, never a countdown that already ran out.
    Check(marks.CastLeft(key, kStaleMs + 1, kTotal) == 5000, "the next window at that key starts fresh");
    // An observed mark keeps its start and stays.
    for (uint32_t at = 0; at <= 60000; at += 2000)
        marks.CastLeft(key, at, kTotal);
    marks.Prune(62000);
    Check(marks.Size() == 1, "a window observed every 2 s is never pruned");
}

// The prune clock counts from the last observation, not from the window's start: a long window still being reported is
// not dropped part-way.
void TestAnObservedLongWindowSurvives()
{
    Marks marks;
    Key const key = Make(249, 1);
    marks.CastLeft(key, 0, 40000);
    Check(marks.CastLeft(key, 20000, 40000) == 20000, "the countdown reaches the second half");
    marks.Prune(20000);
    Check(marks.Size() == 1, "a window observed 20 s in is still held");
    Check(marks.CastLeft(key, 40000, 40000) == 0, "the countdown clamps at zero, never wraps");
    marks.Prune(40000);
    Check(marks.Size() == 1, "an observation at 40 s refreshes the prune clock");
}

// A closing observation forgets that unit only (the binding's other units, and the other instance copy, keep theirs).
void TestForgetIsPerUnit()
{
    Marks marks;
    Key const first = Make(249, 1);
    Key const second = Make(249, 2);
    Key const other = Make(249, 1, kGuid + 1);
    marks.CastLeft(first, 0, kTotal);
    marks.CastLeft(second, 0, kTotal);
    marks.CastLeft(other, 0, kTotal);
    marks.Forget(first);
    Check(marks.Size() == 2, "a closing observation forgets only its own mark");
    Check(marks.CastLeft(second, 1000, kTotal) == 4000, "the other instance copy kept its countdown");
    Check(marks.CastLeft(other, 1000, kTotal) == 4000, "the other unit kept its countdown");
    marks.Clear();
    Check(marks.Size() == 0, "a teardown clears what is left");
}

// A stale mark that a missed prune left behind is never reused: seeing that key again starts a new window.
void TestAStaleMarkNeverTimesTheNextEncounter()
{
    Marks marks;
    Key const key = Make(249, 1);
    marks.CastLeft(key, 0, kTotal);
    Check(marks.CastLeft(key, kStaleMs + 1, kTotal) == 5000,
        "a window seen again after 30 s is a new encounter, not the end of the old countdown");
    marks.CastLeft(key, kStaleMs + 2, kTotal);
    Check(marks.CastLeft(key, kStaleMs + 3, kTotal) == 4998, "the new window counts down from its own start");
}

// The countdown itself: total left on the first sighting, one second less every second, zero from the total on.
void TestCountdown()
{
    Marks marks;
    Key const key = Make(249, 1);
    Check(marks.CastLeft(key, 12345, 5000) == 5000, "the first sighting has the whole cast left");
    Check(marks.CastLeft(key, 13345, 5000) == 4000, "a second later 4 s are left");
    Check(marks.CastLeft(key, 17345, 5000) == 0, "the cast is over at its total");
    Check(marks.CastLeft(key, 17346, 5000) == 0, "and stays at zero");
}

// Two bindings each report a fresh window, and one is released: its key is forgotten at once, while the other binding's
// fresh mark keeps counting. The prune alone would keep both (neither is 30 s old yet), and a released binding's mark
// must not time a later encounter.
void TestOwnerTeardownForgetsOnlyItsOwnFreshMarks()
{
    OwnedMarks marks;
    constexpr uint64_t bindingA = 0x0000000100000001ull;
    constexpr uint64_t bindingB = 0x0000000200000002ull;
    Key const bossA = Make(249, 1);
    Key const bossB = Make(249, 1, kGuid + 1);
    marks.CastLeft(bindingA, bossA, 0, kTotal);
    marks.CastLeft(bindingB, bossB, 0, kTotal);
    Check(marks.Size() == 2, "both bindings' windows are timed");
    Check(marks.OwnerSize(bindingA) == 1 && marks.OwnerSize(bindingB) == 1, "each mark has its own binding");
    marks.ForgetOwner(bindingA);
    Check(marks.Size() == 1, "the released binding's fresh mark is forgotten, the other stays");
    Check(marks.OwnerSize(bindingA) == 0, "the released binding owns nothing");
    Check(marks.CastLeft(bindingB, bossB, 1000, kTotal) == 4000, "the other binding's countdown continues");
    Check(marks.CastLeft(bindingA, bossA, 1000, kTotal) == 5000, "the released binding's unit starts a new window");
    marks.ForgetOwner(bindingB);
    Check(marks.Size() == 1, "the second teardown leaves only the new window of the first binding's unit");
    marks.ForgetOwner(bindingB);
    Check(marks.Size() == 1, "forgetting a binding twice changes nothing");
}

// One unit reported by two bindings keeps one countdown: releasing one drops its claim, not the other binding's mark.
void TestASharedUnitSurvivesOneBindingsTeardown()
{
    OwnedMarks marks;
    constexpr uint64_t bindingA = 0x0000000100000001ull;
    constexpr uint64_t bindingB = 0x0000000200000002ull;
    Key const key = Make(249, 1);
    marks.CastLeft(bindingA, key, 0, kTotal);
    marks.CastLeft(bindingB, key, 2000, kTotal);
    Check(marks.Size() == 1, "two reports of one unit hold one mark");
    Check(marks.OwnerSize(bindingA) == 1 && marks.OwnerSize(bindingB) == 1, "both bindings claim the mark");
    marks.ForgetOwner(bindingA);
    Check(marks.Size() == 1, "the other binding still reports the window, so its mark stays");
    Check(marks.OwnerSize(bindingA) == 0, "the released binding no longer claims it");
    Check(marks.CastLeft(bindingB, key, 3000, kTotal) == 2000, "the shared countdown did not restart");
    marks.ForgetOwner(bindingB);
    Check(marks.Size() == 0, "the last binding's teardown forgets the mark");
}

// The prune drops the claims of a stale mark too: a binding that stopped reporting a unit owns nothing once its mark
// ages out, and a closed window forgets every binding's claim on it.
void TestPruneAndForgetDropTheOwnersClaims()
{
    OwnedMarks marks;
    constexpr uint64_t binding = 0x0000000100000001ull;
    Key const key = Make(249, 1);
    marks.CastLeft(binding, key, 0, kTotal);
    marks.Prune(kStaleMs + 1);
    Check(marks.Size() == 0, "the stale mark is pruned");
    Check(marks.OwnerSize(binding) == 0, "and its binding's claim goes with it");
    marks.CastLeft(binding, key, kStaleMs + 2, kTotal);
    marks.Forget(key);
    Check(marks.Size() == 0, "a closing observation forgets the mark");
    Check(marks.OwnerSize(binding) == 0, "and every binding's claim on it");
    marks.CastLeft(kNoOwner, key, 0, kTotal);
    marks.ForgetOwner(kNoOwner);
    Check(marks.Size() == 0, "a report outside a binding owns its mark until it is forgotten");
}

} // namespace

int main()
{
    TestInstanceCopiesDoNotCollide();
    TestAnUnobservedWindowIsPruned();
    TestAnObservedLongWindowSurvives();
    TestForgetIsPerUnit();
    TestAStaleMarkNeverTimesTheNextEncounter();
    TestCountdown();
    TestOwnerTeardownForgetsOnlyItsOwnFreshMarks();
    TestASharedUnitSurvivesOneBindingsTeardown();
    TestPruneAndForgetDropTheOwnersClaims();
    if (g_failures)
    {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return EXIT_FAILURE;
    }
    std::puts("test_stand_window_marks: all checks passed");
    return EXIT_SUCCESS;
}
