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

constexpr float kMaxBoardReachYd = 30.0f;   // 2D from the transport's origin: a zeppelin or ship deck reaches ~25 yd from it (#215)
constexpr float kMaxDeckZDiffYd = 2.5f;     // vertical band around the transport's deck surface at (botX, botY)
constexpr float kDeckSampleRadiusYd = 26.0f; // #215: the deck is sampled on a grid this far around the origin
constexpr float kDeckSampleStepYd = 2.0f;
constexpr float kDirectStepYd = 10.0f;      // #215: the last metres onto the deck are a straight move (the deck is off the navmesh)
constexpr float kDeckPointRefreshYd = 1.0f; // #215: a boarding point is sampled again when the transport moved this far
constexpr uint32_t kLostTransportGraceMs = 5000; // #215: a cross-map hop removes and re-adds the passenger for a moment
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
    bool hasBoardingPoint = false;  // #215: the deck point the bot walks to, sampled while the transport stands
    ai::DeckPoint boardingPoint;
    float boardingFromX = 0.0f;     // where the transport stood when the point was sampled
    float boardingFromY = 0.0f;
    uint32_t lostMs = 0;            // #215: how long a boarded bot has not been a passenger
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

// #215: the deck surface under (x, y), between fromZ and fromZ - 6 yd, on the transport's own model.
// The core never turns collision on for an MO transport's model (Transport::Create sets the go state before CreateModel, and
// GameObject::SetGoState skips transports), and GameObjectModel::IntersectRay answers "no hit" for a model with collision off:
// that is why the old check said has_deck=0 on the Orgrimmar zeppelin every time, also with the bot on the docked deck. The
// model is switched on for this one query and put back as it was (world thread, the maps are not updating here).
bool SampleDeckZAt(WorldObject const* transportObj, PhaseShift const& phaseShift, float x, float y, float fromZ, float& outDeckZ)
{
    if (!transportObj)
        return false;

    if (GameObject const* go = transportObj->ToGameObject())
    {
        if (go->m_model)
        {
            bool const wasEnabled = go->m_model->IsCollisionEnabled();
            go->m_model->EnableCollision(true);
            float maxDist = 6.0f;
            G3D::Ray const ray(G3D::Vector3(x, y, fromZ), G3D::Vector3(0.0f, 0.0f, -1.0f));
            bool const hit = go->m_model->IntersectRay(ray, maxDist, /*stopAtFirstHit=*/false, phaseShift, VMAP::ModelIgnoreFlags::Nothing);
            go->m_model->EnableCollision(wasEnabled);
            if (hit)
            {
                outDeckZ = fromZ - maxDist;
                return true;
            }
            return false;
        }
    }

    outDeckZ = transportObj->GetPositionZ();
    return true;
}

bool SampleTransportDeckZ(Player const* bot, WorldObject const* transportObj, float& outDeckZ)
{
    if (!bot)
        return false;
    return SampleDeckZAt(transportObj, bot->GetPhaseShift(), bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ() + 2.0f, outDeckZ);
}

