
#include "playerbot/playerbot.h"
#include "CcTargetValue.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/strategy/Action.h"
#include "PossibleAttackTargetsValue.h"
#include "playerbot/GroupMembers.h"

using namespace ai;

class FindTargetForCcStrategy : public FindTargetStrategy
{
public:
    FindTargetForCcStrategy(PlayerbotAI* ai, std::string spell) : FindTargetStrategy(ai)
    {
        this->spell = spell;
        maxDistance = 0;
    }

public:
    virtual void CheckAttacker(Unit* creature, ThreatManager* threatManager)
    {
        Player* bot = ai->GetBot();

        AiObjectContext* context = ai->GetAiObjectContext();

        // Never CC over another CC (generalized from the Fear mark-only
        // branch): a mob already held by someone's breakable/unbreakable aura
        // stays held. Re-CC of our own aura of the same spell still flows
        // through "current cc target" (HasCcTargetTrigger), not this chooser.
        if (!ai->HasAura(spell, creature) &&
            (PossibleAttackTargetsValue::HasBreakableCC(creature, bot) ||
             PossibleAttackTargetsValue::HasUnBreakableCC(creature, bot)))
            return;

        // A bot assigned to this raid mark may still need to close distance.
        // Keep the normal legality/resource checks, but let the mature reach
        // prerequisite handle range for that one assigned target.

        const bool assignedTarget = AI_VALUE(Unit*, "rti cc target") == creature;
        if (!ai->CanCastSpell(spell, creature, true, nullptr, assignedTarget, true))
            return;

        if (assignedTarget)
        {
            // Never CC the last enemy of the fight: the party kills the
            // marked mob last (DPS/tank kill-last fallback), so re-CCing it
            // would only stall the fight or ping-pong sheep and damage.
            if (IsOnlyEnemyLeft(creature))
                return;
            result = creature;
            return;
        }

        // Opt-in smart auto CC ("auto cc" strategy, OFF by default): sheep the
        // loose add. Explicit marks always win (returned above), so this only
        // runs unmarked candidates. All conditions must hold:
        // - the bot may CC at all here (group combat),
        // - the candidate hits a party healer or caster, never the tank,
        // - NOBODY in the group is attacking it (victim + pets),
        // - it carries NO periodic damage aura from any source (else sheep is
        //   pointless — owner's words; shyalya check #8, but for every spell),
        // - it is not the only enemy in the fight (never sheep the last mob),
        // - it is not skull-marked (the tank's target is covered by "attacked").
        // One bot per mob falls out of the shared aura state: the first sheep
        // makes HasBreakableCC true, so every other bot's chooser skips it
        // (stage-1 guard above); one target per bot falls out of the
        // HasMyAura pre-pass in Calculate (a bot holding a sheep returns NULL).
        // A broken sheep is never re-sheeped: once DoT'd/attacked the guards
        // below reject it, and the mark path stays authoritative.
        // With the toggle on this is the only free pick: the legacy chooser
        // below must not run, since the toggle also lifts the dungeon gate.
        if (ai->HasStrategy("auto cc", BotState::BOT_STATE_COMBAT))
        {
            if (IsAutoCcTarget(creature))
                result = creature;
            return;
        }

        if (AI_VALUE(Unit*,"current target") == creature)
            return;

        if (AI_VALUE(Unit*,"rti target") == creature)
            return;

        uint8 health = creature->GetHealthPercent();
        if (health < sPlayerbotAIConfig.mediumHealth)
            return;

        float minDistance = ai->GetRange("spell");
        Group* group = bot->GetGroup();
        if (!group)
            return;

        if (AI_VALUE(uint8,"aoe count") > 2)
        {
            WorldLocation aoe = AI_VALUE(WorldLocation,"aoe position");
            if (sServerFacade.IsDistanceLessOrEqualThan(sServerFacade.getDistance2d(creature, aoe.x, aoe.y), sPlayerbotAIConfig.aoeRadius))
                return;
        }

        if (creature->HasAuraType(SPELL_AURA_PERIODIC_DAMAGE) && !(spell == "fear" || spell == "banish"))
            return;

        if (!creature->IsPlayer())
        {
            int tankCount, dpsCount;
            GetPlayerCount(creature, &tankCount, &dpsCount);
            if (!tankCount || !dpsCount)
            {
                result = creature;
                return;
            }
        }

        Group::MemberSlotList const& groupSlot = group->GetMemberSlots();
        for (Group::member_citerator itr = groupSlot.begin(); itr != groupSlot.end(); itr++)
        {
            Player *member = sObjectMgr.GetPlayer(itr->guid);
            if(!member || !sServerFacade.IsAlive(member) || member == bot || bot->GetMapId() != member->GetMapId())
                continue;

            if (!ai->IsTank(member))
                continue;

            float distance = sServerFacade.getDistance2d(member, creature);
            if (distance < minDistance)
                minDistance = distance;
        }

        if ((!result && !creature->IsPlayer()) || minDistance > maxDistance)
        {
            result = creature;
            maxDistance = minDistance;
        }
    }

private:
    // True when no other live enemy is left among the bot's attack targets.
    bool IsOnlyEnemyLeft(Unit* creature)
    {
        AiObjectContext* context = ai->GetAiObjectContext();
        std::list<ObjectGuid> attackers = AI_VALUE(std::list<ObjectGuid>, "possible attack targets");
        for (ObjectGuid const& guid : attackers)
        {
            if (guid == creature->getObjectGuid())
                continue;
            Unit* other = ai->GetUnit(guid);
            if (other && sServerFacade.IsAlive(other))
                return false;
        }
        return true;
    }

