// Slice 6, package O6 (orders): see TbRunOrders.h and forever/docs/RUN-S6-O-NOTES.md. Built with TORTOISEBOTS_SLICE1 only.
//
// ONE path for every order the run leader emits: parse (TbRunOrdersPure.h) -> read the party -> judge with the facts of the core -> pre-flight every bot's
// queue (h3::PreflightOrder, TbOrders.cpp) -> queue (h3::QueueOrder) -> answer. Nothing is queued unless the whole order passed, so a refusal changes no bot.
#include "host/run/TbRunOrders.h"
#include "host/run/TbRunLiquid.h"

#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1

#include "compat/botpch_forever.h"

#include "host/TbH3.h"
#include "host/TbCc.h"
#include "host/TbCcPure.h"
#include "host/TbOrdersApi.h"
#include "host/cmd/TbCmdBinding.h"
#include "host/run/TbRun.h"
#include "host/run/TbRunFacts.h"
#include "host/run/TbRunOrdersPure.h"
#include "slice/SliceHybridPolicy.h"
#include "host/run/TbRunTasks.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <strings.h>

#include "GridDefines.h"
#include "Group.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "PathGenerator.h"
#include "Unit.h"

#include "TbBotMgr.h"
#include "compat/TbCompat_Unit.h"
#include "playerbot/PlayerbotAI.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/ServerFacade.h"
#include "PlayerbotAIStorage.h"

