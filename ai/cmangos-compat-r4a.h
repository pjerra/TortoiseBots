// The Turtle side of the both-core helpers the ForeverCore port added in slice 1, round R4A (actions, triggers, generic strategies, warrior).
// Same names and signatures as forever/src/compat/TbCompat_A.h; each forwards to the call the bot always made.
// Owned by package R4A: no other package edits this file.
#pragma once

// The core headers the helpers below name (the shim is included before the bot's own headers pull them in).
#include "Maps/PathFinder.h"
#include "Maps/GridMap.h"
#include "Movement/MotionMaster.h"
#include "Movement/spline/MoveSplineInit.h"
#include "Threat/ThreatManager.h"
#include "ReputationMgr.h"

inline float ObjectBoundingRadiusCompat(WorldObject const* obj)
{
    return obj->GetObjectBoundingRadius();
}

inline float MeleeReachCompat(Unit const* unit)
{
    return unit->GetMeleeReach();
}

// Movement flags.
inline bool HasMovementFlagCompat(Unit const* unit, uint64 flag)
{
    // Player::HasMovementFlag (Player.cpp:22989) is `m_movementInfo.HasMovementFlag(f)`; the bot calls it on its own player.
    return unit->m_movementInfo.HasMovementFlag(static_cast<MovementFlags>(flag));
}

// Movement (the mapping table is in forever/src/compat/TbCompat_A.h section 5).
inline void MovePointCompat(MotionMaster& mm, uint32 id, float x, float y, float z, uint32 options)
{
    mm.MovePoint(id, x, y, z, options);
}

inline void MoveFollowCompat(MotionMaster& mm, Unit* target, float dist, float angle)
{
    mm.MoveFollow(target, dist, angle);
}

inline void MoveChaseCompat(MotionMaster& mm, Unit* target, float dist, float angle)
{
    mm.MoveChase(target, dist, angle);
}

inline void MotionClearCompat(MotionMaster& mm, bool reset, bool all)
{
    mm.Clear(reset, all);
}

// The raw spline is the core's own class.
using MoveSplineInitCompat = Movement::MoveSplineInit;

// Other engine calls.
inline Creature* SummonCreatureCompat(WorldObject* owner, uint32 entry, float x, float y, float z, float o, TempSummonType type, uint32 despawnMs)
{
    return owner->SummonCreature(entry, x, y, z, o, type, despawnMs);
}

inline bool TeleportToCompat(Player* player, uint32 mapId, float x, float y, float z, float o, uint32 options)
{
    return player->TeleportTo(mapId, x, y, z, o, options);
}

inline float ListenRangeYellCompat() { return sWorld.getConfig(CONFIG_FLOAT_LISTEN_RANGE_YELL); }

inline void HandleEmoteStateCompat(Unit* unit, uint32 emote)
{
    unit->HandleEmoteState(emote);
}

inline void GetNearPointCompat(WorldObject const* target, WorldObject const* searcher, float& x, float& y, float& z,
    float searcherBoundingRadius, float distance2d, float absAngle)
{
    target->GetNearPoint(searcher, x, y, z, searcherBoundingRadius, distance2d, absAngle);
}

inline bool LineHitsCollisionModelCompat(Map* map, float x1, float y1, float z1, float x2, float y2, float z2)
{
    return map->FindCollisionModel(x1, y1, z1, x2, y2, z2) != nullptr;
}

inline bool MapIsBattlegroundCompat(Map const* map) { return map->IsBattleGround(); }
inline Unit* PlayerMoverCompat(Player* player) { return player->GetMover(); }
inline void UpdateSpeedCompat(Unit* unit, UnitMoveType type, bool forced) { unit->UpdateSpeed(type, forced); }

inline uint32 SpellIconIdCompat(SpellEntry const* entry) { return entry->SpellIconID; }

inline bool IsBehindTargetCompat(Unit const* self, Unit const* target, bool strict) { return self->IsBehindTarget(target, strict); }

// The last value of the spell state enum (T Spell.h:206-214 ends with SPELL_STATE_DELAYED).
constexpr SpellState SpellStateLastCompat = SPELL_STATE_DELAYED;

// Groups (see forever/src/compat/TbCompat_A.h section 10).
inline GroupReference* GroupFirstMemberCompat(Group* group) { return group->GetFirstMember(); }
inline GroupReference* GroupNextMemberCompat(Group* /*group*/, GroupReference* ref) { return ref->next(); }

inline uint32 SpellManaCostCompat(Spell const* spell) { return spell->GetPowerCost(); }

// Pet commands (see forever/src/compat/TbCompat_A.h section 12): the packet the bot always built.
inline void HandlePetActionCompat(WorldSession* session, ObjectGuid const& petGuid, uint32 action, ObjectGuid const& targetGuid)
{
    WorldPacket packet(CMSG_PET_ACTION);
    packet << petGuid;
    packet << action;
    packet << targetGuid;
    session->HandlePetAction(packet);
}

inline void HandlePetAbandonCompat(WorldSession* session, ObjectGuid const& petGuid)
{
    WorldPacket packet(CMSG_PET_ABANDON);
    packet << petGuid;
    session->HandlePetAbandon(packet);
}

inline ObjectGuid GroupScopeGuidCompat(Group const* group, uint64 tag) { return ObjectGuid(tag | uint64(group->GetId())); }

// Reputation flags (see forever/src/compat/TbCompat_A.h section 13).
inline bool ReputationHasFlagCompat(FactionState const* state, uint32 legacyFlag) { return (state->Flags & legacyFlag) != 0; }
