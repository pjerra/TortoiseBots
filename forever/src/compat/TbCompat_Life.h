// ForeverCore side of the both-core helpers of the free-bot rpg layer (TBF #147, package A: RpgAction, RpgSubActions,
// ChooseRpgTargetAction, MoveToRpgTargetAction, RpgStrategy, PossibleRpgTargetsValue, RpgTriggers). Turtle side:
// ai/cmangos-compat-life.h (same names and signatures; each helper there is the expression the bot always made). Both are
// found through ai/playerbot/TbCompatLife.h (`__has_include`), so the shared shim headers stay untouched.
//
// The wrapper is included by the call sites after playerbot.h, so GuidPosition and the bot's own types are complete here.
// The trainer helpers reuse the host town seam (host/TbHostTown.h) through TbCompat_W2Town.h; only the Turtle side reads
// the 1.12 trainer tables directly.
#pragma once

#include "AI/CreatureAI.h"
#include "DataStores/DB2Structure.h"
#include "Entities/Creature/Creature.h"
#include "Entities/Creature/CreatureData.h"
#include "Entities/Creature/GossipDef.h"
#include "Entities/Player/Player.h"
#include "Entities/Player/TradeData.h"
#include "Globals/ObjectMgr.h"
#include "NPCPackets.h"
#include "RestMgr.h"
#include <ctime>

#include "Time/GameTime.h"
#include "TradePackets.h"
#include "WorldSession.h"

#include "TbCompat_W2Town.h"   // TrainerSpell, CollectTrainerSpellsCompat, CanTeachTrainerSpellForeverCompat
#include "playerbot/GuidPosition.h"

// The server's game time as a wall clock (T `sWorld.GetGameTime()`, F GameTime::GetGameTime, Time/GameTime.h:32). Used
// by the camp trigger's night-hours rule.
inline time_t RpgGameTimeCompat()
{
    return GameTime::GetGameTime();
}

// The character's stored rest bonus (T `Player::GetRestBonus()`, F's RestMgr row; TbCompat_CH.h:31 reads the same row as
// rest experience). The camp trigger saves it before a rest and the (unported) camp action would restore it.
inline float RpgRestBonusCompat(Player* bot)
{
    return bot->GetRestMgr().GetRestBonus(REST_TYPE_XP);
}

// =====================================================================================================================
// GOSSIP AND EMOTES (RpgSubActions.cpp, RpgTriggers.cpp)
// =====================================================================================================================
// Talk to an NPC: T builds the raw CMSG_GOSSIP_HELLO packet and feeds the handler; F's handler takes the typed packet. The
// menu the handler leaves in PlayerTalkClass is what RpgGossipTalkTrigger reads next, so the menus are cleared first, as
// the handler does.
inline void RpgGossipHelloCompat(Player* bot, ObjectGuid const& guid)
{
    WorldSession* session = bot->GetSession();
    if (!session)
        return;
    if (bot->PlayerTalkClass)
        bot->PlayerTalkClass->ClearMenus();
    WorldPackets::NPC::Hello hello{ WorldPacket(CMSG_TALK_TO_GOSSIP) };
    hello.Unit = guid;
    session->HandleGossipHelloOpcode(hello);
}

// The emote the bot plays at a target that is not the level 1 chicken of the minecart quest. T picks it from the random
// text-emote table (TalkAction::GetRandomEmote); F has no such table here, so the greeting a friendly bot always had.
inline uint32 RpgRandomEmoteCompat(Unit* /*unit*/)
{
    return TEXT_EMOTE_HELLO;
}

inline void RpgHandleEmoteCompat(Player* bot, uint32 emote)
{
    bot->HandleEmoteCommand(static_cast<Emote>(emote));
}

// T clears the spline and forward movement flags of the unit being faced/talked to; F has no spline flag (the spline is
// its own movement generator), so the forward bit is the one that can still be up.
inline void RpgClearSplineCompat(Unit* unit)
{
    unit->m_movementInfo.RemoveMovementFlag(MOVEMENTFLAG_FORWARD);
}

// =====================================================================================================================
// INTERACTION, DUEL, IGNORE (RpgSubActions.cpp, RpgTriggers.cpp)
// =====================================================================================================================
// T takes the one NPC flag word, F the second one too (UNIT_NPC_FLAG_2_NONE: no service of the second word is asked for).
inline Creature* RpgNpcIfCanInteractCompat(Player* bot, ObjectGuid const& guid, uint32 npcFlag)
{
    return bot->GetNPCIfCanInteractWith(guid, static_cast<NPCFlags>(npcFlag), UNIT_NPC_FLAG_2_NONE);
}

// T keeps the duel in `m_duel`, F in the `duel` pointer (Player.h:2206).
inline bool RpgInDuelCompat(Player* player)
{
    return player && player->duel != nullptr;
}

// T's PlayerSocial::HasIgnore takes the ignored guid only; F also takes that player's account guid, so an ignored
// account hides all of its characters (the same test the core's own channel code makes).
inline bool RpgSocialIgnoreCompat(Player* viewer, Player* ignored)
{
    if (!viewer || !ignored || !viewer->GetSocial())
        return false;
    WorldSession* ignoredSession = ignored->GetSession();
    return viewer->GetSocial()->HasIgnore(ignored->GetGUID(),
        ignoredSession ? ignoredSession->GetAccountGUID() : ObjectGuid::Empty);
}

// Dueling is allowed where the area table says so: T's AREA_FLAG_DUEL is F's AreaFlags::AllowDueling (0x40 on both).
inline bool RpgAreaAllowsDuelCompat(AreaTableEntry const* area)
{
    return area && area->GetFlags().HasFlag(AreaFlags::AllowDueling);
}

