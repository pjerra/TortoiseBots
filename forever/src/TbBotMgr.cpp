#include "TbBotMgr.h"
#include "host/TbTickBudgetPure.h"
#include "host/TbAutoFood.h"
#include "host/TbHostGroupQuestPure.h"
#include "host/TbHostCapitalErrands.h"
#include "host/TbHostAhMarket.h"
#include "host/TbHostGuildMail.h"
#include "host/TbHostGuildJoin.h"
#include "host/TbHostGuildOrders.h"
#include "host/TbHostGuildOrderDisenchant.h"
#include "host/TbHostGuildOrderQueues.h"
#include "host/TbHostGuildOrderEnchant.h"
#include "host/TbHostFishing.h"
#include "host/TbHostProfession.h"
#include "host/TbHostThievery.h"
#include "host/TbHostProfessionUpkeep.h"
#include "host/TbHostWorldPvp.h"
#include "host/TbHostRitualFill.h"
#include "host/TbHostShareMobs.h"
#include "TbBotSession.h"
#include "AccountMgr.h"
#include "AreaTriggerPackets.h"
#include "CellImpl.h"
#include "CharacterPackets.h"
#include "DatabaseEnv.h"
#include "GameTime.h"
#include "QuestDef.h"
#include "GridNotifiers.h"
#include "Group.h"
#include "GroupMgr.h"
#include "InstanceScript.h"
#include "GridNotifiersImpl.h"
#include "ObjectMgr.h"
#include "CharacterCache.h"
#include "Config.h"
#include "Log.h"
#include "StringFormat.h"
#include "DB2Stores.h"
#include "host/TbEngineMapPure.h"
#include "host/TbLuaBotsPure.h"   // TBF-B1 finding 1: KeepInSnapshot, the provider's retention rule, also read here (game-free)
#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1
#include "host/TbOrdersApi.h"     // TBF-B1 finding 7: ForgetBotRecords (the per-bot order record and ring)
#include "host/run/TbRunHooks.h"  // TBF-B1 finding 7: OnBotLogout (an ended run record goes with its leader)
#endif
#include "host/chat/TbChatIn.h"
#include "host/TbThrown.h"
#include "Map.h"
#include "MiscPackets.h"
#include "MotionMaster.h"
#include "Corpse.h"
#include "compat/TbCompat_W1.h"
#include "compat/TbCompat_WL0.h"
#include "ObjectAccessor.h"
#include "Opcodes.h"
#include "Player.h"
#include "RealmList.h"
#include "ScriptMgr.h"
#include "World.h"
#include "WorldSession.h"
#include "WorldPacket.h"
#include "BattlenetAccountMgr.h"
#include <algorithm>
#include <chrono>
#include <future>
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
// H4: an entry (a walk to the area trigger, the leader's wait, the teleport) that has not ended after this long is refused with `block=timeout`.
constexpr uint32 EntryTimeoutMs = 120000;
// N2/N3: a commanded bot's body that has lain this long (TortoiseBots.Slice.DeadBackstopSeconds, 180 s) in a dungeon releases itself (at the entrance, the normal rule) whatever holds it: no rezzer able to
// resurrect, a commander on another map or a hold that never ends must not strand it (and keep the instance from resetting).
constexpr uint32 DeadBackstopDefaultMs = 180000;   // TortoiseBots.Slice.DeadBackstopSeconds
// H4: the walk to the trigger makes no progress (the best distance does not shrink by a yard) for this long: `block=stuck`.
constexpr uint32 EntryNoProgressMs = 20000;
// A bot the budget skipped gets the time since its own last turn when it runs
// next, so its AI delays count wall time, not only the ticks it was visited.
// Capped so a login or a long pause cannot replay a huge step into the engine.
constexpr uint32 MaxAiElapsedMs = 10000;

// The core's reason for refusing a far teleport (Map::PlayerCannotEnter, Maps/Map.cpp:1978-2024), in words.
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
// L8 (package L): reason TRANSFER_ABORT_DIFFICULTY has two sources (Maps/Map.cpp:1992: no MapDifficulty row, id 0; Player::Satisfy,
// Entities/Player/Player.cpp:21587: a failed MapDifficultyXCondition, id != 0). For the second, read the failed condition's PlayerCondition: a
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

// H4: the refusal in the words of `.tb state`'s `block=` key (spec section 6): level full locked difficulty not-found other (the guard answers below add
// teleporting, in-combat, dead, already-on-map). The text is EntryRefusalText's.
namespace
{
std::string EntryRefusalCode(TransferAbortParams const& denied, std::string const& text)
{
    switch (denied.Reason)
    {
        case TRANSFER_ABORT_DIFFICULTY:
            if (text.rfind("level too low", 0) == 0)
                return "level";
            return denied.MapDifficultyXConditionId ? "other" : "difficulty";
        case TRANSFER_ABORT_MAX_PLAYERS: return "full";
        case TRANSFER_ABORT_NOT_FOUND: return "not-found";
        case TRANSFER_ABORT_LOCKED_TO_DIFFERENT_INSTANCE:
        case TRANSFER_ABORT_ALREADY_COMPLETED_ENCOUNTER: return "locked";
        default: return "other";
    }
}
}

// N1 (package N): the ONE place the module asks the core whether a character may enter a map. Map::PlayerCannotEnter reaches InstanceMap::CannotEnter
// (Maps/Map.cpp:3138-3144), which ABORTS the server when the player already stands in the instance it is asked about (live: package L, twice). So the
// question is never asked for a character that is not in the world, is being teleported, or whose map id IS the one asked about. Returns the code and the
// reason in words; an empty code = the core has no objection (or the map is not a dungeon).
// H4 (C1): CanEnterMap (below) and the entry code both go through here: `grep PlayerCannotEnter forever/src` finds this one call.
BotMgr::EntryCheck BotMgr::CheckEntry(Player* who, uint32 mapId)
{
    if (!who || !who->IsInWorld())
        return { "other", "the character is not in the world" };
    if (who->IsBeingTeleported())
        return { "teleporting", "the character is being teleported" };
    if (who->GetMapId() == mapId)
        return { "already-on-map", "already on that map" };
    MapEntry const* target = sMapStore.LookupEntry(mapId);
    if (!target)
        return { "other", Trinity::StringFormat("map {} is not in the map store", mapId) };
    TransferAbortParams const denied = Map::PlayerCannotEnter(mapId, who);
    if (!denied)
    {
        // F-population: a free bot does not pile into a dungeon or raid past its size (dungeons 5, raids 10/20/40 by the map
        // row's player count). Commanded bots enter as a group of their own, under the core's group rules, not this cap.
        if (Instance().IsFreeBot(who->GetGUID()))
        {
            uint32 const cap = world::FreeMapCapFor(target->IsDungeon(), target->IsRaid(), target->MaxPlayers, Instance().GetFreeMapCaps());
            if (cap)
            {
                uint32 const present = Instance().FreeBotsOnMap(mapId);
                if (present >= cap)
                    return { "map-cap", Trinity::StringFormat("map {} already holds {} free bot(s): the cap is {}", mapId, present, cap) };
            }
        }
        return {};
    }
    std::string text = EntryRefusalText(denied, mapId, who);
    std::string code = EntryRefusalCode(denied, text);
    return { std::move(code), std::move(text) };
}

uint32 BotMgr::FreeBotsOnMap(uint32 mapId) const
{
    uint32 count = 0;
    for (ObjectGuid const guid : BotGuids())
    {
        if (!IsFreeBot(guid))
            continue;
        Player const* bot = FindBot(guid);
        if (bot && bot->GetMapId() == mapId)
            ++count;
    }
    return count;
}

// Returns "" when the core has no objection (or the map is not a dungeon), else the reason in words.
std::string BotMgr::CanEnterMap(Player* who, uint32 mapId)
{
    return CheckEntry(who, mapId).text;
}

// The record of a creation in flight. The session is declared before the player so the player goes first (it holds the session).
struct BotMgr::PendingCreation
{
    uint32 accountId = 0;
    std::string name;
    uint8 race = 0;
    uint8 playerClass = 0;
    uint8 gender = 0;
    std::shared_ptr<BotSession> session;
    std::shared_ptr<Player> player;
    LoginDatabaseTransaction loginTransaction;
    std::future<bool> commit;
};

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
    _capitalErrandsEnabled = sConfigMgr->GetBoolDefault("TortoiseBots.World.CapitalErrands", true);
    _guildMailEnabled = sConfigMgr->GetBoolDefault("TortoiseBots.World.GuildMail", true);
    _guildMailAllowMixed = sConfigMgr->GetBoolDefault("TortoiseBots.World.GuildMail.AllowMixedGuild", false);
    _guildsEnabled = sConfigMgr->GetBoolDefault("TortoiseBots.World.Guilds", true);
    _guildFormingEnabled = sConfigMgr->GetBoolDefault("TortoiseBots.World.GuildForming", false);
    _guildAcceptPlayers = sConfigMgr->GetBoolDefault("TortoiseBots.World.GuildAcceptPlayers", false);
    _groupQuestsEnabled = sConfigMgr->GetBoolDefault("TortoiseBots.World.GroupQuests", true);
    _professionsEnabled = sConfigMgr->GetBoolDefault("TortoiseBots.World.Professions", false);
    _fishingEnabled = sConfigMgr->GetBoolDefault("TortoiseBots.World.Fishing", true);
#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1
    tbhost::thievery::LoadConfig(); // the host thievery services are slice-only, like their tick
