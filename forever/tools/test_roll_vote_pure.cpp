// Standalone test of the honest-vote mask fallback (forever/src/host/TbRollVotePure.h)
// plus one case per TB vote class through the shared pure policy
// (ai/playerbot/policy/loot/LootRollPolicy.h):
//   g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/test_roll_vote_pure.cpp -o /tmp/test_roll_vote_pure && /tmp/test_roll_vote_pure
// A mutation that needs on a greed-only mask, greeds where only pass is offered,
// or drops a TB pass case fails here.
#include "forever/src/host/TbRollVotePure.h"

#include "ai/playerbot/policy/loot/LootRollPolicy.h"

#include <cstdio>

namespace rv = tbrollvote;

static int g_checks = 0, g_fail = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static void TestMaskFallback()
{
    // NEED offered (the common group-loot mask) reaches the wire as NEED.
    CHECK(rv::FallbackVote(rv::kVoteNeed, rv::kMaskAll) == rv::kVoteNeed);
    // NEED not offered (CAN_ONLY_ROLL_GREED item, or NBG hiding need) -> GREED, not silence.
    CHECK(rv::FallbackVote(rv::kVoteNeed, rv::kMaskGreed) == rv::kVoteGreed);
    CHECK(rv::FallbackVote(rv::kVoteNeed, rv::kMaskPass | rv::kMaskGreed) == rv::kVoteGreed);
    // GREED offered reaches the wire as GREED.
    CHECK(rv::FallbackVote(rv::kVoteGreed, rv::kMaskAll) == rv::kVoteGreed);
    // GREED not offered (cannot happen on this core today) -> PASS, never silence.
    CHECK(rv::FallbackVote(rv::kVoteGreed, rv::kMaskPass) == rv::kVotePass);
    // NEED with neither NEED nor GREED offered chains down to PASS.
    CHECK(rv::FallbackVote(rv::kVoteNeed, rv::kMaskPass) == rv::kVotePass);
    // PASS always passes: the core never refuses it.
    CHECK(rv::FallbackVote(rv::kVotePass, rv::kMaskAll) == rv::kVotePass);
    CHECK(rv::FallbackVote(rv::kVotePass, 0) == rv::kVotePass);
    // Anything else is left alone: TB never sends it (RollOnItemInSlot normalises to PASS first).
    CHECK(rv::FallbackVote(3, rv::kMaskAll) == 3);
}

static void TestVoteClasses()
{
    // Equip upgrade NEED: TB's usage switch needs ITEM_USAGE_EQUIP
    // (LootRollAction.cpp CalculateRollVote); the mask lets it through.
    CHECK(rv::FallbackVote(rv::kVoteNeed, rv::kMaskAll) == rv::kVoteNeed);
    // Vendor GREED: the usage switch greeds ITEM_USAGE_VENDOR; a tradeable
    // recipe the bot cannot learn is the runnable GREED case.
    CHECK(ai::RecipeRollVote(false, false) == ai::LootRollVote::Greed);
    CHECK(rv::FallbackVote(rv::kVoteGreed, rv::kMaskAll) == rv::kVoteGreed);
    // Duplicate unique GREED: an equipped unique (or a full stack) demotes NEED.
    CHECK(ai::UniqueCopyOwned(true, true, 1, 1));
    CHECK(ai::UniqueCopyOwned(false, false, 5, 5));
    CHECK(!ai::UniqueCopyOwned(false, false, 1, 5));
    // Class token: usable by this class NEEDs, others GREED.
    CHECK(ai::TokenUsableByClass(0, 0x2));
    CHECK(ai::TokenUsableByClass(0x2, 0x2));
    CHECK(!ai::TokenUsableByClass(0x2, 0x4));
    // Pass cases: the loot method takes no rolls (FFA/master) ...
    CHECK(!ai::LootMethodTakesRolls(0));
    CHECK(!ai::LootMethodTakesRolls(2));
    CHECK(ai::LootMethodTakesRolls(3));
    CHECK(ai::LootMethodTakesRolls(4));
    // ... a soulbound recipe the bot cannot learn ...
    CHECK(ai::RecipeRollVote(false, true) == ai::LootRollVote::Pass);
    // ... while a learnable one NEEDs.
    CHECK(ai::RecipeRollVote(true, true) == ai::LootRollVote::Need);
    // Every TB pass reaches the wire as PASS.
    CHECK(rv::FallbackVote(rv::kVotePass, rv::kMaskAll) == rv::kVotePass);
}

int main()
{
    TestMaskFallback();
    TestVoteClasses();
    if (g_fail)
    {
        std::printf("%d of %d checks FAILED\n", g_fail, g_checks);
        return 1;
    }
    std::printf("%d checks passed\n", g_checks);
    return 0;
}
