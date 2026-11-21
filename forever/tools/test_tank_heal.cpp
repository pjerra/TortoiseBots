// Standalone test for substitute tank role resolution (SliceHybridPolicy.h)
// and multi-healer assignment (HealingTriagePolicy.h):
//   g++ -std=c++17 -O1 -Wall -Wextra -I. -Iai forever/tools/test_tank_heal.cpp -o /tmp/test_tank_heal && /tmp/test_tank_heal
#include "forever/src/slice/SliceHybridPolicy.h"
#include "ai/playerbot/HealingTriagePolicy.h"

#include <cstdio>
#include <vector>

namespace hy = TortoiseBots::slice::hybrid;

static int g_checks = 0, g_fail = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

int main()
{
    // ---- 1. Substitute tank policy ----
    // Party with a warrior tank (defensive stance/shield): no substitute tank.
    {
        std::vector<hy::GroupMemberRoleInfo> party{
            {1, hy::ClassWarrior, 0, false, false, false, true, false},
            {2, hy::ClassPaladin, 0, false, false, false, false, true},
            {3, 5 /*priest*/,     0, false, false, false, false, true},
        };
        CHECK(hy::PickSubstituteTankGuid(party, true) == 0);
        CHECK(hy::ResolveGroupRole(party[1], party, true) == hy::RoleHealer);
    }

    // Party with explicit forced tank role: no substitute tank.
    {
        std::vector<hy::GroupMemberRoleInfo> party{
            {1, hy::ClassWarrior, hy::RoleTank, false, false, false, false, false},
            {2, hy::ClassDruid,   0,            false, false, false, false, true},
            {3, 5 /*priest*/,     0,            false, false, false, false, true},
        };
        CHECK(hy::PickSubstituteTankGuid(party, true) == 0);
    }

    // Party with LFG tank role on paladin: paladin tanks via LFG role.
    {
        std::vector<hy::GroupMemberRoleInfo> party{
            {1, hy::ClassPaladin, 0, true,  false, false, false, true},
            {2, 5 /*priest*/,     0, false, true,  false, false, true},
        };
        CHECK(hy::ResolveGroupRole(party[0], party, true) == hy::RoleTank);
        CHECK(hy::ResolveGroupRole(party[1], party, true) == hy::RoleHealer);
    }

    // Party with no warrior tank, paladin + priest + dps: paladin takes substitute tank role.
    {
        std::vector<hy::GroupMemberRoleInfo> party{
            {10, hy::ClassPaladin, 0, false, false, false, false, true},
            {11, 5 /*priest*/,     0, false, false, false, false, true},
            {12, 8 /*mage*/,       0, false, false, false, false, false},
        };
        CHECK(hy::PickSubstituteTankGuid(party, true) == 10);
        CHECK(hy::ResolveGroupRole(party[0], party, true) == hy::RoleTank);
        CHECK(hy::ResolveGroupRole(party[1], party, true) == hy::RoleHealer);
        CHECK(hy::PickSubstituteTankGuid(party, false) == 0);
    }

    // Party with no warrior tank, druid + shaman + dps: druid takes substitute tank role (bear).
    {
        std::vector<hy::GroupMemberRoleInfo> party{
            {20, hy::ClassDruid,  0, false, false, false, false, true},
            {21, hy::ClassShaman, 0, false, false, false, false, true},
            {22, 4 /*rogue*/,     0, false, false, false, false, false},
        };
        CHECK(hy::PickSubstituteTankGuid(party, true) == 20);
        CHECK(hy::ResolveGroupRole(party[0], party, true) == hy::RoleTank);
        CHECK(hy::ResolveGroupRole(party[1], party, true) == hy::RoleHealer);
    }

    // Party with no warrior tank and NO other healer (solo paladin + 2 dps): paladin stays healer unless tankSignal.
    {
        std::vector<hy::GroupMemberRoleInfo> party{
            {30, hy::ClassPaladin, 0, false, false, false, false, true},
            {31, 8 /*mage*/,       0, false, false, false, false, false},
        };
        CHECK(hy::PickSubstituteTankGuid(party, true) == 0);
        CHECK(hy::ResolveGroupRole(party[0], party, true) == hy::RoleHealer);

        // Without another healer, Righteous Fury / tankSignal alone must not trap the paladin in tank role.
        party[0].tankSignal = true;
        CHECK(hy::PickSubstituteTankGuid(party, true) == 0);
        CHECK(hy::ResolveGroupRole(party[0], party, true) == hy::RoleHealer);

        // Solo paladin with Righteous Fury / tankSignal also stays healer.
        std::vector<hy::GroupMemberRoleInfo> solo{party[0]};
        CHECK(hy::PickSubstituteTankGuid(solo, true) == 0);
        CHECK(hy::ResolveGroupRole(solo[0], solo, true) == hy::RoleHealer);

        // With another healer present, tankSignal selects the paladin as substitute tank.
        party.push_back({32, 5 /*priest*/, 0, false, false, false, false, true});
        CHECK(hy::PickSubstituteTankGuid(party, true) == 30);
        CHECK(hy::ResolveGroupRole(party[0], party, true) == hy::RoleTank);
    }

    // Explicit non-tank forced role on paladin prevents substitute tank promotion.
    {
        std::vector<hy::GroupMemberRoleInfo> party{
            {40, hy::ClassPaladin, hy::RoleHealer, false, false, false, false, true},
            {41, 5 /*priest*/,     0,              false, false, false, false, true},
        };
        CHECK(hy::PickSubstituteTankGuid(party, true) == 0);
        CHECK(hy::ResolveGroupRole(party[0], party, true) == hy::RoleHealer);
    }

    // ---- 2. Multi-healer assignment policy ----
    ai::HealingBands const bands{30.0f, 60.0f, 85.0f, 95.0f};
    // Candidates sorted by HealingPriority: DPS1 (50% low), DPS2 (55% low), Tank (80% medium)
    std::vector<ai::HealingCandidate> const mixed{
        {101, 5000, 10000, 0, false, false, false}, // DPS1
        {102, 5500, 10000, 0, false, false, false}, // DPS2
        {100, 8000, 10000, 0, true,  false, false}, // Tank
    };

    // Single healer = unchanged (picks candidates[0], most urgent overall = DPS1)
    CHECK(mixed[ai::HealingAssignment(mixed, bands, 0, 1)].guid == 101);

    // 2 healers: main healer (0) heals tank (100) first; second healer (1) takes rest of group (101)
    CHECK(mixed[ai::HealingAssignment(mixed, bands, 0, 2)].guid == 100);
    CHECK(mixed[ai::HealingAssignment(mixed, bands, 1, 2)].guid == 101);

    // 3 healers: main healer (0) -> tank (100); others (1, 2) split DPS1 (101) and DPS2 (102)
    CHECK(mixed[ai::HealingAssignment(mixed, bands, 0, 3)].guid == 100);
    CHECK(mixed[ai::HealingAssignment(mixed, bands, 1, 3)].guid == 101);
    CHECK(mixed[ai::HealingAssignment(mixed, bands, 2, 3)].guid == 102);

    // 2 healers, only tank injured: both heal tank
    std::vector<ai::HealingCandidate> const tankOnly{
        {100, 7000, 10000, 0, true, false, false},
    };
    CHECK(tankOnly[ai::HealingAssignment(tankOnly, bands, 0, 2)].guid == 100);
    CHECK(tankOnly[ai::HealingAssignment(tankOnly, bands, 1, 2)].guid == 100);

    // 2 healers, 2 tanks injured in same urgency band (no non-tanks injured): healers split tanks
    std::vector<ai::HealingCandidate> const twoTanks{
        {100, 7000, 10000, 0, true, false, false},
        {101, 7200, 10000, 0, true, false, false},
    };
    CHECK(twoTanks[ai::HealingAssignment(twoTanks, bands, 0, 2)].guid == 100);
    CHECK(twoTanks[ai::HealingAssignment(twoTanks, bands, 1, 2)].guid == 101);

    // 2 healers, tank at full health (no tank in candidates): healers split the group
    std::vector<ai::HealingCandidate> const noTank{
        {101, 5000, 10000, 0, false, false, false},
        {102, 5500, 10000, 0, false, false, false},
    };
    CHECK(noTank[ai::HealingAssignment(noTank, bands, 0, 2)].guid == 101);
    CHECK(noTank[ai::HealingAssignment(noTank, bands, 1, 2)].guid == 102);

    if (g_fail)
    {
        std::printf("RESULT: FAIL (%d of %d checks)\n", g_fail, g_checks);
        return 1;
    }
    std::printf("RESULT: PASS (%d checks)\n", g_checks);
    return 0;
}
