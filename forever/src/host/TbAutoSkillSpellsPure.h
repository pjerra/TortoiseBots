// Core-free rules of the skill-rewarded spell pass (forever/src/host/TbAutoSkillSpells.cpp):
// the Dual Wield class/level thresholds and the hunter ranged weapon skill seed,
// tested standalone by forever/tools/test_autoskillspells.cpp.
#pragma once

#include <cstdint>

namespace tbhost
{
namespace skillspells
{

// The class ids these rules name, from the core's SharedDefines.h (CLASS_WARRIOR = 1,
// CLASS_HUNTER = 3, CLASS_ROGUE = 4); the header stays core-free for the standalone test.
enum class PlayerClass : std::uint8_t
{
    Warrior = 1,
    Hunter  = 3,
    Rogue   = 4,
};

// True when a hunter knows no bow, gun or crossbow skill at all: the only state the
// ranged-skill seed answers. Undead and Gnome hunters are new race/class pairs on
// this game version, and the client carries neither a CharacterLoadout row nor a
// create-time weapon skill for them, so they begin with an empty ranged slot and
// no skill Auto Shot could use.
inline bool HunterRangedSkillOwed(std::uint8_t playerClass, std::uint32_t bowsSkill,
    std::uint32_t gunsSkill, std::uint32_t crossbowsSkill)
{
    return playerClass == std::uint8_t(PlayerClass::Hunter) && !bowsSkill && !gunsSkill && !crossbowsSkill;
}

// The skill line a hunter's ranged slot asks for: its own when the weapon is a bow,
// gun or crossbow, otherwise Bows - the Worn Shortbow the starter path and the spawn
// kit equip once the hunter is proficient.
inline std::uint32_t HunterRangedSkill(std::uint32_t rangedWeaponSkill)
{
    switch (rangedWeaponSkill)
    {
    case 46:  return 46;  // SKILL_GUNS
    case 226: return 226; // SKILL_CROSSBOWS
    default:  return 45;  // SKILL_BOWS
    }
}

// The same class thresholds the legacy factory applies for Dual Wield: warrior and rogue
// at 10, hunter at 20. SkillRaceClassInfo id 131 says rogue 10 and id 132 says 20 for the
// warrior/hunter row, but the owner's rule (and the trainer data, npc_trainer for the
// teaching spell 1424) puts a warrior at 10, so the threshold cannot come from the store.
inline std::uint32_t DualWieldLevelForClass(std::uint8_t playerClass)
{
    switch (playerClass)
    {
    case std::uint8_t(PlayerClass::Warrior):
    case std::uint8_t(PlayerClass::Rogue):
        return 10;
    case std::uint8_t(PlayerClass::Hunter):
        return 20;
    default:
        return 0;   // the class cannot train Dual Wield at all
    }
}

// True when the class trains Dual Wield and the bot has reached its level.
inline bool DualWieldOwed(std::uint8_t playerClass, std::uint32_t level)
{
    std::uint32_t const requiredLevel = DualWieldLevelForClass(playerClass);
    return requiredLevel != 0 && level >= requiredLevel;
}

} // namespace skillspells
} // namespace tbhost
