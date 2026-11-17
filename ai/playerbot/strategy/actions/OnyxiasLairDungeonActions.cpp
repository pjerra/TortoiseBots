
#include "playerbot/playerbot.h"
#include "OnyxiasLairDungeonActions.h"
#include "playerbot/CastCancelLog.h"

#include <set>

using namespace ai;
#include "playerbot/ServerFacade.h"
#include "playerbot/GroupMembers.h"
#include "playerbot/BossTacticsMgr.h"
#include "playerbot/BossTacticsPolicy.h"
#include "BossTacticsActions.h"
#include "playerbot/policy/raid/OnyxiaBreathPolicy.h"
#include "playerbot/policy/raid/OnyxiaWhelpEggPolicy.h"
#include "playerbot/BossTacticsStand.h"
#include "ChooseTargetActions.h"
#include "playerbot/strategy/values/PossibleAttackTargetsValue.h"

namespace
{
// Donor safe-zone pairs per breath axis (Onyxia's lair map coords):
// 0 = N-S axis, 1 = E-W axis, 2 = SE-NW axis, 3 = SW-NE axis.
struct BreathSafeSpot { float x, y, z; };
BreathSafeSpot const kBreathSafeSpots[4][2] =
{
    { { -10.0f, -180.0f, -87.0f }, { -20.0f, -250.0f, -88.0f } },
    { { 20.0f, -210.0f, -85.5f }, { -75.0f, -210.0f, -83.4f } },
    { { -60.0f, -195.0f, -85.0f }, { 10.0f, -240.0f, -85.9f } },
    { { 7.0f, -185.0f, -86.2f }, { -60.0f, -240.0f, -85.2f } },
};
} // namespace

bool OnyxiaBreathSafeZoneAction::isUseful()
{
    if (!MovementAction::isUseful())
        return false;
    if (!sPlayerbotAIConfig.bossStandOutranksBreathDodge)
        return true;
    return !BreathDodgeYieldsToStand(sPlayerbotAIConfig.bossStandOutranksBreathDodge, FindBossStand(ai, bot).valid);
}

bool OnyxiaBreathSafeZoneAction::Execute(Event& event)
{
    (void)event;
    // Boss via the attacker scan (works for whelp tanks/melee/healers
    // whose own target is not Onyxia). Axis from boss facing: the core
    // faces the breath destination before clearing target, so facing maps
    // to the breath lane (N-S axis 0, E-W 1, SE-NW 2, SW-NE 3).
    AiObjectContext* context = ai->GetAiObjectContext();
    const std::list<ObjectGuid> attackers = AI_VALUE(std::list<ObjectGuid>, "attackers");
    Unit* boss = nullptr;
    for (const ObjectGuid& attackerGuid : attackers)
    {
        Unit* attacker = ai->GetUnit(attackerGuid);
        if (attacker && attacker->GetEntry() == kOnyxiaEntry && sServerFacade.IsAlive(attacker))
        {
            boss = attacker;
            break;
        }
    }
    if (!boss)
        return false;
    int axis = BreathAxisFromFacing(boss->GetOrientation());
    // Nearest of the pair; hold when already inside (donor early-out).
    BreathSafeSpot const* best = nullptr;
    float bestDist = FLT_MAX;
    for (BreathSafeSpot const& spot : kBreathSafeSpots[axis])
    {
        float dist = bot->GetDistance2d(spot.x, spot.y);
        if (dist < bestDist)
        {
            bestDist = dist;
            best = &spot;
        }
    }
    if (!best)
        return false;
    if (!ShouldMoveToBreathSafeZone(true, bot->IsWithinDist2d(best->x, best->y, kBreathSafeZoneRadius)))
        return false;
    bot->AttackStop();
    ai::LogCastCancelStop(ai, "onyxia_breath", bot->GetDistance2d(best->x, best->y));
    bot->CastStop();
    return MoveTo(bot->GetMapId(), best->x, best->y, best->z, false, IsReaction(), false, true, "onyxia_breath");
}

namespace
{
bool IsEngagedOnyxia(Unit* unit, Player* bot)
{
    return unit && IsOnyxiaBossUnit(unit->GetEntry(), sServerFacade.IsAlive(unit), unit->IsInMap(bot),
                                    unit->IsInCombat());
}
} // namespace

