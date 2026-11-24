// ForeverCore side of the both-core helpers of the battleground PvP kit (round BG2: EnemyPlayerValue.cpp, PvpValues.cpp,
// PvpTriggers.cpp, AttackEnemyPlayersStrategy.cpp, BattlegroundStrategy.cpp). Turtle side: ai/cmangos-compat-bg.h (same names and
// signatures; there each helper is the expression the bot always made). Both are found through ai/playerbot/TbCompatBg.h.
//
// The rule this pair carries (spec D2): the module reads battleground state the way a client could, from the core's own public
// members and from what is on the units: the flag a player carries is the carrier aura on him (BgIds.h), never the map script's
// private state. The cmangos Warsong Gulch class and its picker getters do not exist here.
#pragma once

#include "Battleground.h"
#include "ObjectAccessor.h"
#include "Player.h"

#include "playerbot/policy/bg/BgIds.h"

// The battleground class and the two Player members under their cmangos spellings (casing only).
using BattleGround = Battleground;
#define GetBattleGround GetBattleground
#define GetBattleGroundTypeId GetBattlegroundTypeId

// Set 1 where the legacy battlemaster path (walk to the NPC, queue through him) is compiled: the Turtle build. ForeverCore queues
// through WorldSession::JoinBattlegroundQueue (BG1, forever/src/host/bg/TbBgQueue.cpp), so its pool bots never look for a battlemaster.
#define TB_BG_BATTLEMASTER_PATH 0

// Whether the battleground target order of policy/bg/BgTargetPolicy.h picks the enemy player inside a battleground (EnemyPlayerValue.cpp).
// ForeverCore: yes, that is the round. The Turtle build keeps the pick it always made.
inline constexpr bool kBgTargetPolicyCompat = true;

// The sight range for an enemy player is not here: ai/playerbot/TbCompatPvP.h already answers it on both cores
// (MaxEnemyPlayerAttackDistanceCompat: 60 yd in the world, 40 in a battleground). One helper, one rule.

// The player carrying a team's flag in a Warsong Gulch match, or null. The carrier aura sits on the player (the Alliance carries the
// Horde's Warsong flag); the lookup walks the match's own player list, the same members the core's scripts read. `sameTeam` asks for
// the bot's own side's carrier, otherwise for the enemy's. Without `ignoreRange` a carrier beyond the bot's sight is not returned.
inline Unit* BgFlagCarrierCompat(Player* bot, bool sameTeam, float sightDistance, bool ignoreRange)
{
    Battleground* battleground = bot ? bot->GetBattleground() : nullptr;
    if (!battleground || bot->GetBattlegroundTypeId() != BATTLEGROUND_WS)
        return nullptr;

    Team const side = sameTeam ? bot->GetBGTeam() : (bot->GetBGTeam() == ALLIANCE ? HORDE : ALLIANCE);
    // Alliance players carry the Horde's flag (Warsong), Horde players the Alliance's (Silverwing): the mapping is pure (BgIds.h).
    uint32 const aura = TortoiseBots::bg::WsgCarrierAuraOf(side == ALLIANCE ? TortoiseBots::bg::BgSide::Alliance : TortoiseBots::bg::BgSide::Horde);
    for (auto const& entry : battleground->GetPlayers())
    {
        if (entry.second.Team != side)
            continue;
        Player* carrier = ObjectAccessor::GetPlayer(*bot, entry.first);
        if (!carrier || !carrier->HasAura(aura))
            continue;
        if (ignoreRange || bot->IsWithinDistInMap(carrier, sightDistance))
            return carrier;
        return nullptr;
    }
    return nullptr;
}

// Whether the bot itself carries a flag in a Warsong Gulch match.
inline bool BgBotCarriesFlagCompat(Player* bot)
{
    return bot && bot->GetBattleground() && bot->GetBattlegroundTypeId() == BATTLEGROUND_WS &&
        (bot->HasAura(TortoiseBots::bg::kWsgCarrierAuraWarsong) || bot->HasAura(TortoiseBots::bg::kWsgCarrierAuraSilverwing));
}

// Whether the bot may be told to queue itself. ForeverCore never asks the bot: the fill of a human's queue is the host's (BG1).
inline bool BgBotMayQueueCompat(Player* /*bot*/)
{
    return false;
}

// The distance from the bot to a point, with no size taken off (F GetExactDist(x, y, z): the centre distance).
inline float BgDistanceToCompat(Player* bot, float x, float y, float z)
{
    return bot->GetExactDist(x, y, z);
}
