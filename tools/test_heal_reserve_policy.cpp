// Standalone check for ai/playerbot/HealReservePolicy.h:
//   g++ -std=c++17 -Wall -Wextra tools/test_heal_reserve_policy.cpp -o /tmp/test_heal_reserve_policy && /tmp/test_heal_reserve_policy
#include "../ai/playerbot/HealReservePolicy.h"
#include "../ai/playerbot/HealingTriagePolicy.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using namespace ai;

// The fight every one-shot read below belongs to.
static const HealReserveFightId kLiveFight{101, 5000};

static HealReserveRead Live(float reservePct, float windowSec, std::uint32_t dueInMs,
                            std::uint32_t distress = 0, std::uint32_t hurt = 0)
{
    HealReserveLatch latch;
    HealReserveRead read = HealReservePick(std::vector<HealReserveRow>{{reservePct, windowSec, 14834, distress}},
                                           true, dueInMs, hurt);
    HealReserveApplyDistressLatch(read, latch, hurt, 0, kLiveFight);
    return read;
}

// The named failing case: outside the release window the top-up needs the row's reserve (85 %), so 84 % of mana is
// refused and 85 % allowed; inside the window (due in 60 s or less) today's MediumMana rule (40) decides.
static void TestTopUpHeldUnderReserveOutsideWindow()
{
    const HealReserveRead outside = Live(85.0f, 60.0f, 70000);
    CHECK(outside.live);
    CHECK(!outside.released);
    CHECK(!HealTopUpAllowedAtHealth(true, outside, 84, 40, 75.0f, 70.0f, 90.0f, false));
    CHECK(HealTopUpAllowedAtHealth(true, outside, 85, 40, 75.0f, 70.0f, 90.0f, false));

    const HealReserveRead released = Live(85.0f, 60.0f, 60000);
    CHECK(released.released);
    CHECK(HealTopUpAllowedAtHealth(true, released, 41, 40, 75.0f, 70.0f, 90.0f, false));
    CHECK(!HealTopUpAllowedAtHealth(true, released, 39, 40, 75.0f, 70.0f, 90.0f, false));
}

// A tank or the boss's current victim is on today's rule whatever the reserve.
static void TestTopUpExemptTarget()
{
    const HealReserveRead outside = Live(85.0f, 60.0f, 70000);
    CHECK(HealTopUpAllowedAtHealth(true, outside, 41, 40, 75.0f, 70.0f, 90.0f, true));
    CHECK(!HealTopUpAllowedAtHealth(true, outside, 39, 40, 75.0f, 70.0f, 90.0f, true));
}

// The key off is exactly today's rule, whatever the row says.
static void TestTopUpFlagOffIsToday()
{
    const HealReserveRead outside = Live(85.0f, 60.0f, 70000);
    CHECK(HealTopUpAllowedAtHealth(false, outside, 40, 40, 75.0f, 70.0f, 90.0f, false));
    CHECK(!HealTopUpAllowedAtHealth(false, outside, 39, 40, 75.0f, 70.0f, 90.0f, false));
    const HealReserveRead released = Live(85.0f, 60.0f, 1000);
    CHECK(!HealTopUpAllowedAtHealth(false, released, 39, 40, 75.0f, 70.0f, 90.0f, false));
    const HealReserveRead none;
    CHECK(HealTopUpAllowedAtHealth(true, none, 40, 40, 75.0f, 70.0f, 90.0f, false));
    CHECK(!HealTopUpAllowedAtHealth(true, none, 39, 40, 75.0f, 70.0f, 90.0f, false));
}

