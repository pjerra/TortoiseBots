// ForeverCore side of the both-core helpers and names of slice 1, round R4A (actions, triggers, generic strategies, warrior).
// Owned by package R4A: no other package edits this file. Turtle side: ai/cmangos-compat-r4a.h (same names and signatures,
// each forwarding to the call the bot always made).
//
// T = the Turtle core src/game (~/playerbots-references/penqle-tortoise-wow-modsys/src/game), F = the pinned ForeverCore
// src/server/game. Where the two cores answer the same question with different numbers the entry says so (DIFFERENT ANSWER),
// and the line is also in forever/docs/SLICE1-R4A-NOTES.md under "Different answers".
#pragma once

// The VMAP manager class: the core's VMapFactory.h only forward-declares it, and the module reads its floor height directly
// (MovementActions.cpp magma guard). Its header also defines VMAP_INVALID_HEIGHT_VALUE.
#include "VMapManager.h"

// The parsed pet packets the pet command helpers fill (section 12); the core's WorldSession.h only forward-declares them.
#include "PetPackets.h"

// Reputation state and the charm info of a pet: Unit.h and Player.h only forward-declare them.
#include "ReputationMgr.h"
#include "CharmInfo.h"

// The swing timer of a weapon slot in ms. T Unit.h:1074, F Unit.h:717 (getAttackTimer).
#define GetAttackTimer getAttackTimer
// "Is this a raid group". The Turtle shim has the same rename (ai/cmangos-compat-shim.h:1045); F Group.h:303 isRaidGroup.
#define IsRaidGroup isRaidGroup
// The unit guid a cast aims at. T Spell.h:142, F SpellDefines.h:385 GetUnitTargetGUID.
#define getUnitTargetGuid GetUnitTargetGUID
// The unit's mount display id (0 = not mounted). T Unit.h:579 (UNIT_FIELD_MOUNTDISPLAYID), F Unit.h:930.
#define GetMountID GetMountDisplayId
// Write the selected-target guid of a player. T Unit.h:1351 SetTargetGuid writes UNIT_FIELD_TARGET; F Player.h:1979 SetSelection
// writes UnitData::Target, the same field (F Player::SetTarget is a no-op for players, Player.h:1978). The shim already maps
// SetSelectionGuid to it (forever-compat-shim.h:134).
#define SetTargetGuid SetSelection
// cmangos called the taxi flight motion TAXI_MOTION_TYPE; the Turtle shim maps it to FLIGHT_MOTION_TYPE (cmangos-compat-shim.h:513),
// the same value 7 on both cores (T MotionMaster.h:37-58, F MovementDefines.h:42).
#define TAXI_MOTION_TYPE FLIGHT_MOTION_TYPE

// Item spell trigger "use without an equip cooldown" (T ItemPrototype.h:52, value 5). ForeverCore's item effects have no such
// trigger (F ItemTemplate.h:126-133: 5 is ON_PICKUP, a different thing), and the item mirror hands F's trigger numbers through
// (TbItemPrototype.cpp:325), so a comparison with this name has to be false for every item: the value is not an F trigger number.
// DIFFERENT ANSWER: an item whose Turtle trigger was 5 reads as its F trigger.
constexpr uint32 ITEM_SPELLTRIGGER_ON_NO_DELAY_USE = 0xFFFFFFFFu;

// The last value of the spell state enum. T Spell.h:206-214 ends with SPELL_STATE_DELAYED (5); F Spell.h:254-262 ends with
// SPELL_STATE_IDLE (5). The bot only uses it as the upper bound of `state <= last`.
constexpr SpellState SpellStateLastCompat = SPELL_STATE_IDLE;

// The size a world object adds to distances. T Object.h:862 (the default DEFAULT_WORLD_OBJECT_SIZE, overridden by units with the
// bounding radius field and by game objects with their display size); F has the bounding radius on Unit only (Unit.h:724) and
// says the player radius is "also used for any non Unit world objects" (ObjectDefines.h:39).
// DIFFERENT ANSWER for a game object or a dynamic object: T its display size, F the player radius.
inline float ObjectBoundingRadiusCompat(WorldObject const* obj)
{
    if (Unit const* unit = obj->ToUnit())
        return unit->GetBoundingRadius();
    return DEFAULT_PLAYER_BOUNDING_RADIUS;
}

