// The Turtle side of the both-core helpers the ForeverCore port added in slice 3 (group fight on orders).
// Same names and signatures as forever/src/compat/TbCompat_S3.h; each forwards to the call the bot always made.
// Owned by the slice-3 porting worker: no other package edits this file.
#pragma once

// ===============================================================================================================
// THREAT AND TANK (ThreatStrategy.cpp)
// ===============================================================================================================

// SpellThreatEntry::threat, the flat threat of a spell (SpellMgr.h:108).
// (a template: the shim is included before SpellMgr.h declares SpellThreatEntry on some include orders)
template <class ThreatEntry>
inline float SpellFlatThreatCompat(ThreatEntry const& entry) { return entry.threat; }

// Unit::SendPlaySpellVisual(id): the debug threat strategy's visual (Unit.h:687).
inline void DebugSpellVisualCompat(Unit* unit, uint32 kitId) { unit->SendPlaySpellVisual(kitId); }

// ===============================================================================================================
// PULL (PullTriggers.cpp)
// ===============================================================================================================

// The online player with this guid key: T's key is the raw guid, so this is the original `sObjectMgr.GetPlayer(ObjectGuid(key))`.
inline Player* PlayerOfGuidKeyCompat(uint64 key) { return sObjectMgr.GetPlayer(ObjectGuid(key)); }

// ===============================================================================================================
// MASTER (PlayerbotAI.cpp, the master adoption step of DoNextAction)
// ===============================================================================================================

// Does a commanded bot (BotCommands::IsInCommandedGroup) keep the master it was bound to instead of adopting the group leader? T: no helper effect,
// the step reads exactly what it always read (a commander there is a network player, which HasActivePlayerMaster() counts, so the step does not run).
inline bool CommandedBotKeepsMasterCompat(bool /*commanded*/) { return false; }