// The named failing case (walk 75, D2): outside the release window the reserve lets go while the row's distress count
// of living non-tanks sit under the medium line. Two hurt members release it before the medium tier's own rule, so 41 %
// of mana is allowed and 39 % refused; one hurt member is routine and the 85 % reserve stands.
static void TestDistressReleasesOutsideWindow()
{
    const HealReserveRead hurt = Live(85.0f, 60.0f, 70000, 2, 2);
    CHECK(hurt.live);
    CHECK(hurt.released);
    CHECK(hurt.distressReleased);
    CHECK(!hurt.windowReleased);
    CHECK(HealReserveWhy(hurt) == std::string("distress"));
    CHECK(HealTopUpAllowedAtHealth(true, hurt, 41, 40, 75.0f, 70.0f, 90.0f, false));
    CHECK(!HealTopUpAllowedAtHealth(true, hurt, 39, 40, 75.0f, 70.0f, 90.0f, false));

    const HealReserveRead one = Live(85.0f, 60.0f, 70000, 2, 1);
    CHECK(!one.released);
    CHECK(!one.distressReleased);
    CHECK(!HealTopUpAllowedAtHealth(true, one, 84, 40, 75.0f, 70.0f, 90.0f, false));
    CHECK(HealTopUpAllowedAtHealth(true, one, 85, 40, 75.0f, 70.0f, 90.0f, false));
}

// The opening is held: no exemption was added for the first seconds of the fight, so the pull (due in 89 s) refuses
// the 84 % top-up with nobody hurt.
static void TestOpeningHoldIsKept()
{
    const HealReserveRead opening = Live(85.0f, 60.0f, 89000, 2, 0);
    CHECK(opening.live);
    CHECK(!opening.released);
    CHECK(HealReserveWhy(opening) == std::string("none"));
    CHECK(!HealTopUpAllowedAtHealth(true, opening, 84, 40, 75.0f, 70.0f, 90.0f, false));
}

// The distress release needs no clock: it works with the seek key off and before the first sighting.
static void TestDistressNeedsNoClock()
{
    const std::vector<HealReserveRow> rows{{85.0f, 60.0f, 14834, 2}};
    const HealReserveRead hurt = HealReservePick(rows, false, 0, 2);
    CHECK(hurt.live);
    CHECK(!hurt.clockKnown);
    CHECK(hurt.distressReleased);
    CHECK(hurt.released);
    CHECK(HealReserveWhy(hurt) == std::string("distress"));
    const HealReserveRead one = HealReservePick(rows, false, 0, 1);
    CHECK(!one.distressReleased);
    CHECK(!one.released);
    const HealReserveRead noLead = HealReservePick(std::vector<HealReserveRow>{{85.0f, 0.0f, 14834, 2}}, true, 0, 2);
    CHECK(!noLead.windowReleased);
    CHECK(noLead.released);
}

// A distress count of 0 is walk 75: the reserve never releases on hurt members.
static void TestDistressZeroIsWalk75()
{
    const HealReserveRead read = Live(85.0f, 60.0f, 70000, 0, 20);
    CHECK(read.live);
    CHECK(!read.distressReleased);
    CHECK(!read.released);
    CHECK(read.hurtCount == 20);
    CHECK(HealReserveWhy(read) == std::string("none"));
}

// The named failing case of this round: the distress count read raw health, so two non-tanks at 60 % raw that the
// incoming damage takes below the line never released the reserve, although the heal path already calls them hurt.
// Counted on the same effective health the heal pick triages on, they are two hurt members and the release fires.
static void TestDistressCountReadsEffectiveHealth()
{
    const float medium = 60.0f;
    HealReserveCounts counts;
    HealReserveCountMember(counts, HealReserveMember{true, true, false, 55.0f}, medium); // raw 60 %, predicted 55 %
    HealReserveCountMember(counts, HealReserveMember{true, true, false, 58.0f}, medium); // raw 60 %, predicted 58 %
    CHECK(counts.hurt == 2);
    CHECK(counts.calm == 2);

    HealReserveLatch latch;
    HealReserveRead read = HealReservePick({{85.0f, 60.0f, 14834, 2}}, true, 70000, counts.hurt);
    HealReserveApplyDistressLatch(read, latch, counts.calm, 1000, kLiveFight);
    CHECK(read.released);
    CHECK(read.distressReleased);
    CHECK(!read.windowReleased);
    CHECK(HealReserveWhy(read) == std::string("distress"));
    CHECK(HealTopUpAllowedAtHealth(true, read, 41, 40, 75.0f, 70.0f, 90.0f, false));
    CHECK(!HealTopUpAllowedAtHealth(true, read, 39, 40, 75.0f, 70.0f, 90.0f, false));

    // Only living non-tanks on the healer's map count, and a member above the line but under the margin counts calm.
    HealReserveCounts filtered;
    HealReserveCountMember(filtered, HealReserveMember{false, true, false, 10.0f}, medium);
    HealReserveCountMember(filtered, HealReserveMember{true, false, false, 10.0f}, medium);
    HealReserveCountMember(filtered, HealReserveMember{true, true, true, 10.0f}, medium);
    CHECK(filtered.hurt == 0 && filtered.calm == 0);
    HealReserveCountMember(filtered, HealReserveMember{true, true, false, 65.0f}, medium);
    CHECK(filtered.hurt == 0);
    CHECK(filtered.calm == 1);
}

