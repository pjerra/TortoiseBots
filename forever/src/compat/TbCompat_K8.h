// ForeverCore side of the both-core helpers of slice 8, package K8 (mage, rogue, give item).
// Owned by the slice-8 K8 worker: no other package edits this file. Turtle side: ai/cmangos-compat-k8.h. Notes: forever/docs/SLICE8-K-NOTES.md
// (each helper below with the file:line of the core call it makes). Helpers K8 did NOT need to add are reused: IsImmuneToSpellEffectCompat
// (TbCompat_P.h), SpellPowerCostCompat (TbCompat_Unit.h), GetCombatReachCompat (TbCompat_Unit.h), PetIsMiniPetCompat (TbCompat_V.h).
#pragma once

// A stunned unit, or one about to be stunned. T tests UNIT_STAT_STUNNED | UNIT_STAT_PENDING_STUNNED (a Sapped pack mate detects nothing, so the
// Sap approach may pass close to it). F has no pending-stun state: Unit.h:289 `UNIT_STATE_STUNNED` only, set while a stun aura is on the unit.
// "Different answer" (less, never more): a mate whose stun is queued but not applied reads as awake, so the approach keeps its clearance.
// (Sap itself is out of slice 8 (decision D3); the rule is compiled and unreachable.)
inline bool StunnedOrPendingStunCompat(Unit const* unit) { return unit->HasUnitState(UNIT_STATE_STUNNED); }

// Hand a stack over: the item's owner field becomes the receiver. T `Item::SetOwnerGuid(ObjectGuid)`; F `Item::SetOwnerGUID(ObjectGuid)` (Item.h:206),
// the same field (UF::ItemData::Owner). F's `Player::MoveItemToInventory` sets it again when the owner differs (Player.cpp:13192-13193), so the
// call is redundant there and harmless.
inline void ItemSetOwnerCompat(Item* item, Unit const* owner) { item->SetOwnerGUID(owner->GetGUID()); }

// The spell state a bot's interrupt rule reads off a victim's cast. The rule (ai/playerbot/InterruptCastingPolicy.h:57-61) numbers PREPARING = 1
// and CASTING = 2 (Turtle: SPELL_STATE_CASTING is a CHANNEL in progress, Spell.cpp:4004; a missile in flight is DELAYED = 3). F numbers
// PREPARING 1, LAUNCHED 2, CHANNELING 3 (Spell.h:254-262); the raw F state would turn a missile in flight (LAUNCHED, Spell.cpp:3950: the bar is
// over, the effects are on their way) into "casting", and a real channel (3) into nothing. So: PREPARING -> 1, CHANNELING -> 2, every other state
// -> 0 (not on a bar: not interruptible), which is also the core's own gate for Interrupt Cast (SpellEffects.cpp:3136-3137: CHANNELING, or
// PREPARING with a cast time).
inline uint32 InterruptSpellStateCompat(Spell const* spell)
{
    switch (spell->getState())
    {
        case SPELL_STATE_PREPARING:  return 1;
        case SPELL_STATE_CHANNELING: return 2;
        default:                     return 0;
    }
}

// The item-cheat branch of ApplyPoisonAction: a hand-built Spell (BotUseItemSpell) cast onto the weapon, one per on-use spell of the poison.
// F builds no such Spell: the named feature "item and totem casts" (counted once per call). Failure value: false = no cast started.
// Never reached at level 18 (no poison below level 20, decision D4) and only behind the `item` cheat.
template <class SpellType, class SetDurationFn>
inline bool PoisonItemCheatCastCompat(Player* /*bot*/, Item* /*weapon*/, ItemPrototype const* /*poisonProto*/, SetDurationFn /*setDuration*/)
{
    tbcompat::Unported(tbcompat::Feature::ItemCasts);
    return false;
}
