
#include "playerbot/playerbot.h"
#include "InvalidTargetValue.h"
#include "PossibleAttackTargetsValue.h"
#include "EnemyPlayerValue.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/BossTacticsAirTarget.h"

using namespace ai;

bool InvalidTargetValue::Calculate()
{
    Unit* target = AI_VALUE(Unit*, qualifier);
    if (!target || !target->IsInWorld() || target->GetMapId() != bot->GetMapId())
    {
        return true;
    }

    Unit* duelTarget = AI_VALUE(Unit*, "duel target");
    if (duelTarget && duelTarget == target)
    {
        return false;
    }

    // A creature in evade mode cannot be killed: the core refuses to start an attack on
    // one, and every point of damage aimed at it is dropped (WorldObject::DealDamageMods).
    // AttackersValue::IsValid already refuses such a creature when a new target is picked;
    // one already held has to be dropped here, or the bot keeps "fighting" it - silent,
    // motionless, and taking nothing - until the creature resets 24 s later.
    Creature* creature = dynamic_cast<Creature*>(target);
    if (creature && creature->IsInEvadeMode())
    {
        return true;
    }

    // Same for the window before the walk home: the core flags a creature it cannot
    // path to after 3 s (IsEvadeBecauseTargetNotReachable), a full 21 s before the
    // evade reset. A held order in that window only ever misses, so drop it now and
    // let the reach give-up blacklist the guid instead of holding a ghost.
    // Exception: an air_target row keeps the boss while it is in the air - a boss
    // chased in flight has no floor path and may carry this flag, and the reach walks
    // to the floor under it instead (BossTacticsPolicy.h AirTargetKeep). The evade-mode drop above still applies.
    if (creature && creature->IsEvadeBecauseTargetNotReachable() && !IsAirTargetKept(ai, bot, creature))
    {
        return true;
    }

    if (qualifier == "current target")
    {
        if (target->getObjectGuid() != bot->GetSelectionGuid())
        {
            return true;
        }
    }

    // A player command deliberately selects a hostile target before either side
    // has threat.  The ordinary validity path requires active combat, which made
    // ranged bots immediately discard that target before their first spell (a
    // melee bot happened to survive because auto-attack creates combat at once).
    // Keep every normal attackability/range/CC check, but let the bot's own
    // explicit attack target pass the combat-only gate until the pull begins.
    const bool explicitAttackTarget = qualifier == "current target" &&
        AI_VALUE(ObjectGuid, "attack target") == target->getObjectGuid();

    // A held CC mark (crowd-controlled, under a CC cast bar or in its hold
    // window, while other enemies still fight the group) is nobody's
    // current target: a bot that took it before the mark (or picked it up
    // as a loose mob) otherwise keeps hitting, taunting and DoTing it and
    // breaks every sheep (in-game retest: sheep broke 0.07-2.4 s after landing).
    // A free mark (CC missed or broke) is a normal enemy and stays valid.
    // Exempt: the player's own attack/pull order, and a bot casting crowd
    // control at it (the cc order selects the mark; dropping it would
    // interrupt that very Polymorph). "rti cc" defaults to Moon on every
    // bot, so it cannot tell the CC owner apart.
    if (qualifier == "current target" && !explicitAttackTarget &&
        AI_VALUE(ObjectGuid, "explicit attack target") != target->getObjectGuid() &&
        !FindNonCcTargetStrategy::IsCastingCcAt(bot, target))
    {
        CcGuard ccGuard(ai);
        if (ccGuard.IsHeldCcMark(target))
            return true;
    }
    // An explicit order carries the walk to the target: keep the same wider ceiling
    // here or the order is accepted and then discarded as invalid on the next tick.
    // An air_target the tactics keep gets the same ceiling: her hover points are
    // further than the sight distance from the raid's floor, and the reach walks
    // the floor under her from there (CurrentTargetValue keeps her the same way).
    const bool airTargetKept = IsAirTargetKept(ai, bot, target);
    const bool validTarget = PossibleAttackTargetsValue::IsValid(target, bot,
        (explicitAttackTarget || airTargetKept) ? sPlayerbotAIConfig.reactDistance : sPlayerbotAIConfig.sightDistance,
        false, !explicitAttackTarget);
    if (!validTarget)
    {
        std::list<ObjectGuid> attackers = AI_VALUE(std::list<ObjectGuid>, "possible attack targets");
        if (std::find(attackers.begin(), attackers.end(), target->getObjectGuid()) != attackers.end())
        {
            return false;
        }
    }

    return !validTarget;
}
