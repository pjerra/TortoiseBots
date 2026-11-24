// Core-free Dual Wield class/level rules of the skill-rewarded spell pass
// (forever/src/host/TbAutoSkillSpells.cpp), tested standalone by
// forever/tools/test_autoskillspells.cpp.
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
