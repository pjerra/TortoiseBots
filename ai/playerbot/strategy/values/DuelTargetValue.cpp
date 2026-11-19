
#include "playerbot/playerbot.h"
#include "DuelTargetValue.h"

using namespace ai;

Unit* DuelTargetValue::Calculate()
{
    ObjectGuid const opponent = DuelOpponentGuidCompat(bot);
    if (opponent.IsEmpty()) return nullptr;
    return ObjectAccessor::GetUnit(*bot, opponent);
}
