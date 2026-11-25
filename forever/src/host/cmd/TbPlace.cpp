// Issue #15: `place`, `rally here` and `fall back` (see host/cmd/TbPlace.h for the surface, TbPlacePure.h for the geometry and TbPlacePlan.h for the
// command words, the plan and the hold rules). Built with TORTOISEBOTS_SLICE1 only: without the engine nothing can be placed, and `.tb cmd` already
// answers `engine-not-built` (TbH3Off.cpp answers for the whole command, shift-click included).
//
// One command = one judgement: the binding is read with the commander's own check (`cmd::ResolveBinding` + `cmd::IsCommandedBy`), the plan decides each
// slot (alive, same map, floor from GetHeightCompat, path from the same PathGenerator the move judge reads), and nothing is queued unless that bot's
// slot survived. The bots then run the module's own `come` order (TbOrders.cpp): stay position written at the slot, stay movement, a fresh ordered walk
// and the AI reset, so the order also drops the bot's target. The hold is kept here so a bot that lost its slot (combat dragged it past the engine's stay
// reach, a knockback) is walked back without being told again; every row that ends releases the bot (`follow` while it is still this commander's,
// the placement's own stay cleared when it changed commander).
#include "host/cmd/TbPlace.h"

#include "Chat.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1

#include "compat/botpch_forever.h"

#include "compat/TbCompat_Unit.h"
#include "host/TbH3.h"
#include "host/TbOrdersApi.h"
#include "host/cmd/TbCmdBinding.h"
#include "host/run/TbRun.h"
#include "host/run/TbRunLiquid.h"
#include "host/cmd/TbPlacePlan.h"
#include "host/cmd/TbPlacePure.h"

#include "GameTime.h"
#include "GridDefines.h"
#include "Group.h"
#include "Map.h"
#include "MoveSpline.h"
#include "ObjectAccessor.h"
#include "PathGenerator.h"
#include "Player.h"
#include "Unit.h"
#include "WorldPacket.h"
#include "WorldSession.h"

#include "playerbot/PlayerbotAI.h"
#include "PlayerbotAIStorage.h"

