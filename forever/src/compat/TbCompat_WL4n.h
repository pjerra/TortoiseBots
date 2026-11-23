// ForeverCore side of the both-core helpers of the travel slice W4, round A1 (the node graph: TravelNode.cpp).
// Turtle side: ai/cmangos-compat-wl4n.h (same names and signatures; each is the call the file always made, or the answer that
// leaves the Turtle behaviour untouched). Both are found through ai/playerbot/TbCompatWL4n.h (`__has_include`).
//
// Round A2 owns the WL4 pair (the destination table, zone levels); this is A1's own pair so the two rounds never edit one file.
// The area-trigger entry helpers TravelNode.cpp uses are the W1 pair's (AreaTriggerAtCompat and friends in TbCompat_W1.h).
// Notes: forever/docs/W4-A1-NOTES.md.
#pragma once

#include <memory>
#include <string>

#include "Maps/MoveMap.h" // MMapManager: the navmesh and per-tile probe of the generator
#include "TbCompat_S5.h" // WorldQueryCompat: the single-table SELECT guard (backticked table and column names)
#include "TbCompat_W3.h" // LegacyFormatW3: printf formatting for the saver's INSERT/UPDATE statements
#include "TbCompat_WL4.h" // CreatureLevelMaxWL4: the classic creature level (round A2)
// W5T: the travel graph's flight and transport legs board through the W5 host seams.
#include "host/TbHostTaxi.h"
#include "host/TbHostTransport.h"
#include "playerbot/policy/travel/CreatureLevelPolicy.h"
// The per-link flight decision (known ends, fare, faction, classic maps).
#include "playerbot/policy/travel/TaxiChoicePolicy.h"
#include "playerbot/policy/travel/TravelGraphGenPolicy.h"
#include "playerbot/policy/travel/TravelGraphPolicy.h"
// The old size format of a log line (T `SIZEFMTD`, shared/Util.h). ForeverCore logs through a printf Format, so the size_t
// conversion is the same one.
#ifndef SIZEFMTD
#define SIZEFMTD "%zu"
#endif

// ===============================================================================================================
// THE NODE STORE (TravelNodeMap::loadNodeStore, saveNodeStore)
// ===============================================================================================================
// A read of ai_playerbot_travelnode, _link or _path. The guard first asks information_schema whether the table and every column
// the query names exist: ForeverCore's connection ABORTS THE SERVER on a missing table or column (MySQLConnection.cpp:596-601),
// Turtle logs and answers null. A missing table therefore reads as "no rows" here (schemaMissing), which is what the loader
// already handles: the travel graph stays empty and travel does nothing.
inline QueryResult TravelNodeQueryCompat(char const* sql, bool* schemaMissing = nullptr)
{
    return WorldQueryCompat(sql, schemaMissing);
}

// Are the three travel-node tables, with every column the saver writes, there? Asked once before the first write: a statement
// naming a missing table or column aborts ForeverCore (the same rule WorldQueryCompat follows for reads), so the save is skipped
// with one line instead (does less; the graph in memory is unaffected).
inline bool TravelNodeStoreWritableCompat()
{
    return WorldSchemaHasCompat("ai_playerbot_travelnode", { "id", "name", "map_id", "x", "y", "z", "linked" }) &&
        WorldSchemaHasCompat("ai_playerbot_travelnode_link",
            { "node_id", "to_node_id", "type", "object", "distance", "swim_distance", "extra_cost", "calculated",
              "max_creature_0", "max_creature_1", "max_creature_2" }) &&
        WorldSchemaHasCompat("ai_playerbot_travelnode_path", { "node_id", "to_node_id", "nr", "map_id", "x", "y", "z" });
}

// The saver's one transaction. The save writes a whole graph (hundreds of thousands of path rows): as autocommit statements that
// is an hour-class stall on the world thread, and a crash between the DELETEs and the last INSERT leaves empty tables. So the
// DELETEs and every INSERT are appended to one transaction (TravelDbBeginCompat) that commits on the calling thread when the
// saver is done (TravelDbCommitCompat): the tables hold the old graph or the whole new one.
inline WorldDatabaseTransaction& TravelNodeTransactionCompat()
{
    static WorldDatabaseTransaction transaction;
    return transaction;
}

inline void TravelDbBeginCompat()
{
    TravelNodeTransactionCompat() = WorldDatabase.BeginTransaction();
}

