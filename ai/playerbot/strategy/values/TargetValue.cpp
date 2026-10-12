
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
            if (IsCcMark(attacker))
                result = attacker;
        }
    };

    // Grace for a CC mark at the start of its fight: the CC bot gets this
    // long (whole seconds of creature combat) to land its first CC before
    // the mark counts as a free enemy.
    constexpr time_t CcGraceSeconds = 3;
    // A mob running in may be a few yards outside the CC spell's range.
    constexpr float CcGraceRangeSlack = 5.0f;
    // Cast-time CC a CC bot opens with on its mark (Sap and traps cannot
    // hit a mob that is already fighting).
    char const* const GraceCcSpells[] = { "polymorph", "shackle undead", "hibernate", "banish",
        "fear", "scare beast" };
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
        if (!IsCcMark(unit))
            return false;
    }
    return true;
}

bool CcGuard::IsHeldCcMark(Unit* unit)
{
    if (!unit || !IsCcTarget(unit))
        return false;
    if (onlyMarksLeft < 0)
        onlyMarksLeft = OnlyCcMarksLeft() ? 1 : 0;
    return onlyMarksLeft == 0;
}

bool CcGuard::ProtectedEnemyNear(WorldObject* center, float radius, bool includeMarks)
{
    if (!center)
        return false;
    Player* bot = ai->GetBot();
    std::list<ObjectGuid> attackers = ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid>>("attackers")->Get();
    for (ObjectGuid const& guid : attackers)
    {
        Unit* unit = ai->GetUnit(guid);
        if (!unit || !sServerFacade.IsAlive(unit) || !center->IsWithinDistInMap(unit, radius, false))
            continue;
        if (PossibleAttackTargetsValue::HoldsBreakableCc(ai, unit, bot))
            return true;
        if (includeMarks && IsHeldCcMark(unit))
            return true;
    }
    return false;
}

bool FindNonCcTargetStrategy::IsCrowdControlled(PlayerbotAI* ai, Unit* unit)
{
    if (!unit)
        return false;
    return PossibleAttackTargetsValue::HoldsBreakableCc(ai, unit, ai->GetBot()) ||
        sServerFacade.IsFeared(unit) || ai->HasAura("banish", unit);
}

bool FindNonCcTargetStrategy::IsCastingCcAt(Unit* caster, Unit* target)
{
    Spell* spell = caster ? caster->GetCurrentSpell(CURRENT_GENERIC_SPELL) : nullptr;
    if (!spell || !spell->m_spellInfo || spell->m_targets.getUnitTarget() != target)
        return false;
    uint32 const ccMask = (1 << (MECHANIC_POLYMORPH - 1)) | (1 << (MECHANIC_BANISH - 1)) |
        (1 << (MECHANIC_FEAR - 1)) | (1 << (MECHANIC_SLEEP - 1)) | (1 << (MECHANIC_KNOCKOUT - 1)) |
        (1 << (MECHANIC_SHACKLE - 1));
    return (spell->m_spellInfo->GetAllSpellMechanicMask() & ccMask) != 0;
}

bool FindNonCcTargetStrategy::IsCcTarget(Unit* attacker)
{
    // Mark first: the protection test only runs for the one or two marks.
    return IsCcMark(attacker) && IsCcProtected(attacker);
}

bool FindNonCcTargetStrategy::IsCcProtected(Unit* unit)
{
    if (IsCrowdControlled(ai, unit))
        return true;
    // A mark of an unpulled pack stays untouched until the pull.
    if (!sServerFacade.IsInCombat(unit))
        return true;

    Group* group = ai->GetBot()->GetGroup();
    if (!group)
        return false;

    Creature* creature = unit->ToCreature();
    bool const inGrace = creature && creature->GetCombatTime() <= CcGraceSeconds;

    Group::MemberSlotList const& groupSlot = group->GetMemberSlots();
    for (Group::member_citerator itr = groupSlot.begin(); itr != groupSlot.end(); itr++)
    {
        Player* player = sObjectMgr.GetPlayer(itr->guid);
        if (!player || !sServerFacade.IsAlive(player) || !ai->IsSafe(player) || player->GetMapId() != unit->GetMapId())
            continue;

        // The sheep is on its way: do not hit (or drop the target of) the
        // mob under a group member's CC cast bar.
        if (IsCastingCcAt(player, unit))
            return true;

        if (!inGrace)
            continue;

        // Start of the fight: the mark's CC bot (rti cc target, "cc" on)
        // gets a moment to land the first CC if it knows a CC spell that
        // fits the mob, ready and in reach.
        PlayerbotAI* memberAi = PlayerbotAIStorage::Instance().GetAI(player);
        if (!memberAi || !memberAi->HasStrategy("cc", BotState::BOT_STATE_COMBAT))
            continue;
        if (PAI_VALUE(Unit*, "rti cc target") != unit)
            continue;
        if (!player->IsWithinDistInMap(unit, memberAi->GetRange("spell") + CcGraceRangeSlack))
            continue;
        for (char const* spellName : GraceCcSpells)
        {
            uint32 spellId = PAI_VALUE2(uint32, "spell id", spellName);
            if (!spellId || !memberAi->HasSpell(spellId) || player->HasSpellCooldown(spellId))
                continue;
            SpellEntry const* info = sServerFacade.LookupSpellInfo(spellId);
            if (!info || (info->TargetCreatureType && !(info->TargetCreatureType & unit->GetCreatureTypeMask())))
                continue;
            return true;
        }
    }

    return false;
}

bool FindNonCcTargetStrategy::IsCcMark(Unit* attacker)
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