// Released, then one non-tank recovers above the line plus the margin: the release holds until 3 s of calm have
// passed, so a hurt count hovering on the line cannot flap the reserve.
static void TestReleasedHoldsForThreeSeconds()
{
    const std::vector<HealReserveRow> rows{{85.0f, 60.0f, 14834, 2}};
    HealReserveLatch latch;
    HealReserveRead read = HealReservePick(rows, true, 70000, 2);
    HealReserveApplyDistressLatch(read, latch, 2, 1000, kLiveFight);
    CHECK(read.released);

    // At 3 s only one member is under the line: the calm run starts.
    read = HealReservePick(rows, true, 70000, 1);
    HealReserveApplyDistressLatch(read, latch, 1, 3000, kLiveFight);
    CHECK(read.released);
    CHECK(latch.calm);
    // Under 3 s of calm the release still holds.
    read = HealReservePick(rows, true, 70000, 1);
    HealReserveApplyDistressLatch(read, latch, 1, 5999, kLiveFight);
    CHECK(read.released);
    CHECK(read.distressReleased);
    CHECK(HealReserveWhy(read) == std::string("distress"));
    // 3 s of calm: the reserve applies again.
    read = HealReservePick(rows, true, 70000, 1);
    HealReserveApplyDistressLatch(read, latch, 1, 6000, kLiveFight);
    CHECK(!read.released);
    CHECK(!read.distressReleased);
    CHECK(HealReserveWhy(read) == std::string("none"));
    CHECK(!HealTopUpAllowedAtHealth(true, read, 84, 40, 75.0f, 70.0f, 90.0f, false));
    CHECK(HealTopUpAllowedAtHealth(true, read, 85, 40, 75.0f, 70.0f, 90.0f, false));

    // A member above the line but still under the margin keeps the release without starting the calm clock.
    read = HealReservePick(rows, true, 70000, 2);
    HealReserveApplyDistressLatch(read, latch, 2, 7000, kLiveFight);
    CHECK(read.released);
    read = HealReservePick(rows, true, 70000, 0);
    HealReserveApplyDistressLatch(read, latch, 2, 20000, kLiveFight);
    CHECK(read.released);
    CHECK(!latch.calm);
    read = HealReservePick(rows, true, 70000, 0);
    HealReserveApplyDistressLatch(read, latch, 2, 40000, kLiveFight);
    CHECK(read.released);
}

