#pragma once
// Raid groups on the host: when a host join may add a member, turn a full party into a raid first, or must refuse,
// and how a group's members become the input of RaidLayout::Plan (runtime/RaidLayoutPolicy.h). Plain data and the
// standard library only, so tools/test_forever_raid_groups.cpp runs it without a core.

#include "../../../runtime/RaidLayoutPolicy.h"

#include <cstdint>
#include <string>
#include <vector>

namespace TortoiseBots
{
namespace raidgroups
{
constexpr uint32_t kPartySize = 5;   // MAX_GROUP_SIZE
constexpr uint32_t kRaidSize = 40;   // MAX_RAID_SIZE

enum class JoinStep : uint8_t { Add, ConvertThenAdd, Refuse };

struct JoinPlan
{
    JoinStep step = JoinStep::Refuse;
    std::string code;   // the refusal: raids-off, party-full, raid-full
};

// One more member for a group that holds `members` now (pending logins counted by the caller). With raids off the
// host keeps the 5-man party it always had and refuses a raid; with them on a full party becomes a raid first (the
// core's Group::ConvertToRaid) and a raid takes members up to 40.
inline JoinPlan PlanJoin(bool raidsOn, bool isRaid, uint32_t members)
{
    JoinPlan plan;
    if (isRaid)
    {
        if (!raidsOn)
            plan.code = "raids-off";
        else if (members >= kRaidSize)
            plan.code = "raid-full";
        else
            plan.step = JoinStep::Add;
        return plan;
    }
    if (members < kPartySize)
        plan.step = JoinStep::Add;
    else if (raidsOn)
        plan.step = JoinStep::ConvertThenAdd;
    else
        plan.code = "party-full";
    return plan;
}

// How many members `.tb party <leader> <member>...` may name: four for a party, 39 once raids are on.
inline uint32_t MaxNamedMembers(bool raidsOn)
{
    return (raidsOn ? kRaidSize : kPartySize) - 1;
}

struct MemberFacts
{
    std::string name;
    RaidLayout::Role role = RaidLayout::Role::Damage;
    uint8_t cls = 0;
    bool groupLeader = false;
    bool mainTank = false;   // the group's main-tank mark (MEMBER_FLAG_MAINTANK)
};

// The main tank of a layout: the tank that already carries the group's main-tank mark, else the group's leader when it
// is a tank, else the first tank in roster order. -1: the group has no tank, and no layout can be planned.
inline int PickMainTank(std::vector<MemberFacts> const& members)
{
    for (size_t i = 0; i < members.size(); ++i)
        if (members[i].mainTank && members[i].role == RaidLayout::Role::Tank)
            return static_cast<int>(i);
    for (size_t i = 0; i < members.size(); ++i)
        if (members[i].groupLeader && members[i].role == RaidLayout::Role::Tank)
            return static_cast<int>(i);
    for (size_t i = 0; i < members.size(); ++i)
        if (members[i].role == RaidLayout::Role::Tank)
            return static_cast<int>(i);
    return -1;
}

inline std::vector<RaidLayout::Member> LayoutMembers(std::vector<MemberFacts> const& members, int mainTank)
{
    std::vector<RaidLayout::Member> out;
    out.reserve(members.size());
    for (size_t i = 0; i < members.size(); ++i)
    {
        RaidLayout::Member m;
        m.name = members[i].name;
        m.role = members[i].role;
        m.cls = members[i].cls;
        m.lead = static_cast<int>(i) == mainTank;
        out.push_back(m);
    }
    return out;
}

// `1=A,B;2=C`: the members of each non-empty subgroup (1-based, as the game shows them), in the order given.
inline std::string SubgroupLine(std::vector<std::string> const& names, std::vector<uint8_t> const& subgroups)
{
    std::vector<std::string> lines(RaidLayout::kSubgroups);
    for (size_t i = 0; i < names.size() && i < subgroups.size(); ++i)
        if (subgroups[i] < RaidLayout::kSubgroups)
            lines[subgroups[i]] += (lines[subgroups[i]].empty() ? "" : ",") + names[i];
    std::string out;
    for (uint8_t sub = 0; sub < RaidLayout::kSubgroups; ++sub)
        if (!lines[sub].empty())
            out += (out.empty() ? "" : ";") + std::to_string(sub + 1) + "=" + lines[sub];
    return out;
}
}
}
