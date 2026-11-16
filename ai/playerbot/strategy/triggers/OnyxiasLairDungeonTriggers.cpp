
#include "playerbot/playerbot.h"
#include "OnyxiasLairDungeonTriggers.h"
#include "playerbot/policy/combat/RangedApproachPolicy.h"
#include "playerbot/policy/raid/OnyxiaWhelpEggPolicy.h"
#include "playerbot/BossTacticsMgr.h"
#include "playerbot/strategy/actions/BossTacticsActions.h"
#include "playerbot/strategy/actions/OnyxiasLairDungeonActions.h"
#include "playerbot/strategy/values/PossibleAttackTargetsValue.h"

using namespace ai;

bool OnyxiaAirborneTrigger::IsActive()
{
    Unit* target = GetTarget();
    if (!target || !sServerFacade.IsAlive(target) || target->GetEntry() != 10184)
        return false;
    // Core drives phase 2 flight via the Hover aura (17131); IsFlying()
    // covers the transition frames where auras are mid-update.
    return target->HasAura(kOnyxiaHoverAura) || target->IsFlying();
}

bool OnyxiaWhelpsTrigger::IsActive()
{
    // Toggle and map first: the default install pays nothing here.
    if (!sPlayerbotAIConfig.onyxiaWhelpsAndEggs || bot->GetMapId() != kOnyxiaMapId)
        return false;
    if (!bot->IsInWorld() || bot->IsBeingTeleported() || !sServerFacade.IsAlive(bot))
        return false;
    if (!ai->HasStrategy("onyxia", BotState::BOT_STATE_COMBAT))
        return false;
    const bool duty = IsOnyxiaWhelpDuty(ai, bot);
    if (!duty)
        return false;
    AiObjectContext* context = ai->GetAiObjectContext();
    const bool explicitOrder = !AI_VALUE(ObjectGuid, "explicit attack target").IsEmpty();
    Unit* current = AI_VALUE(Unit*, "current target");
    const bool onWhelp = current && sServerFacade.IsAlive(current) && current->GetEntry() == kOnyxianWhelpEntry;
    const OnyxiaWhelpScan scan = ScanOnyxiaWhelps(ai, bot);
    // P3: the crew's melee and its ranged keep the whelps; a tank follows its post or the generic pick.
    if (!OnyxiaGroundDuty(duty, PlayerbotAI::IsTank(bot), scan.bossAirborne))
        return false;
    return ShouldHuntOnyxiaWhelps(true, true, scan.whelpPhase, true, explicitOrder, onWhelp, scan.whelp != nullptr);
}

bool OnyxiaEggsNearTrigger::IsActive()
{
    if (!sPlayerbotAIConfig.onyxiaWhelpsAndEggs || bot->GetMapId() != kOnyxiaMapId)
        return false;
    if (!bot->IsInWorld() || bot->IsBeingTeleported() || !sServerFacade.IsAlive(bot))
        return false;
    if (!ai->HasStrategy("onyxia", BotState::BOT_STATE_COMBAT))
        return false;
    return ShouldAvoidOnyxiaEggs(true, true, IsInOnyxiaEggZone(bot->GetPositionX(), bot->GetPositionY()),
                                 FindOnyxiaEggInDanger(ai, bot) != nullptr);
}

bool OnyxiaBossDutyTrigger::IsActive()
{
    // The key, the map and the strategy first: the default install pays nothing here.
    if (!sPlayerbotAIConfig.dungeonRunAddCrew || bot->GetMapId() != kOnyxiaMapId)
        return false;
    if (!bot->IsInWorld() || bot->IsBeingTeleported() || !sServerFacade.IsAlive(bot))
        return false;
    if (!ai->HasStrategy("onyxia", BotState::BOT_STATE_COMBAT))
        return false;
    AiObjectContext* context = ai->GetAiObjectContext();
    // Her engagement, never the attacker list: "focus rti targets" collapses that list to the marked unit, so a
    // whelp mark used to hide her from the return. The same independent lookup the scan and the whelp duty read.
    Unit* const boss = ai->GetUnit(AI_VALUE(ObjectGuid, "onyxia boss"));
    const bool explicitOrder = !AI_VALUE(ObjectGuid, "explicit attack target").IsEmpty();
    Unit* current = AI_VALUE(Unit*, "current target");
    const bool onWhelp = current && sServerFacade.IsAlive(current) && current->GetEntry() == kOnyxianWhelpEntry;
    const OnyxiaWhelpScan scan = ScanOnyxiaWhelps(ai, bot);
    const bool bossValid = boss && PossibleAttackTargetsValue::IsValid(boss, bot);
    // The row's `early` mode (AiPlayerbot.DungeonRunAddCrewEarly): a ranged damage dealer past the crew stays on her
    // while she is not its target already -- re-aiming it at the current victim reports success, which ends the engine
    // tick before the rotation runs. The gate_hold rows outrank it: while they hold her off, the kill_first pick sends
    // the bot back to their blocker. They come from the table, not from this bot's fight scan: a ranged bot's attacker
    // list need not hold her at all, and the gate's blocker count is fight-wide anyway.
    ActiveBossRows gateRows;
    if (const std::vector<BossTacticRow>* const gate = GetBossTactics().Rows(bot->GetMapId(), kOnyxiaEntry))
        for (const BossTacticRow& row : *gate)
            if (row.kind == BossTacticKind::GateHold)
                gateRows.push_back(&row);
    const bool bossDuty = AI_VALUE2(uint8, "add crew early", static_cast<int32>(kOnyxiaEntry)) ==
        static_cast<uint8>(AddCrewEarly::Boss);
    const bool gateHeld = bossDuty && GateHoldsUnit(ai, gateRows, boss);
    if (OnyxiaBossDutyFires(bossDuty, explicitOrder, bossValid, gateHeld, current && current == boss))
        return true;
    return ShouldReturnToOnyxia(true, AI_VALUE2(uint8, "add crew", static_cast<int32>(kOnyxiaEntry)),
        HoverReachTakerIsRangedDps(PlayerbotAI::IsTank(bot), PlayerbotAI::IsHeal(bot), ai->IsRanged(bot)),
        scan.whelpPhase, onWhelp, explicitOrder, bossValid);
}
