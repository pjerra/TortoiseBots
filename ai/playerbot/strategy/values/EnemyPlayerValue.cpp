
#include "playerbot/playerbot.h"
#include "playerbot/TbCompatPvP.h"
#include "EnemyPlayerValue.h"
#include "WorldPvpSeekChecks.h"
#include "TargetValue.h"
#include "PositionValue.h"
#include "playerbot/TbCompatBg.h"
#include "playerbot/policy/bg/BgIds.h"
#include "playerbot/policy/bg/BgTargetPolicy.h"

using namespace ai;

// The bot's side for the carrier auras (policy/bg/BgIds.h): a player fights for its own faction here, and the auras are read through the
// pure mapping so the two ids are never typed twice (forever/tools/test_bg_flag_carrier.cpp pins the mapping).
static TortoiseBots::bg::BgSide AiSide(Player* bot)
{
    return bot->GetTeam() == HORDE ? TortoiseBots::bg::BgSide::Horde : TortoiseBots::bg::BgSide::Alliance;
}

std::list<ObjectGuid> EnemyPlayersValue::Calculate()
{
    std::list<ObjectGuid> result;
    if (ai->AllowActivity(ALL_ACTIVITY))
    {
        if (bot->IsInWorld() && !bot->IsBeingTeleported())
        {
            // Check if we only need one attacker
            bool getOne = false;
            if (!qualifier.empty())
            {
                getOne = std::stoi(qualifier);
            }

            if (getOne)
            {
                // Try to get one enemy target
                result = AI_VALUE2(std::list<ObjectGuid>, "possible attack targets", 1);
                ApplyFilter(result, getOne);
            }

            // If the one enemy player failed, retry with multiple possible attack targets
            if (result.empty())
            {
                result = AI_VALUE(std::list<ObjectGuid>, "possible attack targets");
                ApplyFilter(result, getOne);
            }

            // "possible attack targets" is built exclusively from "attackers" -
            // units already in an active combat/threat relationship with the
            // bot (2026-07-27, confirmed live via debug logging: hasEnemy was
            // 0 for every bot standing right next to an untouched enemy). That
            // meant bots never proactively engaged anyone - only native combat
            // (a human player actually landing a hit) ever created a real
            // threat entry, so bots only fought back once directly attacked,
            // and other nearby bots never "assisted" either since this whole
            // value only ever looked at the bot's OWN attacker list. Falling
            // back to "possible targets" - a genuine proximity scan
            // (AnyUnfriendlyUnitInObjectRangeCheck via Cell::VisitAllObjects,
            // see PossibleTargetsValue.cpp) with no threat-table dependency -
            // gives bots a real "is anyone hostile nearby" signal. Scoped to
            // InBattleGround() only so normal world PvE bot behavior against
            // real players elsewhere doesn't change.
            if (result.empty() && bot->InBattleGround())
            {
                result = AI_VALUE(std::list<ObjectGuid>, "possible targets");
                ApplyFilter(result, getOne, true);
            }

            // Outside battlegrounds only bots holding the open-world PvP seek
            // role scan the cached proximity list for a flagged enemy player
            // to start a fight with. No new world scan: "possible targets" is
            // the same cached list the rest of the AI already uses.
            if (result.empty() && !bot->InBattleGround() && WorldPvpSeekChecks::IsSeeking(ai, bot))
            {
                result = AI_VALUE(std::list<ObjectGuid>, "possible targets");
                ApplyWorldPvpFilter(result, getOne);
            }
        }
    }

    return result;
}

bool EnemyPlayersValue::IsValid(Unit* target, Player* player)
{
    if (target)
    {
        // If the target is a player
        Player* enemyPlayer = dynamic_cast<Player*>(target);
        if (enemyPlayer)
        {
            // If the target is friendly to the player
            if (sServerFacade.IsFriendlyTo(target, player))
            {
                return false;
            }

            // Check that the target is not a mind controlled ally
            if (target->HasAuraType(SPELL_AURA_MOD_CHARM) || target->HasAuraType(SPELL_AURA_MOD_POSSESS))
            {
                Player* targetPlayer = dynamic_cast<Player*>(target);
                if (player && targetPlayer && IsInGroup_Helper(player, targetPlayer))
                {
                    return false;
                }
            }

            // A masterless random bot that died to this named player recently does
            // not INITIATE a fight with it; the trade-kill loop in Ashenvale ran
            // every ~60 s because both sides kept re-selecting each other. Owned
            // bots (real master) still obey their player. The gate is the bot's
            // own combat state: while the bot is out of combat and has no
            // attackers, the avoided killer is refused; as soon as the bot is in
            // combat (IsInCombat or any attacker — melee, ranged or caster, the
            // core sets UNIT_FLAG_IN_COMBAT for spells via SetInCombatWith),
            // normal target selection applies and retaliation works with no new
            // heuristics. Selection/target GUIDs deliberately never count: a
            // player merely clicking the bot must not provoke it.
            if (PlayerbotAI* playerAi = PlayerbotAIStorage::Instance().GetAI(player))
            {
                if (!playerAi->HasRealPlayerMaster() && playerAi->ShouldAvoidPlayerKiller(enemyPlayer->GetName()))
                {
                    if (!player->IsInCombat() && player->GetAttackers().empty())
                        return false;
                }

                // A player the bot gave up on (ReachTargetAction: no headway for 15 s) is
                // not picked again until the give-up expires, unless it comes after the
                // bot in plain sight - the same rule AttackersValue::IgnoreTarget applies
                // to the attacker list. Without it the order re-armed the same unreachable
                // player on the next tick and the give-up never took effect.
                std::map<ObjectGuid, uint32>& unreachable =
                    playerAi->GetAiObjectContext()->GetValue<std::map<ObjectGuid, uint32>&>("unreachable targets")->Get();
                auto givenUp = unreachable.find(target->getObjectGuid());
                if (givenUp != unreachable.end() && WorldTimer::getMSTime() < givenUp->second &&
                    (target->GetVictim() != player || !IsWithinLOSInMapCompat(player, target, true)))
                    return false;
            }

            return true;
        }
    }

    return false;
}

