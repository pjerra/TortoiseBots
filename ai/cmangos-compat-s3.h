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

// The unit a bot follows and measures "distance to master" from (the value `master target`): T: the group master, as it always was.
// (a template so that PlayerbotAI need not be complete where this header is read)
template <class AI>
inline Unit* MasterTargetCompat(AI* ai) { return ai->GetGroupMaster(); }

// Does the landing of a BODY pull (the `reach pull` branch of PullAction) restamp the held party bots' clock? T: never (the legacy body pull did not).
template <class B>
inline bool BodyPullRestampCompat(B* /*bot*/) { return false; }

// The items a keyword like `food` / `drink` resolves to in the bags, as the ids `UseAction::Execute` uses (the first is the item, a SECOND one is the item
// it is used ON). T: every stack, as it always was.
template <class Items, class ItemIds>
inline void AppendInventoryUseItemsCompat(ItemIds& ids, Items const& inventoryItems)
{
    for (auto* inventoryItem : inventoryItems)
        if (inventoryItem)
            ids.push_back(inventoryItem->GetEntry());
}
