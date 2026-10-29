
#include "playerbot/playerbot.h"
#include "CurrentTargetValue.h"

#include "playerbot/ServerFacade.h"
#include "playerbot/BossTacticsAirTarget.h"
using namespace ai;

Unit* CurrentTargetValue::Get()
{
    if (selection.IsEmpty())
        return NULL;

    Unit* unit = sObjectAccessor.GetUnit(*bot, selection);
    // An explicit attack order carries the walk to the target: keep it as the current
    // target out to the order range so the reach chain can close the distance. Every
    // other target keeps the background sight-distance ceiling - except an air_target
    // the tactics keep (BossTacticsPolicy.h AirTargetKeep): a flying boss moves across
    // a raid room wider than the sight distance while the reach walks the floor under
    // her, so she stays the current target out to the order range as well.
    float maxDistance = sPlayerbotAIConfig.sightDistance;
    if (unit && ai->GetAiObjectContext()->GetValue<ObjectGuid>("explicit attack target")->Get() == unit->getObjectGuid())
        maxDistance = sPlayerbotAIConfig.reactDistance;
    else if (IsAirTargetKept(ai, bot, unit))
        maxDistance = sPlayerbotAIConfig.reactDistance;
    if (unit && !bot->IsWithinDistInMap(unit, maxDistance))
        return NULL;

    // Distance was the only test here, so a target kept once was kept for good -
    // a player who stealthed after being targeted stayed targeted. This asks the
    // same question the target list asks before picking anyone: can the bot still
    // see them. It covers stealth, invisibility and visibility state and does not
    // involve line of sight, so a target behind a pillar is not lost.
    if (unit && unit != bot &&
        !unit->IsVisibleForOrDetect(bot, bot->GetCamera().GetBody(), true))
        return NULL;

    return unit;
}

void CurrentTargetValue::Set(Unit* target)
{
    selection = target ? target->getObjectGuid() : ObjectGuid();
}
