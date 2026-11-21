// Standalone test of the W4-A2 zone-level rule (ai/playerbot/policy/travel/ZoneLevelPolicy.h):
//   g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/test_zone_level_policy.cpp -o /tmp/test_zone_level_policy && /tmp/test_zone_level_policy
//
// The cases that matter are the ones that made the legacy fallback wrong: four spawns must answer the parent zone rather
// than a median of four, elites and critters must not move the median, and an area with nothing known must answer -1
// (unknown is never a travel target).
#include "ai/playerbot/policy/travel/ZoneLevelPolicy.h"

#include <cstdio>
#include <vector>

namespace zl = ai::zone_level;

static int g_checks = 0, g_fail = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_EQ(a, b) do { ++g_checks; auto va = (a); auto vb = (b); if (!(va == vb)) { ++g_fail; std::printf("FAIL %s:%d: %s == %s (%lld vs %lld)\n", __FILE__, __LINE__, #a, #b, (long long)va, (long long)vb); } } while (0)

namespace
{
zl::Sample Mob(int level)     { zl::Sample s; s.level = level; s.hostile = true; return s; }
zl::Sample Elite(int level)   { zl::Sample s = Mob(level); s.elite = true; return s; }
zl::Sample Critter(int level) { zl::Sample s = Mob(level); s.critter = true; return s; }
zl::Sample Friendly(int level){ zl::Sample s; s.level = level; s.hostile = false; return s; }

zl::Inputs WithParent(int parent)
{
    zl::Inputs in;
    in.parentLevel = parent;
    return in;
}
}

int main()
{
    // Five qualifying spawns: the median, not the parent.
    {
        zl::Inputs in = WithParent(20);
        for (int level : {4, 5, 6, 7, 8})
            in.samples.push_back(Mob(level));
        CHECK_EQ(zl::Resolve(in), 6);
    }

    // Four qualifying spawns answer the parent zone: one entry (or a handful of spawns) says nothing about the area.
    {
        zl::Inputs in = WithParent(12);
        for (int level : {40, 41, 42, 43})
            in.samples.push_back(Mob(level));
        CHECK_EQ(zl::Resolve(in), 12);
    }

    // Elites are a different fight, not the area's baseline: five elite 60s and four mobs leave four samples, so the
    // parent zone answers; add one mob and the median is the mobs', never the elites'.
    {
        zl::Inputs in = WithParent(9);
        for (int level : {60, 60, 60, 60, 60})
            in.samples.push_back(Elite(level));
        for (int level : {3, 4, 5, 6})
            in.samples.push_back(Mob(level));
        CHECK_EQ(zl::Resolve(in), 9);

        in.samples.push_back(Mob(7));
        CHECK_EQ(zl::Resolve(in), 5);
    }

    // Critters have zero XP and are never a travel target: ten critters do not make an area level 1.
    {
        zl::Inputs in = WithParent(15);
        for (int i = 0; i < 10; ++i)
            in.samples.push_back(Critter(1));
        for (int level : {7, 8, 9, 10, 11})
            in.samples.push_back(Mob(level));
        CHECK_EQ(zl::Resolve(in), 9);
    }

    // Friendly NPCs are not evidence of a fighting area either: a town of ten level-30 civilians does not rate a zone.
    {
        zl::Inputs in = WithParent(18);
        for (int i = 0; i < 10; ++i)
            in.samples.push_back(Friendly(30));
        CHECK_EQ(zl::Resolve(in), 18);
    }

    // Unknown levels are not samples.
    {
        zl::Inputs in = WithParent(0);
        in.explorationLevel = 6;
        for (int i = 0; i < 5; ++i)
            in.samples.push_back(Mob(0));
        CHECK_EQ(zl::Resolve(in), 6);
    }

    // Even counts take the lower middle: {2,3,4,5,6,7} -> 4 (the floor side a bot can fight).
    {
        zl::Inputs in = WithParent(30);
        for (int level : {2, 3, 4, 5, 6, 7})
            in.samples.push_back(Mob(level));
        CHECK_EQ(zl::Resolve(in), 4);
    }

    // The DBC exploration level is the last value before unknown, and only when the parent has none.
    {
        zl::Inputs in;
        in.explorationLevel = 11;
        CHECK_EQ(zl::Resolve(in), 11);

        in.parentLevel = 3;
        CHECK_EQ(zl::Resolve(in), 3);
    }

    // Nothing known: -1, which callers read as "never a travel target".
    {
        zl::Inputs in;
        CHECK_EQ(zl::Resolve(in), -1);

        for (int level : {1, 2, 3, 4})
            in.samples.push_back(Mob(level));
        CHECK_EQ(zl::Resolve(in), -1);
    }

    // A median is a median of the qualified set only: the same five mobs with surrounded elites keep their answer.
    {
        zl::Inputs in = WithParent(1);
        for (int i = 0; i < 200; ++i)
            in.samples.push_back(Elite(60));
        for (int level : {10, 10, 12, 14, 30})
            in.samples.push_back(Mob(level));
        CHECK_EQ(zl::Resolve(in), 12);
    }

    std::printf("%d checks, %d failed\n", g_checks, g_fail);
    return g_fail == 0 ? 0 : 1;
}
