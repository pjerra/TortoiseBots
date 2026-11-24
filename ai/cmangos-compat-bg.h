// Turtle side of the both-core helpers of the battleground PvP kit (round BG2: EnemyPlayerValue.cpp, PvpValues.cpp, PvpTriggers.cpp,
// AttackEnemyPlayersStrategy.cpp, BattlegroundStrategy.cpp). Same names and signatures as forever/src/compat/TbCompat_BG.h; each
// helper is the expression the bot always made (the call is moved, not changed), so the Turtle behaviour is untouched. Found through
// ai/playerbot/TbCompatBg.h (`__has_include`).
#pragma once

#include "Battlegrounds/BattleGroundWS.h"

// Set 1 where the legacy battlemaster path (walk to the NPC, queue through him) is compiled: the Turtle build.
#define TB_BG_BATTLEMASTER_PATH 1

// Whether the battleground target order of policy/bg/BgTargetPolicy.h picks the enemy player inside a battleground (EnemyPlayerValue.cpp).
// ForeverCore: yes, that is the round. The Turtle build keeps the pick it always made.
inline constexpr bool kBgTargetPolicyCompat = false;

// The sight range for an enemy player is not here: ai/cmangos-compat-pvp.h already answers it (MaxEnemyPlayerAttackDistanceCompat,
// 60 yd in the world, 40 in a battleground, 10 late in Alterac Valley). One helper, one rule.

// The player carrying a team's flag in a Warsong Gulch match, or null (the picker guids of the match).
inline Unit* BgFlagCarrierCompat(Player* bot, bool sameTeam, float sightDistance, bool ignoreRange)
{
    Unit* carrier = nullptr;

    if (bot->InBattleGround())
    {
        if (bot->GetBattleGroundTypeId() == BattleGroundTypeId::BATTLEGROUND_WS)
        {
            BattleGroundWS *bg = (BattleGroundWS*)bot->GetBattleGround();

            if (!bg)
                return nullptr;

            if ((!sameTeam && bot->GetTeam() == HORDE || (sameTeam && bot->GetTeam() == ALLIANCE)) && !bg->GetHordeFlagPickerGuid().IsEmpty())
                carrier = bg->GetBgMap()->GetPlayer(bg->GetHordeFlagPickerGuid());

            if ((!sameTeam && bot->GetTeam() == ALLIANCE || (sameTeam && bot->GetTeam() == HORDE)) && !bg->GetAllianceFlagPickerGuid().IsEmpty())
                carrier = bg->GetBgMap()->GetPlayer(bg->GetAllianceFlagPickerGuid());

            if (carrier)
            {
                if (ignoreRange || bot->IsWithinDistInMap(carrier, sightDistance))
                    return carrier;
                else
                    return nullptr;
            }
        }
    }
    return carrier;
}

// Whether the bot itself carries a flag in a Warsong Gulch match.
inline bool BgBotCarriesFlagCompat(Player* bot)
{
    if (bot->InBattleGround() && bot->GetBattleGroundTypeId() == BattleGroundTypeId::BATTLEGROUND_WS)
    {
        BattleGroundWS *bg = (BattleGroundWS*)bot->GetBattleGround();
        if (!bg)
            return false;
        return bot->getObjectGuid() == bg->GetAllianceFlagPickerGuid() || bot->getObjectGuid() == bg->GetHordeFlagPickerGuid();
    }
    return false;
}

// Whether the bot may be told to queue itself: the battlemaster path of the Turtle build asks the core.
inline bool BgBotMayQueueCompat(Player* bot)
{
    return bot->CanJoinToBattleground();
}

// The distance from the bot to a point, with no size taken off (the centre distance).
inline float BgDistanceToCompat(Player* bot, float x, float y, float z)
{
    return bot->GetDistance(x, y, z, SizeFactor::None);
}