namespace TortoiseBots
{
namespace place
{
namespace
{
using h3::Rec;      // the reply record builder (TbH3.h): every line here is a `cmd` record

// How often a bot that lost its slot outside combat is walked back, at the most. The engine's own stay walk handles the small drift; this is the net
// under the cases it gives up in (`return to stay position` drops a stay point past reactDistance; a knockback lands a bot on another ledge).
// kWalkBackEveryMs (5 s) is in TbPlacePlan.h: the fall back's cap must not be measured on its clock (F1b).
// The floor search under a slot: wide enough for a ledge or a stair, narrow enough never to find another floor of the map.
constexpr float kGroundSearchYd = 25.0f;
// `fall back` forms in the normal gap.
constexpr Gap kFallbackGap = Gap::Normal;

// The floor under the stand point: the core's own vmap read over a bounded band around `baseZ`, starting high enough
// to find a slot up to kMaxSlotStepYd uphill. Never the compat helper's 10,000 yd retry: a typed point over a chasm
// must read no ground, not a floor tens of yards below.
bool GroundUnder(Map* map, float x, float y, float baseZ, float& z)
{
    if (!map || !Trinity::IsValidMapCoord(x, y))
        return false;
    float const start = baseZ + 2.0f + kMaxSlotStepYd;
    z = map->GetHeight(PhasingHandler::GetEmptyPhaseShift(), x, y, start, true, kGroundSearchYd + 2.0f + kMaxSlotStepYd);
    return z > INVALID_HEIGHT;
}

// A placement's path verdict: the same PathFinder the move judge reads, then the run's ONE move rule over the whole path
// (a bot that stands in water may walk out; a path that enters it after dry ground, or ends wet, is refused) and the detour
// rule. The length is the path's own sum, the straight line the bot's run to the slot.
PathVerdict VerdictFor(Player* bot, Vec2 const& point, float z)
{
    float const dx = bot->GetPositionX() - point.x, dy = bot->GetPositionY() - point.y, dz = bot->GetPositionZ() - z;
    if (dx * dx + dy * dy < 1.0f && std::fabs(dz) < 2.0f)
        return PathVerdict::Ok;
    Map* map = bot->FindMap();
    if (!map)
        return PathVerdict::NoPath;
    PathFinder path(bot);
    PathCalculateCompat(path, G3D::Vector3(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ()), G3D::Vector3(point.x, point.y, z), false);
    PathWalk walk;
    walk.noPath = (PathTypeCompat(path) & PATHFIND_NOPATH) != 0;
    for (G3D::Vector3 const& p : PathPointsCompat(path))
        walk.points.push_back({ p.x, p.y, p.z });
    G3D::Vector3 const end = PathActualEndCompat(path);
    walk.end = { end.x, end.y, end.z };
    return WalkVerdict(walk, point, z, { bot->GetPositionX(), bot->GetPositionY() },
        [map](float x, float y, float z)
        {
            return run::liquid::PointRefusedOnMap(map, x, y, z);
        });
}

// The commander's memory: holds, rally points and pending cursor requests (TbPlacePlan.h decides their rules).
State& Memory()
{
    static State memory;
    return memory;
}

uint32 NowMs() { return GameTime::GetGameTimeMS(); }

// Wrap-safe: the game clock is 32 bits and wraps every 49.7 days.
bool Due(uint32 now, uint32 deadline) { return TimeReached(now, deadline); }

GuidKey KeyOf(ObjectGuid guid) { return { guid.GetRawValue(1), guid.GetRawValue(0) }; }
ObjectGuid BotOf(GuidKey const& key)
{
    ObjectGuid guid;
    guid.SetRawValue(key.high, key.low);
    return guid;
}

char const* ClassWord(uint8 cls)
{
    switch (cls)
    {
        case CLASS_WARRIOR: return "warrior";
        case CLASS_PALADIN: return "paladin";
        case CLASS_HUNTER: return "hunter";
        case CLASS_ROGUE: return "rogue";
        case CLASS_PRIEST: return "priest";
        case CLASS_SHAMAN: return "shaman";
        case CLASS_MAGE: return "mage";
        case CLASS_WARLOCK: return "warlock";
        case CLASS_DRUID: return "druid";
    }
    return "";
}

// A bot's place in the slot order. The tank and healer roles come from the engine's own reads; a damage bot is melee or ranged by its kit (the engine's
// IsRanged, which itself falls back to the class table when the bot has no brain yet).
Rank RankOf(Player* bot)
{
    if (PlayerbotAI::IsTank(bot))
        return Rank::Tank;
    if (PlayerbotAI::IsHeal(bot))
        return Rank::Healer;
    if (PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot))
        if (ai->IsRanged(bot))
            return Rank::Ranged;
    switch (bot->GetClass())
    {
        case CLASS_PALADIN:
        case CLASS_WARRIOR:
        case CLASS_ROGUE:
        case CLASS_DRUID:       // no brain: no form to read, so it stands in the melee row
            return Rank::Melee;
    }
    return Rank::Ranged;
}

BotInfo InfoOf(Player* bot)
{
    BotInfo info;
    info.name = bot->GetName();
    info.className = Lower(ClassWord(bot->GetClass()));
    info.rank = RankOf(bot);
    info.group = uint32(bot->GetSubGroup()) + 1;
    return info;
}

// A bot a dungeon run drives: the run's leader or one of the members it bound. `.tb run` owns their movement, so a place hold never walks them (its `come`
// would read as an outside order and pause the run).
bool InRun(Player* bot)
{
    for (run::RunRec* rec : run::All())
    {
        if (!rec || rec->stopped || rec->run.state == DungeonRun::State::Done)
            continue;
        if (rec->leader == bot->GetGUID() || rec->bound.count(bot->GetGUID()) != 0)
            return true;
    }
    return false;
}

// The commander's binding: every bot it commands, the binding's lead first.
struct Binding
{
    ObjectGuid lead;
    std::vector<ObjectGuid> bots;
};

bool ReadBinding(ObjectGuid commander, Binding& binding)
{
    std::vector<ObjectGuid> members;
    if (!cmd::ResolveBinding(commander, binding.lead, members) || binding.lead.IsEmpty())
        return false;
    binding.bots = std::move(members);
    if (std::find(binding.bots.begin(), binding.bots.end(), binding.lead) == binding.bots.end())
        binding.bots.insert(binding.bots.begin(), binding.lead);
    return true;
}

// The same control check `.tb cmd` makes (foreign bot, another player's party): the chat form and the deferred
// minimap-ping completion run it too, so a whispered `place` cannot do what `.tb cmd place` would refuse. Only the binding's lead refuses the
// whole command; a member owned by another commander or led by another player is refused on its own row (PickBots), as round 1 specified.
bool CheckAccess(ChatHandler* handler, Binding const& binding, std::string& code, std::string& detail)
{
    return h3::CheckBotAccess(handler, binding.lead, code, detail);
}

// A point snapped to the floor within the search under `baseZ` (false when there is none, or the point is off the map).
bool SnapFloor(Player* commander, Vec2 const& point, float baseZ, float& z)
{
    return GroundUnder(commander->FindMap(), point.x, point.y, baseZ, z);
}

// `rally at:` and `fallback at:`: the typed point judged by the stand-point rules 2 and 4, the 100 yd reach (in every axis),
// the floor search and the commander's own path to it. A point over a chasm, off the map or inside geometry is refused; it
// never reaches a slot plan or the stored rally.
bool JudgeTypedPoint(Player* commander, Command const& command, Vec2& point, float& z, std::string& code, std::string& detail)
{
    point = command.point;
    z = command.pointHasZ ? command.pointZ : 0.0f;
    if (!std::isfinite(point.x) || !std::isfinite(point.y) || (command.pointHasZ && !std::isfinite(command.pointZ)))
    {
        code = "bad-argument";
        detail = "the point is not a finite coordinate";
        return false;
    }
    if (!Trinity::IsValidMapCoord(point.x, point.y)
        || (command.pointHasZ && !Trinity::IsValidMapCoord(point.x, point.y, command.pointZ)))
    {
        code = "bad-argument";
        detail = "the point is not on the map";
        return false;
    }
    float const commanderZ = commander->GetPositionZ();
    if (command.pointHasZ && std::fabs(command.pointZ - commanderZ) > kPointReachYd)
    {
        code = "too-far";
        detail = "the point is more than 100 yd from you";
        return false;
    }
    float const base = command.pointHasZ ? command.pointZ : commanderZ;
    if (!GroundUnder(commander->FindMap(), point.x, point.y, base, z))
    {
        code = "no-ground";
        detail = "no floor under the point";
        return false;
    }
    float const dx = point.x - commander->GetPositionX(), dy = point.y - commander->GetPositionY();
    if (std::sqrt(dx * dx + dy * dy) > kPointReachYd || std::fabs(z - commanderZ) > kPointReachYd)
    {
        code = "too-far";
        detail = "the point is more than 100 yd from you";
        return false;
    }
    if (run::liquid::PointRefusedOnMap(commander->FindMap(), point.x, point.y, z))
    {
        code = "liquid";
        detail = "the point is in water or lava";
        return false;
    }
    PathVerdict const verdict = VerdictFor(commander, point, z);
    if (verdict != PathVerdict::Ok)
    {
        code = PathVerdictWord(verdict);
        detail = std::string("no safe walk to the point (") + PathVerdictWord(verdict) + ")";
        return false;
    }
    return true;
}

// The anchor tank: the first tank in rank order among every bound bot that is alive, in the world, on the commander's map and instance and still
// answers to it. Not only the selected bots: `place circle tank tanks` still rings the commander's tank.
Player* FindAnchorTank(Player* commander, Binding const& binding, uint32 map, uint32 instance)
{
    Player* best = nullptr;
    for (ObjectGuid guid : binding.bots)
    {
        Player* bot = ObjectAccessor::FindPlayer(guid);
        if (!bot || !bot->IsInWorld() || !bot->IsAlive())
            continue;
        if (bot->GetMapId() != map || bot->GetInstanceId() != instance)
            continue;
        if (!cmd::IsCommandedBy(commander, bot) || RankOf(bot) != Rank::Tank)
            continue;
        if (!best || RankBefore(Rank::Tank, bot->GetName(), Rank::Tank, best->GetName()))
            best = bot;
    }
    return best;
}

// Reads the anchor of a shape from the commander's world: the points the pure FrameFor needs, plus the map, the instance, the anchor z and (for
// `tank`) the anchor bot. A cursor point and an `at:` point are floor-snapped here; the slots judge their own floors against `anchorZ`.
bool ResolveAnchor(Player* commander, Binding const& binding, Command const& command, bool haveCursor, Vec2 cursor, AnchorFacts& facts,
    ObjectGuid& anchorBot, std::string& code, std::string& detail)
{
    if (!commander->IsInWorld() || commander->IsBeingTeleported() || !commander->FindMap())
    {
        code = "teleporting";
        detail = "the commander is between maps";
        return false;
    }
    facts.kind = command.anchor;
    facts.map = commander->GetMapId();
    facts.instance = commander->GetInstanceId();
    facts.player = { commander->GetPositionX(), commander->GetPositionY() };
    facts.z = commander->GetPositionZ();
    float const ori = commander->GetOrientation();
    facts.playerFront = FrontFromOrientation(ori);      // the core's own facing: x += cos(o), y += sin(o)
    if (command.hasFace)
    {
        facts.hasFace = true;
        facts.faceFront = FaceFront(command.faceDeg);
    }
    switch (command.anchor)
    {
        case Anchor::Here:
            facts.anchorZ = facts.z;
            return true;
        case Anchor::Cursor:
        {
            if (!haveCursor)
            {
                code = "bad-argument";
                detail = "the cursor anchor needs the player's minimap ping";
                return false;
            }
            facts.cursor = cursor;
            float snapped = 0.0f;
            facts.z = SnapFloor(commander, cursor, facts.z, snapped) ? snapped : commander->GetPositionZ();
            facts.anchorZ = facts.z;
            return true;
        }
        case Anchor::Target:
        {
            ObjectGuid const guid = commander->GetTarget();
            if (guid.IsEmpty())
            {
                code = "no-selection";
                detail = "the target anchor needs the commander's target (a creature)";
                return false;
            }
            Unit* unit = ObjectAccessor::GetUnit(*commander, guid);
            if (!unit || !unit->IsInWorld())
            {
                code = "no-selection";
                detail = "the commander's target is not in the world";
                return false;
            }
            if (unit == commander || unit->IsPlayer())
            {
                code = "bad-target";
                detail = "the target anchor needs a creature: a player or a bot has no size to stand round";
                return false;
            }
            if (unit->GetMapId() != facts.map || unit->GetInstanceId() != facts.instance)
            {
                code = "other-map";
                detail = unit->GetName() + " is not on the commander's map and instance";
                return false;
            }
            facts.target = { unit->GetPositionX(), unit->GetPositionY() };
            facts.targetHalf = std::max(0.0f, unit->GetCombatReach());
            facts.z = unit->GetPositionZ();
            facts.anchorZ = facts.z;
            return true;
        }
        case Anchor::Tank:
        {
            Player* tank = FindAnchorTank(commander, binding, facts.map, facts.instance);
            if (!tank)
            {
                code = "no-tank";
                detail = "you command no living tank on this map";
                return false;
            }
            anchorBot = tank->GetGUID();
            facts.tankPos = { tank->GetPositionX(), tank->GetPositionY() };
            float const tankOri = tank->GetOrientation();
            facts.tankFacing = FrontFromOrientation(tankOri);
            facts.tankReach = std::max(0.0f, tank->GetCombatReach());
            facts.z = tank->GetPositionZ();
            facts.anchorZ = facts.z;
            return true;
        }
        case Anchor::Point:
        {
            Vec2 point;
            float z = 0.0f;
            if (!JudgeTypedPoint(commander, command, point, z, code, detail))
                return false;
            facts.point = point;
            facts.z = z;
            facts.anchorZ = z;
            return true;
        }
    }
    return false;
}

bool QueueCome(Player* bot, float x, float y, float z, bool placed, bool disengage, uint32 passiveToken, uint32& seq, std::string& code,
    std::string& detail)
{
    h3::OrderSpec spec;
    spec.verb = "come";
    spec.x = x;
    spec.y = y;
    spec.z = z;
    spec.map = bot->GetMapId();
    spec.placed = placed;
    spec.disengage = disengage;
    spec.passiveToken = passiveToken;
    return h3::QueueOrder(bot, spec, code, detail, seq);
}

bool QueueFollow(Player* bot, uint32& seq, std::string& code, std::string& detail)
{
    h3::OrderSpec spec;
    spec.verb = "follow";
    return h3::QueueOrder(bot, spec, code, detail, seq);
}

// The bots a command moves and the rows that cannot take a slot at all.
struct Picked
{
    std::vector<Player*> bots;
    std::vector<BotInfo> info;
    std::vector<Rec> refused;
};

// Resolves the who words against the commander's own bots. A word that names no bot and no filter is a typed error (a typo never places nobody
// silently); a bound bot that cannot be placed is a refused row, never a whole-command refusal.
bool PickBots(Player* commander, Binding const& binding, std::vector<WhoToken> const& who, std::string const& verb, Picked& picked,
    std::string& code, std::string& detail)
{
    std::vector<std::pair<Player*, BotInfo>> candidates;
    std::vector<std::string> refusedNames;      // a named bot that was refused for its own reason, not a typo
    for (ObjectGuid guid : binding.bots)
    {
        Player* bot = ObjectAccessor::FindPlayer(guid);
        if (!bot || !bot->IsInWorld())
        {
            Rec rec("cmd");
            rec.S("bot", h3::GuidText(guid)).S("verb", verb).S("status", "refused")
                .S("code", "no-character").T("msg", "the bound bot is not in the world");
            picked.refused.push_back(std::move(rec));
            continue;
        }
        std::string accessCode, accessDetail;
        if (!h3::CheckBotAccess(commander, bot->GetGUID(), accessCode, accessDetail))
        {
            Rec rec("cmd");
            rec.S("bot", bot->GetName()).S("verb", verb).S("status", "refused")
                .S("code", accessCode).T("msg", accessDetail);
            refusedNames.push_back(Lower(bot->GetName()));
            picked.refused.push_back(std::move(rec));
            continue;
        }
        if (!cmd::IsCommandedBy(commander, bot))
        {
            Rec rec("cmd");
            rec.S("bot", bot->GetName()).S("verb", verb).S("status", "refused")
                .S("code", "not-in-group").T("msg", "the bot does not answer to this commander any more (.tb master)");
            refusedNames.push_back(Lower(bot->GetName()));
            picked.refused.push_back(std::move(rec));
            continue;
        }
        candidates.emplace_back(bot, InfoOf(bot));
    }
    for (WhoToken const& token : who)
    {
        if (token.kind != WhoKind::Name)
            continue;
        bool found = std::find(refusedNames.begin(), refusedNames.end(), Lower(token.word)) != refusedNames.end();
        for (auto const& [bot, info] : candidates)
            if (Lower(bot->GetName()) == Lower(token.word))
            {
                found = true;
                break;
            }
        if (!found)
        {
            code = "bad-target";
            detail = token.word + " is not one of the bots you command";
            return false;
        }
    }
    std::vector<size_t> order;
    for (size_t i = 0; i < candidates.size(); ++i)
        if (Selected(who, candidates[i].second))
            order.push_back(i);
    if (order.empty())
    {
        code = "no-bots";
        detail = "none of the bots you command matches that selection";
        return false;
    }
    // The slot order: tanks first, then melee, ranged, healers; the name breaks a tie so one command always builds the same slots.
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b)
    {
        return RankBefore(candidates[a].second.rank, candidates[a].second.name, candidates[b].second.rank, candidates[b].second.name);
    });
    for (size_t index : order)
    {
        picked.bots.push_back(candidates[index].first);
        picked.info.push_back(candidates[index].second);
    }
    return true;
}

