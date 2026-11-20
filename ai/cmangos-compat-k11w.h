// The Turtle side of the both-core helpers the ForeverCore port added in slice 11, package K11w (the warlock class code).
// Same names and signatures as forever/src/compat/TbCompat_K11w.h; each is the expression the bot always made (the call is moved, not changed).
// Owned by the K11w worker: no other package edits this file. Notes: forever/docs/SLICE11-NOTES.md.
#pragma once

// CastChoicePetAction::isPossible: the cheat shard, as the call always was (its result was not used).
inline bool AddItemCompat(Player* bot, uint32 itemId, uint32 count) { bot->StoreNewItemInInventorySlot(itemId, count); return true; }

// WarlockBossTarget: the instance-bind flag, as `creature->HasExtraFlag` always read it.
inline bool CreatureExtraFlagCompat(Creature const* creature, CreatureFlagsExtra flag) { return const_cast<Creature*>(creature)->HasExtraFlag(flag); }