ObjectGuid OnyxiaBossValue::Calculate()
{
    // The raid roster answers first, not the bot's own attacker list: "focus rti targets" collapses that list to the
    // marked unit, so a whelp mark hid her and the P3 ground phase never armed. Every live member's victim and
    // attackers is the engagement the raid sees (the source the Fankriss and Patchwerk boss lookups read); a bot
    // without a group scans itself.
    Group* const group = bot->GetGroup();
    if (!group)
    {
        if (IsEngagedOnyxia(bot->GetVictim(), bot))
            return bot->GetVictim()->getObjectGuid();
        for (Unit* attacker : bot->GetAttackers())
            if (IsEngagedOnyxia(attacker, bot))
                return attacker->getObjectGuid();
        return ObjectGuid();
    }
    for (Player* member : LiveGroupMembers(group))
    {
        if (!member || !member->IsInWorld() || member->GetMapId() != bot->GetMapId() ||
            member->GetInstanceId() != bot->GetInstanceId() || !sServerFacade.IsAlive(member))
            continue;
        if (IsEngagedOnyxia(member->GetVictim(), bot))
            return member->GetVictim()->getObjectGuid();
        for (Unit* attacker : member->GetAttackers())
            if (IsEngagedOnyxia(attacker, bot))
                return attacker->getObjectGuid();
    }
    return ObjectGuid();
}

// A whelp's victim rank (OnyxiaWhelpVictimRank): a raid tank it eats first, then a healer, the rest last. The ground
// pick and the kill action's switch both read it, so the two rank the same whelp the same way.
static int OnyxiaWhelpRankOf(Unit* whelp)
{
    Unit* const victim = whelp ? whelp->GetVictim() : nullptr;
    Player* const victimPlayer = victim && victim->GetTypeId() == TYPEID_PLAYER ? static_cast<Player*>(victim) : nullptr;
    return OnyxiaWhelpVictimRank(victimPlayer && PlayerbotAI::IsTank(victimPlayer),
                                 victimPlayer && PlayerbotAI::IsHeal(victimPlayer));
}

// The crew bots already hitting a whelp: the living group members on its attacker list that are neither tanks nor
// healers (the crew's own, by their own roles: no AI value walks, since the kill action reads this every tick of a
// ground fight). A pet counts for its owner.
static int OnyxiaWhelpHitters(Player* bot, Unit* whelp)
{
    Group* const group = bot->GetGroup();
    std::set<uint64> counted;
    int hitters = 0;
    for (Unit* attacker : whelp->GetAttackers())
    {
        Player* const player = attacker ? attacker->GetCharmerOrOwnerPlayerOrPlayerItself() : nullptr;
        if (!player || player == bot || !sServerFacade.IsAlive(player))
            continue;
        if (!group || !group->IsMember(player->GetObjectGuid()))
            continue;
        if (!counted.insert(player->GetObjectGuid().GetRawValue()).second)
            continue;
        if (PlayerbotAI::IsTank(player) || PlayerbotAI::IsHeal(player))
            continue;
        ++hitters;
    }
    return hitters;
}

