
#include "playerbot/playerbot.h"
#include "TargetValue.h"

#include "playerbot/ServerFacade.h"
#include "PossibleAttackTargetsValue.h"
#include "RtiTargetValue.h"
#include "Objects/Unit.h"
#include "LastMovementValue.h"
#include "playerbot/strategy/values/Formations.h"

using namespace ai;

Unit* TargetValue::FindTarget(FindTargetStrategy* strategy)
{
    std::list<ObjectGuid> attackers = ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid>>("possible attack targets")->Get();
    for (std::list<ObjectGuid>::iterator i = attackers.begin(); i != attackers.end(); ++i)
    {
        Unit* unit = ai->GetUnit(*i);
        if (!unit)
            continue;

        ThreatManager &threatManager = sServerFacade.GetThreatManager(unit);
        strategy->CheckAttacker(unit, &threatManager);
    }

    return strategy->GetResult();
}

Unit* TargetValue::GetExplicitAttackTarget()
{
    ObjectGuid guid = AI_VALUE(ObjectGuid, "explicit attack target");
    if (guid.IsEmpty())
        return nullptr;

    Unit* target = ai->GetUnit(guid);
    if (!target || !PossibleAttackTargetsValue::IsValid(target, bot,
            sPlayerbotAIConfig.sightDistance, false, false))
        return nullptr;

    return target;
}

namespace
{
    class FindLastCcTargetStrategy : public FindNonCcTargetStrategy
    {
    public:
        FindLastCcTargetStrategy(PlayerbotAI* ai) : FindNonCcTargetStrategy(ai) {}

        void CheckAttacker(Unit* attacker, ThreatManager* /*threatManager*/) override
        {
            if (result || !attacker || attacker->IsPlayer() || !sServerFacade.IsAlive(attacker))
                return;
            // Only a mark that is already fighting: never open on a marked
            // mob of an unpulled pack.
            if (!sServerFacade.IsInCombat(attacker))
                return;
            if (IsCcTarget(attacker))
                result = attacker;
        }
    };
}

Unit* TargetValue::FindLastCcTarget()
{
    FindLastCcTargetStrategy strategy(ai);
    // "possible attack targets" drops stunned/rooted/feared mobs while a
    // free one is left, so the tournaments returning nothing does not mean
    // only marks are left; check the raw attackers first.
    if (!strategy.OnlyCcMarksLeft())
        return nullptr;
    return FindTarget(&strategy);
}

bool FindNonCcTargetStrategy::OnlyCcMarksLeft(Unit* except)
{
    Player* bot = ai->GetBot();
    std::list<ObjectGuid> attackers = ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid>>("attackers")->Get();
    for (ObjectGuid const& guid : attackers)
    {
        Unit* unit = ai->GetUnit(guid);
        if (!unit || unit == except || !sServerFacade.IsAlive(unit))
            continue;
        // Same filter "possible attack targets" applies before its CC strip
        // (ignoreCC = true): crowd-controlled mobs stay in.
        if (!PossibleAttackTargetsValue::IsValid(unit, bot, sPlayerbotAIConfig.sightDistance, true, false))
            continue;
        if (!IsCcTarget(unit))
            return false;
    }
    return true;
}

bool FindNonCcTargetStrategy::IsCcTarget(Unit* attacker)
{
    Group* group = ai->GetBot()->GetGroup();
    if (group)
    {
        Group::MemberSlotList const& groupSlot = group->GetMemberSlots();
        for (Group::member_citerator itr = groupSlot.begin(); itr != groupSlot.end(); itr++)
        {
            Player *player = sObjectMgr.GetPlayer(itr->guid);
            if (!player || !sServerFacade.IsAlive(player) || !ai->IsSafe(player))
                continue;

            if (PlayerbotAIStorage::Instance().GetAI(player))
            {
                if (PAI_VALUE(Unit*,"rti cc target") == attacker)
                    return true;

                std::string rti = PAI_VALUE(std::string,"rti cc");
                int index = RtiTargetValue::GetRtiIndex(rti);
                if (index != -1)
                {
                    uint64 guid = group->GetTargetIcon(index);
                    if (guid && attacker->getObjectGuid() == ObjectGuid(guid))
                        return true;
                }
            }
        }

        uint64 guid = group->GetTargetIcon(4);
        if (guid && attacker->getObjectGuid() == ObjectGuid(guid))
            return true;
    }

    return false;
}

