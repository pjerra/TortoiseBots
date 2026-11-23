// Standalone test for ai/playerbot/policy/profession/EnchantUpkeepPolicy.h:
// the self-maintenance an enchanter bot runs (which carried items it breaks
// down, which enchant replaces the one on a worn item, which recipes the
// profession grants as the skill grows).
// Build & run: g++ -std=c++17 -I. -Wall -Wextra -Werror forever/tools/test_enchant_upkeep_policy.cpp -o /tmp/test_enchant_upkeep_policy && /tmp/test_enchant_upkeep_policy

#include "ai/playerbot/policy/profession/EnchantUpkeepPolicy.h"

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
    using namespace TortoiseBots::prof::upkeep;

    // Armor and weapons only; a bound-on-acquire item is kept; normal and
    // poor quality are vendor trash, not disenchant stock.
    CHECK(DisenchantCandidateClass(4, 2, 2), "uncommon armor");
    CHECK(DisenchantCandidateClass(2, 2, 3), "rare weapon");
    CHECK(!DisenchantCandidateClass(4, 1, 3), "bound on acquire is kept");
    CHECK(!DisenchantCandidateClass(4, 2, 1), "normal quality is not stock");
    CHECK(!DisenchantCandidateClass(7, 2, 2), "trade goods are not stock");

    // Rare and better are kept next to a real player; an all-bot world breaks
    // them down too.
    CHECK(DisenchantQualityAllowed(2, true), "uncommon next to a player");
    CHECK(!DisenchantQualityAllowed(3, true), "rare next to a player");
    CHECK(DisenchantQualityAllowed(3, false), "rare in an all-bot guild");

    DisenchantUpkeepFacts f;
    f.itemClass = 4;
    f.bonding = 2;
    f.quality = 2;
    f.hasLoot = true;
    f.requiredSkill = 100;
    f.botSkill = 125;
    CHECK(DisenchantUpkeepWants(f), "disenchantable at skill");
    f.botSkill = 99;
    CHECK(!DisenchantUpkeepWants(f), "skill short");
    f.botSkill = 125;
    f.hasLoot = false;
    CHECK(!DisenchantUpkeepWants(f), "no disenchant loot");
    f.hasLoot = true;
    f.protectedItem = true;
    CHECK(!DisenchantUpkeepWants(f), "order or service stock is protected");
    f.protectedItem = false;
    f.requiredSkill = 0;
    f.botSkill = 1;
    CHECK(DisenchantUpkeepWants(f), "a zero requirement needs only the spell");
    f.quality = 3;
    f.sharesWithRealPlayer = true;
    CHECK(!DisenchantUpkeepWants(f), "rare kept next to a player");

    EnchantUpkeepFacts e;
    e.spellTakesReagents = true;
    e.hasReagents = true;
    e.currentWeight = 10;
    e.newWeight = 11;
    CHECK(EnchantUpkeepWants(e), "a better enchant replaces");
    e.newWeight = 10;
    CHECK(!EnchantUpkeepWants(e), "the same score does not replace");
    e.newWeight = 9;
    CHECK(!EnchantUpkeepWants(e), "a worse score does not replace");
    e.newWeight = 11;
    e.hasReagents = false;
    CHECK(!EnchantUpkeepWants(e), "missing reagents");
    e.hasReagents = true;
    e.protectedReagents = true;
    CHECK(!EnchantUpkeepWants(e), "order reagents are protected");
    e.protectedReagents = false;
    e.spellTakesReagents = false;
    CHECK(!EnchantUpkeepWants(e), "a reagent-free spell is not upkeep");
    e.spellTakesReagents = true;
    e.currentWeight = 0;
    e.newWeight = 1;
    CHECK(EnchantUpkeepWants(e), "an unenchanted item takes a scoring enchant");

    // Recipes: known requirement covered by the skill; a zero requirement
    // names no trainer or recipe and is never granted.
    CHECK(EnchantRecipeGrantable(265, 300), "covered requirement");
    CHECK(EnchantRecipeGrantable(265, 265), "exact requirement");
    CHECK(!EnchantRecipeGrantable(265, 264), "skill short");
    CHECK(!EnchantRecipeGrantable(0, 300), "no requirement names no recipe");

    if (g_failures == 0)
        std::printf("test_enchant_upkeep_policy ok\n");
    return g_failures ? 1 : 0;
}
