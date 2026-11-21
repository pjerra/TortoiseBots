#include "../forever/src/host/TbRaidGroupsPure.h"
#include <cassert>
#include <iostream>
#include <map>
using namespace TortoiseBots;
using namespace TortoiseBots::raidgroups;
using Role = RaidLayout::Role;

static MemberFacts M(std::string name, Role role, uint8_t cls, bool leader = false, bool mt = false)
{
    MemberFacts m;
    m.name = std::move(name);
    m.role = role;
    m.cls = cls;
    m.groupLeader = leader;
    m.mainTank = mt;
    return m;
}

int main()
{
    // Raids off: the 5-man party the host always had.
    assert(PlanJoin(false, false, 4).step == JoinStep::Add);
    assert(PlanJoin(false, false, 5).step == JoinStep::Refuse && PlanJoin(false, false, 5).code == "party-full");
    assert(PlanJoin(false, true, 6).step == JoinStep::Refuse && PlanJoin(false, true, 6).code == "raids-off");
    assert(MaxNamedMembers(false) == 4);
    // Raids on: a full party becomes a raid, a raid takes up to 40.
    assert(PlanJoin(true, false, 1).step == JoinStep::Add);
    assert(PlanJoin(true, false, 5).step == JoinStep::ConvertThenAdd);
    assert(PlanJoin(true, true, 39).step == JoinStep::Add);
    assert(PlanJoin(true, true, 40).step == JoinStep::Refuse && PlanJoin(true, true, 40).code == "raid-full");
    assert(MaxNamedMembers(true) == 39);

    // Main tank: the marked tank, else a tank leader, else the first tank; none without a tank.
    std::vector<MemberFacts> members = {M("Lead", Role::Damage, 11, true), M("Ta", Role::Tank, 1), M("Tb", Role::Tank, 1, false, true)};
    assert(PickMainTank(members) == 2);
    members[2].mainTank = false;
    assert(PickMainTank(members) == 1);
    members[0].role = Role::Tank;
    assert(PickMainTank(members) == 0);
    assert(PickMainTank({M("A", Role::Damage, 4), M("B", Role::Healer, 5)}) == -1);
    members[2].mainTank = true;
    members[2].role = Role::Damage;   // a mark on a non-tank does not count
    assert(PickMainTank(members) == 0);

    // A 20-member raid (4 tanks, 5 healers, 11 damage dealers) led by a damage dealer: the layout plans every member,
    // the main tank and each off-tank head subgroups 1-4, no subgroup holds more than five, and the plan's moves place
    // everyone from the order the joins left them in (five per subgroup in join order).
    std::vector<MemberFacts> raid;
    raid.push_back(M("Leader", Role::Damage, 8, true));
    for (int i = 0; i < 4; ++i)
        raid.push_back(M("Tank" + std::to_string(i), Role::Tank, 1));
    uint8_t healerClasses[] = {5, 5, 7, 11, 11};
    for (int i = 0; i < 5; ++i)
        raid.push_back(M("Heal" + std::to_string(i), Role::Healer, healerClasses[i]));
    for (int i = 0; i < 10; ++i)
        raid.push_back(M("Dps" + std::to_string(i), Role::Damage, 4));
    assert(raid.size() == 20);
    int const mt = PickMainTank(raid);
    assert(mt == 1);
    RaidLayout::Layout layout = RaidLayout::Plan(LayoutMembers(raid, mt), true);
    assert(layout.ok);
    assert(layout.placements.size() == 20);
    std::map<uint8_t, int> perSub;
    for (auto const& p : layout.placements)
        ++perSub[p.subgroup];
    for (auto const& [sub, count] : perSub)
        assert(sub < 8 && count <= 5);
    for (int t = 0; t < 4; ++t)
    {
        assert(layout.placements[1 + t].tankIndex == t);
        assert(layout.placements[1 + t].subgroup == t);
    }
    int tankHealers = 0, raidHealers = 0;
    for (auto const& p : layout.placements)
        if (p.role == Role::Healer)
            (p.healsTank >= 0 ? tankHealers : raidHealers) += 1;
    assert(raidHealers >= int(RaidLayout::RaidHealerMinimum(5)));
    assert(tankHealers + raidHealers == 5);

    std::vector<RaidLayout::SubgroupMember> moves;
    std::vector<uint8_t> at(20);
    for (size_t i = 0; i < 20; ++i)
    {
        at[i] = uint8_t(i / 5);
        moves.push_back({uint64_t(i + 1), at[i], layout.placements[i].subgroup});
    }
    for (auto const& op : RaidLayout::PlanSubgroupMoves(moves))
    {
        uint32_t counts[8] = {};
        for (uint8_t s : at)
            ++counts[s];
        if (op.swap)
            std::swap(at[op.member - 1], at[op.other - 1]);
        else
        {
            assert(counts[op.sub] < 5);   // a move never overfills (the core ignores it then)
            at[op.member - 1] = op.sub;
        }
    }
    for (size_t i = 0; i < 20; ++i)
        assert(at[i] == layout.placements[i].subgroup);

    std::vector<std::string> names;
    std::vector<uint8_t> subs;
    for (auto const& p : layout.placements)
    {
        names.push_back(p.name);
        subs.push_back(p.subgroup);
    }
    std::string const line = SubgroupLine(names, subs);
    assert(line.rfind("1=", 0) == 0 && line.find("Tank0") != std::string::npos && line.find(";4=") != std::string::npos);
    assert(SubgroupLine({"A", "B", "C"}, {2, 0, 2}) == "1=B;3=A,C");

    std::cout << "PASS: raid joins (party cap, convert, raid cap), main tank pick, a 20-member layout and its subgroup moves\n";
    return 0;
}
