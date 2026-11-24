// `enemy player near`, in its own translation unit: PvpTriggers.cpp also implements the Warsong Gulch flag triggers and
// includes the battleground header, which the Forever slice does not have; this row is the one the world-PvP strategies use.
#include "playerbot/playerbot.h"
#include "PvpTriggers.h"

using namespace ai;

bool EnemyPlayerNear::IsActive()
{
    // Check if we have any enemy players to attack
    if(AI_VALUE(bool, "has enemy player targets"))
    {
        Unit* currentTarget = AI_VALUE(Unit*, "current target");
        if (currentTarget)
        {
            // Check if we have a better enemy player to attack
            Player* currentPlayerTarget = dynamic_cast<Player*>(currentTarget);
            if(currentPlayerTarget)
            {
                return currentPlayerTarget != AI_VALUE(Unit*, "enemy player target");
            }
        }

        return true;
    }

    return false;
}
