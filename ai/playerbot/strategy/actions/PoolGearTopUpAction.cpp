#include "playerbot/playerbot.h"
#include "PoolGearTopUpAction.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/TbCompatPoolGear.h"
#include "../../../../runtime/PoolGearTopUpPolicy.h"

using namespace ai;

bool PoolGearTopUpAction::isUseful()
{
    if (!TortoiseBots::PoolGearTopUpEligible(sPlayerbotAIConfig.poolGearTopUpOnLevel,
            PoolGearTopUpIsPoolBotCompat(bot),
            PoolGearTopUpIsHiredCompat(bot),
            PoolGearTopUpIsClaimedCompat(bot),
            ai->HasRealPlayerMaster()))
        return false;

    return TortoiseBots::PoolGearTopUpDue(bot->GetLevel(), PoolGearTopUpLastLevelCompat(bot));
}

bool PoolGearTopUpAction::Execute(Event& event)
{
    // Worn gear is never changed in combat (#336, see EquipUpgradesAction):
    // a ding on the killing blow leaves the level open and the next
    // out-of-combat "equipment audit" picks it up.
    if (!sServerFacade.IsAlive(bot) || sServerFacade.IsInCombat(bot))
        return false;

    uint32 const level = bot->GetLevel();
    // Mark first: one attempt per level, also when no slot found a candidate,
    // so a slot the item cache cannot fill is not re-searched every audit.
    PoolGearTopUpMarkLevelCompat(bot, level);

    uint32 const changed = PoolGearTopUpRunCompat(bot, level);
    if (changed)
        sPlayerbotAIConfig.logEvent(ai, "PoolGearTopUp", std::to_string(level), std::to_string(changed));

    return changed > 0;
}