// The two world questions the pure plan asks: the floor under a slot and whether this bot can walk there.
Probe WorldProbe(Player* commander, std::vector<Player*> const& bots, float baseZ)
{
    Probe probe;
    Map* map = commander->FindMap();
    probe.ground = [map, baseZ](Vec2 const& point, float& z)
    {
        return GroundUnder(map, point.x, point.y, baseZ, z);
    };
    probe.liquid = [map](Vec2 const& point, float z)
    {
        return !map || run::liquid::PointRefusedOnMap(map, point.x, point.y, z);
    };
    probe.reachable = [&bots](size_t bot, Vec2 const& point, float z)
    {
        return bot < bots.size() ? VerdictFor(bots[bot], point, z) : PathVerdict::NoPath;
    };
    return probe;
}

constexpr uint32 kUpdateMs = 1000;

// Queues one `come` per planned bot and fills the hold rows; returns how many were queued. A planned refusal is already in the plan's count.
// `placed` marks a placement's own order (only its `come` keeps the combat-time behaviour); `disengage` is a fall back: the order stops the bot's
// swing, its cast and its pet's attack, and holds it passive until it reaches the spot.
uint32 ApplyPlan(h3::Reply& reply, std::string const& verb, PlanResult const& plan, Picked const& picked,
    AnchorFacts const& anchor, bool placed, bool disengage, std::vector<HoldRow>& rows, std::vector<GuidKey>& addressed, uint32& failed)
{
    uint32 const now = NowMs();
    for (Step const& step : plan.steps)
    {
        Player* bot = picked.bots[step.bot];
        Rec rec("cmd");
        rec.S("bot", bot->GetName()).S("verb", verb).S("role", RankWord(picked.info[step.bot].rank)).N("slot", int64(step.slot));
        addressed.push_back(KeyOf(bot->GetGUID()));
        if (!step.placed)
        {
            rec.S("status", "refused").S("code", step.code).T("msg", step.detail);
            reply.Line(rec);
            continue;
        }
        uint32 seq = 0;
        std::string code, detail;
        if (!QueueCome(bot, step.pos.x, step.pos.y, step.z, placed, disengage, 0, seq, code, detail))
        {
            rec.S("status", "refused").S("code", code.empty() ? std::string("failed") : code).T("msg", detail);
            reply.Line(rec);
            ++failed;
            continue;
        }
        rec.S("status", "queued");
        if (!step.to.empty())
            rec.S("to", step.to).S("code", step.code);
        else if (step.nudged > 0.0f)
            rec.F("nudged", double(step.nudged), 1);
        rec.Coord("pos", step.pos.x, step.pos.y, step.z).N("seq", int64(seq));
        reply.Line(rec);
        HoldRow row;
        row.bot = KeyOf(bot->GetGUID());
        row.x = step.pos.x;
        row.y = step.pos.y;
        row.z = step.z;
        row.map = anchor.map;
        row.instance = anchor.instance;
        row.slot = step.slot;
        row.seq = seq;
        row.rank = picked.info[step.bot].rank;
        row.placed = placed;
        row.disengage = disengage;
        StartRowClock(row, now);      // the passive's owner is the fall back's own `come`: the flags are set before it
        rows.push_back(row);
    }
    return uint32(rows.size());
}

