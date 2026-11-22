// Core-free unit tests for forever/src/host/TbAutoToolsPure.h.
// Build & run: g++ -std=c++17 -I. -Wall -Wextra -Werror forever/tools/test_auto_tools.cpp -o /tmp/test_auto_tools && /tmp/test_auto_tools

#include "forever/src/host/TbAutoToolsPure.h"

#include <cstdio>
#include <set>
#include <vector>

namespace
{

int g_failures = 0;

#define CHECK(cond, msg)                                                      \
    do                                                                        \
    {                                                                         \
        if (!(cond))                                                          \
        {                                                                     \
            std::printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__);     \
            ++g_failures;                                                     \
        }                                                                     \
    } while (0)

} // namespace

int main()
{
    using namespace TortoiseBots::autotools;
    namespace prof = TortoiseBots::prof;

    // 1. Mining + Blacksmithing with empty bags -> Mining Pick + Blacksmith Hammer.
    {
        std::set<uint32_t> skills = { prof::SkillMining, prof::SkillBlacksmithing };
        std::set<uint32_t> items;
        std::vector<uint32_t> const missing = MissingToolsForSkills(
            [&](uint32_t s) { return skills.count(s) > 0; },
            [&](uint32_t i) { return items.count(i) > 0; });
        CHECK(missing.size() == 2, "mining+bs needs 2 tools");
        CHECK(missing[0] == kItemMiningPick && missing[1] == kItemBlacksmithHammer, "mining pick + bs hammer");
    }

    // 2. Blacksmithing + Engineering -> single Blacksmith Hammer (no duplicate) + Arclight Spanner.
    {
        std::set<uint32_t> skills = { prof::SkillBlacksmithing, prof::SkillEngineering };
        std::set<uint32_t> items;
        std::vector<uint32_t> const missing = MissingToolsForSkills(
            [&](uint32_t s) { return skills.count(s) > 0; },
            [&](uint32_t i) { return items.count(i) > 0; });
        CHECK(missing.size() == 2, "bs+eng needs 2 tools (hammer deduped)");
        CHECK(missing[0] == kItemBlacksmithHammer && missing[1] == kItemArclightSpanner, "hammer + spanner");
    }

    // 3. Skinning + Fishing + Enchanting -> Skinning Knife + Fishing Pole + Runed Copper Rod.
    {
        std::set<uint32_t> skills = { prof::SkillSkinning, prof::SkillFishing, prof::SkillEnchanting };
        std::set<uint32_t> items;
        std::vector<uint32_t> const missing = MissingToolsForSkills(
            [&](uint32_t s) { return skills.count(s) > 0; },
            [&](uint32_t i) { return items.count(i) > 0; });
        CHECK(missing.size() == 3, "skinning+fishing+enchanting needs 3 tools");
        CHECK(missing[0] == kItemSkinningKnife && missing[1] == kItemFishingPole && missing[2] == kItemRunedCopperRod,
            "knife + pole + runed copper rod");
    }

    // 4. Already owns Mining Pick or alternate pick/knife/rod -> nothing granted.
    {
        std::set<uint32_t> skills = { prof::SkillMining, prof::SkillSkinning, prof::SkillEnchanting };
        std::set<uint32_t> items = { 1959, 12709, 16207 }; // Cold Iron Pick, Pip's Skinner, Runed Arcanite Rod
        std::vector<uint32_t> const missing = MissingToolsForSkills(
            [&](uint32_t s) { return skills.count(s) > 0; },
            [&](uint32_t i) { return items.count(i) > 0; });
        CHECK(missing.empty(), "alternate tools satisfy mining/skinning/enchanting without duplicates");
        CHECK(HasRequiredGatherTool(prof::SkillMining, [&](uint32_t i) { return items.count(i) > 0; }),
            "HasRequiredGatherTool accepts alternate pick");
        CHECK(HasRequiredGatherTool(prof::SkillSkinning, [&](uint32_t i) { return items.count(i) > 0; }),
            "HasRequiredGatherTool accepts alternate knife");
        CHECK(HasRequiredGatherTool(prof::SkillHerbalism, [&](uint32_t i) { return items.count(i) > 0; }),
            "HasRequiredGatherTool returns true for herbalism");
    }

    // 5. Professions that need no bag tool (Herbalism, Alchemy, Leatherworking, Tailoring, First Aid, Cooking).
    {
        std::set<uint32_t> skills = {
            prof::SkillHerbalism, prof::SkillAlchemy, prof::SkillLeatherworking,
            prof::SkillTailoring, prof::SkillFirstAid, prof::SkillCooking
        };
        std::set<uint32_t> items;
        std::vector<uint32_t> const missing = MissingToolsForSkills(
            [&](uint32_t s) { return skills.count(s) > 0; },
            [&](uint32_t i) { return items.count(i) > 0; });
        CHECK(missing.empty(), "toolless professions need no tools");
    }

    if (g_failures == 0)
        std::printf("PASS: test_auto_tools (5 cases, 0 failed)\n");
    return g_failures == 0 ? 0 : 1;
}
