#include "TbBotMgr.h"
#include "TbBotSession.h"
#include "AccountMgr.h"
#include "CellImpl.h"
#include "CharacterPackets.h"
#include "DatabaseEnv.h"
#include "GridNotifiers.h"
#include "Group.h"
#include "GroupMgr.h"
#include "GridNotifiersImpl.h"
#include "ObjectMgr.h"
#include "CharacterCache.h"
#include "Config.h"
#include "Log.h"
#include "StringFormat.h"
#include "DB2Stores.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "World.h"
#include <algorithm>
#include <memory>
#include <optional>
#include <vector>

namespace TortoiseBots
{
namespace
{
constexpr uint32 ThinkIntervalMs = 500;
// The core waits 30 s for a bot to leave when its owner logs in to the same character. A normal character load is
// well under this, so a real session that is still loading after this long is waiting for us.
constexpr uint32 OwnerLoadingLimitMs = 2000;
// R11: how long a bot whose master is on a map it could not enter waits before it asks the core again (it also asks at once when the master changes map).
constexpr uint32 FollowRetryMs = 30000;
// N2: the catch-up teleport of one bot happens at most once per this long.
constexpr uint32 CatchUpCooldownMs = 30000;

// The core's reason for refusing a far teleport (Map::PlayerCannotEnter, Maps/Map.cpp:1829-1875), in words.
std::string AbortReasonText(TransferAbortParams const& denied)
{
    char const* what = nullptr;
    switch (denied.Reason)
    {
        case TRANSFER_ABORT_ERROR: what = "error"; break;
        case TRANSFER_ABORT_MAX_PLAYERS: what = "the instance is full"; break;
        case TRANSFER_ABORT_NOT_FOUND: what = "instance not found"; break;
        case TRANSFER_ABORT_TOO_MANY_INSTANCES: what = "too many instances entered recently"; break;
        case TRANSFER_ABORT_ZONE_IN_COMBAT: what = "an encounter is in progress"; break;
        case TRANSFER_ABORT_INSUF_EXPAN_LVL: what = "the expansion is not available"; break;
        case TRANSFER_ABORT_DIFFICULTY: what = denied.MapDifficultyXConditionId ? "refused by an entry condition (a MapDifficultyXCondition failed)" :
            "no map difficulty data for the dungeon difficulty asked for (GetDownscaledMapDifficultyData found none)"; break;
        case TRANSFER_ABORT_NEED_GROUP: what = "a raid group is needed"; break;
        case TRANSFER_ABORT_MAP_NOT_ALLOWED: what = "map not allowed"; break;
        case TRANSFER_ABORT_LOCKED_TO_DIFFERENT_INSTANCE: what = "locked to a different instance"; break;
        case TRANSFER_ABORT_DIFFICULTY_NOT_FOUND: what = "difficulty not found"; break;
        default: break;
    }
    return what ? Trinity::StringFormat("{} (TransferAbortReason {})", what, uint32(denied.Reason))
                : Trinity::StringFormat("TransferAbortReason {}", uint32(denied.Reason));
}
}

namespace
{
// L8 (package L): reason TRANSFER_ABORT_DIFFICULTY has two sources (Maps/Map.cpp:1843: no MapDifficulty row, id 0; Player::Satisfy,
// Entities/Player/Player.cpp:21463: a failed MapDifficultyXCondition, id != 0). For the second, read the failed condition's PlayerCondition: a
// minimum level the player is under is "level too low for this instance (needs N)" (measured live: level 9 refused, 10 allowed for Ragefire
// Chasm); any other condition is "refused by an entry condition". The text of the other reasons is AbortReasonText's.
std::string EntryRefusalText(TransferAbortParams const& denied, uint32 mapId, Player* who)
{
    if (denied.Reason == TRANSFER_ABORT_DIFFICULTY && denied.MapDifficultyXConditionId && who)
    {
        if (MapEntry const* entry = sMapStore.LookupEntry(mapId))
        {
            Group* group = who->GetGroup();
            Difficulty const difficulty = group ? group->GetDifficultyID(entry) : who->GetDifficultyID(entry);
            if (MapDifficultyEntry const* mapDiff = sDB2Manager.GetDownscaledMapDifficultyData(mapId, difficulty))
                if (DB2Manager::MapDifficultyConditionsContainer const* conditions = sDB2Manager.GetMapDifficultyConditions(mapDiff->ID))
                    for (auto const& [order, condition] : *conditions)
                        if (int32(order) == denied.MapDifficultyXConditionId && condition)
                        {
                            if (condition->MinLevel && who->GetLevel() < condition->MinLevel)
                                return Trinity::StringFormat("level too low for this instance (needs {}) (TransferAbortReason {}, MapDifficultyXCondition {}, PlayerCondition {})",
                                    uint32(condition->MinLevel), uint32(denied.Reason), order, condition->ID);
                            return Trinity::StringFormat("refused by an entry condition (TransferAbortReason {}, MapDifficultyXCondition {}, PlayerCondition {})",
                                uint32(denied.Reason), order, condition->ID);
                        }
        }
        return Trinity::StringFormat("refused by an entry condition (TransferAbortReason {})", uint32(denied.Reason));
    }
    return AbortReasonText(denied);
}
}

// N1 (package N): the ONE place the module asks the core whether a character may enter a map. Map::PlayerCannotEnter reaches InstanceMap::CannotEnter
// (Maps/Map.cpp:2965-2971), which ABORTS the server when the player already stands in the instance it is asked about (live: package L, twice). So the
// question is never asked for a character that is not in the world, is being teleported, or whose map id IS the one asked about. Returns "" when the core has
// no objection (or the map is not a dungeon), else the reason in words (an entry refusal as EntryRefusalText says it).
std::string BotMgr::CanEnterMap(Player* who, uint32 mapId)
{
    if (!who || !who->IsInWorld())
        return "the character is not in the world";
    if (who->IsBeingTeleported())
        return "the character is being teleported";
    if (who->GetMapId() == mapId)
        return "already on that map";
    if (!sMapStore.LookupEntry(mapId))
        return Trinity::StringFormat("map {} is not in the map store", mapId);
    TransferAbortParams const denied = Map::PlayerCannotEnter(mapId, who);
    return denied ? EntryRefusalText(denied, mapId, who) : std::string();
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
    _engineByDefault = sConfigMgr->GetBoolDefault("TortoiseBots.Slice.EngineByDefault", false);
    // R10 (S2): how long a bot waits for a master that logged out. Default 300 s, the hire lifecycle's own: AiPlayerbot.HireDisconnectGracePeriod,
    // default 300 (ai/playerbot/PlayerbotAIConfig.h:621, read at PlayerbotAIConfig.cpp:1050, capped at 3600 on the next line, used by
    // HireLifecycle::SweepGracePeriod, runtime/HireLifecycle.cpp:781-896). The same cap here; 0 = no grace, the bots log out with their master.
    int32 const graceSeconds = sConfigMgr->GetIntDefault("TortoiseBots.Slice.MasterGraceSeconds", 300);
    _masterGraceMs = uint32(std::clamp<int32>(graceSeconds, 0, 3600)) * 1000;
    if (_engineByDefault && !engine::Built())
    {
        TC_LOG_WARN("tortoisebots", "BotMgr: TortoiseBots.Slice.EngineByDefault is 1 but the engine is not built in (TORTOISEBOTS_SLICE1 is off); bots use the spike behaviour");
        _engineByDefault = false;
    }
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
    // R10 (S3): the engine hears of every teleport the session acknowledges for this bot (looked up by guid at the time: the brain comes and goes).
    bot.session->SetTeleportAckHook([this, characterGuid]()
    {
        if (auto itr = _bots.find(characterGuid); itr != _bots.end() && itr->second.brain)
            engine::HandleTeleportAck(itr->second.brain.get());
    });
    bot.master = master;
    bot.wantParty = !master.IsEmpty();
    bot.useEngine = _engineByDefault;
    if (bot.wantParty)
    {
        // A party holds 5: the master and four bots. Count the bots that are on their way into the party as well, so five
        // logins in a row are not all accepted just because none has joined yet.
        uint32 members = 1;
        if (Player* leader = ObjectAccessor::FindPlayer(master))
            if (Group* group = leader->GetGroup())
            {
                // R10 (S1): the host forms 5-man parties only. A raid is refused here, before a session is made (JoinParty checks it again).
                if (group->isRaidGroup())
                    return "the master leads a raid: raids are not in this slice, the bot is not logged in";
                members = group->GetMembersCount();
            }
        for (auto const& [guid, other] : _bots)
            if (other.wantParty && other.master == master)
                ++members;
        if (members >= MAX_GROUP_SIZE)
            return Trinity::StringFormat("the party of that master is full ({} members at most, raids are not in this slice)", uint32(MAX_GROUP_SIZE));
    }
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

std::string BotMgr::JoinParty(Player* bot, Player* master)
{
    if (bot == master)
        return "a bot cannot be its own master";
    if (!bot->IsInWorld() || !master->IsInWorld())
        return "both characters must be in the world";

    Group* group = master->GetGroup();
    // R10 (S1): the host forms and keeps 5-man parties. Group::IsFull is the 40-member check for a raid, so a raid would have taken the bot as
    // its 4th or 30th member: refuse it first (even when the bot is already in it: the host's master binding is for a party).
    if (group && group->isRaidGroup())
        return "the master leads a raid: raids are not in this slice, the bot was not added";
    if (group && bot->GetGroup() == group)
        return {};                                   // already in it
    if (bot->GetGroup() || bot->GetGroupInvite())
        return "the bot is already in another party (use .tb ungroup first)";
    if (master->GetGroupInvite())
        return "the master has a pending party invite";
    if (group)
    {
        if (!group->IsLeader(master->GetGUID()))
            return "the master is not the leader of its party";
        // The party's own five, counted against MAX_GROUP_SIZE here and not through IsFull (which is the raid's 40 for a raid).
        if (group->GetMembersCount() >= MAX_GROUP_SIZE)
            return Trinity::StringFormat("the party is full ({} members at most, raids are not in this slice)", uint32(MAX_GROUP_SIZE));
    }

    // The invite, as HandlePartyInviteOpcode makes it: a new group is not created or saved until the first accept.
    bool createdHere = false;
    if (!group)
    {
        group = new Group();
        if (!group->AddLeaderInvite(master))
        {
            delete group;
            return "the core refused the leader's invite";
        }
        if (!group->AddInvite(bot))
        {
            group->RemoveAllInvites();
            delete group;
            return "the core refused the invite";
        }
    }
    else if (!group->AddInvite(bot))
        return "the core refused the invite";

    // The accept, as HandlePartyInviteResponseOpcode (Accept = true) does it.
    group->RemoveInvite(bot);
    if (!group->IsCreated())
    {
        group->RemoveInvite(master);
        group->Create(master);
        sGroupMgr->AddGroup(group);
        createdHere = true;
    }
    if (!group->AddMember(bot))
    {
        // R10 (S1): a group this call made is saved (Group::Create writes it) and holds only the master now: take it down again, so the master is not
        // left in a persisted one-member group nobody asked for (Group::Disband removes the saved rows, the group manager's entry and the group itself;
        // `group` is gone after it). A group the master already had is left as it was.
        if (createdHere)
        {
            group->Disband(true);
            master->RemovePlayerFlag(PLAYER_FLAGS_GROUP_LEADER);   // Group::Create set it; the core clears it again only when the player loads (_LoadGroup)
            TC_LOG_ERROR("tortoisebots", "BotMgr: the party made for {} was disbanded again: the core refused to add {}", master->GetName(), bot->GetName());
        }
        return "the core refused to add the bot to the party";
    }
    group->BroadcastGroupUpdate();
    return {};
}

std::string BotMgr::GroupWith(ObjectGuid botGuid, ObjectGuid masterGuid)
{
    auto itr = _bots.find(botGuid);
    if (itr == _bots.end())
        return "not a logged-in bot";
    Player* bot = FindBot(botGuid);
    Player* master = ObjectAccessor::FindPlayer(masterGuid);
    if (!bot)
        return "the bot is not in the world yet";
    if (!master)
        return "the master is not in the world";
    std::string const error = JoinParty(bot, master);
    if (!error.empty())
        return error;
    itr->second.master = masterGuid;
    itr->second.wantParty = false;
    itr->second.masterAway = false;
    return {};
}

std::string BotMgr::Ungroup(ObjectGuid botGuid)
{
    auto itr = _bots.find(botGuid);
    if (itr == _bots.end())
        return "not a logged-in bot";
    Player* bot = FindBot(botGuid);
    if (!bot)
        return "the bot is not in the world";
    if (!bot->GetGroup())
        return "the bot is not in a party";
    bot->RemoveFromGroup();
    itr->second.master = ObjectGuid::Empty;
    itr->second.wantParty = false;
    itr->second.masterAway = false;
    return {};
}

std::string BotMgr::SetEngine(ObjectGuid characterGuid, bool on)
{
    auto itr = _bots.find(characterGuid);
    if (itr == _bots.end())
        return "not a logged-in bot";
    Bot& bot = itr->second;
    if (on)
    {
        if (!engine::Built())
            return "the engine is not built in (TORTOISEBOTS_SLICE1 is off)";
        // Marked again after a failure: try to build the brain once more.
        bot.useEngine = true;
        bot.engineFailed = false;
        bot.engineNote.clear();
        return {};
    }

    bot.useEngine = false;
    bot.engineFailed = false;
    bot.engineNote.clear();
    if (bot.brain)
    {
        DropBrain(bot);
        // The engine's movement and attack state is left behind: start clean, the spike's Think takes over.
        if (Player* me = bot.session->GetPlayer(); me && me->IsInWorld())
        {
            me->AttackStop();
            me->GetMotionMaster()->Clear();
        }
        bot.following = false;
        bot.chasing.Clear();
    }
    return {};
}

std::vector<std::string> BotMgr::DescribeBrain(ObjectGuid characterGuid) const
{
    std::vector<std::string> lines;
    auto itr = _bots.find(characterGuid);
    if (itr == _bots.end())
        return lines;
    Bot const& bot = itr->second;
    if (bot.masterAway)
    {
        lines.push_back("brain: none (detached)");
        lines.push_back(Trinity::StringFormat("master away, {} s left", (bot.graceLeftMs + 999) / 1000));
    }
    else if (bot.brain)
    {
        lines.push_back("brain: engine");
        for (std::string const& line : engine::Describe(bot.brain.get()))
            lines.push_back(line);
    }
    else if (bot.engineFailed)
        lines.push_back("brain: spike (engine requested, not running: " + bot.engineNote + ")");
    else if (bot.useEngine)
        lines.push_back("brain: spike (engine requested, waiting for the bot to be placed next to its master)");
    else
        lines.push_back("brain: spike");
    if (bot.followBlocked)
        lines.push_back(Trinity::StringFormat("follow: blocked, the master is on map {} (instance {}) and the bot cannot go there: {}; asks again in {} s or when the master changes map",
            bot.followBlockedMap, bot.followBlockedInstance, bot.followBlockedWhy, (bot.followRetryMs + 999) / 1000));
    return lines;
}

void BotMgr::DropBrain(Bot& bot)
{
    // The engine's registry entry and the AI go before the Player does (see PlayerbotAIAdapter::Shutdown).
    bot.brain.reset();
}

void BotMgr::DetachFromMaster(Bot& bot)
{
    DropBrain(bot);
    if (_masterGraceMs == 0)
    {
        bot.master = ObjectGuid::Empty;
        bot.wantParty = false;
        if (!bot.session->LogoutRequested())
            bot.session->RequestLogout("the master left the game");
        return;
    }
    if (bot.masterAway)
        return;
    bot.masterAway = true;
    bot.masterSeenGone = false;
    bot.graceLeftMs = _masterGraceMs;
    bot.following = false;
    bot.chasing.Clear();
    bot.trail.clear();
    // Stop what the engine or the follow was doing: nothing steers the bot now. It stays where it is, in its party.
    if (Player* me = bot.session->GetPlayer(); me && me->IsInWorld())
    {
        me->AttackStop();
        me->GetMotionMaster()->Clear();
    }
    TC_LOG_INFO("tortoisebots", "BotMgr: the master of {} is away: the bot stays and waits {} s for it", bot.session->GetCharacterGuid().ToString(), _masterGraceMs / 1000);
}

bool BotMgr::UpdateMasterAway(Bot& bot, uint32 diff)
{
    Player* master = ObjectAccessor::FindPlayer(bot.master);
    if (!master)
        bot.masterSeenGone = true;
    // The master's own logout runs in an update of its own session: until it has been missing once, a master that is still found is the one on
    // its way out, not one that came back.
    if (master && bot.masterSeenGone)
    {
        bot.masterAway = false;
        bot.masterSeenGone = false;
        bot.graceLeftMs = 0;
        bot.placed = false;      // stand next to the master again (Think teleports it when it is on another map or far away)
        Player* me = bot.session->GetPlayer();
        if (me && me->IsInWorld())
        {
            // The master is in the saved party when it was a real player (the core keeps a logged-out member): nothing to do. A party that does not
            // hold the master any more (it was a bot, which leaves its party when it leaves the world) is left, and Think joins the master's own.
            if (Group* group = me->GetGroup(); group && !group->IsMember(master->GetGUID()))
                me->RemoveFromGroup(GROUP_REMOVEMETHOD_LEAVE);
            if (!me->GetGroup())
                bot.wantParty = true;
        }
        TC_LOG_INFO("tortoisebots", "BotMgr: the master of {} is back: the bot follows again", bot.session->GetCharacterGuid().ToString());
        return true;
    }

    if (bot.graceLeftMs > diff)
    {
        bot.graceLeftMs -= diff;
        return false;
    }
    bot.graceLeftMs = 0;
    bot.masterAway = false;
    bot.master = ObjectGuid::Empty;
    bot.wantParty = false;
    if (!bot.session->LogoutRequested())
        bot.session->RequestLogout("the master did not come back within the grace period");
    return false;
}

void BotMgr::OnPlayerLogout(Player* player)
{
    // PlayerScript::OnLogout runs inside WorldSession::LogoutPlayer, before the Player is deleted: the last point at
    // which the engine can still be taken down in the order the legacy code requires. Covers the logouts the core
    // starts by itself as well as ours.
    ObjectGuid const leaving = player->GetGUID();

    // The bots whose MASTER is the player logging out (R9, findings R1'): their engines hold a pointer to it (the engine re-checks it by guid
    // every tick, so it is safe today; this is the defence). R10 (S2): the original keeps hired bots through the master's disconnect and reunites them
    // (HireLifecycle, the disconnect grace), so the bot is DETACHED, not logged out: the brain goes (its master pointer with it), the bot stays in the
    // world and in its party, keeps the master's guid, and DetachFromMaster starts the grace. A bot that is already on its way out (server shutdown,
    // the account owner logging in) is not detached: its brain goes and its master is cleared, as in R9.
    for (auto& [guid, bot] : _bots)
    {
        if (guid == leaving || bot.master != leaving)
            continue;
        if (bot.session->LogoutRequested())
        {
            DropBrain(bot);
            bot.master = ObjectGuid::Empty;
            bot.wantParty = false;
            continue;
        }
        DetachFromMaster(bot);
    }

    auto itr = _bots.find(leaving);
    if (itr == _bots.end())
        return;
    DropBrain(itr->second);
    // A bot that leaves the world leaves its party too (a dismissed hire): the core keeps a logged-out member in the saved group, which
    // would count against the party's five seats at the next login and re-form a party nobody asked for.
    if (player->GetGroup())
        player->RemoveFromGroup(GROUP_REMOVEMETHOD_LEAVE);
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
    // The character statements are prepared for the asynchronous connections only, so the commit is queued, as in the core.
    CharacterDatabase.CommitTransaction(characterTransaction);
    LoginDatabase.CommitTransaction(loginTransaction);

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

std::string BotMgr::Hop(ObjectGuid characterGuid, float dx, float dy)
{
    Player* me = FindBot(characterGuid);
    if (!me)
        return "not a logged-in bot";
    if (me->IsBeingTeleported())
        return "the bot is already being teleported";
    float const x = me->GetPositionX() + dx;
    float const y = me->GetPositionY() + dy;
    float z = me->GetPositionZ();
    me->UpdateAllowedPositionZ(x, y, z);
    if (!me->TeleportTo(WorldLocation(me->GetMapId(), x, y, z, me->GetOrientation())))
        return "the core refused the teleport";
    return {};
}

std::vector<std::string> BotMgr::CanEnter(ObjectGuid characterGuid, uint32 mapId)
{
    std::vector<std::string> lines;
    Player* who = ObjectAccessor::FindPlayer(characterGuid);
    if (!who)
    {
        lines.push_back("refused: the character is not in the world");
        return lines;
    }
    MapEntry const* entry = sMapStore.LookupEntry(mapId);
    if (!entry)
    {
        lines.push_back(Trinity::StringFormat("refused: map {} is not in the map store", mapId));
        return lines;
    }
    std::string const answer = CanEnterMap(who, mapId);
    lines.push_back(answer.empty() ? std::string("allowed: Map::PlayerCannotEnter has no objection") : (answer == "already on that map" ? answer : "refused: " + answer));
    Group* group = who->GetGroup();
    lines.push_back(Trinity::StringFormat("map {}: dungeon {}, raid {}; {} difficulty asked for: {}", mapId, entry->IsDungeon() ? "yes" : "no", entry->IsRaid() ? "yes" : "no",
        group ? "the party's" : "the character's own", uint32(group ? group->GetDifficultyID(entry) : who->GetDifficultyID(entry))));
    return lines;
}

std::string BotMgr::ConvertToRaid(ObjectGuid masterGuid)
{
    Player* master = ObjectAccessor::FindPlayer(masterGuid);
    if (!master)
        return "the master is not in the world";
    Group* group = master->GetGroup();
    if (!group)
        return "the master is not in a party";
    if (group->isRaidGroup())
        return "the party is a raid already";
    if (!group->IsLeader(masterGuid))
        return "the character is not the leader of its party";
    group->ConvertToRaid();
    return {};
}

std::string BotMgr::AttackNearest(ObjectGuid characterGuid, float range)
{
    Player* me = FindBot(characterGuid);
    if (!me)
        return "not a logged-in bot";

    // Nearest living creature the bot is allowed to attack. Neutral ones count (a level 1 wolf is neutral), so this
    // is "may attack", not "is hostile".
    std::vector<Unit*> units;
    Trinity::AnyUnfriendlyUnitInObjectRangeCheck check(me, me, range);
    Trinity::UnitListSearcher<Trinity::AnyUnfriendlyUnitInObjectRangeCheck> searcher(me, units, check);
    Cell::VisitAllObjects(me, searcher, range);

    Unit* target = nullptr;
    for (Unit* unit : units)
        if (unit->IsCreature() && unit->IsAlive() && me->IsValidAttackTarget(unit) && (!target || me->GetDistance(unit) < me->GetDistance(target)))
            target = unit;
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

std::vector<Player*> BotMgr::BotsOfMaster(ObjectGuid master) const
{
    std::vector<Player*> result;
    for (auto const& [guid, bot] : _bots)
    {
        if (bot.master != master)
            continue;
        Player* player = bot.session->GetPlayer();
        if (player && player->IsInWorld())
            result.push_back(player);
    }
    return result;
}

bool BotMgr::SetMaster(ObjectGuid characterGuid, ObjectGuid master)
{
    auto itr = _bots.find(characterGuid);
    if (itr == _bots.end())
        return false;
    itr->second.master = master;
    itr->second.masterAway = false;
    return true;
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

        // A logout is pending (ours: the console, the owner logging in, a missing master): the next Tick logs the Player
        // out, so the engine goes first.
        if (bot.session->LogoutRequested())
            DropBrain(bot);

        if (!bot.session->Tick(diff))
        {
            finished.push_back(guid);
            continue;
        }

        // R10 (S2): a bot whose master is away does nothing (no Think, no engine) until it is back; the grace counts down meanwhile.
        if (bot.masterAway && !UpdateMasterAway(bot, diff))
            continue;

        Player* me = bot.session->GetPlayer();
        if (!me || !me->IsInWorld() || me->IsBeingTeleported())
            continue;

        bot.followRetryMs = bot.followRetryMs > diff ? bot.followRetryMs - diff : 0;
        bot.catchUpCooldownMs = bot.catchUpCooldownMs > diff ? bot.catchUpCooldownMs - diff : 0;
        bot.thinkMs += diff;
        if (bot.thinkMs >= ThinkIntervalMs)
        {
            bot.thinkMs = 0;
            Think(bot, me);
        }

        // The engine brain: every tick, with the tick's diff (PlayerbotAIBase::UpdateAI keeps its own delay). World
        // thread, from WorldScript::OnBeforeMapsUpdate, i.e. before the maps update; the legacy engine takes no map
        // lock and expects none (SLICE1-R5-NOTES.md). Think may have dropped the brain or sent the bot on a teleport.
        // Not in the update in which its logout was requested or its master was lost (R9): Think may have asked for the logout above, and
        // the brain is gone by then.
        if (bot.brain && !bot.session->LogoutRequested() && !me->IsBeingTeleported())
            engine::Tick(bot.brain.get(), diff);
    }

    for (ObjectGuid const& guid : finished)
    {
        TC_LOG_INFO("tortoisebots", "BotMgr: session of {} ended", guid.ToString());
        // Normally gone already (OnPlayerLogout, or the pending-logout check above); never leave one behind.
        if (auto itr = _bots.find(guid); itr != _bots.end())
            DropBrain(itr->second);
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
        // Master gone (logged out or on a loading screen for a long time is handled by the caller's next ticks). The engine goes with it
        // (R9): a brain is never kept, and never ticked, while its master is not in the world.
        // R10 (S2): a master that is gone from the server starts the grace (the bot stays); one that is only between maps is waited for.
        DropBrain(bot);
        if (!ObjectAccessor::FindConnectedPlayer(bot.master))
            DetachFromMaster(bot);
        return;
    }

    // The hire flow's party (findings item 21): the master leads a party of its bots, so the engine's "remove bot masters not in
    // our group" rule keeps the master of a console-driven (bot session) master. One try; a refusal logs the bot out again.
    if (bot.wantParty)
    {
        bot.wantParty = false;
        std::string const error = JoinParty(me, master);
        if (!error.empty())
        {
            TC_LOG_ERROR("tortoisebots", "BotMgr: {} could not join the party of {}: {}", me->GetName(), master->GetName(), error);
            bot.session->RequestLogout("it could not join its master's party");
            return;
        }
        TC_LOG_INFO("tortoisebots", "BotMgr: {} joined the party of {} ({} member(s))", me->GetName(), master->GetName(),
            me->GetGroup() ? me->GetGroup()->GetMembersCount() : 0u);
    }

    // N2 (package N): the catch-up teleport has brakes. A bound bot is put next to its master only in the open world: never while the bot or the
    // master is in combat, dead, being teleported or changing maps; never into or inside an instance map and never between instances (a bot gets into
    // a dungeon by its own entry in slice 4, or by an explicit console command); on the same continent map only when it is over 100 yd away and has
    // been out of line of sight for 10 s; on another (non-instance) map at once; at most once per 30 s per bot. A refused catch-up sets the blocked
    // reason `.tb state` shows (`stuck=blocked:<reason>`); a bot whose master is in sight or close just follows.
    bool const sameMap = me->GetMap() == master->GetMap();
    bool const sameMapId = me->GetMapId() == master->GetMapId();
    bool const far = sameMap && !me->IsWithinDist(master, 100.0f);
    if (sameMap && !far)
        bot.outOfSightMs = 0;
    else if (far)
        bot.outOfSightMs = me->IsWithinLOSInMap(master) ? 0 : bot.outOfSightMs + ThinkIntervalMs;
    if (sameMap && !far)
    {
        bot.placed = true;
        bot.followBlocked = false;
    }
    else
    {
        bool const inInstance = me->GetMap()->Instanceable();
        bool const masterInInstance = master->GetMap()->Instanceable();
        std::string reason;
        bool wanted = true;
        if (me->IsInCombat() || master->IsInCombat())
            reason = "in combat";
        else if (!me->IsAlive() || !master->IsAlive())
            reason = "dead";
        else if (me->IsBeingTeleported() || master->IsBeingTeleported() || !master->IsInWorld())
            reason = "teleporting";
        else if (sameMapId && (inInstance || masterInInstance))
            reason = "commander in another instance";
        else if (masterInInstance)
            reason = "commander in an instance (a bot enters one by its own entry or a console command)";
        else if (inInstance)
            reason = "bot in an instance";
        else if (sameMap && !bot.placed && !far)
            wanted = false;
        else if (far && bot.outOfSightMs < 10000)
        {
            wanted = false;                     // in sight (or not yet 10 s out of it): the engine's own follow walks
            bot.followBlocked = false;
        }
        else if (bot.catchUpCooldownMs > 0)
            reason = "catch-up cooldown";

        if (wanted && reason.empty())
        {
            // The core's own entry check, behind the one guarded host function (N1).
            uint32 const masterMap = master->GetMapId();
            // The same continent needs no entry check (and the guard would answer "already on that map"); another non-instance map is asked.
            std::string const denied = sameMapId ? std::string() : CanEnterMap(me, masterMap);
            if (!denied.empty())
                reason = denied;
            else
            {
                bot.placed = true;
                bot.following = false;
                bot.chasing.Clear();
                bot.trail.clear();
                bot.outOfSightMs = 0;
                bot.catchUpCooldownMs = CatchUpCooldownMs;
                me->AttackStop();
                me->GetMotionMaster()->Clear();
                if (me->TeleportTo(master->GetWorldLocation()))
                {
                    bot.followBlocked = false;
                    return;
                }
                reason = "Player::TeleportTo refused the teleport";
            }
        }

        if (wanted)
        {
            uint32 const masterMap = master->GetMapId();
            uint32 const masterInstance = master->GetInstanceId();
            bool const changed = !bot.followBlocked || bot.followBlockedMap != masterMap || bot.followBlockedInstance != masterInstance || bot.followBlockedWhy != reason;
            bot.followBlocked = true;
            bot.followBlockedMap = masterMap;
            bot.followBlockedInstance = masterInstance;
            bot.followBlockedWhy = reason;
            // A bot that is on another map than its master cannot follow by path: its engine goes (live: 395 "[BOT PATH BUG]" lines a minute), and comes
            // back below once it stands on the master's map. A bot on the master's map keeps its engine (it is fighting or walking).
            if (!sameMap)
                DropBrain(bot);
            if (changed)
                TC_LOG_INFO("tortoisebots", "BotMgr: {} is not put next to its master {} (map {}, instance {}): {}", me->GetName(), master->GetName(), masterMap, masterInstance, reason);
            if (!sameMap)
                return;
        }
    }

    // The engine: once the bot is in the world and next to its master (above), build its PlayerbotAI. From then on the
    // engine does the following and fighting, and Think only keeps the placement and teleport above.
    if (bot.useEngine && !bot.engineFailed && !bot.brain)
    {
        std::string error;
        bot.brain.reset(engine::Create(me, master, error));
        if (!bot.brain)
        {
            bot.engineFailed = true;
            bot.engineNote = error;
            TC_LOG_ERROR("tortoisebots", "BotMgr: engine for {} not started: {} (the spike behaviour stays)", me->GetName(), error);
        }
    }
    if (bot.brain)
        return;

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