void NoteOrder(Binding const& binding, std::string const& order, std::string const& result)
{
    cmd::NoteRunOrder(binding.lead, order, true, "", result);
}

// Releases a bot whose hold row ended: `follow` while this commander still commands it; a bot that changed commander only has the stay this
// placement wrote cleared (and only while that `come` is still its last order).
void ReleaseRow(HoldRow const& row, Release how)
{
    if (how == Release::None)
        return;
    Player* bot = ObjectAccessor::FindPlayer(BotOf(row.bot));
    if (!bot || !bot->IsInWorld())
        return;      // nothing to release
    if (how == Release::Follow)
    {
        // A newer order (a higher number than the row's `come`) owns the bot now: its order decides, so this release sends nothing
        // (the emergency passive below is still dropped: it never outlives the row).
        if (!NewerOrderOwns(h3::LastOrder(bot->GetGUID()).seq, row.seq))
        {
            uint32 seq = 0;
            std::string code, detail;
            QueueFollow(bot, seq, code, detail);
        }
    }
    else
        h3::ReleasePlaceStay(bot, row.seq);
    if (row.disengage)
        h3::ReleasePlacePassive(bot, PassiveOwner(row));      // the emergency hold never outlives its row (cancel, death, logout, unbind), and never strips another's
}

// Every dropped row is released. `commander` may be null (the commander left): then only the placement's own stay is cleared.
void ReleaseDropped(ObjectGuid commander, std::vector<HoldRow> const& rows)
{
    Player* cmdPlayer = ObjectAccessor::FindPlayer(commander);
    for (HoldRow const& row : rows)
    {
        Player* bot = ObjectAccessor::FindPlayer(BotOf(row.bot));
        bool const ours = cmdPlayer && bot && bot->IsInWorld() && cmd::IsCommandedBy(cmdPlayer, bot);
        ReleaseRow(row, ours ? Release::Follow : Release::ClearStay);
    }
}

std::vector<BotFacts> FactsOf(Picked const& picked, AnchorFacts const& anchor)
{
    std::vector<BotFacts> facts;
    for (size_t i = 0; i < picked.bots.size(); ++i)
    {
        Player* bot = picked.bots[i];
        BotFacts fact;
        fact.name = picked.info[i].name;
        fact.className = picked.info[i].className;
        fact.group = picked.info[i].group;
        fact.rank = picked.info[i].rank;
        fact.alive = bot->IsAlive();
        fact.sameMap = bot->GetMapId() == anchor.map && bot->GetInstanceId() == anchor.instance;
        facts.push_back(std::move(fact));
    }
    return facts;
}

// The reply of one shape placement. `anchorName` is the word the caller typed (`here`, `target`, `cursor`, `tank`, `at`).
bool RunPlace(ChatHandler* handler, h3::Words const& args, Player* commander, Binding const& binding, Command const& command,
    AnchorFacts const& anchor, std::string const& anchorName, ObjectGuid anchorBot = ObjectGuid())
{
    Picked picked;
    std::string code, detail;
    if (!PickBots(commander, binding, command.who, "place", picked, code, detail))
        return h3::Fail(handler, args, "cmd", code, detail);
    // The anchor tank is never in the shape: it holds its own mob and its own hold row (if any) is untouched. The plan excludes it,
    // so a name that matches the tank is satisfied by the tank and never a `bad-target`.
    std::string tankName;
    if (!anchorBot.IsEmpty())
        if (Player* tank = ObjectAccessor::FindPlayer(anchorBot))
            tankName = tank->GetName();

    std::vector<BotFacts> const facts = FactsOf(picked, anchor);
    PlanResult const plan = PlanPlace(command.shape, command.gapSpec, anchor, facts, command.who,
        WorldProbe(commander, picked.bots, anchor.z), tankName);
    if (!plan.ok)
        return h3::Fail(handler, args, "cmd", plan.code, plan.detail);

    h3::Reply reply(handler, args, "cmd");
    reply.Begin();
    Rec head("cmd");
    head.S("verb", "place").S("status", "accepted").N("bots", int64(plan.selected))
        .S("shape", ShapeWord(command.shape)).S("gap", GapText(command.gapSpec)).S("anchor", anchorName);
    if (command.hasFace)
        head.N("face", command.faceDeg);
    if (command.anchor == Anchor::Point)
        head.Coord("at", anchor.point.x, anchor.point.y, anchor.anchorZ);
    if (!anchorBot.IsEmpty())
    {
        Player* tank = ObjectAccessor::FindPlayer(anchorBot);
        head.S("anchor_bot", tank ? tank->GetName() : h3::GuidText(anchorBot));
    }
    if (IsRing(command.shape))
        head.F("radius", plan.layout.radius, 1);
    head.N("slots", int64(plan.layout.slots.size()));
    reply.Line(head);

    uint32 failed = 0, refused = uint32(picked.refused.size() + plan.refused);
    for (Rec const& rec : picked.refused)
        reply.Line(rec);
    std::vector<HoldRow> rows;
    std::vector<GuidKey> addressed;
    uint32 const placed = ApplyPlan(reply, "place", plan, picked, anchor, true, false, rows, addressed, failed);
    refused += failed;
    std::vector<HoldRow> const dropped = StorePlace(Memory(), commander->GetGUID().GetCounter(), rows, addressed, placed > 0);
    ReleaseDropped(commander->GetGUID(), dropped);
    NoteOrder(binding, "place " + std::string(ShapeWord(command.shape)) + " " + GapText(command.gapSpec) + " " + anchorName,
        "placed " + std::to_string(placed));

    Player* lead = ObjectAccessor::FindPlayer(binding.lead);
    Rec end("cmd");
    end.S("verb", "place").S("lead", lead ? lead->GetName() : h3::GuidText(binding.lead))
        .N("bots", int64(plan.selected)).N("placed", int64(placed)).N("refused", int64(refused));
    reply.End(end);
    return true;
}