OnyxiaWhelpScan ai::ScanOnyxiaWhelps(PlayerbotAI* ai, Player* bot)
{
    OnyxiaWhelpScan scan;
    AiObjectContext* context = ai->GetAiObjectContext();
    // Her engagement, never the attacker list: "focus rti targets" collapses that list to the marked unit, and a
    // whelp mark used to hide her from the phase. The cached boss value walks the raid roster's victims and attackers.
    Unit* const boss = ai->GetUnit(AI_VALUE(ObjectGuid, "onyxia boss"));
    bool bossFound = false;
    float bossPct = 0.0f;
    if (boss && IsOnyxiaBossUnit(boss->GetEntry(), sServerFacade.IsAlive(boss), boss->IsInMap(bot), boss->IsInCombat()))
    {
        bossFound = true;
        scan.bossAirborne = boss->HasAura(kOnyxiaHoverAura);
        bossPct = boss->GetHealthPercent();
    }
    // Cached attackers only: whelps jump the raid, so they are on this
    // list; no grid sweep.
    const std::list<ObjectGuid> attackers = AI_VALUE(std::list<ObjectGuid>, "attackers");
    // AiPlayerbot.OnyxiaWhelpPickTankFirst: on the ground the whelp eating the raid's tank ranks first (slot 1 already
    // holds them in P3 and dies), one eating a healer second, and the distance breaks a tie inside one rank. In the
    // air the pick stays the nearest: a whelp cannot be pulled off a tank there.
    const bool pickTankFirst = sPlayerbotAIConfig.onyxiaWhelpPickTankFirst && bossFound && !scan.bossAirborne;
    float bestDist = FLT_MAX;
    int bestRank = OnyxiaWhelpVictimRank(false, false);
    int pickedRank = -1;
    // The lowest-HP whelp eating a tank: the one crowded pick the kill action may still switch to.
    float lowestTankPct = FLT_MAX;
    Unit* lowestTankWhelp = nullptr;
    // Egg positions from the bot's cached egg search, fetched at most once
    // and only for a whelp inside the padded egg-field box.
    std::vector<OnyxiaPoint> eggs;
    bool eggsLoaded = false;
    for (const ObjectGuid& guid : attackers)
    {
        Unit* unit = ai->GetUnit(guid);
        if (!unit || !sServerFacade.IsAlive(unit))
            continue;
        if (unit->GetEntry() != kOnyxianWhelpEntry)
            continue;
        const float dist = bot->GetDistance(unit);
        if (dist > kOnyxiaWhelpEngageRange)
            continue;
        bool nearEgg = false;
        if (IsInOnyxiaEggZone(unit->GetPositionX(), unit->GetPositionY()))
        {
            if (!eggsLoaded)
            {
                eggsLoaded = true;
                for (GameObject* egg : CollectOnyxiaEggs(ai))
                    eggs.push_back({ egg->GetPositionX(), egg->GetPositionY() });
            }
            nearEgg = IsNearAnyOnyxiaEgg(unit->GetPositionX(), unit->GetPositionY(), eggs.data(), eggs.size());
        }
        if (!IsOnyxiaWhelpPickable(true, dist, nearEgg))
            continue;
        if (!pickTankFirst)
        {
            if (dist < bestDist)
            {
                bestDist = dist;
                scan.whelp = unit;
            }
            continue;
        }
        const int rank = OnyxiaWhelpRankOf(unit);
        if (rank == 0 && unit->GetHealthPercent() < lowestTankPct)
        {
            lowestTankPct = unit->GetHealthPercent();
            lowestTankWhelp = unit;
        }
        if (!scan.whelp || OnyxiaWhelpPickBetter(rank, dist, bestRank, bestDist))
        {
            bestRank = rank;
            bestDist = dist;
            pickedRank = rank;
            scan.whelp = unit;
        }
    }
    scan.whelpRank = pickedRank;
    scan.whelpLowestHp = scan.whelp && scan.whelp == lowestTankWhelp;
    // The ground crew changes the add_crew's work, so it needs both keys; the whelp key itself is the action's own gate.
    const bool crewOnGround = sPlayerbotAIConfig.onyxiaWhelpCrewOnGround && sPlayerbotAIConfig.onyxiaWhelpsAndEggs &&
        sPlayerbotAIConfig.dungeonRunAddCrew;
    scan.whelpPhase = OnyxiaWhelpPhase(scan.bossAirborne, crewOnGround, bossFound, bossPct);
    return scan;
}

bool ai::IsOnyxiaWhelpDuty(PlayerbotAI* ai, Player* bot)
{
    bool const today = IsOnyxiaWhelpHunter(bot->GetClass(), PlayerbotAI::IsTank(bot), PlayerbotAI::IsHeal(bot), ai->IsRanged(bot));
    if (!sPlayerbotAIConfig.dungeonRunAddCrew)
        return today;
    AiObjectContext* context = ai->GetAiObjectContext();
    uint8 const crewState = AI_VALUE2(uint8, "add crew", static_cast<int32>(kOnyxiaEntry));
    if (crewState != static_cast<uint8>(AddCrewState::Waiting))
        return OnyxiaWhelpDutyFor(today, crewState);
    // The row's `early` mode (AiPlayerbot.DungeonRunAddCrewEarly): the crew's AoE ranged take the whelps a phase
    // before Cindarion dies, the ranged past the crew stay on her (the boss duty trigger aims them) or, on the split's
    // rest, on him (the `Switch` duty: his kill_first row holds them), so those are not on whelp duty. None (the key or
    // the mode off, Cindarion dead, the ground) falls through to today's duty.
    uint8 const early = AI_VALUE2(uint8, "add crew early", static_cast<int32>(kOnyxiaEntry));
    if (early == static_cast<uint8>(AddCrewEarly::Whelps))
        return true;
    if (early == static_cast<uint8>(AddCrewEarly::Boss) || early == static_cast<uint8>(AddCrewEarly::Switch))
        return false;
    // The row still waits (its switch unit lives): the air keeps today's duty, but the P3 ground keeps only the crew's
    // own bots on the whelps instead of every AoE ranged damage dealer of the raid.
    Unit* const boss = ai->GetUnit(AI_VALUE(ObjectGuid, "onyxia boss"));
    bool const found = boss && IsOnyxiaBossUnit(boss->GetEntry(), sServerFacade.IsAlive(boss), boss->IsInMap(bot),
                                              boss->IsInCombat());
    bool const airborne = found && boss->HasAura(kOnyxiaHoverAura);
    bool const crewOnGround = sPlayerbotAIConfig.onyxiaWhelpCrewOnGround && sPlayerbotAIConfig.onyxiaWhelpsAndEggs;
    if (!OnyxiaGroundPhase(airborne, crewOnGround, found, found ? boss->GetHealthPercent() : 0.0f))
        return today;
    return OnyxiaWaitingGroundDuty(today,
        AI_VALUE2(bool, "add crew keeps duty", static_cast<int32>(kOnyxiaEntry)));
}