inline void TravelDbCommitCompat()
{
    WorldDatabaseTransaction& transaction = TravelNodeTransactionCompat();
    if (!transaction)
        return;

    WorldDatabase.DirectCommitTransaction(transaction);
    transaction.reset();
}

// One write of the saver (after TravelNodeStoreWritableCompat): printf formats (the Turtle rules) sent as text, because
// ForeverCore's PExecute takes `{}` formats and would silently drop the arguments. Inside the saver's transaction it is appended to
// it; outside one it is a DirectExecute on the calling thread. THE raw statements of this pair, named in
// tools/test_forever_no_direct_queries.py with their count.
template <class... Args>
inline void TravelNodeExecuteCompat(char const* format, Args... args)
{
    std::string const sql = LegacyFormatW3(format, args...);
    WorldDatabaseTransaction& transaction = TravelNodeTransactionCompat();
    if (transaction)
        transaction->Append(sql.c_str());
    else
        WorldDatabase.DirectExecute(sql.c_str());
}

// ===============================================================================================================
// THE GENERATOR'S PASSES (generateWalkPaths, generateHelperNodes, removeUselessPaths, calculatePathCosts)
// ===============================================================================================================
// May the per-map passes of the node generator run on worker threads? On Turtle they always did (std::async), and the navmesh
// calls are safe there. On ForeverCore a PathGenerator must query its map on that map's update thread (PathGenerator.h: the
// closed-door and liquid checks read the map's game objects), so every pass runs on the calling (world) thread: false. The
// graph is generated in the generation boot only (decision D3), so nothing waits on a server with players.
inline bool TravelGenParallelCompat()
{
    return false;
}

// Load the map data under one grid of a map, the way the core loads it when an object is added to that grid
// (Map::EnsureGridCreated, Map.cpp:300-301: TerrainInfo::LoadMapAndVMap + LoadMMap). ForeverCore loads it per instance id.
// Used by the generation boot (round B) for the far ends of the paths it builds: PathGenerator needs the navmesh tile and the
// terrain grid of both ends. False when the map has no terrain (the caller does less: no path from that tile).
inline bool MmapTileLoadCompat(uint32 mapId, uint32 instanceId, int x, int y)
{
    std::shared_ptr<TerrainInfo> terrain = sTerrainMgr.LoadTerrain(mapId);
    if (!terrain)
        return false;

    terrain->LoadMapAndVMap(x, y);
    terrain->LoadMMap(instanceId, x, y);
    return true;
}

// ===============================================================================================================
// THE LINK TYPES THE ROUTE MAY USE (TravelNodePath::getCost, the A* of TravelNodeMap::getRoute)
// ===============================================================================================================
// `pathType` is TravelNodePathType as its number (0 none, 1 walk, 2 areaTrigger, 3 transport, 4 flightPath, 5 teleportSpell,
// 6 staticPortal). On ForeverCore walks, area triggers, transport legs and flight paths are routes
// (ai::travel_graph::LinkUsableOnForever): flights board through tbhost::taxi::Fly and rides through tbhost::ride (never
// instant flight or teleport), and decision D4 allows no travel teleport but the host's logged rescue, so the
// teleport-spell and static-portal links stay refused. A route that needs one is refused so the bot picks another target
// (does less), and reachability (TravelNode::getNodeMap) and the map transfers (TravelMgr::LoadMapTransfers) ask the
// same rule. On Turtle every type stays usable, as it always was.
inline bool TravelLinkUsableCompat(uint8 pathType)
{
    return ai::travel_graph::LinkUsableOnForever(pathType);
}

// May the loader keep a link of an unknown or invalid type (0 or past static portal)? ForeverCore refuses it when the graph is built
// (counted, one log line), so no route and no map transfer can come from it. Turtle loads its rows as it always did.
inline bool TravelLoaderChecksLinkTypeCompat()
{
    return true;
}

// May the loader keep a walk link that joins two maps, or whose path runs through a point on another map? A walk is costed as
// `distance` yards on foot, so such a row (one bad row from the generator is enough) would leave the bot standing at the node: ForeverCore
// skips it, one log line counts them. Turtle loads its rows as it always did.
inline bool TravelLoaderChecksWalkMapsCompat()
{
    return true;
}

// The Deeprun Tram exit: a TeleportTo outside the logged rescue, which decision D4 does not allow. Off until W5 brings the transport
// legs (and with them a way into the tram); a free bot already inside is left to the host's stuck rescue. Turtle keeps the exit.
inline bool TravelTramExitAllowedCompat()
{
    return false;
}