// A unit's melee reach. T Unit.cpp:11024-11033: the combat reach, at least 1.5; F has no separate melee reach (the combat reach
// field, Unit.h:722), so the same clamp is applied to it. T's 1.5 is the Turtle value (F's own minimum, MIN_MELEE_REACH, is 2.0
// and is applied by Unit::GetMeleeRange, which GetCombatReachCompat uses).
inline float MeleeReachCompat(Unit const* unit)
{
    return std::max(unit->GetCombatReach(), 1.5f);
}

// ---------------------------------------------------------------------------------------------------------------
// 4. MOVEMENT FLAGS. T names (Object.h:212-238, MOVEFLAG_*) as the F flag (MovementInfo.h:30-83, MOVEMENTFLAG_*) of the same fact.
//    DIFFERENT ANSWER: MOVEFLAG_JUMPING. T has a flag for "in a jump" (0x2000) and one for "fell far" (0x4000); F sets FALLING
//    for the whole airborne stretch, jump or drop alike, so JUMPING reads true for a drop too.
// ---------------------------------------------------------------------------------------------------------------

// Does the unit carry the movement flag. T Unit's HasMovementFlag(flag) reads m_movementInfo; F Unit.h:1770 HasUnitMovementFlag
// (the name HasMovementFlag is MovementInfo's member on F, so no macro).
inline bool HasMovementFlagCompat(Unit const* unit, uint64 flag)
{
    return unit->HasUnitMovementFlag(static_cast<MovementFlags>(flag));
}

// ---------------------------------------------------------------------------------------------------------------
// 5. MOVEMENT: the calls the bot makes on the unit's MotionMaster, grouped so that every place the two cores differ is one
//    function here. Mapping table, Turtle call -> ForeverCore call, with file:line on both cores:
//      MovePointCompat      T MotionMaster.h:136-137 MovePoint(id, x, y, z, options, speed, finalOrientation)
//                           F MotionMaster.h:174 MovePoint(id, x, y, z, generatePath, finalOrient, speed, speedSelectionMode, ...)
//      MoveFollowCompat     T MotionMaster.cpp:425 (Clear(), then FollowMovementGenerator(target, dist, angle))
//                           F MotionMaster.h:163 MoveFollow(target, dist, ChaseAngle) (adds to the active slot, no clear)
//      MoveChaseCompat      T MotionMaster.cpp:405 MoveChase(target, dist, angle)    F MotionMaster.h:166 MoveChase(target, float, float)
//      MotionClearCompat    T MotionMaster.h:112 Clear(reset, all)                   F MotionMaster.h:145-148 Clear() / Clear(slot)
//      MoveSplineInitCompat T MoveSplineInit.h (Unit&, name; MovebyPath, SetWalk, Launch)
//                           F Spline/MoveSplineInit.h:40,51,86,113 (Unit*; MovebyPath(span), SetWalk, Launch)
//      current generator    GetCurrentMovementGeneratorType() is the same call on both (T MotionMaster.h:150 / F MotionMaster.h:130),
//                           the enumerators the bot compares with (CHASE 5, POINT 8, FOLLOW 14 and FLIGHT 7, IDLE 0) have the
//                           same names and values; EFFECT is 15 on T and 16 on F (the name is what the bot uses).
// ---------------------------------------------------------------------------------------------------------------
// The cmangos names the Turtle shim keeps (cmangos-compat-shim.h:795-802): the mode a dispatch walks in.
typedef int ForcedMovement;
#define FORCED_MOVEMENT_RUN 1
#define FORCED_MOVEMENT_WALK 0

// The option bits of MovePoint. T MotionMaster.h:69-79 (MoveOptions); F has no bit word: pathfinding is a bool, walk/run a
// speed selection mode.
enum TbLegacyMoveOption : uint32
{
    MOVE_PATHFINDING = 0x001,
    MOVE_WALK_MODE = 0x002,
    MOVE_RUN_MODE = 0x004,
};

// Move to a point. T: the point generator with the given options; F: MovePoint with path generation on when MOVE_PATHFINDING is set
// and the walk or run selection for MOVE_WALK_MODE / MOVE_RUN_MODE (neither: the unit's own mode). The point id is passed on as is.
// DIFFERENT ANSWER: the generator type and its re-path rules are each core's own (F's PointMovementGenerator re-paths on its
// own events); T's `speed` (0 = unit speed) and final orientation (-10 = none) are never set by the bot and are not carried.
inline void MovePointCompat(MotionMaster& mm, uint32 id, float x, float y, float z, uint32 options)
{
    MovementWalkRunSpeedSelectionMode mode = MovementWalkRunSpeedSelectionMode::Default;
    if (options & MOVE_WALK_MODE)
        mode = MovementWalkRunSpeedSelectionMode::ForceWalk;
    else if (options & MOVE_RUN_MODE)
        mode = MovementWalkRunSpeedSelectionMode::ForceRun;
    mm.MovePoint(id, x, y, z, (options & MOVE_PATHFINDING) != 0, {}, {}, mode);
}