// #215: the boarding point: the deck is sampled on a grid around the transport's origin at the bot's height band, and the
// sample nearest the bot is the place it walks to (approaching the origin itself ended at the tower edge).
bool FindBoardingPoint(Player const* bot, WorldObject const* transportObj, ai::DeckPoint& out)
{
    if (!bot || !transportObj || !transportObj->ToGameObject() || !transportObj->ToGameObject()->m_model)
        return false;
    std::vector<ai::DeckPoint> samples;
    float const ox = transportObj->GetPositionX();
    float const oy = transportObj->GetPositionY();
    float const fromZ = bot->GetPositionZ() + kMaxDeckZDiffYd + 0.5f;
    for (float dx = -kDeckSampleRadiusYd; dx <= kDeckSampleRadiusYd; dx += kDeckSampleStepYd)
        for (float dy = -kDeckSampleRadiusYd; dy <= kDeckSampleRadiusYd; dy += kDeckSampleStepYd)
        {
            if (dx * dx + dy * dy > kDeckSampleRadiusYd * kDeckSampleRadiusYd)
                continue;
            float z = 0.0f;
            if (SampleDeckZAt(transportObj, bot->GetPhaseShift(), ox + dx, oy + dy, fromZ, z))
                samples.push_back({ ox + dx, oy + dy, z });
        }
    return ai::PickBoardingPoint(samples, bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), kMaxDeckZDiffYd, out);
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

    // #215 stale state: boarded, and no longer a passenger. After the grace the order fails instead of answering
    // pending:aboard for ever (live 2026-11-22: he fell off the Orgrimmar tower and stayed "aboard" with transport=none).
    if (rs.boarded && !rs.arrived)
    {
        rs.lostMs = currentlyOnBoard ? 0 : rs.lostMs + diffMs;
        if (ai::HasLostTransport(rs.boarded, rs.arrived, currentlyOnBoard, rs.lostMs, kLostTransportGraceMs))
        {
            rs.active = false;
            rs.boarded = false;
            rs.outcome = ai::RideOrderResult(false, false, false, false, "lost-transport");
            TC_LOG_INFO("tortoisebots", "[W5] ride-giveup bot={} entry={} code=lost-transport lost_ms={} map={}",
                bot->GetName(), rs.entry, rs.lostMs, map->GetId());
            return;
        }
        if (!currentlyOnBoard)
            return;   // inside the grace: no new boarding somewhere else
    }

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
            // #215: walk to a point on the deck, not to the transport's origin (that ended at the tower edge). Far: the navmesh
            // path toward it (it ends at the dock edge, the deck is off the navmesh); near: one straight step onto the deck.
            if (found.obj && (!rs.hasBoardingPoint ||
                Dist2D(found.obj->GetPositionX(), found.obj->GetPositionY(), rs.boardingFromX, rs.boardingFromY) > kDeckPointRefreshYd))
            {
                rs.hasBoardingPoint = FindBoardingPoint(bot, found.obj, rs.boardingPoint);
                rs.boardingFromX = found.obj->GetPositionX();
                rs.boardingFromY = found.obj->GetPositionY();
                if (rs.hasBoardingPoint)
                    TC_LOG_INFO("tortoisebots", "[W5] ride-deck-point bot={} entry={} x={:.2f} y={:.2f} z={:.2f} map={}",
                        bot->GetName(), rs.entry, rs.boardingPoint.x, rs.boardingPoint.y, rs.boardingPoint.z, map->GetId());
            }
            if (found.obj && !rs.hasBoardingPoint)
            {
                // No deck surface at the bot's height near the transport: wait (it may still be docking) until the give-up.
                rs.outcome = ai::RideOrderResult(rs.active, rs.boarded, rs.arrived, rs.offDeckRefused, "");
                break;
            }
            if (found.obj && !bot->isMoving())
            {
                ai::DeckPoint const& p = rs.boardingPoint;
                float const d = Dist2D(bot->GetPositionX(), bot->GetPositionY(), p.x, p.y);
                if (ai::ChooseDeckApproach(d, kDirectStepYd) == ai::DeckApproachMode::Direct)
                    bot->GetMotionMaster()->MovePoint(0, p.x, p.y, p.z, /*generatePath=*/false);
                else
                {
                    PathGenerator path(bot);
                    path.CalculatePath(p.x, p.y, p.z, false);
                    if (!ai::IsApproachPathUsable(static_cast<uint32_t>(path.GetPathType()), path.GetPath().size()))
                    {
                        rs.approachPathFailed = true;
                        rs.active = false;
                        rs.outcome = ai::RideOrderResult(false, false, false, false, "no-path");
                        TC_LOG_INFO("tortoisebots", "[W5] ride-giveup bot={} entry={} code=no-path map={}",
                            bot->GetName(), rs.entry, map->GetId());
                        break;
                    }
                    // the end of the navmesh path: the dock edge nearest the deck point. Standing there already, the rest is the
                    // straight step (the gap from the edge to the deck point can be more than kDirectStepYd).
                    G3D::Vector3 const& end = path.GetPath().back();
                    if (Dist2D(bot->GetPositionX(), bot->GetPositionY(), end.x, end.y) < 2.0f)
                        bot->GetMotionMaster()->MovePoint(0, p.x, p.y, p.z, /*generatePath=*/false);
                    else
                        bot->GetMotionMaster()->MovePoint(0, end.x, end.y, end.z, /*generatePath=*/true);
                }
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

bool HoldsMovement(Player const* bot)
{
    if (!bot)
        return false;
    auto it = RideStates().find(bot->GetGUID());
    return it != RideStates().end() && ai::RideHoldsMovement(it->second.active, it->second.arrived);
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
