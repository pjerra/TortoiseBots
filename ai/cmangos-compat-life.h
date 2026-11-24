// Turtle side of the both-core helpers of the free-bot rpg layer (TBF #147, package A). Same names and signatures as
// forever/src/compat/TbCompat_Life.h; each helper is the expression the bot always made, moved here unchanged so the
// shared rpg files compile on both cores.
#pragma once

#include <ctime>

#include "playerbot/GuidPosition.h"
#include "playerbot/strategy/actions/EmoteAction.h"          // TalkAction::GetRandomEmote
#include "playerbot/strategy/values/TrainerValues.h"          // TrainerSpellLearnableNow

inline time_t RpgGameTimeCompat()
{
    return sWorld.GetGameTime();
}

inline float RpgRestBonusCompat(Player* bot)
{
    return bot->GetRestBonus();
}

// =====================================================================================================================
// GOSSIP AND EMOTES
// =====================================================================================================================
inline void RpgGossipHelloCompat(Player* bot, ObjectGuid const& guid)
{
    WorldPacket p1;
    p1 << guid;
    bot->GetSession()->HandleGossipHelloOpcode(p1);
}

inline uint32 RpgRandomEmoteCompat(Unit* unit)
{
    return ai::TalkAction::GetRandomEmote(unit);
}

inline void RpgHandleEmoteCompat(Player* bot, uint32 emote)
{
    bot->HandleEmoteCommand(emote);
}

inline void RpgClearSplineCompat(Unit* unit)
{
    unit->m_movementInfo.RemoveMovementFlag(MovementFlags(MOVEFLAG_SPLINE_ENABLED | MOVEFLAG_FORWARD));
}

// =====================================================================================================================
// INTERACTION, DUEL, IGNORE
// =====================================================================================================================
inline Creature* RpgNpcIfCanInteractCompat(Player* bot, ObjectGuid const& guid, uint32 npcFlag)
{
    return bot->GetNPCIfCanInteractWith(guid, npcFlag);
}

inline bool RpgInDuelCompat(Player* player)
{
    return player && player->m_duel != nullptr;
}

inline bool RpgSocialIgnoreCompat(Player* viewer, Player* ignored)
{
    if (!viewer || !ignored || !viewer->GetSocial())
        return false;
    return viewer->GetSocial()->HasIgnore(ignored->getObjectGuid());
}

inline bool RpgAreaAllowsDuelCompat(AreaTableEntry const* area)
{
    return area && (area->Flags & AREA_FLAG_DUEL);
}

// =====================================================================================================================
// TRAINERS
// =====================================================================================================================
inline bool RpgTrainerIsOfCompat(ai::GuidPosition const& guidP, Player* pPlayer)
{
    CreatureInfo const* cInfo = guidP.GetCreatureTemplate();

    if (!cInfo)
        return false;

    switch (cInfo->trainer_type)
    {
    case TRAINER_TYPE_CLASS:
        if (pPlayer->GetClass() != cInfo->trainer_class)
        {
            return false;
        }
        break;
    case TRAINER_TYPE_PETS:
        if (pPlayer->GetClass() != CLASS_HUNTER)
        {
            return false;
        }
        break;
    case TRAINER_TYPE_MOUNTS:
        if (cInfo->trainer_race && pPlayer->GetRace() != cInfo->trainer_race)
        {
            // Allowed to train if exalted
            if (FactionTemplateEntry const* faction_template = sFactionTemplateStore.LookupEntry(cInfo->faction))
            {
                if (pPlayer->GetReputationRank(faction_template->faction) == REP_EXALTED)
                    return true;
            }
            return false;
        }
        break;
    case TRAINER_TYPE_TRADESKILLS:
        if (cInfo->trainer_spell && !pPlayer->HasSpell(cInfo->trainer_spell))
        {
            return false;
        }
        break;
    default:
        return false;                                   // checked and error output at creature_template loading
    }
    return true;
}

inline TrainerType RpgTrainerTypeCompat(ai::GuidPosition const& guidP, Player* /*bot*/)
{
    CreatureInfo const* cInfo = guidP.GetCreatureTemplate();
    return cInfo ? TrainerType(cInfo->trainer_type) : TRAINER_TYPE_CLASS;
}

inline bool RpgTrainerRowEligibleCompat(Player* bot, Creature* /*trainer*/, TrainerSpell const& tSpell)
{
    return ai::TrainerSpellLearnableNow(bot, &tSpell);
}

// =====================================================================================================================
// GOSSIP MENU PROBE
// =====================================================================================================================
inline bool RpgCreatureHasGossipMenuCompat(Creature* /*creature*/, CreatureInfo const* cInfo)
{
    if (!cInfo)
        return false;
    GossipMenuItemsMapBounds pMenuItemBounds = sObjectMgr.GetGossipMenuItemsMapBounds(cInfo->gossip_menu_id);
    return pMenuItemBounds.first != pMenuItemBounds.second;
}

inline void RpgPrepareGossipMenuCompat(Player* bot, Creature* creature)
{
    if (!creature)
        return;
    if (!sScriptMgr.OnGossipHello(bot, creature))
        bot->PrepareGossipMenu(creature, creature->GetDefaultGossipMenuId());
}

inline uint32 RpgGossipMenuItemCountCompat(GossipMenu const& menu)
{
    return menu.MenuItemCount();
}

// =====================================================================================================================
// THE MINECART
// =====================================================================================================================
inline bool RpgMinecartBlockedCompat(Player* bot, ai::GuidPosition const& guidP)
{
    Creature* creature = nullptr;
    MaNGOS::NearestCreatureEntryWithLiveStateInObjectRangeCheck creature_check(*bot, 28841, true, 500.0f);
    MaNGOS::CreatureLastSearcher<MaNGOS::NearestCreatureEntryWithLiveStateInObjectRangeCheck> searcher(creature, creature_check);
    Cell::VisitGridObjects(bot, searcher, 500.0f);

    if (!creature || guidP.distance(creature) > 300.0f)
        return false;

    return true;
}

// =====================================================================================================================
// TRADE WINDOW
// =====================================================================================================================
inline void RpgInitiateTradeCompat(Player* initiator, Player* target)
{
    if (!initiator || !initiator->GetSession() || !target)
        return;
    WorldPacket packet(CMSG_INITIATE_TRADE);
    packet << target->getObjectGuid();
    initiator->GetSession()->HandleInitiateTradeOpcode(packet);
}

inline void RpgAcceptTradeCompat(Player* bot)
{
    WorldPacket p;
    uint32 status = TRADE_STATUS_TRADE_ACCEPT;
    p << status;
    bot->GetSession()->HandleAcceptTradeOpcode(p);
}
