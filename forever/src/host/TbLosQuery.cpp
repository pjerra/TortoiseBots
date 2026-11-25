// `.tb los` (#308): see host/TbLosQuery.h for the command, the line format and the sight call it mirrors. The rules
// (argument forms, refusal codes, rate bucket, access rule) are core-free in host/TbLosQueryPure.h and tested by
// forever/tools/test_los_query.cpp; this file reads the world and sends the one line.
#include "host/TbLosQuery.h"
#include "host/TbH3.h"
#include "host/TbLosQueryPure.h"

#include "Chat.h"
#include "Player.h"

#include <chrono>
#include <map>
#include <string>
#include <vector>

#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1

#include "compat/botpch_forever.h"

#include "Map.h"
#include "MapManager.h"
#include "ObjectAccessor.h"
#include "SharedDefines.h"
#include "Unit.h"

#include "compat/TbCompat_Unit.h"

namespace TortoiseBots
{
namespace losq
{
using namespace h3;

namespace
{
// The callers' token buckets. The world thread is the only writer (console commands and chat commands both run there),
// so the table is plain data; an entry idle for a second and full again is dropped on the next query.
std::map<ObjectGuid, RateBucket>& Buckets()
{
    static std::map<ObjectGuid, RateBucket> buckets;
    return buckets;
}

std::uint64_t NowMs()
{
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

// One side after resolution. A position side keeps its Side pointer for the coordinates; a unit side carries the unit.
struct Resolved
{
    Side const* side = nullptr;
    SideState state;
    AccessSide access;
    Unit* unit = nullptr;
    std::string display;        // the resolved name, or the typed word when it did not resolve
    std::string typed;          // always the word as typed (`target`, a name, a guid, x,y,z): what a hidden answer shows
};

Unit* LookupUnit(ObjectGuid guid, Player* sender, Unit* other)
{
    if (guid.IsEmpty())
        return nullptr;
    // A player is found in the whole world; a creature only on a map we can name, so the caller's own map is tried first
    // and then the other side's.
    if (Player* player = ObjectAccessor::FindPlayer(guid))
        return player;
    if (sender && sender->IsInWorld())
        if (Unit* unit = ObjectAccessor::GetUnit(*sender, guid))
            return unit;
    if (other && other->IsInWorld())
        if (Unit* unit = ObjectAccessor::GetUnit(*other, guid))
            return unit;
    return nullptr;
}

bool Resolve(Side const& side, Player* sender, Unit* other, Resolved& out)
{
    out = Resolved();
    out.side = &side;
    out.display = DisplaySide(side, "");
    out.typed = out.display;
    if (side.kind == SideKind::Position)
    {
        // The map is filled in once both sides are known: the other side's map, or the `map=` word.
        out.state.isPosition = true;
        out.state.found = true;
        return true;
    }

    Unit* unit = nullptr;
    switch (side.kind)
    {
        case SideKind::Name:
            unit = FindOnline(side.word);
            break;
        case SideKind::Target:
            if (sender)
                unit = LookupUnit(sender->GetTarget(), sender, other);
            break;
        case SideKind::Guid:
        {
            ObjectGuid guid;
            guid.SetRawValue(side.guidHigh, side.guidLow);
            unit = LookupUnit(guid, sender, other);
            break;
        }
        default:
            break;
    }
    if (!unit)
        return false;

    out.unit = unit;
    out.state.found = true;
    out.state.inWorld = unit->IsInWorld();
    out.state.mapKnown = true;
    out.state.mapId = unit->GetMapId();
    out.state.instanceKnown = true;
    out.state.instanceId = unit->GetInstanceId();
    out.display = unit->GetName();
    return true;
}

// The loaded copy of a map: the lowest loaded instance (0 for a continent; a dungeon at its first instance id). A
// position-position query cannot name an instance, so it is answered on this copy; a position with a unit side inherits
// that unit's own instance instead.
Map* FindLoadedMap(std::uint32_t mapId)
{
    return sMapMgr.FindMap(mapId, FirstInstanceIdCompat(mapId));
}

// Who may read this side: the shared `.tb run` pair, exactly as `.tb run start` checks its leader. A creature is
// nobody's unit (no gate), a position has no unit at all.
void FillAccess(Resolved& resolved, ChatHandler* handler, Player* sender)
{
    if (!resolved.unit)
        return;
    Player* player = resolved.unit->ToPlayer();
    if (!player)
        return;
    resolved.access.isUnit = true;
    resolved.access.isPlayerCharacter = true;
    resolved.access.isOwnCharacter = sender && player == sender;
    resolved.access.isHostBot = IsHostBot(player);
    if (resolved.access.isHostBot && !resolved.access.isOwnCharacter)
    {
        std::string code, detail;
        resolved.access.botAccessAllowed = CheckRunObserverAccess(handler, player, code, detail) ||
                                           CheckPartyLeadAccess(handler, player, code, detail);
    }
}

// The line: `los` then `a= b= map= los= dist= same_map= in_world= [reason=]`, in that order (#308; a stable interface
// for tools). A key whose fact is unknown is empty; `los` is 0 and `dist` empty whenever the query was refused.
void SendAnswer(ChatHandler* handler, Words const& args, Resolved const& a, Resolved const& b, Reason reason)
{
    // A `no-access` answer shows only what the caller typed: no resolved name, map, same_map or in_world, so another
    // player's character cannot be told from a name that resolved to nothing.
    bool const hideWorld = AnswerHidesWorld(reason);
    bool los = false;
    std::string distance;
    if (reason == Reason::None)
    {
        if (a.unit && b.unit)
        {
            // The bots' own attack-target check, same core call and flags.
            los = IsWithinLOSInMapCompat(a.unit, b.unit, true);
            distance = FormatDist(a.unit->GetDistance2d(b.unit));
        }
        else if (a.unit && b.side)
        {
            // A position has no eye of its own: the ray is cast from the unit side, so its height offsets apply.
            los = IsWithinLOSCompat(a.unit, b.side->pos.x, b.side->pos.y, b.side->pos.z, true);
            distance = FormatDist(a.unit->GetDistance2d(b.side->pos.x, b.side->pos.y));
        }
        else if (a.side && b.unit)
        {
            los = IsWithinLOSCompat(b.unit, a.side->pos.x, a.side->pos.y, a.side->pos.z, true);
            distance = FormatDist(b.unit->GetDistance2d(a.side->pos.x, a.side->pos.y));
        }
        else if (a.side && b.side)
        {
            if (Map* map = FindLoadedMap(a.state.mapId))
                los = IsInLineOfSightCompat(map, a.side->pos.x, a.side->pos.y, a.side->pos.z,
                                            b.side->pos.x, b.side->pos.y, b.side->pos.z, true);
            distance = FormatDist(HorizontalDistance(a.side->pos, b.side->pos));
        }
    }

    Rec rec("los");
    auto const value = [&](char const* key, Resolved const& side)
    {
        if (side.state.isPosition)
            rec.Coord(key, side.side->pos.x, side.side->pos.y, side.side->pos.z);
        else
            rec.S(key, hideWorld ? side.typed : side.display);
    };
    value("a", a);
    value("b", b);
    if (a.state.mapKnown && !hideWorld)
        rec.N("map", static_cast<int64>(a.state.mapId));
    else
        rec.S("map", "");
    rec.N("los", los ? 1 : 0);
    rec.S("dist", distance);
    if (a.state.mapKnown && b.state.mapKnown && !hideWorld)
        rec.N("same_map", SamePlace(a.state, b.state) ? 1 : 0);
    else
        rec.S("same_map", "");
    if (a.state.found && b.state.found && !hideWorld)
        rec.N("in_world", a.state.inWorld && b.state.inWorld ? 1 : 0);
    else
        rec.S("in_world", "");
    if (reason != Reason::None)
        rec.S("reason", ReasonCode(reason));
    Reply(handler, args, "los").Single(rec);
}
}

bool HandleLos(ChatHandler* handler, Trinity::ChatCommands::Tail tail)
{
    Words const args = Split(tail);
    if (!RequireBuilt(handler, args, "los"))
        return false;

    Player* sender = SenderPlayer(handler);
    WorldSession* session = handler->GetSession();
    bool const isGm = !session || session->GetSecurity() >= SEC_GAMEMASTER;
    ObjectGuid const caller = sender ? sender->GetGUID() : ObjectGuid::Empty;

    QueryResult const parsed = ParseQuery(args.w);
    Query const& query = parsed.query;

    // Rate: five a second per caller, before anything looks at the world. An entry idle for a second and full again is
    // dropped as we go, so the table only holds callers that queried within the last second.
    std::uint64_t const now = NowMs();
    for (auto it = Buckets().begin(); it != Buckets().end();)
    {
        if (!(it->first == caller) && it->second.Idle(now))
            it = Buckets().erase(it);
        else
            ++it;
    }
    bool const overRate = !Buckets()[caller].Take(now);

    // `target` is the caller's selection: the console has none, and a player with nothing selected gets a bad argument.
    bool const targetMissing = (query.a.kind == SideKind::Target && (!sender || sender->GetTarget().IsEmpty())) ||
                               (query.b.kind == SideKind::Target && (!sender || sender->GetTarget().IsEmpty()));

    Resolved a, b;
    Reason reason = Reason::None;
    if (args.badToken || targetMissing)
    {
        reason = Reason::BadArgument;
        a.display = args.w.empty() ? std::string() : args.w[0];
        b.display = args.w.size() > 1 ? args.w[1] : std::string();
    }
    else if (parsed.reason != Reason::None)
    {
        reason = parsed.reason;
        a.display = args.w.empty() ? std::string() : args.w[0];
        b.display = args.w.size() > 1 ? args.w[1] : std::string();
    }
    else if (overRate)
    {
        reason = Reason::Rate;
        a.display = DisplaySide(query.a, "");
        b.display = DisplaySide(query.b, "");
    }
    else
    {
        bool const okA = Resolve(query.a, sender, nullptr, a);
        bool const okB = Resolve(query.b, sender, a.unit, b);
        // A creature guid on the other side's map: the second pass names that map.
        if (!okA && okB && b.unit)
            Resolve(query.a, sender, b.unit, a);

        // Positions sit on the other side's map and in its instance, or on `map=` (a position-position query: ParseQuery
        // required it, and that map's first loaded copy is the one answered on).
        if (query.hasMap)
        {
            // ParseQuery only lets `map=` through for two positions, so both sides take it.
            a.state.mapKnown = true;
            b.state.mapKnown = true;
            a.state.mapId = query.mapId;
            b.state.mapId = query.mapId;
        }
        else
        {
            if (a.state.isPosition && b.state.mapKnown)
            {
                a.state.mapKnown = true;
                a.state.mapId = b.state.mapId;
                a.state.instanceKnown = b.state.instanceKnown;
                a.state.instanceId = b.state.instanceId;
            }
            if (b.state.isPosition && a.state.mapKnown)
            {
                b.state.mapKnown = true;
                b.state.mapId = a.state.mapId;
                b.state.instanceKnown = a.state.instanceKnown;
                b.state.instanceId = a.state.instanceId;
            }
        }
        // A position's world state is its map being loaded; a position with no instance of its own takes the loaded copy's.
        auto placePosition = [](Resolved& side)
        {
            if (!side.state.isPosition)
                return;
            Map* map = FindLoadedMap(side.state.mapId);
            side.state.inWorld = map != nullptr;
            if (map && !side.state.instanceKnown)
            {
                side.state.instanceKnown = true;
                side.state.instanceId = map->GetInstanceId();
            }
        };
        placePosition(a);
        placePosition(b);

        // A GameMaster passes the access rule on everything, and the observer check logs an INFO line for a GM reading a
        // foreign bot: skipping it keeps a polling tool from flooding the log.
        if (!isGm)
        {
            FillAccess(a, handler, sender);
            FillAccess(b, handler, sender);
        }
        Facts const facts{ a.state, b.state, AccessAllowed(isGm, { a.access, b.access }) };
        // `SeenByCaller`: below GameMaster, a foreign character and a name that resolved to nothing answer one code.
        reason = SeenByCaller(isGm, Decide(facts));
    }

    SendAnswer(handler, args, a, b, reason);
    return true;
}
}
}

#else

namespace TortoiseBots
{
namespace losq
{
bool HandleLos(ChatHandler* handler, Trinity::ChatCommands::Tail args) { return h3::NotBuilt(handler, args, "los"); }
}
}

#endif
