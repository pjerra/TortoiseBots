// The Turtle side of the both-core helpers the ForeverCore port added in wide pass 2 (branch port/wide-pass-2).
// Same names and signatures as forever/src/compat/TbCompat_W2.h; each is the expression the bot always made (the call is moved,
// not changed). Owned by that pass: no other package edits this file. Notes: forever/docs/WIDE-PASS-R2-NOTES.md.
#pragma once

// Family 1 (names): nothing to define here. The Turtle core has TEXTEMOTE_*, BIND_WHEN_USE, EQUIP_ERR_* and the rest under the
// names the code already uses; the ForeverCore header maps them.

// Family 2 (server settings): the Turtle getter is overloaded on the key's enum; one helper per value type is the same call.
inline uint32 WorldU32ConfigW2(eConfigUInt32Values key) { return sWorld.getConfig(key); }
inline int32 WorldI32ConfigW2(eConfigInt32Values key) { return sWorld.getConfig(key); }
inline bool WorldBoolConfigW2(eConfigBoolValues key) { return sWorld.getConfig(key); }
inline float WorldFloatConfigW2(eConfigFloatValues key) { return sWorld.getConfig(key); }

// Family 3 (a group's id as an in-memory identity): the original call.
inline uint32 GroupIdW2(Group const* group) { return group->GetId(); }

// Family 4 (one component of a spawn record's position): the original field read.
#define TBW2_SPAWN_READ(NAME, TYPE, FIELD) \
    inline TYPE NAME(CreatureData const& data) { return data.position.FIELD; } \
    inline TYPE NAME(GameObjectData const& data) { return data.position.FIELD; }
TBW2_SPAWN_READ(SpawnMapIdW2, uint32, mapId)
TBW2_SPAWN_READ(SpawnXW2, float, x)
TBW2_SPAWN_READ(SpawnYW2, float, y)
TBW2_SPAWN_READ(SpawnZW2, float, z)
TBW2_SPAWN_READ(SpawnOW2, float, o)
#undef TBW2_SPAWN_READ
