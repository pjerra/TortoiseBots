#include "host/TbRaidLayout.h"
#include "host/TbH3.h"
#include "host/TbRaidGroupsPure.h"
#include "TbBotMgr.h"

#include "compat/botpch_forever.h"

#include "Group.h"
#include "ObjectAccessor.h"
#include "Player.h"

#include "playerbot/PlayerbotAI.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/strategy/Value.h"
#include "PlayerbotAIStorage.h"

#include <list>
#include <map>
#include <string>
#include <vector>

namespace TortoiseBots
{
namespace h3
{
namespace
{
namespace RL = RaidLayout;

// What a layout left on a group, for the members that join it later: its tanks (the main tank first) and how many
// healers each tank and the raid have.
struct Applied
{
    std::vector<ObjectGuid> tanks;
    std::vector<uint32> healersPerTank;
    uint32 raidHealers = 0;
};

std::map<ObjectGuid, Applied>& Layouts()
{
    static std::map<ObjectGuid, Applied> layouts;
    return layouts;
}

// The role `.tb state` reports (`role=`): the engine's answer for a bot with one, else the role `.tb role` stored (1 tank, 2 healer).
RL::Role RoleFor(Player* member)
{
    bool const engine = PlayerbotAIStorage::Instance().GetAI(member) != nullptr;
    uint8 const forced = PlayerbotAIStorage::Instance().GetPlayerForcedRole(member->GetGUID());
    if (engine ? PlayerbotAI::IsTank(member, true) : forced == 1)
        return RL::Role::Tank;
    if (engine ? PlayerbotAI::IsHeal(member, true) : forced == 2)
        return RL::Role::Healer;
    return RL::Role::Damage;
}

// A tank healer heals its tank first: the engine's "focus heal targets" (the tank, then itself), as the legacy layout set it.
// A real player or a bot without an engine is only told its seat.
bool FocusHeal(Player* healer, ObjectGuid tank)
{
    PlayerbotAI* ai = IsHostBot(healer) ? PlayerbotAIStorage::Instance().GetAI(healer) : nullptr;
    if (!ai || !ai->GetAiObjectContext())
        return false;
    std::list<ObjectGuid> targets = {tank, healer->GetGUID()};
    ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid>>("focus heal targets")->Set(targets);
    ai->ChangeStrategy("+focus heal targets", BotState::BOT_STATE_COMBAT);
    return true;
}

char const* RoleWord(RL::Role role)
{
    return role == RL::Role::Tank ? "tank" : role == RL::Role::Healer ? "healer" : "dps";
}
}

bool HandleLayout(ChatHandler* handler, Trinity::ChatCommands::Tail tail)
{
    Words const args = Split(tail);
    if (!RequireBuilt(handler, args, "layout"))
        return false;
    if (args.w.empty())
        return Fail(handler, args, "layout", "missing-leader", ".tb layout <leader>");
    if (args.w.size() > 1)
        return Fail(handler, args, "layout", "unexpected-argument", args.w[1]);
    Player* leader = FindOnline(args.w[0]);
    if (!leader)
        return Fail(handler, args, "layout", "no-such-player", args.w[0] + " is not in the world");
    std::string code, detail;
    if (!CheckPartyLeadAccess(handler, leader, code, detail))
        return Fail(handler, args, "layout", code, detail);
    if (!MayLeadBots(handler, leader->GetGUID()))
        return Fail(handler, args, "layout", "not-allowed", args.w[0] + " is neither a host bot nor your own character");
    Group* group = leader->GetGroup();
    if (!group)
        return Fail(handler, args, "layout", "no-party", leader->GetName() + " is not in a group");
    if (!group->isRaidGroup())
        return Fail(handler, args, "layout", "not-raid", leader->GetName() + "'s group is a party: a layout is for a raid");

    // Every member in the world, real players too: their seats count, so the plan never overfills a subgroup around them.
    std::vector<Player*> players;
    std::vector<raidgroups::MemberFacts> facts;
    for (Group::MemberSlot const& slot : group->GetMemberSlots())
    {
        Player* member = ObjectAccessor::FindPlayer(slot.guid);
        if (!member || !member->IsInWorld())
            continue;
        raidgroups::MemberFacts f;
        f.name = member->GetName();
        f.role = RoleFor(member);
        f.cls = member->GetClass();
        f.groupLeader = group->IsLeader(slot.guid);
        f.mainTank = (slot.flags & MEMBER_FLAG_MAINTANK) != 0;
        players.push_back(member);
        facts.push_back(f);
    }
    int const mainTank = raidgroups::PickMainTank(facts);
    if (mainTank < 0)
        return Fail(handler, args, "layout", "no-tank", "no member in the world has the tank role (.tb role <bot> tank)");
    RL::Layout const layout = RL::Plan(raidgroups::LayoutMembers(facts, mainTank), sPlayerbotAIConfig.raidHealerSeatsByClass);
    if (!layout.ok)
        return Fail(handler, args, "layout", "layout-refused", layout.error);

    // The policy names members by a 64-bit id; a Forever guid is 128 bits, so the id is the member's index plus one.
    std::vector<RL::SubgroupMember> moves;
    for (size_t i = 0; i < players.size(); ++i)
        moves.push_back({uint64_t(i + 1), group->GetMemberGroup(players[i]->GetGUID()), layout.placements[i].subgroup});
    for (RL::SubgroupOp const& op : RL::PlanSubgroupMoves(moves))
    {
        ObjectGuid const member = players[size_t(op.member - 1)]->GetGUID();
        if (op.swap)
            group->SwapMembersGroups(member, players[size_t(op.other - 1)]->GetGUID());
        else
            group->ChangeMembersGroup(member, op.sub);
    }

    Applied applied;
    std::vector<ObjectGuid> tankByIndex;
    for (size_t i = 0; i < players.size(); ++i)
        if (layout.placements[i].tankIndex >= 0)
        {
            size_t const t = size_t(layout.placements[i].tankIndex);
            if (tankByIndex.size() <= t)
                tankByIndex.resize(t + 1);
            tankByIndex[t] = players[i]->GetGUID();
        }
    group->SetGroupMemberFlag(players[size_t(mainTank)]->GetGUID(), true, MEMBER_FLAG_MAINTANK);
    for (size_t t = 1; t < tankByIndex.size(); ++t)
        if (!group->IsAssistant(tankByIndex[t]))
            group->SetGroupMemberFlag(tankByIndex[t], true, MEMBER_FLAG_ASSISTANT);
    applied.tanks = tankByIndex;
    applied.healersPerTank.assign(tankByIndex.size(), 0);

    Reply reply(handler, args, "layout");
    reply.Begin();
    std::vector<std::string> names;
    std::vector<uint8> real;
    uint32 misplaced = 0, focused = 0;
    for (size_t i = 0; i < players.size(); ++i)
    {
        RL::Placement const& p = layout.placements[i];
        uint8 const sub = group->GetMemberGroup(players[i]->GetGUID());
        names.push_back(p.name);
        real.push_back(sub);
        Rec rec("layout");
        rec.S("member", p.name).S("role", RoleWord(p.role)).N("sub", sub + 1);
        if (p.tankIndex == 0)
            rec.S("tank", "main");
        else if (p.tankIndex > 0)
            rec.S("tank", "off");
        if (p.healsTank >= 0 && size_t(p.healsTank) < tankByIndex.size())
        {
            ObjectGuid const tank = tankByIndex[size_t(p.healsTank)];
            ++applied.healersPerTank[size_t(p.healsTank)];
            bool const focus = FocusHeal(players[i], tank);
            focused += focus ? 1 : 0;
            Player* tankPlayer = ObjectAccessor::FindPlayer(tank);
            rec.S("heals", tankPlayer ? std::string(tankPlayer->GetName()) : std::string()).B("focus", focus);
        }
        else if (p.role == RL::Role::Healer)
        {
            ++applied.raidHealers;
            rec.S("heals", "raid");
        }
        if (sub != p.subgroup)
        {
            ++misplaced;
            rec.N("planned", p.subgroup + 1);
        }
        reply.Line(rec);
    }
    Layouts()[group->GetGUID()] = applied;
    Rec end("layout");
    end.S("leader", leader->GetName()).N("members", players.size()).N("tanks", tankByIndex.size()).N("focus", focused).N("misplaced", misplaced)
        .S("groups", raidgroups::SubgroupLine(names, real));
    reply.End(end);
    return true;
}

int PlaceJoiner(Group* group, Player* member)
{
    if (!group || !member || !group->isRaidGroup())
        return -1;
    auto const found = Layouts().find(group->GetGUID());
    if (found == Layouts().end())
        return -1;
    Applied& applied = found->second;
    RL::JoinFacts facts;
    facts.role = RoleFor(member);
    for (Group::MemberSlot const& slot : group->GetMemberSlots())
    {
        if (slot.guid == member->GetGUID() || slot.group >= RL::kSubgroups)
            continue;
        ++facts.seated[slot.group];
        if (Player* other = ObjectAccessor::FindPlayer(slot.guid))
        {
            RL::Role const role = RoleFor(other);
            facts.tankIn[slot.group] |= role == RL::Role::Tank;
            facts.healerIn[slot.group] |= role == RL::Role::Healer;
        }
    }
    RL::JoinSeat const seat = RL::PlanJoinerSubgroup(facts);
    if (!seat.ok)
        return -1;
    group->ChangeMembersGroup(member->GetGUID(), seat.subgroup);
    if (facts.role == RL::Role::Tank)
    {
        if (!group->IsAssistant(member->GetGUID()))
            group->SetGroupMemberFlag(member->GetGUID(), true, MEMBER_FLAG_ASSISTANT);
        applied.tanks.push_back(member->GetGUID());
        applied.healersPerTank.push_back(0);
    }
    else if (facts.role == RL::Role::Healer)
    {
        int const tank = RL::PickJoinerHealTank(applied.healersPerTank, applied.raidHealers);
        if (tank >= 0 && size_t(tank) < applied.tanks.size())
        {
            ++applied.healersPerTank[size_t(tank)];
            FocusHeal(member, applied.tanks[size_t(tank)]);
        }
        else
            ++applied.raidHealers;
    }
    return seat.subgroup;
}
}
}