// `place follow [who...]`: the selected bots go back to following and their slot is released.
bool RunPlaceFollow(ChatHandler* handler, h3::Words const& args, Player* commander, Binding const& binding, std::vector<WhoToken> const& who)
{
    Picked picked;
    std::string code, detail;
    if (!PickBots(commander, binding, who, "place", picked, code, detail))
        return h3::Fail(handler, args, "cmd", code, detail);

    h3::Reply reply(handler, args, "cmd");
    reply.Begin();
    Rec head("cmd");
    head.S("verb", "place").S("status", "accepted").B("follow", true).N("bots", int64(picked.bots.size()));
    reply.Line(head);
    uint32 released = 0, refused = uint32(picked.refused.size());
    std::vector<GuidKey> freed;
    for (Rec const& rec : picked.refused)
        reply.Line(rec);
    for (Player* bot : picked.bots)
    {
        Rec rec("cmd");
        rec.S("bot", bot->GetName()).S("verb", "place").B("follow", true);
        uint32 seq = 0;
        std::string fail, message;
        if (!QueueFollow(bot, seq, fail, message))
        {
            rec.S("status", "refused").S("code", fail.empty() ? std::string("failed") : fail).T("msg", message);
            reply.Line(rec);
            ++refused;
            continue;
        }
        rec.S("status", "queued").N("seq", int64(seq));
        reply.Line(rec);
        freed.push_back(KeyOf(bot->GetGUID()));
        ++released;
    }
    for (HoldRow const& row : DropRows(Memory(), commander->GetGUID().GetCounter(), [&](HoldRow const& row)
    {
        return std::any_of(freed.begin(), freed.end(), [&](GuidKey const& key) { return key == row.bot; });
    }))
        if (row.disengage)
            if (Player* bot = ObjectAccessor::FindPlayer(BotOf(row.bot)))
                h3::ReleasePlacePassive(bot, PassiveOwner(row));
    NoteOrder(binding, "place follow", "released " + std::to_string(released));

    Player* lead = ObjectAccessor::FindPlayer(binding.lead);
    Rec end("cmd");
    end.S("verb", "place").S("lead", lead ? lead->GetName() : h3::GuidText(binding.lead))
        .N("bots", int64(picked.bots.size())).N("released", int64(released)).N("refused", int64(refused));
    reply.End(end);
    return true;
}

// `place <shape> ...`: resolve the anchor and (cursor) park the request for the player's ping.
bool HandlePlace(ChatHandler* handler, h3::Words const& args, Player* commander, Binding const& binding, Command const& command)
{
    if (command.kind == CommandKind::PlaceFollow)
        return RunPlaceFollow(handler, args, commander, binding, command.who);
    if (command.anchor == Anchor::Cursor)
    {
        // The slots cannot be known before the ping: park the request and answer `pending=yes`; the packet hook (or the timeout) answers again.
        PendingRequest pending;
        pending.commander = commander->GetGUID().GetCounter();
        pending.deadline = NowMs() + uint32(kCursorWaitSeconds) * 1000u;
        pending.shape = command.shape;
        pending.gap = command.gapSpec;
        pending.hasFace = command.hasFace;
        pending.faceDeg = command.faceDeg;
        pending.who = command.who;
        pending.words = args.w;
        pending.token = args.token;
        pending.badToken = args.badToken;
        Memory().pendings[commander->GetGUID().GetCounter()] = std::move(pending);

        h3::Reply reply(handler, args, "cmd");
        reply.Begin();
        Rec head("cmd");
        head.S("verb", "place").S("status", "waiting").S("anchor", "cursor")
            .S("shape", ShapeWord(command.shape)).S("gap", GapText(command.gapSpec));
        if (command.hasFace)
            head.N("face", command.faceDeg);
        head.N("wait", int64(kCursorWaitSeconds));
        reply.Line(head);
        Player* lead = ObjectAccessor::FindPlayer(binding.lead);
        Rec end("cmd");
        end.S("verb", "place").S("lead", lead ? lead->GetName() : h3::GuidText(binding.lead)).B("pending", true);
        reply.End(end);
        return true;
    }

    AnchorFacts facts;
    ObjectGuid anchorBot;
    std::string code, detail;
    if (!ResolveAnchor(commander, binding, command, false, {}, facts, anchorBot, code, detail))
        return h3::Fail(handler, args, "cmd", code, detail);
    return RunPlace(handler, args, commander, binding, command, facts, AnchorWord(command.anchor), anchorBot);
}

// `fallback` / `fall back` / `back`: every bot drops its target, runs to the chosen point (stored rally, the commander, or a typed point) and forms
// up there. A bot whose own slot fails still disengages (PlanFallback).
bool HandleFallback(ChatHandler* handler, h3::Words const& args, Player* commander, Binding const& binding, Command const& command)
{
    AnchorFacts facts;
    ObjectGuid anchorBot;
    std::string code, detail;
    if (!ResolveAnchor(commander, binding, command, false, {}, facts, anchorBot, code, detail))
        return h3::Fail(handler, args, "cmd", code, detail);

    RallyState const* storedRally = StoredRallyFor(Memory(), commander->GetGUID().GetCounter(), facts.map, facts.instance);

    Vec2 point{ facts.player.x, facts.player.y };
    float pointZ = facts.z;
    if (command.fallbackTo == FallbackTo::At && !JudgeTypedPoint(commander, command, point, pointZ, code, detail))
        return h3::Fail(handler, args, "cmd", code, detail);
    Vec2 const storedPoint = storedRally ? Vec2{ storedRally->x, storedRally->y } : Vec2{ facts.player.x, facts.player.y };
    float const storedZ = storedRally ? storedRally->z : facts.z;
    FallbackChoice const choice = ChooseFallback(command.fallbackTo, storedRally != nullptr, storedPoint, storedZ,
        facts.player, facts.z, point, pointZ);
    if (!choice.ok)
        return h3::Fail(handler, args, "cmd", choice.code, choice.detail);
    Vec2 const centre = choice.centre;
    float const z = choice.z;
    bool const frontStored = choice.frontStored;
    std::string const& from = choice.from;
    facts.z = z;

    Picked picked;
    if (!PickBots(commander, binding, {}, "fallback", picked, code, detail))
        return h3::Fail(handler, args, "cmd", code, detail);

    GapSpec gap;
    gap.preset = kFallbackGap;
    gap.yd = GapYd(kFallbackGap);
    std::vector<BotFacts> const factsOfBots = FactsOf(picked, facts);
    PlanResult const plan = PlanFallback(gap, centre, facts.player, facts.playerFront, frontStored, z, commander->GetPositionZ(),
        factsOfBots, WorldProbe(commander, picked.bots, z));
    if (!plan.ok)
        return h3::Fail(handler, args, "cmd", plan.code, plan.detail);

    h3::Reply reply(handler, args, "cmd");
    reply.Begin();
    Rec head("cmd");
    head.S("verb", "fallback").S("status", "accepted").N("bots", int64(plan.selected))
        .S("gap", GapText(gap)).S("from", from)
        .Coord("rally", centre.x, centre.y, z).N("slots", int64(plan.layout.slots.size()));
    reply.Line(head);
    uint32 failed = 0, refused = uint32(picked.refused.size() + plan.refused);
    for (Rec const& rec : picked.refused)
        reply.Line(rec);
    std::vector<HoldRow> rows;
    std::vector<GuidKey> addressed;
    uint32 const placed = ApplyPlan(reply, "fallback", plan, picked, facts, false, true, rows, addressed, failed);
    refused += failed;
    std::vector<HoldRow> const dropped = StoreFallback(Memory(), commander->GetGUID().GetCounter(), rows);
    ReleaseDropped(commander->GetGUID(), dropped);
    NoteOrder(binding, "fallback", "formed " + std::to_string(placed) + " of " + std::to_string(plan.selected));

    Player* lead = ObjectAccessor::FindPlayer(binding.lead);
    Rec end("cmd");
    end.S("verb", "fallback").S("lead", lead ? lead->GetName() : h3::GuidText(binding.lead))
        .N("bots", int64(plan.selected)).N("placed", int64(placed)).N("refused", int64(refused));
    reply.End(end);
    return true;
}