bool EnemyPlayersValue::CanEngageFromHere(Unit* target) const
{
    // mod-playerbots EnemyPlayerValue (79bd428): an enemy player found by proximity
    // alone is only taken when the bot can see it and could plausibly reach it - in
    // line of sight, less than 30 yd above or below, and inside the aggro range
    // (the full range for a target with less health than the bot, 20 yd otherwise).
    // The scan behind it ("possible targets") has none of these, so a player behind
    // a wall, down in the flag-room tunnel or across the field qualified, and the
    // bot stood still holding an order it could never carry out.
    float const seekRange = bot->GetHealth() > target->GetHealth() ?
        EnemyPlayerValue::GetMaxAttackDistance(bot) : 20.0f;
    if (!bot->IsWithinDist(target, seekRange))
        return false;

    if (fabs(bot->GetPositionZ() - target->GetPositionZ()) >= 30.0f)
        return false;

    return IsWithinLOSInMapCompat(bot, target, true);
}

void EnemyPlayersValue::ApplyFilter(std::list<ObjectGuid>& targets, bool getOne, bool proximityScan)
{
    std::list<ObjectGuid> filteredTargets;
    for (const ObjectGuid& targetGuid : targets)
    {
        Unit* target = ai->GetUnit(targetGuid);
        if (IsValid(target, bot) && (!proximityScan || CanEngageFromHere(target)))
        {
            filteredTargets.push_back(target->getObjectGuid());

            if (getOne)
            {
                break;
            }
        }
    }

    targets = filteredTargets;
}

void EnemyPlayersValue::ApplyWorldPvpFilter(std::list<ObjectGuid>& targets, bool getOne)
{
    std::list<ObjectGuid> filteredTargets;
    for (const ObjectGuid& targetGuid : targets)
    {
        Unit* target = ai->GetUnit(targetGuid);
        Player* enemy = dynamic_cast<Player*>(target);
        if (enemy && IsValid(target, bot) && WorldPvpSeekChecks::IsSeekTarget(ai, bot, enemy))
        {
            filteredTargets.push_back(target->getObjectGuid());

            if (getOne)
            {
                break;
            }
        }
    }

    if (filteredTargets.empty())
    {
        if (Unit* assist = WorldPvpSeekChecks::FindAssistTarget(ai, bot))
            filteredTargets.push_back(assist->getObjectGuid());
    }

    targets = filteredTargets;
}

bool HasEnemyPlayersValue::Calculate()
{
    return !context->GetValue<std::list<ObjectGuid>>("enemy player targets", 1)->Get().empty();
}

Unit* EnemyPlayerValue::Calculate()
{
    // Prioritize the duel opponent
    ObjectGuid const duelOpponent = DuelOpponentGuidCompat(bot);
    if (!duelOpponent.IsEmpty()) {
        if (Unit* opp = ObjectAccessor::GetUnit(*bot, duelOpponent)) {
            if (!sServerFacade.IsFriendlyTo(opp, bot))
                return opp;
        }
    }

    Unit* bestEnemyPlayer = nullptr;
    std::list<ObjectGuid> enemyPlayers = AI_VALUE(std::list<ObjectGuid>, "enemy player targets");
    if (kBgTargetPolicyCompat && bot->InBattleGround())
        return PickInBattleground(enemyPlayers);

    if (!enemyPlayers.empty())
    {
        const bool isMelee = !ai->IsRanged(bot);
        uint32 bestEnemyPlayerHealth = std::numeric_limits<uint32>::max();
        float bestEnemyPlayerDistance = std::numeric_limits<float>::max();

        // Use the first enemy player as a base
        Unit* firstTarget = ai->GetUnit(enemyPlayers.front());
        if (firstTarget)
        {
            bestEnemyPlayerDistance = firstTarget->GetDistance(bot);
            bestEnemyPlayerHealth = firstTarget->GetHealth();
            bestEnemyPlayer = firstTarget;
        }

        for (const ObjectGuid& targetGuid : enemyPlayers)
        {
            Unit* target = ai->GetUnit(targetGuid);
            if (target)
            {
                // Prioritize an enemy player if it has a battleground flag (the aura the enemy's own side carries, BgIds.h)
                if (target->HasAura(TortoiseBots::bg::WsgEnemyCarrierAura(AiSide(bot))))
                {
                    bestEnemyPlayer = target;
                    break;
                }

                if (isMelee)
                {
                    // Score best enemy player based on lowest distance
                    const float distanceToEnemyPlayer = target->GetDistance(bot);
                    if (distanceToEnemyPlayer < bestEnemyPlayerDistance)
                    {
                        bestEnemyPlayerDistance = distanceToEnemyPlayer;
                        bestEnemyPlayer = target;
                    }
                }
                else
                {
                    // Score best enemy player based on lowest health
                    const uint32 enemyPlayerHealth = target->GetHealth();
                    if (enemyPlayerHealth < bestEnemyPlayerHealth)
                    {
                        bestEnemyPlayerHealth = enemyPlayerHealth;
                        bestEnemyPlayer = target;
                    }
                }
            }
        }
    }

    return bestEnemyPlayer;
}


