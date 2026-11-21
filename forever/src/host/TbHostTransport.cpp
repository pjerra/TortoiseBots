#include "host/TbHostTransport.h"

#include "DB2Structure.h"
#include "GameObject.h"
#include "GameObjectModel.h"
#include "GameTime.h"
#include "Log.h"
#include "Map.h"
#include "MotionMaster.h"
#include "PathGenerator.h"
#include "Player.h"
#include "Transport.h"
#include "TransportMgr.h"
#include "VMapDefinitions.h"
#include "VehicleDefines.h"
#include "playerbot/policy/travel/TransportWaitPolicy.h"

#include <G3D/Ray.h>
#include <G3D/Vector3.h>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <vector>

namespace tbhost
{
namespace ride
{
namespace
{

constexpr float kMaxBoardReachYd = 12.0f;   // 2D from the transport's centre: the bot stands on the deck
constexpr float kMaxDeckZDiffYd = 2.5f;     // vertical band around the transport's deck surface at (botX, botY)
constexpr float kApproachReachYd = 60.0f;
constexpr float kLeaveDockMinDistYd = 100.0f;
constexpr uint32_t kGiveUpMarginMs = 15000;
constexpr uint32_t kMissingTimeoutMs = 30000;
constexpr uint32_t kRescanIntervalMs = 3000;

struct RideState
{
    bool active = false;
    bool boarded = false;
    bool leftStartDock = false;
    bool offDeckRefused = false;
    bool approachPathFailed = false;
    bool arrived = false;
    uint32_t entry = 0;
    uint32_t orderSeq = 0;
    ObjectGuid transportGuid;
    uint32_t startMap = 0;
    float startX = 0.0f;
    float startY = 0.0f;
    float startZ = 0.0f;
    uint32_t waitedMs = 0;
    uint32_t nextRescanMs = 0;
    std::string outcome;
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

bool IsMoTransportStopped(Transport const* t)
{
    if (!t)
        return false;
    if (t->IsStopped())
        return true;

    // Regular ships/zeppelins (allowstopping=0) never set GO_DYNFLAG_LO_STOPPED;
    // their dock pauses are reported by TransportTemplate::ComputePosition.
    if (TransportTemplate const* tmpl = sTransportMgr->GetTransportTemplate(t->GetEntry()))
    {
        TransportMovementState moveState = TransportMovementState::Moving;
        if (tmpl->ComputePosition(t->GetTimer(), &moveState, nullptr))
            return moveState != TransportMovementState::Moving;
    }
    return false;
}

bool IsGoTransportStopped(GameObject const* go, uint32_t* outTotalTimeMs = nullptr)
{
    if (!go)
        return false;

    if (TransportAnimation const* anim = sTransportMgr->GetTransportAnimInfo(go->GetEntry()))
    {
        if (outTotalTimeMs)
            *outTotalTimeMs = anim->TotalTime;

        if (go->HasDynamicFlag(GO_DYNFLAG_LO_STOPPED))
            return true;

        if (!go->GetPauseTimes().empty())
            return GameTime::GetGameTimeMS() >= static_cast<uint32_t>(*go->m_gameObjectData->Level);

        if (anim->TotalTime > 0)
        {
            uint32_t progressMs = 0;
            if (Optional<float> pct = go->GetPathProgressForClient())
                progressMs = static_cast<uint32_t>(*pct * static_cast<float>(anim->TotalTime)) % anim->TotalTime;
            else
                progressMs = GameTime::GetGameTimeMS() % anim->TotalTime;

            TransportAnimationEntry const* prev = anim->GetPrevAnimNode(progressMs);
            TransportAnimationEntry const* next = anim->GetNextAnimNode(progressMs);
            if (prev && next)
                return ai::IsTransportAnimPaused(prev->Pos.X, prev->Pos.Y, prev->Pos.Z, next->Pos.X, next->Pos.Y, next->Pos.Z);
        }
    }

    return go->HasDynamicFlag(GO_DYNFLAG_LO_STOPPED);
}

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
        out.stopped = IsMoTransportStopped(t);
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
            out.stopped = IsGoTransportStopped(go, &out.totalPathTimeMs);
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
        out.stopped = IsMoTransportStopped(t);
        out.totalPathTimeMs = t->GetTransportPeriod();
        return out;
    }