// `rally` (read-back), `rally here`, `rally clear` and `rally at:<x>,<y>[,<z>]`: the fall-back point is memory only; nothing moves.
bool HandleRally(ChatHandler* handler, h3::Words const& args, Player* commander, Binding const& binding, Command const& command)
{
    AnchorFacts facts;
    ObjectGuid anchorBot;
    std::string code, detail;
    if (!ResolveAnchor(commander, binding, command, false, {}, facts, anchorBot, code, detail))
        return h3::Fail(handler, args, "cmd", code, detail);
    uint32 const key = commander->GetGUID().GetCounter();
    auto it = Memory().rallies.find(key);
    bool const stored = it != Memory().rallies.end() && it->second.set;

    if (command.rallyWord == RallyWord::Show)
    {
        h3::Reply reply(handler, args, "cmd");
        reply.Begin();
        Rec rec("cmd");
        rec.S("verb", "rally").S("status", "shown");
        if (stored)
            rec.B("set", true).Coord("pos", it->second.x, it->second.y, it->second.z)
                .N("map", int64(it->second.map)).N("inst", int64(it->second.instance));
        else
            rec.B("set", false);
        reply.Line(rec);
        Rec end("cmd");
        end.S("verb", "rally").N("set", int64(stored ? 1 : 0));
        reply.End(end);
        return true;
    }
    if (command.rallyWord == RallyWord::Clear)
    {
        if (it != Memory().rallies.end())
            Memory().rallies.erase(it);
        h3::Reply reply(handler, args, "cmd");
        reply.Begin();
        Rec rec("cmd");
        rec.S("verb", "rally").S("status", "accepted").B("cleared", stored);
        reply.Line(rec);
        Rec end("cmd");
        end.S("verb", "rally").B("cleared", stored);
        reply.End(end);
        return true;
    }

    // The point is decided first and the memory is written only after the decision: a refused `rally at:` leaves the stored point
    // (absent, or an older one on its own map) exactly as it was.
    RallyDecision const decision = DecideRally(command.rallyWord, facts.player, facts.z, facts.map, facts.instance, command.point,
        [commander, &command](Vec2& point, float& z, std::string& atCode, std::string& atDetail)
        {
            return JudgeTypedPoint(commander, command, point, z, atCode, atDetail);
        });
    if (!decision.ok)
        return h3::Fail(handler, args, "cmd", decision.code, decision.detail);
    Memory().rallies[key] = decision.value;
    RallyState const& rally = decision.value;
    NoteOrder(binding, command.rallyWord == RallyWord::At ? "rally at" : "rally here", "rally point set");

    h3::Reply reply(handler, args, "cmd");
    reply.Begin();
    Rec rec("cmd");
    rec.S("verb", "rally").S("status", "accepted").S("lead", commander->GetName())
        .Coord("pos", rally.x, rally.y, rally.z).N("map", int64(rally.map)).N("inst", int64(rally.instance));
    reply.Line(rec);
    Rec end("cmd");
    end.S("verb", "rally").N("set", int64(1));
    reply.End(end);
    return true;
}

// `resume`: end a fall back and put the bots that formed up back on follow. A place hold is not touched (that is `place follow` / `follow`).
bool HandleResume(ChatHandler* handler, h3::Words const& args, Player* commander, Binding const& binding)
{
    uint32 const key = commander->GetGUID().GetCounter();
    auto it = Memory().holds.find(key);
    if (it == Memory().holds.end() || !it->second.fallback || it->second.rows.empty())
        return h3::Fail(handler, args, "cmd", "no-fallback", "the bots are not holding a fall back (the command never formed one, or an order already ended it)");

    h3::Reply reply(handler, args, "cmd");
    reply.Begin();
    Rec head("cmd");
    head.S("verb", "resume").S("status", "accepted").N("bots", int64(it->second.rows.size()));
    reply.Line(head);
    uint32 released = 0, refused = 0;
    std::vector<GuidKey> freed, stale;
    std::vector<HoldRow> const rows = it->second.rows;
    for (HoldRow const& row : rows)
    {
        Player* bot = ObjectAccessor::FindPlayer(BotOf(row.bot));
        Rec rec("cmd");
        rec.S("bot", bot && bot->IsInWorld() ? bot->GetName() : h3::GuidText(BotOf(row.bot))).S("verb", "resume")
            .S("role", RankWord(row.rank)).N("slot", int64(row.slot));
        // The row is historical: before it turns into an order, the bot must still answer to this commander.
        if (!bot || !bot->IsInWorld() || h3::CommanderGuid(bot->GetGUID()) != commander->GetGUID() || !cmd::IsCommandedBy(commander, bot))
        {
            rec.S("status", "refused").S("code", "not-in-group").T("msg", "the bot does not answer to this commander any more (.tb master)");
            reply.Line(rec);
            ++refused;
            stale.push_back(row.bot);
            continue;
        }
        uint32 seq = 0;
        std::string code, detail;
        if (!QueueFollow(bot, seq, code, detail))
        {
            rec.S("status", "refused").S("code", code.empty() ? std::string("failed") : code).T("msg", detail);
            reply.Line(rec);
            ++refused;
            continue;
        }
        rec.S("status", "queued").N("seq", int64(seq));
        reply.Line(rec);
        freed.push_back(row.bot);
        ++released;
    }
    // A released row is done; a stale one is dropped and only its placement stay is cleared.
    for (HoldRow const& row : DropRows(Memory(), key, [&](HoldRow const& row)
    {
        return std::any_of(freed.begin(), freed.end(), [&](GuidKey const& guid) { return guid == row.bot; });
    }))
        if (row.disengage)
            if (Player* bot = ObjectAccessor::FindPlayer(BotOf(row.bot)))
                h3::ReleasePlacePassive(bot, PassiveOwner(row));
    std::vector<HoldRow> const dropped = DropRows(Memory(), key, [&](HoldRow const& row)
    {
        return std::any_of(stale.begin(), stale.end(), [&](GuidKey const& guid) { return guid == row.bot; });
    });
    ReleaseDropped(commander->GetGUID(), dropped);
    NoteOrder(binding, "resume", "released " + std::to_string(released));

    Rec end("cmd");
    end.S("verb", "resume").N("bots", int64(rows.size())).N("released", int64(released)).N("refused", int64(refused));
    reply.End(end);
    return true;
}

// The pending cursor request completes: the point is known now, the command runs with it and answers with the same `#token`.
void CompleteCursor(ChatHandler* handler, Player* commander, PendingRequest const& pending, h3::Words const& args, Vec2 cursor)
{
    Binding binding;
    if (!ReadBinding(commander->GetGUID(), binding))
    {
        h3::Fail(handler, args, "cmd", "not-bound", "the binding ended while the command waited for the ping: no bot was moved");
        return;
    }
    std::string code, detail;
    if (!CheckAccess(handler, binding, code, detail))
    {
        h3::Fail(handler, args, "cmd", code, detail);
        return;
    }
    Command command;
    command.kind = CommandKind::Place;
    command.shape = pending.shape;
    command.gapSpec = pending.gap;
    command.anchor = Anchor::Cursor;
    command.hasFace = pending.hasFace;
    command.faceDeg = pending.faceDeg;
    command.who = pending.who;
    AnchorFacts facts;
    ObjectGuid anchorBot;
    if (!ResolveAnchor(commander, binding, command, true, cursor, facts, anchorBot, code, detail))
    {
        h3::Fail(handler, args, "cmd", code, detail);
        return;
    }
    RunPlace(handler, args, commander, binding, command, facts, "cursor");
}