namespace TortoiseBots
{
namespace run
{
namespace
{
namespace od = orders;
using RemoteCommander::MoveFacts;
using RemoteCommander::MovePoint;

using Pending = od::PendingOrder<ObjectGuid>;

std::map<ObjectGuid, std::vector<Pending>>& PendingByRun()
{
    static std::map<ObjectGuid, std::vector<Pending>> pending;
    return pending;
}

// ---- the party ----

struct ResolvedParty
{
    Player* lead = nullptr;
    std::vector<Player*> bots;      // host bots in the world on the lead's map and instance, the lead first (alive or dead)
    ObjectGuid expectedCommander;   // who followers must be bound to in h3's table
};

bool Usable(Player* player)
{
    return player && player->IsInWorld() && !player->IsBeingTeleported() && player->FindMap();   // GetMap() ASSERTs on a player with no map
}

PlayerbotAI* AiOf(Player* bot)
{
    PlayerbotAI* ai = bot ? PlayerbotAIStorage::Instance().GetAI(bot) : nullptr;
    return ai && ai->GetAiObjectContext() ? ai : nullptr;
}

bool IsHostBot(Player* player)
{
    return player && BotMgr::Instance().IsBot(player->GetGUID());
}

std::string NameOf(Player* player)
{
    return player ? std::string(player->GetName()) : std::string();
}

bool LeadAvailable(Player* lead)
{
    return lead && lead->IsAlive() && !lead->IsInFlight() && !lead->IsBeingTeleported();
}

OrderAnswer Refuse(std::string const& code, std::string const& message)
{
    OrderAnswer a;
    a.code = code;
    a.text = od::RefusalText(code, message);
    return a;
}

OrderAnswer Refuse(od::Judged const& judged)
{
    OrderAnswer a;
    a.code = judged.code;
    a.text = judged.text;
    return a;
}

// The leader and its party. A refusal (empty `party.lead`) carries the answer.
bool ReadParty(Party const& ctx, ResolvedParty& party, OrderAnswer& refusal)
{
    Player* lead = ObjectAccessor::FindPlayer(ctx.leader);
    if (!Usable(lead) || (ctx.fromRun && !lead->GetGroup()))
    {
        refusal = Refuse("lead-unavailable", "The lead bot is between maps. Try again in a moment.");
        return false;
    }
    party.lead = lead;
    ObjectGuid const leadCmd = h3::CommanderGuid(lead->GetGUID());
    party.expectedCommander = ctx.fromRun ? (leadCmd.IsEmpty() ? lead->GetGUID() : leadCmd) : (ctx.owner.IsEmpty() ? lead->GetGUID() : ctx.owner);
    party.bots.push_back(lead);
    if (ctx.members.empty())
    {
        if (Group* group = lead->GetGroup())
        {
            for (Group::MemberSlot const& slot : group->GetMemberSlots())
            {
                if (slot.guid == lead->GetGUID())
                    continue;
                Player* member = ObjectAccessor::FindPlayer(slot.guid);
                if (Usable(member) && IsHostBot(member) && member->GetMapId() == lead->GetMapId() && member->GetInstanceId() == lead->GetInstanceId())
                    party.bots.push_back(member);
            }
        }
    }
    else
    {
        for (ObjectGuid guid : ctx.members)
        {
            if (guid == lead->GetGUID())
                continue;
            Player* member = ObjectAccessor::FindPlayer(guid);
            if (Usable(member) && IsHostBot(member) && member->GetMapId() == lead->GetMapId() && member->GetInstanceId() == lead->GetInstanceId())
                party.bots.push_back(member);
        }
    }
    return true;
}

Player* FindBotNamed(ResolvedParty const& party, std::string const& name)
{
    for (Player* bot : party.bots)
        if (strcasecmp(bot->GetName().c_str(), name.c_str()) == 0)
            return bot;
    return nullptr;
}

// ---- the move judge (CheckLeadMove, Commander.cpp:3431), on the core's public API ----

struct MoveCheck
{
    MovePoint point;                // after the floor snap
    MoveFacts facts;
    bool pathRead = false;
    bool liquidOnWay = false;       // R6: the point, or the walked route to it, is in or on liquid (TbRunLiquid.h rule)
    std::string liquidAt;           // where the route was refused ("x y z"), for the answer's text
};

// The floor snap and the path of one mover. Differences from Turtle (said in the notes): the snap reads no liquid (a water point is not kept in the
// water, it is lifted to the surface like any point with no floor under it, and then judged by its path), and the path is one query with no steep-slope
// exclusion and no permissive retry (ExcludeSteepSlopes is not ported: the path may use a slope Turtle excludes, the verdict is never stricter than
// the mesh).
MoveCheck CheckLeadMove(Player* mover, MovePoint point, bool fromRun, bool gather)
{
    MoveCheck check;
    Map* map = mover->GetMap();
    float const ox = mover->GetPositionX(), oy = mover->GetPositionY(), oz = mover->GetPositionZ();
    if (Trinity::IsValidMapCoord(point.x, point.y, point.z + RemoteCommander::kFloorSnapRange))
    {
        float const belowZ = GetHeightCompat(map, point.x, point.y, point.z, true, RemoteCommander::kFloorBelowBand + RemoteCommander::kFloorBelowHeadroom);
        float aboveZ = INVALID_HEIGHT;
        if (!RemoteCommander::FloorBelowUsable(point.z, belowZ, belowZ > INVALID_HEIGHT))
            aboveZ = GetHeightCompat(map, point.x, point.y, point.z + RemoteCommander::kFloorSnapRange, true, 2.0f * RemoteCommander::kFloorSnapRange);
        point.z = RemoteCommander::ChooseFloorZ(point.z, belowZ, belowZ > INVALID_HEIGHT, aboveZ, aboveZ > INVALID_HEIGHT, false, 0.0f, false, true);
    }
    check.point = point;
    MovePoint origin;
    origin.x = ox;
    origin.y = oy;
    origin.z = oz;
    check.facts = RemoteCommander::LeadMoveFacts(origin, point, 0, 0.0f, -1.0f, sPlayerbotAIConfig.reactDistance, fromRun && gather, fromRun);
    // Review fix 6: a point beyond ReactDistance is refused TooFar by the judge before any path fact is read (RemoteCommander::CheckMove tests it first), so
    // neither the PathFinder nor the liquid samples of a far point (up to the map's edge: one terrain read per 1.5 yd) are worth a single query in this tick.
    // The caller's refusal comes from the facts alone; `pathRead` stays false (only an ACCEPTED order's path type is noted).
    if (RemoteCommander::CheckMove(check.facts) == RemoteCommander::MoveVerdict::TooFar)
        return check;
    if (!RemoteCommander::IsAtLead(check.facts.distanceFromLead))
    {
        PathFinder path(mover);
        PathCalculateCompat(path, G3D::Vector3(ox, oy, oz), G3D::Vector3(point.x, point.y, point.z), false);
        check.facts.pathType = PathTypeCompat(path);
        G3D::Vector3 const end = PathActualEndCompat(path);
        check.facts.endOffset = std::sqrt((end.x - point.x) * (end.x - point.x) + (end.y - point.y) * (end.y - point.y) + (end.z - point.z) * (end.z - point.z));
        check.facts.endFromLead = std::sqrt((end.x - ox) * (end.x - ox) + (end.y - oy) * (end.y - oy) + (end.z - oz) * (end.z - oz));
        check.pathRead = true;
        // R6 (review item 7, round 2): the route the mover will walk is the navmesh path: its vertices and legs are read in order (every <= 1.5 yd) with the move rule
        // of TbRunLiquid.h: wet samples only as the contiguous prefix that holds the origin, the destination dry. A path that is not a real path (fewer than two
        // points: no route at all) falls back to the straight line, which is what a shortcut walks.
        Movement::PointsArray const& way = PathPointsCompat(path);
        auto const wet = [map](float x, float y, float z) { return liquid::PointRefusedOnMap(map, x, y, z); };
        liquid::PrefixState where;
        check.liquidOnWay = way.size() >= 2 ? liquid::PathPrefixRefused(way, way.size(), wet, &where) : liquid::SegmentPrefixRefused(ox, oy, oz, point.x, point.y, point.z, wet, &where);
        if (check.liquidOnWay)
        {
            float wx, wy, wz;
            where.Where(wx, wy, wz);
            char buffer[80];
            std::snprintf(buffer, sizeof buffer, "%.1f %.1f %.1f", wx, wy, wz);
            check.liquidAt = buffer;
        }
    }
    else
        check.liquidOnWay = liquid::PointRefusedOnMap(map, point.x, point.y, point.z);      // already at the point: it must not be liquid
    // A wet destination always refuses (also when no path was read).
    if (!check.liquidOnWay)
        check.liquidOnWay = liquid::PointRefusedOnMap(map, point.x, point.y, point.z);
    return check;
}

// ---- queueing ----

// The slots and the answer of an order being assembled. Nothing is queued before `Commit`.
struct Batch
{
    struct Item
    {
        Player* bot;
        h3::OrderSpec spec;
        bool decisive;      // its result decides whether the order failed (any decisive success is enough)
        bool required;      // a refusal of its pre-flight refuses the whole order (the lead of a move, the tank of a pull, a named bot)
    };
    bool fromRun = false;
    std::vector<Item> items;