// Decision D4: the only travel teleport of the slice is the host's rescue of a stuck free bot, so the A* adds no portal
// edge here (the hearthstone edge and the teleport-spell edges are cut; the bot walks or refuses the target). Turtle keeps
// its portal edges.
inline bool TravelTeleportLinksCompat()
{
    return false;
}

// ===============================================================================================================
// AREA TRIGGERS (TravelCostContext::canUseAreaTrigger, getAreaTriggerId, LeaveDeeprunTram)
// ===============================================================================================================
// The box centre an area trigger teleport is used from: T `box_x/box_y/box_z`, F the DBC `Pos` (DB2Structure.h:166).
inline void AreaTriggerBoxPosCompat(AreaTriggerEntry const* entry, float& x, float& y, float& z)
{
    x = entry ? entry->Pos.X : 0.0f;
    y = entry ? entry->Pos.Y : 0.0f;
    z = entry ? entry->Pos.Z : 0.0f;
}

// May the bot use this area trigger teleport? F's WorldSafeLocsEntry (ObjectMgr.h:886, the transport destination of an area
// trigger) carries no condition, and the core checks its own conditions when the bot crosses the trigger; nothing is refused
// here (does less: the route may try a trigger the bot cannot use, and AreaTriggerAction closes that edge then). Turtle keeps
// the legacy condition check.
inline bool AreaTriggerConditionAllowsCompat(AreaTriggerTeleport const* /*at*/, Player* /*bot*/, Map* /*map*/)
{
    return true;
}

// Is there a record for this area trigger id? ForeverCore has the DBC entries (sAreaTriggerStore) and looks the teleport up in
// the world-thread handler; Turtle keeps the legacy teleport-record lookup of the path code.
inline bool AreaTriggerKnownCompat(uint32 id)
{
    return sAreaTriggerStore.LookupEntry(id) != nullptr;
}

// Whether a position is inside an area trigger's zone. ForeverCore has no static zone test for a DBC AreaTriggerEntry (the
// check lives in the world-thread handler), so a trigger is not treated as being at the point: the path keeps walking and the
// core teleports the bot when it crosses the trigger (does less). Turtle keeps the legacy test.
inline bool AreaTriggerZoneContainsCompat(AreaTriggerEntry const* /*entry*/, uint32 /*mapId*/, float /*x*/, float /*y*/, float /*z*/, float /*radius*/)
{
    return false;
}

// ===============================================================================================================
// CREATURES AND THEIR SPAWNS (TravelNodePath::calculateCost, generateNpcNodes, generateStartNodes)
// ===============================================================================================================
// The entry of a spawn: T `creature_id[0]`, F SpawnData::id (SpawnData.h:135).
inline uint32 SpawnEntryCompat(CreatureData const& data) { return data.id; }

// The template's fields the node generator names: T `entry/name/npc_flags/rank`, F `Entry/Name/npcflag/Classification`.
inline uint32 CreatureTemplateEntryCompat(CreatureInfo const* info) { return info->Entry; }
inline std::string CreatureTemplateNameCompat(CreatureInfo const* info) { return info->Name; }
inline uint64 CreatureTemplateNpcFlagsCompat(CreatureInfo const* info) { return info->npcflag; }
inline uint32 CreatureTemplateRankCompat(CreatureInfo const* info) { return uint32(info->Classification); }
// The highest level of the template, for the creature scan of a link's cost (max_creature_0..2). ForeverCore has no template level:
// the classic level of the entry is the helper A2 merged (CreatureLevelMaxWL4 = `creature_classic_level`). An entry without a row is
// not rated (0: it adds no threat reading; the unknown level of the destination rules, kUnknown, must never reach a link's cost).
// A constant 0 here stored 0,0,0 on every link and made the creature cost modifier always 1 (a level-5 bot routed through a level-50
// camp as through an empty road).
inline uint32 CreatureTemplateMaxLevelCompat(CreatureInfo const* info)
{
    return info ? ai::travel_level::ThreatLevelOf(CreatureLevelMaxWL4(info->Entry)) : 0u;
}

// The NPC service flags of a template. T `UNIT_NPC_FLAG_SPIRITHEALER/SPIRITGUIDE` (0x20/0x40), F `UNIT_NPC_FLAG_SPIRIT_HEALER`
// (0x4000) and no guide flag at all.
inline bool NpcFlagIsInnkeeperCompat(uint64 flags) { return (flags & UNIT_NPC_FLAG_INNKEEPER) != 0; }
inline bool NpcFlagIsFlightMasterCompat(uint64 flags) { return (flags & UNIT_NPC_FLAG_FLIGHTMASTER) != 0; }
inline bool NpcFlagIsSpiritHealerCompat(uint64 flags) { return (flags & UNIT_NPC_FLAG_SPIRIT_HEALER) != 0; }
inline bool NpcFlagIsSpiritGuideCompat(uint64 /*flags*/) { return false; }

