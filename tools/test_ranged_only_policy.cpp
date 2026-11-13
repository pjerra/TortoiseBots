// Standalone check for ai/playerbot/policy/raid/RangedOnlyPolicy.h:
//   g++ -std=c++17 -Wall -Wextra tools/test_ranged_only_policy.cpp -o /tmp/test_ranged_only_policy && /tmp/test_ranged_only_policy
#include "../ai/playerbot/policy/raid/RangedOnlyPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " << #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using namespace ai;

namespace
{
// One ranged_only row as the table holds it, and the fight scan's active row list built from rows like it.
BossTacticRow MakeRangedOnly(std::uint32_t bossEntry, std::uint32_t targetEntry, float gapYd)
{
    BossTacticRow row;
    row.map = 249;
    row.bossEntry = bossEntry;
    row.kind = BossTacticKind::RangedOnly;
    row.targetEntry = targetEntry;
    row.param = gapYd;
    return row;
}
}

int main()
{
    // Ayamiss climbs 20 yd in the air: a melee bot under her is held while she stands 8 yd or more above it.
    CHECK(RangedOnlyHolds(true, 105.0f, 85.0f, 8.0f, false)); // MeleeHeldWhileSubjectHigh
    // A ranged bot is never held: the row exists because ranged attacks still reach.
    CHECK(!RangedOnlyHolds(false, 105.0f, 85.0f, 8.0f, false)); // RangedNeverHeld
    // Once she lands the gap closes and the hold ends by itself.
    CHECK(!RangedOnlyHolds(true, 87.0f, 85.0f, 8.0f, false)); // MeleeFreeAfterLanding
    // The threshold is inclusive: exactly 8 yd above still holds.
    CHECK(RangedOnlyHolds(true, 93.0f, 85.0f, 8.0f, false)); // ExactThresholdHolds
    // The row outranks the bot's own attack order while it is engaged (Onyxia P2: the pull order lasted the whole air
    // phase and the tanks chased the airborne boss). The 5th argument is still the caller's order input; it no longer
    // exempts the bot, and the flipped check fails on the old policy.
    CHECK(RangedOnlyHolds(true, 105.0f, 85.0f, 8.0f, true)); // ExplicitOrderDoesNotBeatTheEngagedRow
    // Outside a row (no gap for the target) nothing holds, so the player's order still wins as before.
    CHECK(!RangedOnlyHolds(true, 105.0f, 85.0f, 0.0f, true)); // NoRowLeavesTheOrderAlone

    // The gap is resolved through the fight scan (RangedOnlyFightGap), never the map's static table. The map carries
    // Onyxia's row (10 yd) and the subject matches its entry and gap, but this fight does not: her encounter is not
    // engaged, so the row is not in the scan's rows and the explicit order is kept. The static lookup of the previous
    // rule answered 10 here and erased the order.
    const BossTacticRow onyxia = MakeRangedOnly(10184, 0, 10.0f);
    const ActiveBossRows notEngaged; // the whelp fight elsewhere on the map: no ranged_only row of Onyxia in it
    CHECK(RangedOnlyFightGap(notEngaged, 10184) == 0.0f); // NotEngagedRowAnswersNothing
    CHECK(!RangedOnlyHolds(true, 105.0f, 85.0f, RangedOnlyFightGap(notEngaged, 10184), true)); // NotEngagedKeepsTheOrder

    // Her encounter is engaged: the fight scan carries the row and the gap holds the melee bot whatever it was
    // ordered to, and the action may drop that order.
    const ActiveBossRows engaged = { &onyxia };
    CHECK(RangedOnlyFightGap(engaged, 10184) == 10.0f); // EngagedRowAnswers
    CHECK(RangedOnlyHolds(true, 105.0f, 85.0f, RangedOnlyFightGap(engaged, 10184), true)); // EngagedRowBeatsTheOrder

    // No row for the subject at all (its boss has none, or the subject is another entry): the order is kept.
    CHECK(RangedOnlyFightGap(engaged, 11357) == 0.0f); // NoRowForTheEntry
    CHECK(!RangedOnlyHolds(true, 105.0f, 85.0f, RangedOnlyFightGap(engaged, 11357), true)); // NoRowKeepsTheOrder

    // A row that names another unit (target_entry) holds that unit, not its boss; a row of another kind answers
    // nothing even when it names the entry.
    const BossTacticRow ayamissAdd = MakeRangedOnly(15369, 15934, 12.0f);
    BossTacticRow killFirst = MakeRangedOnly(15369, 0, 8.0f);
    killFirst.kind = BossTacticKind::KillFirst;
    const ActiveBossRows mixed = { &killFirst, &ayamissAdd };
    CHECK(RangedOnlyFightGap(mixed, 15934) == 12.0f); // EntryRowAnswersForItsTarget
    CHECK(RangedOnlyFightGap(mixed, 15369) == 0.0f);  // EntryRowDoesNotHoldItsBoss
    CHECK(RangedOnlyFightGap(mixed, 0) == 0.0f);      // NoEntryAnswersNothing

    std::cout << "All ranged only policy tests passed.\n";
    return 0;
}
