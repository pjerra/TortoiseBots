
#include "playerbot/playerbot.h"
#include "EnemyPlayerValue.h"
#include "TargetValue.h"

using namespace ai;

namespace
{
    // AiPlayerbot.WorldPvpEngage: may this masterless pool bot open a fight with
    // a PvP-flagged enemy player outside a battleground? Ranges follow the
    // mod-playerbots proximity rule (40 yd, 20 yd when the target has more HP,
    // line of sight, within 30 yd of height); level rule: never a target 5+ levels
    // above the bot. Cheap checks first; callers only get here out of combat.
    bool CanEngageWorldPvp(PlayerbotAI* ai, Player* bot, Unit* unit)
    {
        Player* enemy = dynamic_cast<Player*>(unit);
        if (!enemy || !enemy->IsPvP())
            return false;

        if (enemy->GetLevel() >= bot->GetLevel() + 5)
            return false;

        if (sPlayerbotAIConfig.IsInPvpProhibitedZone(sServerFacade.GetAreaId(enemy)))
            return false;

        if (enemy->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_SANCTUARY))
            return false;

        const float maxDist = bot->GetHealth() > enemy->GetHealth() ? 40.0f : 20.0f;
        if (!bot->IsWithinDist(enemy, maxDist) || fabs(bot->GetPositionZ() - enemy->GetPositionZ()) >= 30.0f)
            return false;

        if (AreaTableEntry const* area = GetAreaEntryByAreaID(sServerFacade.GetAreaId(enemy)))
        {
            if (area->Flags & AREA_FLAG_CAPITAL)
                return false;
        }

        return bot->IsWithinLOSInMap(enemy);
    }
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
                ApplyFilter(result, getOne);
            }

            // Opt-in world PvP (AiPlayerbot.WorldPvpEngage): masterless pool bots
            // start the fight with a PvP-flagged enemy player in range instead of
            // only hitting back. Reuses the "possible targets" scan above, so no
            // extra world scan. Never for a real player's companions, never while
            // the bot is already fighting, never in a city.
            if (result.empty() && sPlayerbotAIConfig.worldPvpEngage && !bot->InBattleGround() &&
                !ai->GetMaster() && !bot->IsInCombat() && bot->GetAttackers().empty() &&
                !ai->IsInCapitalCity() && !bot->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_SANCTUARY) &&
                TortoiseBots::BotManager::Instance().IsRandomBot(bot->GetObjectGuid()))
            {
                std::list<ObjectGuid> candidates = AI_VALUE(std::list<ObjectGuid>, "possible targets");
                for (const ObjectGuid& guid : candidates)
                {
                    if (!guid.IsPlayer())
                        continue;

                    Unit* unit = ai->GetUnit(guid);
                    if (unit && IsValid(unit, bot) && CanEngageWorldPvp(ai, bot, unit))
                    {
                        result.push_back(guid);
                        if (getOne)
                            break;
                    }
                }
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
            }

            return true;
        }
    }

    return false;
}

void EnemyPlayersValue::ApplyFilter(std::list<ObjectGuid>& targets, bool getOne)
{
    std::list<ObjectGuid> filteredTargets;
    for (const ObjectGuid& targetGuid : targets)
    {
        Unit* target = ai->GetUnit(targetGuid);
        if (IsValid(target, bot))
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

bool HasEnemyPlayersValue::Calculate()
{
    return !context->GetValue<std::list<ObjectGuid>>("enemy player targets", 1)->Get().empty();
}

Unit* EnemyPlayerValue::Calculate()
{
    // Prioritize the duel opponent
    if (bot->m_duel && !bot->m_duel->opponent.IsEmpty()) {
        if (Unit* opp = ObjectAccessor::GetUnit(*bot, bot->m_duel->opponent)) {
            if (!sServerFacade.IsFriendlyTo(opp, bot))
                return opp;
        }
    }

    Unit* bestEnemyPlayer = nullptr;
    std::list<ObjectGuid> enemyPlayers = AI_VALUE(std::list<ObjectGuid>, "enemy player targets");
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
                // Prioritize an enemy player if it has a battleground flag
                if ((bot->GetTeam() == HORDE && target->HasAura(23333)) ||
                    (bot->GetTeam() == ALLIANCE && target->HasAura(23335)))
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
    if (!bot->GetBattleGround())
        return 60.0f;

    if (bot->InBattleGround())
    {
        BattleGround* bg = bot->GetBattleGround();
        if (!bg)
            return 40.0f;

        BattleGroundTypeId bgType = bg->GetTypeID();

        if (bgType == BATTLEGROUND_AV)
        {
            bool strifeTime = bg->GetStartTime() < (uint32)(20 * MINUTE * IN_MILLISECONDS);
            return strifeTime ? 40.0f : 10.0f;
        }
    }

    return 40.0f;
}