// ===============================================================================================================
// GAME OBJECTS (the static portal and transport generators)
// ===============================================================================================================
// Whether a static portal may be used by this bot: T reads the template's faction, F's GameObjectTemplate has no faction (the
// core checks the use of the game object itself), so nothing is refused (does less). Turtle keeps the legacy check.
inline bool GameObjectFactionAllowsCompat(GameObjectInfo const* /*data*/, Player* /*bot*/) { return true; }

// The spell of a GAMEOBJECT_TYPE_SPELLCASTER: T `spellcaster.spellId`, F `spellCaster.spell` (GameObjectData.h:412).
inline uint32 GameObjectSpellcasterSpellCompat(GameObjectInfo const* data) { return data->spellCaster.spell; }

// The taxi path of a transport game object: T `moTransport.taxiPathId`, F `moTransport.taxiPathID` (GameObjectData.h:349).
inline uint32 GameObjectMoTransportTaxiPathCompat(GameObjectInfo const* data) { return data->moTransport.taxiPathID; }

// The landing position of a spell: T `GetSpellTargetPosition(id)` (the first effect), F takes the effect index too
// (SpellMgr.h:695); effect 0 is the one the legacy call answered.
inline SpellTargetPosition const* SpellTargetPositionCompat(uint32 spellId)
{
    return sSpellMgr.GetSpellTargetPosition(spellId, EFFECT_0);
}

// The name of a spell for a log line and a node name: T `SpellName[0]` (a char*), F `SpellName[LOCALE_enUS]` (a std::string).
inline char const* SpellNameCompat(SpellEntry const* spell)
{
    return spell ? spell->SpellName[LOCALE_enUS].c_str() : "";
}

// ===============================================================================================================
// TAXI TABLES (getCost, getPrice, generateTaxiPaths)
// ===============================================================================================================
inline uint32 TaxiPathFromCompat(TaxiPathEntry const* path) { return path->FromTaxiNode; }
inline uint32 TaxiPathToCompat(TaxiPathEntry const* path) { return path->ToTaxiNode; }
inline uint32 TaxiPathPriceCompat(TaxiPathEntry const* path) { return path->Cost; }
inline uint32 TaxiNodeMapIdCompat(TaxiNodesEntry const* node) { return node->ContinentID; }
inline float TaxiNodeXCompat(TaxiNodesEntry const* node) { return node->Pos.X; }
inline float TaxiNodeYCompat(TaxiNodesEntry const* node) { return node->Pos.Y; }
inline float TaxiNodeZCompat(TaxiNodesEntry const* node) { return node->Pos.Z; }
inline bool PlayerTaxiMaskKnowsCompat(Player* bot, uint32 node) { return bot->m_taxi.IsTaximaskNodeKnown(node); }
// F's path node list is a vector of pointers (DB2Stores.h:375), T's a Path of values.
inline uint32 TaxiPathNodeMapIdCompat(TaxiPathNodeList const& nodes, std::size_t i) { return nodes[i]->ContinentID; }
inline float TaxiPathNodeXCompat(TaxiPathNodeList const& nodes, std::size_t i) { return nodes[i]->Loc.X; }
inline float TaxiPathNodeYCompat(TaxiPathNodeList const& nodes, std::size_t i) { return nodes[i]->Loc.Y; }
inline float TaxiPathNodeZCompat(TaxiPathNodeList const& nodes, std::size_t i) { return nodes[i]->Loc.Z; }
inline uint32 TaxiPathNodeDelayCompat(TaxiPathNodeList const& nodes, std::size_t i) { return nodes[i]->Delay; }
// ===============================================================================================================
// W5T: THE PER-LINK CHECKS AND THE EXECUTOR (TravelNodePath::getCost, MovementAction::UseTaxi/UseTransport)
// ===============================================================================================================
class PlayerbotAI;

