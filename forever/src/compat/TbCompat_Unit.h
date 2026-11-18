// Both-core helpers for unit, map and session calls whose shape differs (slice 1, round R3). Included from
// forever-compat-shim.h. The Turtle counterpart, with the same names and signatures that forward to the old calls, is
// ai/cmangos-compat-r3.h, so the shared bot code reads the same on both cores.
//
// Each helper says what the Turtle call was and what ForeverCore answers it with (T = Turtle core src/game, F = pinned
// ForeverCore src/server/game).
#pragma once

// (PhasingHandler.h is one of the core headers botpch_forever.h includes before the shim.)

// Line of sight between two objects of one map. T Object.cpp:1514: `IsWithinLOSInMap(obj, bool checkDynLos)`, the bool adds
// the dynamic (game object) models to the static ones. F Object.h:364 takes LineOfSightChecks: static data is VMAP, game
// object models are GOBJECT (SharedDefines.h:7475-7481), so true is both and false the static data alone.
inline bool IsWithinLOSInMapCompat(WorldObject const* from, WorldObject const* to, bool checkDynLos = true)
{
    return from->IsWithinLOSInMap(to, checkDynLos ? LINEOFSIGHT_ALL_CHECKS : LINEOFSIGHT_CHECK_VMAP);
}

// Line of sight between two points of a map. T Map.cpp:3047 `isInLineOfSight(x1..z2, checkDynLos)`; F Map.h:509 also asks
// for a phase shift and the ignore flags. A map-only caller has no viewer, so the empty phase shift (what a viewer without
// phases sees) and no ignored models.
inline bool IsInLineOfSightCompat(Map const* map, float x1, float y1, float z1, float x2, float y2, float z2, bool checkDynLos = true)
{
    return map->isInLineOfSight(PhasingHandler::GetEmptyPhaseShift(), x1, y1, z1, x2, y2, z2,
        checkDynLos ? LINEOFSIGHT_ALL_CHECKS : LINEOFSIGHT_CHECK_VMAP, VMAP::ModelIgnoreFlags::Nothing);
}

// Height of the ground at a point. T Map.h:568 `GetHeight(x, y, z, vmap, maxSearchDist)`, F Map.h:330 the same plus the phase
// shift (the game-object floors depend on it); the default search distance is 10 yards in both (T GridMap.h:38).
inline float GetHeightCompat(Map* map, float x, float y, float z, bool vmap = true, float maxSearchDist = DEFAULT_HEIGHT_SEARCH)
{
    return map->GetHeight(PhasingHandler::GetEmptyPhaseShift(), x, y, z, vmap, maxSearchDist);
}

// The bearing from one object to another, 0..2*pi. T Object.cpp:1650-1668 (a null target gives 0), F Position.h:
// GetAbsoluteAngle (atan2 normalised to 0..2*pi).
inline float GetAngleCompat(WorldObject const* from, WorldObject const* to)
{
    return to ? from->GetAbsoluteAngle(to) : 0.0f;
}

// Whether the session belongs to a connected player and not to a headless bot. The Turtle core's WorldSession has
// HasNetworkTransport (a socket is attached); ForeverCore keeps the same fact as the flag the bot session sets when it is
// made (WorldSession.h:1078, TbBotSession.cpp). A null session has no transport.
inline bool HasNetworkTransportCompat(WorldSession const* session) { return session && !session->IsBot(); }

// The creature's home point. T Creature.h:579 `GetRespawnCoord(x, y, z, &ori, &dist)`: where it respawns and the wander
// distance; F Creature.h:115 `GetRespawnPosition(&dist)` returns the same point as a Position.
inline void GetRespawnCoordCompat(Creature const* creature, float& x, float& y, float& z, float* ori = nullptr, float* dist = nullptr)
{
    Position const pos = creature->GetRespawnPosition(dist);
    x = pos.GetPositionX();
    y = pos.GetPositionY();
    z = pos.GetPositionZ();
    if (ori)
        *ori = pos.GetOrientation();
}

// How far a melee swing from `self` reaches `victim`. T Unit.cpp:11035 `GetCombatReach(victim, ability, flat_mod)`: both
// combat reaches plus a flat part plus the melee-leeway bonus; F Unit.cpp:749 `GetMeleeRange(victim)`: both combat reaches
// plus 4/3, at least the nominal melee range. The core's own answer to the same question; the flat part is added on top,
// the ability flag only selects Turtle's leeway and has no ForeverCore counterpart.
inline float GetCombatReachCompat(Unit const* self, Unit const* victim, bool /*ability*/, float flatMod)
{
    return self->GetMeleeRange(victim) + flatMod;
}

// The three "real player / same group" helpers the Turtle shim defines (ai/cmangos-compat-shim.h:1053-1057), here over the
// core's own group and session calls. T Group::IsMember takes the guid (ai/cmangos-compat-shim.h), F Group.h IsMember(ObjectGuid).
inline bool isRealPlayer_Helper(Player* p) { return p && p->GetSession() && HasNetworkTransportCompat(p->GetSession()); }
inline bool IsRealPlayer_Helper(Player* p) { return isRealPlayer_Helper(p); }
inline bool IsInGroup_Helper(Player* a, Player* b, bool sameGroup = false)
{
    if (!a || !b)
        return false;
    Group* group = a->GetGroup();
    if (!group)
        return false;
    if (sameGroup)
        return group == b->GetGroup();
    return group->IsMember(b->GetGUID());
}

// Whether `target` lies in the arc in front of `self`. T Object.h:944 `HasInArc(target, arcangle, offset)`, F Position.h
// `HasInArc(arcangle, pos, border = 2)`: both test the target's bearing against the facing within +-arcangle/2 (the default
// border 2 halves the arc), the arguments come in the other order.
inline bool HasInArcCompat(WorldObject const* self, WorldObject const* target, float arcAngle)
{
    return self->HasInArc(arcAngle, target);
}
