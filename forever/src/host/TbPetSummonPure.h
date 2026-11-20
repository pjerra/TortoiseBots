// Slice 11, package P11 (review fix): the order of the hunter's first pet, core-free so tools/test_pet_summon.cpp can force a map insert that does not take.
//
// GiveHunterPet (TbHostPetFactory.cpp) makes the pet with the core's own tame path (Unit::CreateTamedPetFrom, which fills an active slot of the stable) and then
// takes the steps of Spell::EffectTameCreature. The core ignores Map::AddToMap's answer; this path may not: a pet the map refused is not attached, not saved and
// not named in characters.summonedPetNumber, and the active slot InitTamedPet took goes back, so the next check can make a pet again. A pet the bot cannot read
// back after the attach is undone the same way, before anything is written.
#pragma once

#include <cstdint>

namespace TortoiseBots
{
namespace p11
{
enum class PetSummonOutcome : uint8_t
{
    Out,          // the bot reads the pet back, and it is kept
    NotInserted,  // the map would not take the pet: nothing was attached or written
    NotAttached   // the bot does not read the pet back: nothing was written
};

// "Steps" is the host in core terms: Insert() -> bool (the map took the pet), Attach() (the bot takes it as its minion), Out() -> bool (the bot reads the pet
// back), Keep() (autocast defaults, the save, the pet bar), Discard(bool attached) (detach when it was attached, remove the object, free the stable slot; no
// database write). Keep is reached only when Out said the pet is there.
template <class Steps>
PetSummonOutcome SummonTamedPet(Steps& steps)
{
    if (!steps.Insert())
    {
        steps.Discard(false);
        return PetSummonOutcome::NotInserted;
    }
    steps.Attach();
    if (!steps.Out())
    {
        steps.Discard(true);
        return PetSummonOutcome::NotAttached;
    }
    steps.Keep();
    return PetSummonOutcome::Out;
}
}
}
