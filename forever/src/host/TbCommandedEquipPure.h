// Commanded-bot auto-equip gate: TB parity for EquipUpgradesAction.
// TB runs the action for every bot; a commanded bot self-equips iff
// AiPlayerbot.ClaimedBotSelfEquip (default 1). The explicit `.tb gear upgrade`
// order (ScopedExplicitEquipOrder) always runs. Core-free so the g++ test can
// pin the truth table without a world.
#pragma once

namespace TortoiseBots
{
namespace commandedequip
{

inline bool MayCommandedBotAutoEquip(bool isInCommandedGroup, bool claimedBotSelfEquip, bool explicitOrder)
{
    if (explicitOrder)
        return true;
    if (!isInCommandedGroup)
        return true;
    return claimedBotSelfEquip;
}

} // namespace commandedequip
} // namespace TortoiseBots
