// Standalone test for forever/src/host/TbSpecEnchantPure.h: the level-60 gear
// enchant rule (when the armory table's row for a spec and slot applies, when
// the slot stays empty, and when the table says nothing).
// Build & run: g++ -std=c++17 -Iforever/src -Wall -Wextra -Werror forever/tools/test_spec_enchant_rules.cpp -o /tmp/test_spec_enchant_rules && /tmp/test_spec_enchant_rules

#include "host/TbSpecEnchantPure.h"

#include <cstdio>

namespace
{

int g_failures = 0;

#define CHECK(cond, msg)                                                  \
    do                                                                    \
    {                                                                     \
        if (!(cond))                                                      \
        {                                                                 \
            std::printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); \
            ++g_failures;                                                 \
        }                                                                 \
    } while (0)

} // namespace

int main()
{
    using namespace TortoiseBots::enchant;

    // The item-quality bands the ordinary picker uses: white 10, green 5,
    // blue and better 0.
    CHECK(SpecEnchantLevelMargin(kQualityPoor) == 0, "grey has no margin");
    CHECK(SpecEnchantLevelMargin(kQualityNormal) == 10, "white margin");
    CHECK(SpecEnchantLevelMargin(kQualityUncommon) == 5, "green margin");
    CHECK(SpecEnchantLevelMargin(3) == 0, "rare has no margin");
    CHECK(SpecEnchantLevelMargin(4) == 0, "epic has no margin");

    SpecEnchantRule row;
    row.present = true;
    row.spellId = 20034; // Enchant Weapon - Crusader: level 60, blue and better
    row.minLevel = 60;

    // No row: the table has nothing to say, whatever the item.
    SpecEnchantRule none;
    CHECK(DecideSpecEnchant(none, 60, 4).verdict == SpecEnchantVerdict::NoRule, "no row, no rule");
    CHECK(DecideSpecEnchant(none, 60, 4).spellId == 0, "no row carries no spell");
    none.spellId = 20034;
    CHECK(DecideSpecEnchant(none, 60, 4).verdict == SpecEnchantVerdict::NoRule, "a present flag alone does not apply");

    // Below 60 the sample says nothing, so the ordinary picker keeps the slot.
    CHECK(DecideSpecEnchant(row, 59, 4).verdict == SpecEnchantVerdict::NoRule, "level 59 no rule");
    CHECK(DecideSpecEnchant(row, 59, 4).spellId == 0, "level 59 no spell");
    CHECK(DecideSpecEnchant(row, 60, 4).verdict == SpecEnchantVerdict::Apply, "level 60 applies");

    // A named slot whose choice the item cannot carry stays empty; it never
    // falls back to another enchant.
    SpecEnchantDecision at60 = DecideSpecEnchant(row, 60, 3);
    CHECK(at60.verdict == SpecEnchantVerdict::Apply && at60.spellId == 20034, "rare weapon takes the level-60 choice");
    CHECK(DecideSpecEnchant(row, 60, 2).verdict == SpecEnchantVerdict::LeaveEmpty, "green cannot take a level-60 choice");
    CHECK(DecideSpecEnchant(row, 60, 1).verdict == SpecEnchantVerdict::LeaveEmpty, "white cannot take a level-60 choice");
    CHECK(DecideSpecEnchant(row, 60, 1).spellId == 0, "an empty slot carries no spell");
    CHECK(DecideSpecEnchant(row, 60, kQualityPoor).verdict == SpecEnchantVerdict::LeaveEmpty, "grey gear stays bare");
    CHECK(DecideSpecEnchant(row, 65, 2).verdict == SpecEnchantVerdict::Apply, "a level later the green takes it");

    // A low-level choice applies down the quality bands by its own margin.
    SpecEnchantRule spurs;
    spurs.present = true;
    spurs.spellId = 9783; // Mithril Spurs: level 43
    spurs.minLevel = 43;
    CHECK(DecideSpecEnchant(spurs, 60, 1).verdict == SpecEnchantVerdict::Apply, "43 + white margin fits at 60");
    CHECK(DecideSpecEnchant(spurs, 53, 1).verdict == SpecEnchantVerdict::NoRule, "43 + 10 fits, but 53 is below the level the table covers");
    spurs.minLevel = 50;
    CHECK(DecideSpecEnchant(spurs, 60, 1).verdict == SpecEnchantVerdict::Apply, "50 + white margin is exactly 60");
    spurs.minLevel = 51;
    CHECK(DecideSpecEnchant(spurs, 60, 1).verdict == SpecEnchantVerdict::LeaveEmpty, "51 + white margin misses 60");
    CHECK(DecideSpecEnchant(spurs, 60, 2).verdict == SpecEnchantVerdict::Apply, "51 + green margin fits 60");
    spurs.minLevel = 56;
    CHECK(DecideSpecEnchant(spurs, 60, 2).verdict == SpecEnchantVerdict::LeaveEmpty, "56 + green margin misses 60");
    CHECK(DecideSpecEnchant(spurs, 60, 3).verdict == SpecEnchantVerdict::Apply, "a rare item takes 56 at 60");

    // The table's own "players leave this slot empty" row.
    SpecEnchantRule empty;
    empty.present = true;
    empty.spellId = 0;
    CHECK(DecideSpecEnchant(empty, 60, 4).verdict == SpecEnchantVerdict::LeaveEmpty, "an empty-slot row leaves the slot empty");
    CHECK(DecideSpecEnchant(empty, 60, 1).verdict == SpecEnchantVerdict::LeaveEmpty, "quality does not resurrect it");
    CHECK(DecideSpecEnchant(empty, 59, 4).verdict == SpecEnchantVerdict::NoRule, "below 60 even an empty row is no rule");

    if (g_failures == 0)
        std::printf("test_spec_enchant_rules ok\n");
    return g_failures ? 1 : 0;
}
