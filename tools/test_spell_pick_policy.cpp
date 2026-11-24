// Standalone regression test for the cast-spell pick (SpellIdValue ->
// ai/playerbot/SpellPickPolicy.h): a same-named spell in no rank chain (the
// proc a buff triggers, a teaching spell) must not shadow the ranked line, or
// the self-buff action casts the hostile proc at itself and fails every tick.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_spell_pick_policy.cpp -o /tmp/test_spell_pick
//   /tmp/test_spell_pick

#include "../ai/playerbot/SpellPickPolicy.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace ai;

static int checks = 0;
#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        std::exit(1); \
    } \
} while (0)

static void TestRankText()
{
    CHECK(SpellRankNumber("Rank 6") == 6);
    CHECK(SpellRankNumber("Rank 1") == 1);
    CHECK(SpellRankNumber("") == 0);
    CHECK(SpellRankNumber("Rank") == 0);
    CHECK(SpellRankNumber("6") == 6);
    CHECK(SpellRankNumber("Rank 12") == 12);
}

static void TestTouchOfWeakness()
{
    // The bots know the ranked line (trainer rows 2652/19261/19262/19264/19265/19266,
    // repack trainer_spell rows) and the proc spells the buff triggers
    // (2943/19249/19251/19252/19253/19254, same name, in no rank chain).
    std::vector<KnownSpellRank> known = {
        { 2652, 1 }, { 19261, 2 }, { 19262, 3 }, { 19264, 4 }, { 19265, 5 }, { 19266, 6 },
        { 2943, 0 }, { 19249, 0 }, { 19251, 0 }, { 19252, 0 }, { 19253, 0 }, { 19254, 0 },
        { 19318, 0 }, { 28598, 0 },
    };
    CHECK(PickCastSpellId(known) == 19266);
}

static void TestRankTiesAndFallback()
{
    // Equal top rank: the higher id wins, as the old descending-id scan did.
    CHECK(PickCastSpellId({ { 900, 3 }, { 700, 3 } }) == 900);
    CHECK(PickCastSpellId({ { 700, 3 }, { 900, 3 } }) == 900);
    // Lower ranks lose to a higher rank whatever their id.
    CHECK(PickCastSpellId({ { 9000, 1 }, { 700, 6 } }) == 700);
    // Nothing ranked: the highest id (not the lowest the old scan left behind).
    CHECK(PickCastSpellId({ { 2652, 0 }, { 2943, 0 }, { 459714, 0 } }) == 459714);
    // Empty and single-rank-less answers.
    CHECK(PickCastSpellId({}) == 0);
    CHECK(PickCastSpellId({ { 1234, 0 } }) == 1234);
}

int main()
{
    TestRankText();
    TestTouchOfWeakness();
    TestRankTiesAndFallback();
    std::printf("All cast-spell pick tests passed (%d checks).\n", checks);
    return 0;
}
