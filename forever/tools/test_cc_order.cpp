// Standalone test of the crowd-control clear's core-free half (forever/src/host/TbCcPure.h; G8 review fix: a clear that is refused changes nothing, and a
// teardown that cannot reach its bot stays pending):
//   g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/test_cc_order.cpp -o /tmp/test_cc_order && /tmp/test_cc_order
// The fakes below are the two accessors TbCc.cpp fills with the bot's `rti cc` / `run cc family` values and the party's raid icons; each can be made to
// drop its write, which is the read-back that does not take.
#include "forever/src/host/TbCcPure.h"

#include <cstdio>
#include <string>

namespace cc = TortoiseBots::h8;

static int g_checks = 0, g_fail = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

struct FakeValues
{
    std::string mark, family;
    bool dropCc = false, dropFamily = false;

    std::string GetCc() { return mark; }
    std::string GetFamily() { return family; }
    void SetCc(std::string const& text) { if (!dropCc) mark = text; }
    void SetFamily(std::string const& text) { if (!dropFamily) family = text; }
};

struct FakeIcons
{
    int slots[8] = {};
    int before[8] = {};
    bool dropSet = false;

    int Get(int index) { return slots[index]; }
    void Set(int index, int guid) { if (!dropSet) slots[index] = guid; }
    void Snapshot() { for (int i = 0; i < 8; ++i) before[i] = slots[i]; }
    void Restore() { for (int i = 0; i < 8; ++i) slots[i] = before[i]; }
};

static cc::CcClearPlan<int> Plan(int icon, int held)
{
    cc::CcClearPlan<int> plan;
    plan.icon = icon;
    plan.held = held;
    return plan;
}

// The icon a clear takes down, decided before any write.
static void TestTarget()
{
    cc::CcClearTarget const named = cc::CcClearTargetOf(true, true, 4, -1);
    CHECK(named.icon == 4 && !named.own && named.needsGroup);
    // A mark with no unit is a values-only reset: it needs no party and takes no icon down.
    cc::CcClearTarget const markOnly = cc::CcClearTargetOf(true, false, 4, -1);
    CHECK(markOnly.icon == -1 && !markOnly.own && !markOnly.needsGroup);
    // A bare clear takes the icon of the bot's own mark, whatever it holds, and needs no party for it.
    cc::CcClearTarget const bare = cc::CcClearTargetOf(false, false, -1, 3);
    CHECK(bare.icon == 3 && bare.own && !bare.needsGroup);
    cc::CcClearTarget const none = cc::CcClearTargetOf(false, false, -1, -1);
    CHECK(none.icon == -1 && none.own && !none.needsGroup);
}

static void TestSuccess()
{
    FakeValues values{ "moon", "polymorph" };
    FakeIcons icons;
    icons.slots[4] = 77;
    CHECK(cc::ClearCcState(values, icons, Plan(4, 77)) == cc::CcClearFailure::None);
    CHECK(values.mark == "none" && values.family == "none");
    CHECK(icons.slots[4] == 0);
    CHECK(icons.slots[0] == 0 && icons.slots[7] == 0);
}

static void TestSuccessValuesOnly()
{
    FakeValues values{ "moon", "polymorph" };
    FakeIcons icons;
    icons.slots[4] = 77;
    CHECK(cc::ClearCcState(values, icons, Plan(-1, 0)) == cc::CcClearFailure::None);
    CHECK(values.mark == "none" && values.family == "none");
    CHECK(icons.slots[4] == 77);   // a values-only reset touches no icon
}

// The icon removal does not take: both values must be exactly what they were.
static void TestIconReadBackFails()
{
    FakeValues values{ "moon", "polymorph" };
    FakeIcons icons;
    icons.slots[4] = 77;
    icons.dropSet = true;
    CHECK(cc::ClearCcState(values, icons, Plan(4, 77)) == cc::CcClearFailure::Icon);
    CHECK(values.mark == "moon" && values.family == "polymorph");
    CHECK(icons.slots[4] == 77);
}