float EnemyPlayerValue::GetMaxAttackDistance(Player* bot)
{
    return MaxEnemyPlayerAttackDistanceCompat(bot);
}

// An enemy player counts as a healer for the battleground target order when its spec heals or it is casting a heal now (the test
// "enemy healer target" makes, without that value's interrupt gate: the pick is about who to hit, not what can be kicked).
static bool IsEnemyHealer(Unit* enemy)
{
    if (Player* player = dynamic_cast<Player*>(enemy))
    {
        if (PlayerbotAI::IsHeal(player))
            return true;
    }

    Spell* spell = enemy->GetCurrentSpell(CURRENT_GENERIC_SPELL);
    if (spell && PlayerbotAI::IsHealSpell(GetSpellEntryCompat(spell->m_spellInfo)))
        return true;
    spell = enemy->GetCurrentSpell(CURRENT_CHANNELED_SPELL);
    return spell && PlayerbotAI::IsHealSpell(GetSpellEntryCompat(spell->m_spellInfo));
}

// The battleground target order (policy/bg/BgTargetPolicy.h): the engine's own list of valid enemy players, ranked by the pure policy.
// The objective the leash reads is the "bg objective" position, which the objective strategy of the match sets.
Unit* EnemyPlayerValue::PickInBattleground(std::list<ObjectGuid> const& enemyPlayers)
{
    if (enemyPlayers.empty())
        return nullptr;

    uint32 const ownCarrierAura = TortoiseBots::bg::WsgOwnCarrierAura(AiSide(bot));
    uint32 const enemyCarrierAura = TortoiseBots::bg::WsgEnemyCarrierAura(AiSide(bot));

    TortoiseBots::bg::BgTargetContext policyContext;
    policyContext.bound = ai->HasRealPlayerMaster();

    PositionEntry objective = context->GetValue<PositionMap&>("position")->Get()["bg objective"];
    policyContext.hasObjective = objective.isSet() && objective.mapId == bot->GetMapId();

    // The human's target: what he is hitting, else what he has selected ("master target" is the master himself, the follow target).
    ObjectGuid masterTargetGuid;
    if (policyContext.bound)
    {
        if (Player* master = ai->GetMaster())
        {
            if (Unit* victim = master->GetVictim())
                masterTargetGuid = victim->getObjectGuid();
            else
                masterTargetGuid = master->GetSelectionGuid();
        }
    }

    std::vector<Unit*> units;
    std::vector<TortoiseBots::bg::BgTargetCandidate> candidates;
    for (ObjectGuid const& guid : enemyPlayers)
    {
        Unit* enemy = ai->GetUnit(guid);
        if (!enemy)
            continue;

        TortoiseBots::bg::BgTargetCandidate candidate;
        candidate.handle = uint32(units.size());
        candidate.distanceYd = enemy->GetDistance(bot);
        if (policyContext.hasObjective)
            candidate.objectiveDistanceYd = sServerFacade.getDistance2d(enemy, objective.x, objective.y);
        candidate.healthPct = enemy->GetHealthPercent();
        candidate.isFlagCarrier = enemy->HasAura(enemyCarrierAura);
        candidate.isHealer = IsEnemyHealer(enemy);
        candidate.isMyAttacker = enemy->GetVictim() == bot;
        candidate.isMasterTarget = !masterTargetGuid.IsEmpty() && masterTargetGuid == enemy->getObjectGuid();
        if (Unit* victim = enemy->GetVictim())
        {
            if (victim->HasAura(ownCarrierAura))
                candidate.hitsOurCarrier = true;
            if (Player* victimPlayer = dynamic_cast<Player*>(victim))
            {
                if (victimPlayer->GetTeam() == bot->GetTeam() && PlayerbotAI::IsHeal(victimPlayer))
                    candidate.hitsOurHealer = true;
            }
        }

        units.push_back(enemy);
        candidates.push_back(candidate);
    }

    TortoiseBots::bg::BgTargetPick const pick = TortoiseBots::bg::BgPickTarget(candidates, policyContext);
    return pick.found ? units[pick.handle] : nullptr;
}
