#include "TbBotMgr.h"
#include "TbBotSession.h"
#include "AccountMgr.h"
#include "CellImpl.h"
#include "CharacterPackets.h"
#include "DatabaseEnv.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "ObjectMgr.h"
#include "CharacterCache.h"
#include "Config.h"
#include "Log.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "World.h"
#include <memory>
#include <vector>

namespace TortoiseBots
{
namespace
{
constexpr uint32 ThinkIntervalMs = 500;
// The core waits 30 s for a bot to leave when its owner logs in to the same character. A normal character load is
// well under this, so a real session that is still loading after this long is waiting for us.
constexpr uint32 OwnerLoadingLimitMs = 2000;
}

BotMgr& BotMgr::Instance()
{
    static BotMgr instance;
    return instance;
}

void BotMgr::LoadConfig()
{
    _enabled = sConfigMgr->GetBoolDefault("TortoiseBots.Enable", false);
    _followDistance = sConfigMgr->GetFloatDefault("TortoiseBots.Spike.FollowDistance", 3.0f);
}

std::string BotMgr::Login(ObjectGuid characterGuid, ObjectGuid master)
{
    if (!_enabled)
        return "TortoiseBots.Enable is 0";
    if (characterGuid.IsEmpty())
        return "no such character";
    if (_bots.count(characterGuid))
        return "that bot is already logged in or loading";
    if (ObjectAccessor::FindConnectedPlayer(characterGuid))
        return "that character is already in the world";

    uint32 const accountId = sCharacterCache->GetCharacterAccountIdByGuid(characterGuid);
    if (!accountId)
        return "that character has no account";

    std::string accountName;
    if (!AccountMgr::GetName(accountId, accountName))
        accountName = "account" + std::to_string(accountId);

    Bot bot;
    bot.session = std::make_shared<BotSession>(accountId, std::move(accountName));
    bot.master = master;
    if (!bot.session->BeginLogin(characterGuid))
        return "the login could not be started (see the server log)";

    _bots.emplace(characterGuid, std::move(bot));
    TC_LOG_INFO("tortoisebots", "BotMgr: login started for {} (account {})", characterGuid.ToString(), accountId);
    return {};
}

bool BotMgr::Logout(ObjectGuid characterGuid, char const* reason)
{
    auto itr = _bots.find(characterGuid);
    if (itr == _bots.end())
        return false;
    itr->second.session->RequestLogout(reason);
    return true;
}

void BotMgr::LogoutAll(char const* reason)
{
    for (auto& [guid, bot] : _bots)
        bot.session->RequestLogout(reason);
}

std::string BotMgr::CreateCharacter(uint32 accountId, std::string const& name, uint8 race, uint8 playerClass, uint8 gender)
{
    if (!_enabled)
        return "TortoiseBots.Enable is 0";
    if (!accountId)
        return "no such account";
    if (!sObjectMgr->GetPlayerInfo(race, playerClass))
        return "that race and class do not go together";
    if (ObjectMgr::CheckPlayerName(name, LOCALE_enUS, true) != CHAR_NAME_SUCCESS)
        return "that name is not allowed";
    if (!sCharacterCache->GetCharacterGuidByName(name).IsEmpty())
        return "that name is taken";

    std::string accountName;
    if (!AccountMgr::GetName(accountId, accountName))
        return "no such account";

    // A Player needs a session. This one lives only for the creation; nothing is logged in through it.
    BotSession session(accountId, std::move(accountName));

    WorldPackets::Character::CharacterCreateInfo info;
    info.Race = race;
    info.Class = playerClass;
    info.Sex = gender;
    info.Name = name;

    // Made and destroyed the way the core's own character creation does it.
    std::unique_ptr<Player, void (*)(Player*)> player(new Player(&session), [](Player* p)
    {
        p->CleanupsBeforeDelete();
        delete p;
    });
    player->GetMotionMaster()->Initialize();
    if (!player->Create(sObjectMgr->GetGenerator<HighGuid::Player>().Generate(), &info))
        return "the core refused to create it";

    player->setCinematic(1);
    player->SetAtLoginFlag(AT_LOGIN_FIRST);

    LoginDatabaseTransaction loginTransaction = LoginDatabase.BeginTransaction();
    CharacterDatabaseTransaction characterTransaction = CharacterDatabase.BeginTransaction();
    player->SaveToDB(loginTransaction, characterTransaction, true);
    CharacterDatabase.DirectCommitTransaction(characterTransaction);
    LoginDatabase.DirectCommitTransaction(loginTransaction);

    sCharacterCache->AddCharacterCacheEntry(player->GetGUID(), accountId, player->GetName(), player->GetNativeGender(), player->GetRace(),
        player->GetClass(), player->GetLevel(), false);
    TC_LOG_INFO("tortoisebots", "BotMgr: created {} ({}) on account {}", player->GetName(), player->GetGUID().ToString(), accountId);
    return {};
}

bool BotMgr::WalkBy(ObjectGuid characterGuid, float dx, float dy)
{
    Player* me = FindBot(characterGuid);
    if (!me)
        return false;
    float x = me->GetPositionX() + dx;
    float y = me->GetPositionY() + dy;
    float z = me->GetPositionZ();
    me->UpdateAllowedPositionZ(x, y, z);
    me->GetMotionMaster()->MovePoint(0, x, y, z);
    return true;
}

std::string BotMgr::AttackNearest(ObjectGuid characterGuid, float range)
{
    Player* me = FindBot(characterGuid);
    if (!me)
        return "not a logged-in bot";

    Unit* target = nullptr;
    Trinity::NearestAttackableUnitInObjectRangeCheck check(me, me, range);
    Trinity::UnitLastSearcher<Trinity::NearestAttackableUnitInObjectRangeCheck> searcher(me, target, check);
    Cell::VisitAllObjects(me, searcher, range);
    if (!target)
        return "nothing to attack in range";

    me->SetSelection(target->GetGUID());
    me->Attack(target, true);
    me->GetMotionMaster()->MoveChase(target);
    return "attacking " + target->GetName();
}

Player* BotMgr::FindBot(ObjectGuid characterGuid) const
{
    auto itr = _bots.find(characterGuid);
    if (itr == _bots.end())
        return nullptr;
    Player* player = itr->second.session->GetPlayer();
    return player && player->IsInWorld() ? player : nullptr;
}

void BotMgr::Update(uint32 diff)
{
    std::vector<ObjectGuid> finished;

    for (auto& [guid, bot] : _bots)
    {
        // The owner logs in to this very character: the core asks the bot to leave and waits. A bot session cannot
        // see that request (it is a private flag), so it watches the account's real session instead.
        if (WorldSession* real = sWorld->FindSession(bot.session->GetAccountId()); real && real->PlayerLoading() && !real->GetPlayer())
        {
            bot.ownerLoadingMs += diff;
            if (bot.ownerLoadingMs >= OwnerLoadingLimitMs)
                bot.session->RequestLogout("the account owner is logging in");
        }
        else
            bot.ownerLoadingMs = 0;

        if (!bot.session->Tick(diff))
        {
            finished.push_back(guid);
            continue;
        }

        Player* me = bot.session->GetPlayer();
        if (!me || !me->IsInWorld() || me->IsBeingTeleported())
            continue;

        bot.thinkMs += diff;
        if (bot.thinkMs < ThinkIntervalMs)
            continue;
        bot.thinkMs = 0;
        Think(bot, me);
    }

    for (ObjectGuid const& guid : finished)
    {
        TC_LOG_INFO("tortoisebots", "BotMgr: session of {} ended", guid.ToString());
        _bots.erase(guid);
    }
}

void BotMgr::Think(Bot& bot, Player* me)
{
    // No master: the bot only does what the console tells it (WalkBy, AttackNearest).
    if (bot.master.IsEmpty())
        return;

    Player* master = ObjectAccessor::FindPlayer(bot.master);
    if (!master)
    {
        // Master gone (logged out or on a loading screen for a long time is handled by the caller's next ticks).
        if (!ObjectAccessor::FindConnectedPlayer(bot.master))
            bot.session->RequestLogout("the master left the game");
        return;
    }

    // Stand next to the master once after login, and again whenever it is on another map or far away.
    bool const sameMap = me->GetMap() == master->GetMap();
    if (!bot.placed || !sameMap || !me->IsWithinDist(master, 100.0f))
    {
        bot.placed = true;
        bot.following = false;
        bot.chasing.Clear();
        bot.trail.clear();
        me->AttackStop();
        me->GetMotionMaster()->Clear();
        me->TeleportTo(master->GetWorldLocation());
        return;
    }

    if (!me->IsAlive())
        return;

    Unit* target = master->GetVictim();
    if (target && target->IsAlive() && me->IsValidAttackTarget(target))
    {
        if (bot.chasing != target->GetGUID() || me->GetVictim() != target)
        {
            bot.chasing = target->GetGUID();
            bot.following = false;
            me->SetSelection(target->GetGUID());
            me->Attack(target, true);
            me->GetMotionMaster()->MoveChase(target);
        }
        return;
    }

    if (!bot.chasing.IsEmpty())
    {
        bot.chasing.Clear();
        me->AttackStop();
        me->SetSelection(ObjectGuid::Empty);
    }

    // Far behind and out of combat: walk the master's own path instead of a straight line (the same rule and numbers
    // as the Turtle build, from the shared FollowTrailPolicy.h).
    ai::FollowTrailRecord(bot.trail, master->GetPositionX(), master->GetPositionY(), master->GetPositionZ());
    if (me->GetExactDist2d(master) > ai::kRaidFollowCatchUpDistanceYd)
    {
        int const index = ai::FollowTrailWaypoint(bot.trail, me->GetPositionX(), me->GetPositionY());
        if (index >= 0)
        {
            ai::FollowTrailPoint const& point = bot.trail[std::size_t(index)];
            bot.following = false;
            me->GetMotionMaster()->MovePoint(0, point.x, point.y, point.z);
            return;
        }
    }

    if (!bot.following)
    {
        bot.following = true;
        me->GetMotionMaster()->Clear();
        me->GetMotionMaster()->MoveFollow(master, _followDistance, ChaseAngle(float(M_PI)));
    }
}
}
