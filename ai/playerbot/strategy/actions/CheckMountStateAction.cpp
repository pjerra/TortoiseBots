
#include "playerbot/MountDismountPolicy.h"
#include "playerbot/policy/travel/MountShopPolicy.h"
#include "playerbot/policy/travel/MountBreakEvenPolicy.h"
#include "playerbot/GroupMembers.h"
#include "playerbot/playerbot.h"
#include "CheckMountStateAction.h"
#include "playerbot/strategy/values/PositionValue.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/strategy/values/MountValues.h"
#include "playerbot/TbCompatRIDE.h"
#include "playerbot/TravelMgr.h"

using namespace ai;

uint64 extractGuid(WorldPacket& packet);

bool CheckMountStateAction::Execute(Event& event)
{
    Player* requester = event.GetOwner() ? event.GetOwner() : GetMaster();
    bool const acted = Decide(requester);
    safeMountRoll = -1;
    return acted;
}

bool CheckMountStateAction::DebugMount() const
{
    return !planOnly && ai->HasStrategy("debug mount", BotState::BOT_STATE_NON_COMBAT);
}

bool CheckMountStateAction::Decide(Player* requester)
{
    Player* groupMaster = ai->GetGroupMaster();

    if (bot->IsMounted() && (bot->GetTransport() || bot->IsTaxiFlying() || bot->IsBeingTeleported()))
    {
        if (DebugMount())
            ai->TellPlayerNoFacing(requester, "Unmount. On a taxi or a boat/zeppelin.");

        return UnMount();
    }

    bool hasAttackers = AI_VALUE(bool, "has attackers");
    bool hasEnemy = AI_VALUE(bool, "has enemy player targets") || AI_VALUE(Unit*, "dps target");

    bool farFromMaster = false;

    if(groupMaster)
        farFromMaster = bot->GetMapId() != groupMaster->GetMapId() || bot->GetDistance(groupMaster) > sPlayerbotAIConfig.sightDistance;

    bool canAttackTarget = false;
    bool shouldChaseTarget = false;
    bool farFromTarget = false;
    // The target the combat decision below can dismount for (a dps pick the
    // engine holds, or an enemy player pick). Null when there is no enemy.
    Unit* fightTarget = AI_VALUE(Unit*, "dps target");
    if (!fightTarget)
        fightTarget = AI_VALUE(Unit*, "enemy player target");

    if (hasEnemy)
    {
        float distToTarget = AI_VALUE(Unit*, "current target") ? AI_VALUE2(float, "distance", "current target") : 0;
        canAttackTarget = sServerFacade.IsDistanceLessThan(distToTarget, GetAttackDistance());
        shouldChaseTarget = sServerFacade.IsDistanceGreaterThan(distToTarget, 45.0f) && AI_VALUE2(bool, "moving", "current target");
        farFromTarget = sServerFacade.IsDistanceGreaterThan(distToTarget, 40.0f);
    }

    bool IsMounted = AI_VALUE2(uint32, "current mount speed", "self target");
    bool IsLeaderMounted = groupMaster && AI_VALUE2(uint32, "current mount speed", "master target");

    //Mount up in battle grounds
    if (bot->InBattleGround())
    {
        if (WorldPosition(bot).currentHeight() < -5.0f)
            return UnMount();

        if (!canAttackTarget)
        {
            if (!hasAttackers || (farFromTarget && !bot->IsInCombat()))
            {
                if (CanMountInBg())
                {
                    if (DebugMount() && !IsMounted)
                        ai->TellPlayerNoFacing(requester, "Mount in bg. No attackers or far from target and not in combat.");

                    return Mount(requester);
                }
            }
        }
    }

    //Unmounted when able to attack a target the bot is actually fighting or
    // pulling - not for a dps pick the travelling engine merely holds. A
    // masterless bot riding past a nearby hostile (dps target set, nobody
    // fighting anybody) kept dismounting every tick and re-mounting on the
    // next one (issue #1097: same-spot mount rows for 10+ min). The donor
    // (mod-playerbots) only dismounts within reach while in combat with
    // attackers or for an engaged enemy/dps pick.
    if (canAttackTarget && fightTarget)
    {
        bool const targetFightsBot = fightTarget->GetVictim() == bot ||
            fightTarget->GetThreatManager().getThreat(bot) > 0.0f;
        ObjectGuid const orderedAttack = AI_VALUE(ObjectGuid, "attack target");
        ObjectGuid const orderedExplicit = AI_VALUE(ObjectGuid, "explicit attack target");
        Unit* const orderedPull = AI_VALUE(Unit*, "pull target");
        bool const botOrderedToAttack = (!orderedAttack.IsEmpty() && orderedAttack == fightTarget->getObjectGuid()) ||
            (!orderedExplicit.IsEmpty() && orderedExplicit == fightTarget->getObjectGuid()) ||
            (orderedPull && orderedPull == fightTarget);
        if (!ai::CombatDismountAllowed(bot->IsInCombat(), targetFightsBot, botOrderedToAttack))
            canAttackTarget = false;
    }
    if (canAttackTarget)
    {
        if (DebugMount() && IsMounted)
            ai->TellPlayerNoFacing(requester, "Unmount. Able to attack target.");
        return UnMount();
    }

    //No more logic needed in bg.
    if (bot->InBattleGround())
        return false;

    //Chase to current target.
    if (!canAttackTarget && (farFromTarget || shouldChaseTarget))
    {
        if (DebugMount() && !IsMounted)
            ai->TellPlayerNoFacing(requester, "Mount. Unable to attack target and target is far or chaseable.");

        return Mount(requester);
    }

    //Following master and close to master that is unmounted.
    if ((ai->HasStrategy("follow", BotState::BOT_STATE_NON_COMBAT) ||
        ai->HasStrategy("wander", BotState::BOT_STATE_NON_COMBAT)) &&
        groupMaster && groupMaster != bot &&
        !farFromMaster && !IsLeaderMounted)
    {
        if (DebugMount() && IsMounted)
            ai->TellPlayerNoFacing(requester, "Unmount. Near umounted group master.");

        return UnMount();
    }

    //Near guard position
    if (ai->HasStrategy("guard", ai->GetState()))
    {
        PositionMap& posMap = AI_VALUE(PositionMap&, "position");
        PositionEntry guardPosition = posMap["guard"];

        float distance = AI_VALUE2(float, "distance", "position_guard");

        if (guardPosition.isSet() && distance < ai->GetRange("follow"))
        {
            if (DebugMount() && IsMounted)
                ai->TellPlayerNoFacing(requester, "Unmount. Near umounted guard position.");

            return UnMount();
        }
    }

    //Near stay position
    if (ai->HasStrategy("stay", ai->GetState()))
    {
        PositionMap& posMap = AI_VALUE(PositionMap&, "position");
        PositionEntry stayPosition = posMap["stay"];

        float distance = AI_VALUE2(float, "distance", "position_stay");

        if (stayPosition.isSet() && distance < ai->GetRange("follow"))
        {
            if (DebugMount() && IsMounted)
                ai->TellPlayerNoFacing(requester, "Unmount. Near stay location.");

            return UnMount();
        }
    }

    // At a herb/ore node: the gather cast needs the bot on foot, and a gather trip
    // can stop a few yards short of the node while its status still reads
    // TRAVEL, so the "working" rule below never dismounted it (live 2026-10-09:
    // bots mounted 3-14 yd from their node until the 5-min move-stuck reset).
    // No mount rule below may fire here either: "rpg target far away" re-mounted
    // a bot standing at its node every tick (issue #1097).
    {
        TravelTarget* gatherTarget = AI_VALUE(TravelTarget*, "travel target");
        if (gatherTarget && dynamic_cast<GatherTravelDestination*>(gatherTarget->GetDestination()) &&
            AI_VALUE2(float, "distance", "travel target") < 20.0f)
            return IsMounted ? UnMount() : false;
    }

    //Doing stuff nearby.
    if (AI_VALUE(bool, "travel target working"))
    {
        if (DebugMount() && IsMounted)
            ai->TellPlayerNoFacing(requester, "Unmount. Near travel target.");

        return UnMount();
    }

    //Rping nearby.
    if (AI_VALUE(GuidPosition, "rpg target") && sServerFacade.IsDistanceLessThan(AI_VALUE2(float, "distance", "rpg target"), sPlayerbotAIConfig.farDistance))
    {
        if (DebugMount() && IsMounted)
            ai->TellPlayerNoFacing(requester, "Unmount. Near rpg target.");

        return UnMount();
    }

    if ((ai->HasStrategy("follow", BotState::BOT_STATE_NON_COMBAT) ||
        ai->HasStrategy("wander", BotState::BOT_STATE_NON_COMBAT)) &&
        groupMaster && groupMaster != bot)
    {
        //Mounting with master.
        if (IsLeaderMounted && !hasAttackers)
        {
            if (DebugMount() && !IsMounted)
                ai->TellPlayerNoFacing(requester, "Mount. Group master mounted and no attackers.");

            return Mount(requester);
        }

        //Mounting to move to master.
        if (farFromMaster && !bot->IsInCombat())
        {
            if (DebugMount() && !IsMounted)
                ai->TellPlayerNoFacing(requester, "Mount. Far from group master and not in combat.");

            return Mount(requester);
        }
    }

    if (!ai->IsStateActive(BotState::BOT_STATE_COMBAT) && !hasEnemy)
    {
        //Mounting to travel: only when the remaining trip is long enough
        // for the cast to pay off (see MountBreakEvenDistance), and only while
        // the bot is already walking to the target (or already mounted). A 3 s
        // mount cast consumes the whole tick - it always beats "move to travel
        // target" on relevance ties - so a standing bot whose cast never sticks
        // re-casts instead of walking: 63 same-spot repeat groups live
        // (Oct 2026, e.g. a shaman casting every ~31 s for 14 min with zero
        // displacement). Walking first means a failed cast degrades to run-speed
        // travel instead of a standstill, and a wedged walk counts toward the
        // travel move-failure drop instead of being starved by re-casts.
        if (AI_VALUE(bool, "travel target traveling") && AI_VALUE(bool, "can move around") &&
            AI_VALUE2(float, "distance", "travel target") > MountBreakEvenDistance() &&
            (bot->IsMounted() || sServerFacade.isMoving(bot)))
        {
            if (DebugMount() && !IsMounted)
                ai->TellPlayerNoFacing(requester, "Mount. Traveling some place.");

            return Mount(requester, true);
        }
        else if (!hasAttackers)
        {
            //Mounting to move to rpg target.
            if (AI_VALUE(GuidPosition, "rpg target") && sServerFacade.IsDistanceGreaterThan(AI_VALUE2(float, "distance", "rpg target"), std::max(sPlayerbotAIConfig.sightDistance, MountBreakEvenDistance())))
            {
                if (DebugMount() && !IsMounted)
                    ai->TellPlayerNoFacing(requester, "Mount. Rpg target far away.");

                return Mount(requester, true);
            }

            //Mounting in safe place: only for bots with a real-player master
            // (pre-mounted so they can follow when the party moves). Random
            // masterless bots never coin-flip a mount here: a 3 s cast for a
            // speculative hop is pure loss.
            if (ai->HasRealPlayerMaster() && !ai->HasStrategy("guard", ai->GetState()) && !ai->HasStrategy("stay", ai->GetState()) && !AI_VALUE(std::list<ObjectGuid>, "possible rpg targets").empty() && SafeMountRoll())
            {
                if (DebugMount() && !IsMounted)
                    ai->TellPlayerNoFacing(requester, "Mount. Near rpg targets.");

                return Mount(requester, true);
            }
        }

        //Far from guard position
        if (ai->HasStrategy("guard", ai->GetState()))
        {
            PositionMap& posMap = AI_VALUE(PositionMap&, "position");
            PositionEntry guardPosition = posMap["guard"];

            float distance = AI_VALUE2(float, "distance", "position_guard");

            if (guardPosition.isSet() && distance > 40.0f)
            {
                if (DebugMount() && !IsMounted)
                    ai->TellPlayerNoFacing(requester, "Mount. Move to guard.");

                return Mount(requester);
            }
        }

        //Far from stay position
        if (ai->HasStrategy("stay", ai->GetState()))
        {
            PositionMap& posMap = AI_VALUE(PositionMap&, "position");
            PositionEntry guardPosition = posMap["stay"];

            float distance = AI_VALUE2(float, "distance", "position_stay");

            if (guardPosition.isSet() && distance > 40.0f)
            {
                if (DebugMount() && !IsMounted)
                    ai->TellPlayerNoFacing(requester, "Mount. Move to stay.");

                return Mount(requester);
            }
        }
    }

    return false;
}

