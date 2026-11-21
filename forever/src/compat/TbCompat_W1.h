// ForeverCore side of the both-core helpers of the world-life slice W1 (grind targets, the open-world corpse run, the spirit
// healer). Turtle side: ai/cmangos-compat-w1.h (same names and signatures; there each helper is the expression the bot always
// made). Both are found through ai/playerbot/TbCompatW1.h (`__has_include`), so the shared shim headers stay untouched.
//
// The rules this pair carries (spec 1.4 and decision D2): the ONE teleport of W1 is the host's ghost rescue, so the corpse run
// and the spirit-healer leg only walk; the spirit healer revives through the core's own handler (sickness, durability, bones);
// a corpse is never cross-map; and the Turtle side keeps every legacy teleport because these helpers answer true there.
#pragma once

#include <list>

#include "Globals/ObjectMgr.h"
#include "Grids/Cells/CellImpl.h"
#include "Grids/Notifiers/GridNotifiers.h"
#include "Maps/Map.h"
#include "MiscPackets.h"          // WorldPackets::Misc::ReclaimCorpse
#include "NPCPackets.h"            // WorldPackets::NPC::SpiritHealerActivate

// ===============================================================================================================
// GRIND TARGETS (GrindTargetValue.cpp)
// ===============================================================================================================

// `bot->isHonorOrXPTarget(unit)`: does killing this unit still pay experience (no grey)? F spells it with a small i.
inline bool IsHonorOrXPTargetCompat(Player* bot, Unit* unit)
{
    return bot->isHonorOrXPTarget(unit);
}

// The units in range an unfriendly check answers for, in one call: F's UnitListSearcher takes the searcher object first and the
// check by reference (ForeverCore's grid notifiers), Turtle's takes the container first.
inline void VisitUnfriendlyUnitsCompat(Player* bot, float range, std::list<Unit*>& units)
{
    Trinity::AnyUnfriendlyUnitInObjectRangeCheck check(bot, bot, range);
    Trinity::UnitListSearcher<Trinity::AnyUnfriendlyUnitInObjectRangeCheck> searcher(bot, units, check);
    Cell::VisitAllObjects(bot, searcher, range);
}

// Does killing this creature lower a reputation (the core's ReputationOnKill rows)? Used only by the autonomous-alt rule, which
// the host answers false for every host bot; the read still compiles and is data-only.
inline bool KillLowersReputationCompat(uint32 entry)
{
    ReputationOnKillEntry const* rep = ObjectMgr::instance()->GetReputationOnKilEntry(entry);
    return rep && (rep->RepValue1 < 0 || rep->RepValue2 < 0);
}

// Whether the unit offers a service (UNIT_NPC_FLAG_*): a vendor, trainer, quest giver, flight master or innkeeper. T reads the update
// field the old names use; F has Unit::HasNpcFlag (Unit.h:1026) over the same flag.
inline bool UnitHasNpcFlagW1Compat(Unit* unit, uint32 flag)
{
    return unit && unit->HasNpcFlag(static_cast<NPCFlags>(flag));
}

// W1: `FreeGrindPolicy.h` is this core's rule only. On Turtle `IsFreeBot` is also true for every pool random bot, which today may fight
// opposing-faction players and other NPCs; the Turtle side of the pair answers false and keeps its original pick.
inline bool FreeBotStartFightRuleCompat()
{
    return true;
}


// ===============================================================================================================
// GRAVEYARDS AND MAPS (DeadValues.cpp, ReviveFromCorpseAction.cpp)
// ===============================================================================================================

// The closest graveyard for a location, team-conditioned through `who` (F conditions and phases are checked against the player).
inline WorldSafeLocsEntry const* ClosestGraveyardCompat(WorldObject* who, uint32 mapId, float x, float y, float z, uint32 team)
{
    return ObjectMgr::instance()->GetClosestGraveyard(WorldLocation(mapId, x, y, z), team, who);
}

inline uint32 SafeLocMapIdCompat(WorldSafeLocsEntry const* entry)
{
    return entry ? entry->Loc.GetMapId() : 0;
}

inline float SafeLocXCompat(WorldSafeLocsEntry const* entry)
{
    return entry ? entry->Loc.GetPositionX() : 0.0f;
}