    // Type-11 transports (e.g. Deeprun Tram) are active GameObjects that roam across the whole map,
    // so check the map's spawn store first rather than a 100 yd grid search around the bot.
    GameObject* bestGo = nullptr;
    float bestDist = std::numeric_limits<float>::max();
    for (auto const& pair : map->GetGameObjectBySpawnIdStore())
    {
        GameObject* go = pair.second;
        if (!go || !go->IsInWorld() || go->GetEntry() != entry || !go->ToTransportBase())
            continue;
        float const d = bot->GetDistance(go);
        if (d < bestDist)
        {
            bestDist = d;
            bestGo = go;
        }
    }

    if (!bestGo)
    {
        std::vector<GameObject*> gos;
        bot->GetGameObjectListWithEntryInGrid(gos, entry, 250.0f);
        for (GameObject* go : gos)
        {
            if (!go || go->GetEntry() != entry || !go->ToTransportBase())
                continue;
            float const d = bot->GetDistance(go);
            if (d < bestDist)
            {
                bestDist = d;
                bestGo = go;
            }
        }
    }

    if (bestGo)
    {
        out.base = bestGo->ToTransportBase();
        out.obj = bestGo;
        out.guid = bestGo->GetGUID();
        out.stopped = IsGoTransportStopped(bestGo, &out.totalPathTimeMs);
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

bool SampleTransportDeckZ(Player const* bot, WorldObject const* transportObj, float& outDeckZ)
{
    if (!bot || !transportObj)
        return false;

    if (GameObject const* go = transportObj->ToGameObject())
    {
        if (go->m_model)
        {
            float const rayStartZ = bot->GetPositionZ() + 2.0f;
            float maxDist = 6.0f;
            G3D::Ray const ray(G3D::Vector3(bot->GetPositionX(), bot->GetPositionY(), rayStartZ), G3D::Vector3(0.0f, 0.0f, -1.0f));
            if (go->m_model->IntersectRay(ray, maxDist, /*stopAtFirstHit=*/false, bot->GetPhaseShift(), VMAP::ModelIgnoreFlags::Nothing))
            {
                outDeckZ = rayStartZ - maxDist;
                return true;
            }
            return false;
        }
    }

    outDeckZ = transportObj->GetPositionZ();
    return true;
}

} // namespace

bool StartRide(Player* bot, uint32_t transportEntry, std::string* stateOut, uint32_t orderSeq)
{
    if (stateOut)
        stateOut->clear();
    if (!bot || !transportEntry)
    {
        if (stateOut)
            *stateOut = "bad-args";
        return false;
    }

    Map* map = bot->FindMap();
    if (!map)
    {
        if (stateOut)
            *stateOut = "no-map";
        return false;
    }

    if (!CheckClassicRoute(transportEntry))
    {
        if (stateOut)
            *stateOut = "non-classic-route";
        TC_LOG_INFO("tortoisebots", "[W5] ride-refused bot={} entry={} code=non-classic-route map={}",
            bot->GetName(), transportEntry, map->GetId());
        return false;
    }

    RideState& rs = RideStates()[bot->GetGUID()];
    rs = RideState{};
    rs.active = true;
    rs.entry = transportEntry;
    rs.orderSeq = orderSeq;
    rs.startMap = map->GetId();
    rs.startX = bot->GetPositionX();
    rs.startY = bot->GetPositionY();
    rs.startZ = bot->GetPositionZ();
    rs.nextRescanMs = kRescanIntervalMs;

    FoundTransport const found = FindByEntry(bot, map, transportEntry);
    if (found.obj)
        rs.transportGuid = found.guid;

    rs.outcome = ai::RideOrderResult(true, false, false, false, "");
    if (stateOut)
        *stateOut = rs.outcome;

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
    if (ai::ShouldRescanTransport(found.obj != nullptr, rs.boarded, rs.waitedMs, rs.nextRescanMs))
    {
        rs.nextRescanMs = rs.waitedMs + kRescanIntervalMs;
        found = FindByEntry(bot, map, rs.entry);
        if (found.obj)
            rs.transportGuid = found.guid;
    }

    uint32_t totalPathTimeMs = found.totalPathTimeMs;
    if (!totalPathTimeMs)
    {
        if (TransportTemplate const* tmpl = sTransportMgr->GetTransportTemplate(rs.entry))
            totalPathTimeMs = tmpl->TotalPathTime;
        else if (TransportAnimation const* anim = sTransportMgr->GetTransportAnimInfo(rs.entry))
            totalPathTimeMs = anim->TotalTime;
    }

    bool const currentlyOnBoard = bot->GetTransport() != nullptr &&
        (!rs.transportGuid.IsEmpty() ? bot->GetTransport()->GetTransportGUID() == rs.transportGuid : true);

    if (rs.boarded && found.obj)
    {
        float const distFromStart = Dist2D(found.obj->GetPositionX(), found.obj->GetPositionY(), rs.startX, rs.startY);
        if (ai::HasLeftStartDock(map->GetId() == rs.startMap, found.stopped, distFromStart, kLeaveDockMinDistYd))
            rs.leftStartDock = true;
    }

    bool const atDestinationDock = rs.boarded && rs.leftStartDock && found.obj && found.stopped &&
        (map->GetId() != rs.startMap || Dist2D(found.obj->GetPositionX(), found.obj->GetPositionY(), rs.startX, rs.startY) > kLeaveDockMinDistYd);

    float const distToDeck = found.obj ? Dist2D(bot->GetPositionX(), bot->GetPositionY(), found.obj->GetPositionX(), found.obj->GetPositionY()) : 9999.0f;

    bool hasDeckZ = false;
    float deckZ = 0.0f;
    if (!rs.boarded && found.obj && found.stopped && !atDestinationDock && distToDeck <= kMaxBoardReachYd)
    {
        hasDeckZ = SampleTransportDeckZ(bot, found.obj, deckZ);
        if (!ai::IsOnTransportDeck(hasDeckZ, bot->GetPositionZ(), deckZ, kMaxDeckZDiffYd))
        {
            if (!rs.offDeckRefused)
            {
                rs.offDeckRefused = true;
                TC_LOG_INFO("tortoisebots", "[W5] ride-board-refused bot={} entry={} code=off-deck bot_z={:.2f} deck_z={:.2f} has_deck={}",
                    bot->GetName(), rs.entry, bot->GetPositionZ(), deckZ, hasDeckZ ? 1 : 0);
            }
        }
        else
        {
            rs.offDeckRefused = false;
        }
    }
    else
    {
        rs.offDeckRefused = false;
    }

    ai::TransportWaitInput in;
    in.mapReady = true;
    in.onBoard = rs.boarded && currentlyOnBoard;
    in.transportFound = found.obj != nullptr;
    in.transportStopped = found.stopped;
    in.atDestinationDock = atDestinationDock;
    in.distanceToDeck = distToDeck;
    in.maxBoardRange = kMaxBoardReachYd;
    in.approachRange = kApproachReachYd;
    in.hasDeckZ = hasDeckZ;
    in.botZ = bot->GetPositionZ();
    in.deckZ = deckZ;
    in.maxDeckZDiff = kMaxDeckZDiffYd;
    in.approachPathFailed = rs.approachPathFailed;
    in.waitedMs = rs.waitedMs;
    in.totalPathTimeMs = totalPathTimeMs;
    in.marginMs = kGiveUpMarginMs;
    in.missingTimeoutMs = kMissingTimeoutMs;

    switch (ai::DecideTransportWait(in))
    {
        case ai::TransportWaitStep::Board:
        {
            if (found.base && found.obj)
            {
                bot->GetMotionMaster()->Clear();
                bot->StopMoving();
                // Where the bot stands, in the transport's own frame: boarding never moves it.
                found.base->AddPassenger(bot, found.base->GetPositionOffsetTo(bot->GetPosition()));
                rs.boarded = true;
                rs.offDeckRefused = false;
                rs.outcome = ai::RideOrderResult(true, true, false, false, "");
                TC_LOG_INFO("tortoisebots", "[W5] ride-board bot={} entry={} guid={} map={}",
                    bot->GetName(), rs.entry, rs.transportGuid.ToString(), map->GetId());
            }
            break;
        }
        case ai::TransportWaitStep::Approach:
        {
            if (found.obj && !bot->isMoving())
            {
                PathGenerator path(bot);
                path.CalculatePath(found.obj->GetPositionX(), found.obj->GetPositionY(), found.obj->GetPositionZ(), false);
                if (!ai::IsApproachPathUsable(static_cast<uint32_t>(path.GetPathType()), path.GetPath().size()))
                {
                    rs.approachPathFailed = true;
                    rs.active = false;
                    rs.outcome = ai::RideOrderResult(false, false, false, false, "no-path");
                    TC_LOG_INFO("tortoisebots", "[W5] ride-giveup bot={} entry={} code=no-path map={}",
                        bot->GetName(), rs.entry, map->GetId());
                    break;
                }
                bot->GetMotionMaster()->MovePoint(0, found.obj->GetPositionX(), found.obj->GetPositionY(), found.obj->GetPositionZ(), true);
            }
            rs.outcome = ai::RideOrderResult(rs.active, rs.boarded, rs.arrived, rs.offDeckRefused, "");
            break;
        }
        case ai::TransportWaitStep::Leave:
        {
            if (TransportBase* tb = bot->GetTransport())
                tb->RemovePassenger(bot);
            rs.active = false;
            rs.arrived = true;
            rs.outcome = ai::RideOrderResult(false, true, true, false, "");
            TC_LOG_INFO("tortoisebots", "[W5] ride-leave bot={} entry={} guid={} map={}",
                bot->GetName(), rs.entry, rs.transportGuid.ToString(), map->GetId());
            break;
        }
        case ai::TransportWaitStep::GiveUp:
        {
            std::string const failCode = rs.approachPathFailed ? "no-path"
                : (!found.obj ? "not-found" : (rs.offDeckRefused ? "off-deck" : "timeout"));
            rs.active = false;
            rs.outcome = ai::RideOrderResult(false, rs.boarded, false, rs.offDeckRefused, failCode);
            TC_LOG_INFO("tortoisebots", "[W5] ride-giveup bot={} entry={} code={} waited_ms={} total_ms={} map={}",
                bot->GetName(), rs.entry, failCode, rs.waitedMs, totalPathTimeMs, map->GetId());
            break;
        }
        case ai::TransportWaitStep::Wait:
        default:
            rs.outcome = ai::RideOrderResult(rs.active, rs.boarded, rs.arrived, rs.offDeckRefused, "");
            break;
    }
}

bool PollOrderErrand(Player const* bot, uint32_t orderSeq, std::string& outResult)
{
    if (!bot || orderSeq == 0)
        return false;
    auto it = RideStates().find(bot->GetGUID());
    if (it == RideStates().end())
        return false;
    RideState const& rs = it->second;
    if (rs.orderSeq != orderSeq || rs.outcome.empty())
        return false;
    outResult = rs.outcome;
    return true;
}

void CancelOrderErrand(Player const* bot)
{
    if (!bot)
        return;
    auto it = RideStates().find(bot->GetGUID());
    if (it == RideStates().end())
        return;
    it->second.active = false;
    it->second.orderSeq = 0;
    it->second.outcome.clear();
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