// Follow a unit at a distance and an angle from its facing. T clears the generators above the default one first (and refuses
// while the unit has lost control), then follows; F adds the follow generator, so the clear is done here, the spike's way
// (TbBotMgr.cpp:411-412). DIFFERENT ANSWER: F keeps the follower within +-pi/4 of the asked angle (ChaseAngle tolerance), T
// walks to the exact slot; T's lost-control refusal is F's own (a stunned unit does not move).
inline void MoveFollowCompat(MotionMaster& mm, Unit* target, float dist, float angle)
{
    mm.Clear();
    if (!target)
        return;
    mm.MoveFollow(target, dist, ChaseAngle(angle));
}

// Chase a unit to a distance and an angle. T MotionMaster.cpp:405 and F MotionMaster.h:166 take the same three values.
// DIFFERENT ANSWER: F's ChaseAngle has a +-pi/4 tolerance and its ChaseRange keeps the unit between the asked range and the
// melee/combat reach; T walks to the exact point.
// Found live (R8): the bot asks for distance 0 when it means "melee contact" (ReachMeleeAction, ChaseTo), and T's chase generator stops at the
// attack reach for it. F's float overload turns 0 into ChaseRange(0), which is "to the target's centre": the warrior stood 0.005 to 0.3 yd
// from its victim, inside it. F's own default (no range) is the melee/combat reach, and with no angle the bot closes in from where it
// stands instead of being steered round the target to a +-pi/4 wedge of a bearing T read differently.
inline void MoveChaseCompat(MotionMaster& mm, Unit* target, float dist, float angle)
{
    if (dist <= 0.0f)
    {
        mm.MoveChase(target);
        return;
    }
    mm.MoveChase(target, dist, angle);
}

// Drop movement generators. T Clear(reset, all): pops every generator above the bottom one, and with `all` the bottom one too;
// `reset` re-initialises the remaining one. F Clear() pops the whole active slot and Clear(slot) the default one; there is no
// reset (the default generator is recreated by the core when needed).
inline void MotionClearCompat(MotionMaster& mm, bool /*reset*/, bool all)
{
    mm.Clear();
    if (all)
        mm.Clear(MOTION_SLOT_DEFAULT);
}

// A raw spline the bot launches itself. T `MoveSplineInit(Unit&, name)`, F `MoveSplineInit(Unit*)` (the name is only a debug
// tag on T). The transport form (T SetTransport) is not carried: boarding a transport is left open (see the notes).
class MoveSplineInitCompat
{
public:
    MoveSplineInitCompat(Unit& unit, char const* /*name*/) : _init(&unit) {}
    void MovebyPath(std::vector<G3D::Vector3> const& path) { _init.MovebyPath(path); }
    void SetWalk(bool walk) { _init.SetWalk(walk); }
    int32 Launch() { return _init.Launch(); }

private:
    Movement::MoveSplineInit _init;
};

// ---------------------------------------------------------------------------------------------------------------
// 8. OTHER ENGINE CALLS with a different shape.
// ---------------------------------------------------------------------------------------------------------------
// Summon a creature at a point for a time. T Object.h:1076 (despawn time in ms, a uint32); F Object.h:422 (Milliseconds).
inline Creature* SummonCreatureCompat(WorldObject* owner, uint32 entry, float x, float y, float z, float o, TempSummonType type, uint32 despawnMs)
{
    return owner->SummonCreature(entry, x, y, z, o, type, Milliseconds(despawnMs));
}

// Teleport a player to a point. T Player.h:2057 (options a uint32, 0 = none), F Player.h:1266 (TeleportToOptions). The bot passes 0
// in every call (grep), the one value both cores mean "no option" by; no other option number is translated.
inline bool TeleportToCompat(Player* player, uint32 mapId, float x, float y, float z, float o, uint32 options)
{
    ASSERT(options == 0);
    return player->TeleportTo(mapId, x, y, z, o, TELE_TO_NONE);
}