// The latch is per fight, keyed by the boss and the fight's combat start: a released fight hands nothing to the next
// one, even when the new fight's first read arrives directly and no call ever saw an empty row set.
static void TestLatchResetsOnANewFight()
{
    const std::vector<HealReserveRow> rows{{85.0f, 60.0f, 14834, 2}};
    HealReserveLatch latch;
    HealReserveRead read = HealReservePick(rows, true, 70000, 2);
    HealReserveApplyDistressLatch(read, latch, 2, 1000, kLiveFight);
    CHECK(latch.released);

    // Straight to a new pull of the same boss: only the fight's start changed.
    const HealReserveFightId nextPull{101, 9000};
    read = HealReservePick(rows, true, 70000, 1);
    HealReserveApplyDistressLatch(read, latch, 1, 2000, nextPull);
    CHECK(!latch.released);
    CHECK(!latch.calm);
    CHECK(!read.released);
    CHECK(HealReserveWhy(read) == std::string("none"));

    // Another boss guid is another fight too, even with the same start.
    read = HealReservePick(rows, true, 70000, 2);
    HealReserveApplyDistressLatch(read, latch, 2, 3000, nextPull);
    CHECK(latch.released);
    const HealReserveFightId otherBoss{102, 9000};
    read = HealReservePick(rows, true, 70000, 1);
    HealReserveApplyDistressLatch(read, latch, 1, 4000, otherBoss);
    CHECK(!latch.released);

    // A read with no live row is the fight's end as well, and the next fight starts unlatched.
    read = HealReservePick(rows, true, 70000, 2);
    HealReserveApplyDistressLatch(read, latch, 2, 5000, otherBoss);
    CHECK(latch.released);
    HealReserveRead none;
    HealReserveApplyDistressLatch(none, latch, 1, 6000, otherBoss);
    CHECK(!latch.released);
    CHECK(!latch.calm);
    read = HealReservePick(rows, true, 70000, 1);
    HealReserveApplyDistressLatch(read, latch, 1, 7000, otherBoss);
    CHECK(!read.released);

    // Its own distress fires the release again.
    read = HealReservePick(rows, true, 70000, 2);
    HealReserveApplyDistressLatch(read, latch, 2, 8000, otherBoss);
    CHECK(read.released);
    HealReserveLatchReset(latch);
    CHECK(!latch.released);
}

// The D6 stale clear keeps running when only the pick counts refusals and no trigger row moves the window's stamp: the
// count stays within one window instead of growing for the rest of the fight.
static void TestPickOnlyRefusalsStayBounded()
{
    const std::uint32_t gap = 5000;
    std::uint32_t stamp = 1000; // the last row the trigger wrote before the healer stopped topping up
    std::uint32_t refused = 0;
    std::uint32_t most = 0;
    for (std::uint32_t now = 2000; now <= 300000; now += 200)
    {
        if (HealReserveRefusalStale(stamp, now, gap))
        {
            stamp = HealReserveRefusalRestamp(now, gap);
            refused = 0;
        }
        ++refused;
        most = std::max(most, refused);
    }
    // The first window can be the full two gaps old when the trigger stopped; after a clear the count restarts and the
    // clear repeats every gap. Either way the count never runs on for the rest of the fight.
    CHECK(most <= 2 * gap / 200 + 2);
}

// The window wins the label when both releases hold.
static void TestWhyWindowWins()
{
    const HealReserveRead read = Live(85.0f, 60.0f, 60000, 2, 5);
    CHECK(read.windowReleased);
    CHECK(read.distressReleased);
    CHECK(read.released);
    CHECK(HealReserveWhy(read) == std::string("window"));
}

// No clock, or a lead of 0, never releases the reserve.
static void TestReserveNoClock()
{
    CHECK(!HealReserveReleased(false, 0, 60.0f));
    CHECK(!HealReserveReleased(false, 60000, 60.0f));
    CHECK(!HealReserveReleased(true, 0, 0.0f));
    const HealReserveRead none = HealReservePick(std::vector<HealReserveRow>{{85.0f, 60.0f, 14834, 0}}, false, 0, 0);
    CHECK(none.live);
    CHECK(!none.released);
    CHECK(!none.clockKnown);
    CHECK(!HealTopUpAllowedAtHealth(true, none, 84, 40, 75.0f, 70.0f, 90.0f, false));
    const HealReserveRead leadZero = Live(85.0f, 0.0f, 0);
    CHECK(leadZero.live);
    CHECK(!leadZero.released);
}

