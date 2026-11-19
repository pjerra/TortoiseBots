
#include "playerbot/playerbot.h"
#include "NearestFriendlyPlayersValue.h"

#include "Maps/GridNotifiers.h"
#include "Maps/GridNotifiersImpl.h"
#include "Maps/CellImpl.h"

using namespace ai;
using namespace MaNGOS;

void NearestFriendlyPlayersValue::FindUnits(std::list<Unit*> &targets)
{
    VisitFriendlyUnitsCompat(bot, targets, range);
}

bool NearestFriendlyPlayersValue::AcceptUnit(Unit* unit)
{
    ObjectGuid guid = unit->getObjectGuid();
    return guid.IsPlayer() && guid != ai->GetBot()->getObjectGuid();
}
