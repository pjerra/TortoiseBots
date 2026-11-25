#include "host/TbRaidLayout.h"
#include "host/TbH3.h"
#include "host/TbRaidGroupsPure.h"
#include "host/run/TbRun.h"
#include "TbBotMgr.h"

#include "compat/botpch_forever.h"

#include "Group.h"
#include "GroupMgr.h"
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

// What a layout left on a raid, for the members that join it later (raidgroups::HealBook): its tanks (the main tank first), which healer
// heals which tank, and the raid healers. Pruned to the raid as it is before every joiner is placed; dropped with the group.
using Applied = raidgroups::HealBook<ObjectGuid>;

RL::Role RoleFor(Player* member);

std::map<ObjectGuid, Applied>& Layouts()
{
    static std::map<ObjectGuid, Applied> layouts;
    return layouts;
}

// The book's input from the raid as it is now: the members in the world with the tank role, and those with the healer role.
void RolesNow(Group* group, Player* except, std::vector<ObjectGuid>& tanks, std::vector<ObjectGuid>& healers)
{
    for (Group::MemberSlot const& slot : group->GetMemberSlots())
    {
        Player* other = ObjectAccessor::FindPlayer(slot.guid);
        if (!other || !other->IsInWorld() || other == except)
            continue;
        RL::Role const role = RoleFor(other);
        if (role == RL::Role::Tank)
            tanks.push_back(slot.guid);
        else if (role == RL::Role::Healer)
            healers.push_back(slot.guid);
    }
}