#endif
    _disenchantUpkeepEnabled = sConfigMgr->GetBoolDefault("TortoiseBots.World.Disenchant", true);
    _enchantGearEnabled = sConfigMgr->GetBoolDefault("TortoiseBots.World.EnchantGear", true);
    // R10 (S2): how long a bot waits for a master that logged out. Default 300 s, the hire lifecycle's own: AiPlayerbot.HireDisconnectGracePeriod,
    // default 300 (ai/playerbot/PlayerbotAIConfig.h:621, read at PlayerbotAIConfig.cpp:1050, capped at 3600 on the next line, used by
    // HireLifecycle::SweepGracePeriod, runtime/HireLifecycle.cpp:781-896). The same cap here; 0 = no grace, the bots log out with their master.
    int32 const graceSeconds = sConfigMgr->GetIntDefault("TortoiseBots.Slice.MasterGraceSeconds", 300);
    _masterGraceMs = uint32(std::clamp<int32>(graceSeconds, 0, 3600)) * 1000;
    _deadBackstopMs = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("TortoiseBots.Slice.DeadBackstopSeconds", DeadBackstopDefaultMs / 1000), 10, 3600)) * 1000;
    // F-population: how many free bots one map of each kind holds before another free bot is refused its entry (0 = no cap).
    _freeMapCaps.dungeon = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("TortoiseBots.Free.MaxPerMap.Dungeon", 5), 0, 1000));
    _freeMapCaps.raid10 = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("TortoiseBots.Free.MaxPerMap.Raid10", 10), 0, 1000));
    _freeMapCaps.raid20 = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("TortoiseBots.Free.MaxPerMap.Raid20", 20), 0, 1000));
    _freeMapCaps.raid40 = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("TortoiseBots.Free.MaxPerMap.Raid40", 40), 0, 1000));
    // #24: pool bots leave camps and quest mobs to the real players working them. The switch is OFF until the live rows prove it;
    // the radii, the workers one quest spot feeds and the give-up time are clamped by the policy (5-100, near-150, 1-10, 60-3600).
    world::ShareMobsConfig shareMobs;
    shareMobs.enabled = sConfigMgr->GetBoolDefault("TortoiseBots.World.PoolShareMobs", false);
    shareMobs.nearYd = sConfigMgr->GetFloatDefault("TortoiseBots.World.PoolShareMobs.NearYd", 30.0f);
    shareMobs.farYd = sConfigMgr->GetFloatDefault("TortoiseBots.World.PoolShareMobs.FarYd", 60.0f);
    shareMobs.questSpotCap = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("TortoiseBots.World.PoolShareMobs.QuestSpotCap", 3), 0, 1000));
    shareMobs.giveUpSeconds = uint32(std::clamp<int32>(sConfigMgr->GetIntDefault("TortoiseBots.World.PoolShareMobs.GiveUpSeconds", 600), 0, 100000));
    _shareMobs = world::ClampShareMobsConfig(shareMobs);
    if (_engineByDefault && !engine::Built())
    {
        TC_LOG_WARN("tortoisebots", "BotMgr: TortoiseBots.Slice.EngineByDefault is 1 but the engine is not built in (TORTOISEBOTS_SLICE1 is off); bots use the spike behaviour");
        _engineByDefault = false;
    }
}

bool BotMgr::AccountHasBot(uint32 accountId) const
{
    for (auto const& entry : _bots)
        if (entry.second.session->GetAccountId() == accountId)
            return true;
    return false;
}

uint32 BotMgr::OwnedBotsOf(uint32 ownerBnet) const
{
    // Every state counts (loading, in the world, away, on its way out): the cap is the Battle.net account's, whichever of its
    // characters or game accounts added the bot.
    if (ownerBnet == 0)
        return 0;
    uint32 count = 0;
    for (auto const& [guid, bot] : _bots)
        if (bot.owned && bot.ownerBnet == ownerBnet)
            ++count;
    return count;
}

std::vector<uint32> BotMgr::BotAccounts() const
{
    std::vector<uint32> accounts;
    accounts.reserve(_bots.size());
    for (auto const& entry : _bots)
        accounts.push_back(entry.second.session->GetAccountId());
    std::sort(accounts.begin(), accounts.end());
    accounts.erase(std::unique(accounts.begin(), accounts.end()), accounts.end());
    return accounts;
}

