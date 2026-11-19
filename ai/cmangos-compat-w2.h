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