bool CheckMountStateAction::isUseful()
{
    if (bot->IsDead())
        return false;

    if (!bot->IsInWorld())
        return false;

    bool isOutdoor = IsOutdoorsCompat(bot);
    if (!isOutdoor)
        return false;

    if (bot->IsTaxiFlying())
        return false;

    if (bot->GetClass() == CLASS_ROGUE && bot->InBattleGround() && (ai->HasAura("stealth", bot) || ai->HasAura("sprint", bot)))
        return false;

    if (bot->GetClass() == CLASS_DRUID && bot->InBattleGround() && (ai->HasAura("prowl", bot) || ai->HasAura("dash", bot)))
        return false;


    if (!ai->HasStrategy("mount", BotState::BOT_STATE_NON_COMBAT) && !bot->IsMounted())
        return false;

    if (!bot->IsMounted() && bot->IsInWater())
        return false;

    // Do not use with BG Flags, except forms like "Travel Form" and "Ghost Wolf"
    if (bot->HasAura(23333) || bot->HasAura(23335))
{
    if (!bot->HasSpell(783) && !bot->HasSpell(2645))
        return false;
}


    // Only mount if BG starts in less than 30 sec
    if (bot->InBattleGround())
    {
        if (BattlegroundMountStartBlockedCompat(bot))
            return false;
    }

    if (!MapAllowsMountsCompat(bot) && bot->GetMapId() != 531)
        return false;

    if (AI_VALUE(std::vector<MountValue>, "mount list").empty())
        return false;

    // Useful only when the decision would change something. Execute returns
    // false whenever the bot is already in the wanted state (mounted at its
    // best speed on a trip, on foot where it would dismount), and every
    // trigger re-queues it each tick: a riding bot moves, which clears the
    // failure backoff, so those no-ops ran as failures every tick (live
    // 2026-10-12: 58 ACTION_LOOP anomalies from 47 pool bots in 50 min, the
    // sampled ones mounted at ~11-13 y/s on "travel target traveling").
    planOnly = true;
    safeMountRoll = -1;
    bool const wouldAct = Decide(GetMaster());
    planOnly = false;
    return wouldAct;
}

