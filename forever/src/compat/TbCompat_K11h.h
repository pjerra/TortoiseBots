// ForeverCore side of the both-core helpers of slice 11, package K11h (the hunter class code).
// Owned by the K11h worker: no other package edits this file. Turtle side: ai/cmangos-compat-k11h.h. Notes: forever/docs/SLICE11-NOTES.md
// (each helper below with the file:line of the core call it makes). Reused, not added: GetDistanceCompat (TbCompat_Unit.h:106),
// GetCombatDistanceCompat (TbCompat_V.h:233), PetIsHappyCompat (TbCompat_V.h:210), AmmoItemIdCompat (TbCompat_Unported.h:284).
#pragma once

// Stop a unit's melee swing (SwitchToRangedAction, CombatActions.cpp:45: a hunter leaving its melee kit). T `Unit::AttackStop(bool targetSwitch, bool includingCast,
// bool includingCombo)`; F `Unit::AttackStop()` (Unit.h:747, Unit.cpp:6373): no arguments. T's `targetSwitch = true` marks "a target change, not a real combat stop" and only
// skips the creature-only reset at the end of the call; F's body does that reset for creatures unconditionally (Unit.cpp:6392-6395). The only caller is a Player, where
// both sides do the same: drop the victim, clear the target, interrupt the melee swing, send the stop packet. NOTE: package K10 (branch s10/k, TbCompat_S10.h) adds the
// same helper under the same name and body for BalanceDruidStrategy.cpp; the two wrappers are never in one translation unit, and at the merge one definition stays (the
// same name, signature and answer).
inline bool AttackStopCompat(Unit* unit, bool /*targetSwitch*/) { return unit->AttackStop(); }

// Make `itemId` the ammo the ranged weapon draws from. T writes the PLAYER_AMMO_ID field and refreshes the ranged damage
// (`UpdateDamagePhysical(RANGED_ATTACK)`), and always reports success. F `Player::SetAmmo` (Player.cpp:10663) refuses an item that is not
// a projectile, that the bot does not hold or cannot use (false: nothing changed), and refreshes the ranged damage itself when the ammo
// changed. A refusal makes the equip-ammo action answer false (nothing done), which is the "do less" side.
inline bool SetAmmoCompat(Player* bot, uint32 itemId) { return bot->SetAmmo(itemId); }

// Is this creature a beast the hunter could tame. T `CreatureInfo::isTameable()` (beast type, a family, the tameable flag; no player test).
// F `CreatureTemplate::IsTameable(canTameExotic, difficulty)` (CreatureData.h:552): the same three tests plus the exotic rule, which the
// core asks of the CASTER (`Player::CanTameExoticPets`, Player.h:2621: a GM or an aura), exactly as Spell.cpp:6858 does. A bot that cannot
// tame exotic beasts reads them as not tameable (less). A creature without a template or a difficulty row reads false.
inline bool IsTameableCompat(Player const* player, Creature const* creature)
{
    CreatureTemplate const* info = creature->GetCreatureTemplate();
    CreatureDifficulty const* difficulty = creature->GetCreatureDifficulty();
    return info && difficulty && info->IsTameable(player->CanTameExoticPets(), difficulty);
}

// Visit every unit on `attacker`'s threat list (the units that attacker has threat on). T `getThreatList()` references and
// `HostileReference::getTarget()`; F `ThreatManager::GetUnsortedThreatList()` (ThreatManager.h:136) and `ThreatReference::GetVictim()`
// (:294). Offline entries are visited as well, as T's list holds them: the callers count targets that are alive and not the bot.
// fn(Unit*) returns nothing; the walk reads only.
template <class Fn>
inline void ForEachThreatListTargetCompat(Unit* attacker, Fn&& fn)
{
    for (ThreatReference const* ref : attacker->GetThreatManager().GetUnsortedThreatList())
        fn(ref ? ref->GetVictim() : nullptr);
}

// Is the unit unable to move: rooted, stunned or under a root aura. T twin ai/cmangos-compat-shim.h:64 (`IsRooted() || UNIT_STAT_STUNNED ||
// MOD_ROOT`), the same three tests here: `IsRooted()` is the root movement flag (the macro of TbCompat_Names.h:91), UNIT_STATE_STUNNED
// (Unit.h:768 `HasUnitState`) and the root aura type. A null unit reads false.
inline bool IsImmobilizedStateCompat(Unit const* unit)
{
    return unit && (unit->IsRooted() || unit->HasUnitState(UNIT_STATE_STUNNED) || unit->HasAuraType(SPELL_AURA_MOD_ROOT));
}

inline bool IsTameBeastSpellCompat(SpellEntry const* spell)
{
    if (!spell)
        return false;
    SpellInfo const* si = sSpellMgr.GetSpellInfo(spell->Id, DIFFICULTY_NONE);
    return si && si->HasEffect(SPELL_EFFECT_TAMECREATURE);
}

inline std::string CreatureFamilyNameCompat(CreatureInfo const* info)
{
    CreatureFamilyEntry const* family = info ? sCreatureFamilyStore.LookupEntry(info->family) : nullptr;
    return (family && family->Name[LOCALE_enUS]) ? std::string(family->Name[LOCALE_enUS]) : std::string();
}

inline bool IsReservedNameCompat(std::string const& name)
{
    return sObjectMgr.IsReservedName(name);
}

inline void SetPetNameTimestampCompat(Pet* pet, uint32 ts)
{
    if (pet)
        pet->SetPetNameTimestamp(ts);
}

inline void RemovePetCompat(Player* bot)
{
    if (bot)
        bot->RemovePet(nullptr, PET_SAVE_NOT_IN_SLOT);
}
