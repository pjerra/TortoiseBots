// The Turtle side of the both-core helpers the ForeverCore port added in slice 11, package K11h (the hunter class code).
// Same names and signatures as forever/src/compat/TbCompat_K11h.h; each is the expression the bot always made (the call is moved, not changed).
// Owned by the K11h worker: no other package edits this file. IsImmobilizedStateCompat is NOT repeated here: Turtle has it in
// ai/cmangos-compat-shim.h:64 already. Notes: forever/docs/SLICE11-NOTES.md.
#pragma once

// SwitchToRangedAction (CombatActions.cpp): the swing stop as the bot always made it.
inline bool AttackStopCompat(Unit* unit, bool targetSwitch) { return unit->AttackStop(targetSwitch); }

// HunterEquipAmmoAction: the ammo field and the ranged damage refresh, as the action always did. Reports success.
inline bool SetAmmoCompat(Player* bot, uint32 itemId)
{
    bot->SetUInt32Value(PLAYER_AMMO_ID, itemId);
    bot->UpdateDamagePhysical(RANGED_ATTACK);
    return true;
}

// TameBeastAction::isUseful: the creature template's own test (a beast with a family and the tameable flag). The player is not asked on Turtle.
inline bool IsTameableCompat(Player const* /*player*/, Creature const* creature)
{
    return creature->GetCreatureInfo() && creature->GetCreatureInfo()->isTameable();
}

// CastFeignDeathAction::isUseful: every unit on the attacker's threat list (each entry's target), as the loop always read it.
template <class Fn>
inline void ForEachThreatListTargetCompat(Unit* attacker, Fn&& fn)
{
    for (HostileReference* ref : attacker->GetThreatManager().getThreatList())
        fn(ref ? ref->getTarget() : nullptr);
}

inline bool IsTameBeastSpellCompat(SpellEntry const* spell)
{
    return spell && (spell->AttributesEx2 & SPELL_ATTR_EX2_TAME_BEAST);
}

inline std::string CreatureFamilyNameCompat(CreatureInfo const* info)
{
    CreatureFamilyEntry const* family = info ? sCreatureFamilyStore.LookupEntry(info->beast_family) : nullptr;
    return (family && family->Name[LOCALE_enUS]) ? std::string(family->Name[LOCALE_enUS]) : std::string();
}

inline bool IsReservedNameCompat(std::string const& name)
{
    return sObjectMgr.IsReservedName(name);
}

inline void SetPetNameTimestampCompat(Pet* pet, uint32 ts)
{
    if (pet)
        pet->SetUInt32Value(UNIT_FIELD_PET_NAME_TIMESTAMP, ts);
}

inline bool HasAnyHunterPetCompat(Player const* bot)
{
    return bot && (bot->GetPet() || !bot->GetPetGuid().IsEmpty());
}

inline void RemovePetCompat(Player* bot)
{
    if (bot)
        bot->RemovePet(PET_SAVE_AS_DELETED);
}