inline float SafeLocYCompat(WorldSafeLocsEntry const* entry)
{
    return entry ? entry->Loc.GetPositionY() : 0.0f;
}

inline float SafeLocZCompat(WorldSafeLocsEntry const* entry)
{
    return entry ? entry->Loc.GetPositionZ() : 0.0f;
}

inline MapEntry const* MapEntryW1Compat(uint32 mapId)
{
    return sMapStore.LookupEntry(mapId);
}

inline bool MapIsDungeonCompat(uint32 mapId)
{
    MapEntry const* entry = MapEntryW1Compat(mapId);
    return entry && entry->IsDungeon();
}

inline bool MapInstanceableCompat(uint32 mapId)
{
    MapEntry const* entry = MapEntryW1Compat(mapId);
    return entry && entry->Instanceable();
}

// The map an instance is entered from in ghost mode (Turtle: MapEntry::ghostEntranceMap; F: CorpseMapID, -1 when none).
inline uint32 MapGhostEntranceMapCompat(uint32 mapId)
{
    MapEntry const* entry = MapEntryW1Compat(mapId);
    return entry && entry->CorpseMapID >= 0 ? uint32(entry->CorpseMapID) : 0;
}

// ===============================================================================================================
// AREA TRIGGERS (the instance entrance scan: unreachable on F, W1 corpses are never cross-map)
// ===============================================================================================================

inline uint32 AreaTriggerCountCompat()
{
    return sAreaTriggerStore.GetNumRows();
}

inline AreaTriggerEntry const* AreaTriggerAtCompat(uint32 id)
{
    return sAreaTriggerStore.LookupEntry(id);
}

inline AreaTriggerTeleport const* AreaTriggerTeleportCompat(uint32 id)
{
    return ObjectMgr::instance()->GetAreaTrigger(id);
}

inline uint32 AreaTriggerIdCompat(AreaTriggerEntry const* entry)
{
    return entry ? entry->ID : 0;
}

inline uint32 AreaTriggerMapIdCompat(AreaTriggerEntry const* entry)
{
    return entry ? entry->ContinentID : 0;
}

inline float AreaTriggerRadiusCompat(AreaTriggerEntry const* entry)
{
    return entry ? entry->Radius : 0.0f;
}

inline void AreaTriggerPosCompat(AreaTriggerEntry const* entry, float& x, float& y, float& z)
{
    x = entry ? entry->Pos.X : 0.0f;
    y = entry ? entry->Pos.Y : 0.0f;
    z = entry ? entry->Pos.Z : 0.0f;
}

inline uint32 AreaTeleportMapIdCompat(AreaTriggerTeleport const* at)
{
    return at ? at->Loc.GetMapId() : 0;
}

inline void AreaTeleportPosCompat(AreaTriggerTeleport const* at, float& x, float& y, float& z)
{
    x = at ? at->Loc.GetPositionX() : 0.0f;
    y = at ? at->Loc.GetPositionY() : 0.0f;
    z = at ? at->Loc.GetPositionZ() : 0.0f;
}

inline float AreaTeleportOrientationCompat(AreaTriggerTeleport const* at)
{
    return at ? at->Loc.GetOrientation() : 0.0f;
}

// ===============================================================================================================
// DEATH AND REVIVAL (ReviveFromCorpseAction.cpp, DeadValues.cpp, ReleaseSpiritAction.h)
// ===============================================================================================================

// W1 cuts every corpse-run teleport (spec 1.4): the ghost walks, and the only remaining teleport is the host's rescue. Turtle
// keeps its legacy hops.
inline bool CorpseRunTeleportsEnabledCompat()
{
    return false;
}

// W1 corpses are never cross-map (spec 1.4: F revives inside dungeons at the entrance, so the instance-entrance scan and the
// cross-map corpse walk are cut). Turtle keeps them.
inline bool CorpseRunCrossMapEnabledCompat()
{
    return false;
}