// Several rows keep the highest reserve, and its own window, boss and distress count go with it.
static void TestReservePickHighest()
{
    const std::vector<HealReserveRow> rows{{70.0f, 30.0f, 111, 1}, {85.0f, 60.0f, 222, 3}};
    const HealReserveRead pick = HealReservePick(rows, true, 45000, 2);
    CHECK(pick.live);
    CHECK(pick.reservePct == 85.0f);
    CHECK(pick.bossEntry == 222);
    CHECK(pick.clockKnown);
    CHECK(pick.dueInMs == 45000);
    CHECK(pick.released);               // the 85 % row's 60 s window, not the losing row's 30 s
    CHECK(pick.distressCount == 3);     // the winning row's count, not the losing row's 1
    CHECK(!HealReservePick(rows, true, 70000, 2).released);
    CHECK(!HealReservePick(rows, true, 60000, 2).distressReleased); // 3 hurt members are needed
    CHECK(HealReservePick(rows, true, 60000, 2).released);          // ... the window still releases it
    CHECK(HealReservePick(rows, true, 70000, 3).released);          // ... and the distress count does
    CHECK(!HealReservePick(rows, true, 60001, 2).released);
    CHECK(!HealReservePick(std::vector<HealReserveRow>{}, true, 0, 0).live);
    // A tie keeps the first row, and its boss with it.
    CHECK(HealReservePick({{85.0f, 60.0f, 111, 0}, {85.0f, 30.0f, 222, 0}}, true, 70000, 0).bossEntry == 111);
}

// The reserve governs only the almost-full top-up tier; a 60 % member is another tier's decision.
static void TestTopUpTier()
{
    CHECK(!HealReserveTopUpTier(60.0f, 70.0f, 90.0f));
    CHECK(HealReserveTopUpTier(70.0f, 70.0f, 90.0f));
    CHECK(HealReserveTopUpTier(89.9f, 70.0f, 90.0f));
    CHECK(!HealReserveTopUpTier(90.0f, 70.0f, 90.0f));
}

// The named D2 case: six non-tanks at 71-77 % are refused while the healer (60 % mana) sits under the live 85 % line;
// the tank at 88 % is exempt, so the pick's skip leaves it as the only candidate and the pick.
static void TestPickFallsToTheExemptTank()
{
    HealingBands const bands{20.0f, 50.0f, 70.0f, 90.0f};
    const HealReserveRead read = Live(85.0f, 60.0f, 70000);
    const std::uint8_t mana = 60;
    const std::uint8_t mediumMana = 70;
    std::vector<HealingCandidate> candidates;
    for (std::uint64_t i = 0; i < 6; ++i)
        candidates.push_back(HealingCandidate{i + 1, static_cast<std::uint32_t>(71000 + i * 1000), 100000, 0, false,
            false, false});
    candidates.push_back(HealingCandidate{7, 88000, 100000, 0, true, false, false});
    // The skip is the pick's: an almost-full candidate the reserve refuses (a tank is exempt) is not a candidate.
    candidates.erase(std::remove_if(candidates.begin(), candidates.end(),
        [&](HealingCandidate const& candidate)
        {
            return HealReserveSkipsTopUp(true, read, mana, mediumMana, PredictedHealHealth(candidate), bands.medium,
                bands.almostFull, candidate.tank);
        }), candidates.end());
    CHECK(candidates.size() == 1);
    std::sort(candidates.begin(), candidates.end(), [&](HealingCandidate const& left, HealingCandidate const& right)
        { return HealingPriority(left, right, bands); });
    CHECK(candidates[HealingAssignment(candidates, bands, 0)].guid == 7);
    // Key off: nothing is refused, so the pick is today's (the lowest health non-tank, the six all equal on the ledge).
    CHECK(!HealReserveRefuses(false, read, mana, mediumMana, false));
    // A member below the tier is never skipped, whatever the reserve says.
    CHECK(!HealReserveTopUpTier(60.0f, bands.medium, bands.almostFull));
}

