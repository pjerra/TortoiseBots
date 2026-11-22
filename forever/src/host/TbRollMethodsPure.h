// Piece D of #136 (loot methods): the method table both the roll gate and the run gate read, core-free so a unit test can prove it
// (tools/test_roll_methods_pure.cpp).
//
// What TB does (ai/playerbot/policy/loot/LootRollPolicy.h LootMethodTakesRolls): rolls are announced and voted in GROUP_LOOT and NEED_BEFORE_GREED;
// in free-for-all and round robin there is no roll window, and under master loot the human master distributes, so bots stay silent there. The table
// takes the method as the core numbers it (Group.h LootMethod: 0 free-for-all, 1 round robin, 2 master loot, 3 group loot, 4 need-before-greed),
// which are the same numbers the policy reads (RemoteCommander kLoot*); anything else (no group, the core's personal loot) takes no rolls.
#pragma once

#include "runtime/RemoteCommanderTasks.h"

namespace TortoiseBots
{
namespace rollmethods
{
namespace RC = ::TortoiseBots::RemoteCommander;

// A roll window exists under this method: the bot's roll is announced to the engine and voted. Free-for-all, round robin and master loot stay silent;
// the core's own timeout (or the human master) decides there.
inline bool MethodTakesRolls(int method)
{
    return method == RC::kLootGroup || method == RC::kLootNeedBeforeGreed;
}

// A run goes on under this method: group loot and need-before-greed (the bots vote both). Master loot would need a human to distribute, free-for-all
// and round robin leave items to chance, and personal loot is a method the policy does not know: the run pauses instead of guessing.
inline bool RunMayUseMethod(int policyMethod)
{
    return MethodTakesRolls(policyMethod);
}

}  // namespace rollmethods
}  // namespace TortoiseBots
