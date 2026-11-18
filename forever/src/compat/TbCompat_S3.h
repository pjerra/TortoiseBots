// ForeverCore side of the both-core helpers and names of slice 3 (group fight on orders: threat/tank, pull, orders).
// Owned by the slice-3 porting worker: no other package edits this file. Turtle side: ai/cmangos-compat-s3.h.
// Notes: forever/docs/SLICE3-P-NOTES.md (every helper below, with the file:line of the core call it makes).
#pragma once

#include <shared_mutex>

// ===============================================================================================================
// THREAT AND TANK (ThreatStrategy.cpp)
// Already there and reused (no second helper): GuidKeyCompat (the one uint64 key of a guid, TbCompat_Unit.h:396: the `uint64`-keyed log
// tables and the seek-cycle snapshot of ThreatStrategy.cpp), CurrentThreatVictimCompat, ForEachThreatenerCompat and
// ForEachThreatenerThreatCompat (slice 1). The `getThreat` spelling is the shim's `#define getThreat GetThreat` (forever-compat-shim.h:148).
//
// THREAT CALLS OF THIS PACKAGE: every one is a pure READ of the core's ThreatManager (the engine runs on the world thread before the maps
// update, World.cpp:2465-2475, and must not change the core's own choice of victim). See SLICE3-P-NOTES.md section 3 for the table.
// ===============================================================================================================

// The flat threat a spell adds on top of its damage or healing (the spell threat table). T `SpellThreatEntry::threat` (SpellMgr.h:108,
// uint16, the `spell_threat` row's flat value); F `SpellThreatEntry::flatMod` (SpellMgr.h:362, int32, "flat threat-value for this Spell",
// the same row). Only the field name and the sign range differ: the ThreatCap interrupt rule adds it to the bot's own threat as a float.
template <class ThreatEntry>
inline float SpellFlatThreatCompat(ThreatEntry const& entry) { return static_cast<float>(entry.flatMod); }

// A visual the `debug threat` strategy plays on the bot to show its threat band (high / medium). T `Unit::SendPlaySpellVisual(id)`
// (Unit.cpp:11476: SMSG_PLAY_SPELL_VISUAL, guid + a SpellVisualKit.dbc index of the 1.12 client). F has no call with that meaning:
// `Unit::SendPlaySpellVisual(target, spellVisualId, ...)` (Unit.cpp:12952) names a spell visual id of the new client and a target, and
// `SendPlaySpellVisualKit(id, type, duration)` (:12989) takes a kit id of the new client's DB2, so the two 1.12 numbers (6372, 5036)
// would show nothing or something unrelated. Unported feature "debug spell visuals": counted, nothing shown (a debug aid; the bot does
// less, never more).
inline void DebugSpellVisualCompat(Unit* /*unit*/, uint32 /*kitId*/) { tbcompat::Unported(tbcompat::Feature::DebugVisuals); }

// ===============================================================================================================
// PULL (PullTriggers.cpp)
// ===============================================================================================================

// The online player whose guid key (GuidKeyCompat) is `key`; null when there is none. T: the key IS the guid (GetRawValue), so the
// lookup is the original `sObjectMgr.GetPlayer(ObjectGuid(key))`. F: the key is a hash of the 128-bit guid and cannot be turned back
// into a guid (TbCompat_Unit.h:391-395), so the online players are walked and the one with that key is returned, under the container's
// own shared lock (ObjectAccessor.h:84-91: "when using this, you must use the hashmapholder's lock"). Only reached while an engage
// window runs (`PullHoldExpiredTrigger`, PullTriggers.cpp:182), which is opened by the command layer (commands/Action.cpp:1519), not
// in this slice: one scan per tick per held bot at most. Same player, same answer as T (in the world, else null).
inline Player* PlayerOfGuidKeyCompat(uint64 key)
{
    if (!key)
        return nullptr;
    std::shared_lock<std::shared_mutex> lock(*HashMapHolder<Player>::GetLock());
    for (auto const& entry : ObjectAccessor::GetPlayers())
    {
        Player* player = entry.second;
        if (player && player->IsInWorld() && GuidKeyCompat(entry.first) == key)
            return player;
    }
    return nullptr;
}

// ===============================================================================================================
// MASTER (PlayerbotAI.cpp, the master adoption step of DoNextAction)
// ===============================================================================================================

// Does a commanded bot (BotCommands::IsInCommandedGroup) keep the master it was bound to instead of adopting the group leader? F: yes. The commander of
// a console or GM-less setup is a bot session, which HasActivePlayerMaster() does not count, so the adoption step (PlayerbotAI.cpp: `!master ||
// !HasActivePlayerMaster()`) runs and, for a leader that is a bot, hands the master over to the leader (M package, live: four of five bots of the party
// led by the tank lost the commander on the first tick). The strip rule is already skipped for a commanded bot; the adoption must be too.
// "Different answer": T keeps the original expression (always false here), so Turtle behaviour is unchanged.
inline bool CommandedBotKeepsMasterCompat(bool commanded) { return commanded; }