// The member's role (review #187 item 4): PlayerbotAI::IsTank / IsHeal, which need no engine: the role `.tb role` stored first, then a bot's
// engine (its forced role and its tank or heal strategy) when it has one, else the class and talents (AiFactory::GetPlayerRoles, the slice's
// EffectiveRole). So a layout right after `.tb party`, before `.tb master` gave the bots their engines, still finds the tanks and healers of
// a raid whose roles were never set; real players are read the same way. `.tb state` reports `none` for a bot without an engine and no
// stored role: the layout's read is wider on purpose.
RL::Role RoleFor(Player* member)
{
    if (PlayerbotAI::IsTank(member, true))
        return RL::Role::Tank;
    if (PlayerbotAI::IsHeal(member, true))
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

// The other way round (review #187 re-review): a healer that heals the raid gets no focus list. With "focus heal targets" on, a
// healer heals ONLY its list, so a stale list (its tank left, or a new layout made it a raid healer) must go: the list is emptied and
// the strategy dropped. Only a host bot whose engine has the strategy is touched.
void ClearFocusHeal(Player* healer)
{
    PlayerbotAI* ai = (healer && IsHostBot(healer)) ? PlayerbotAIStorage::Instance().GetAI(healer) : nullptr;
    if (!ai || !ai->GetAiObjectContext() || !ai->HasStrategy("focus heal targets", BotState::BOT_STATE_COMBAT))
        return;
    ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid>>("focus heal targets")->Set(std::list<ObjectGuid>());
    ai->ChangeStrategy("-focus heal targets", BotState::BOT_STATE_COMBAT);
    sLog.outString("TortoiseBots raid: %s heals the whole raid again (focus heal targets cleared)", healer->GetName().c_str());
}

void ClearFocusHeal(ObjectGuid healer)
{
    ClearFocusHeal(ObjectAccessor::FindPlayer(healer));
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

    // Every member in the world is planned, real players too. Every member slot holds a seat, the offline ones included (the saved
    // group keeps them): `slots` lists them all, so the moves below count each seat and never fill a subgroup past five around them.
    std::vector<Player*> players;
    std::vector<raidgroups::MemberFacts> facts;
    std::vector<ObjectGuid> slots;
    std::vector<int> playerOfSlot;   // index into players, -1 for a member that is not in the world
    for (Group::MemberSlot const& slot : group->GetMemberSlots())
    {
        slots.push_back(slot.guid);
        playerOfSlot.push_back(-1);
        Player* member = ObjectAccessor::FindPlayer(slot.guid);
        if (!member || !member->IsInWorld())
            continue;
        playerOfSlot.back() = int(players.size());
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

    // The policy names members by a 64-bit id; a Forever guid is 128 bits, so the id is the slot's index plus one. An offline member is
    // planned where it sits (raidgroups::LayoutMoves). Group::ChangeMembersGroup does not check the target's room (only the client's
    // handler does), so every plain move is guarded with HasFreeSlotSubGroup as well; a swap keeps both counts.
    std::vector<raidgroups::SeatFacts> seats;
    for (size_t s = 0; s < slots.size(); ++s)
    {
        raidgroups::SeatFacts seat;
        seat.current = group->GetMemberGroup(slots[s]);
        if (playerOfSlot[s] >= 0)
        {
            seat.planned = true;
            seat.plannedSub = layout.placements[size_t(playerOfSlot[s])].subgroup;
        }
        seats.push_back(seat);
    }
    for (RL::SubgroupOp const& op : RL::PlanSubgroupMoves(raidgroups::LayoutMoves(seats)))
    {
        if (op.member == 0 || op.member > slots.size())
            continue;
        ObjectGuid const member = slots[size_t(op.member - 1)];
        if (op.swap)
        {
            if (op.other != 0 && op.other <= slots.size())
                group->SwapMembersGroups(member, slots[size_t(op.other - 1)]);
        }
        else if (op.sub < RL::kSubgroups && group->HasFreeSlotSubGroup(op.sub))
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
            applied.tankHealers.emplace_back(players[i]->GetGUID(), tank);
            bool const focus = FocusHeal(players[i], tank);
            focused += focus ? 1 : 0;
            Player* tankPlayer = ObjectAccessor::FindPlayer(tank);
            rec.S("heals", tankPlayer ? std::string(tankPlayer->GetName()) : std::string()).B("focus", focus);
        }
        else
        {
            // Every layout rebuilds the lists: a raid healer (or a member that is no healer any more) keeps no old focus.
            ClearFocusHeal(players[i]);
            if (p.role == RL::Role::Healer)
            {
                applied.raidHealers.push_back(players[i]->GetGUID());
                rec.S("heals", "raid");
            }
        }
        if (sub != p.subgroup)
        {
            ++misplaced;
            rec.N("planned", p.subgroup + 1);
        }
        reply.Line(rec);
    }
    // Layouts of groups that are gone (a missed disband) go too, so the table never grows past the raids that exist.
    for (auto itr = Layouts().begin(); itr != Layouts().end();)
        itr = sGroupMgr->GetGroupByGUID(itr->first) ? std::next(itr) : Layouts().erase(itr);
    Layouts()[group->GetGUID()] = applied;
    Rec end("layout");
    end.S("leader", leader->GetName()).N("members", players.size()).N("tanks", tankByIndex.size()).N("focus", focused).N("misplaced", misplaced)
        .S("groups", raidgroups::SubgroupLine(names, real));
    reply.End(end);
    return true;
}

bool RaidConvertBlocked(Group* group, std::string& why)
{
    if (!group)
        return false;
    std::string const leader = group->GetLeaderName();
    if (run::RunRec* rec = run::Find(group->GetLeaderGUID()); rec && !run::Ended(*rec))
    {
        why = leader + "'s party has a run (.tb run status " + leader + "): a sixth member would make it a raid and end the run, so it was not added";
        return true;
    }
    for (Group::MemberSlot const& slot : group->GetMemberSlots())
        if (!CommanderGuid(slot.guid).IsEmpty())
        {
            why = leader + "'s party has a commander binding (" + slot.name + "): a sixth member would make it a raid and end the binding, so it was "
                "not added; .tb raid " + leader + " converts the party on purpose";
            return true;
        }
    return false;
}

void NoteLayoutMemberLeft(Group* group, ObjectGuid member)
{
    if (!group)
        return;
    auto const found = Layouts().find(group->GetGUID());
    if (found == Layouts().end())
        return;
    // The tank's healers heal the raid from now on (their focus list would keep them on a tank who is gone); the leaver's own
    // list goes too (a bot healer that leaves the raid heals its next party normally).
    for (ObjectGuid const& orphan : raidgroups::ForgetMember(found->second, member))
        ClearFocusHeal(orphan);
    ClearFocusHeal(member);
}

void ForgetLayout(Group* group)
{
    if (group)
        Layouts().erase(group->GetGUID());
}

int PlaceJoiner(Group* group, Player* member, std::string* heals)
{
    if (!group || !member)
        return -1;
    auto const found = Layouts().find(group->GetGUID());
    if (found == Layouts().end())
        return -1;
    if (!group->isRaidGroup())
    {
        Layouts().erase(found);
        return -1;
    }
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
    // The core's ChangeMembersGroup does not check the room: guard it, as the client's handler does (the joiner keeps the seat it got).
    if (seat.subgroup != group->GetMemberGroup(member->GetGUID()))
    {
        if (!group->HasFreeSlotSubGroup(seat.subgroup))
            return -1;
        group->ChangeMembersGroup(member->GetGUID(), seat.subgroup);
    }
    if (facts.role == RL::Role::Tank)
    {
        if (!group->IsAssistant(member->GetGUID()))
            group->SetGroupMemberFlag(member->GetGUID(), true, MEMBER_FLAG_ASSISTANT);
        raidgroups::AddTankOnce(applied, member->GetGUID());
    }
    else if (facts.role == RL::Role::Healer)
    {
        // The book as the raid is now (the legacy code counted present members only): a tank that left or is no tank any more is
        // dropped with its healers, a healer that left is not counted, so the joiner is never pointed at a tank who is not there.
        std::vector<ObjectGuid> tanksNow, healersNow;
        RolesNow(group, member, tanksNow, healersNow);
        for (ObjectGuid const& orphan : raidgroups::PruneHealBook(applied, tanksNow, healersNow))
            ClearFocusHeal(orphan);
        raidgroups::HealPick<ObjectGuid> const pick = raidgroups::PickAndRecordHealer(applied, member->GetGUID());
        if (pick.tank)
            FocusHeal(member, pick.id);
        else
            ClearFocusHeal(member);
        if (heals)
        {
            Player* tank = pick.tank ? ObjectAccessor::FindPlayer(pick.id) : nullptr;
            *heals = pick.tank ? (tank ? std::string(tank->GetName()) : pick.id.ToString()) : std::string("raid");
        }
    }
    return seat.subgroup;
}
}
}