// =====================================================================================================================
// TRAINERS (RpgTriggers.cpp, MaintenanceValues.cpp)
// =====================================================================================================================
// T decides from the creature template's trainer_type/class/race/spell columns; F has the trainer templates in the core's
// Trainer store, read through the host town seam (host/TbHostTown.cpp:241, "is this trainer for this player").
inline bool RpgTrainerIsOfCompat(ai::GuidPosition const& guidP, Player* bot)
{
    return bot && tbhost::town::IsTrainerOf(guidP.GetCreature(bot->GetInstanceId()), bot);
}

// The trainer kind decides which purse pays for a rank (TrainerSpendableMoney). F's TrainerKind has no mount trainer:
// class, talent and anything unknown share the class budget, tradeskill and pet keep their own.
inline TrainerType RpgTrainerTypeCompat(ai::GuidPosition const& guidP, Player* bot)
{
    switch (bot ? tbhost::town::GetTrainerKind(guidP.GetCreature(bot->GetInstanceId())) : tbhost::town::TrainerKind::Class)
    {
        case tbhost::town::TrainerKind::Tradeskill:
            return TRAINER_TYPE_TRADESKILLS;
        case tbhost::town::TrainerKind::Pet:
            return TRAINER_TYPE_PETS;
        default:
            return TRAINER_TYPE_CLASS;
    }
}

// Whether the visit would teach this row now: the same core check TrainerAction's visit makes before it pays.
inline bool RpgTrainerRowEligibleCompat(Player* bot, Creature* trainer, TrainerSpell const& tSpell)
{
    return CanTeachTrainerSpellForeverCompat(bot, trainer, tSpell);
}

// =====================================================================================================================
// GOSSIP MENU PROBE (RpgTriggers.cpp)
// =====================================================================================================================
// Does the creature have gossip options at all? T reads the template's gossip_menu_id and its item bounds; F's template
// keeps a list of menu ids (CreatureData.h:499) and the client-visible one is resolved per player
// (Player::GetGossipMenuForSource, Player.cpp:15612), so a script-set id and every listed id are asked.
inline bool RpgGossipMenuIdHasItems(uint32 menuId)
{
    auto bounds = sObjectMgr.GetGossipMenuItemsMapBounds(menuId);
    return bounds.begin() != bounds.end();
}

inline bool RpgCreatureHasGossipMenuCompat(Creature* creature, CreatureInfo const* cInfo)
{
    if (!creature || !cInfo)
        return false;
    if (uint32 const menuId = creature->GetGossipMenuId())
        return RpgGossipMenuIdHasItems(menuId);
    for (uint32 const menuId : cInfo->GossipMenuIds)
        if (RpgGossipMenuIdHasItems(menuId))
            return true;
    return false;
}

// T asks the script manager whether a script handles the gossip hello and otherwise prepares the menu itself; on F the
// creature's own AI is the script hook (the same call WorldSession::HandleGossipHelloOpcode makes, NPCHandler.cpp:193).
inline void RpgPrepareGossipMenuCompat(Player* bot, Creature* creature)
{
    if (!creature)
        return;
    CreatureAI* ai = creature->AI();
    if (!ai || !ai->OnGossipHello(bot))
        bot->PrepareGossipMenu(creature, bot->GetGossipMenuForSource(creature));
}

inline uint32 RpgGossipMenuItemCountCompat(GossipMenu const& menu)
{
    return menu.GetMenuItemCount();
}

// =====================================================================================================================
// THE MINECART (RpgSubActions.cpp, RpgUseAction::isUseful)
// =====================================================================================================================
// Entry 190767 is the Turtle mine cart of the "Inconspicuous Mine Car" quest; miner 28841 may be carrying another bot, in
// which case boarding would send the cart to that bot. T scans the grid; F asks the map for the nearest live one and keeps
// the same 300 yd limit.
inline bool RpgMinecartBlockedCompat(Player* bot, ai::GuidPosition const& guidP)
{
    Creature* miner = bot->FindNearestCreature(28841, 500.0f);
    return miner && guidP.distance(miner) <= 300.0f;
}

// =====================================================================================================================
// TRADE WINDOW (RpgSubActions.cpp)
// =====================================================================================================================
// T hands the session a raw CMSG_INITIATE_TRADE packet with the target's guid; F's handler reads the guid from the typed
// packet. The initiator is the player whose session handles it (T ignores the packet body the same way).
inline void RpgInitiateTradeCompat(Player* initiator, Player* target)
{
    if (!initiator || !initiator->GetSession() || !target)
        return;
    WorldPackets::Trade::InitiateTrade packet{ WorldPacket(CMSG_INITIATE_TRADE) };
    packet.Guid = target->GetGUID();
    initiator->GetSession()->HandleInitiateTradeOpcode(packet);
}

// Accepting a trade needs the trader's state index on F: the handler refuses a stale answer with TRADE_STATUS_STATE_CHANGED
// (TradeHandler.cpp:267), and this call is the answer to the window the bot was just offered.
inline void RpgAcceptTradeCompat(Player* bot)
{
    if (!bot || !bot->GetSession())
        return;
    TradeData* myTrade = bot->GetTradeData();
    Player* trader = myTrade ? myTrade->GetTrader() : nullptr;
    TradeData* hisTrade = trader ? trader->GetTradeData() : nullptr;
    if (!myTrade || !hisTrade)
        return;
    WorldPackets::Trade::AcceptTrade packet{ WorldPacket(CMSG_ACCEPT_TRADE) };
    packet.StateIndex = hisTrade->GetServerStateIndex();
    bot->GetSession()->HandleAcceptTradeOpcode(packet);
}
