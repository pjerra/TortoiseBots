// The Turtle side of the both-core helpers the ForeverCore port added in slice 1, round R3 (forever/src/compat/TbCompat_Unit.h).
// Same names and signatures; each forwards to the call the bot always made, so nothing changes on this core.
// Included at the end of ai/cmangos-compat-shim.h.
#pragma once

inline bool IsWithinLOSInMapCompat(WorldObject const* from, WorldObject const* to, bool checkDynLos = true)
{
    return from->IsWithinLOSInMap(to, checkDynLos);
}

inline bool IsInLineOfSightCompat(Map const* map, float x1, float y1, float z1, float x2, float y2, float z2, bool checkDynLos = true)
{
    return map->isInLineOfSight(x1, y1, z1, x2, y2, z2, checkDynLos);
}

inline float GetHeightCompat(Map* map, float x, float y, float z, bool vmap = true, float maxSearchDist = DEFAULT_HEIGHT_SEARCH)
{
    return map->GetHeight(x, y, z, vmap, maxSearchDist);
}

inline float GetAngleCompat(WorldObject const* from, WorldObject const* to)
{
    return from->GetAngle(to);
}

inline bool HasNetworkTransportCompat(WorldSession const* session)
{
    return session->HasNetworkTransport();
}

inline void GetRespawnCoordCompat(Creature const* creature, float& x, float& y, float& z, float* ori = nullptr, float* dist = nullptr)
{
    creature->GetRespawnCoord(x, y, z, ori, dist);
}

inline float GetCombatReachCompat(Unit const* self, Unit const* victim, bool ability, float flatMod)
{
    return self->GetCombatReach(victim, ability, flatMod);
}

inline bool HasInArcCompat(WorldObject const* self, WorldObject const* target, float arcAngle)
{
    return self->HasInArc(target, arcAngle);
}