std::vector<GameObject*> ai::CollectOnyxiaEggs(PlayerbotAI* ai)
{
    // Cached value, qualified by the egg entry: spawned eggs only.
    AiObjectContext* context = ai->GetAiObjectContext();
    const std::list<ObjectGuid> guids =
        AI_VALUE2(std::list<ObjectGuid>, "nearest game objects no los", std::to_string(kOnyxiaEggEntry));
    std::vector<GameObject*> eggs;
    for (const ObjectGuid& guid : guids)
    {
        GameObject* egg = ai->GetGameObject(guid);
        if (egg && sServerFacade.isSpawned(egg))
            eggs.push_back(egg);
    }
    return eggs;
}

GameObject* ai::FindOnyxiaEggInDanger(PlayerbotAI* ai, Player* bot)
{
    // Cheap box test first: outside the egg fields no search runs.
    if (!IsInOnyxiaEggZone(bot->GetPositionX(), bot->GetPositionY()))
        return nullptr;
    GameObject* nearest = nullptr;
    float bestDist = FLT_MAX;
    for (GameObject* egg : CollectOnyxiaEggs(ai))
    {
        const float dist = OnyxiaDist2d(bot->GetPositionX(), bot->GetPositionY(), egg->GetPositionX(), egg->GetPositionY());
        if (IsInsideOnyxiaEggDanger(dist) && dist < bestDist)
        {
            bestDist = dist;
            nearest = egg;
        }
    }
    return nearest;
}

bool OnyxiaKillWhelpsAction::Execute(Event& event)
{
    (void)event;
    if (!sPlayerbotAIConfig.onyxiaWhelpsAndEggs || bot->GetMapId() != kOnyxiaMapId)
        return false;
    const bool duty = IsOnyxiaWhelpDuty(ai, bot);
    if (!duty || !AI_VALUE(ObjectGuid, "explicit attack target").IsEmpty())
        return false;
    const OnyxiaWhelpScan scan = ScanOnyxiaWhelps(ai, bot);
    // On the ground the crew is the melee and its ranged; a tank keeps its post or the generic pick.
    if (!OnyxiaGroundDuty(duty, PlayerbotAI::IsTank(bot), scan.bossAirborne) || !scan.whelpPhase || !scan.whelp)
        return false;
    Unit* current = AI_VALUE(Unit*, "current target");
    if (current && sServerFacade.IsAlive(current) && current->GetEntry() == kOnyxianWhelpEntry)
    {
        // AiPlayerbot.OnyxiaWhelpPickTankFirst, grounded: the donor early-out would keep a whelp eating neither a tank
        // nor a healer while another one eats the tank, so that one gives way to the scan's pick (a tank's, then a
        // healer's). OnyxiaWhelpSwitch holds the hysteresis: one switch per 4 s, never off a whelp on a tank or a
        // healer, and a crowded pick only when it is the tank's lowest. Airborne or key off, scan.whelpRank is -1 and
        // the early-out stands exactly as before.
        const uint32 nowMs = WorldTimer::getMSTime();
        const uint32 lastMs = AI_VALUE(uint32, "onyxia whelp switch");
        const uint32 sinceMs = lastMs == 0 ? kOnyxiaWhelpSwitchMs : WorldTimer::getMSTimeDiff(lastMs, nowMs);
        if (scan.whelpRank < 0 ||
            !OnyxiaWhelpSwitch(OnyxiaWhelpRankOf(current), scan.whelpRank, sinceMs,
                OnyxiaWhelpHitters(bot, scan.whelp), scan.whelpLowestHp))
            return false;
        if (!PossibleAttackTargetsValue::IsValid(scan.whelp, bot))
            return false;
        const bool switched = Attack(bot, scan.whelp);
        if (switched)
            SET_AI_VALUE(uint32, "onyxia whelp switch", nowMs);
        return switched;
    }
    if (!PossibleAttackTargetsValue::IsValid(scan.whelp, bot))
        return false;
    return Attack(bot, scan.whelp);
}

