#pragma once
#include "playerbot/GroupMembers.h"
#include "playerbot/PlayerbotAI.h"
#include "GenericActions.h"

#include "playerbot/ServerFacade.h"
#include "playerbot/strategy/Action.h"
#include "runtime/BotActivityLease.h"
#include "runtime/BotManager.h"
#include "runtime/RunReleaseHold.h"
#include "Group/Group.h"
#include "MovementActions.h"
#include "playerbot/strategy/values/LastMovementValue.h"
#include "ReviveFromCorpseAction.h"
#include "playerbot/TbCompatW1.h"
#include "playerbot/TravelMgr.h"
#include "playerbot/policy/travel/GraveyardTeleportPolicy.h"
#include "playerbot/policy/rpg/GroupHygienePolicy.h"
#include "playerbot/policy/death/DeathReleasePausePolicy.h"

namespace ai
{
    class ReleaseSpiritAction : public ChatCommandAction
    {
    public:
        ReleaseSpiritAction(PlayerbotAI* ai, std::string name = "release") : ChatCommandAction(ai, name) {}

    public:
        virtual bool Execute(Event& event) override
        {
            Player* requester = event.GetOwner() ? event.GetOwner() : GetMaster();
            if (sServerFacade.IsAlive(bot))
                return false;

            if (bot->GetCorpse() && HasPlayerFlagCompat(bot, PLAYER_FLAGS_GHOST))
            {
                ai->TellPlayerNoFacing(requester, "I am already a spirit", PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
                return false;
            }

            // W1 (ForeverCore): a stale corpse on the bot's own map makes BuildPlayerRepop refuse; wake the body as bones first.
            StaleCorpseBonesCompat(bot);

            WorldPacket& p = event.GetPacket();
            if (!p.empty() && p.getOpcode() == CMSG_REPOP_REQUEST)
                ai->TellPlayerNoFacing(requester, "Releasing...", PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
            else
                ai->TellPlayerNoFacing(requester, "Meet me at the graveyard", PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);

            sLog.outDetail("Bot #%d %s:%d <%s> released", bot->GetGUIDLow(), bot->GetTeam() == ALLIANCE ? "A" : "H", bot->GetLevel(), CStrCompat(bot->GetName()));

            // Both cores: the core's own release handler (RepopRequestCompat). Turtle: always true, as before; ForeverCore: false when the
            // handler did nothing (not dead, already a ghost, a teleport under way), so the action never reports a release that did not happen.
            if (!RepopRequestCompat(bot))
                return false;

            // add waiting for ress aura
            if (bot->InBattleGround() && !ai->HasAura(2584, bot))
            {
                // cast Waiting for Resurrect
                WaitingForResurrectAuraCompat(bot);
            }

            return true;
        }
    };

    class AutoReleaseSpiritAction : public ReleaseSpiritAction
    {
    public:
        // Any living group member on the bot's map whose class can resurrect?
        bool DungeonCrewHasLivingResurrecter() const
        {
            Group* group = bot->GetGroup();
            if (!group)
                return false;
            for (Player* member : LiveGroupMembers(group))
            {
                if (member == bot || !member->IsAlive() || member->GetMapId() != bot->GetMapId())
                    continue;
                switch (member->GetClass())
                {
                    case CLASS_PRIEST: case CLASS_PALADIN: case CLASS_SHAMAN: case CLASS_DRUID:
                        if (MemberKnowsOocResurrectionCompat(member))   // N3: T always true; F asks the spell book
                            return true;
                        break;
                    default:
                        break;
                }
            }
            return false;
        }

        // Any living group member on the bot's map still fighting?
        bool DungeonCrewInCombat() const
        {
            Group* group = bot->GetGroup();
            if (!group)
                return false;
            for (Player* member : LiveGroupMembers(group))
            {
                if (member != bot && member->IsAlive() && member->GetMapId() == bot->GetMapId() && member->IsInCombat())
                    return true;
            }
            return false;
        }

        // Why this dead bot is not releasing, for the host's `relhold` state key: "no-entrance" (inside a dungeon whose instance script has no
        // entrance set, ForeverCore only), "rezzer" (a living priest/paladin/shaman/druid is on the map), "combat" (a living member fights),
        // "wipe" (the dungeon-crew rule says release: nobody is left to resurrect), "none" (not held by the dungeon rule). Reads the same
        // predicates isUseful() reads; a host that wants the key finds this action with GetAction("auto release") and calls it.
        char const* ReleaseHoldReason() const
        {
            if (!sServerFacade.UnitIsDead(bot) || bot->GetCorpse())
                return "none";
            if (!InstanceEntranceKnownCompat(bot))
                return "no-entrance";
            if (TortoiseBots::BotActivityLeaseManager::Instance().GetActivity(bot->GetGUIDLow()) == TortoiseBots::BotActivity::Dungeon &&
                bot->GetMap() && bot->GetMap()->Instanceable())
            {
                if (DungeonCrewHasLivingResurrecter())
                    return "rezzer";
                if (DungeonCrewInCombat())
                    return "combat";
                return "wipe";
            }
            return "none";
        }

        AutoReleaseSpiritAction(PlayerbotAI* ai, std::string name = "auto release") : ReleaseSpiritAction(ai, name) {}

        virtual bool Execute(Event& event) override
        {
            TortoiseBots::ResetDeathReleasePause(deathPause_);
            sLog.outDetail("Bot #%d %s:%d <%s> auto released", bot->GetGUIDLow(), bot->GetTeam() == ALLIANCE ? "A" : "H", bot->GetLevel(), CStrCompat(bot->GetName()));
            // Diagnosis: a dungeon crew member that releases costs its party a corpse run
            // from the instance entrance. Name what the predicates read when it happened.
            if (bot->FindMap() && bot->FindMap()->IsDungeon())
                sLog.outDetail("[AUTO-RELEASE] %s map=%u lease=%s group=%d livingRezzer=%d crewInCombat=%d",
                    CStrCompat(bot->GetName()), bot->GetMapId(),
                    TortoiseBots::BotActivityName(TortoiseBots::BotActivityLeaseManager::Instance().GetActivity(bot->GetGUIDLow())),
                    bot->GetGroup() ? 1 : 0, DungeonCrewHasLivingResurrecter() ? 1 : 0, DungeonCrewInCombat() ? 1 : 0);

            // See ReleaseSpiritAction::Execute: false on ForeverCore when the core did not release (nothing is logged as a release then).
            StaleCorpseBonesCompat(bot);
            if (!RepopRequestCompat(bot))
                return false;

            // add waiting for ress aura
            if (bot->InBattleGround() && !ai->HasAura(2584, bot))
            {
                // cast Waiting for Resurrect
                WaitingForResurrectAuraCompat(bot);
            }

            sPlayerbotAIConfig.logEvent(ai, "AutoReleaseSpiritAction");

            return true;
        }

        virtual bool isUseful() override
        {
            if (!sServerFacade.UnitIsDead(bot))
            {
                TortoiseBots::ResetDeathReleasePause(deathPause_);
                return false;
            }

            if (bot->GetCorpse())
            {
                TortoiseBots::ResetDeathReleasePause(deathPause_);
                // W1: a dead body with a stale corpse on its own map can release after the bones (ForeverCore's BuildPlayerRepop
                // refuses otherwise). Turtle answers false here, so its rule is unchanged.
                return StaleCorpsePresentCompat(bot);
            }


            if (bot->InBattleGround())
                return !bot->GetCorpse() || !HasPlayerFlagCompat(bot, PLAYER_FLAGS_GHOST);

            if (HasPlayerFlagCompat(bot, PLAYER_FLAGS_GHOST))
            {
                TortoiseBots::ResetDeathReleasePause(deathPause_);
                return false;
            }
            // ForeverCore: a player released inside a dungeon is revived at the instance script's entrance and, with none set, at the nearest
            // graveyard OUTSIDE (Player::RepopAtGraveyard); until the host has set the entrance the bot waits at its body (relhold=no-entrance).
            // Turtle: always known (ai/cmangos-compat-d4.h), nothing changes.
            if (!InstanceEntranceKnownCompat(bot))
                return false;
            // A dungeon crew (another module holds the Dungeon activity lease) waits for
            // its own resurrection while a living group member of a resurrecting class is
            // on its map; only a wipe releases. Stock bots without a player master release
            // at once, which put crew members at the graveyard outside the instance.
            // A hired bot bound to an active dungeon run waits at its body until the run's
            // regroup (or the player) releases it; once the run is over the stock rules
            // below decide again (runtime/RunReleaseHold.h).
            TortoiseBots::RunReleaseHold::AutoRelease const verdict = TortoiseBots::RunReleaseHold::Decide(
                TortoiseBots::BotActivityLeaseManager::Instance().GetActivity(bot->GetGUIDLow()),
                TortoiseBots::RunReleaseHold::HeldByRun(bot), DungeonCrewHasLivingResurrecter(), DungeonCrewInCombat(),
                bot->GetMap() && bot->GetMap()->Instanceable());
            if (verdict == TortoiseBots::RunReleaseHold::AutoRelease::Wait)
                return false;
            if (verdict == TortoiseBots::RunReleaseHold::AutoRelease::Release)
                return true;

            if (!bot->GetGroup())
                return !ShouldHoldFreeDeathRelease();

            if (!ai->GetGroupMaster())
                return !ShouldHoldFreeDeathRelease();

            if (ai->GetGroupMaster() == bot)
                return !ShouldHoldFreeDeathRelease();

            if (!ai->HasActivePlayerMaster())
                return !ShouldHoldFreeDeathRelease();

            if (ai->HasActivePlayerMaster() && ai->GetGroupMaster()->GetMapId() == bot->GetMapId() && (bot->GetMap()->IsRaid() || bot->GetMap()->IsDungeon()))
                return false;

            if(sServerFacade.UnitIsDead(ai->GetGroupMaster()))
                return true;

            if (sServerFacade.IsDistanceGreaterThan(AI_VALUE2(float, "distance", "master target"), sPlayerbotAIConfig.sightDistance))
                return true;

            return false;
        }

    private:
        bool ShouldHoldFreeDeathRelease() const
        {
            return !ai->HasActivePlayerMaster() &&
                TortoiseBots::ShouldHoldDeathRelease(deathPause_, WorldTimer::getMSTime(),
                    sPlayerbotAIConfig.deathReleasePauseMinSec, sPlayerbotAIConfig.deathReleasePauseMaxSec,
                    urand(0, 9999));
        }

        mutable TortoiseBots::DeathReleasePauseState deathPause_;
    };

    // "corpse run" chat command: set the "corpse run" flag so FindCorpseAction ignores the
    // wait-for-master gate and the bot runs to its own corpse (e.g. when the master has no
    // way to resurrect it). The flag is cleared automatically when the bot resurrects.
    class CorpseRunAction : public ChatCommandAction
    {
    public:
        CorpseRunAction(PlayerbotAI* ai, std::string name = "corpse run") : ChatCommandAction(ai, name) {}

        virtual bool Execute(Event& event) override
        {
            Player* requester = event.GetOwner() ? event.GetOwner() : GetMaster();

            if (sServerFacade.IsAlive(bot) || !bot->GetCorpse())
            {
                ai->TellPlayerNoFacing(requester, "I am not dead", PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
                return false;
            }

            SET_AI_VALUE(bool, "corpse run", true);
            sLog.outString("[BOT CORPSE] %s: corpse run command received - overriding wait-for-master, running to corpse", CStrCompat(bot->GetName()));
            ai->TellPlayerNoFacing(requester, "Running to my corpse", PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
            return true;
        }
    };

    class RepopAction : public SpiritHealerAction
    {
    public:
        RepopAction(PlayerbotAI* ai, std::string name = "repop") : SpiritHealerAction(ai, name) {}

    public:
        virtual bool Execute(Event& event) override
        {
            Player* requester = event.GetOwner() ? event.GetOwner() : GetMaster();

            sLog.outDetail("Repop bot #%d %s:%d <%s>", bot->GetGUIDLow(), bot->GetTeam() == ALLIANCE ? "A" : "H", bot->GetLevel(), CStrCompat(bot->GetName()));

            SET_AI_VALUE(uint32, "death count", 0);

            if (bot->IsDead())
            {
                bot->ResurrectPlayer(1.0f, false);
                bot->SpawnCorpseBones();
                bot->SaveToDB();
            }


            if (ai::RepopLeavesGroup(ai->HasRealPlayerMaster(),
                    TortoiseBots::BotActivityLeaseManager::Instance().GetActivity(bot->GetGUIDLow()) ==
                        TortoiseBots::BotActivity::Dungeon))
            {
                if (Group* group = bot->GetGroup())
                {
                    sLog.outDetail("Repop: Removing bot #%d %s:%d <%s> from group", bot->GetGUIDLow(), bot->GetTeam() == ALLIANCE ? "A" : "H", bot->GetLevel(), CStrCompat(bot->GetName()));
                    GroupRemoveMemberCompat(group, bot->getObjectGuid());
                }
            }

            SET_AI_VALUE(Unit*, "old target", nullptr);
            SET_AI_VALUE(Unit*, "current target", nullptr);
            SET_AI_VALUE(Unit*, "pull target", nullptr);
            RESET_AI_VALUE(bool, "combat::self target");
            RESET_AI_VALUE(WorldPosition, "current position");

            bot->SetSelectionGuid(ObjectGuid());
            ai->TellPlayer(requester, BOT_TEXT("hello"), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);

            TravelTarget* travelTarget = AI_VALUE(TravelTarget*, "travel target");
            sTravelMgr.SetNullTravelTarget(travelTarget);
            travelTarget->SetStatus(TravelStatus::TRAVEL_STATUS_EXPIRED);
            travelTarget->SetExpireIn(1000);

            // Goblin and High Elf bots are spawned in Durotar/Elwynn instead of their real
            // (custom, player-only, bot-excluded) starting zone, with their homebind set to match.
            // GetPlayerInfo() below returns the real racial DBC spawn point instead, which would send
            // the bot right back to the excluded zone on death, so route these two races through homebind.
            // A random bot without a player master comes back at the nearest graveyard, like a
            // player at the spirit healer. The racial spawn point below sent every repopped bot
            // back to its starting zone (85 repops in 43 minutes on a live realm).
            if (sPlayerbotAIConfig.repopAtGraveyard && !ai->HasRealPlayerMaster())
            {
                // The core lookup is zone-linked, not proximity-bounded: with no
                // same-map same-faction graveyard for the bot's zone (bot in
                // enemy territory) it returns an arbitrary linked graveyard on
                // another map (entryFar) - a horde bot stuck in Teldrassil woke
                // up in Booty Bay. Only follow the pick when it shares the
                // bot's map and is reasonably near (GraveyardTeleportPolicy.h);
                // otherwise fall through to the spawn/homebind path below.
                WorldSafeLocsEntry const* grave = ClosestGraveyardCompat(bot);
                if (grave)
                {
                    WorldPosition gravePos(grave);
                    uint32 const graveMapId = gravePos.getMapId();
                    float const graveDist = WorldPosition(bot).fDist(gravePos);
                    // A bot already standing on its graveyard (revived there,
                    // then nopath-trapped) would teleport onto itself and loop;
                    // send it to spawn/homebind below instead.
                    if (graveDist > 30.0f && IsUsableGraveyardTarget(true, graveMapId == bot->GetMapId(), graveDist))
                    {
                        sLog.outDetail("Repop: Teleporting bot #%d %s:%d <%s> to the nearest graveyard", bot->GetGUIDLow(), bot->GetTeam() == ALLIANCE ? "A" : "H", bot->GetLevel(), CStrCompat(bot->GetName()));
                        bot->TeleportTo(graveMapId, gravePos.getX(), gravePos.getY(), gravePos.getZ(), bot->GetOrientation());
                        sPlayerbotAIConfig.logEvent(ai, "RepopAction", "graveyard");
                        // A sub-10 pool bot landed above its level (valley GY in
                        // the next town) goes straight home (LowbieGraveyardPolicy.h).
                        SendStrandedLowbieHomeCompat(bot);
                        return true;
                    }
                    sLog.outDetail("Repop: nearest graveyard for bot #%d %s:%d <%s> is off-map/far (map %u), using spawn/homebind instead",
                        bot->GetGUIDLow(), bot->GetTeam() == ALLIANCE ? "A" : "H", bot->GetLevel(), CStrCompat(bot->GetName()), graveMapId);
                }
            }

            bool useHomebindOverride = RepopUsesHomebindCompat(bot);
            PlayerInfo const* defaultPlayerInfo = useHomebindOverride ? nullptr : sObjectMgr.GetPlayerInfo(bot->GetRace(), bot->GetClass());
            if (defaultPlayerInfo)
            {
                sLog.outDetail("Repop: Teleporting bot #%d %s:%d <%s> to spawn", bot->GetGUIDLow(), bot->GetTeam() == ALLIANCE ? "A" : "H", bot->GetLevel(), CStrCompat(bot->GetName()));
                //teleport bot to spawn
                WorldPosition const spawnPos(defaultPlayerInfo);
                bot->TeleportTo(spawnPos.getMapId(), spawnPos.getX(), spawnPos.getY(), spawnPos.getZ(), spawnPos.getO());
                if (isRealPlayer_Helper(bot))
                    SendHeartBeatCompat(bot);
            }
            else
            {
                sLog.outDetail("Repop: Teleporting bot #%d %s:%d <%s> to homebind", bot->GetGUIDLow(), bot->GetTeam() == ALLIANCE ? "A" : "H", bot->GetLevel(), CStrCompat(bot->GetName()));
                //teleport bot to homebind
                TeleportToHomebindCompat(bot);
                SendHeartBeatCompat(bot);
            }

            sPlayerbotAIConfig.logEvent(ai, "RepopAction");

            return true;
        }

        virtual bool isUseful() override
        {
            if (bot->InBattleGround())
                return false;

            if (ai->HasActivePlayerMaster())
                return false;

            return true;
        }
    };

    class SelfResurrectAction : public ChatCommandAction
    {
    public:
        SelfResurrectAction(PlayerbotAI* ai, std::string name = "self resurrect") : ChatCommandAction(ai, name) {}

    public:
        bool Execute(Event& event) override
        {
            return SelfResurrectCompat(bot);
        }

        bool isPossible() override
        {
            return ai->IsStateActive(BotState::BOT_STATE_DEAD) && SelfResurrectPossibleCompat(bot);
        }
    };
}
