// Standalone pure test for autonomous level-5 profession grants (forever/src/host/TbAutoProfsPure.h):
//   g++ -std=c++17 -O1 -Wall -Wextra -I. -Iai forever/tools/test_auto_profs.cpp -o /tmp/test_auto_profs && /tmp/test_auto_profs

#include "forever/src/host/TbAutoProfsPure.h"

#include <cstdio>
#include <set>
#include <utility>

namespace autoprofs = TortoiseBots::autoprofs;
namespace prof = TortoiseBots::prof;

static int g_checks = 0;
static int g_failed = 0;

#define CHECK(cond, msg)                                                      \
    do                                                                        \
    {                                                                         \
        ++g_checks;                                                           \
        if (!(cond))                                                          \
        {                                                                     \
            ++g_failed;                                                       \
            std::printf("FAIL: %s (line %d)\n", (msg), __LINE__);             \
        }                                                                     \
    } while (0)

int main()
{
    // 1. Failing-first guard: level 4 pool bot must receive nothing.
    {
        autoprofs::AutoProfInputs in;
        in.level = 4;
        in.classId = prof::ClassWarrior;
        in.guidCounter = 100;
        in.isPoolBot = true;
        auto plan = autoprofs::PlanAutoProfessions(in);
        CHECK(plan.skillsToLearn.empty(), "level 4 pool bot gets no professions yet");
    }

    // 2. Non-pool bot (e.g. player's own alt) at level 5+ must receive nothing.
    {
        autoprofs::AutoProfInputs in;
        in.level = 10;
        in.classId = prof::ClassMage;
        in.guidCounter = 101;
        in.isPoolBot = false;
        auto plan = autoprofs::PlanAutoProfessions(in);
        CHECK(plan.skillsToLearn.empty(), "non-pool bot gets no auto professions");
    }

    // 3. Level 5 pool bot with no primaries gets 2 distinct primaries + First Aid + Cooking.
    {
        autoprofs::AutoProfInputs in;
        in.level = 5;
        in.classId = prof::ClassWarrior;
        in.guidCounter = 42;
        in.isPoolBot = true;
        auto plan = autoprofs::PlanAutoProfessions(in);
        CHECK(plan.skillsToLearn.size() == 4, "level 5 warrior gets 2 primaries + First Aid + Cooking");
        CHECK(prof::IsPrimaryProfession(plan.skillsToLearn[0]), "first is primary");
        CHECK(prof::IsPrimaryProfession(plan.skillsToLearn[1]), "second is primary");
        CHECK(plan.skillsToLearn[0] != plan.skillsToLearn[1], "two distinct primaries");
        CHECK(plan.skillsToLearn[2] == prof::SkillFirstAid, "third is First Aid");
        CHECK(plan.skillsToLearn[3] == prof::SkillCooking, "fourth is Cooking");
    }

    // 4. Bot already holding a primary is left alone for primaries, but still catches up missing secondaries.
    {
        autoprofs::AutoProfInputs in;
        in.level = 12;
        in.classId = prof::ClassHunter;
        in.guidCounter = 7;
        in.isPoolBot = true;
        in.hasPrimaryProfession = true;
        in.hasFirstAid = true;
        in.hasCooking = false;
        auto plan = autoprofs::PlanAutoProfessions(in);
        CHECK(plan.skillsToLearn.size() == 1 && plan.skillsToLearn[0] == prof::SkillCooking,
              "bot with primary + First Aid only catches up Cooking");
    }

    // 5. Pair selection is stable per guid and covers sensible pairs for every class.
    {
        std::uint8_t const classes[] = {
            prof::ClassWarrior, prof::ClassPaladin, prof::ClassHunter,
            prof::ClassRogue, prof::ClassPriest, prof::ClassShaman,
            prof::ClassMage, prof::ClassWarlock, prof::ClassDruid
        };
        for (std::uint8_t cls : classes)
        {
            std::set<std::pair<std::uint32_t, std::uint32_t>> seen;
            for (std::uint64_t g = 1; g <= 12; ++g)
            {
                auto p1 = autoprofs::PickAutoProfessionPair(cls, g);
                auto p2 = autoprofs::PickAutoProfessionPair(cls, g);
                CHECK(p1 == p2, "pair choice must be stable for same guid");
                CHECK(prof::IsPrimaryProfession(p1.first) && prof::IsPrimaryProfession(p1.second),
                      "both picked skills must be primary professions");
                CHECK(p1.first != p1.second, "picked pair must have two distinct professions");
                CHECK(autoprofs::ApprenticeSpellForSkill(p1.first) != 0 &&
                      autoprofs::ApprenticeSpellForSkill(p1.second) != 0,
                      "both picked skills must have apprentice spells");
                seen.insert(p1);
            }
            CHECK(seen.size() >= 2, "each class rolls multiple sensible pairs across guids");
        }
    }

    // 6. All 11 supported skills have non-zero apprentice spell IDs; fishing/unknown return 0.
    {
        std::uint32_t const supported[] = {
            prof::SkillFirstAid, prof::SkillCooking,
            prof::SkillBlacksmithing, prof::SkillLeatherworking, prof::SkillAlchemy,
            prof::SkillHerbalism, prof::SkillMining, prof::SkillTailoring,
            prof::SkillEngineering, prof::SkillEnchanting, prof::SkillSkinning
        };
        for (std::uint32_t sid : supported)
            CHECK(autoprofs::ApprenticeSpellForSkill(sid) != 0, "supported skill has apprentice spell");
        CHECK(autoprofs::ApprenticeSpellForSkill(prof::SkillFishing) == 0, "fishing is excluded");
        CHECK(autoprofs::ApprenticeSpellForSkill(0) == 0, "0 returns 0");
    }

    if (g_failed != 0)
    {
        std::printf("FAILED: %d/%d checks\n", g_failed, g_checks);
        return 1;
    }
    std::printf("PASS: %d/%d checks\n", g_checks, g_checks);
    return 0;
}