// A stored flight link may carry this bot only when it knows both ends and can pay the fare: the from node (the bot
// boards there, so Turtle's destination-only check is not enough), the faction of each end (the mount a team can use)
// and the classic maps (the runtime flight links W5 builds) feed the pure per-link decision; the fare is the path's own
// price, like the legacy check below it in getCost. A refused link costs -1 and the A* routes around it (cannot: travel).
inline bool TravelFlightLinkUsableCompat(Player* bot, TaxiPathEntry const* taxiPath, uint32 cGold)
{
    if (!bot || !taxiPath)
        return false;
    uint32 const fromId = TaxiPathFromCompat(taxiPath);
    uint32 const toId = TaxiPathToCompat(taxiPath);
    TaxiNodesEntry const* fromNode = sTaxiNodesStore.LookupEntry(fromId);
    TaxiNodesEntry const* toNode = sTaxiNodesStore.LookupEntry(toId);
    bool const cheater = bot->IsTaxiCheater();
    uint32 const teamIdx = bot->GetTeam() == ALLIANCE ? 1 : 0;
    ai::FlightLinkAccess access;
    access.fromKnown = cheater || PlayerTaxiMaskKnowsCompat(bot, fromId);
    access.toKnown = cheater || PlayerTaxiMaskKnowsCompat(bot, toId);
    access.fareCopper = TaxiPathPriceCompat(taxiPath);
    access.botMoneyCopper = cGold;
    access.fromFactionOk = fromNode && fromNode->MountCreatureID[teamIdx] != 0;
    access.toFactionOk = toNode && toNode->MountCreatureID[teamIdx] != 0;
    access.fromClassic = fromNode && ai::IsClassicContinentMap(TaxiNodeMapIdCompat(fromNode));
    access.toClassic = toNode && ai::IsClassicContinentMap(TaxiNodeMapIdCompat(toNode));
    return ai::DecideFlightLinkUsable(access).action == ai::TaxiChoiceResult::Fly;
}

// Boards a graph flight leg through the W5 host seam: learn the master's node when it is new (the legacy executor
// learns it too), then fly the core's own ActivateTaxi path. Never instant flight or teleport: the bot flies for real
// and TickLandingWatch logs the landing. Always handled here, so the legacy body below never runs on ForeverCore.
// False is the refusal (unknown node, fare, the master out of reach): the move fails and the travel layer drops the
// target (cannot: travel).
inline bool TravelTaxiFlyCompat(PlayerbotAI* ai, uint32 pathId, bool* handled)
{
    if (handled)
        *handled = true;
    Player* bot = ai ? ai->GetBot() : nullptr;
    TaxiPathEntry const* taxiPath = bot ? sTaxiPathStore.LookupEntry(pathId) : nullptr;
    if (!taxiPath)
        return false;
    Creature* npc = tbhost::taxi::NearestFlightMaster(bot);
    if (!npc)
        return false;
    uint32 const nodeId = tbhost::taxi::NodeOfFlightMaster(bot, npc);
    if (nodeId && !bot->IsTaxiCheater() && !PlayerTaxiMaskKnowsCompat(bot, nodeId))
        tbhost::taxi::Learn(bot, npc);
    std::string fail;
    return tbhost::taxi::Fly(bot, npc, TaxiPathToCompat(taxiPath), &fail);
}

// Arms a graph transport leg (boat, zeppelin, tram) through the W5 host seam: the engine tick boards the stopped
// transport where it stands, rides it and leaves at the destination dock (walk on, board where it stands). Always
// returns false: the bot waits at the dock while TickRide works, so no call site may read this as boarded, and no
// legacy teleport ever runs for the wait (TravelTransportTeleportCompat). A refused StartRide is the host's own
// [W5] ride-refused line; the bot keeps waiting (cannot: travel, never a silent success).
inline bool TravelRideBoardCompat(PlayerbotAI* ai, uint32 entry, bool* handled)
{
    if (handled)
        *handled = true;
    Player* bot = ai ? ai->GetBot() : nullptr;
    if (!bot || !entry || tbhost::ride::IsOnBoard(bot))
        return false;
    std::string fail;
    tbhost::ride::StartRide(bot, entry, &fail);
    return false;
}

// The legacy executor teleports a waiting bot to the far dock (TransportTeleportType 2). On ForeverCore the bot rides
// instead, so that teleport never runs in travel. Turtle keeps it.
inline bool TravelTransportTeleportCompat()
{
    return false;
}

