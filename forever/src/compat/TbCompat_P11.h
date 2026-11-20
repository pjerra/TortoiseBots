// ForeverCore side of the both-core pet helpers of slice 11, package P11 (the pet host). Owned by the P11 worker: no other package edits this file.
// Turtle side: ai/cmangos-compat-p11.h (the bodies the bot always had). Found through ai/playerbot/TbCompatP11.h. Notes: forever/docs/SLICE11-NOTES.md
// (the pet model on ForeverCore, gaps G1-G5, with the core file:line of every call below).
// The first four helpers MOVED here from TbCompat_Unported.h (where they answered "the action did not happen" through Feature::Pets).
// Reused, not added: PetSpellIdsCompat (TbCompat_V.h:198, the spells of a pet that are not removed), HandlePetActionCompat (TbCompat_A.h:284).
#pragma once

// Teach the pet a spell. T `Pet::LearnSpell(id)`; F `Pet::learnSpell` (Pet.cpp:1962): adds the spell, and for a pet that is not loading tells the owner,
// re-sends the pet bar and refreshes the training points. A spell the core refuses (already known, no spell entry) changes nothing.
inline void PetLearnSpellCompat(Pet* pet, uint32 spellId) { pet->learnSpell(spellId); }

// Switch a pet spell's autocast. T `Pet::ToggleAutocast(id, on)`. F: the same two calls the client's autocast opcode makes (PetHandler.cpp:668-675):
// `Pet::ToggleAutocast(SpellInfo const*, bool)` (Pet.cpp:2136, keeps m_autospells and the spell's active state) and `CharmInfo::SetSpellAutocast`
// (the action bar button). The opcode's own refusals come first: a spell the pet does not know, or that is not autocastable (a passive), is left alone.
inline void PetToggleAutocastCompat(Pet* pet, uint32 spellId, bool on)
{
    SpellInfo const* info = SpellMgr::instance()->GetSpellInfo(spellId, DIFFICULTY_NONE);
    if (!info || !pet->HasSpell(spellId) || !info->IsAutocastable())
        return;
    pet->ToggleAutocast(info, on);
    if (CharmInfo* charm = pet->GetCharmInfo())
        charm->SetSpellAutocast(info, on);
}

// Dismiss the bot's summoned pet (a warlock's demon). T sends the pet action COMMAND_DISMISS. F's word for it is COMMAND_ABANDON (UnitDefines.h:466): for a
// summoned pet PetHandler.cpp:236 removes it (PET_SAVE_NOT_IN_SLOT), for a hunter pet it would DELETE it (:229), which the callers never ask for (a hunter's
// dismiss goes through the spell). HandlePetAction (PetHandler.cpp:64) refuses a mounted owner and a pet that is not the owner's first controlled unit, so
// the answer is read back: true only when the bot has no pet any more.
inline bool PetDismissCommandCompat(Player* bot, Pet* pet)
{
    if (!pet || pet->getPetType() == HUNTER_PET)
        return false;
    uint32 const command = (uint32(ACT_COMMAND) << 24) | uint32(COMMAND_ABANDON);
    HandlePetActionCompat(bot->GetSession(), pet->GetGUID(), command, ObjectGuid::Empty);
    return bot->GetPet() == nullptr;
}

// Visit every guardian the bot controls except its pet and totems, with a snapshot of the list (a visitor may dismiss one). T
// `CallForAllControlledUnits(visitor, CONTROLLED_GUARDIANS)`; F `Unit::m_Controlled` (Unit.h:1256, one set of pets, guardians, totems, charms).
// The visitor is called as visitor(Unit*).
template <class Visitor>
inline void ForAllGuardiansCompat(Player* bot, Visitor&& visitor)
{
    std::vector<Unit*> const snapshot(bot->m_Controlled.begin(), bot->m_Controlled.end());
    for (Unit* unit : snapshot)
        if (unit && unit->HasUnitTypeMask(UNIT_MASK_GUARDIAN) && !unit->IsPet() && !unit->IsTotem())
            visitor(unit);
}

// Is the bot's pet dead. T (StatsValues.cpp PetIsDeadValue): the pet object is not alive, or the pet guid is set with no object. F removes a dead
// hunter pet's body after its timer (Pet.cpp:670-677, saved PET_SAVE_NOT_IN_SLOT) and then keeps only the stable row with Health 0; Call Pet refuses
// with SPELL_FAILED_DONT_REPORT while any active pet is dead (Spell.cpp:6779-6787) and Revive Pet reads that row (Spell.cpp:6692-6715), so the value
// must read true for it or "no pet" fires and Call Pet fails silently for ever (G2). A pet that is out reads by its own state.
inline bool PetDeadCompat(Player const* bot)
{
    if (Pet* pet = bot->GetPet())
        return !pet->IsAlive();
    if (PetStable const* stable = bot->GetPetStable())
        for (Optional<PetStable::PetInfo> const& info : stable->ActivePets)
            if (info && info->Health == 0)
                return true;
    return false;
}

// Does the bot own a pet that Call Pet could bring out: an active-slot row of its pet stable whose creature template exists. T asked the
// `character_pet` table for the current slot or a stable slot (GenericActions.cpp, the "initialize pet" check). F's current slot is -3 and its active
// slots 0-4 (PetDefines.h:40-48), so that query finds nothing there (G1); `Player::GetPetStable` (Player.h:1384) is the same fact in memory. A
// stabled pet (slot 5 and up) is not callable and does not count.
inline bool HasStoredPetCompat(Player const* bot)
{
    PetStable const* stable = bot->GetPetStable();
    if (!stable)
        return false;
    for (Optional<PetStable::PetInfo> const& info : stable->ActivePets)
        if (info && ObjectMgr::instance()->GetCreatureTemplate(info->CreatureId))
            return true;
    return false;
}
