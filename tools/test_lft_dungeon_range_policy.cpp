#include "../runtime/policy/lfg/LftDungeonRangePolicy.h"

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>

#define CHECK(x) do { \
    ++checks; \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using TortoiseBots::LftDungeonRange;
using TortoiseBots::LftDungeonRangeTable;
using TortoiseBots::LftLevelRange;
using TortoiseBots::NormalizeLftToken;

namespace
{
int checks = 0;

static void TestLookups()
{
    LftLevelRange const* rfc = LftDungeonRange("Ragefire Chasm");
    CHECK(rfc && rfc->minLevel == 13 && rfc->maxLevel == 19);
    CHECK(LftDungeonRange("RFC") && LftDungeonRange("RFC")->minLevel == rfc->minLevel && LftDungeonRange("RFC")->maxLevel == rfc->maxLevel);
    CHECK(LftDungeonRange("dm") && LftDungeonRange("dm")->minLevel == 16 && LftDungeonRange("dm")->maxLevel == 24);
    CHECK(LftDungeonRange("stockade") && LftDungeonRange("stockade")->minLevel == 23);
    CHECK(LftDungeonRange("cotbm") && LftDungeonRange("cotbm")->minLevel == 60);
    CHECK(LftDungeonRange("dmr") && LftDungeonRange("dmr")->minLevel == 26 && LftDungeonRange("dmr")->maxLevel == 35);
    CHECK(LftDungeonRange("Dragonmaw Retreat") && LftDungeonRange("Dragonmaw Retreat")->minLevel == LftDungeonRange("dmr")->minLevel && LftDungeonRange("Dragonmaw Retreat")->maxLevel == LftDungeonRange("dmr")->maxLevel);
    CHECK(LftDungeonRange("swc") && LftDungeonRange("swc")->minLevel == 32 && LftDungeonRange("swc")->maxLevel == 40);
    CHECK(LftDungeonRange("Stormwrought Castle") && LftDungeonRange("Stormwrought Castle")->minLevel == LftDungeonRange("swc")->minLevel && LftDungeonRange("Stormwrought Castle")->maxLevel == LftDungeonRange("swc")->maxLevel);
    CHECK(LftDungeonRange("swd") && LftDungeonRange("swd")->minLevel == 35 && LftDungeonRange("swd")->maxLevel == 44);
    CHECK(LftDungeonRange("Stormwrought Descent") && LftDungeonRange("Stormwrought Descent")->minLevel == LftDungeonRange("swd")->minLevel && LftDungeonRange("Stormwrought Descent")->maxLevel == LftDungeonRange("swd")->maxLevel);
    CHECK(LftDungeonRange("emeraldsanctum") == nullptr);
    CHECK(LftDungeonRange("") == nullptr);
    CHECK(NormalizeLftToken("Zul'Farrak") == "zulfarrak");
}

static void TestEveryRangeValid()
{
    CHECK(LftDungeonRangeTable().size() == 86);
    for (auto const& entry : LftDungeonRangeTable())
    {
        CHECK(entry.first == NormalizeLftToken(entry.first));
        CHECK(entry.second.minLevel > 0 && entry.second.minLevel <= entry.second.maxLevel && entry.second.maxLevel <= 60);
    }
}
}

int main()
{
    TestLookups();
    TestEveryRangeValid();
    std::printf("lft dungeon range policy: %d checks passed\n", checks);
    return 0;
}