// ===============================================================================================================
// THE START POSITION OF A RACE AND CLASS (generateStartNodes)
// ===============================================================================================================
// T `mapId/positionX/positionY/positionZ/orientation`, F PlayerInfo::createPosition.Loc (ObjectMgr.h:704-712).
inline uint32 PlayerInfoMapIdCompat(PlayerInfo const* info) { return info->createPosition.Loc.GetMapId(); }
inline float PlayerInfoXCompat(PlayerInfo const* info) { return info->createPosition.Loc.GetPositionX(); }
inline float PlayerInfoYCompat(PlayerInfo const* info) { return info->createPosition.Loc.GetPositionY(); }
inline float PlayerInfoZCompat(PlayerInfo const* info) { return info->createPosition.Loc.GetPositionZ(); }
inline float PlayerInfoOrientationCompat(PlayerInfo const* info) { return info->createPosition.Loc.GetOrientation(); }

// Turtle's own high-elf race: ForeverCore has no such race, 0xFF is never reached by the MAX_RACES loop and the name is unused.
inline uint32 TravelHighElfRaceCompat() { return 0xFF; }

// ===============================================================================================================
// THE FIGHT GATES OF THE DUNGEON-FOLLOW HOLD (TravelPath::ClipBeforePack) AND THE STACK DISTANCES
// ===============================================================================================================
// Can this creature start a fight with `target`: T CanInitiateAttack(), F CanStartAttack (Creature.h:334), each core's own rule.
inline bool CreatureCanInitiateAttackCompat(Creature* creature, Unit* target) { return creature->CanStartAttack(target, false); }

// The combat reach of `from` against `to`: T takes the target (and the extra flags), F reads the unit's own reach (Unit.h).
inline float CombatReachToCompat(Unit* from, Unit* /*to*/) { return from->GetCombatReach(); }

// ===============================================================================================================
// THE GENERATOR'S NAVMESH SEARCHES (TravelNode.cpp's first-boot walk linking, WorldPosition::isMmapLoaded)
// ===============================================================================================================
// One map's navmesh for the generator's own Detour queries, held under the navmesh read lock: no tile load or unload can
// change the mesh while a query runs, and the query is the calling thread's own (the query MMapManager hands out per
// instance is shared and not thread-safe, MMapManager.h:69-71). GetNavMesh (MMapManager.h:72) answers null until the map's
// mesh data exists, which the caller's tile load creates.
class TravelGenNavMeshCompat
{
public:
    TravelGenNavMeshCompat(uint32 mapId, uint32 instanceId)
    {
        MMAP::MMapManager* mmap = MMAP::MMapManager::instance();
        _mesh = mmap ? mmap->GetNavMesh(mapId, instanceId) : nullptr;
        _ready = _mesh && dtStatusSucceed(_query.init(_mesh, kMaxNodes));
    }

    bool Ready() const { return _ready; }
    dtNavMesh* Mesh() const { return _mesh; }
    dtNavMeshQuery const* Query() const { return &_query; }

private:
    // The core's own instance query node pool (MMapManager.cpp:39: long bot routes ran out at 1024).
    static constexpr int kMaxNodes = 8192;
    // The documented pattern for a search off a map-update thread (MMapManager.h:107-112): the thread flag, then the
    // shared tile lock for the whole search.
    MMAP::ScopedThreadNavMeshQuery _threadQuery;
    MMAP::NavMeshReadLock _lock;
    dtNavMesh* _mesh = nullptr;
    dtNavMeshQuery _query;
    bool _ready = false;
};

// Is the navmesh tile of this grid loaded? The mesh existing is not enough: loadMapAndVMap skips a tile whose probe answers
// yes (WorldPosition.cpp), and a Detour query over a missing tile finds no polygon. Detour reads the world point as
// (y, z, x) and calcTileLoc answers (tx, ty) = (gridY, gridX) (PathGenerator.cpp:1375-1383 does the same probe).
inline bool NavMeshTileLoadedCompat(uint32 mapId, uint32 instanceId, int gridX, int gridY,
    float pointX, float pointY, float pointZ)
{
    MMAP::ScopedThreadNavMeshQuery threadQuery;
    MMAP::NavMeshReadLock lock;
    MMAP::MMapManager* mmap = MMAP::MMapManager::instance();
    dtNavMesh* navMesh = mmap ? mmap->GetNavMesh(mapId, instanceId) : nullptr;
    if (!navMesh)
        return false;

    float const point[3] = { pointY, pointZ, pointX };
    int tx = 0, ty = 0;
    navMesh->calcTileLoc(point, &tx, &ty);
    if (!ai::TravelGenTileCoordsMatch(tx, ty, gridX, gridY))
        return false;
    return navMesh->getTileAt(tx, ty, 0) != nullptr;
}