std::string BotMgr::Login(ObjectGuid characterGuid, ObjectGuid master, bool free, bool pool, bool owned, bool engine)
{
    if (!_enabled)
        return "TortoiseBots.Enable is 0";
    if (characterGuid.IsEmpty())
        return "no such character";
    if (_bots.count(characterGuid))
        return "that bot is already logged in or loading";
    if (free && !master.IsEmpty())
        return "a free bot has no master";
    if (pool && !free)
        return "a pool bot is a free bot";
    if (ObjectAccessor::FindConnectedPlayer(characterGuid))
        return "that character is already in the world";

    uint32 const accountId = sCharacterCache->GetCharacterAccountIdByGuid(characterGuid);
    if (!accountId)
        return "that character has no account";
    // D2: one session per account. The check is here, where the session is made, so no caller can put two characters of an
    // account in the world at once (the pool's scheduler sees the same rule through AccountHasBot).
    // D-3 (amended: owner = Battle.net account): an owned login needs its owner (the master) in the world now, and is stamped with the
    // owner's Battle.net account. It may share a game account with other bots only when that game account is the owner's and every bot
    // already on it is an owned bot of the same owner; a pool or a stranger's bot on the account keeps the one-bot rule. Fail closed.
    uint32 ownerBnet = 0;
    if (owned)
    {
        Player* owner = ObjectAccessor::FindPlayer(master);
        if (!owner || !owner->IsInWorld() || !owner->GetSession())
            return "the owner is not in the world";
        ownerBnet = owner->GetSession()->GetBattlenetAccountId();
        if (ownerBnet == 0)
            return "the owner has no Battle.net account";
    }
    if (AccountHasBot(accountId))
    {
        bool share = owned && Battlenet::AccountMgr::GetIdByGameAccount(accountId) == ownerBnet;
        for (auto const& [guid, other] : _bots)
            if (share && other.session->GetAccountId() == accountId && (!other.owned || other.ownerBnet != ownerBnet))
                share = false;
        if (!share)
            return "another bot of that account is already logged in or loading";
    }

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
    bot.free = free;
    bot.pool = pool;
    bot.owned = owned;
    bot.ownerBnet = ownerBnet;
    // TBF-B1 (review finding 5): the API's `owner` is the Battle.net account that owns the bot's CHARACTER: the session already
    // carries it (BotSession hands WorldSession Battlenet::AccountMgr::GetIdByGameAccount(accountId) once, at login), so every path
    // (`.bot add` by the owner or a GM, `.tb login`, the pool) reports the character's own account, never the adder's and never 0.
    bot.characterBnet = bot.session->GetBattlenetAccountId();
    bot.wantParty = !master.IsEmpty();
    // A free bot always runs the engine: there is no spike behaviour that grinds, rests and corpse-runs.
    // An owned bot always runs the engine too: `.bot` orders and chat need it. A hire asks for it explicitly.
    bot.useEngine = _engineByDefault || free || owned || engine;
    // A bot with no master has nothing to follow, so an engine for it is the free kit (engine::Create refuses a
    // masterless brain without the free flag, and PlayerbotAI's default strategies read the free flag). A login that
    // asks for the engine without naming a master therefore starts a free bot; with the engine off it stands.
    if (bot.useEngine && bot.master.IsEmpty())
        bot.free = true;
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
    _knownBots.insert(characterGuid);
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

// W1 (review): a bot has one identity, and a master wins. A free bot that is handed a master stops being free at that moment: the
// autonomous kit and the ghost bookkeeping go, and it plays for the master until `.tb free on` says otherwise. Every path that binds a
// master lands here (`.tb group`, the commander binding, a run, the engine's adoption), so none of them can leave a bot both commanded
// and free. The engine needs the master pointer before its kit is rebuilt (the factory reads the free flag, so the flag goes first).
void BotMgr::BindMaster(Bot& bot, ObjectGuid master)
{
    if (!master.IsEmpty() && bot.free)
    {
        bot.free = false;
        bot.ghostRescue.Reset();
        bot.ghostYd = -1.0f;
        bot.rescuedThisDeath = false;
        if (bot.brain)
            engine::BindMaster(bot.brain.get(), ObjectAccessor::FindPlayer(master));
        Player* me = bot.session->GetPlayer();
        TC_LOG_INFO("tortoisebots", "BotMgr: {} is no longer a free bot (a master is bound)", me ? me->GetName() : bot.session->GetCharacterGuid().ToString());
    }
    bot.master = master;
    bot.masterAway = false;
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
    BindMaster(itr->second, masterGuid);
    itr->second.wantParty = false;
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
        if (bot.master.IsEmpty() && !bot.free)
        {
            // A bot with no master runs the engine only as a free bot (engine::Create refuses a masterless brain
            // without the free flag): marking it without switching the free kit on would report success while the
            // bot stands, as the spike does for a masterless bot.
            Player* me = bot.session->GetPlayer();
            if (me && me->GetGroup())
                return "the bot has no master and is in a party: a free bot plays alone";
            bot.free = true;
            TC_LOG_INFO("tortoisebots", "BotMgr: {} runs the engine without a master: it is a free bot", me ? me->GetName() : characterGuid.ToString());
        }
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
        lines.push_back(bot.free ? "brain: spike (engine requested, the free kit builds it on the next tick)"
                                 : "brain: spike (engine requested, waiting for the bot to be placed next to its master)");
    else
        lines.push_back("brain: spike");
    if (bot.entry.active)
        lines.push_back(Trinity::StringFormat("entry: area trigger {} -> map {}, {}{}", bot.entry.triggerId, bot.entry.targetMap, bot.entry.phase, bot.entry.direct ? " (direct teleport)" : ""));
    if (!bot.block.empty())
        lines.push_back("last refusal: " + bot.block + " (" + bot.blockText + ")");
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
    // A battleground holds the bot until the match is over or the battleground module takes it out: logging out here would pull a
    // participant out of a running match. A queue entry does not hold it: the battleground module takes a bot out of a queue whose
    // human is gone, and a bot left waiting in a queue forever is a worse leak than a re-queue. UpdateMasterAway retries once it is
    // out.
    bool const held = [&]
    {
        Player* me = bot.session->GetPlayer();
        return me && me->IsInWorld() && me->InBattleground();
    }();
    if (_masterGraceMs == 0 && !held)
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
    // A running match holds the bot: the battleground module leaves it (master-gone) and this pass logs it out afterwards. A queue
    // entry does not: the module took the bot out of the queue already, and waiting for it here would keep the bot forever.
    if (Player* me = bot.session->GetPlayer())
        if (me->IsInWorld() && me->InBattleground())
        {
            bot.graceLeftMs = 0;
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
    kb::Forget(leaving);

    // The bots whose MASTER is the player logging out (R9, findings R1'): their engines hold a pointer to it (the engine re-checks it by guid
    // every tick, so it is safe today; this is the defence). R10 (S2): the original keeps hired bots through the master's disconnect and reunites them
    // (HireLifecycle, the disconnect grace), so the bot is DETACHED, not logged out: the brain goes (its master pointer with it), the bot stays in the
    // world and in its party, keeps the master's guid, and DetachFromMaster starts the grace. A bot that is already on its way out (server shutdown,
    // the account owner logging in) is not detached: its brain goes and its master is cleared, as in R9.
    for (auto& [guid, bot] : _bots)
    {
        if (guid == leaving || bot.master != leaving)
            continue;
        // D-3: an owned bot is in the world only while its owner is: no grace, it leaves with the owner (a pending login too: the
        // session logs the Player out as soon as its load finishes).
        if (bot.session->LogoutRequested() || bot.owned)
        {
            DropBrain(bot);
            bot.master = ObjectGuid::Empty;
            bot.wantParty = false;
            if (!bot.session->LogoutRequested())
                bot.session->RequestLogout("its owner left the world");
            continue;
        }
        DetachFromMaster(bot);
    }

    auto itr = _bots.find(leaving);
    if (itr == _bots.end())
        return;
    chatin::Forget(leaving);     // S1-R2: the bot's chat inbox and its counters go with it (a later bot reusing the guid starts clean)
#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1
    tbhost::prof::ClearBotErrands(leaving);
    tbhost::errands::ClearBotCapitalErrands(leaving);
    tbhost::ah_market::ClearBotAhMarket(leaving);
    tbhost::guild_mail::ClearBotGuildMail(leaving);
    tbhost::guildjoin::ForgetBot(leaving);
    tbhost::thievery::ForgetBot(leaving.GetCounter());
#endif
    DropBrain(itr->second);
    // A bot that leaves the world leaves its party too (a dismissed hire): the core keeps a logged-out member in the saved group, which
    // would count against the party's five seats at the next login and re-form a party nobody asked for.
    if (player->GetGroup())
        player->RemoveFromGroup(GROUP_REMOVEMETHOD_LEAVE);
#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1
    tbhost::guild_orders::OnBotLogout(leaving.GetCounter());
    // TBF-B1 (review finding 7): the bot's per-bot records go with it: its order record and ring (nothing of the order or of the
    // ring leaks across a relog), the accept/role-check timers, and an ENDED run record it left in the run table (a live one is
    // the tick's to stop). The run record is removed after the provider took its unread events (run::Remove's flush listener).
    h3::ForgetBotRecords(leaving);
    run::OnBotLogout(leaving);
#endif
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
    for (std::shared_ptr<PendingCreation> const& pending : _creating)
        if (pending->name == name)
            return "that name is taken";

    std::string accountName;
    if (!AccountMgr::GetName(accountId, accountName))
        return "no such account";

    // The realm's character count of the account after this one, as the core's own creation counts it (characters not deleted,
    // plus this one, plus any creation of the account still in flight). A database that does not answer stops the creation:
    // a count that is guessed is a wrong `realmcharacters` row.
    std::uint64_t existing = 0;
    if (!PoolRealmCharacterCountCompat(accountId, existing))
        return "the characters database did not answer how many characters the account has";
    for (std::shared_ptr<PendingCreation> const& pending : _creating)
        if (pending->accountId == accountId)
            ++existing;
    uint32 const realmCharacters = uint32(existing + 1);

    // A Player needs a session. This one lives as long as the creation does (the creation hooks may read it); nothing is
    // logged in through it.
    auto pending = std::make_shared<PendingCreation>();
    pending->session = std::make_shared<BotSession>(accountId, std::move(accountName));

    WorldPackets::Character::CharacterCreateInfo info;
    info.Race = race;
    info.Class = playerClass;
    info.Sex = gender;
    info.Name = name;
    // W0: a pool character is made in its race's own start position. The core's default is already false (CharacterPackets.h:67);
    // written out because the pool's gate depends on the normal (not the NPE) create position.
    info.UseNPE = false;

    // Made and destroyed the way the core's own character creation does it.
    pending->player = std::shared_ptr<Player>(new Player(pending->session.get()), [](Player* p)
    {
        p->CleanupsBeforeDelete();
        delete p;
    });
    Player* const player = pending->player.get();
    player->GetMotionMaster()->Initialize();
    if (!player->Create(sObjectMgr->GetGenerator<HighGuid::Player>().Generate(), &info))
        return "the core refused to create it";

    player->setCinematic(1);
    player->SetAtLoginFlag(AT_LOGIN_FIRST);

    LoginDatabaseTransaction loginTransaction = LoginDatabase.BeginTransaction();
    CharacterDatabaseTransaction characterTransaction = CharacterDatabase.BeginTransaction();
    player->SaveToDB(loginTransaction, characterTransaction, true);
    LoginDatabasePreparedStatement* realmStmt = LoginDatabase.GetPreparedStatement(LOGIN_REP_REALM_CHARACTERS);
    realmStmt->setUInt32(0, realmCharacters);
    realmStmt->setUInt32(1, accountId);
    realmStmt->setUInt32(2, sRealmList->GetCurrentRealmId().Realm);
    loginTransaction->Append(realmStmt);

    // The character commit goes first and alone: the login side is committed only when it answers true (ConfirmCreations), so a
    // failed character commit leaves no `realmcharacters` row and no account data behind. The character statements are prepared
    // for the asynchronous connections only, so the commit is queued, as in the core.
    pending->name = name;
    pending->accountId = accountId;
    pending->race = race;
    pending->playerClass = playerClass;
    pending->gender = gender;
    pending->loginTransaction = loginTransaction;
    TransactionCallback commit = CharacterDatabase.AsyncCommitTransaction(characterTransaction);
    pending->commit = std::move(commit.m_future);
    TC_LOG_INFO("tortoisebots", "BotMgr: creating {} ({}) on account {}: its saves are queued", name, player->GetGUID().ToString(), accountId);
    _creating.push_back(std::move(pending));
    return {};
}

bool BotMgr::CreationInFlight(std::string const& name) const
{
    for (std::shared_ptr<PendingCreation> const& pending : _creating)
        if (pending->name == name)
            return true;
    return false;
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

Player* BotMgr::SnapshotBot(ObjectGuid characterGuid) const
{
    auto itr = _bots.find(characterGuid);
    if (itr == _bots.end())
        return nullptr;
    Player* player = itr->second.session->GetPlayer();
    // TBF-B1 (review finding 1): the provider's rule, in the pure half's words: the character object exists, the session has not
    // asked for a logout, and the bot is in the world or on a far teleport (IsInWorld() is false for that tick).
    return luabots::KeepInSnapshot(player != nullptr, player && player->IsInWorld(), player && player->IsBeingTeleportedFar(),
                                   itr->second.session->LogoutRequested())
               ? player
               : nullptr;
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
    BindMaster(itr->second, master);
    return true;
}


// ---- slice 4, host package H4 ----

void BotMgr::FailEntry(Bot& bot, std::string code, std::string text)
{
    TC_LOG_INFO("tortoisebots", "BotMgr: the entry of {} (trigger {}) ended: {} ({})", bot.session->GetCharacterGuid().ToString(), bot.entry.triggerId, code, text);
    // N6: every exit of an entry stops the walk (the MovePoint generator would otherwise keep going, as M4-1 showed after a landing).
    if (Player* walker = bot.session->GetPlayer(); walker && walker->IsInWorld() && !walker->IsBeingTeleported() && !engine::InFlight(bot.brain.get()))
    {
        walker->GetMotionMaster()->Clear();
        walker->StopMoving();
    }
    bot.entry = Entry();
    bot.landing.pending = false;
    bot.block = std::move(code);
    bot.blockText = std::move(text);
}

BotMgr::EntryStart BotMgr::StartEntry(ObjectGuid botGuid, uint32 triggerId, ObjectGuid leader, bool direct)
{
    auto itr = _bots.find(botGuid);
    if (itr == _bots.end())
        return { "not-a-bot", "not a logged-in host bot" };
    Bot& bot = itr->second;
    Player* me = FindBot(botGuid);
    if (!me)
        return { "other", "the bot is not in the world yet" };
    if (bot.entry.active)
        return { "duplicate", "an entry of this bot is running already" };
    AreaTriggerEntry const* at = sAreaTriggerStore.LookupEntry(triggerId);
    if (!at)
        return { "bad-id", "no such area trigger in AreaTrigger.db2" };
    // The trigger's target: the same row the core's handler reads (areatrigger_teleport, ObjectMgr::GetAreaTrigger, MiscHandler.cpp:748).
    AreaTriggerTeleport const* target = sObjectMgr->GetAreaTrigger(triggerId);
    if (!target)
        return { "bad-id", "the area trigger has no areatrigger_teleport target (it leads nowhere)" };
    if (me->GetMapId() != at->ContinentID)
        return { "other-map", Trinity::StringFormat("the bot is on map {}, the trigger is on map {}", me->GetMapId(), uint32(at->ContinentID)) };
    if (!me->IsAlive())
        return { "dead", "a dead bot does not enter" };
    if (me->IsInCombat())
        return { "in-combat", "the bot is in combat" };
    uint32 const targetMap = target->Loc.GetMapId();
    if (EntryCheck const check = CheckEntry(me, targetMap); !check.code.empty())
        return { check.code, check.text };

    Entry entry;
    entry.active = true;
    entry.triggerId = triggerId;
    entry.targetMap = targetMap;
    entry.safeLocId = target->ID;
    entry.leader = leader == botGuid ? ObjectGuid::Empty : leader;
    entry.direct = direct;
    entry.phase = "walking";
    bot.entry = entry;
    bot.block.clear();
    bot.blockText.clear();
    // The engine is down while the host walks the bot (it would fight the walk with its own follow); Think does not build it again until the entry has ended.
    DropBrain(bot);
    bot.following = false;
    bot.chasing.Clear();
    me->AttackStop();
    me->GetMotionMaster()->Clear();
    TC_LOG_INFO("tortoisebots", "BotMgr: {} enters through area trigger {} (map {}){}", me->GetName(), triggerId, targetMap, direct ? ", direct teleport (D1 fallback)" : "");
    return {};
}

void BotMgr::UpdateEntry(Bot& bot, Player* me, uint32 diff)
{
    Entry& e = bot.entry;
    // Landed: the bot stands on the trigger's target map. (The caller made sure the bot is in the world and not being teleported.)
    if (me->GetMapId() == e.targetMap)
    {
        if (!engine::InFlight(bot.brain.get()))
        {
            me->GetMotionMaster()->Clear();   // nothing of the walk may carry over into the dungeon
            me->StopMoving();
        }
        // N6: only a landing the entry job itself started (its handler or direct teleport) is `entered=trigger|order`; any other way onto the map is `other`.
        bot.entered = e.teleporting ? (e.direct ? "order" : "trigger") : "other";
        bot.block.clear();
        bot.blockText.clear();
        uint32 const instanceId = me->GetInstanceId();
        // N1: keyed by (map, instance); the entry that opened a fresh instance (entrance still 0) names the trigger, an older entry's name is kept otherwise.
        InstanceScript const* const landedScript = me->GetMap() && me->GetMap()->ToInstanceMap() ? me->GetMap()->ToInstanceMap()->GetInstanceScript() : nullptr;
        std::pair<uint32, uint32> const landedKey{ me->GetMapId(), instanceId };
        if (instanceId && (!_entryTriggerByInstance.count(landedKey) || (landedScript && landedScript->GetEntranceLocation() == 0)))
            _entryTriggerByInstance[landedKey] = e.triggerId;
        TC_LOG_INFO("tortoisebots", "BotMgr: {} entered map {} (instance {}) through area trigger {}", me->GetName(), e.targetMap, instanceId, e.triggerId);
        bot.entry = Entry();
        return;
    }
    e.ageMs += diff;
    if (e.teleporting)
    {
        // The handler's teleport has ended and the bot is not on the target map: the core ported it somewhere else (the arrival check can fail after
        // the entry check, Handlers/MovementHandler.cpp:241-248).
        FailEntry(bot, "teleport-refused", "the teleport ended and the bot is not in the dungeon (the core ported it to its homebind; see the server log, category network)");
        return;
    }
    if (e.ageMs > EntryTimeoutMs)
    {
        FailEntry(bot, "timeout", "the entry did not finish in 120 s");
        return;
    }
    e.stepMs += diff;
    if (e.stepMs < 500)
        return;
    e.stepMs = 0;

    if (!me->IsAlive())
    {
        FailEntry(bot, "dead", "the bot died before it entered");
        return;
    }
    if (me->IsInCombat())
    {
        FailEntry(bot, "in-combat", "the bot is in combat: no entry (it walks again after the fight when it is ordered again)");
        return;
    }
    AreaTriggerEntry const* at = sAreaTriggerStore.LookupEntry(e.triggerId);
    if (!at)
    {
        FailEntry(bot, "bad-id", "the area trigger is gone");
        return;
    }

    // Followers enter after their leader (the group's recent instance, MapManager.cpp:300, is the one the leader opened).
    bool leaderIn = true;
    if (!e.leader.IsEmpty())
    {
        Player* leader = ObjectAccessor::FindPlayer(e.leader);
        leaderIn = leader && leader->IsInWorld() && !leader->IsBeingTeleported() && leader->GetMapId() == e.targetMap;
        auto const litr = _bots.find(e.leader);
        bool const leaderWorking = litr != _bots.end() && litr->second.entry.active;
        if (!leaderIn && !leaderWorking)
        {
            FailEntry(bot, "leader-refused", "the party leader is not in the dungeon and is not entering it");
            return;
        }
    }

    bool const inBox = !e.direct && me->IsInAreaTrigger(at);
    if (!e.direct && !inBox)
    {
        e.phase = "walking";
        float const distance = me->GetExactDist(at->Pos.X, at->Pos.Y, at->Pos.Z);
        if (distance + 1.0f < e.bestDist)
        {
            e.bestDist = distance;
            e.noProgressMs = 0;
        }
        else
            e.noProgressMs += 500;
        if (e.noProgressMs >= EntryNoProgressMs)
        {
            FailEntry(bot, "stuck", Trinity::StringFormat("the walk to the trigger stalled {:.1f} yd from its centre", e.bestDist));
            return;
        }
        if (me->GetMotionMaster()->GetCurrentMovementGeneratorType() != POINT_MOTION_TYPE)
            me->GetMotionMaster()->MovePoint(1, at->Pos.X, at->Pos.Y, at->Pos.Z);
        return;
    }
    if (!leaderIn)
    {
        e.phase = "waiting-leader";
        return;
    }

    // M4 live finding: the walk's MovePoint generator survived the teleport and the bot kept walking toward the BOX'S map-1 coordinates inside the dungeon
    // (a straight line, 200+ yd in 30 s). Stop it before the handler runs. A flight in the air is not cut (S3).
    if (!engine::InFlight(bot.brain.get()))
    {
        me->GetMotionMaster()->Clear();
        me->StopMoving();
    }

    // The entry check, behind the one guarded function (C1), then the core's own path.
    if (EntryCheck const check = CheckEntry(me, e.targetMap); !check.code.empty())
    {
        FailEntry(bot, check.code, check.text);
        return;
    }
    if (e.direct)
    {
        // D1's fallback: the handler's last step without the walk (MiscHandler.cpp:748-753): the instance's entrance when it names one, else the trigger's target.
        AreaTriggerTeleport const* target = sObjectMgr->GetAreaTrigger(e.triggerId);
        WorldSafeLocsEntry const* entrance = me->GetInstanceEntrance(e.targetMap);
        bool const started = target && (entrance ? me->TeleportTo(entrance->Loc, TELE_TO_NOT_LEAVE_TRANSPORT) : me->TeleportTo(target->Loc, TELE_TO_NOT_LEAVE_TRANSPORT));
        if (!started)
        {
            FailEntry(bot, "teleport-refused", "Player::TeleportTo refused the teleport");
            return;
        }
    }
    else
    {
        // The core's own area-trigger handler for a bot inside the box: the typed packet {trigger, Entered}; the handler checks the box, the corpse rule,
        // the entry rule and teleports to the instance's entrance (MiscHandler.cpp:538-753). A bot session is a WorldSession: the handler is a public member.
        WorldPackets::AreaTrigger::AreaTrigger packet{ WorldPacket(CMSG_AREA_TRIGGER) };
        packet.AreaTriggerID = int32(e.triggerId);
        packet.Entered = true;
        packet.FromClient = false;
        me->GetSession()->HandleAreaTriggerOpcode(packet);
        if (!me->IsBeingTeleported())
        {
            FailEntry(bot, "other", "the core's area-trigger handler did not teleport the bot (a condition, a script or the corpse rule refused it: see the server log, category maps)");
            return;
        }
    }
    e.teleporting = true;
    e.phase = "teleporting";
}

BotMgr::EntryInfo BotMgr::GetEntryInfo(ObjectGuid botGuid) const
{
    EntryInfo info;
    auto itr = _bots.find(botGuid);
    if (itr == _bots.end())
        return info;
    Bot const& bot = itr->second;
    info.entered = bot.entered;
    info.block = bot.block;
    info.blockText = bot.blockText;
    info.catchups = bot.catchups;
    info.entering = bot.entry.active;
    info.phase = bot.entry.active ? bot.entry.phase : std::string();
    return info;
}

void BotMgr::NoteTeleportStarted(ObjectGuid botGuid, char const* how, uint32 destMapId)
{
    auto itr = _bots.find(botGuid);
    if (itr == _bots.end())
        return;
    itr->second.landing.pending = true;
    itr->second.landing.how = how;
    ++itr->second.teleports;               // W1 `tele=`: every teleport the bot took, any cause

    itr->second.landing.mapId = destMapId;
    itr->second.block.clear();
    itr->second.blockText.clear();
}

void BotMgr::MarkPractice(ObjectGuid botGuid)
{
    auto itr = _bots.find(botGuid);
    if (itr == _bots.end())
        return;
    itr->second.practice = true;
    Player* me = FindBot(botGuid);
    itr->second.practiceSawCombat = me && me->IsInCombat();
}

// D2 (the lead's decision of 2026-11-19): the entrance of a dungeon is given to the instance's script by the host, so a release inside revives the
// bot at the entrance INSIDE the instance (Player::RepopAtGraveyard asks the script first, Player.cpp:5248-5249, and revives in place for every
// non-raid dungeon, :5220; without an entrance it falls to the closest graveyard, which for Ragefire Chasm and an Alliance bot is Razor Hill).
// InstanceScript::SetEntranceLocation is a public member (InstanceScript.h:293). Once per instance; the trigger the first entry used is the source, else
// ObjectMgr::GetMapEntranceTrigger(map) (the first areatrigger_teleport row that leads into the map; a dungeon with several entrances needs the entry's own).
// N5: the host's own "commanded" test, engine-free (this file is in both builds): the bot's host master is a character OUTSIDE the bot's party (the commander,
// `.tb master`). A bot led by its own party's leader is not commanded.
bool BotMgr::IsCommandedHere(Bot const& bot, Player* me) const
{
    if (bot.master.IsEmpty() || !me)
        return false;
    Group* const group = me->GetGroup();
    return group && !group->IsMember(bot.master);
}

// N1 (package N): `EnsureEntrance` keeps no decision by instance id alone. Every think of a commanded bot that stands in a dungeon of ITS OWN group (N5: never a
// real player's instance), the instance script's entrance is read; when it is 0 the entrance for THIS (map, instance) is worked out and set, and a failure is
// retried at the next think (the log line is once per (map, instance)). The trigger of the last entry into the instance is used only when its target is this
// map (a stale RFC trigger must not name an entrance for Deadmines); otherwise the first trigger that leads into the map (ObjectMgr::GetMapEntranceTrigger).
// InstanceScript::SetEntranceLocation is public (InstanceScript.h:293). D2: the bot then revives at the entrance inside the instance after a release
// (Player::RepopAtGraveyard, Player.cpp:5248-5249) instead of at the zone's graveyard (Razor Hill for Ragefire Chasm).
void BotMgr::EnsureEntrance(Bot const& bot, Player* me)
{
    Map* map = me->GetMap();
    InstanceMap* instanceMap = map ? map->ToInstanceMap() : nullptr;
    if (!instanceMap || !instanceMap->IsDungeon())
        return;
    if (!IsCommandedHere(bot, me) || !instanceMap->GetOwningGroup() || instanceMap->GetOwningGroup() != me->GetGroup())
        return;
    uint32 const mapId = instanceMap->GetId();
    uint32 const instanceId = instanceMap->GetInstanceId();
    std::pair<uint32, uint32> const key{ mapId, instanceId };
    InstanceScript* script = instanceMap->GetInstanceScript();
    if (!script)
    {
        if (_entranceFailLogged.insert(key).second)
            TC_LOG_WARN("tortoisebots", "BotMgr: map {} (instance {}) has no instance script: the host cannot give it an entrance, a release there is held (relhold=no-entrance)", mapId, instanceId);
        return;
    }
    if (script->GetEntranceLocation())
    {
        _entranceFailLogged.erase(key);
        return;
    }
    uint32 safeLocId = 0;
    if (auto const itr = _entryTriggerByInstance.find(key); itr != _entryTriggerByInstance.end())
        if (AreaTriggerTeleport const* used = sObjectMgr->GetAreaTrigger(itr->second); used && used->Loc.GetMapId() == mapId)
            safeLocId = used->ID;
    if (!safeLocId)
        if (AreaTriggerTeleport const* first = sObjectMgr->GetMapEntranceTrigger(mapId); first && first->Loc.GetMapId() == mapId)
            safeLocId = first->ID;
    WorldSafeLocsEntry const* loc = safeLocId ? sObjectMgr->GetWorldSafeLoc(safeLocId) : nullptr;
    if (!loc || loc->Loc.GetMapId() != mapId)
    {
        if (_entranceFailLogged.insert(key).second)
            TC_LOG_WARN("tortoisebots", "BotMgr: map {} (instance {}): no area trigger leads into it, so no entrance is set (a release there is held, relhold=no-entrance; retried at every think)", mapId, instanceId);
        return;
    }
    script->SetEntranceLocation(safeLocId);
    _entranceFailLogged.erase(key);
    TC_LOG_INFO("tortoisebots", "BotMgr: map {} (instance {}) got entrance world_safe_locs {} ({:.1f}, {:.1f}, {:.1f}): a release inside revives there",
        mapId, instanceId, safeLocId, loc->Loc.GetPositionX(), loc->Loc.GetPositionY(), loc->Loc.GetPositionZ());
}

uint32 BotMgr::InstanceEntrance(Player* player) const
{
    if (!player || !player->IsInWorld())
        return 0;
    Map* map = player->GetMap();
    InstanceMap* instanceMap = map ? map->ToInstanceMap() : nullptr;
    InstanceScript* script = instanceMap ? instanceMap->GetInstanceScript() : nullptr;
    return script ? script->GetEntranceLocation() : 0;
}

bool BotMgr::ReleaseHasEntrance(Player* bot) const
{
    if (!bot || !bot->IsInWorld())
        return true;
    Map* map = bot->GetMap();
    if (!map || !map->ToInstanceMap() || !map->IsDungeon())
        return true;                      // not inside a dungeon: the stock rules (a graveyard is where the core sends a ghost)
    // The stricter of the two entrance checks (P4's InstanceEntranceKnownCompat names a real world_safe_locs row, H4's was only non-zero): the core's own
    // lookup (RepopAtGraveyard, Player.cpp:5251) uses the row, so an id with no row would fall back to the zone's graveyard outside.
    uint32 const entrance = InstanceEntrance(bot);
    return entrance != 0 && sObjectMgr->GetWorldSafeLoc(entrance) != nullptr;
}

// N2/N3 (package N): the bounded wait. A dead commanded bot in a dungeon of its group that has lain for DeadBackstopMs releases itself through the core's own
// repop handler (the typed RepopRequest, MiscHandler.cpp:63-93, what the engine's release action sends), but only when the instance has its entrance (the
// release then revives it at the entrance INSIDE the instance; with none it would revive at a graveyard outside: held, retried). Not when a resurrection is
// pending (the priest is casting) or the bot is already a ghost. It does not depend on the engine's brain or on where the commander is.
void BotMgr::DeadBackstop(Bot& bot, Player* me)
{
    if (me->IsAlive())
    {
        bot.deadMs = 0;
        bot.deadEngineFailed = false;
        return;
    }
    bot.deadMs += ThinkIntervalMs;
    if (bot.deadMs < _deadBackstopMs)
        return;
    Map* map = me->GetMap();
    if (!map || !map->ToInstanceMap() || !map->IsDungeon() || !IsCommandedHere(bot, me))
        return;
    if (me->HasPlayerFlag(PLAYER_FLAGS_GHOST) || me->IsResurrectRequested() || me->IsBeingTeleported() || !me->GetSession() || !ReleaseHasEntrance(me))
        return;
    TC_LOG_INFO("tortoisebots", "BotMgr: {} has lain dead for {} s in map {} (instance {}) with nothing raising it: it releases (the bounded wait); it comes back at the entrance",
        me->GetName(), bot.deadMs / 1000, map->GetId(), me->GetInstanceId());
    WorldPackets::Misc::RepopRequest packet{ WorldPacket(CMSG_REPOP_REQUEST) };
    me->GetSession()->HandleRepopRequest(packet);
    bot.deadMs = 0;     // one try per bound
}

// What `.tb state` keeps between two reads: a bot that was dead and is alive again is `running-back` until it is within 20 yd of its commander (its
// leader without one); `.tb revive` marks `practice` until the next combat ends.
void BotMgr::TrackLife(Bot& bot, Player* me)
{
    bool const ghost = !me->IsAlive() && me->HasPlayerFlag(PLAYER_FLAGS_GHOST);
    if (!me->IsAlive())
    {
        if (!bot.wasDead)
        {
            bot.wasDead = true;
            ++bot.deaths;
            bot.rescuedThisDeath = false;
            bot.reviveHow = -1;
            bot.ghostRescue.Reset();
            bot.ghostYd = -1.0f;
            bot.wasGhost = false;
            bot.diedAt = time(nullptr);
            if (bot.free)
                TC_LOG_INFO("tortoisebots", "[W1] death bot={} level={}", me->GetName(), me->GetLevel());
        }
        // The core's release teleports the ghost to the graveyard: count it as the bot's first teleport of this death.
        if (ghost && !bot.wasGhost)
        {
            bot.wasGhost = true;
            ++bot.teleports;
        }
        // BG1 (D6): inside a battleground the W1 ghost rescue is off: the battleground module releases the ghost and queues it at
        // the area spirit healer (host/bg/TbBgQueue.cpp), so the stall clock is reset rather than left to fire on the way out. (The
        // slice's dead engine has no corpse-run action; a class slice that compiles ReviveFromCorpseAction is not touched here.)
        bool const inBattleground = me->GetMap() && me->GetMap()->IsBattleground();
        if (bot.free && !inBattleground)
            GhostRescue(bot, me);
        else if (bot.free)
        {
            bot.ghostRescue.Reset();
            bot.ghostYd = -1.0f;
        }
        bot.runBack = false;
        return;
    }
    Player* follow = bot.master.IsEmpty() ? nullptr : ObjectAccessor::FindPlayer(bot.master);
    if (!follow)
        if (Group* group = me->GetGroup())
            follow = ObjectAccessor::FindPlayer(group->GetLeaderGUID());
    if (follow == me)
        follow = nullptr;
    if (bot.wasDead)
    {
        bot.wasDead = false;
        bot.wasGhost = false;
        bot.runBack = follow && (follow->GetMap() != me->GetMap() || !me->IsWithinDist(follow, 30.0f));
        if (bot.free)
        {
            char const* how = bot.rescuedThisDeath ? "rescue" : bot.reviveHow == 1 ? "healer" : "reclaim";
            long secs = bot.diedAt ? long(time(nullptr) - bot.diedAt) : 0;
            TC_LOG_INFO("tortoisebots", "[W1] revive bot={} how={} secs={} yd={:.1f}", me->GetName(), how, secs,
                bot.ghostYd < 0.0f ? 0.0f : bot.ghostYd);
            bot.ghostRescue.Reset();
            bot.ghostYd = -1.0f;
        }
    }
    if (bot.runBack && (!follow || (follow->GetMap() == me->GetMap() && me->IsWithinDist(follow, 20.0f))))
        bot.runBack = false;
    if (bot.practice)
    {
        if (me->IsInCombat())
            bot.practiceSawCombat = true;
        else if (bot.practiceSawCombat)
        {
            bot.practice = false;
            bot.practiceSawCombat = false;
        }
    }
}

// W1 (spec D2): the ONE teleport of a free bot besides the core's release. A ghost that makes no headway toward its corpse or
// toward the spirit healer for five minutes is rescued by the core's own repop (Player::RepopAtGraveyard), once per death, with
// the reason in the log. Every read is in-memory (corpse position, the graveyard store); no pointer is kept between ticks.
void BotMgr::GhostRescue(Bot& bot, Player* me)
{
    if (!me->HasPlayerFlag(PLAYER_FLAGS_GHOST) || me->IsBeingTeleported() || !me->FindMap())
    {
        bot.ghostRescue.Reset();
        bot.ghostYd = -1.0f;
        return;
    }

    Corpse* corpse = me->GetCorpse();
    bool const corpseHere = corpse && corpse->GetMapId() == me->GetMapId();
    float corpseYd = corpseHere ? me->GetDistance(corpse) : -1.0f;
    bot.ghostYd = corpseYd;

    // The spirit healer stands at the graveyard the core would send this ghost to: the corpse's (the only reference a dead bot
    // always has), or the bot's own when the corpse is gone or on another map. Plain coordinates only: no legacy type enters the
    // host file, so the option-OFF build stays clean.
    uint32 const refMap = corpseHere ? corpse->GetMapId() : me->GetMapId();
    float const refX = corpseHere ? corpse->GetPositionX() : me->GetPositionX();
    float const refY = corpseHere ? corpse->GetPositionY() : me->GetPositionY();
    float const refZ = corpseHere ? corpse->GetPositionZ() : me->GetPositionZ();
    WorldSafeLocsEntry const* grave = ClosestGraveyardCompat(me, refMap, refX, refY, refZ, me->GetTeam());
    float healerYd = -1.0f;
    if (grave && SafeLocMapIdCompat(grave) == me->GetMapId())
        healerYd = me->GetDistance(SafeLocXCompat(grave), SafeLocYCompat(grave), SafeLocZCompat(grave));

    if (corpseYd < 0.0f && healerYd < 0.0f)
        return;
    if (corpseYd < 0.0f)
        corpseYd = healerYd;
    if (healerYd < 0.0f)
        healerYd = corpseYd;

    bot.ghostRescue.Observe(corpseYd, healerYd, ThinkIntervalMs);
    if (!bot.ghostRescue.Due() || bot.rescuedThisDeath)
        return;
    if (!RepopAtGraveyardCompat(me))
        return;
    bot.rescuedThisDeath = true;
    ++bot.rescues;
    ++bot.teleports;
    TC_LOG_INFO("tortoisebots", "[W1] rescue bot={} reason=ghost-stuck secs={} yd={:.1f}", me->GetName(),
        TortoiseBots::GHOST_RESCUE_STALL_MS / 1000, corpseYd);
}

void BotMgr::ConfirmCreations()
{
    for (auto itr = _creating.begin(); itr != _creating.end();)
    {
        PendingCreation& creation = **itr;
        if (!creation.commit.valid())
        {
            itr = _creating.erase(itr);
            continue;
        }
        if (creation.commit.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        {
            ++itr;
            continue;
        }

        Player* const player = creation.player.get();
        if (!creation.commit.get())
        {
            // The character side did not commit: the login side is never committed, the creation hooks never fire and the cache
            // never hears of it. Nothing of it is left behind.
            TC_LOG_ERROR("tortoisebots", "BotMgr: the character commit of {} on account {} failed; nothing was created and its login side was not committed",
                creation.name, creation.accountId);
            itr = _creating.erase(itr);
            continue;
        }

        LoginDatabase.CommitTransaction(creation.loginTransaction);
        TC_LOG_INFO("tortoisebots", "BotMgr: {} ({}) is in the characters database on account {}: its creation is complete",
            creation.name, player->GetGUID().ToString(), creation.accountId);
        sScriptMgr->OnPlayerCreate(player);
        sCharacterCache->AddCharacterCacheEntry(player->GetGUID(), creation.accountId, player->GetName(), player->GetNativeGender(), player->GetRace(),
            player->GetClass(), player->GetLevel(), false);
        itr = _creating.erase(itr);
    }
}

bool BotMgr::Budgetable(Bot const& bot) const
{
    // Only a pool bot with AI work to give is cuttable. A bot under a real
    // player (a live master, or an owner's alt) runs first and whole: a player
    // waits on its reactions. A bot with no in-world player yet (loading), a
    // teleport to acknowledge, or a logout pending only does session
    // bookkeeping, so delaying it buys nothing and can hold a login or a
    // shutdown.
    if (!bot.master.IsEmpty() || bot.owned)
        return false;
    Player* player = bot.session ? bot.session->GetPlayer() : nullptr;
    return player && player->IsInWorld() && !player->IsBeingTeleported() && !bot.session->LogoutRequested();
}

void BotMgr::UpdateTickBudget(uint32 diff)
{
    engine::TickBudgetConfig const config = engine::ReadTickBudgetConfig();
    if (!_budgetSynced ||
        config.poolUs != _budgetPoolCeilingUs ||
        config.combatUs != _budgetCombatCeilingUs ||
        config.targetMs != _budgetTargetMs)
    {
        _budgetTargetMs = config.targetMs;
        _budgetPoolCeilingUs = config.poolUs;
        _budgetCombatCeilingUs = config.combatUs;
        _adaptiveBudget = AdaptiveBudget(config.targetMs, config.poolUs, config.combatUs);
        _budgetSynced = true;
    }
    _budgetGateMs = config.gateMs;
    // The controller decides on the previous world tick and the share of it the
    // budgeted turns took; both are read from the pass before this one.
    _adaptiveBudget.Update(diff, _lastPoolTurnsMs);

    uint64 const poolUs = _adaptiveBudget.PoolUs();
    uint64 const combatUs = _adaptiveBudget.CombatUs();
    // 0 in either ceiling keeps its "no budget" meaning: one zero switches the
    // budget off rather than leaving half of it behind.
    _budgetUs = (poolUs == 0 || combatUs == 0) ? 0 : poolUs + combatUs;
}

BotMgr::FleetCounts BotMgr::GetFleetCounts() const
{
    FleetCounts counts;
    for (auto const& [guid, bot] : _bots)
    {
        ++counts.bots;
        counts.engines += bot.brain ? 1 : 0;
        counts.owned += bot.owned ? 1 : 0;
        counts.mastered += bot.master.IsEmpty() ? 0 : 1;
        counts.free += bot.free ? 1 : 0;
        counts.pool += bot.pool ? 1 : 0;
    }
    return counts;
}

void BotMgr::Update(uint32 diff)
{
    // S1-R2: the chat lines the hook queued since the last update, resolved and judged here, on the world thread, before any bot's
    // engine step reads its inbox.
    TortoiseBots::chatin::DrainArrivals();
#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1
    // #14: the ritual fill's own once-a-second pass (the objects real players use, the summons that are channeled, the held bots).
    tbhost::ritualfill::Tick(diff);
#endif

    ConfirmCreations();
    std::vector<ObjectGuid> finished;

    UpdateTickBudget(diff);

    auto updateOne = [this, diff, &finished](ObjectGuid const& guid, Bot& bot)
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
            return;
        }

        // S3: the flight watcher runs before the master-away gate: a bot knocked while its commander is away still flies and
        // lands with the core's fall damage (without a brain it still lands; only the engine's hold is skipped).
        Player* me = bot.session->GetPlayer();
        if (me && me->IsInWorld() && !me->IsBeingTeleported())
            kb::Update(me, bot.brain.get());

        // R10 (S2): a bot whose master is away does nothing (no Think, no engine) until it is back; the grace counts down meanwhile.
        if (bot.masterAway && !UpdateMasterAway(bot, diff))
            return;

        if (!me || !me->IsInWorld() || me->IsBeingTeleported())
            return;

        // A budgeted bot is visited every few ticks, so its own clocks count the
        // time since ITS last turn, not one world tick: a skipped visit must not
        // stretch its think cadence, its retries or the engine's delays.
        uint32 const turnNowMs = getMSTime();
        uint32 const sinceLastMs = bot.lastTurnMs ? getMSTimeDiff(bot.lastTurnMs, turnNowMs) : diff;
        bot.lastTurnMs = turnNowMs ? turnNowMs : 1;
        uint32 const aiElapsed = std::min(sinceLastMs, MaxAiElapsedMs);

        bot.followRetryMs = bot.followRetryMs > sinceLastMs ? bot.followRetryMs - sinceLastMs : 0;
        bot.catchUpCooldownMs = bot.catchUpCooldownMs > sinceLastMs ? bot.catchUpCooldownMs - sinceLastMs : 0;

        // H4: a teleport the host started (`.tb tele`) has finished: where it ended is the answer (`entered=` / `block=teleport-refused`).
        if (bot.landing.pending)
        {
            if (me->GetMapId() == bot.landing.mapId)
            {
                bot.entered = bot.landing.how;
                bot.block.clear();
                bot.blockText.clear();
            }
            else
            {
                bot.block = "teleport-refused";
                bot.blockText = Trinity::StringFormat("the teleport to map {} ended on map {} (the core refused or ported the bot back: see the server log)", bot.landing.mapId, me->GetMapId());
            }
            bot.landing.pending = false;
        }
        if (bot.entry.active)
            UpdateEntry(bot, me, diff);

        bot.thinkMs += sinceLastMs;
        if (bot.thinkMs >= ThinkIntervalMs)
        {
            bot.thinkMs = 0;
            TrackLife(bot, me);
            EnsureEntrance(bot, me);
            DeadBackstop(bot, me);
            Think(bot, me);
        }

        // The engine brain: every visit, with the time since this bot's own last
        // turn (PlayerbotAIBase::UpdateAI keeps its own delay): a bot the budget
        // skipped must not have its delays stretched by the ticks it missed.
        // World thread, from WorldScript::OnBeforeMapsUpdate, i.e. before the
        // maps update; the legacy engine takes no map lock and expects none
        // (SLICE1-R5-NOTES.md). Think may have dropped the brain or sent the bot
        // on a teleport. Not in the update in which its logout was requested or
        // its master was lost (R9): Think may have asked for the logout above,
        // and the brain is gone by then.
#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1
        // Every bot answers a duel, not only a free one: TB gives the duel strategy to any bot outside a battleground
        // (AiFactory.cpp:1489-1495), and the core's Player::duel is the only signal a socketless session gets.
        tbhost::errands::TickDuelResponse(me);
        tbhost::prof::TickErrands(me, diff);
        tbhost::fishing::TickIdle(me, diff, bot.free && _fishingEnabled);
        if (bot.free)
            tbhost::thievery::TickThievery(me, diff);
        bool const pauseEngineForErrand = bot.free && !me->IsInCombat() &&
            (tbhost::errands::HasActiveErrand(me) || tbhost::ah_market::HasActiveErrand(me) ||
             tbhost::guild_mail::HasActiveErrand(me) ||
             tbhost::guild_orders::HasActiveDisenchantErrand(me) ||
             tbhost::guild_orders::HasActiveEnchantErrand(me) ||
             tbhost::fishing::HasActiveIdleErrand(me) ||
             tbhost::thievery::HasActiveErrand(me) ||
             tbhost::upkeep::HasActiveUpkeepCast(me));
#else
        bool const pauseEngineForErrand = false;
#endif
        if (bot.brain && !bot.session->LogoutRequested() && !me->IsBeingTeleported() && !pauseEngineForErrand)
            engine::Tick(bot.brain.get(), aiElapsed);
#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1
        // The open-world PvP seek role: rolled at login (the first tick with a brain), re-rolled on the configured
        // interval, stripped as soon as the bot is no longer a free bot of the flagged share.
        tbhost::worldpvp::Tick(me);
        // A free bot answers an outstanding guild invite, keeps clear of the member limit and (when the
        // founding key is on) founds its own guild. Cheap while no invite stands and forming is off.
        if (_guildsEnabled && bot.free)
            tbhost::guildjoin::TickFreeBotGuild(me, _guildFormingEnabled);
        if (me->IsAlive() && !me->IsInCombat() && !me->isMoving() && !me->GetCurrentSpell(CURRENT_GENERIC_SPELL))
            tbhost::guild_orders::TickBotOrder(me);
        if (me->IsAlive() && !me->IsInCombat() && !me->isMoving())
            tbhost::guild_orders::TickGuildOrderQueues(me);
        tbhost::guild_orders::TickEnchantOrder(me);
        if (bot.free && _professionsEnabled)
        {
            if (tbhost::prof::AdvanceFreeGatherTimer(guid.GetCounter(), diff))
                tbhost::prof::TickFreeGather(me);
        }
        if (bot.free && _capitalErrandsEnabled)
            tbhost::errands::TickCapitalErrands(me, diff);
        if (bot.free)
            tbhost::ah_market::Tick(me, diff);
        if (bot.free && _guildMailEnabled)
        {
            tbhost::guild_orders::TickDisenchantService(me, diff);
            tbhost::guild_mail::TickGuildMail(me, diff);
        }
        if (bot.free && (_disenchantUpkeepEnabled || _enchantGearEnabled))
            tbhost::upkeep::TickProfessionUpkeep(me, diff, _disenchantUpkeepEnabled, _enchantGearEnabled);
#endif
    };

    // One scan splits the pass in two: bots that must not wait (under a real
    // player, loading, teleporting, logging out) are served first and whole,
    // the rest is the pool the budget may cut. A bot that changes side mid-pass
    // gets its one turn and is reclassified next time.
    _alwaysOrder.clear();
    _turnOrder.clear();
    _alwaysOrder.reserve(_bots.size());
    _turnOrder.reserve(_bots.size());
    for (auto const& [guid, bot] : _bots)
        (Budgetable(bot) ? _turnOrder : _alwaysOrder).push_back(guid);

    for (ObjectGuid const& guid : _alwaysOrder)
        if (auto it = _bots.find(guid); it != _bots.end())
            updateOne(guid, it->second);

    // Pass 2: the pool, in the order the last pass left. The budget is checked
    // before each bot, so a pass never starts new work over budget; whoever is
    // left is first next pass (RotateToFirst), so no bot waits two rounds.
    if (!_nextPoolTurn.IsEmpty())
        tickbudget::RotateToFirst(_turnOrder, _nextPoolTurn);

    bool const budgetActive = tickbudget::PassIsBudgeted(_budgetUs, _budgetGateMs, diff);
    auto const nowUs = []()
    {
        return static_cast<uint64>(std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    };
    uint64 const turnsStartUs = nowUs();
    uint32 processed = 0;
    _nextPoolTurn.Clear();
    for (ObjectGuid const& guid : _turnOrder)
    {
        if (budgetActive && nowUs() - turnsStartUs >= _budgetUs)
        {
            _nextPoolTurn = guid;
            break;
        }
        auto it = _bots.find(guid);
        if (it == _bots.end())
            continue;
        updateOne(guid, it->second);
        ++processed;
    }
    // The cuttable share of this tick, fed to the controller next pass.
    _lastPoolTurnsMs = static_cast<uint32>((nowUs() - turnsStartUs) / 1000);

    BudgetStats stats;
    stats.poolUs = static_cast<uint32>(_adaptiveBudget.PoolUs());
    stats.combatUs = static_cast<uint32>(_adaptiveBudget.CombatUs());
    stats.budgetUs = static_cast<uint32>(_budgetUs);
    stats.targetMs = _budgetTargetMs;
    stats.smoothedTickMs = _adaptiveBudget.SmoothedTickMs();
    stats.held = _adaptiveBudget.Held();
    stats.poolBots = static_cast<uint32>(_turnOrder.size());
    stats.processed = processed;
    stats.cut = !_nextPoolTurn.IsEmpty();
    _lastBudget = stats;

    for (ObjectGuid const& guid : finished)
    {
        TC_LOG_INFO("tortoisebots", "BotMgr: session of {} ended", guid.ToString());
        // Normally gone already (OnPlayerLogout, or the pending-logout check above); never leave one behind.
#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1
        tbhost::prof::ClearBotErrands(guid);
        tbhost::errands::ClearBotCapitalErrands(guid);
        tbhost::ah_market::ClearBotAhMarket(guid);
        tbhost::guild_mail::ClearBotGuildMail(guid);
        tbhost::guildjoin::ForgetBot(guid);
        tbhost::thievery::ForgetBot(guid.GetCounter());
#endif
        if (auto itr = _bots.find(guid); itr != _bots.end())
            DropBrain(itr->second);
        _bots.erase(guid);
    }

    TickQuestGroups(diff);
#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1
    // #24: the real-player table the pool-share rule reads, rebuilt once a second on this thread (the engine's own pick runs in the
    // same world-thread pass, so no lock is taken). One branch a tick while the switch is off.
    tbhost::sharemobs::Tick(diff);
#endif

    // The engine's cumulative action_counts.csv snapshot every five minutes of tick time. The dump itself
    // checks AiPlayerbot.ActionCountsLog, so the off path is one branch and an add per tick.
    _actionCountsElapsedMs += diff;
    if (_actionCountsElapsedMs >= 5u * 60u * 1000u)
    {
        _actionCountsElapsedMs = 0;
        engine::DumpActionCounts();
    }
}

void BotMgr::TickQuestGroups(uint32 diff)
{
    if (!_groupQuestsEnabled)
        return;

    _questGroupTimerMs += diff;
    if (_questGroupTimerMs < 10000)
        return;
    _questGroupTimerMs = 0;

    uint64 const nowMs = GameTime::GetGameTimeMS();

    // 1. Disband finished/broken quest groups.
    for (auto it = _questGroups.begin(); it != _questGroups.end();)
    {
        Group* group = nullptr;
        for (ObjectGuid const& mGuid : it->members)
        {
            if (Player* p = FindBot(mGuid))
            {
                if (Group* g = p->GetGroup(); g && g->GetGUID() == it->groupGuid)
                {
                    group = g;
                    break;
                }
            }
        }

        tbhost::group_quest::QuestGroupState st;
        st.questId = it->questId;
        st.formedAtMs = it->formedAtMs;
        ObjectGuid const leaderGuid = it->members.empty() ? ObjectGuid::Empty : it->members.front();
        if (group)
        {
            for (Group::MemberSlot const& slot : group->GetMemberSlots())
            {
                auto bIt = _bots.find(slot.guid);
                if (bIt == _bots.end())
                {
                    st.hasRealPlayer = true;
                    break;
                }
                if (!bIt->second.free || !bIt->second.master.IsEmpty())
                {
                    // The engine adopts the party leader as master for grouped bots: the party working, not lost.
                    if (!bIt->second.master.IsEmpty() && bIt->second.master == leaderGuid)
                    {
                        ++st.membersBoundToLeader;
                        if (Player* p = bIt->second.session ? bIt->second.session->GetPlayer() : nullptr)
                        {
                            if (p->GetQuestStatus(it->questId) == QUEST_STATUS_INCOMPLETE)
                                ++st.membersStillIncomplete;
                        }
                        continue;
                    }
                    st.anyMemberLostFree = true;
                    continue;
                }
                ++st.activeMemberCount;
                if (Player* p = bIt->second.session ? bIt->second.session->GetPlayer() : nullptr)
                {
                    if (p->GetQuestStatus(it->questId) == QUEST_STATUS_INCOMPLETE)
                        ++st.membersStillIncomplete;
                }
            }
        }

        char const* reason = nullptr;
        if (!group || tbhost::group_quest::ShouldDisbandQuestGroup(st, nowMs, &reason))
        {
            if (group && !st.hasRealPlayer)
                group->Disband(true);
            for (ObjectGuid const& mGuid : it->members)
            {
                auto bIt = _bots.find(mGuid);
                if (bIt == _bots.end())
                    continue;
                if (!bIt->second.master.IsEmpty() && bIt->second.master == leaderGuid && !st.hasRealPlayer)
                {
                    // The engine adopted the party leader while questing: hand the bot back to the free kit
                    // instead of stranding it master-bound. The AI pointer goes first so its master-strip
                    // does not fire on the next tick and undo the free strategies.
                    if (bIt->second.brain)
                        engine::BindMaster(bIt->second.brain.get(), nullptr);
                    bIt->second.master = ObjectGuid::Empty;
                    bIt->second.free = true;
                    bIt->second.useEngine = true;
                    if (bIt->second.brain)
                        engine::ReapplyStrategies(bIt->second.brain.get(), true);
                    continue;
                }
                if (bIt->second.free && bIt->second.brain)
                    engine::SetQuestGroupMember(bIt->second.brain.get(), false);
            }
            TC_LOG_INFO("tortoisebots", "[W3] group-quest disband quest={} reason={}",
                it->questId, reason ? reason : "gone");
            it = _questGroups.erase(it);
        }
        else
            ++it;
    }

    // 2. Collect free bots holding incomplete group/elite quests (in-memory only, no world scan).
    std::vector<tbhost::group_quest::BotQuestCandidate> candidates;
    for (auto const& [guid, bot] : _bots)
    {
        if (!bot.free || !bot.master.IsEmpty())
            continue;
        Player* me = bot.session ? bot.session->GetPlayer() : nullptr;
        if (!me || !me->IsInWorld() || !me->FindMap() || !me->IsAlive() || me->IsInCombat())
            continue;
        if (me->GetGroup() || me->GetGroupInvite())
            continue;

        tbhost::group_quest::BotQuestCandidate cand;
        cand.guidLow = guid.GetCounter();
        cand.mapId = me->GetMapId();
        cand.x = me->GetPositionX();
        cand.y = me->GetPositionY();
        cand.level = me->GetLevel();
        cand.alive = true;
        cand.inCombat = false;
        cand.inGroup = false;
        cand.team = uint32_t(me->GetTeam());

        for (uint16 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
        {
            uint32 const qid = me->GetQuestSlotQuestId(slot);
            if (!qid)
                continue;
            Quest const* q = sObjectMgr->GetQuestTemplate(qid);
            if (!q || !tbhost::group_quest::IsGroupOrEliteQuest(q->GetQuestInfoID(), q->GetSuggestedPlayers()))
                continue;
            if (me->GetQuestStatus(qid) == QUEST_STATUS_INCOMPLETE)
                cand.incompleteGroupQuests.push_back(qid);
        }
        if (!cand.incompleteGroupQuests.empty())
            candidates.push_back(std::move(cand));
    }

    auto const plans = tbhost::group_quest::PlanQuestGroups(candidates);
    for (auto const& plan : plans)
    {
        ObjectGuid const leaderGuid = ObjectGuid::Create<HighGuid::Player>(plan.leaderGuidLow);
        Player* leader = FindBot(leaderGuid);
        if (!leader)
            continue;
        std::vector<ObjectGuid> members{ leaderGuid };
        for (uint32 mLow : plan.memberGuidLows)
        {
            ObjectGuid const mGuid = ObjectGuid::Create<HighGuid::Player>(mLow);
            Player* member = FindBot(mGuid);
            if (!member)
                continue;
            if (JoinParty(member, leader).empty())
            {
                members.push_back(mGuid);
                if (auto bIt = _bots.find(mGuid); bIt != _bots.end() && bIt->second.brain)
                    engine::SetQuestGroupMember(bIt->second.brain.get(), true);
            }
        }
        if (Group* g = leader->GetGroup(); g && members.size() >= 2)
        {
            _questGroups.push_back({ g->GetGUID(), plan.questId, nowMs, std::move(members) });
            TC_LOG_INFO("tortoisebots", "[W3] group-quest form leader={} members={} quest={}",
                leader->GetName(), uint32(members.size()), plan.questId);
        }
    }
}

void BotMgr::Think(Bot& bot, Player* me)
{
    // W1: a free bot has no master and no host steering: its engine plays the world (grind, food, loot, roll, buff) and the
    // dead kit (release, corpse run, spirit healer). Think only builds the brain once and otherwise stays out of the way.
    if (bot.free)
    {
        FreeThink(bot, me);
        return;
    }
    // No master: the bot only does what the console tells it (WalkBy, AttackNearest).
    if (bot.master.IsEmpty())
        return;
    // H4: while the host walks the bot into an area trigger, nothing else steers it (the engine is down and is not built again until the entry ends).
    if (bot.entry.active)
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
        else if (sameMap && inInstance)
        {
            // D4 (slice 4): inside an instance the 100-yd distance catch-up is off. A bot that fell behind WALKS (the engine's own follow); nothing is put in `stuck`.
            wanted = false;
            bot.followBlocked = false;
        }
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
                    ++bot.catchups;
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
            // N2 (package N): a DEAD commanded bot keeps (or gets) its engine while its commander is on another map: the dead kit holds only the release and
            // the acceptance of a resurrection (no follow, no path), which are the lifecycle that must run so the body can come back. The bounded wait
            // (DeadBackstop) covers a brain that cannot be built. An alive bot inside an instance its party stands in keeps its engine too
            // (EngineKeptOnOtherMap): it follows the group's leader there, and the bots that entered while their commander stayed outside must go on.
            bool kept = false;
            if (!sameMap)
            {
                if (me->IsAlive())
                {
                    // A party stands beside the bot when one OTHER member is in the world, not between maps, on the bot's exact map (same instance): the
                    // group's leader is no proxy for that. A bot that leads its own group (a run's leader) still has its party in the instance and keeps
                    // its engine; a lone leader has nobody to follow and none (PartyMemberOnMap).
                    bool partyHere = false;
                    if (Group* group = me->GetGroup())
                        for (Group::MemberSlot const& slot : group->GetMemberSlots())
                        {
                            Player* member = ObjectAccessor::FindPlayer(slot.guid);
                            if (member && PartyMemberOnMap(slot.guid == me->GetGUID(), member->IsInWorld(), member->IsBeingTeleported(), me->GetMap() == member->GetMap()))
                            {
                                partyHere = true;
                                break;
                            }
                        }
                    kept = EngineKeptOnOtherMap(me->IsAlive(), inInstance, partyHere);
                }
                if (!me->IsAlive() && IsCommandedHere(bot, me))
                {
                    if (bot.useEngine && !bot.engineFailed && !bot.brain && !bot.deadEngineFailed)
                    {
                        std::string error;
                        bot.brain.reset(engine::Create(me, master, error));
                        if (!bot.brain)
                        {
                            bot.deadEngineFailed = true;       // once per death
                            TC_LOG_WARN("tortoisebots", "BotMgr: a dead engine for {} could not be built while its commander is away: {} (the bounded wait releases it)", me->GetName(), error);
                        }
                    }
                }
                else if (!kept)
                {
                    // An alive bot (or a bot that is not commanded) loses its engine here. Live (N4 boot): a bot raised by its engine while the commander was away
                    // kept the follow generator the engine had set and walked 60 yd through the first room: stop what the dropped engine started.
                    if (bot.brain && me->IsAlive())
                    {
                        if (!engine::InFlight(bot.brain.get()))
                        {
                            me->GetMotionMaster()->Clear();
                            me->StopMoving();
                        }
                    }
                    DropBrain(bot);
                }
            }
            if (changed)
                TC_LOG_INFO("tortoisebots", "BotMgr: {} is not put next to its master {} (map {}, instance {}): {}", me->GetName(), master->GetName(), masterMap, masterInstance, reason);
            // The engine kept for an instance party falls through to the build/keep below (a bot whose brain went with the worldport gets it back at once).
            if (!sameMap && !kept)
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
    // The bot kept its engine for its instance party but has none now (not marked, or a failed build): the spike does not act across maps either.
    if (!sameMap)
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

void BotMgr::FreeThink(Bot& bot, Player* me)
{
    if (bot.entry.active)
        return;
    autofood::TickTopUp(me);
    if (bot.useEngine && !bot.engineFailed && !bot.brain)
    {
        std::string error;
        bot.brain.reset(engine::Create(me, nullptr, error, true));
        if (!bot.brain)
        {
            bot.engineFailed = true;
            bot.engineNote = error;
            TC_LOG_ERROR("tortoisebots", "BotMgr: engine for free bot {} not started: {} (it stands where it is)", me->GetName(), error);
        }
    }
}

std::string BotMgr::SetFree(ObjectGuid characterGuid, bool on)
{
    auto itr = _bots.find(characterGuid);
    if (itr == _bots.end())
        return "that character is not a logged-in host bot";
    Bot& bot = itr->second;
    if (on)
    {
        if (!bot.master.IsEmpty())
            return "the bot has a master: `.tb ungroup` it first";
        Player* me = bot.session->GetPlayer();
        if (me && me->GetGroup())
            return "the bot is in a party: a free bot plays alone";
        if (bot.free)
            return std::string();
        bot.free = true;
        bot.useEngine = true;
        if (bot.brain)
            engine::ReapplyStrategies(bot.brain.get(), true);
        TC_LOG_INFO("tortoisebots", "BotMgr: {} is a free bot (grind, food, loot, roll, buff; no master)", me ? me->GetName() : characterGuid.ToString());
        return std::string();
    }
    if (!bot.free)
        return std::string();
    bot.free = false;
    bot.ghostRescue.Reset();
    bot.ghostYd = -1.0f;
    bot.rescuedThisDeath = false;
    if (bot.brain)
    {
        if (bot.master.IsEmpty())
            DropBrain(bot);                    // no master and no free kit: it stands, as any console bot without a master
        else
            engine::ReapplyStrategies(bot.brain.get(), false);
    }
    Player* me = bot.session->GetPlayer();
    TC_LOG_INFO("tortoisebots", "BotMgr: {} is no longer a free bot", me ? me->GetName() : characterGuid.ToString());
    return std::string();
}

void BotMgr::NoteKill(ObjectGuid characterGuid)
{
    auto itr = _bots.find(characterGuid);
    if (itr != _bots.end() && itr->second.free)
        ++itr->second.kills;
}

void BotMgr::NoteReviveKind(ObjectGuid characterGuid, int kind)
{
    auto itr = _bots.find(characterGuid);
    if (itr != _bots.end())
        itr->second.reviveHow = kind;
}

BotMgr::LifeInfo BotMgr::GetLifeInfo(ObjectGuid characterGuid) const
{
    LifeInfo info;
    auto itr = _bots.find(characterGuid);
    if (itr == _bots.end())
        return info;
    Bot const& bot = itr->second;
    info.free = bot.free;
    info.kills = bot.kills;
    info.deaths = bot.deaths;
    info.rescues = bot.rescues;
    info.teleports = bot.teleports;
    info.rescuedThisDeath = bot.rescuedThisDeath;
    info.reviveHow = bot.reviveHow;
    info.ghostYd = bot.ghostYd;
    return info;
}
}

// TbCompat_W1.h: the engine's revive paths (ReviveFromCorpseAction.cpp, ReviveFromCorpseAction's spirit healer leg) name how the
// bot came back, for the host's `[W1] revive` line and `.tb state corpserun=`.
void NoteReviveKindCompat(Player* bot, int kind)
{
    if (bot)
        TortoiseBots::BotMgr::Instance().NoteReviveKind(bot->GetGUID(), kind);
}
