// Pool bot spawn (owner's design, Oct 2026): the start level and the one-time gear kit of a fresh pool bot.
#include "host/pool/TbPoolSpawn.h"

#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1

#include "TbBotMgr.h"
#include "compat/TbCompat_CharStore.h"
#include "host/gear/TbGearGen.h"

#include "Config.h"
#include "Log.h"
#include "Player.h"
#include "Random.h"

#include "playerbot/PlayerbotAIConfig.h"
#include "runtime/GearSeedMixPolicy.h"
#include "runtime/PoolSpawnPolicy.h"

#include <cstdint>
#include <ctime>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace TortoiseBots
{
namespace poolspawn
{
namespace
{
char const* const kKeySeed = "TortoiseBots.Pool.GearOnCreate";
char const* const kTable = "tortoise_bots_player_flag";
char const* const kFlag = "pool-spawn";

// Same DDL as forever/sql/characters/2026_11_24_10_tb_player_flag.sql (the add-on hint's store): one CREATE IF NOT
// EXISTS, so a server whose database user may create tables needs no migration applied by hand.
char const* const kCreate =
    "CREATE TABLE IF NOT EXISTS `tortoise_bots_player_flag` ("
    "`guid` bigint unsigned NOT NULL,"
    "`flag` varchar(32) NOT NULL,"
    "`set_at` int unsigned NOT NULL DEFAULT '0',"
    "PRIMARY KEY (`guid`, `flag`)"
    ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4";

enum class SpawnState : uint8_t
{
    No = 1,   // not a fresh pool bot, or the spawn already happened
    Yes = 2   // this login is the bot's spawn
};

struct State
{
    std::unordered_map<uint64_t, SpawnState> state;   // this login's answer, per character
    std::unordered_set<uint64_t> done;                // spawned during this server run
    bool storeReady = false;
    bool storeTried = false;
    bool storeWarned = false;
};

State& Store()
{
    static State s;
    return s;
}

bool s_seedEnabled = true;

int64_t NowSeconds()
{
    return static_cast<int64_t>(std::time(nullptr));
}

void EnsureStore()
{
    State& s = Store();
    if (!s.storeTried)
    {
        s.storeTried = true;
        CharacterExecuteGuardedCompat(kCreate);
    }
    std::set<std::string> const columns = { "guid", "flag", "set_at" };
    s.storeReady = CharacterSchemaHasCompat(kTable, columns);
    if (!s.storeReady && !s.storeWarned)
    {
        s.storeWarned = true;
        TC_LOG_WARN("tortoisebots", "TortoiseBots: `{}` is missing or lacks columns; a pool bot's spawn is remembered for this server run only. Apply forever/sql/characters/2026_11_24_10_tb_player_flag.sql.", kTable);
    }
}

bool StoredSpawn(uint64_t key)
{
    State& s = Store();
    if (!s.storeReady)
        return false;
    QueryResult const result = CharacterQueryGuardedCompat(
        "SELECT `guid` FROM `tortoise_bots_player_flag` WHERE `guid` = " + std::to_string(key)
        + " AND `flag` = '" + kFlag + "'");
    return static_cast<bool>(result);
}

void MarkSpawned(uint64_t key)
{
    State& s = Store();
    if (s.storeReady)
        CharacterExecuteGuardedCompat(
            "INSERT IGNORE INTO `tortoise_bots_player_flag` (`guid`, `flag`, `set_at`) VALUES ("
            + std::to_string(key) + ", '" + kFlag + "', " + std::to_string(NowSeconds()) + ")");
    s.done.insert(key);
}

SpawnState Resolve(Player* bot)
{
    if (!bot)
        return SpawnState::No;

    uint64_t const key = bot->GetGUID().GetCounter();
    State& s = Store();
    if (s.done.count(key))
        return SpawnState::No;
    auto const it = s.state.find(key);
    if (it != s.state.end())
        return it->second;

    ObjectGuid const guid = bot->GetGUID();
    bool const pool = BotMgr::Instance().IsPoolBot(guid);
    bool const hasMaster = !BotMgr::Instance().MasterOf(guid).IsEmpty();
    bool const fresh = NeedsPoolSpawn(bot->GetTotalPlayedTime(), StoredSpawn(key));
    SpawnState const state = PoolSpawnSubject(pool, hasMaster) && fresh ? SpawnState::Yes : SpawnState::No;
    s.state.emplace(key, state);
    return state;
}
} // namespace

void LoadConfig()
{
    s_seedEnabled = sConfigMgr->GetBoolDefault(kKeySeed, true);
}

void LoadAtStartup()
{
    if (BotMgr::Instance().Enabled())
        EnsureStore();
}

void ApplyStartLevel(Player* bot)
{
    if (Resolve(bot) != SpawnState::Yes)
        return;

    SpawnLevelRange const range = NormalizeSpawnLevelRange(
        sPlayerbotAIConfig.randomBotStartLevelMin, sPlayerbotAIConfig.randomBotStartLevelMax);
    uint32 const seedLevel = range.minLevel + urand(0, range.maxLevel - range.minLevel);
    uint32 const level = bot->GetLevel();
    if (level >= seedLevel)
        return;

    bot->GiveLevel(seedLevel);
    TC_LOG_INFO("tortoisebots", "TortoiseBots: fresh pool bot {} spawned at level {} (start range {}-{})",
        bot->GetName(), seedLevel, range.minLevel, range.maxLevel);
}

void SeedSpawnKit(Player* bot)
{
    if (!bot || Resolve(bot) != SpawnState::Yes)
        return;

    uint64_t const key = bot->GetGUID().GetCounter();
    // One attempt per spawn, whatever the outcome: a bot that finds no legal item, or spawns below level 5, keeps
    // what it has and progresses on drops from there.
    Store().state[key] = SpawnState::No;

    uint32 const level = bot->GetLevel();
    if (!s_seedEnabled)
    {
        TC_LOG_INFO("tortoisebots", "TortoiseBots: spawn kit for {} is off ({} = 0); it keeps the starter outfit",
            bot->GetName(), kKeySeed);
    }
    else if (!SpawnSeedReady(level))
    {
        TC_LOG_INFO("tortoisebots", "TortoiseBots: pool bot {} spawned at level {}: below level {}, it keeps the starter outfit for good",
            bot->GetName(), level, kSpawnSeedMinLevel);
    }
    else
    {
        gear::GearSetOutcome const outcome = gear::SeedGearForTarget(bot, level);
        if (outcome.ok)
            TC_LOG_INFO("tortoisebots", "TortoiseBots: spawned pool bot {} at level {} with {} kit piece(s) ({} slot(s) left empty)",
                bot->GetName(), level, outcome.equipped, outcome.failed);
        else
            TC_LOG_WARN("tortoisebots", "TortoiseBots: spawn kit for pool bot {} at level {} was refused ({}: {})",
                bot->GetName(), level, outcome.errorCode, outcome.errorDetail);
    }

    MarkSpawned(key);
}

void Forget(ObjectGuid guid)
{
    Store().state.erase(guid.GetCounter());
}
} // namespace poolspawn
} // namespace TortoiseBots

#endif
