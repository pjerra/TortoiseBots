#include "../../ai/playerbot/policy/quest/QuestFilterPolicy.h"

#include <cassert>
#include <iostream>

int main()
{
    // Retail-era quest under vanilla zone sort (e.g. Darkshore Cataclysm chain 13504, Expansion 3) must be refused.
    assert(!ai::IsVanillaOrCustomQuest(13504u, 3, true, true));
    assert(!ai::IsVanillaOrCustomQuest(13504u, 3, false, false));

    // No-giver or no-ender quest must be refused even if Expansion == 0.
    assert(!ai::IsVanillaOrCustomQuest(7961u, 0, false, true));
    assert(!ai::IsVanillaOrCustomQuest(91752u, 0, true, false));
    assert(!ai::IsVanillaOrCustomQuest(91758u, 0, false, false));

    // Vanilla Expansion == 0 with spawned giver and ender is accepted.
    assert(ai::IsVanillaOrCustomQuest(783u, 0, true, true));
    // Forever custom re-ID bands (91xxx / 99xxx) with spawned giver and ender are accepted.
    assert(ai::IsVanillaOrCustomQuest(91750u, 1, true, true));
    assert(ai::IsVanillaOrCustomQuest(99001u, 0, true, true));

    // Title deduplication: custom re-ID wins over non-custom copy.
    assert(ai::PreferQuestByTitle(91750u, 13504u));
    assert(!ai::PreferQuestByTitle(13504u, 91750u));
    assert(ai::PreferQuestByTitle(783u, 1000u));

    // Bounded drop rule: unresolvable item objective must be dropped after bound, not worked forever.
    assert(!ai::ShouldDropUnresolvableQuest(0u, 10u));
    assert(!ai::ShouldDropUnresolvableQuest(9u, 10u));
    assert(ai::ShouldDropUnresolvableQuest(10u, 10u));
    assert(ai::ShouldDropUnresolvableQuest(15u, 10u));

    std::cout << "test_quest_filter: PASS\n";
    return 0;
}