    explicit Batch(bool run) : fromRun(run) {}

    void Add(Player* bot, std::string const& verb, bool decisive, bool required)
    {
        Item item{ bot, h3::OrderSpec(), decisive, required };
        item.spec.verb = verb;
        item.spec.fromRun = fromRun;
        items.push_back(std::move(item));
    }
    h3::OrderSpec& Back() { return items.back().spec; }
};

// Pre-flights every item (h3::PreflightOrder: engine, registered actions, queue room, the unit still valid). A REQUIRED item that is refused refuses the order;
// any other refused item is dropped (the count in the answer says so), and the order stands when at least one DECISIVE item remains (when it had any).
bool PreflightBatch(Batch& batch, OrderAnswer& refusal)
{
    std::vector<Batch::Item> kept;
    std::string firstCode, firstDetail;
    bool anyDecisive = false, keptDecisive = false;
    for (Batch::Item& item : batch.items)
    {
        anyDecisive = anyDecisive || item.decisive;
        std::string code, detail;
        if (h3::PreflightOrder(item.bot, item.spec, code, detail))
        {
            keptDecisive = keptDecisive || item.decisive;
            kept.push_back(std::move(item));
            continue;
        }
        // A pull with no tank is the legacy "no-tank" refusal; every other code is the host's own.
        if (code == "no-pull-strategy")
            code = "no-tank";
        if (item.required)
        {
            refusal = Refuse(code, NameOf(item.bot) + ": " + detail);
            return false;
        }
        if (item.decisive && firstCode.empty())
        {
            firstCode = code;
            firstDetail = NameOf(item.bot) + ": " + detail;
        }
    }
    batch.items = std::move(kept);
    if (batch.items.empty() || (anyDecisive && !keptDecisive))
    {
        refusal = Refuse(firstCode.empty() ? "failed" : firstCode, firstCode.empty() ? "No bot of the group accepted the order." : firstDetail);
        return false;
    }
    return true;
}

void Track(ObjectGuid leader, std::string const& order, uint32 nowMs, std::vector<std::pair<Player*, uint32>> const& queued, std::vector<bool> const& decisive)
{
    Pending pending;
    pending.order = order;
    pending.queuedMs = nowMs;
    for (size_t i = 0; i < queued.size(); ++i)
    {
        od::Slot<ObjectGuid> slot;
        slot.bot = queued[i].first->GetGUID();
        slot.seq = queued[i].second;
        slot.decisive = decisive[i];
        pending.slots.push_back(slot);
    }
    std::vector<Pending>& list = PendingByRun()[leader];
    list.push_back(std::move(pending));
    if (list.size() > od::kMaxPending)
        list.erase(list.begin());
}

// Queues every kept item (each already pre-flighted, so a failure here is a race inside this call stack: it cannot happen on the world thread) and
// records the pending order when `ctx.fromRun`. Returns the number queued.
uint32 Commit(Party const& ctx, std::string const& order, uint32 nowMs, Batch& batch, OrderAnswer& answer)
{
    std::vector<std::pair<Player*, uint32>> queued;
    std::vector<bool> decisive;
    for (Batch::Item& item : batch.items)
    {
        std::string code, detail;
        uint32 seq = 0;
        if (!h3::QueueOrder(item.bot, item.spec, code, detail, seq))
            continue;
        queued.emplace_back(item.bot, seq);
        decisive.push_back(item.decisive);
    }
    if (ctx.fromRun && !queued.empty())
        Track(ctx.leader, order, nowMs, queued, decisive);
    answer.pending = ctx.fromRun && !queued.empty();
    answer.bots = uint32(queued.size());
    return answer.bots;
}

OrderAnswer Accept(OrderAnswer answer, std::string const& scope, uint32 count)
{
    answer.accepted = true;
    answer.code.clear();
    answer.text = od::AckText(scope, count);
    return answer;
}

// ---- the target of a unit order ----

// A handle of ReadFacts' unit list back to a unit of the lead's map, judged as the Turtle `ResolveOrderTarget` does (RemoteCommander::CheckOrderTarget):
// a handle that no longer resolves is `invalid-target`, never a guess.
Unit* ResolveUnit(Party const& ctx, ResolvedParty const& party, uint64 handle, std::string const& token, ObjectGuid& guid, od::Judged& judged)
{
    Unit* unit = nullptr;
    if (ctx.resolveTarget && ctx.resolveTarget(handle, token, guid))
        unit = ObjectAccessor::GetUnit(*party.lead, guid);
    Player* judge = party.lead->IsAlive() ? party.lead : nullptr;
    for (Player* bot : party.bots)
        if (!judge && bot->IsAlive() && !bot->IsCharmed())
            judge = bot;
    if (!judge)
        judge = party.lead;
    RemoteCommander::TargetFacts facts;
    facts.found = unit != nullptr;
    if (unit)
    {
        Group* group = party.lead->GetGroup();
        facts.inWorld = unit->IsInWorld();
        facts.groupMemberOrBot = BotMgr::Instance().IsBot(unit->GetGUID()) || (group && group->IsMember(unit->GetGUID()));
        facts.alive = unit->IsAlive();
        facts.friendly = sServerFacade.IsFriendlyTo(unit, judge);
        facts.attackable = judge->IsValidAttackTarget(unit);
        if (unit->isTargetableForAttack(false))
            for (Player* bot : party.bots)
                if (bot->IsAlive() && bot->CanSeeOrDetect(unit))
                {
                    facts.visible = true;
                    break;
                }
    }
    judged = od::JudgeTarget(facts);
    return judged.ok ? unit : nullptr;
}

void SetPersistentSkull(Party const& ctx, ResolvedParty const& party, Unit const* target)
{
    if (!ctx.persistentIcons || !target)
        return;
    Group* group = party.lead->GetGroup();
    if (!group)
        return;
    ObjectGuid const changedBy = ctx.owner.IsEmpty() ? party.lead->GetGUID() : ctx.owner;
    group->SetTargetIcon(7, target->GetGUID(), changedBy);
}

// ---- the verbs ----

OrderAnswer DoMove(Party const& ctx, ResolvedParty const& party, od::Parsed const& p, std::string const& order, uint32 nowMs, bool gather)
{
    Player* lead = party.lead;
    if (!LeadAvailable(lead))
        return Refuse("lead-unavailable", "The lead bot is dead, on a taxi or teleporting.");
    if (!AiOf(lead))
        return Refuse("no-ai", "The lead bot has no AI.");
    MovePoint wanted;
    wanted.x = p.x;
    wanted.y = p.y;
    wanted.z = p.z;
    OrderAnswer answer;

    if (p.direct)
    {
        // CommanderMoveDirect's checks for the lead, then the lead walks straight (move-direct), the followers `follow`.
        float const dx = p.x - lead->GetPositionX(), dy = p.y - lead->GetPositionY();
        float ground = INVALID_HEIGHT;
        if (Trinity::IsValidMapCoord(p.x, p.y, p.z))
            ground = GetHeightCompat(lead->GetMap(), p.x, p.y, p.z, true, RemoteCommander::kDirectGroundTolerance + 2.0f);
        float const h = RemoteCommander::kDirectLosHeight;
        bool const leadSees = Trinity::IsValidMapCoord(lead->GetPositionX(), lead->GetPositionY(), lead->GetPositionZ()) && ground > INVALID_HEIGHT &&
            IsInLineOfSightCompat(lead->GetMap(), lead->GetPositionX(), lead->GetPositionY(), lead->GetPositionZ() + h, p.x, p.y, ground + h);
        std::vector<od::DirectFollower> followers;
        for (Player* bot : party.bots)
        {
            if (bot == lead)
                continue;
            od::DirectFollower f;
            f.name = NameOf(bot);
            f.alive = bot->IsAlive();
            f.horizontalToLead = std::sqrt((bot->GetPositionX() - lead->GetPositionX()) * (bot->GetPositionX() - lead->GetPositionX()) +
                (bot->GetPositionY() - lead->GetPositionY()) * (bot->GetPositionY() - lead->GetPositionY()));
            f.verticalToLead = bot->GetPositionZ() - lead->GetPositionZ();
            if (f.alive && ground > INVALID_HEIGHT)
            {
                PathFinder path(bot);
                PathCalculateCompat(path, G3D::Vector3(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ()), G3D::Vector3(p.x, p.y, ground), false);
                G3D::Vector3 const end = PathActualEndCompat(path);
                uint32 const type = PathTypeCompat(path);
                f.pathComplete = (type & PATHFIND_NORMAL) && !(type & (PATHFIND_INCOMPLETE | PATHFIND_NOPATH | PATHFIND_SHORTCUT)) &&
                    std::sqrt((end.x - p.x) * (end.x - p.x) + (end.y - p.y) * (end.y - p.y) + (end.z - ground) * (end.z - ground)) <= 2.0f &&
                    PathPointsCompat(path).size() >= 2;
            }
            followers.push_back(f);
        }
        od::Judged const judged = od::JudgeDirect(NameOf(lead), std::sqrt(dx * dx + dy * dy), p.z - lead->GetPositionZ(), ground > INVALID_HEIGHT, ground, p.z, leadSees, followers);
        if (!judged.ok)
            return Refuse(judged);
        // R6 (review item 7): a direct step walks the straight line, so the whole line is read, not its end: any liquid on it (lava, water: TbRunLiquid.h rule,
        // sampled every <= 1.5 yd, both ends; wet only as the prefix that holds the lead's own spot, the end dry) refuses the step with the code a point with no safe ground gets.
        if (liquid::SegmentPrefixRefusedOnMap(lead->GetMap(), lead->GetPositionX(), lead->GetPositionY(), lead->GetPositionZ(), p.x, p.y, ground))
            return Refuse("no-ground", "The straight line to the point crosses or ends in liquid (lava or water): a direct step does not walk through it.");
        Batch batch(ctx.fromRun);
        batch.Add(lead, "move-direct", true, true);
        batch.Back().x = p.x;
        batch.Back().y = p.y;
        batch.Back().z = ground;
        batch.Back().map = lead->GetMapId();
        batch.Back().instance = lead->GetInstanceId();
        for (Player* bot : party.bots)
            if (bot != lead && bot->IsAlive())
            {
                if (h3::CommanderGuid(bot->GetGUID()) != party.expectedCommander)
                    return Refuse("not-bound", NameOf(bot) + " is not bound to the lead bot as its commander (`.tb master`): it would not follow.");
                batch.Add(bot, "follow", false, false);
            }
        OrderAnswer refusal;
        if (!PreflightBatch(batch, refusal))
            return refusal;
        Commit(ctx, order, nowMs, batch, answer);
        return Accept(answer, "party", answer.bots);
    }

    MoveCheck const check = CheckLeadMove(lead, wanted, ctx.fromRun, gather);
    answer.movePathRead = check.pathRead;
    answer.movePathType = check.facts.pathType;
    od::Judged const judged = od::JudgeMove(check.facts);
    if (!judged.ok)
    {
        OrderAnswer refusal = Refuse(judged);
        refusal.movePathRead = answer.movePathRead;
        refusal.movePathType = answer.movePathType;
        return refusal;
    }
    if (check.liquidOnWay)
    {
        OrderAnswer refusal = Refuse("unreachable", "The lead bot's way to that point crosses or ends in liquid (lava or water)" +
            (check.liquidAt.empty() ? std::string() : " at " + check.liquidAt) + ": no walkable path avoids it.");
        refusal.movePathRead = answer.movePathRead;
        refusal.movePathType = answer.movePathType;
        return refusal;
    }
    Batch batch(ctx.fromRun);
    batch.Add(lead, "come", true, true);
    batch.Back().x = check.point.x;
    batch.Back().y = check.point.y;
    batch.Back().z = check.point.z;
    batch.Back().map = lead->GetMapId();
    for (Player* bot : party.bots)
        if (bot != lead && bot->IsAlive())
        {
            if (h3::CommanderGuid(bot->GetGUID()) != party.expectedCommander)
            {
                OrderAnswer refusal = Refuse("not-bound", NameOf(bot) + " is not bound to the lead bot as its commander (`.tb master`): it would not follow.");
                return refusal;
            }
            batch.Add(bot, "follow", false, false);
        }
    OrderAnswer refusal;
    if (!PreflightBatch(batch, refusal))
        return refusal;
    Commit(ctx, order, nowMs, batch, answer);
    return Accept(answer, "party", answer.bots);
}

OrderAnswer DoWalk(Party const& ctx, ResolvedParty const& party, od::Parsed const& p, std::string const& order, uint32 nowMs)
{
    Player* bot = FindBotNamed(party, p.bot);
    if (!bot)
        return Refuse("no-bot", "That bot is not in your commanded group.");
    if (!bot->IsAlive() || bot->IsInFlight() || bot->IsBeingTeleported())
        return Refuse("unavailable", NameOf(bot) + " is dead, on a taxi or teleporting.");
    MovePoint wanted;
    wanted.x = p.x;
    wanted.y = p.y;
    wanted.z = p.z;
    MoveCheck const check = CheckLeadMove(bot, wanted, ctx.fromRun, false);
    od::Judged const judged = od::JudgeMove(check.facts);
    if (!judged.ok)
        return Refuse(judged);
    if (check.liquidOnWay)
        return Refuse("unreachable", NameOf(bot) + "'s way to that point crosses or ends in liquid (lava or water): no walkable path avoids it.");
    Batch batch(ctx.fromRun);
    batch.Add(bot, "come", true, true);
    batch.Back().x = check.point.x;
    batch.Back().y = check.point.y;
    batch.Back().z = check.point.z;
    batch.Back().map = bot->GetMapId();
    OrderAnswer refusal;
    if (!PreflightBatch(batch, refusal))
        return refusal;
    OrderAnswer answer;
    Commit(ctx, order, nowMs, batch, answer);
    return Accept(answer, "bot:" + NameOf(bot), 1);
}

OrderAnswer DoPull(Party const& ctx, ResolvedParty const& party, od::Parsed const& p, std::string const& order, uint32 nowMs)
{
    Player* lead = party.lead;
    if (!LeadAvailable(lead))
        return Refuse("lead-unavailable", "The lead bot is dead, on a taxi or teleporting.");
    ObjectGuid guid;
    od::Judged judged;
    Unit* target = ResolveUnit(ctx, party, p.handle, p.targetToken, guid, judged);
    if (!target)
        return Refuse(judged.ok ? od::Refused("invalid-target", "No unit with that GUID is on the group's map and instance.") : judged);
    Batch batch(ctx.fromRun);
    batch.Add(lead, p.verb == od::Verb::Pullback ? "pullback" : "pull", true, true);
    batch.Back().unit = guid;
    OrderAnswer refusal;
    if (!PreflightBatch(batch, refusal))
        return refusal;
    OrderAnswer answer;
    if (Commit(ctx, order, nowMs, batch, answer))
        SetPersistentSkull(ctx, party, target);
    return Accept(answer, "bot:" + NameOf(lead), 1);
}

OrderAnswer DoAttack(Party const& ctx, ResolvedParty const& party, od::Parsed const& p, std::string const& order, uint32 nowMs)
{
    ObjectGuid guid;
    od::Judged judged;
    Unit* target = ResolveUnit(ctx, party, p.handle, p.targetToken, guid, judged);
    if (!target)
        return Refuse(judged.ok ? od::Refused("invalid-target", "No unit with that GUID is on the group's map and instance.") : judged);
    Player* named = nullptr;
    if (!p.bot.empty())
    {
        named = FindBotNamed(party, p.bot);
        if (!named)
            return Refuse("no-bot", "That bot is not in your commanded group.");
    }
    Batch batch(ctx.fromRun);
    for (Player* bot : party.bots)
    {
        if (named && bot != named)
            continue;
        if (!bot->IsAlive())
            continue;
        if (!named)
        {
            // A party-wide order leaves the healers healing and a tank on the mob it holds (RemoteCommander::PartyAttackLeavesTankOnItsMob).
            if (PlayerbotAI::IsHeal(bot))
                continue;
            Unit* victim = bot->GetVictim();
            if (IsTankMember(bot) && victim && victim != target && victim->IsAlive() && victim->GetVictim() == bot)
                continue;
        }
        batch.Add(bot, "attack", true, named != nullptr);
        batch.Back().unit = guid;
    }
    if (batch.items.empty())
        return Refuse("failed", named ? "That bot did not accept the attack." : "No bot of the group accepted the attack.");
    OrderAnswer refusal;
    if (!PreflightBatch(batch, refusal))
        return refusal;
    OrderAnswer answer;
    if (Commit(ctx, order, nowMs, batch, answer))
        SetPersistentSkull(ctx, party, target);
    return named ? Accept(answer, "bot:" + NameOf(named), 1) : Accept(answer, "party", answer.bots);
}

OrderAnswer DoFocus(Party const& ctx, ResolvedParty const& party, od::Parsed const& p, std::string const& order, uint32 nowMs)
{
    ObjectGuid guid;
    od::Judged judged;
    Unit* target = ResolveUnit(ctx, party, p.handle, p.targetToken, guid, judged);
    if (!target)
        return Refuse(judged.ok ? od::Refused("invalid-target", "No unit with that GUID is on the group's map and instance.") : judged);
    Batch batch(ctx.fromRun);
    for (Player* bot : party.bots)
        if (bot->IsAlive())
        {
            batch.Add(bot, "focus", true, false);
            batch.Back().unit = guid;
        }
    OrderAnswer refusal;
    if (batch.items.empty() || !PreflightBatch(batch, refusal))
        return batch.items.empty() ? Refuse("failed", "No bot of the group accepted the focus order.") : refusal;
    OrderAnswer answer;
    Commit(ctx, order, nowMs, batch, answer);
    return Accept(answer, "party", answer.bots);
}

// G8: `cc <mark> <handle> <bot> [family]`. The run's crowd-control assignment (the plan's cc rows) is carried out SYNCHRONOUSLY by h8::ApplyCcOrder, the same
// call `.tb cc` makes: the group's raid icon goes onto the unit, the bot's `rti cc` and `run cc family` are set, and the change is read back (undone when it
// did not hold). No cast is started here: the mage's `cc` strategy polymorphs the `rti cc` unit when its fight starts. Nothing is queued, so nothing can
// fail late: the answer is the final one. A refusal changes nothing (ApplyCcOrder puts everything back).
OrderAnswer DoCc(RunRec& rec, Party const& ctx, ResolvedParty const& party, od::Parsed const& p)
{
    Player* bot = FindBotNamed(party, p.bot);
    if (!bot)
        return Refuse("no-bot", "That bot is not in your commanded group.");
    if (!bot->IsAlive() || bot->IsInFlight() || bot->IsBeingTeleported())
        return Refuse("unavailable", NameOf(bot) + " is dead, on a taxi or teleporting.");
    ObjectGuid guid;
    od::Judged judged;
    Unit* target = ResolveUnit(ctx, party, p.handle, p.targetToken, guid, judged);
    if (!target)
        return Refuse(judged.ok ? od::Refused("invalid-target", "No unit with that GUID is on the group's map and instance.") : judged);
    std::string code, detail;
    if (!h8::ApplyCcOrder(party.lead, bot, p.mark, guid, p.family, code, &detail))
        return Refuse(od::CcAnswerCode(code), detail.empty() ? "The crowd-control order was refused (" + code + ")." : detail);
    DropPendingCc(bot->GetName());      // a newer assignment: an older run's pending clear must leave it alone
    h8::CcOwnedRecord(rec.ccOwned, bot->GetName(), p.mark);
    return Accept(OrderAnswer(), "bot:" + NameOf(bot), 1);
}

// G8: `cc clear <bot> [<mark> <handle>]`: the bot's `rti cc` and `run cc family` go to none; with a mark and a unit the party's icon goes too, only while it
// still holds that unit. A handle that no longer names a unit is refused invalid-target (the icon cannot be matched to a unit that is not known).
OrderAnswer DoCcClear(RunRec& rec, ResolvedParty const& party, od::Parsed const& p)
{
    Player* bot = FindBotNamed(party, p.bot);
    if (!bot)
        return Refuse("no-bot", "That bot is not in your commanded group.");
    ObjectGuid guid;
    if (!p.mark.empty() && !ResolveHandle(rec.leader, p.handle, guid))
        return Refuse("invalid-target", "No unit with that GUID is known to this run.");
    std::string code, detail;
    if (!h8::ClearCcOrder(party.lead, bot, p.mark, guid, code, &detail))
        return Refuse(od::CcAnswerCode(code), detail.empty() ? "The crowd-control clear was refused (" + code + ")." : detail);
    DropPendingCc(bot->GetName());
    h8::CcOwnedForget(rec.ccOwned, bot->GetName());
    return Accept(OrderAnswer(), "bot:" + NameOf(bot), 1);
}

// A party-wide order with no argument (rest, stay, follow, hold): the bots that pass their pre-flight take it; at least one must. `verbFor` names the verb per bot.
template <class VerbFor>
OrderAnswer DoPartyWide(Party const& ctx, ResolvedParty const& party, std::string const& order, uint32 nowMs, VerbFor verbFor)
{
    Batch batch(ctx.fromRun);
    for (Player* bot : party.bots)
    {
        if (!bot->IsAlive())
            continue;
        std::string const verb = verbFor(bot);
        if (verb == "follow" && h3::CommanderGuid(bot->GetGUID()) != party.expectedCommander)
            return Refuse("not-bound", NameOf(bot) + " is not bound to the lead bot as its commander (`.tb master`): it would not follow.");
        batch.Add(bot, verb, true, false);
    }
    OrderAnswer refusal;
    if (batch.items.empty() || !PreflightBatch(batch, refusal))
        return batch.items.empty() ? Refuse("failed", "No bot of the group accepted the order.") : refusal;
    OrderAnswer answer;
    Commit(ctx, order, nowMs, batch, answer);
    return Accept(answer, "party", answer.bots);
}

// `revive party`: every dead bot releases its spirit (slice 4: inside the instance the core revives it at the entrance, D2), every living member follows the
// lead. A bot that already released (a ghost) is on its own way back and is left alone. Nothing dead: refused not-dead.
OrderAnswer DoReviveParty(Party const& ctx, ResolvedParty const& party, std::string const& order, uint32 nowMs)
{
    Batch batch(ctx.fromRun);
    uint32 dead = 0, ghosts = 0;
    for (Player* bot : party.bots)
    {
        if (bot->IsAlive())
        {
            if (bot != party.lead)
            {
                if (h3::CommanderGuid(bot->GetGUID()) != party.expectedCommander)
                    return Refuse("not-bound", NameOf(bot) + " is not bound to the lead bot as its commander (`.tb master`): it would not follow.");
                batch.Add(bot, "follow", false, false);
            }
            continue;
        }
        ++dead;
        if (bot->HasPlayerFlag(PLAYER_FLAGS_GHOST))
        {
            ++ghosts;
            continue;
        }
        batch.Add(bot, "release", true, false);
        // After its release the revived bot follows the lead again (the follow runs in the same tick, after the release).
        if (bot != party.lead && h3::CommanderGuid(bot->GetGUID()) == party.expectedCommander)
            batch.Add(bot, "follow", false, false);
    }
    if (!dead)
        return Refuse("not-dead", "No bot of the party is dead.");
    OrderAnswer answer;
    if (dead == ghosts)
        return Accept(answer, "party", 0);          // every dead bot already released: nothing to order, and that is what is true
    OrderAnswer refusal;
    if (!PreflightBatch(batch, refusal))
        return refusal;
    Commit(ctx, order, nowMs, batch, answer);
    return Accept(answer, "party", answer.bots);
}
}

namespace
{
OrderAnswer IssueOrder(Party const& ctx, std::string const& order, uint32 nowMs, bool gather)
{
    od::Parsed p;
    std::string code, message;
    if (!od::ParseOrder(order, p, code, message))
        return Refuse(code, message);
    if (p.verb == od::Verb::Summon)
        return Refuse("unsupported", "summon is not carried out on this core (decision D3): no bot is teleported to the lead bot; the party walks to it.");
    ResolvedParty party;
    OrderAnswer refusal;
    if (!ReadParty(ctx, party, refusal))
        return refusal;
    switch (p.verb)
    {
        case od::Verb::Move:
            return DoMove(ctx, party, p, order, nowMs, gather);
        case od::Verb::Walk:
            return DoWalk(ctx, party, p, order, nowMs);
        case od::Verb::Pull:
        case od::Verb::Pullback:
            return DoPull(ctx, party, p, order, nowMs);
        case od::Verb::Attack:
            return DoAttack(ctx, party, p, order, nowMs);
        case od::Verb::Focus:
            return DoFocus(ctx, party, p, order, nowMs);
        case od::Verb::Rest:
            return DoPartyWide(ctx, party, order, nowMs, [](Player*) { return std::string("rest"); });
        case od::Verb::Stay:
            return DoPartyWide(ctx, party, order, nowMs, [](Player*) { return std::string("stay"); });
        case od::Verb::Follow:
            return DoPartyWide(ctx, party, order, nowMs, [&](Player* bot) { return std::string((ctx.fromRun && bot == party.lead) ? "stay" : "follow"); });
        case od::Verb::Hold:
            return DoPartyWide(ctx, party, order, nowMs, [](Player*) { return std::string("hold"); });
        case od::Verb::ReviveParty:
            return DoReviveParty(ctx, party, order, nowMs);
        case od::Verb::Cc:
            return ctx.runRec ? DoCc(*ctx.runRec, ctx, party, p) : Refuse("unsupported", "The order `cc` is not carried out by `.tb cmd`.");
        case od::Verb::CcClear:
            return ctx.runRec ? DoCcClear(*ctx.runRec, party, p) : Refuse("unsupported", "The order `cc` is not carried out by `.tb cmd`.");
        // S9 (O9): the object, loot and loot-method orders are the task module's (TbRunTasks.cpp); `use <item>` is refused with its reason.
        case od::Verb::Open:
        case od::Verb::Loot:
            return ctx.runRec ? tasks::Begin(*ctx.runRec, party.lead, party.bots, p.task, nowMs) : Refuse("unsupported", "The order `" + p.word + "` is not carried out by `.tb cmd`.");
        case od::Verb::LootMethod:
            return ctx.runRec ? tasks::SetLootMethod(*ctx.runRec, party.lead, party.bots, p.task) : Refuse("unsupported", "The order `lootmethod` is not carried out by `.tb cmd`.");
        case od::Verb::Use:
            return Refuse("unsupported", tasks::UseUnsupportedText());
        default:
            break;
    }
    return Refuse("invalid", "That order is not carried out.");
}
}

// The P1/P2/P3 order log of the commander card (host/cmd/TbCmdBinding.cpp): every order emitted by a run or a bound commander is noted with the
// executor's own answer, here in the ONE lifted judge.
OrderAnswer Issue(Party const& party, std::string const& order, uint32 nowMs, bool gather)
{
    OrderAnswer const answer = IssueOrder(party, order, nowMs, gather);
    cmd::NoteRunOrder(party.leader, order, answer.accepted, answer.code, answer.text);
    return answer;
}

OrderAnswer Issue(RunRec& rec, std::string const& order, uint32 nowMs, bool gather)
{
    Party party;
    party.leader = rec.leader;
    party.owner = rec.leader;
    party.runRec = &rec;
    party.fromRun = true;
    party.persistentIcons = false;
    party.resolveTarget = [leader = rec.leader](uint64 handle, std::string const&, ObjectGuid& out) { return ResolveHandle(leader, handle, out); };
    return Issue(party, order, nowMs, gather);
}

void TakeLateFailures(RunRec& rec, uint32 nowMs, std::vector<LateFailure>& out)
{
    auto found = PendingByRun().find(rec.leader);
    if (found == PendingByRun().end())
        return;
    std::vector<Pending>& list = found->second;
    auto const lookup = [](ObjectGuid bot, uint32 seq)
    {
        od::SlotResult r;
        h3::OrderStatus const status = h3::LastOrder(bot);
        if (status.seq > seq)
            r.state = od::SlotState::Superseded;
        else if (status.seq == seq && status.result == "ok")
            r.state = od::SlotState::Ok;
        else if (status.seq == seq && status.result.compare(0, 5, "fail:") == 0)
        {
            r.state = od::SlotState::Failed;
            r.result = status.result;
        }
        return r;       // queued, or not known yet: still waiting
    };
    for (auto it = list.begin(); it != list.end();)
    {
        std::string failure;
        if (od::Settle(*it, nowMs, lookup, failure))
        {
            if (!failure.empty())
                out.push_back(LateFailure{ it->order, failure });
            it = list.erase(it);
        }
        else
            ++it;
    }
    if (list.empty())
        PendingByRun().erase(found);
}

void ForgetRunOrders(ObjectGuid leader)
{
    PendingByRun().erase(leader);
}

bool StartCheck(ObjectGuid leader, std::string& code, std::string& message)
{
    Player* lead = ObjectAccessor::FindPlayer(leader);
    if (!Usable(lead) || !IsHostBot(lead))
    {
        code = "not-bot";
        message = "The leader is not a host bot in the world.";
        return false;
    }
    if (!AiOf(lead))
    {
        code = "no-engine";
        message = NameOf(lead) + " has no running engine (`.tb engine <bot> on`).";
        return false;
    }
    Group* group = lead->GetGroup();
    if (!group || !group->IsLeader(lead->GetGUID()))
    {
        code = "not-leader";
        message = NameOf(lead) + " does not lead a group.";
        return false;
    }
    if (!PlayerbotAI::IsTank(lead, true))
    {
        code = "no-tank";
        message = NameOf(lead) + " has no tank role: set it with `.tb role " + NameOf(lead) + " tank` and let one engine tick pass, then start the run.";
        return false;
    }
    // Slice 10: the role bit alone is not a tank kit. `PlayerbotAI::IsTank` reads the forced role first, so a shaman ordered tank answered true while its
    // kit was the restoration one; only the warrior tank and the bear druid lead a run (D1: the protection paladin is recorded only). The kit itself must
    // carry "pull", which the run's first tank order needs (TbOrders.cpp:844); a class with a tank order but no tank kit is refused here, not started.
    if (!TortoiseBots::slice::hybrid::CanLeadRun(unsigned(lead->GetClass())))
    {
        code = "no-tank";
        message = NameOf(lead) + " cannot lead a run as its tank: only a warrior tank and a bear druid can.";
        return false;
    }
    if (!AiOf(lead)->HasStrategy("pull", BotState::BOT_STATE_COMBAT))
    {
        code = "no-tank";
        message = NameOf(lead) + " has no tank kit with a pull strategy: its role says tank but its kit cannot tank.";
        return false;
    }
    for (Group::MemberSlot const& slot : group->GetMemberSlots())
    {
        if (slot.guid == lead->GetGUID())
            continue;
        Player* member = ObjectAccessor::FindPlayer(slot.guid);
        if (!Usable(member) || !IsHostBot(member))
        {
            code = "not-bot";
            message = slot.name + " is not a host bot in the world.";
            return false;
        }
        if (!AiOf(member))
        {
            code = "no-engine";
            message = slot.name + " has no running engine (`.tb engine <bot> on`).";
            return false;
        }
    }
    return true;
}
}
}

#else

namespace TortoiseBots
{
namespace run
{
// Option OFF: the executor is slice-only code (its callers are too); this object is empty.
}
}

#endif
