
#include "playerbot/playerbot.h"
#include "../../runtime/PlayerbotAIStorage.h" // Headless storage shim
#include "NearestNonBotPlayersValue.h"

#include "Maps/GridNotifiers.h"
#include "Maps/GridNotifiersImpl.h"
#include "Maps/CellImpl.h"

using namespace ai;

void NearestNonBotPlayersValue::FindUnits(std::list<Unit*> &targets)
{
    MaNGOS::AnyUnitInObjectRangeCheck u_check(bot, range);
    VisitUnitsCompat(bot, targets, u_check, range);
}

bool NearestNonBotPlayersValue::AcceptUnit(Unit* unit)
{
    ObjectGuid guid = unit->getObjectGuid();
#ifdef MANGOS
    return guid.IsPlayer() && !PlayerbotAIStorage::Instance().GetAI((Player*)unit) && (!((Player*)unit)->IsGameMaster() || ((Player*)unit)->IsGMVisible());
#endif
#ifdef CMANGOS
    return guid.IsPlayer() && !PlayerbotAIStorage::Instance().GetAI((Player*)unit) && (!((Player*)unit)->IsGameMaster() || ((Player*)unit)->IsGMVisible());
#endif
}