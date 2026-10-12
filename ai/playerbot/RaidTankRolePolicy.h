#pragma once

#include <cstddef>
#include <vector>

// RaidTankRolePolicy — one order for the group's tank jobs: the main tank
// first, then the assist tanks. Shared by the `main tank` value and the boss
// scripts that split tank jobs (Golemagg boss/rager camps, Razorgore hold),
// which each used to run their own "first live tank by slot" loop and ignored
// the raid's Main Tank flag.
//
// Donor behaviour (mod-playerbots @ 79bd428, read-only reference,
// src/Bot/PlayerbotAI.cpp): `GetMainTankGuid` (2497-2516) takes the raid
// main-tank flag, else the first live tank; `IsAssistTankOfIndex`
// (2622-2672) orders the other tanks with promoted raid assistants first,
// then the rest, each in slot order. Vanilla's group also stores a Main
// Assist (`Group::GetMainAssistantGuid`); when that member is a tank it is
// the next tank job after the Main Tank.
//
// No core includes: the caller resolves each group slot into plain flags so
// the order stays testable in tools/test_raid_tank_role_policy.cpp.

namespace ai
{
    struct RaidTankCandidate
    {
        bool usable;           // online, alive, same map and instance as the asking bot
        bool isTank;           // PlayerbotAI::IsTank (forced role, tank strategy, talents)
        bool isMainTankFlag;   // raid Main Tank (Group::GetMainTankGuid)
        bool isMainAssistFlag; // raid Main Assist (Group::GetMainAssistantGuid)
        bool isRaidAssistant;  // promoted raid assistant (Group::IsAssistant)
    };

    // Slot indices in tank-job order: [0] the main tank, [1..] assist tanks.
    // Main tank:
    //  - the raid Main Tank, whatever its spec: an explicit order from the
    //    raid leader (the `main tank` value always honoured it);
    //  - else the raid Main Assist, if it is a tank (a DPS main assist calls
    //    targets, it takes no tank job);
    //  - else the first tank in group slot order (the old election).
    // Assist tanks, after the main tank: the Main Assist if it is a tank,
    // then promoted raid assistants, then every other tank, in slot order.
    // Members that are not usable are skipped, so a dead or absent Main Tank
    // hands the job to the next one in this order.
    inline std::vector<std::size_t> OrderRaidTankRoles(std::vector<RaidTankCandidate> const& slots)
    {
        std::vector<std::size_t> order;
        std::vector<bool> taken(slots.size(), false);

        auto takeFirst = [&](auto matches)
        {
            for (std::size_t i = 0; i < slots.size(); ++i)
            {
                if (!taken[i] && slots[i].usable && matches(slots[i]))
                {
                    taken[i] = true;
                    order.push_back(i);
                    return true;
                }
            }
            return false;
        };
        auto takeAll = [&](auto matches)
        {
            for (std::size_t i = 0; i < slots.size(); ++i)
            {
                if (!taken[i] && slots[i].usable && matches(slots[i]))
                {
                    taken[i] = true;
                    order.push_back(i);
                }
            }
        };

        auto mainTankFlag = [](RaidTankCandidate const& s) { return s.isMainTankFlag; };
        auto mainAssistTank = [](RaidTankCandidate const& s) { return s.isTank && s.isMainAssistFlag; };
        auto promotedTank = [](RaidTankCandidate const& s) { return s.isTank && s.isRaidAssistant; };
        auto anyTank = [](RaidTankCandidate const& s) { return s.isTank; };

        // Main tank.
        if (!takeFirst(mainTankFlag) && !takeFirst(mainAssistTank))
            takeFirst(anyTank);

        // Assist tanks.
        takeFirst(mainAssistTank);
        takeAll(promotedTank);
        takeAll(anyTank);

        return order;
    }
}