// The named regression: the reserve used raw health while triage used the effective health, so a member at 75 % raw
// whose predicted incoming damage takes it to 68 % was refused as a top-up although triage calls it urgent. The
// effective value is the triage one (HealEffectiveHealthPct, the same PredictedHealHealth the pick passes), and the
// reserve must not skip it even with the healer under the live 85 % line.
static void TestReserveTierReadsEffectiveHealthNotRaw()
{
    const HealReserveRead read = Live(85.0f, 60.0f, 70000);
    const std::uint8_t mana = 60;
    const std::uint8_t mediumMana = 40;
    const float mediumHealth = 70.0f;
    const float almostFull = 90.0f;
    float const damaged = HealEffectiveHealthPct(75000, 100000, 0, 7000, 20);
    CHECK(damaged < mediumHealth);
    CHECK(!HealReserveSkipsTopUp(true, read, mana, mediumMana, damaged, mediumHealth, almostFull, false));
    CHECK(HealTopUpAllowedAtHealth(true, read, mana, mediumMana, damaged, mediumHealth, almostFull, false));
    // The raw health would have called it a top-up; that is the classification this case replaces.
    CHECK(HealReserveTopUpTier(75.0f, mediumHealth, almostFull));
    // The player's own mana rule still stands: a healer under MediumMana is refused at that tier.
    CHECK(!HealTopUpAllowedAtHealth(true, read, 39, mediumMana, damaged, mediumHealth, almostFull, false));
}

// The reverse: raw 65 % with credited incoming heals reaching 75 % is a top-up, refused like any other.
static void TestReserveTierReadsCreditedHealsIntoTheBand()
{
    const HealReserveRead read = Live(85.0f, 60.0f, 70000);
    const std::uint8_t mana = 60;
    const std::uint8_t mediumMana = 40;
    const float mediumHealth = 70.0f;
    const float almostFull = 90.0f;
    float const healed = HealEffectiveHealthPct(65000, 100000, 10000, 0, 20);
    CHECK(healed >= mediumHealth && healed < almostFull);
    CHECK(HealReserveSkipsTopUp(true, read, mana, mediumMana, healed, mediumHealth, almostFull, false));
    CHECK(!HealTopUpAllowedAtHealth(true, read, mana, mediumMana, healed, mediumHealth, almostFull, false));
    // Key off: today's rule, so 60 % mana allows the top-up and 39 % does not.
    CHECK(!HealReserveSkipsTopUp(false, read, mana, mediumMana, healed, mediumHealth, almostFull, false));
    CHECK(HealTopUpAllowedAtHealth(false, read, mana, mediumMana, healed, mediumHealth, almostFull, false));
    CHECK(!HealTopUpAllowedAtHealth(false, read, 39, mediumMana, healed, mediumHealth, almostFull, false));
}

// The reserve never pulls the line under today's MediumMana rule.
static void TestReserveNeverBelowMedium()
{
    const HealReserveRead low = Live(20.0f, 60.0f, 70000);
    CHECK(!HealTopUpAllowedAtHealth(true, low, 39, 40, 75.0f, 70.0f, 90.0f, false));
    CHECK(HealTopUpAllowedAtHealth(true, low, 40, 40, 75.0f, 70.0f, 90.0f, false));
}

int main()
{
    TestReserveTierReadsEffectiveHealthNotRaw();
    TestReserveTierReadsCreditedHealsIntoTheBand();
    TestTopUpHeldUnderReserveOutsideWindow();
    TestTopUpExemptTarget();
    TestTopUpFlagOffIsToday();
    TestReserveNoClock();
    TestReservePickHighest();
    TestDistressReleasesOutsideWindow();
    TestOpeningHoldIsKept();
    TestDistressNeedsNoClock();
    TestDistressZeroIsWalk75();
    TestDistressCountReadsEffectiveHealth();
    TestReleasedHoldsForThreeSeconds();
    TestLatchResetsOnANewFight();
    TestPickOnlyRefusalsStayBounded();
    TestWhyWindowWins();
    TestReserveNeverBelowMedium();
    TestTopUpTier();
    TestPickFallsToTheExemptTank();

    std::cout << "All heal reserve policy tests passed.\n";
    return 0;
}