bool OnyxiaAttackBossAction::Execute(Event& event)
{
    (void)event;
    if (!sPlayerbotAIConfig.dungeonRunAddCrew || bot->GetMapId() != kOnyxiaMapId)
        return false;
    // The trigger's own lookup: "focus rti targets" collapses the attacker list to the marked whelp, and this return
    // must still reach her.
    Unit* const boss = ai->GetUnit(AI_VALUE(ObjectGuid, "onyxia boss"));
    if (!boss || !PossibleAttackTargetsValue::IsValid(boss, bot))
        return false;
    return Attack(bot, boss);
}

float OnyxiaWhelpHoldMultiplier::GetValue(Action* action)
{
    // The early add crew's boss duty (AiPlayerbot.DungeonRunAddCrewEarly): the assist re-pick would aim the bot at a
    // whelp every tick and pull it off her, so the assist action is held while she is its target (the boss's own attack
    // action keeps it there). AoE actions are untouched. The add crew's keys and the map are its gate, not
    // AiPlayerbot.OnyxiaWhelpsAndEggs, and a gate_hold row holding her off outranks it: the assist then moves the bot
    // to that row's blocker.
    if (action && bot->GetMapId() == kOnyxiaMapId && sPlayerbotAIConfig.dungeonRunAddCrewEarly &&
        dynamic_cast<DpsAssistAction*>(action) != nullptr)
    {
        AiObjectContext* context = ai->GetAiObjectContext();
        const bool bossDuty = AI_VALUE2(uint8, "add crew early", static_cast<int32>(kOnyxiaEntry)) ==
            static_cast<uint8>(AddCrewEarly::Boss);
        if (bossDuty)
        {
            Unit* const boss = ai->GetUnit(AI_VALUE(ObjectGuid, "onyxia boss"));
            ActiveBossRows gateRows;
            if (const std::vector<BossTacticRow>* const gate =
                    GetBossTactics().Rows(bot->GetMapId(), kOnyxiaEntry))
                for (const BossTacticRow& row : *gate)
                    if (row.kind == BossTacticKind::GateHold)
                        gateRows.push_back(&row);
            if (boss && OnyxiaBossDutyHoldsAssist(bossDuty, GateHoldsUnit(ai, gateRows, boss),
                    AI_VALUE(Unit*, "current target") == boss,
                    !AI_VALUE(ObjectGuid, "explicit attack target").IsEmpty()))
                return 0.0f;
        }
    }
    if (!action || !sPlayerbotAIConfig.onyxiaWhelpsAndEggs)
        return 1.0f;
    const bool assist = dynamic_cast<DpsAssistAction*>(action) || dynamic_cast<TankAssistAction*>(action);
    const bool aoe = dynamic_cast<DpsAoeAction*>(action) != nullptr;
    if (!assist && !aoe)
        return 1.0f;
    if (bot->GetMapId() != kOnyxiaMapId)
        return 1.0f;
    const bool duty = IsOnyxiaWhelpDuty(ai, bot);
    if (!duty)
        return 1.0f;
    Unit* current = AI_VALUE(Unit*, "current target");
    const bool onWhelp = current && sServerFacade.IsAlive(current) && current->GetEntry() == kOnyxianWhelpEntry;
    if (!onWhelp || !AI_VALUE(ObjectGuid, "explicit attack target").IsEmpty())
        return 1.0f;
    // The hold lasts while the whelp phase does: airborne, or the ground crew's last 40 % (a tank's hold ends there).
    const OnyxiaWhelpScan scan = ScanOnyxiaWhelps(ai, bot);
    if (!OnyxiaGroundDuty(duty, PlayerbotAI::IsTank(bot), scan.bossAirborne))
        return 1.0f;
    return scan.whelpPhase ? 0.0f : 1.0f;
}

bool OnyxiaAvoidEggsAction::Execute(Event& event)
{
    (void)event;
    if (!sPlayerbotAIConfig.onyxiaWhelpsAndEggs || bot->GetMapId() != kOnyxiaMapId)
        return false;
    if (!ShouldAvoidOnyxiaEggs(true, true, IsInOnyxiaEggZone(bot->GetPositionX(), bot->GetPositionY()),
                               FindOnyxiaEggInDanger(ai, bot) != nullptr))
        return false;
    const OnyxiaEggExit& exit = kOnyxiaEggExits[NearestOnyxiaEggExit(bot->GetPositionX(), bot->GetPositionY())];
    return MoveTo(bot->GetMapId(), exit.x, exit.y, exit.z, false, IsReaction(), false, true, "onyxia_eggs");
}
