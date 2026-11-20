// The Turtle side of the both-core pet helpers the ForeverCore port added in slice 11, package P11 (the pet host).
// Same names and signatures as forever/src/compat/TbCompat_P11.h; each body is the expression the bot always made (moved, not changed): the first four
// are the ones that stood in ai/cmangos-compat-unported.h, PetDeadCompat is the body of PetIsDeadValue::Calculate, HasStoredPetCompat the `character_pet`
// query of InitializePetAction::isUseful. Owned by the P11 worker. Notes: forever/docs/SLICE11-NOTES.md.
#pragma once

inline void PetLearnSpellCompat(Pet* pet, uint32 spellId) { pet->LearnSpell(spellId); }
inline void PetToggleAutocastCompat(Pet* pet, uint32 spellId, bool on) { pet->ToggleAutocast(spellId, on); }

// GenericActions.cpp: the dismiss command of the pet action, sent as a pet action packet.
inline bool PetDismissCommandCompat(Player* bot, Pet* pet)
{
    const ObjectGuid& petGuid = pet->getObjectGuid();
    const ObjectGuid& targetGuid = ObjectGuid();
    const uint8 flag = ACT_COMMAND;
    const uint32 spellId = COMMAND_DISMISS;
    const uint32 command = (flag << 24) | spellId;

    HandlePetActionCompat(bot->GetSession(), petGuid, command, targetGuid);
    return true;
}

template <class Visitor>
inline void ForAllGuardiansCompat(Player* bot, Visitor&& visitor) { bot->CallForAllControlledUnits(visitor, CONTROLLED_GUARDIANS); }

// StatsValues.cpp PetIsDeadValue: the pet object is not alive, or the pet guid is set with no object.
inline bool PetDeadCompat(Player const* bot)
{
    Player* player = const_cast<Player*>(bot);
    if (player->GetPet())
        return player->GetPet()->GetDeathState() != ALIVE;
    return !player->GetPetGuid().IsEmpty();
}

// GenericActions.cpp InitializePetAction::isUseful: a `character_pet` row of the current slot or a stable slot whose creature template exists.
inline bool HasStoredPetCompat(Player const* bot)
{
    bool hasTamedPet = false;
    auto queryResult = QueryGuardCompat(CharacterPQueryCompat("SELECT id, entry, owner "
                                                              "FROM character_pet WHERE owner = '%u' AND (slot = '%u' OR slot > '%u') ",
                                                              bot->GetGUIDLow(), PET_SAVE_AS_CURRENT, PET_SAVE_LAST_STABLE_SLOT));
    if (queryResult)
    {
        Field* fields = queryResult->Fetch();
        const uint32 entry = fields[1].GetUInt32();
        hasTamedPet = sObjectMgr.GetCreatureTemplate(entry);
    }
    return hasTamedPet;
}