// A server float setting the bot reads: how far a yell carries. T `sWorld.getConfig(CONFIG_FLOAT_LISTEN_RANGE_YELL)`
// (World.h:515), F `getFloatConfig(CONFIG_LISTEN_RANGE_YELL)` (World.h:231, :737). The two cores' own configured values.
inline float ListenRangeYellCompat() { return sWorld.getFloatConfig(CONFIG_LISTEN_RANGE_YELL); }

// Stop the unit's emote loop. T Unit.h:643 HandleEmoteState(id) (0 = none); F Unit.h:882 SetEmoteState.
inline void HandleEmoteStateCompat(Unit* unit, uint32 emote)
{
    unit->SetEmoteState(static_cast<Emote>(emote));
}

// A point near an object, free of obstacles. T Object.cpp:2399 GetNearPoint(searcher, x, y, z, searcherBoundingRadius,
// distance2d, absAngle): the point is distance2d + both bounding radii away. F Object.cpp:2777 GetNearPoint(searcher, x, y, z,
// distance2d, absAngle) takes no radius and adds both COMBAT reaches itself (GetNearPoint2D, :2748-2754), so the searcher's
// radius argument is dropped. DIFFERENT ANSWER: the distance differs by (combat reach - bounding radius) of the two units.
inline void GetNearPointCompat(WorldObject const* target, WorldObject const* searcher, float& x, float& y, float& z,
    float /*searcherBoundingRadius*/, float distance2d, float absAngle)
{
    target->GetNearPoint(searcher, x, y, z, distance2d, absAngle);
}

// Does the straight line between two points hit a static collision model. T Map.h:577 FindCollisionModel(...) != nullptr (the static
// model tree); F has the same question as the VMAP line of sight (Map.h:509): no line of sight over the static models.
inline bool LineHitsCollisionModelCompat(Map const* map, float x1, float y1, float z1, float x2, float y2, float z2)
{
    return !map->isInLineOfSight(PhasingHandler::GetEmptyPhaseShift(), x1, y1, z1, x2, y2, z2, LINEOFSIGHT_CHECK_VMAP, VMAP::ModelIgnoreFlags::Nothing);
}

// T Map::IsBattleGround, F Map.h:383 IsBattleground.
inline bool MapIsBattlegroundCompat(Map const* map) { return map->IsBattleground(); }

// The unit the player's client is moving. T Player::GetMover; F Unit.h:1275 GetUnitBeingMoved (the unit under direct client control).
inline Unit* PlayerMoverCompat(Player* player) { return player->GetUnitBeingMoved(); }

// Recompute one speed. T Unit::UpdateSpeed(type, forced) (forced = send the change at once); F Unit.h:1741 UpdateSpeed(type) always
// sends it.
inline void UpdateSpeedCompat(Unit* unit, UnitMoveType type, bool /*forced*/) { unit->UpdateSpeed(type); }

// The icon identity of a spell (the ranks of one spell share it; the bot only compares two of them). T SpellEntry::SpellIconID (an
// index of SpellIcon.dbc), F SpellInfo.h:415 IconFileDataId (the icon's file data id) through the mirror's link to the core spell.
// DIFFERENT ANSWER: another number space; equal for the ranks of one spell on both, but not the same value.
inline uint32 SpellIconIdCompat(SpellEntry const* entry)
{
    SpellInfo const* info = GetSpellInfoCompat(entry);
    return info ? info->IconFileDataId : 0;
}

// Is `self` behind `target`. T Unit.cpp:3242 IsBehindTarget(target, strict): not within the target's front arc (pi); the strict form
// also reads a creature's facing (its victim and its cast target guid). F has isInBack on the other unit with its own arc rule
// (Object.cpp:674) and no creature cast-target guid. The bot calls it with strict = false only, which is carried as
// !HasInArc(target -> self, pi) (HasInArcCompat); a strict call is refused.
inline bool IsBehindTargetCompat(Unit const* self, Unit const* target, bool strict)
{
    ASSERT(!strict);
    return !HasInArcCompat(target, self, float(M_PI));
}