// The core's reclaim handler, with F's typed packet (MiscHandler.cpp:472-505). True only when the bot is alive afterwards: the
// core drops the request silently for a missing corpse, the reclaim delay or a ghost outside CORPSE_RECLAIM_RADIUS.
inline bool ReclaimCorpseCompat(Player* bot)
{
    if (!bot || !bot->GetSession())
        return false;
    WorldPackets::Misc::ReclaimCorpse packet{ WorldPacket(CMSG_RECLAIM_CORPSE) };
    packet.CorpseGUID = bot->GetCorpse() ? bot->GetCorpse()->GetGUID() : ObjectGuid::Empty;
    bot->GetSession()->HandleReclaimCorpse(packet);
    return bot->IsAlive();
}

// F revives a ghost through the core's spirit-healer handler (sickness, 25% durability, bones, Player.cpp:4966) instead of a
// direct ResurrectPlayer. Turtle keeps the direct revive: this answers false there and the legacy block runs.
inline bool SpiritHealerCoreHandlerCompat()
{
    return true;
}

inline bool UnitIsSpiritHealerCompat(Unit* unit)
{
    return unit && unit->IsSpiritHealer();
}

// The core's own interact reach (GetNPCIfCanInteractWith: combat reach + 4 yd, Player.cpp:1990).
inline bool SpiritHealerInReachCompat(Player* bot, Unit* healer)
{
    Creature* creature = healer ? healer->ToCreature() : nullptr;
    return bot && creature && bot->IsWithinDistInMap(creature, creature->GetCombatReach() + 4.0f);
}

inline bool ActivateSpiritHealerCompat(Player* bot, Unit* healer)
{
    if (!bot || !healer || !bot->GetSession())
        return false;
    WorldPackets::NPC::SpiritHealerActivate packet{ WorldPacket(CMSG_SPIRIT_HEALER_ACTIVATE) };
    packet.Healer = healer->GetGUID();
    bot->GetSession()->HandleSpiritHealerActivate(packet);
    return bot->IsAlive();
}

// The core's release/rescue step: the ghost goes to the graveyard of its own team (Player::RepopAtGraveyard). The host's ghost
// rescue is its only caller in W1.
inline bool RepopAtGraveyardCompat(Player* bot)
{
    if (!bot || !bot->FindMap())
        return false;
    bot->RepopAtGraveyard();
    return true;
}

// F's BuildPlayerRepop refuses a dead, non-ghost player that still has a corpse on the same map (Player.cpp:4714-4719): the bot
// would wedge in front of its own body after a relogin. A player wakes up as bones first. Turtle never had the guard.
inline bool StaleCorpseBonesCompat(Player* bot)
{
    if (!bot || bot->IsAlive() || bot->HasPlayerFlag(PLAYER_FLAGS_GHOST) || !bot->HasCorpse())
        return false;
    if (bot->GetCorpseLocation().GetMapId() != bot->GetMapId())
        return false;
    bot->SpawnCorpseBones();
    return true;
}

// Is this bot's body stuck on a corpse the core will refuse to release past (a dead, non-ghost player with a corpse on its own
// map)? W1: `auto release` may run in that state after all (it wakes the body as bones first); Turtle: never (unchanged).
inline bool StaleCorpsePresentCompat(Player* bot)
{
    return bot && !bot->IsAlive() && !bot->HasPlayerFlag(PLAYER_FLAGS_GHOST) && bot->HasCorpse() &&
        bot->GetCorpseLocation().GetMapId() == bot->GetMapId();
}

inline bool MotionStackEmptyCompat(MotionMaster* motion)
{
    return !motion || motion->Empty();
}

// Player::SendHeartBeat is a Turtle heartbeat after a ghost teleport; F has no such call (movement interrupts are typed).
inline void HeartBeatCompat(Player* /*bot*/)
{
}

// Told by the engine's two revive paths so `.tb state`/the [W1] line can name how the bot came back: 0 reclaim, 1 healer.
// Defined in forever/src/TbBotMgr.cpp (the host owns the per-bot record); a no-op for a character the host does not know.
void NoteReviveKindCompat(Player* bot, int kind);

// W1 cuts the post-revive relocation (spec 1.4: `RelocateHopelessBot` and the lowbie home are never). On F the calls do nothing.
inline void RelocateHopelessBotCompat(Player* /*bot*/)
{
}

inline void SendStrandedLowbieHomeCompat(Player* /*bot*/)
{
}
