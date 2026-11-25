#pragma once
// Raid groups on the host: when a host join may add a member, turn a full party into a raid first, or must refuse,
// and how a group's members become the input of RaidLayout::Plan (runtime/RaidLayoutPolicy.h). Plain data and the
// standard library only, so tools/test_forever_raid_groups.cpp runs it without a core.

#include "../../../runtime/RaidLayoutPolicy.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
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
// One member slot of the group, for the subgroup moves of a layout: the seat it holds now and, for a member the layout
// planned (one in the world), the layout's subgroup. A member that is not in the world still holds its seat in the
// saved group, so it is planned where it sits: PlanSubgroupMoves then counts it and never plans a move into its subgroup
// that would make six (review #187, item 1).
struct SeatFacts
{
    uint8_t current = 0;
    bool planned = false;
    uint8_t plannedSub = 0;
};

// The input of RaidLayout::PlanSubgroupMoves for every member slot, the id being the slot's index plus one.
inline std::vector<RaidLayout::SubgroupMember> LayoutMoves(std::vector<SeatFacts> const& seats)
{
    std::vector<RaidLayout::SubgroupMember> out;
    out.reserve(seats.size());
    for (size_t i = 0; i < seats.size(); ++i)
        out.push_back({static_cast<uint64_t>(i + 1), seats[i].current, seats[i].planned ? seats[i].plannedSub : seats[i].current});
    return out;
}

// The ops applied the way the host applies them to a Group: a swap always, a plain move only when its subgroup has a
// free seat (the host checks Group::HasFreeSlotSubGroup first: Group::ChangeMembersGroup itself does not check, only
// the client's handler does). `at` holds each slot's subgroup (index = id - 1) and is updated. Returns the moves
// skipped because the subgroup was full.
inline uint32_t ApplyMovesGuarded(std::vector<uint8_t>& at, std::vector<RaidLayout::SubgroupOp> const& ops)
{
    uint32_t skipped = 0;
    for (RaidLayout::SubgroupOp const& op : ops)
    {
        if (op.member == 0 || op.member > at.size())
            continue;
        if (op.swap)
        {
            if (op.other != 0 && op.other <= at.size())
                std::swap(at[op.member - 1], at[op.other - 1]);
            continue;
        }
        uint32_t seated = 0;
        for (uint8_t s : at)
            seated += s == op.sub ? 1 : 0;
        if (op.sub >= RaidLayout::kSubgroups || seated >= RaidLayout::kSubgroupSize)
        {
            ++skipped;
            continue;
        }
        at[op.member - 1] = op.sub;
    }
    return skipped;
}

// What a layout leaves on a raid for the members that join it later (review #187, item 2): its tanks (the main tank
// first), which healer heals which tank, and the raid healers. `Id` is the caller's member id (an ObjectGuid on the host).
// Before a joiner is placed the book is pruned to the raid as it is now, as the legacy PlaceJoinerInRaidLayout counted
// only present members: a tank that left (or is no tank any more) is dropped with its healer pairs, a healer that left
// is no longer counted.
template <typename Id>
struct HealBook
{
    std::vector<Id> tanks;
    std::vector<std::pair<Id, Id>> tankHealers;   // healer, tank
    std::vector<Id> raidHealers;
};

// Returns the healers still in the raid whose tank was dropped: they heal the raid now (they are moved to the raid healers),
// and the caller clears their focus list (review #187 re-review: a stale list makes a healer heal only its old tank).
template <typename Id>
std::vector<Id> PruneHealBook(HealBook<Id>& book, std::vector<Id> const& tanksNow, std::vector<Id> const& healersNow)
{
    auto const has = [](std::vector<Id> const& v, Id const& id) { return std::find(v.begin(), v.end(), id) != v.end(); };
    book.tanks.erase(std::remove_if(book.tanks.begin(), book.tanks.end(), [&](Id const& t) { return !has(tanksNow, t); }), book.tanks.end());
    std::vector<Id> orphans;
    book.tankHealers.erase(std::remove_if(book.tankHealers.begin(), book.tankHealers.end(),
        [&](std::pair<Id, Id> const& p)
        {
            if (!has(healersNow, p.first))
                return true;
            if (has(book.tanks, p.second))
                return false;
            orphans.push_back(p.first);
            return true;
        }), book.tankHealers.end());
    book.raidHealers.erase(std::remove_if(book.raidHealers.begin(), book.raidHealers.end(),
        [&](Id const& h) { return !has(healersNow, h); }), book.raidHealers.end());
    for (Id const& h : orphans)
        if (!has(book.raidHealers, h))
            book.raidHealers.push_back(h);
    return orphans;
}

// A member left the raid (GroupScript::OnRemoveMember): a tank goes with its healer pairs and its healers become raid healers
// (returned: their focus list must be cleared); a healer goes from the book.
template <typename Id>
std::vector<Id> ForgetMember(HealBook<Id>& book, Id const& member)
{
    std::vector<Id> orphans;
    book.tanks.erase(std::remove(book.tanks.begin(), book.tanks.end(), member), book.tanks.end());
    book.tankHealers.erase(std::remove_if(book.tankHealers.begin(), book.tankHealers.end(),
        [&](std::pair<Id, Id> const& p)
        {
            if (p.first == member)
                return true;
            if (p.second != member)
                return false;
            orphans.push_back(p.first);
            return true;
        }), book.tankHealers.end());
    book.raidHealers.erase(std::remove(book.raidHealers.begin(), book.raidHealers.end(), member), book.raidHealers.end());
    for (Id const& h : orphans)
        if (std::find(book.raidHealers.begin(), book.raidHealers.end(), h) == book.raidHealers.end())
            book.raidHealers.push_back(h);
    return orphans;
}

// A tank joins: listed once, even when it rejoins (re-review low: a rejoining tank was listed twice).
template <typename Id>
void AddTankOnce(HealBook<Id>& book, Id const& tank)
{
    if (std::find(book.tanks.begin(), book.tanks.end(), tank) == book.tanks.end())
        book.tanks.push_back(tank);
}

// The tank a healer's focus list must name after the book changed; false: none (a raid healer, or not in the book), so its list is cleared.
template <typename Id>
bool FocusTankOf(HealBook<Id> const& book, Id const& healer, Id& tank)
{
    for (auto const& p : book.tankHealers)
        if (p.first == healer)
        {
            tank = p.second;
            return true;
        }
    return false;
}

template <typename Id>
struct HealPick
{
    bool tank = false;   // false: the joiner heals the raid
    Id id{};             // the tank it heals
};

// The joining healer's target from the (pruned) book, by RaidLayout::PickJoinerHealTank, and the book updated with it.
template <typename Id>
HealPick<Id> PickAndRecordHealer(HealBook<Id>& book, Id const& healer)
{
    std::vector<uint32_t> perTank(book.tanks.size(), 0);
    for (auto const& p : book.tankHealers)
        for (size_t t = 0; t < book.tanks.size(); ++t)
            if (book.tanks[t] == p.second)
                ++perTank[t];
    int const t = RaidLayout::PickJoinerHealTank(perTank, static_cast<uint32_t>(book.raidHealers.size()));
    HealPick<Id> pick;
    if (t >= 0 && static_cast<size_t>(t) < book.tanks.size())
    {
        pick.tank = true;
        pick.id = book.tanks[static_cast<size_t>(t)];
        book.tankHealers.emplace_back(healer, pick.id);
    }
    else
        book.raidHealers.push_back(healer);
    return pick;
}
}
}
