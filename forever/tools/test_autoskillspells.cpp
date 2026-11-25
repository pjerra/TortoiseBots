// Standalone pure unit tests for forever/src/host/TbAutoSkillSpellsPure.h.
//   g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/test_autoskillspells.cpp -o /tmp/test_autoskillspells && /tmp/test_autoskillspells
#include "forever/src/host/TbAutoSkillSpellsPure.h"

#include <cstdio>
#include <map>
#include <string>

namespace S = tbhost::skillspells;

static int g_checks = 0;
static std::map<std::string, bool> g_cases;

static void BeginCase(char const* name)
{
    g_cases.emplace(name, true);
}

static void Check(bool ok, char const* caseName, char const* what, int line)
{
    ++g_checks;
    if (ok)
        return;
    g_cases[caseName] = false;
    std::printf("FAIL %s: %s (line %d)\n", caseName, what, line);
}

#define CHECK(caseName, cond) Check((cond), (caseName), #cond, __LINE__)

static void TestClassLevels()
{
    char const* const c = "case 1 (Dual Wield class thresholds)";
    BeginCase(c);
    CHECK(c, S::DualWieldLevelForClass(std::uint8_t(S::PlayerClass::Warrior)) == 10);
    CHECK(c, S::DualWieldLevelForClass(std::uint8_t(S::PlayerClass::Rogue)) == 10);
    CHECK(c, S::DualWieldLevelForClass(std::uint8_t(S::PlayerClass::Hunter)) == 20);
    // paladin, priest, shaman, mage, warlock, druid: no Dual Wield in this game
    for (std::uint8_t cls : { 2, 5, 7, 8, 9, 11 })
        CHECK(c, S::DualWieldLevelForClass(cls) == 0);
}

static void TestOwedAtLevel()
{
    char const* const c = "case 2 (Dual Wield owed only at the class level)";
    BeginCase(c);
    CHECK(c, !S::DualWieldOwed(std::uint8_t(S::PlayerClass::Warrior), 9));
    CHECK(c, S::DualWieldOwed(std::uint8_t(S::PlayerClass::Warrior), 10));
    CHECK(c, !S::DualWieldOwed(std::uint8_t(S::PlayerClass::Rogue), 9));
    CHECK(c, S::DualWieldOwed(std::uint8_t(S::PlayerClass::Rogue), 10));
    CHECK(c, S::DualWieldOwed(std::uint8_t(S::PlayerClass::Rogue), 60));
    CHECK(c, !S::DualWieldOwed(std::uint8_t(S::PlayerClass::Hunter), 19));
    CHECK(c, S::DualWieldOwed(std::uint8_t(S::PlayerClass::Hunter), 20));
    CHECK(c, S::DualWieldOwed(std::uint8_t(S::PlayerClass::Hunter), 60));
    CHECK(c, !S::DualWieldOwed(std::uint8_t(S::PlayerClass::Warrior), 0));
    // A class without the ability is never owed it, whatever its level.
    CHECK(c, !S::DualWieldOwed(2, 60));
    CHECK(c, !S::DualWieldOwed(7, 60));
    CHECK(c, !S::DualWieldOwed(0, 60));
}

static void TestHunterRangedSkill()
{
    char const* const c = "case 3 (hunter ranged weapon skill seed)";
    BeginCase(c);
    // 45 Bows, 46 Guns, 226 Crossbows; anything that is not an Auto Shot weapon falls back to Bows.
    CHECK(c, S::HunterRangedSkill(45) == 45);
    CHECK(c, S::HunterRangedSkill(46) == 46);
    CHECK(c, S::HunterRangedSkill(226) == 226);
    CHECK(c, S::HunterRangedSkill(0) == 45);
    CHECK(c, S::HunterRangedSkill(228) == 45);   // wands: not an Auto Shot weapon, the starter bow is the fallback
    // Only a hunter that knows none of the three is owed a skill.
    CHECK(c, S::HunterRangedSkillOwed(std::uint8_t(S::PlayerClass::Hunter), 0, 0, 0));
    CHECK(c, !S::HunterRangedSkillOwed(std::uint8_t(S::PlayerClass::Hunter), 1, 0, 0));
    CHECK(c, !S::HunterRangedSkillOwed(std::uint8_t(S::PlayerClass::Hunter), 0, 300, 0));
    CHECK(c, !S::HunterRangedSkillOwed(std::uint8_t(S::PlayerClass::Hunter), 0, 0, 1));
    // Every other class keeps exactly the weapon skills the world seeded.
    for (std::uint8_t cls : { 1, 2, 4, 5, 6, 7, 8, 9, 11 })
        CHECK(c, !S::HunterRangedSkillOwed(cls, 0, 0, 0));
}

int main()
{
    TestClassLevels();
    TestOwedAtLevel();
    TestHunterRangedSkill();

    int failed = 0;
    for (auto const& [name, ok] : g_cases)
    {
        std::printf("%s: %s\n", name.c_str(), ok ? "PASS" : "FAIL");
        if (!ok)
            ++failed;
    }
    std::printf("%d checks, %d failed\n", g_checks, failed);
    return failed ? 1 : 0;
}
