#include "../ai/playerbot/LootRollPolicy.h"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::LootMethodTakesRolls;
using ai::LootRollVote;
using ai::LootVoteIsEcho;
using ai::LootWireVote;
using ai::PoliteRollVote;
using ai::RealPlayerCompetes;
using ai::RecipeRollVote;
using ai::RollWaitElapsed;
using ai::RollWaitVoteMarginMs;
using ai::TokenUsableByClass;
using ai::UniqueCopyOwned;

int main()
{
    std::cout << "Starting TortoiseBots loot-roll gate tests...\n";

    // (1) Loot method: free-for-all (0) and master loot (2) take no auto
    // vote (donor passes); group (3) and need-before-greed (4) do.
    {
        CHECK(!LootMethodTakesRolls(0));
        CHECK(!LootMethodTakesRolls(2));
        CHECK(LootMethodTakesRolls(3));
        CHECK(LootMethodTakesRolls(4));
        std::cout << "  [PASS] FFA/master pass, group/NBG vote\n";
    }

    // (2) Recipe: learnable NEEDs, soulbound-unusable PASSes (nobody else
    // can use it), tradeable-unusable GREEDs for the AH.
    {
        CHECK(RecipeRollVote(true, true) == LootRollVote::Need);
        CHECK(RecipeRollVote(true, false) == LootRollVote::Need);
        CHECK(RecipeRollVote(false, true) == LootRollVote::Pass);
        CHECK(RecipeRollVote(false, false) == LootRollVote::Greed);
        std::cout << "  [PASS] recipe NEED/PASS/GREED split\n";
    }

    // (3) Duplicate unique: UNIQUE_EQUIPPED already worn, or a stack at
    // MaxCount, is owned (donor RollUniqueCheck demotes NEED to GREED).
    // Zero MaxCount means no cap; an unworn unique is not owned.
    {
        CHECK(UniqueCopyOwned(true, true, 1, 1));
        CHECK(UniqueCopyOwned(false, false, 5, 5));
        CHECK(UniqueCopyOwned(false, false, 3, 5) == false);
        CHECK(UniqueCopyOwned(false, false, 3, 0) == false);
        CHECK(UniqueCopyOwned(false, true, 0, 0) == false);
        std::cout << "  [PASS] duplicate-unique ownership\n";
    }

    // (4) Class token: NEED only when the bot's class is in the
    // AllowableClass mask; an empty mask is unrestricted (all classes).
    // Warrior mask bit: class 1 -> bit 0.
    {
        const std::uint32_t warrior = 1u << (1 - 1);
        CHECK(TokenUsableByClass(warrior, warrior));
        CHECK(!TokenUsableByClass(warrior << 2, warrior));
        CHECK(TokenUsableByClass(0, warrior));
        std::cout << "  [PASS] token class-mask gate\n";
    }

    // (5) Real player in the roll: a seen NEED competes, a seen GREED or
    // PASS steps aside even for an item they could use. An unseen vote
    // never competes on bare usability alone (#967: in an LFT/pool group
    // every usable drop is usable by someone, which demoted every upgrade
    // NEED to GREED); only a hired companion grouped with its master yields
    // those. Alts wait like pool bots instead.
    {
        CHECK(RealPlayerCompetes(true, LootRollVote::Need, false));
        CHECK(!RealPlayerCompetes(true, LootRollVote::Greed, true));
        CHECK(!RealPlayerCompetes(true, LootRollVote::Pass, true));
        CHECK(!RealPlayerCompetes(false, LootRollVote::Pass, true));
        CHECK(!RealPlayerCompetes(false, LootRollVote::Pass, false));
        CHECK(RealPlayerCompetes(false, LootRollVote::Pass, true, true));
        CHECK(!RealPlayerCompetes(false, LootRollVote::Pass, false, true));
        std::cout << "  [PASS] real-player competition\n";
    }

    // (6) Polite vote: NEED steps down to GREED against a competing player
    // unless the player forced NEED; GREED/PASS untouched; no competitor,
    // no change.
    {
        CHECK(PoliteRollVote(LootRollVote::Need, true, false) == LootRollVote::Greed);
        CHECK(PoliteRollVote(LootRollVote::Need, true, true) == LootRollVote::Need);
        CHECK(PoliteRollVote(LootRollVote::Need, false, false) == LootRollVote::Need);
        CHECK(PoliteRollVote(LootRollVote::Greed, true, false) == LootRollVote::Greed);
        CHECK(PoliteRollVote(LootRollVote::Pass, true, false) == LootRollVote::Pass);
        std::cout << "  [PASS] never NEED against a player\n";
    }

    // (7) Roll wait (#967): the wait ends when every real roller voted, or
    // when the window ends within the 15 s margin (clears the 5 s "very
    // often" re-check gap plus tick jitter under load); unknown remaining
    // time never ends it on its own (the call site votes at once instead).
    {
        CHECK(RollWaitVoteMarginMs() == 15000);
        CHECK(RollWaitElapsed(60000, false) == false);
        CHECK(RollWaitElapsed(15001, false) == false);
        CHECK(RollWaitElapsed(15000, false));
        CHECK(RollWaitElapsed(5000, false));
        CHECK(RollWaitElapsed(1000, false));
        CHECK(RollWaitElapsed(60000, true));
        CHECK(RollWaitElapsed(0, false) == false);
        std::cout << "  [PASS] roll wait elapses on votes or margin\n";
    }

    // (8) Wire mapping (#967 M2): live votes are (number 0, type 0) NEED,
    // (128, 2) GREED, (128, 128) PASS per Group::CountRollVote; anything
    // with a real roll number is a post-resolution echo (CountTheRoll).
    {
        CHECK(LootWireVote(0, 0) == LootRollVote::Need);
        CHECK(LootWireVote(128, 2) == LootRollVote::Greed);
        CHECK(LootWireVote(128, 128) == LootRollVote::Pass);
        CHECK(LootWireVote(42, 1) == LootRollVote::Pass);
        CHECK(LootWireVote(87, 2) == LootRollVote::Pass);
        CHECK(!LootVoteIsEcho(0, 0));
        CHECK(!LootVoteIsEcho(128, 2));
        CHECK(!LootVoteIsEcho(128, 128));
        CHECK(LootVoteIsEcho(42, 1));
        CHECK(LootVoteIsEcho(87, 2));
        CHECK(LootVoteIsEcho(100, 0));
        std::cout << "  [PASS] wire vote mapping, echoes ignored\n";
    }

    std::cout << "All loot-roll gate checks PASSED!\n";
    return 0;
}