// One value does not reset: the other value and the icon go back too.
static void TestValueReadBackFails()
{
    FakeValues values{ "moon", "polymorph" };
    FakeIcons icons;
    icons.slots[4] = 77;
    values.dropCc = true;
    CHECK(cc::ClearCcState(values, icons, Plan(4, 77)) == cc::CcClearFailure::Values);
    CHECK(values.mark == "moon" && values.family == "polymorph");
    CHECK(icons.slots[4] == 77);

    FakeValues familyOnly{ "moon", "polymorph" };
    FakeIcons untouched;
    untouched.slots[4] = 77;
    familyOnly.dropFamily = true;
    CHECK(cc::ClearCcState(familyOnly, untouched, Plan(-1, 0)) == cc::CcClearFailure::Values);
    CHECK(familyOnly.mark == "moon" && familyOnly.family == "polymorph");
    CHECK(untouched.slots[4] == 77);
}

// The fail-to-reset report comes after the icon's own read-back, and a clear that never wrote an icon leaves every slot alone.
static void TestIconFirstThenValues()
{
    FakeValues values{ "moon", "polymorph" };
    FakeIcons icons;
    icons.slots[4] = 77;
    icons.dropSet = true;
    values.dropCc = true;
    CHECK(cc::ClearCcState(values, icons, Plan(4, 77)) == cc::CcClearFailure::Icon);
    CHECK(values.mark == "moon" && values.family == "polymorph");
}

// ---- the run's record of the bots its own `cc` orders marked ----
static void TestOwned()
{
    std::map<std::string, std::string> owned;
    cc::CcOwnedRecord(owned, "Mmage", "moon");
    cc::CcOwnedRecord(owned, "Mrogue", "square");
    CHECK(owned.size() == 2 && owned["Mmage"] == "moon");
    cc::CcOwnedRecord(owned, "Mmage", "cross");
    CHECK(owned["Mmage"] == "cross");
    cc::CcOwnedForget(owned, "Mrogue");
    CHECK(owned.size() == 1 && owned.count("Mrogue") == 0);

    // Still the run's mark: the recorded one, or none (already cleared / taken off the mark by another order).
    CHECK(cc::CcMarkOwned("moon", "moon"));
    CHECK(cc::CcMarkOwned("moon", ""));
    // Another mark is a later order's: the run leaves it.
    CHECK(!cc::CcMarkOwned("moon", "cross"));
}

// ---- a clear the teardown could not take stays pending until an attempt settles it (TbRun.cpp ClearRunCc / RetryPendingCc) ----
static void TestTeardownRetries()
{
    std::map<std::string, std::string> pending;
    cc::CcOwnedRecord(pending, "Mmage", "moon");

    // The bot logged out before the run ended: nothing can be read or written, so its entry survives the pass.
    cc::CcTeardownAttempt(pending, "Mmage", false, false, false);
    CHECK(pending.size() == 1 && pending["Mmage"] == "moon");

    // Back in the world, but the clear is refused (no engine, or a write that did not read back): still pending.
    cc::CcTeardownAttempt(pending, "Mmage", true, true, false);
    CHECK(pending.size() == 1 && pending["Mmage"] == "moon");

    // Another pass, now the clear takes: settled, the entry goes.
    cc::CcTeardownAttempt(pending, "Mmage", true, true, true);
    CHECK(pending.empty());

    // Nothing of the run's is left (another order's mark, or none): settled without a clear.
    cc::CcOwnedRecord(pending, "Mrogue", "square");
    cc::CcTeardownAttempt(pending, "Mrogue", true, false, false);
    CHECK(pending.empty());

    // Out of the world, an unreadable mark settles nothing either.
    cc::CcOwnedRecord(pending, "Mpriest", "circle");
    cc::CcTeardownAttempt(pending, "Mpriest", false, false, true);
    CHECK(pending.size() == 1 && pending["Mpriest"] == "circle");
}

int main()
{
    TestTarget();
    TestSuccess();
    TestSuccessValuesOnly();
    TestIconReadBackFails();
    TestValueReadBackFails();
    TestIconFirstThenValues();
    TestOwned();
    TestTeardownRetries();
    if (g_fail)
    {
        std::printf("FAILED: %d of %d checks\n", g_fail, g_checks);
        return 1;
    }
    std::printf("cc order: %d checks passed\n", g_checks);
    return 0;
}