// ---------------------------------------------------------------------------------------------------------------
// 10. GROUPS.
// ---------------------------------------------------------------------------------------------------------------
// Walk a group's member references the old way: `for (ref = first; ref; ref = next)`. T Group::GetFirstMember() and the reference's
// next() end with a null pointer; F keeps the references in a ring closed by the list head (LinkedList.h:44, RefManager.h:30-40),
// so the end is detected against the list's end iterator and a null pointer is handed back.
inline GroupReference* GroupFirstMemberCompat(Group* group)
{
    GroupRefManager& members = group->GetMembers();
    return members.begin() == members.end() ? nullptr : &*members.begin();
}
inline GroupReference* GroupNextMemberCompat(Group* group, GroupReference* ref)
{
    LinkedListElement* next = ref->next();
    return next == group->GetMembers().end().node() ? nullptr : static_cast<GroupReference*>(next);
}


// The mana a cast costs. T Spell.h:467 GetPowerCost (the one cost of the spell's power type); F Spell.h:713 a list of power costs
// (one per power type). Only the mana entry is read: the bot asks it for mana healers' savings. DIFFERENT ANSWER: a spell paid in
// another power reads 0 here and its cost on T.
inline uint32 SpellManaCostCompat(Spell const* spell)
{
    uint32 mana = 0;
    for (SpellPowerCost const& cost : spell->GetPowerCost())
        if (cost.Power == POWER_MANA && cost.Amount > 0)
            mana += static_cast<uint32>(cost.Amount);
    return mana;
}

// ---------------------------------------------------------------------------------------------------------------
// 12. PET COMMANDS. The bot sends a pet command the way a client would. T builds the packet CMSG_PET_ACTION (pet guid, action word,
//     target guid) and hands it to the session's handler; F's handler takes the parsed packet (Server/Packets/PetPackets.h:192-203,
//     :51-59), so the three values fill its fields. The action word is the same on both cores (the button type in the top byte,
//     ACT_COMMAND / ACT_REACTION, and the command in the low 24 bits).
// ---------------------------------------------------------------------------------------------------------------
inline void HandlePetActionCompat(WorldSession* session, ObjectGuid const& petGuid, uint32 action, ObjectGuid const& targetGuid)
{
    WorldPackets::Pet::PetAction packet{ WorldPacket(CMSG_PET_ACTION) };
    packet.PetGUID = petGuid;
    packet.Action = action;
    packet.TargetGUID = targetGuid;
    session->HandlePetAction(packet);
}

inline void HandlePetAbandonCompat(WorldSession* session, ObjectGuid const& petGuid)
{
    WorldPackets::Pet::PetAbandon packet{ WorldPacket(CMSG_PET_ABANDON) };
    packet.Pet = petGuid;
    session->HandlePetAbandon(packet);
}

// The scope guid the group buff claims use. T builds a synthetic guid out of a tag and the group id (Group.h:215); F uses the
// group's own guid (Group.h:312), which no unit or player can share. The claims only compare it and take its raw value.
inline ObjectGuid GroupScopeGuidCompat(Group const* group, uint64 /*tag*/) { return group->GetGUID(); }

// ---------------------------------------------------------------------------------------------------------------
// 13. REPUTATION flags. T ReputationMgr.h:32-38 (FACTION_FLAG_*, a bit word on the state); F ReputationMgr.h:33-45 (ReputationFlags in
//     an EnumFlag). The legacy numbers are kept as names; the test maps each to the F flag of the same fact.
//     DIFFERENT ANSWER: FACTION_FLAG_INVISIBLE_FORCED (T 0x08, the server hides the faction of the opposite team) has no F flag (F's 0x08
//     is Header), so it reads false; FACTION_FLAG_PEACE_FORCED reads F's Peaceful (0x10).
// ---------------------------------------------------------------------------------------------------------------
constexpr uint32 FACTION_FLAG_VISIBLE = 0x01;
constexpr uint32 FACTION_FLAG_AT_WAR = 0x02;
constexpr uint32 FACTION_FLAG_HIDDEN = 0x04;
constexpr uint32 FACTION_FLAG_INVISIBLE_FORCED = 0x08;
constexpr uint32 FACTION_FLAG_PEACE_FORCED = 0x10;

inline bool ReputationHasFlagCompat(FactionState const* state, uint32 legacyFlag)
{
    switch (legacyFlag)
    {
        case FACTION_FLAG_VISIBLE: return state->Flags.HasFlag(ReputationFlags::Visible);
        case FACTION_FLAG_AT_WAR: return state->Flags.HasFlag(ReputationFlags::AtWar);
        case FACTION_FLAG_HIDDEN: return state->Flags.HasFlag(ReputationFlags::Hidden);
        case FACTION_FLAG_PEACE_FORCED: return state->Flags.HasFlag(ReputationFlags::Peaceful);
        default: return false;
    }
}
