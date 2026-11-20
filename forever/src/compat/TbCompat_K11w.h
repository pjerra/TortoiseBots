// ForeverCore side of the both-core helpers of slice 11, package K11w (the warlock class code).
// Owned by the K11w worker: no other package edits this file. Turtle side: ai/cmangos-compat-k11w.h. Notes: forever/docs/SLICE11-NOTES.md.
// Reused, not added: IsImmuneToSpellEffectCompat (TbCompat_P.h:115, its F side bounds the effect index), SpellPowerCostCompat
// (TbCompat_Unit.h:521), GuidKeyCompat (TbCompat_Unit.h:396).
#pragma once

// Put `count` new items of `itemId` into the bags. T `Player::StoreNewItemInInventorySlot(id, count)` (the result is not used by the caller);
// F `Player::StoreNewItemInBestSlots(id, count, ItemContext)` (Player.h:1626), the call `.tb additem` makes (TbScripts.cpp:731): false when
// the bags have no room or the item does not exist. Reached only under the item cheat (CastChoicePetAction::isPossible, one Soul Shard
// before a summon), which the slice keeps off (lead decision D3: shards are real); on F nothing else calls it.
inline bool AddItemCompat(Player* bot, uint32 itemId, uint32 count) { return bot->StoreNewItemInBestSlots(itemId, count, ItemContext::NONE); }

// Does the creature's template carry this extra flag. T `Creature::HasExtraFlag(flag)`; F keeps the flags on the template
// (`CreatureTemplate::flags_extra`, the field Creature.h:126-175 and TbCompat_V.h:136 read).
inline bool CreatureExtraFlagCompat(Creature const* creature, CreatureFlagsExtra flag)
{
    return (creature->GetCreatureTemplate()->flags_extra & flag) != 0;
}