h3::Words WordsOf(PendingRequest const& pending)
{
    h3::Words args;
    args.w = pending.words;
    args.token = pending.token;
    args.badToken = pending.badToken;
    return args;
}
}

bool Handle(ChatHandler* handler, h3::Words const& args)
{
    if (args.w.empty() || !IsPlaceVerbWord(args.w[0]))
        return false;
    if (args.badToken)
        return h3::Fail(handler, args, "cmd", "bad-token", "a token starts with # and holds 1-32 characters of [A-Za-z0-9_.:-]");

    Player* commander = h3::SenderPlayer(handler);
    if (!commander)
        return h3::Fail(handler, args, "cmd", "needs-player-sender",
            "place, rally and fall back address the caller's own bots; run them from a player character");
    // Any later command from this commander ends a pending cursor request: its token gets `cancelled`, and nothing else may place on the old ping.
    CancelPending(commander->GetGUID(), handler);

    Command const command = ParseCommand(args.w);
    if (!command.code.empty())
        return h3::Fail(handler, args, "cmd", command.code, command.detail);

    Binding binding;
    if (!ReadBinding(commander->GetGUID(), binding))
        return h3::Fail(handler, args, "cmd", "not-bound", "you have no bound bots (`.tb master <bot|party> <commander>`)");
    {
        std::string accessCode, accessDetail;
        if (!CheckAccess(handler, binding, accessCode, accessDetail))
            return h3::Fail(handler, args, "cmd", accessCode, accessDetail);
    }

    switch (command.kind)
    {
        case CommandKind::Place:
        case CommandKind::PlaceFollow:
            return HandlePlace(handler, args, commander, binding, command);
        case CommandKind::RallyShow:
        case CommandKind::RallyHere:
        case CommandKind::RallyClear:
        case CommandKind::RallyAt:
            return HandleRally(handler, args, commander, binding, command);
        case CommandKind::Fallback:
            return HandleFallback(handler, args, commander, binding, command);
        case CommandKind::Resume:
            return HandleResume(handler, args, commander, binding);
        case CommandKind::None:
            break;
    }
    return false;
}

void CancelPending(ObjectGuid commander, ChatHandler* handler)
{
    PendingRequest pending;
    if (!TakePending(Memory(), commander.GetCounter(), pending))
        return;
    if (!handler)
        return;
    h3::Words const args = WordsOf(pending);
    h3::Fail(handler, args, "cmd", "cancelled", "a later command from the commander replaced this cursor request");
}

void NoteLogout(Player* player)
{
    if (!player)
        return;
    uint32 const key = player->GetGUID().GetCounter();
    auto const hold = Memory().holds.find(key);
    if (hold != Memory().holds.end())
        for (HoldRow const& row : hold->second.rows)
            if (row.disengage)
                if (Player* bot = ObjectAccessor::FindPlayer(BotOf(row.bot)))
                    h3::ReleasePlacePassive(bot, PassiveOwner(row));
    Forget(Memory(), key);
}

bool IsPlaceText(std::string const& text)
{
    h3::Words const args = h3::Split(text);
    return ChatWordsAreCommand(args.w);
}

bool HandleChat(Player* sender, std::string const& text, Group* group)
{
    if (!sender || !sender->GetSession() || !IsPlaceText(text))
        return false;
    // Only a commander's chat is an order: anybody else's "back" or "place ..." is ordinary conversation and is left alone.
    ObjectGuid lead;
    std::vector<ObjectGuid> members;
    if (!cmd::ResolveBinding(sender->GetGUID(), lead, members) || lead.IsEmpty())
        return false;
    // Party/raid chat only counts when the channel holds the bots it would order: a commander typing "back" in a group of
    // strangers must not move bots standing elsewhere. (A whisper already checked its one receiver.)
    if (group)
    {
        bool holdsBot = group->IsMember(lead);
        for (size_t i = 0; i < members.size() && !holdsBot; ++i)
            holdsBot = group->IsMember(members[i]);
        if (!holdsBot)
            return false;
    }
    h3::Words const args = h3::Split(text);
    ChatHandler handler(sender->GetSession());
    return Handle(&handler, args);
}

void OnPacket(WorldSession* session, WorldPacket& packet)
{
    if (!session || packet.GetOpcode() != CMSG_MINIMAP_PING)
        return;
    Player* commander = session->GetPlayer();
    if (!commander)
        return;
    auto it = Memory().pendings.find(commander->GetGUID().GetCounter());
    if (it == Memory().pendings.end())
        return;
    CursorPoint point;
    if (!ParseMinimapPing(packet.data(), packet.size(), point))
        return;
    if (!Trinity::IsValidMapCoord(point.x, point.y))
        return;
    PendingRequest const pending = it->second;
    Memory().pendings.erase(it);
    ChatHandler handler(session);
    h3::Words const args = WordsOf(pending);
    CompleteCursor(&handler, commander, pending, args, { point.x, point.y });
}

void NoteOrdered(ObjectGuid commander, std::string const& verb)
{
    auto it = Memory().holds.find(commander.GetCounter());
    if (it == Memory().holds.end())
        return;
    // `follow` releases every hold (its own SetMovementStrategy("follow") drops the stay; the emergency passive is released here, so a
    // fall back cannot leave a bot passive behind the new order); the orders that end a fall back must clear the emergency movement
    // first, or the bots stay anchored on `stay`/`passive` while the new order expects them to move.
    if (verb == "follow")
    {
        for (HoldRow const& row : it->second.rows)
            if (row.disengage)
                if (Player* bot = ObjectAccessor::FindPlayer(BotOf(row.bot)))
                    h3::ReleasePlacePassive(bot, PassiveOwner(row));
        Memory().holds.erase(it);
    }
    else if (it->second.fallback && (verb == "attack" || verb == "pull" || verb == "pullback" || verb == "resume"))
    {
        for (HoldRow const& row : it->second.rows)
            if (Player* bot = ObjectAccessor::FindPlayer(BotOf(row.bot)))
            {
                h3::ReleasePlaceHold(bot, row.x, row.y, row.z);
                if (row.disengage)
                    h3::ReleasePlacePassive(bot, PassiveOwner(row));
            }
        Memory().holds.erase(it);
    }
}

