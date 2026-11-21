// Standalone test of spec-choice pure policy (forever/src/slice/SliceSpecPolicy.h & SliceHybridPolicy.h):
//   g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/test_spec_choice.cpp -o /tmp/test_spec_choice && /tmp/test_spec_choice
#include "forever/src/slice/SliceSpecPolicy.h"
#include "forever/src/slice/SliceHybridPolicy.h"

#include <cstdio>
#include <vector>

namespace sp = TortoiseBots::slice::spec;
namespace hy = TortoiseBots::slice::hybrid;

static int g_checks = 0, g_fail = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

int main()
{
    // 1. Node-to-tab layout clustering (matches TbDashboard.cpp:1157-1189)
    {
        std::vector<sp::TraitNodeLayout> nodes;
        // Tab 0 at x=100..700 (8 nodes)
        for (int i = 0; i < 8; ++i)
            nodes.push_back({100 + i, 100 + (i % 4) * 600, 100 + (i / 4) * 600});
        // Tab 1 at x=4000..5800 (8 nodes)
        for (int i = 0; i < 8; ++i)
            nodes.push_back({200 + i, 4000 + (i % 4) * 600, 100 + (i / 4) * 600});
        // Tab 2 at x=8000..9800 (8 nodes)
        for (int i = 0; i < 8; ++i)
            nodes.push_back({300 + i, 8000 + (i % 4) * 600, 100 + (i / 4) * 600});
        // Hidden node far below row 6 (row 10) and orphan node (< 8 in cluster)
        nodes.push_back({900, 12000, 100});
        nodes.push_back({901, 100, 100 + 10 * 600});

        auto const map = sp::BuildNodeTabMap(nodes);
        CHECK(map.size() == 24);
        CHECK(map.at(100) == 0);
        CHECK(map.at(107) == 0);
        CHECK(map.at(200) == 1);
        CHECK(map.at(307) == 2);
        CHECK(map.find(900) == map.end());
        CHECK(map.find(901) == map.end());
    }

    // 2. PickSpecTab: forced override wins, else max points at level >= 10, else class default
    CHECK(sp::PickSpecTab(sp::ClassWarrior, 20, -1, 0, 0, 0) == 0);  // arms default
    CHECK(sp::PickSpecTab(sp::ClassWarrior, 20, -1, 2, 9, 0) == 1);  // fury talents
    CHECK(sp::PickSpecTab(sp::ClassWarrior, 9, -1, 0, 5, 0) == 0);   // under 10 ignores points
    CHECK(sp::PickSpecTab(sp::ClassWarrior, 20, 2, 0, 11, 0) == 2);  // .tb spec overrides talents
    CHECK(sp::PickSpecTab(sp::ClassPriest, 20, -1, 0, 0, 11) == 2);  // shadow
    CHECK(sp::PickSpecTab(sp::ClassMage, 20, -1, 0, 0, 0) == 1);     // fire default
    CHECK(sp::PickSpecTab(sp::ClassMage, 20, -1, 0, 0, 11) == 2);    // frost
    CHECK(sp::PickSpecTab(sp::ClassDruid, 20, -1, 11, 0, 0) == 0);   // balance

    // 3. ResolveKitSpecTab: forced spec wins; otherwise role overrides tank/non-tank prot
    CHECK(sp::ResolveKitSpecTab(sp::ClassWarrior, 20, -1, hy::RoleTank, 11, 0, 0) == 2);
    CHECK(sp::ResolveKitSpecTab(sp::ClassWarrior, 20, -1, hy::RoleDps, 0, 0, 11) == 0);
    CHECK(sp::ResolveKitSpecTab(sp::ClassPaladin, 20, -1, hy::RoleDps, 0, 11, 0) == 2);
    CHECK(sp::ResolveKitSpecTab(sp::ClassPaladin, 20, -1, hy::RoleHealer, 0, 11, 0) == 0);
    CHECK(sp::ResolveKitSpecTab(sp::ClassWarrior, 20, 1, hy::RoleDps, 11, 0, 0) == 1);

    // 4. DefaultSpecRole: DPS specs on hybrid classes resolve to RoleDps, not RoleHealer
    CHECK(sp::DefaultSpecRole(sp::ClassPriest, 20, 2) == hy::RoleDps);     // shadow
    CHECK(sp::DefaultSpecRole(sp::ClassPriest, 20, 0) == hy::RoleHealer);  // disc
    CHECK(sp::DefaultSpecRole(sp::ClassPaladin, 20, 2) == hy::RoleDps);    // ret
    CHECK(sp::DefaultSpecRole(sp::ClassPaladin, 20, 1) == hy::RoleTank);   // prot
    CHECK(sp::DefaultSpecRole(sp::ClassShaman, 20, 0) == hy::RoleDps);     // ele
    CHECK(sp::DefaultSpecRole(sp::ClassShaman, 20, 1) == hy::RoleDps);     // enh
    CHECK(sp::DefaultSpecRole(sp::ClassShaman, 20, 2) == hy::RoleHealer);  // resto
    CHECK(sp::DefaultSpecRole(sp::ClassDruid, 5, 2) == hy::RoleDps);       // under 10 leveling
    CHECK(sp::DefaultSpecRole(sp::ClassDruid, 20, 0) == hy::RoleDps);      // balance
    CHECK(sp::DefaultSpecRole(sp::ClassDruid, 20, 1) == hy::RoleDps);      // cat
    CHECK(sp::DefaultSpecRole(sp::ClassDruid, 20, 2) == hy::RoleHealer);   // resto

    // 5. ResolveGroupRole with defaultRole
    {
        hy::GroupMemberRoleInfo shadow{1, 5 /*priest*/, hy::RoleNone, false, false, false, false, true, hy::RoleDps};
        std::vector<hy::GroupMemberRoleInfo> solo{shadow};
        CHECK(hy::ResolveGroupRole(shadow, solo, true) == hy::RoleDps);
    }

    // 6. ParseSpecArg
    {
        int tab = -99;
        CHECK(sp::ParseSpecArg(sp::ClassWarrior, "1", tab) && tab == 1);
        CHECK(sp::ParseSpecArg(sp::ClassWarrior, "fury", tab) && tab == 1);
        CHECK(sp::ParseSpecArg(sp::ClassWarrior, "none", tab) && tab == -1);
        CHECK(sp::ParseSpecArg(sp::ClassPriest, "shadow", tab) && tab == 2);
        CHECK(sp::ParseSpecArg(sp::ClassDruid, "cat", tab) && tab == 1);
        CHECK(sp::ParseSpecArg(sp::ClassHunter, "bm", tab) && tab == 0);
        CHECK(!sp::ParseSpecArg(sp::ClassWarrior, "shadow", tab));
        CHECK(!sp::ParseSpecArg(sp::ClassWarrior, "3", tab));
    }

    if (g_fail)
    {
        std::printf("RESULT: FAIL (%d of %d checks)\n", g_fail, g_checks);
        return 1;
    }
    std::printf("RESULT: PASS (%d checks)\n", g_checks);
    return 0;
}
