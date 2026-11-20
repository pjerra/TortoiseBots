// Turtle side of the both-core helpers of the travel slice W4, round A1 (the node graph: TravelNode.cpp).
// Same names and signatures as forever/src/compat/TbCompat_WL4n.h; every helper is the call the file always made, or the
// answer that leaves the Turtle behaviour exactly as it was. Found through ai/playerbot/TbCompatWL4n.h (`__has_include`).
#pragma once

#include <cstddef>
#include <memory>

// The world-database read of the node store: the original expression, the core's Query handed over as a raw QueryResult* the
// caller used to hold in a unique_ptr. Turtle logs a missing table itself and answers null; `schemaMissing` is always false.
inline std::unique_ptr<QueryResult> TravelNodeQueryCompat(char const* sql, bool* schemaMissing = nullptr)
{
    if (schemaMissing)
        *schemaMissing = false;
    return std::unique_ptr<QueryResult>(WorldDatabase.Query(sql));
}

// One write of the saver: the engine's own printf-format statement.
template <class... Args>
inline void TravelNodeExecuteCompat(char const* format, Args... args)
{
    WorldDatabase.PExecute(format, args...);
}

inline void TravelDbBeginCompat() { WorldDatabase.BeginTransaction(); }
inline void TravelDbCommitCompat() { WorldDatabase.CommitTransaction(); }

// The generator's per-map passes stay on worker threads, as they always were on this core.
inline bool TravelGenParallelCompat()
{
    return true;
}

// Nothing to load here: the Turtle core loads a grid through its Map (Map::LoadMapAndVMap, private), and the module always
// relied on the active map's grids plus WorldPosition's own navmesh load. True: the caller may go on.
inline bool MmapTileLoadCompat(uint32 /*mapId*/, uint32 /*instanceId*/, int /*x*/, int /*y*/)
{
    return true;
}

// Every link type stays usable, as it always was.
inline bool TravelLinkUsableCompat(uint8 /*pathType*/)
{
    return true;
}

// Area triggers: the T fields and the T condition test of the teleport.
inline void AreaTriggerBoxPosCompat(AreaTriggerEntry const* entry, float& x, float& y, float& z)
{
    x = entry ? entry->box_x : 0.0f;
    y = entry ? entry->box_y : 0.0f;
    z = entry ? entry->box_z : 0.0f;
}

inline bool AreaTriggerConditionAllowsCompat(AreaTriggerTeleport const* at, Player* bot, Map* map)
{
    return !at || !at->requiredCondition || !map ||
        sObjectMgr.IsConditionSatisfied(at->requiredCondition, bot, map, nullptr, (ConditionSource)CONDITION_FROM_AREATRIGGER_TELEPORT);
}

inline bool AreaTriggerZoneContainsCompat(AreaTriggerEntry const* entry, uint32 mapId, float x, float y, float z, float radius)
{
    return entry && IsPointInAreaTriggerZone(entry, mapId, x, y, z, radius);
}

// Creatures: the T fields.
inline uint32 SpawnEntryCompat(CreatureData const& data) { return data.creature_id[0]; }
inline uint32 CreatureTemplateEntryCompat(CreatureInfo const* info) { return info->entry; }
inline std::string CreatureTemplateNameCompat(CreatureInfo const* info) { return info->name; }
inline uint64 CreatureTemplateNpcFlagsCompat(CreatureInfo const* info) { return info->npc_flags; }
inline uint32 CreatureTemplateRankCompat(CreatureInfo const* info) { return info->rank; }
inline uint32 CreatureTemplateMaxLevelCompat(CreatureInfo const* info) { return info->level_max; }