void Update()
{
    // The world tick (OnBeforeMapsUpdate) runs many times a second; the ping deadline and the walk back only need one pass a second, and a hold on
    // five bots would otherwise mean five player lookups every world update. The clock is uint32 and wraps (TimeReached), not int64.
    static uint32 nextMs = 0;
    uint32 const now = NowMs();
    if (nextMs != 0 && !TimeReached(now, nextMs))
        return;
    nextMs = now + kUpdateMs;

    // The cursor waits: a ping never came.
    for (auto it = Memory().pendings.begin(); it != Memory().pendings.end();)
    {
        if (!Due(now, it->second.deadline))
        {
            ++it;
            continue;
        }
        PendingRequest const pending = it->second;
        Player* commander = ObjectAccessor::FindPlayerByLowGUID(pending.commander);
        if (commander && commander->GetSession())
        {
            h3::Words const args = WordsOf(pending);
            ChatHandler handler(commander->GetSession());
            h3::Fail(&handler, args, "cmd", "timeout",
                "no minimap ping within " + std::to_string(kCursorWaitSeconds) + " s: nothing was moved");
        }
        sLog.outString("TortoiseBots place: the cursor request timed out after %d s", kCursorWaitSeconds);
        it = Memory().pendings.erase(it);
    }

    // The hold: walk a placed bot that lost its slot back, and forget the rows that can no longer be served (releasing each bot).
    for (auto it = Memory().holds.begin(); it != Memory().holds.end();)
    {
        uint32 const key = it->first;
        Player* commander = ObjectAccessor::FindPlayerByLowGUID(key);
        std::vector<std::pair<HoldRow, RowEnd>> ended;
        for (HoldRow& row : it->second.rows)
        {
            Player* bot = ObjectAccessor::FindPlayer(BotOf(row.bot));
            RowEnd end = RowEnd::None;
            if (!bot || !bot->IsInWorld())
                end = RowEnd::Dead;      // gone: the row can never be served again
            else if (!bot->IsAlive())
                end = RowEnd::Dead;
            else if (bot->IsBeingTeleported())
                end = RowEnd::Teleport;
            else if (h3::CommanderGuid(bot->GetGUID()) != (commander ? commander->GetGUID() : ObjectGuid()))
                end = RowEnd::Commander;
            else if (bot->GetMapId() != row.map || bot->GetInstanceId() != row.instance)
                end = RowEnd::OtherMap;
            else if (InRun(bot))
                end = RowEnd::Run;       // a run drives this bot: its hold is over
            else
            {
                // The row's hold is the `stay` its `come` wrote. A newer order (`.tb order ... follow`, `.tb action free`) or a whisper shortcut
                // (`follow`, `free`, ...: the engine's chat path, which advances no order number) leaves the bot off its stay: it answers to
                // something else now, and walking it back would fight that. The row's own `come` still waiting in the queue is no loss: its stay
                // has not been written yet.
                h3::OrderStatus const last = h3::LastOrder(bot->GetGUID());
                PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
                bool const ownComeQueued = last.seq == row.seq && last.verb == "come" && last.result == "queued";
                if (RowStayGone(ai != nullptr, ai && ai->HasStrategy("stay", BotState::BOT_STATE_NON_COMBAT), ownComeQueued))
                    end = RowEnd::StayGone;
            }
            if (end != RowEnd::None)
            {
                ended.emplace_back(row, end);
                continue;
            }
            float const dx = bot->GetPositionX() - row.x, dy = bot->GetPositionY() - row.y;
            float const distance = std::sqrt(dx * dx + dy * dy);
            if (RowDisengageEnds(row, distance, bot->IsInCombat(), bot->movespline->Finalized(), now))
            {
                // The fall back has landed, or the bot stopped short in a fight (a blocked spot), or the walk outlived its cap: the
                // emergency passivity ends, the bot fights what follows it (and still returns to the spot after the fight). A newer order
                // may own the bot by now (its number no longer matches): only the row's OWN passive is released (F1), so a `flee` or `hold`
                // ordered on the way keeps its own; only the matching `come` still owns the combat-state stay. The passive is released by the
                // order that OWNS it: a walk back re-queued the movement `come` under a new number, and asking with that number found no
                // record, reported "nothing to release" and cleared the flag with the bot still passive (#316).
                bool const passiveGone = h3::ReleasePlacePassive(bot, PassiveOwner(row));
                h3::ReleasePlaceDisengage(bot, row.seq);
                DisengageSettled(row, passiveGone);
            }
            bool const idle = !bot->IsInCombat() && !bot->IsNonMeleeSpellCast(true, false, true) && bot->movespline->Finalized();
            if (distance > kSlotToleranceYd && idle && Due(now, row.walkMs + kWalkBackEveryMs))
            {
                // The slot may have become unreachable (a knock to another floor, water where it was dry): the same move rule the
                // placement used judges the walk back, and a few failed checks give the row up.
                PathVerdict const verdict = VerdictFor(bot, { row.x, row.y }, row.z);
                if (verdict != PathVerdict::Ok)
                {
                    ++row.walkFails;
                    if (WalkGiveUp(row.walkFails))
                    {
                        sLog.outString("TortoiseBots place: giving up %s's slot after %u failed walk-backs (%s)",
                            bot->GetName(), row.walkFails, PathVerdictWord(verdict));
                        ended.emplace_back(row, RowEnd::Unreachable);
                        continue;
                    }
                    row.walkMs = now;
                }
                else
                {
                    uint32 seq = 0;
                    std::string code, detail;
                    if (QueueCome(bot, row.x, row.y, row.z, row.placed, row.disengage, PassiveOwner(row), seq, code, detail))
                        AfterWalkBack(row, seq, now);
                }
            }
        }
        if (ended.empty())
        {
            ++it;
            continue;
        }
        std::vector<GuidKey> const dropKeys = [&]
        {
            std::vector<GuidKey> keys;
            for (auto const& [row, end] : ended)
                keys.push_back(row.bot);
            return keys;
        }();
        DropRows(Memory(), key, [&](HoldRow const& row)
        {
            return std::any_of(dropKeys.begin(), dropKeys.end(), [&](GuidKey const& guid) { return guid == row.bot; });
        });
        for (auto const& [row, end] : ended)
        {
            Player* bot = ObjectAccessor::FindPlayer(BotOf(row.bot));
            bool const ours = commander && bot && bot->IsInWorld() && cmd::IsCommandedBy(commander, bot);
            ReleaseRow(row, ReleaseOn(end, ours));
        }
        it = Memory().holds.upper_bound(key);   // the hold may be gone; continue with the next commander either way
    }
}

HoldView ViewFor(ObjectGuid commander)
{
    HoldView view;
    uint32 const key = commander.GetCounter();
    auto const hold = Memory().holds.find(key);
    if (hold != Memory().holds.end() && !hold->second.rows.empty())
    {
        view.any = true;
        view.fallback = hold->second.fallback;
        for (HoldRow const& row : hold->second.rows)
            view.slots.push_back({ row.bot, { row.x, row.y, row.z } });
    }
    auto const rally = Memory().rallies.find(key);
    if (rally != Memory().rallies.end() && rally->second.set)
    {
        view.rallySet = true;
        view.x = rally->second.x;
        view.y = rally->second.y;
        view.z = rally->second.z;
        view.map = rally->second.map;
        view.inst = rally->second.instance;
    }
    return view;
}
}
}

#else   // no engine: nothing to place, and `.tb cmd` already answers engine-not-built for the whole command

namespace TortoiseBots
{
namespace place
{
bool Handle(ChatHandler*, h3::Words const&) { return false; }
bool HandleChat(Player*, std::string const&, Group*) { return false; }
bool IsPlaceText(std::string const&) { return false; }
void CancelPending(ObjectGuid, ChatHandler*) {}
void NoteLogout(Player*) {}
void OnPacket(WorldSession*, WorldPacket&) {}
void NoteOrdered(ObjectGuid, std::string const&) {}
void Update() {}
HoldView ViewFor(ObjectGuid) { return {}; }
}
}

#endif
