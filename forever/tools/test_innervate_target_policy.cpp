// Standalone proof of the Innervate target & raid mana balance rules (ai/playerbot/policy/heal/InnervateTargetPolicy.h):
//   g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/test_innervate_target_policy.cpp -o /tmp/test_innervate_target_policy && /tmp/test_innervate_target_policy
#include "ai/playerbot/policy/heal/InnervateTargetPolicy.h"

#include <cassert>
#include <iostream>
#include <vector>

int main()
{
    int passed = 0;

    // 1. Healer first, even if a DPS caster has slightly lower mana.
    {
        std::vector<ai::InnervateCandidate> candidates = {
            { 1, false, false, true, 5,  true, false, false },  // Mage at 5%
            { 2, false, true,  true, 18, true, false, false },  // Priest healer at 18%
            { 3, true,  true,  true, 60, true, false, false },  // Self (druid) at 60%
        };
        assert(ai::PickInnervateTarget(candidates, 25) == 2);
        ++passed;
    }

    // 2. Among healers below threshold, pick the lowest mana %.
    {
        std::vector<ai::InnervateCandidate> candidates = {
            { 1, false, true, true, 22, true, false, false },
            { 2, false, true, true, 9,  true, false, false },
            { 3, false, true, true, 15, true, false, false },
        };
        assert(ai::PickInnervateTarget(candidates, 25) == 2);
        ++passed;
    }

    // 3. When no healer is below threshold, pick the lowest-mana caster DPS.
    {
        std::vector<ai::InnervateCandidate> candidates = {
            { 1, false, true,  true, 55, true, false, false },  // Healer fine
            { 2, false, false, true, 12, true, false, false },  // Warlock at 12%
            { 3, false, false, true, 8,  true, false, false },  // Mage at 8%
        };
        assert(ai::PickInnervateTarget(candidates, 25) == 3);
        ++passed;
    }

    // 4. Claimed healer yields to unclaimed healer; claimed healer still picked if no unclaimed healer is low.
    {
        std::vector<ai::InnervateCandidate> candidates = {
            { 1, false, true, true, 5,  true, false, true  },  // Claimed by another druid
            { 2, false, true, true, 19, true, false, false },  // Unclaimed
        };
        assert(ai::PickInnervateTarget(candidates, 25) == 2);

        candidates[1].manaPct = 80; // No longer low
        assert(ai::PickInnervateTarget(candidates, 25) == 1);
        ++passed;
    }

    // 5. Skips non-mana, out-of-range, already-Innervated, and >= threshold.
    {
        std::vector<ai::InnervateCandidate> candidates = {
            { 1, false, false, false, 0,  true,  false, false }, // Warrior (no mana)
            { 2, false, true,  true,  5,  false, false, false }, // Out of range
            { 3, false, true,  true,  5,  true,  true,  false }, // Already has Innervate
            { 4, false, true,  true,  25, true,  false, false }, // At threshold (not below)
        };
        assert(ai::PickInnervateTarget(candidates, 25) == 0);
        ++passed;
    }

    std::cout << "PASS: test_innervate_target_policy (" << passed << " checks, 0 failed)\n";
    return 0;
}
