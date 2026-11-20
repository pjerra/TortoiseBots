#include "host/TbHostTransport.h"

#include "GameObject.h"
#include "Log.h"
#include "Map.h"
#include "MotionMaster.h"
#include "Player.h"
#include "Transport.h"
#include "TransportMgr.h"
#include "VehicleDefines.h"
#include "playerbot/policy/travel/TransportWaitPolicy.h"

#include <cmath>
#include <unordered_map>
#include <vector>

namespace tbhost
{
namespace ride
{
namespace
{

constexpr float kMaxBoardReachYd = 35.0f;
constexpr float kLeaveDockMinDistYd = 50.0f;
constexpr uint32_t kGiveUpMarginMs = 15000;

struct RideState
{
    bool active = false;
    bool boarded = false;
    bool leftStartDock = false;
    uint32_t entry = 0;
    ObjectGuid transportGuid;
    uint32_t startMap = 0;
    float startX = 0.0f;
    float startY = 0.0f;
    float startZ = 0.0f;
    uint32_t waitedMs = 0;
};

std::unordered_map<ObjectGuid, RideState>& RideStates()
{
    static std::unordered_map<ObjectGuid, RideState> states;
    return states;
}

float Dist2D(float x1, float y1, float x2, float y2)
{
    float const dx = x1 - x2;
    float const dy = y1 - y2;
    return std::sqrt(dx * dx + dy * dy);
}

struct FoundTransport
{
    TransportBase* base = nullptr;
    WorldObject* obj = nullptr;
    ObjectGuid guid;
    bool stopped = false;
    uint32_t totalPathTimeMs = 0;
};

// Finds a transport by GUID on the bot's current map (MO_TRANSPORT or type-11 GAMEOBJECT_TYPE_TRANSPORT).
FoundTransport FindByGuid(Map* map, ObjectGuid const& guid)
{
    FoundTransport out;
    if (!map || guid.IsEmpty())
        return out;

    if (Transport* t = map->GetTransport(guid))
    {
        out.base = t;
        out.obj = t;
        out.guid = t->GetGUID();
        out.stopped = t->IsStopped();
        out.totalPathTimeMs = t->GetTransportPeriod();
        return out;
    }

    if (GameObject* go = map->GetGameObject(guid))
    {
        if (TransportBase* tb = go->ToTransportBase())
        {
            out.base = tb;
            out.obj = go;
            out.guid = go->GetGUID();
            out.stopped = go->HasDynamicFlag(GO_DYNFLAG_LO_STOPPED);
            if (TransportAnimation const* anim = sTransportMgr->GetTransportAnimInfo(go->GetEntry()))
                out.totalPathTimeMs = anim->TotalTime;
            return out;
        }
    }
    return out;
}

// Finds a transport by entry on the bot's current map.
FoundTransport FindByEntry(Player const* bot, Map* map, uint32_t entry)
{
    FoundTransport out;
    if (!bot || !map || !entry)
        return out;

    for (Transport* t : map->GetTransports())
    {
        if (!t || t->GetEntry() != entry)
            continue;
        out.base = t;
        out.obj = t;
        out.guid = t->GetGUID();
        out.stopped = t->IsStopped();
        out.totalPathTimeMs = t->GetTransportPeriod();
        return out;
    }

    std::vector<GameObject*> gos;
    bot->GetGameObjectListWithEntryInGrid(gos, entry, 100.0f);
    for (GameObject* go : gos)
    {
        if (!go || go->GetEntry() != entry)
            continue;
        if (TransportBase* tb = go->ToTransportBase())
        {
            out.base = tb;
            out.obj = go;
            out.guid = go->GetGUID();
            out.stopped = go->HasDynamicFlag(GO_DYNFLAG_LO_STOPPED);
            if (TransportAnimation const* anim = sTransportMgr->GetTransportAnimInfo(go->GetEntry()))
                out.totalPathTimeMs = anim->TotalTime;
            return out;
        }
    }
    return out;
}

bool CheckClassicRoute(uint32_t entry)
{
    if (TransportTemplate const* tmpl = sTransportMgr->GetTransportTemplate(entry))
    {
        std::vector<uint32_t> legMaps;
        legMaps.reserve(tmpl->PathLegs.size());
        for (TransportPathLeg const& leg : tmpl->PathLegs)
            legMaps.push_back(leg.MapId);
        return ai::IsAllowedClassicTransportRoute(entry, legMaps, {}, /*allowTramMap=*/true);
    }

    // Type-11 transports (e.g. Deeprun Tram on map 369) use TransportAnimation rather than TransportTemplate.
    return sTransportMgr->GetTransportAnimInfo(entry) != nullptr;
}

} // namespace

bool StartRide(Player* bot, uint32_t transportEntry, std::string* failCode)
{
    if (failCode)
        failCode->clear();
    if (!bot || !transportEntry)
    {
        if (failCode)
            *failCode = "bad-args";
        return false;
    }

    Map* map = bot->FindMap();
    if (!map)
    {
        if (failCode)
            *failCode = "no-map";
        return false;
    }

    if (!CheckClassicRoute(transportEntry))
    {
        if (failCode)
            *failCode = "non-classic-route";
        TC_LOG_INFO("tortoisebots", "[W5] ride-refused bot={} entry={} code=non-classic-route map={}",
            bot->GetName(), transportEntry, map->GetId());
        return false;
    }

    RideState& rs = RideStates()[bot->GetGUID()];
    rs = RideState{};
    rs.active = true;
    rs.entry = transportEntry;
    rs.startMap = map->GetId();
    rs.startX = bot->GetPositionX();
    rs.startY = bot->GetPositionY();
    rs.startZ = bot->GetPositionZ();

    FoundTransport const found = FindByEntry(bot, map, transportEntry);
    if (found.obj)
        rs.transportGuid = found.guid;

    TC_LOG_INFO("tortoisebots", "[W5] ride-wait bot={} entry={} guid={} map={}",
        bot->GetName(), transportEntry, rs.transportGuid.ToString(), map->GetId());
    return true;
}

void TickRide(Player* bot, uint32_t diffMs)
{
    if (!bot)
        return;

    auto it = RideStates().find(bot->GetGUID());
    if (it == RideStates().end() || !it->second.active)
        return;

    // Mid-teleport (e.g. far-teleport across continents): FindMap() is null, do nothing this tick.
    Map* map = bot->FindMap();
    if (!map)
        return;

    RideState& rs = it->second;
    rs.waitedMs += diffMs;

    FoundTransport found = FindByGuid(map, rs.transportGuid);
    if (!found.obj && !rs.boarded)
    {
        found = FindByEntry(bot, map, rs.entry);
        if (found.obj)
            rs.transportGuid = found.guid;
    }

    uint32_t totalPathTimeMs = found.totalPathTimeMs;
    if (!totalPathTimeMs)
    {
        if (TransportTemplate const* tmpl = sTransportMgr->GetTransportTemplate(rs.entry))
            totalPathTimeMs = tmpl->TotalPathTime;
    }

    bool const currentlyOnBoard = bot->GetTransport() != nullptr &&
        (!rs.transportGuid.IsEmpty() ? bot->GetTransport()->GetTransportGUID() == rs.transportGuid : true);

    if (rs.boarded && found.obj)
    {
        if (map->GetId() != rs.startMap || Dist2D(found.obj->GetPositionX(), found.obj->GetPositionY(), rs.startX, rs.startY) > kLeaveDockMinDistYd)
            rs.leftStartDock = true;
    }

    bool const atDestinationDock = rs.boarded && rs.leftStartDock && found.obj && found.stopped &&
        (map->GetId() != rs.startMap || Dist2D(found.obj->GetPositionX(), found.obj->GetPositionY(), rs.startX, rs.startY) > kLeaveDockMinDistYd);

    float const distToDeck = found.obj ? bot->GetDistance(found.obj) : 9999.0f;

    ai::TransportWaitInput in;
    in.mapReady = true;
    in.onBoard = rs.boarded && currentlyOnBoard;
    in.transportFound = found.obj != nullptr;
    in.transportStopped = found.stopped;
    in.atDestinationDock = atDestinationDock;
    in.distanceToDeck = distToDeck;
    in.maxBoardRange = kMaxBoardReachYd;
    in.waitedMs = rs.waitedMs;
    in.totalPathTimeMs = totalPathTimeMs;
    in.marginMs = kGiveUpMarginMs;

    switch (ai::DecideTransportWait(in))
    {
        case ai::TransportWaitStep::Board:
        {
            if (found.base && found.obj)
            {
                // Walk onto the deck and attach as passenger (core MovementHandler.cpp:529-533).
                bot->GetMotionMaster()->MovePoint(0, found.obj->GetPositionX(), found.obj->GetPositionY(), found.obj->GetPositionZ());
                Position const offset = found.base->GetPositionOffsetTo(found.obj->GetPosition());
                found.base->AddPassenger(bot, offset);
                rs.boarded = true;
                TC_LOG_INFO("tortoisebots", "[W5] ride-board bot={} entry={} guid={} map={}",
                    bot->GetName(), rs.entry, rs.transportGuid.ToString(), map->GetId());
            }
            break;
        }
        case ai::TransportWaitStep::Leave:
        {
            if (TransportBase* tb = bot->GetTransport())
                tb->RemovePassenger(bot);
            rs.active = false;
            TC_LOG_INFO("tortoisebots", "[W5] ride-leave bot={} entry={} guid={} map={}",
                bot->GetName(), rs.entry, rs.transportGuid.ToString(), map->GetId());
            break;
        }
        case ai::TransportWaitStep::GiveUp:
        {
            rs.active = false;
            TC_LOG_INFO("tortoisebots", "[W5] ride-giveup bot={} entry={} waited_ms={} total_ms={} map={}",
                bot->GetName(), rs.entry, rs.waitedMs, totalPathTimeMs, map->GetId());
            break;
        }
        case ai::TransportWaitStep::Wait:
        default:
            break;
    }
}

bool IsOnBoard(Player const* bot)
{
    return bot && bot->GetTransport() != nullptr;
}

ObjectGuid CurrentTransportGuid(Player const* bot)
{
    if (!bot || !bot->GetTransport())
        return ObjectGuid::Empty;
    return bot->GetTransport()->GetTransportGUID();
}

} // namespace ride
} // namespace tbhost