void FindTargetStrategy::GetPlayerCount(Unit* creature, int* tankCount, int* dpsCount)
{
    Player* bot = ai->GetBot();
    if (tankCountCache.find(creature) != tankCountCache.end())
    {
        *tankCount = tankCountCache[creature];
        *dpsCount = dpsCountCache[creature];
        return;
    }

    *tankCount = 0;
    *dpsCount = 0;

    Unit::AttackerSet attackers(creature->GetAttackers());
    for (std::set<Unit*>::const_iterator i = attackers.begin(); i != attackers.end(); i++)
    {
        Unit* attacker = *i;
        if (!attacker || !sServerFacade.IsAlive(attacker) || attacker == bot)
            continue;

        Player* player = dynamic_cast<Player*>(attacker);
        if (!player)
            continue;

        if (ai->IsTank(player))
            (*tankCount)++;
        else
            (*dpsCount)++;
    }

    tankCountCache[creature] = *tankCount;
    dpsCountCache[creature] = *dpsCount;
}

TravelTarget* LeaderTravelTargetValue::Calculate()
{
    TravelTarget* target = AI_VALUE(TravelTarget*, "travel target");

    Player* player = ai->GetGroupMaster();
    if (!player || player == bot || !PlayerbotAIStorage::Instance().GetAI(player) || !ai->IsSafe(player))
        return target;

    if (bot->GetGroup() && !ai->IsGroupLeader())
        if (!(ai->HasStrategy("follow", BotState::BOT_STATE_NON_COMBAT) ||
            ai->HasStrategy("wander", BotState::BOT_STATE_NON_COMBAT)) &&
            !ai->HasStrategy("stay", BotState::BOT_STATE_NON_COMBAT) &&
            !ai->HasStrategy("guard", BotState::BOT_STATE_NON_COMBAT))
            return target;

     TravelTarget* leaderTarget = PAI_VALUE(TravelTarget*, "travel target");

     if (!leaderTarget)
         return target;

     return leaderTarget;
}

WorldPosition LastLongMoveValue::Calculate()
{
    LastMovement& lastMove = *context->GetValue<LastMovement&>("last movement");

    if (lastMove.lastPath.empty())
        return WorldPosition();

    return lastMove.lastPath.GetBack();
}

WorldPosition HomeBindValue::Calculate()
{
    return WorldPosition(bot->GetHomeBindLocation());
}

std::string HomeBindValue::Format()
{
    WorldPosition pos = this->Calculate();
    return chat->formatWorldPosition(pos);
}

void PullTargetValue::Set(Unit* unit)
{
    guid = unit ? unit->getObjectGuid() : ObjectGuid();
}

Unit* PullTargetValue::Get()
{
    Unit* unit = nullptr;
    if (!guid.IsEmpty())
    {
        unit = sObjectAccessor.GetUnit(*bot, guid);
    }

    return unit;
}

Unit* FollowTargetValue::Calculate()
{
    Unit* followTarget = AI_VALUE(GuidPosition, "manual follow target").GetUnit(bot->GetInstanceId());
    if (followTarget == nullptr)
    {
        Formation* formation = AI_VALUE(Formation*, "formation");
        if (formation && !formation->GetTargetName().empty())
        {
            followTarget = AI_VALUE(Unit*, formation->GetTargetName());
        }
        else
        {
            followTarget = AI_VALUE(Unit*, "master target");
        }
    }

    return followTarget;
}

Unit* ClosestAttackerTargetingMeTargetValue::Calculate()
{
    Unit* result = nullptr;
    float closest = 9999.0f;

    const std::list<ObjectGuid>& attackers = AI_VALUE(std::list<ObjectGuid>, "attackers targeting me");
    for (const ObjectGuid& attackerGuid : attackers)
    {
        Unit* attacker = ai->GetUnit(attackerGuid);
        if (attacker)
        {
            const float distance = bot->GetCombatDistance(attacker);
            if (distance < closest)
            {
                closest = distance;
                result = attacker;
            }
        }
    }

    return result;
}

std::list<ObjectGuid> FriendlyManualTargetsValue::Get()
{
    value.remove_if([&](const ObjectGuid& playerGuid)
    {
        Unit* player = ai->GetUnit(playerGuid);
        if (ai->IsSafe(player))
        {
            Player* playerTarget = dynamic_cast<Player*>(player);
            if (playerTarget && IsInGroup_Helper(bot, playerTarget))
            {
                return false;
            }
        }

        return true;
    });

    return value;
}

std::list<ObjectGuid> FriendlyManualTargetsValue::LazyGet()
{
    return Get();
}
