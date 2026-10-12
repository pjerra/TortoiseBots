#pragma once

#include <string>
#include <unordered_set>

// Pure decision rule for `.bot action aoe off` (the "no aoe" strategy): the
// player's order is "no area spells at all", for example before a Polymorph
// or Sap pull. Area spells sit in the class "aoe" strategies and also in
// plain rotations (Whirlwind, Hurricane, Consecration, Magma Totem, Blade
// Flurry, Frost Nova...), so the gate matches the action NAME wherever the
// action was queued from. Single-target casts, multi-target dot spreading
// ("corruption on attacker"), heals and defensive clouds are not area
// spells and stay allowed.
//
// "dps aoe" is the AoE target picker: with area spells off there is no
// reason to pick the target that sits in the densest cluster.

namespace ai
{

inline bool IsAreaSpellAction(const std::string& actionName)
{
    static const std::unordered_set<std::string> areaActions = {
        // AoE target picker
        "dps aoe",
        // mage
        "arcane explosion", "blast wave", "blizzard", "cone of cold",
        "flamestrike", "frost nova",
        // warlock
        "rain of fire", "howl of terror",
        // priest
        "holy nova", "psychic scream",
        // druid
        "hurricane", "swipe", "swipe (bear)", "swipe (cat)",
        "demoralizing roar", "challenging roar",
        // hunter
        "multi-shot", "volley", "carve", "explosive trap",
        "explosive trap on target", "frost trap", "frost trap on target",
        // paladin
        "consecration", "holy wrath",
        // shaman
        "chain lightning", "magma totem", "fire nova", "earthquake",
        // warrior
        "cleave", "whirlwind", "sweeping strikes", "thunder clap",
        "thunder clap on snare target", "demoralizing shout",
        "challenging shout", "intimidating shout", "piercing howl",
        // rogue
        "blade flurry",
        // racial
        "war stomp",
        // engineering / consumables
        "goblin sapper", "throw grenade", "oil of immolation",
    };
    return areaActions.count(actionName) != 0;
}

} // namespace ai
