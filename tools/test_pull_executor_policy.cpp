// Standalone test for the pull executor policy (issue #908): which tanks
// count as pull executors, and why the others are refused.
//
// Rules under test:
// - A live tank with no combat flag can pull.
// - A combat flag with no victim and no attackers is stale: still a pull
//   candidate, never `tank-busy`.
// - A combat flag backed by a victim or an attacker set is a real fight:
//   the tank is busy, not missing.
// - World-state and capability gates (dead, away, another map, no AI,
//   non-tank, missing pull strategy) refuse with a named reason word.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_pull_executor_policy.cpp -o /tmp/test_pull_executor_policy
//   /tmp/test_pull_executor_policy

#include "../ai/playerbot/PullExecutorPolicy.h"

#include <cstdlib>
#include <cstring>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::ClassifyPullCandidate;
using ai::PullCandidateBlocked;
using ai::PullCandidateFacts;
using ai::PullCandidateInRealFight;
using ai::PullExcludeReason;
using ai::PullExcludeWord;

// A live tank on the requester's map with the pull strategy: the MC case.
static PullCandidateFacts FreeTank()
{
    PullCandidateFacts facts;
    facts.alive = true;
    facts.inWorld = true;
    facts.sameMap = true;
    facts.hasAi = true;
    facts.tankCapable = true;
    facts.pullReady = true;
    return facts;
}

int main()
{
    std::cout << "Starting TortoiseBots pull executor policy tests...\n";

    // (1) A free tank pulls.
    {
        CHECK(ClassifyPullCandidate(FreeTank()) == PullExcludeReason::None);
        CHECK(!PullCandidateBlocked(FreeTank()));
        std::cout << "  [PASS] free tank can pull\n";
    }

    // (2) Stale combat flag: flag set, no victim, nobody attacking. The tank
    // stays a candidate (issue #908: a bare IsInCombat() refused these).
    {
        PullCandidateFacts facts = FreeTank();
        facts.combatFlag = true;
        CHECK(!PullCandidateInRealFight(facts));
        CHECK(ClassifyPullCandidate(facts) == PullExcludeReason::None);
        CHECK(!PullCandidateBlocked(facts));
        std::cout << "  [PASS] stale combat flag does not exclude the tank\n";
    }

    // (3) Real fights exclude as busy: flag plus a victim, or attackers.
    {
        PullCandidateFacts victim = FreeTank();
        victim.combatFlag = true;
        victim.hasVictim = true;
        CHECK(PullCandidateInRealFight(victim));
        CHECK(ClassifyPullCandidate(victim) == PullExcludeReason::Busy);
        CHECK(PullCandidateBlocked(victim));

        PullCandidateFacts attacked = FreeTank();
        attacked.combatFlag = true;
        attacked.hasAttackers = true;
        CHECK(PullCandidateInRealFight(attacked));
        CHECK(ClassifyPullCandidate(attacked) == PullExcludeReason::Busy);
        std::cout << "  [PASS] flag with a victim or attackers is busy\n";
    }

    // (4) A victim or attackers without the flag is no fight either.
    {
        PullCandidateFacts facts = FreeTank();
        facts.hasVictim = true;
        CHECK(!PullCandidateInRealFight(facts));
        CHECK(ClassifyPullCandidate(facts) == PullExcludeReason::None);
        facts.hasVictim = false;
        facts.hasAttackers = true;
        CHECK(!PullCandidateInRealFight(facts));
        CHECK(ClassifyPullCandidate(facts) == PullExcludeReason::None);
        std::cout << "  [PASS] victim or attackers alone set no busy fight\n";
    }

    // (5) Every other gate refuses with a named reason.
    {
        PullCandidateFacts facts = FreeTank();
        facts.inWorld = false;
        CHECK(ClassifyPullCandidate(facts) == PullExcludeReason::NotInWorld);
        CHECK(PullCandidateBlocked(facts));

        facts = FreeTank();
        facts.alive = false;
        CHECK(ClassifyPullCandidate(facts) == PullExcludeReason::Dead);

        facts = FreeTank();
        facts.sameMap = false;
        CHECK(ClassifyPullCandidate(facts) == PullExcludeReason::WrongMap);

        facts = FreeTank();
        facts.hasAi = false;
        CHECK(ClassifyPullCandidate(facts) == PullExcludeReason::NoAi);

        facts = FreeTank();
        facts.tankCapable = false;
        CHECK(ClassifyPullCandidate(facts) == PullExcludeReason::NotTank);

        // The selection attaches "pull" on demand, so a missing strategy is
        // counted, not blocking.
        facts = FreeTank();
        facts.pullReady = false;
        CHECK(ClassifyPullCandidate(facts) == PullExcludeReason::NoPullStrategy);
        CHECK(!PullCandidateBlocked(facts));
        std::cout << "  [PASS] world, map, AI, role and strategy gates refuse\n";
    }

    // (6) Reason words: short, non-empty, and '|' free for the protocol line.
    {
        PullExcludeReason reasons[] = {
            PullExcludeReason::None, PullExcludeReason::NotInWorld,
            PullExcludeReason::Dead, PullExcludeReason::Busy,
            PullExcludeReason::WrongMap, PullExcludeReason::NoAi,
            PullExcludeReason::NotTank, PullExcludeReason::NoPullStrategy,
        };
        for (PullExcludeReason reason : reasons)
        {
            char const* word = PullExcludeWord(reason);
            CHECK(word && std::strlen(word) > 0);
            CHECK(std::strchr(word, '|') == nullptr);
        }
        CHECK(std::strcmp(PullExcludeWord(PullExcludeReason::Busy), "in combat") == 0);
        std::cout << "  [PASS] reason words are short and protocol-safe\n";
    }

    std::cout << "All pull executor policy checks PASSED!\n";
    return 0;
}
