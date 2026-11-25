// Standalone unit tests for forever/src/host/TbTalentsPure.h.
#include "forever/src/host/TbTalentsPure.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <set>
#include <string>
#include <unordered_map>

using namespace TortoiseBots::talents;

int main()
{
    int fails = 0;
    auto check = [&](bool cond, char const* msg)
    {
        if (!cond)
        {
            std::printf("FAIL: %s\n", msg);
            ++fails;
        }
    };

    // 1. All 9 classes x 3 tabs have non-empty builds.
    uint8_t const classes[] = { 1, 2, 3, 4, 5, 7, 8, 9, 11 };
    for (uint8_t cls : classes)
    {
        for (int tab = 0; tab < 3; ++tab)
        {
            if (BuildStepCount(cls, tab) == 0)
            {
                std::printf("FAIL: class %u tab %d has 0 build steps\n", unsigned(cls), tab);
                ++fails;
            }
        }
    }

    // 2. Forced spec (.tb spec) always wins, even if points are spent elsewhere.
    check(PickSpecTabForBot(1, 100, 2, 10, 0, 0) == 2, "forced spec overrides spent points");
    check(PickSpecTabForBot(8, 100, 0, 0, 0, 0) == 0, "forced spec overrides fresh pick");

    // 3. Once points are spent (and no override), keep the tab with most points.
    check(PickSpecTabForBot(1, 100, -1, 5, 0, 0) == 0, "spent tab 0 kept");
    check(PickSpecTabForBot(1, 100, -1, 2, 10, 0) == 1, "spent tab 1 kept");
    check(PickSpecTabForBot(1, 100, -1, 0, 0, 1) == 2, "spent tab 2 kept");

    // 4. Fresh bot (0 spent, no override) gets a deterministic tab in 0..2, and varies across guids.
    for (uint8_t cls : classes)
    {
        int const p1 = PickSpecTabForBot(cls, 42, -1, 0, 0, 0);
        int const p2 = PickSpecTabForBot(cls, 42, -1, 0, 0, 0);
        check(p1 >= 0 && p1 <= 2 && p1 == p2, "fresh pick is deterministic and in 0..2");

        std::set<int> seen;
        for (uint32_t g = 1; g <= 200; ++g)
            seen.insert(PickSpecTabForBot(cls, g, -1, 0, 0, 0));
        check(seen.size() == 3, "fresh pick covers all 3 tabs across 200 guids");
    }

    // 4b. The pool's census spec roll (TortoiseBots.Pool.CensusSpec): 1000 guids per class land within 3
    // percentage points of the live spec share (spec-population.csv, per mille in kCensusSpecWeights).
    for (uint8_t cls : classes)
    {
        int counts[3] = {};
        for (uint32_t g = 1; g <= 1000; ++g)
        {
            int const tab = PickSpecTabForBot(cls, g, -1, 0, 0, 0, true);
            if (tab >= 0 && tab <= 2)
                ++counts[tab];
        }
        for (int tab = 0; tab < 3; ++tab)
        {
            double const expected = CensusSpecSharePm(cls, tab) / 10.0;
            double const got = counts[tab] / 10.0;
            check(counts[tab] > 0, "census spec roll reaches every tab");
            if (std::fabs(got - expected) > 3.0)
                std::printf("FAIL: class %u tab %d: pool %.1f%% census %.1f%%\n", unsigned(cls), tab, got, expected);
            check(std::fabs(got - expected) <= 3.0, "census spec roll within 3 points of the file");
        }
    }

    // 5. PlanNextSteps for Warrior Arms (class 1, tab 0) produces single-point steps in build order.
    {
        std::unordered_map<uint32_t, uint8_t> empty;
        auto steps = PlanNextSteps(1, 0, empty);
        check(steps.size() == 51, "warrior arms has 51 single-point steps");
        if (steps.size() >= 9)
        {
            // Deflection 130 x5, Improved Rend 127 x3, then Improved Charge 126 (talent-builds.csv).
            for (int i = 0; i < 5; ++i)
                check(steps[i] == 130, "arms first 5 points are 130");
            for (int i = 5; i < 8; ++i)
                check(steps[i] == 127, "arms next 3 points are 127");
            check(steps[8] == 126, "arms 9th point is 126");
        }

        // Partial progress: 3 points already in 130 -> next 2 should be 130, then 127.
        std::unordered_map<uint32_t, uint8_t> partial = { { 130, 3 } };
        auto rem = PlanNextSteps(1, 0, partial);
        check(rem.size() == 48, "partial progress leaves 48 steps");
        if (rem.size() >= 3)
        {
            check(rem[0] == 130 && rem[1] == 130 && rem[2] == 127, "partial progress resumes at point 4");
        }
    }

    // 5b. Every class and tab spends exactly 51 points at level 60.
    for (uint8_t cls : classes)
        for (int tab = 0; tab < 3; ++tab)
            if (PlanNextSteps(cls, tab, {}).size() != 51)
            {
                std::printf("FAIL: class %u tab %d does not plan 51 points\n", unsigned(cls), tab);
                ++fails;
            }

    // 5c. A Forever-only talent is named by its trait node ID (Weaponmaster, node 105944, warrior arms), and its
    //     progress is read under that ID.
    {
        check(IsTraitNodeStepId(105944) && !IsTraitNodeStepId(641), "node ids and Talent.db2 ids are told apart");
        auto steps = PlanNextSteps(1, 0, {});
        check(std::count(steps.begin(), steps.end(), 105944u) == 5, "arms plans 5 points of Weaponmaster by node id");
        std::unordered_map<uint32_t, uint8_t> done = { { 105944, 5 } };
        auto rem = PlanNextSteps(1, 0, done);
        check(std::count(rem.begin(), rem.end(), 105944u) == 0, "a filled node-id step is not planned again");
    }

    // 6. SpecName returns known names and "none" for unset/out-of-range tab.
    check(std::string(SpecName(1, 2)) == "protection", "warrior tab 2 is protection");
    check(std::string(SpecName(5, 1)) == "holy", "priest tab 1 is holy");
    check(std::string(SpecName(3, -1)) == "none", "unset tab -1 is none");
    check(std::string(SpecName(1, 3)) == "none", "out-of-range tab 3 is none");

    // 7. Classic 1.60.1 Talent.db2 leaves ClassID=0; TalentMatchesClass accepts 0 or exact class.
    check(TalentMatchesClass(0, 1), "ClassID=0 matches warrior");
    check(TalentMatchesClass(0, 11), "ClassID=0 matches druid");
    check(TalentMatchesClass(1, 1), "ClassID=1 matches warrior");
    check(!TalentMatchesClass(2, 1), "ClassID=2 does not match warrior");

    // 8. Fallback ordering puts mainTab first, then other tabs, each in row order (posY asc, posX asc),
    //    and filters out invalid tabs or zero-rank/invalid nodes.
    {
        std::vector<FallbackNode> nodes = {
            { 10, 100, 5, 200, 600, 0 }, // tab 0, row 1
            { 11, 101, 5, 100,   0, 0 }, // tab 0, row 0
            { 20, 200, 3, 100, 600, 1 }, // tab 1, row 1
            { 21, 201, 5, 300,   0, 1 }, // tab 1, row 0, right
            { 22, 202, 5, 100,   0, 1 }, // tab 1, row 0, left
            { 30, 300, 5, 100,   0, 2 }, // tab 2, row 0
            { 99, 999, 5, 100,   0, -1 }, // invalid tab (hidden)
            { 98, 998, 0, 100,   0, 1 }, // 0 maxRanks
        };
        auto ordered = OrderFallbackNodes(nodes, 1);
        check(ordered.size() == 6, "fallback filters invalid tab and 0 maxRanks");
        if (ordered.size() == 6)
        {
            check(ordered[0].traitNodeId == 22, "mainTab row 0 left first");
            check(ordered[1].traitNodeId == 21, "mainTab row 0 right second");
            check(ordered[2].traitNodeId == 20, "mainTab row 1 third");
            check(ordered[3].traitNodeId == 11, "other tab 0 row 0 fourth");
            check(ordered[4].traitNodeId == 10, "other tab 0 row 1 fifth");
            check(ordered[5].traitNodeId == 30, "other tab 2 row 0 sixth");
        }
    }

    if (fails == 0)
        std::printf("PASS: test_talents (all checks passed)\n");
    else
        std::printf("FAIL: test_talents (%d failed)\n", fails);
    return fails ? 1 : 0;
}
