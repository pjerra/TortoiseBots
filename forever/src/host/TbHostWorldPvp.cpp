// Open-world PvP host service: see TbHostWorldPvp.h. The engine side (strategies, target values) is the legacy module's;
// this file only decides who carries the /pvp flag and who holds the `world pvp` seek role.
#include "host/TbHostWorldPvp.h"

#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1

#include "compat/botpch_forever.h"

#include <algorithm>
#include <set>

#include "Group.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "WorldSession.h"

#include "TbBotMgr.h"
#include "runtime/policy/pvp/WorldPvpSeekPolicy.h"
#include "playerbot/PlayerbotAI.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "PlayerbotAIStorage.h"
#include "playerbot/strategy/values/WorldPvpSeekLog.h"

namespace tbhost
{
namespace worldpvp
{
namespace
{
using TortoiseBots::WorldPvpSeekInputs;

PlayerbotAI* AiOf(Player* bot)
{
    return bot ? PlayerbotAIStorage::Instance().GetAI(bot) : nullptr;
}

// Bots whose last roll granted the role. A kit rebuild (a role change, a quest party, `.tb free`) drops a strategy the
// kit does not name, so the pass re-adds the granted role the way TbTactics re-enforces its toggles; the set is what
// keeps a player's own `nc -world pvp` off until the next re-roll.
std::set<ObjectGuid>& Granted()
{
    static std::set<ObjectGuid> granted;
    return granted;
}

bool HoldsSeek(PlayerbotAI* ai)
{
    return ai->HasStrategy("world pvp", BotState::BOT_STATE_NON_COMBAT) ||
        ai->HasStrategy("world pvp", BotState::BOT_STATE_COMBAT);
}

// A party with a real player in it is not the bot's own play, so the role goes (TB AiFactory.cpp:1195-1254).
bool GroupedWithRealPlayer(Player* bot)
{
    Group* group = bot->GetGroup();
    if (!group)
        return false;
    for (Group::MemberSlot const& slot : group->GetMemberSlots())
    {
        Player* member = ObjectAccessor::FindPlayer(slot.guid);
        if (member && member != bot && member->IsInWorld() && HasNetworkTransportCompat(member->GetSession()))
            return true;
    }
    return false;
}

// The host's facts for the pure policy. A bot with a master is never free (BotMgr::BindMaster clears `free` first), and
// every owned or hired bot has one, so `freePoolBot` alone implies "not owned, not hired".
WorldPvpSeekInputs SeekInputs(Player* bot)
{
    WorldPvpSeekInputs in;
    in.seekEnabled = sPlayerbotAIConfig.worldPvpSeek != 0;
    in.freePoolBot = TortoiseBots::BotMgr::Instance().IsFreeBot(bot->GetGUID());
    in.inBattleground = bot->InBattleground();
    in.groupedWithRealPlayer = GroupedWithRealPlayer(bot);
    in.pvpFlagged = !sPlayerbotAIConfig.randomBotForcePvp ||
        TortoiseBots::PoolBotInPvpFlaggedShare(bot->GetGUIDLow(), sPlayerbotAIConfig.randomBotForcePvpPercent);
    return in;
}

void Strip(PlayerbotAI* ai)
{
    ai->ChangeStrategy("-world pvp", BotState::BOT_STATE_NON_COMBAT);
    ai->ChangeStrategy("-world pvp", BotState::BOT_STATE_COMBAT);
}

// TB's RollWorldPvpSeek (AiFactory.cpp:1219-1254): strip when ineligible, else grant to a share of the eligible bots.
void Roll(Player* bot, PlayerbotAI* ai)
{
    WorldPvpSeekInputs const in = SeekInputs(bot);
    if (!TortoiseBots::WorldPvpSeekEligible(in))
    {
        Granted().erase(bot->GetGUID());
        if (HoldsSeek(ai))
        {
            Strip(ai);
            sLog.outString("[W2] world-pvp role=strip bot=%s reason=ineligible", bot->GetName().c_str());
        }
        ai->worldPvpSeekRollMs_ = WorldTimer::getMSTime();
        return;
    }

    uint32 percent = std::min<uint32>(sPlayerbotAIConfig.worldPvpSeekBotPercent, 100);
    bool const grant = percent > 0 && urand(0, 99) < percent;
    bool const held = HoldsSeek(ai);
    if (grant)
    {
        Granted().insert(bot->GetGUID());
        if (!held)
        {
            ai->ChangeStrategy("+world pvp", BotState::BOT_STATE_NON_COMBAT);
            ai->ChangeStrategy("+world pvp", BotState::BOT_STATE_COMBAT);
            ai::WorldPvpSeekLog::LogGrant(ai, bot);
            sLog.outString("[W2] world-pvp role=grant bot=%s share=%u", bot->GetName().c_str(), percent);
        }
    }
    else
    {
        Granted().erase(bot->GetGUID());
        if (held)
        {
            Strip(ai);
            sLog.outString("[W2] world-pvp role=strip bot=%s share=%u", bot->GetName().c_str(), percent);
        }
    }
    ai->worldPvpSeekRollMs_ = WorldTimer::getMSTime();
}
}

bool HoldsRole(Player* bot)
{
    PlayerbotAI* ai = AiOf(bot);
    return ai && HoldsSeek(ai);
}

void OnBotLogin(Player* bot)
{
    if (!bot || !sPlayerbotAIConfig.randomBotForcePvp)
        return;
    if (!TortoiseBots::BotMgr::Instance().IsFreeBot(bot->GetGUID()))
        return;

    uint32 const percent = std::min<uint32>(sPlayerbotAIConfig.randomBotForcePvpPercent, 100);
    if (!TortoiseBots::PoolBotInPvpFlaggedShare(bot->GetGUIDLow(), percent))
        return;
    if (bot->HasPlayerFlag(PLAYER_FLAGS_IN_PVP))
        return;

    // What HandleSetPvP does for a client's /pvp (MiscHandler.cpp:336-350), with the override the handler uses so a
    // friendly area cannot start the decline timer. The flag is saved, so Player::InitPvP re-flags the bot after a relog.
    bot->SetPlayerFlag(PLAYER_FLAGS_IN_PVP);
    bot->RemovePlayerFlag(PLAYER_FLAGS_PVP_TIMER);
    if (!bot->IsPvP() || bot->pvpInfo.EndTimer)
        bot->UpdatePvP(true, true);
    sLog.outString("[W2] world-pvp flag bot=%s share=%u", bot->GetName().c_str(), percent);
}

void ApplyGrantedRole(Player* bot)
{
    if (!bot)
        return;
    auto it = Granted().find(bot->GetGUID());
    if (it == Granted().end())
        return;
    PlayerbotAI* ai = AiOf(bot);
    if (!ai)
        return;
    if (!TortoiseBots::WorldPvpSeekEligible(SeekInputs(bot)))
    {
        Granted().erase(it);
        return;
    }
    if (HoldsSeek(ai))
        return;
    ai->ChangeStrategy("+world pvp", BotState::BOT_STATE_NON_COMBAT);
    ai->ChangeStrategy("+world pvp", BotState::BOT_STATE_COMBAT);
    sLog.outString("[W2] world-pvp role=regrant bot=%s", bot->GetName().c_str());
}

void Tick(Player* bot)
{
    if (!bot || !bot->IsInWorld())
        return;
    PlayerbotAI* ai = AiOf(bot);
    if (!ai)
        return;

    // Only a masterless bot is the host's to manage; a master binding is the hire and owned-bot rule, and the kit rebuild
    // that binding does drops the role anyway. A player's own `nc +world pvp` on a commanded bot stays theirs. Forgetting
    // the roll here means `.tb free on` rolls at once instead of waiting out an interval the commanded bot had started.
    if (!TortoiseBots::BotMgr::Instance().IsFreeBot(bot->GetGUID()))
    {
        Granted().erase(bot->GetGUID());
        ai->worldPvpSeekRollMs_ = 0;
        ai->worldPvpSeekNextMs_ = 0;
        return;
    }

    // An ineligible holder loses the role on the next pass, not at the next re-roll (TB RandomBotService.cpp:1998-2006).
    if (HoldsSeek(ai) && !TortoiseBots::WorldPvpSeekEligible(SeekInputs(bot)))
    {
        Granted().erase(bot->GetGUID());
        Strip(ai);
        sLog.outString("[W2] world-pvp role=strip bot=%s reason=ineligible", bot->GetName().c_str());
        ai->worldPvpSeekRollMs_ = WorldTimer::getMSTime();
        return;
    }
    // A new brain (login, kit rebuild) starts at 0 and rolls once, as TB's factory does when it builds the kit.
    if (!ai->worldPvpSeekRollMs_)
    {
        Roll(bot, ai);
        return;
    }

    uint32 const rerollMinutes = sPlayerbotAIConfig.worldPvpSeekRerollMinutes;
    if (!rerollMinutes)
        return;
    uint32 const nowMs = WorldTimer::getMSTime();
    if (!ai->worldPvpSeekNextMs_)
        ai->worldPvpSeekNextMs_ = ai->worldPvpSeekRollMs_ + rerollMinutes * 60 * 1000 + urand(0, 5 * 60 * 1000);
    if (nowMs >= ai->worldPvpSeekNextMs_ &&
        TortoiseBots::WorldPvpSeekRerollDue(ai->worldPvpSeekRollMs_, nowMs, rerollMinutes, bot->IsInCombat()))
    {
        Roll(bot, ai);
        ai->worldPvpSeekNextMs_ = nowMs + rerollMinutes * 60 * 1000 + urand(0, 5 * 60 * 1000);
    }
}

}
}

#else

namespace tbhost
{
namespace worldpvp
{
bool HoldsRole(Player* /*bot*/) { return false; }
void OnBotLogin(Player* /*bot*/) {}
void Tick(Player* /*bot*/) {}
void ApplyGrantedRole(Player* /*bot*/) {}
}
}

#endif
