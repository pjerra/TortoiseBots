// Slice 11, package P11: the pet host. The bot-side work the legacy PlayerbotFactory did for pets (a hunter's first pet, its pet spells), done on ForeverCore
// through the core's own pet API (forever/docs/SLICE11-NOTES.md, section "the pet host"). The functions here are the CALLS the legacy factory members
// (PlayerbotFactory::InitPet and friends, TbHostPetFactory.cpp) and the `.tb pet` command (TbPet.cpp) both make, so a result is the same however it was asked for.
//
// This header names no legacy type. Everything runs on the world thread. The functions are defined in the slice build only (TbHostPetFactory.cpp);
// the default build answers `.tb pet` with "the engine is not built in" and never reaches them.
#pragma once

#include "Define.h"

class Player;

namespace TortoiseBots
{
namespace p11
{
// Why a first pet was or was not made. The words are the `code` of a refusal in `.tb pet give`.
enum class GiveResult : uint8
{
    Ok = 0,
    NotHunter,        // only a hunter is given a pet (a warlock's demon is a cast, not a gift)
    LevelTooLow,      // classic hunters tame their first beast at 10 (TortoiseBots::HUNTER_PET_MIN_LEVEL)
    NotInWorld,       // the bot has no map yet
    HasPet,           // a pet is out: nothing is replaced
    HasStoredPet,     // a pet is in an active slot of the stable: Call Pet brings it back, a second one would fill the stable
    NoEntry,          // no starter beast of the table is a tameable beast in this data
    CreateFailed,     // the core refused (no free active slot, no stats for that level)
    NoMap,            // the pet has no map (the owner is not placed)
    NotSummoned       // the pet was made but is not out after the steps (read back)
};
char const* GiveResultCode(GiveResult result);
char const* GiveResultText(GiveResult result);

// The starter beast for the bot's race: the table entry when it is a tameable, non-exotic beast in this data, else the faction's fallback when that is,
// else 0. (`hunter-pets.csv` of the collaborator lists humanoid NPCs, not beasts: the table is forever/data/slice11/hunter-starter-pets.csv.)
uint32 StarterPetEntry(Player const* bot);

// Give a hunter its first pet: `Unit::CreateTamedPetFrom(entry)` (the core's tame path with no wild creature), then the four steps `Spell::EffectTameCreature`
// takes (add to the map, make it the bot's minion, save it as the current pet, send the pet bar), the classic hunter rules the bot needs (Best Friend, defensive,
// every learned active spell on its autocast default) and the pet number written to `characters.summonedPetNumber` at once. A map that refuses the pet, or a
// pet the bot cannot read back after the attach, is undone completely (active slot, attachment, object; nothing saved), and the result is read back from the
// world before anything is written.
GiveResult GiveHunterPet(Player* bot, bool saveToDb = true);
// A hunter's ranged slot: true when it holds a bow, gun or crossbow (what Auto Shot needs). Below level 5 an empty
// slot gets the core's starter bow back; above 5 the slot is the spawn kit's and autogear's.
bool EnsureHunterStarterWeapon(Player* bot);
void EnsureHunterStarterAmmo(Player* bot);
void EnsureShamanStarterTotems(Player* bot);
}
}
