#include "playerbot/playerbot.h"
#include "playerbot/AoeOffPolicy.h"
#include "NoAoeStrategy.h"

using namespace ai;

float NoAoeMultiplier::GetValue(Action* action)
{
    if (action && IsAreaSpellAction(action->getName()))
        return 0.0f;
    return 1.0f;
}

void NoAoeStrategy::InitCombatMultipliers(std::list<Multiplier*>& multipliers)
{
    multipliers.push_back(new NoAoeMultiplier(ai));
}