inline bool NpcFlagIsInnkeeperCompat(uint64 flags) { return (flags & UNIT_NPC_FLAG_INNKEEPER) != 0; }
inline bool NpcFlagIsFlightMasterCompat(uint64 flags) { return (flags & UNIT_NPC_FLAG_FLIGHTMASTER) != 0; }
inline bool NpcFlagIsSpiritHealerCompat(uint64 flags) { return (flags & UNIT_NPC_FLAG_SPIRITHEALER) != 0; }
inline bool NpcFlagIsSpiritGuideCompat(uint64 flags) { return (flags & UNIT_NPC_FLAG_SPIRITGUIDE) != 0; }

// Game objects: the T fields and the T faction check.
inline bool GameObjectFactionAllowsCompat(GameObjectInfo const* data, Player* bot)
{
    FactionTemplateEntry const* factionEntry = sFactionTemplateStore.LookupEntry(data->faction);
    return !factionEntry || PlayerbotAI::GetFactionReaction(factionEntry, bot->GetFactionTemplateEntry()) >= REP_NEUTRAL;
}

inline uint32 GameObjectSpellcasterSpellCompat(GameObjectInfo const* data) { return data->spellcaster.spellId; }
inline uint32 GameObjectMoTransportTaxiPathCompat(GameObjectInfo const* data) { return data->moTransport.taxiPathId; }

inline SpellTargetPosition const* SpellTargetPositionCompat(uint32 spellId) { return sSpellMgr.GetSpellTargetPosition(spellId); }
inline char const* SpellNameCompat(SpellEntry const* spell) { return spell ? spell->SpellName[0] : ""; }

// Taxi tables: the T fields.
inline uint32 TaxiPathFromCompat(TaxiPathEntry const* path) { return path->from; }
inline uint32 TaxiPathToCompat(TaxiPathEntry const* path) { return path->to; }
inline uint32 TaxiPathPriceCompat(TaxiPathEntry const* path) { return path->price; }
inline uint32 TaxiNodeMapIdCompat(TaxiNodesEntry const* node) { return node->map_id; }
inline float TaxiNodeXCompat(TaxiNodesEntry const* node) { return node->x; }
inline float TaxiNodeYCompat(TaxiNodesEntry const* node) { return node->y; }
inline float TaxiNodeZCompat(TaxiNodesEntry const* node) { return node->z; }
inline bool PlayerTaxiMaskKnowsCompat(Player* bot, uint32 node) { return bot->GetTaxi().IsTaximaskNodeKnown(node); }
inline uint32 TaxiPathNodeMapIdCompat(TaxiPathNodeList const& nodes, std::size_t i) { return nodes[i].mapid; }
inline float TaxiPathNodeXCompat(TaxiPathNodeList const& nodes, std::size_t i) { return nodes[i].x; }
inline float TaxiPathNodeYCompat(TaxiPathNodeList const& nodes, std::size_t i) { return nodes[i].y; }
inline float TaxiPathNodeZCompat(TaxiPathNodeList const& nodes, std::size_t i) { return nodes[i].z; }
inline uint32 TaxiPathNodeDelayCompat(TaxiPathNodeList const& nodes, std::size_t i) { return nodes[i].delay; }

// The start position of a race and class: the T fields.
inline uint32 PlayerInfoMapIdCompat(PlayerInfo const* info) { return info->mapId; }
inline float PlayerInfoXCompat(PlayerInfo const* info) { return info->positionX; }
inline float PlayerInfoYCompat(PlayerInfo const* info) { return info->positionY; }
inline float PlayerInfoZCompat(PlayerInfo const* info) { return info->positionZ; }
inline float PlayerInfoOrientationCompat(PlayerInfo const* info) { return info->orientation; }
inline uint32 TravelHighElfRaceCompat() { return RACE_HIGH_ELF; }

// The hold's fight gates: the T calls.
inline bool CreatureCanInitiateAttackCompat(Creature* creature, Unit* /*target*/) { return creature->CanInitiateAttack(); }
inline float CombatReachToCompat(Unit* from, Unit* to) { return from->GetCombatReach(to, false, 0.0f); }