    // Smart auto-CC candidacy, see the call site for the rules.
    bool IsAutoCcTarget(Unit* creature)
    {
        Player* bot = ai->GetBot();
        Group* group = bot->GetGroup();
        if (!group || !bot->IsInWorld())
            return false;

        // Group combat only: at least two live enemies engaged with the group.
        // Count distinct attackers (one mob may hit several members).
        std::set<Unit*> liveEnemies;
        for (Player* member : LiveGroupMembers(group))
        {
            if (!member || !sServerFacade.IsAlive(member) || member->GetMapId() != bot->GetMapId())
                continue;
            for (Unit* attacker : member->GetAttackers())
                if (attacker && sServerFacade.IsAlive(attacker))
                    liveEnemies.insert(attacker);
            if (liveEnemies.size() > 1)
                break;
        }
        if (liveEnemies.size() < 2)
            return false;

        // Must hit a party healer or caster — never the tank, never nobody.
        // Victim decides; no threat-manager consult needed for the question
        // "who is this mob chewing on".
        Unit* victim = creature->GetVictim();
        if (!victim)
            return false;
        Player* victimPlayer = dynamic_cast<Player*>(victim);
        if (!victimPlayer)
            return false;
        if (ai->IsTank(victimPlayer))
            return false;
        if (!PlayerbotAI::IsHeal(victimPlayer) && !ai->IsRanged(victimPlayer))
            return false;

        // Nobody in the group may be attacking it (members + their pets);
        // this also covers the tank's target.
        for (Player* member : LiveGroupMembers(group))
        {
            if (!member || member->GetMapId() != bot->GetMapId())
                continue;
            if (member->GetVictim() == creature)
                return false;
            if (Unit* pet = member->GetPet())
                if (pet->GetVictim() == creature)
                    return false;
        }

        // No periodic damage aura from any source — sheep would be pointless.
        // Shyalya precedent (CcTargetValue check #8) exempted fear/banish;
        // here even those are pointless on a dotted mob, so no exemptions.
        if (creature->HasAuraType(SPELL_AURA_PERIODIC_DAMAGE))
            return false;

        // Not skull-marked (focus fire stays sacred).
        if (group->GetTargetIcon(7) == creature->getObjectGuid())
            return false;

        return true;
    }

    std::string spell;
    float maxDistance;
};

Unit* CcTargetValue::Calculate()
{
    std::list<ObjectGuid> possible = AI_VALUE(std::list<ObjectGuid>,"possible targets no los");

    for (std::list<ObjectGuid>::iterator i = possible.begin(); i != possible.end(); ++i)
    {
        ObjectGuid guid = *i;
        Unit* add = ai->GetUnit(guid);
        if (!add)
            continue;

        if (!ai->IsSafe(add))
            continue;

        if (ai->HasMyAura(qualifier, add))
            return NULL;

        if (qualifier == "polymorph")
        {
            if (ai->HasMyAura("polymorph: pig", add))
                return NULL;
            if (ai->HasMyAura("polymorph: turtle", add))
                return NULL;
        }
    }

    FindTargetForCcStrategy strategy(ai, qualifier);
    return FindTarget(&strategy);
}