bool CheckMountStateAction::SafeMountRoll()
{
    if (planOnly || safeMountRoll < 0)
        safeMountRoll = urand(0, 100) > 50 ? 1 : 0;
    return safeMountRoll == 1;
}

bool CheckMountStateAction::CanMountInBg() const
{
    // Turtle keeps the WS flag-stand rule; ForeverCore refuses the battleground
    // branch (the client flag objects are not its objects). Both answer LESS,
    // never more, than the legacy check.
    return CanMountInBattlegroundCompat(bot);
}

float CheckMountStateAction::GetAttackDistance() const
{
    switch (bot->GetClass())
    {
    case CLASS_WARRIOR:
    case CLASS_PALADIN:
        return 10.0f;
    case CLASS_ROGUE:
        return 50.0f;
    }
    /*if (enemy)
    attack_distance /= 2;*/

    if (ai->IsHeal(bot) || ai->IsRanged(bot))
        return 40.0f;

    return 35.0f;
}

float CheckMountStateAction::MountBreakEvenDistance() const
{
    // 0 disables the threshold (mount for any travel/RPG trip).
    if (sPlayerbotAIConfig.mountBreakEvenFactor <= 0.0f)
        return 0.0f;
    // Best mount usable HERE at the rider's real speed: one SKILL_RIDING
    // lookup, then bot-aware scoring (turtle 9% at 18, 60 at 75, 100 at
    // 150). Same usability gates as Mount(): location first, then item
    // presence and spell ownership, so an AQ-only 99% mount outside AQ (or
    // a missing item) cannot collapse the threshold.
    uint32 riderSpeed = MountValue::GetRiderMountSpeed(bot);
    std::vector<MountValue> mounts = AI_VALUE(std::vector<MountValue>, "mount list");
    uint32 bestSpeed = 0;
    uint32 bestSpell = 0;
    int bestPreference = -1;
    for (auto& mount : mounts)
    {
        if (!mount.IsValidLocation(bot))
            continue;
        if (mount.IsItem() && !FindItemByEntryCompat(bot, mount.GetItemProto()->ItemId))
            continue;
        if (!mount.IsItem() && !ai->HasSpell(mount.GetSpellId()))
            continue;
        uint32 speed = mount.GetSpeedFor(bot, riderSpeed);
        int pref = MountShopPolicy::GetMountPreference(bot->GetRace(), bot->GetLevel(), mount.GetItemId(), mount.GetSpellId());
        if (speed > bestSpeed || (speed == bestSpeed && pref > bestPreference))
        {
            bestSpeed = speed;
            bestSpell = mount.GetSpellId();
            bestPreference = pref;
        }
    }
    if (!bestSpeed)
        return FLT_MAX;
    // Real cast time of the winning mount: instant forms (travel form 783)
    // report 0 ms (usable on the move) and mount for any trip. Only when
    // the DBC entry is missing do we fall back to 3000 ms.
    float castSec = 3.0f;
    if (SpellEntry const* spellInfo = sServerFacade.LookupSpellInfo(bestSpell))
        castSec = GetSpellCastTime(spellInfo, bot) / 1000.0f;
    // breakEven = runSpeed * castTime / (mountBonus), where mountBonus is
    // the fractional gain (speed % / 100). Base run is 7 y/s in 1.12.
    return ai::MountBreakEvenDistance(7.0f, castSec, bestSpeed, sPlayerbotAIConfig.mountBreakEvenFactor);
}

