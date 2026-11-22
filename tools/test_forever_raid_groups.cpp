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

    // Review #187 item 1: an offline member still holds its seat. Subgroup 1 holds four members in the world and one
    // offline; the layout wants one more member in the world there. Counted, the offline seat leaves no room and no
    // swap partner, so no move is planned and no subgroup goes above five; the mover is left out (reported misplaced).
    {
        std::vector<SeatFacts> seats;
        for (int i = 0; i < 4; ++i)
            seats.push_back({0, true, 0});            // in the world, planned where they sit (subgroup 1)
        seats.push_back({0, false, 0});               // offline, in subgroup 1
        seats.push_back({1, true, 0});                // in the world, in subgroup 2, planned into subgroup 1
        for (int i = 0; i < 4; ++i)
            seats.push_back({1, true, 1});
        std::vector<RaidLayout::SubgroupMember> const in = LayoutMoves(seats);
        assert(in[4].planned == 0 && in[4].current == 0);   // the offline member is planned where it sits
        std::vector<RaidLayout::SubgroupOp> const ops = RaidLayout::PlanSubgroupMoves(in);
        assert(ops.empty());
        std::vector<uint8_t> at;
        for (auto const& s : seats)
            at.push_back(s.current);
        assert(ApplyMovesGuarded(at, ops) == 0);
        std::map<uint8_t, int> n;
        for (uint8_t s : at)
            ++n[s];
        assert(n[0] == 5 && n[1] == 5 && at[4] == 0 && at[5] == 1);

        // The old input (members in the world only) does plan the move into the full subgroup: the host's
        // HasFreeSlotSubGroup guard then skips it instead of making six.
        std::vector<RaidLayout::SubgroupMember> worldOnly;
        std::vector<uint8_t> at2;
        for (size_t i = 0; i < seats.size(); ++i)
            if (seats[i].planned)
                worldOnly.push_back({uint64_t(i + 1), seats[i].current, seats[i].plannedSub});
        for (auto const& s : seats)
            at2.push_back(s.current);
        std::vector<RaidLayout::SubgroupOp> const naive = RaidLayout::PlanSubgroupMoves(worldOnly);
        assert(naive.size() == 1 && !naive[0].swap && naive[0].sub == 0);
        assert(ApplyMovesGuarded(at2, naive) == 1);
        std::map<uint8_t, int> n2;
        for (uint8_t s : at2)
            ++n2[s];
        assert(n2[0] == 5 && n2[1] == 5);
    }

    // Review #187 item 2: the heal book follows the raid. Main tank 1 has two healers (the most a tank takes), tank 2
    // one, two raid healers. Tank 2 leaves: a joining healer is not pointed at it (unpruned, the book would pick it).
    {
        HealBook<int> book;
        book.tanks = {1, 2};
        book.tankHealers = {{11, 1}, {12, 1}, {21, 2}};
        book.raidHealers = {31, 32};
        HealBook<int> stale = book;
        HealPick<int> const old = PickAndRecordHealer(stale, 40);
        assert(old.tank && old.id == 2);                  // what the cached layout did
        PruneHealBook(book, {1}, {11, 12, 21, 31, 32});
        assert(book.tanks == std::vector<int>({1}) && book.tankHealers.size() == 2);
        HealPick<int> const pick = PickAndRecordHealer(book, 40);
        assert(!pick.tank);                               // tank 1 is full: the joiner heals the raid
        assert(book.raidHealers.back() == 40);
        // A healer of tank 1 leaves too: the next joiner takes its place on tank 1.
        PruneHealBook(book, {1}, {11, 31, 32, 40});
        HealPick<int> const next = PickAndRecordHealer(book, 41);
        assert(next.tank && next.id == 1);
        // A tank that is no tank any more (role change) is dropped like one that left.
        PruneHealBook(book, {}, {11, 31, 32, 40, 41});
        assert(book.tanks.empty() && book.tankHealers.empty());
        assert(!PickAndRecordHealer(book, 42).tank);
    }

    // Re-review Medium: a healer whose tank leaves must lose its focus list (with "focus heal targets" on, a healer heals only that
    // list). The tank leaving the raid hands back its healers; they heal the raid now and name no tank.
    {
        HealBook<int> book;
        book.tanks = {1, 2};
        book.tankHealers = {{11, 1}, {21, 2}, {22, 2}};
        book.raidHealers = {31};
        std::vector<int> const orphans = ForgetMember(book, 2);
        assert(orphans == std::vector<int>({21, 22}));
        assert(book.tanks == std::vector<int>({1}));
        int tank = 0;
        assert(!FocusTankOf(book, 21, tank) && !FocusTankOf(book, 22, tank));      // their focus is cleared
        assert(FocusTankOf(book, 11, tank) && tank == 1);                          // the other tank's healer keeps its tank
        assert(book.raidHealers == std::vector<int>({31, 21, 22}));
        // A healer leaving only goes from the book.
        assert(ForgetMember(book, 11).empty() && !FocusTankOf(book, 11, tank));
        // The prune before a joiner hands back the healers of a tank that is no tank any more (or gone) the same way.
        HealBook<int> b2;
        b2.tanks = {1, 2};
        b2.tankHealers = {{11, 1}, {21, 2}};
        assert(PruneHealBook(b2, {1}, {11, 21}) == std::vector<int>({21}));
        assert(!FocusTankOf(b2, 21, tank) && b2.raidHealers == std::vector<int>({21}));
    }

    // Re-review Medium: a re-run layout that makes a former tank healer a raid healer leaves it with no focus tank (heals=raid),
    // so the host clears its list; one that keeps it on a tank names that tank (heals=<tank>).
    {
        HealBook<int> before;
        before.tanks = {1};
        before.tankHealers = {{11, 1}};
        HealBook<int> relaid;           // what the new layout recorded: 11 is a raid healer now
        relaid.tanks = {1};
        relaid.raidHealers = {11};
        int tank = 0;
        assert(FocusTankOf(before, 11, tank) && tank == 1);
        assert(!FocusTankOf(relaid, 11, tank));
    }

    // Re-review low: a tank that rejoins is listed once.
    {
        HealBook<int> book;
        AddTankOnce(book, 1);
        AddTankOnce(book, 2);
        AddTankOnce(book, 1);
        assert(book.tanks == std::vector<int>({1, 2}));
    }

    std::cout << "PASS: raid joins (party cap, convert, raid cap), main tank pick, a 20-member layout and its subgroup moves, an offline member holds its seat, the heal book follows the raid, a departed tank frees its healers' focus, a rejoining tank is listed once\n";
    return 0;
}