bool CheckMountStateAction::Mount(Player* requester, bool limitSpeedToGroup)
{
    uint32 currentSpeed = AI_VALUE2(uint32, "current mount speed", "self target");

    uint32 maxSpeed = 9999;

    if (limitSpeedToGroup && bot->GetGroup() && ai->IsGroupLeader())
    {
        for (Player* member : LiveGroupMembers(bot->GetGroup()))
        {
            if (member == bot)
                continue;

            if (!ai->IsSafe(member))
                continue;

            if (!PlayerbotAIStorage::Instance().GetAI(member))
                continue;

            if (!member->IsAlive())
                continue;

            PlayerbotAI* memberAi = PlayerbotAIStorage::Instance().GetAI(member);
            if (!memberAi)
                continue;

            if (!(memberAi->HasStrategy("follow", BotState::BOT_STATE_NON_COMBAT) ||
                memberAi->HasStrategy("wander", BotState::BOT_STATE_NON_COMBAT)))
                continue;

            if (WorldPosition(bot).distance(member) > sPlayerbotAIConfig.reactDistance * 5)
                continue;

            if (auto* speedValue = memberAi->GetAiObjectContext()->GetValue<uint32>(
                    "max mount speed"))
                maxSpeed = std::min(maxSpeed, speedValue->Get());
        }
    }

    std::vector<MountValue> mountList = AI_VALUE(std::vector<MountValue>, "mount list");

    // One SKILL_RIDING lookup per mount attempt; the sort must not probe
    // the skill map on every comparison.
    uint32 riderSpeed = MountValue::GetRiderMountSpeed(bot);
    Player* sortBot = bot;
    // Stable per-bot choice at equal speed: no reshuffle (issue #1097:
    // Warhorse vs Stag alternation was pure shuffle noise), and ties break
    // by a hash of bot guid + spell id - one bot always rides the same
    // mount while bots differ from each other (a global spell-id order
    // would put every bot on the same mount). std::sort is not stable, so
    // the tie-break is explicit and deterministic.
    uint32 const sortGuid = bot->GetGUIDLow();
    uint8 const botRace = bot->GetRace();
    uint8 const botLevel = bot->GetLevel();
    std::sort(mountList.begin(), mountList.end(), [sortBot, riderSpeed, sortGuid, botRace, botLevel](MountValue const& i, MountValue const& j)
        {
            uint32 const si = i.GetSpeedFor(sortBot, riderSpeed);
            uint32 const sj = j.GetSpeedFor(sortBot, riderSpeed);
            if (si != sj)
                return si > sj;
            int const pi = MountShopPolicy::GetMountPreference(botRace, botLevel, i.GetItemId(), i.GetSpellId());
            int const pj = MountShopPolicy::GetMountPreference(botRace, botLevel, j.GetItemId(), j.GetSpellId());
            if (pi != pj)
                return pi > pj;
            return ai::StableMountTieBreak(sortGuid, i.GetSpellId()) < ai::StableMountTieBreak(sortGuid, j.GetSpellId());
        });
    for (auto& mount : mountList)
    {
        uint32 mountSpeed = mount.GetSpeedFor(bot, riderSpeed);
        if (mountSpeed > maxSpeed)
            continue;

        if (currentSpeed > maxSpeed)
        {
            if (DebugMount())
                ai->TellPlayerNoFacing(requester, "Mounted, unmount to mount slower next time.");
            return UnMount();
        }

        if (DebugMount())
            ai->TellPlayerNoFacing(requester, "Try to mount with " + chat->formatSpell(mount.GetSpellId()));

        if (currentSpeed >= mountSpeed)
        {
            if (DebugMount())
                ai->TellPlayerNoFacing(requester, "Speed not faster than current.");

            return false;
        }

        if (currentSpeed) //Already mounted
        {
            if (DebugMount())
                ai->TellPlayerNoFacing(requester, "Mounted, unmount to mount faster next time.");
            return UnMount();
        }

        // Usability first, so an unusable mount neither cancels a form nor
        // stops the bot, and planOnly can answer without side effects.
        if (!mount.IsValidLocation(bot))
        {
            if (DebugMount())
                ai->TellPlayerNoFacing(requester, "Bot can not use this mount here.", PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, true, false);
            continue;
        }

        if (mount.IsItem() && !FindItemByEntryCompat(bot, mount.GetItemProto()->ItemId))
        {
            if (DebugMount())
                ai->TellPlayerNoFacing(requester, "Bot does not have this mount.", PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, true, false);
            continue;
        }

        if (!mount.IsItem() && !ai->HasSpell(mount.GetSpellId()))
        {
            if (DebugMount())
                ai->TellPlayerNoFacing(requester, "Bot does not have this mount spell.", PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, true, false);
            continue;
        }

        if (planOnly)
            return true;

        ai->RemoveShapeshift();

        bool wasMoving = sServerFacade.isMoving(bot);

        if (wasMoving)
        {
            ai->StopMoving();
        }
        // A standing bot keeps its follow generator, and with it the on-foot
        // follow offset, through the mount cast. Drop it so "update follow"
        // re-issues the follow with the mounted spacing.
        else if (bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == FOLLOW_MOTION_TYPE)
        {
            ai->StopMoving();
        }

        bool didMount = false;

        if (mount.IsItem())
        {
            if (UseItem(requester, mount.GetItemProto()->ItemId))
            {
                SetDuration(3000U); // 3s
                didMount = true;
            }
            else
            {
                if (DebugMount())
                    ai->TellPlayerNoFacing(requester, "Mounting failed.", PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, true, false);
            }
        }
        else
        {
            uint32 castDuration;
            if (ai->CastSpell(mount.GetSpellId(), bot, nullptr, true, &castDuration))
            {
                sPlayerbotAIConfig.logEvent(ai, "CheckMountStateAction", sServerFacade.LookupSpellInfo(mount.GetSpellId())->SpellName[0], std::to_string(mountSpeed));
                SetDuration(castDuration);
                didMount = true;
            }
            else
            {
                if (DebugMount())
                    ai->TellPlayerNoFacing(requester, "Mounting spell failed.", PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, true, false);
            }
        }

        if (didMount)
        {
            if (wasMoving)
            {
                ai->HandleCommand(CHAT_MSG_WHISPER, "queue do check mount state", *bot);
                ai->TellDebug(requester, "was moving trying to mount again.", "debug mount");
            }

            if (DebugMount())
                ai->TellPlayerNoFacing(requester, "Mounting.");

            return didMount;
        }
    }

    return false;
}

bool CheckMountStateAction::UnMount() const
{
    if (!AI_VALUE2(uint32, "current mount speed", "self target"))
        return false;

    if (bot->IsFlying() && WorldPosition(bot).currentHeight() > 10.0f)
    {
        return false;
    }

    if (planOnly)
        return true;

    if (bot->IsMounted())
    {
        ai->Unmount();

        // Back to the on-foot follow offset (see Mount()).
        if (bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == FOLLOW_MOTION_TYPE)
            ai->StopMoving();
    }

    ai->RemoveShapeshift();

    return true;
}
