// W0 (FOREVER-W0-W4-SPEC.md, round W0-B): the pool scheduler, see host/pool/TbPoolScheduler.h. World thread only.
//
// The clock: a wall-clock base taken at the first call plus the process's steady clock, so the 60 s login window and a session's
// end are exact to the millisecond while a clock step of the host does not move them. `tb_pool_character`'s stamps are unix
// seconds, as the registry's `created` is.
#include "host/pool/TbPoolScheduler.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef TORTOISEBOTS_SLICE1

#include "host/pool/TbPoolRegistry.h"
#include "host/pool/TbPoolReset.h"
#include "TbBotMgr.h"

#include "ai/playerbot/policy/world/PoolSchedulePolicy.h"

#include "Config.h"
#include "CharacterCache.h"
#include "Log.h"
#include "ObjectGuid.h"
#include "Player.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "Random.h"
#include "StringFormat.h"
#include "World.h"

namespace TortoiseBots
{
namespace pool
{
namespace PoolScheduler
{
namespace
{

std::int64_t NowMs()
{
    static std::chrono::steady_clock::time_point const started = std::chrono::steady_clock::now();
    static std::int64_t const base = std::int64_t(std::time(nullptr)) * 1000;
    return base + std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
}

std::int64_t UnixNow()
{
    return std::int64_t(std::time(nullptr));
}

ObjectGuid PlayerGuidOf(std::uint64_t guid)
{
    return ObjectGuid::Create<HighGuid::Player>(std::uint32_t(guid));
}

// What the scheduler knows of one pool character between passes. The registry is the source of truth for the pool's members and
// their stamps; `state` is the pass's working state (the policy's own type), `name` the registry key the stamps go by.
struct Tracked
{
    world::PoolBotState state;
    std::string name;
};

struct State
{
    bool configured = false;
    bool running = false;
    bool stopping = false;
    bool warnedOff = false;
    bool warnedReset = false;
    std::uint32_t accMs = 0;
    bool ladderConfigTried = false;
    world::PoolScheduleSettings settings;
    std::uint32_t levelLadderLogMinutes = 5;   // host-only: the cadence of the per-band log line
    std::int64_t nextLadderBandLogMs = 0;
    std::int64_t nextLadderPassLogMs = 0;
    world::PoolFaction nextFaction = world::PoolFaction::Alliance;
    std::unordered_map<std::uint64_t, Tracked> bots;
    std::vector<std::int64_t> logins;     // when the scheduler started a login (the 60 s window)
    std::vector<std::int64_t> logouts;    // when it asked for a logout (the 5 per second drain)
    std::uint32_t lastPassUs = 0;
};

State& Get()
{
    static State state;
    return state;
}

std::int64_t MinutesMs(std::int32_t minutes)
{
    std::int32_t const capped = std::clamp<std::int32_t>(minutes, 0, 24 * 60);
    return std::int64_t(capped) * world::kPoolMinuteMs;
}

char const* FactionName(world::PoolFaction faction)
{
    return faction == world::PoolFaction::Alliance ? "alliance" : "horde";
}

// The registry's ready rows are the pool. A row without a character (guid 0) is not the scheduler's; a row that is gone keeps its
// track while the bot is still in the world (the drain has to reach it) and drops it once the bot is offline.
void Refresh(State& s, std::int64_t now)
{
    std::map<std::uint64_t, Character const*> rows;
    for (Character const& row : Characters())
        if (row.guid)
            rows[row.guid] = &row;
    for (auto itr = s.bots.begin(); itr != s.bots.end();)
    {
        if (world::PoolTrackKept(itr->second.state, rows.count(itr->first) != 0))
            ++itr;
        else
            itr = s.bots.erase(itr);
    }
    for (auto const& [guid, row] : rows)
    {
        auto const found = s.bots.find(guid);
        if (found == s.bots.end())
        {
            Tracked bot;
            bot.name = row->name;
            world::PoolBotState& state = bot.state;
            state.guid = guid;
            state.accountId = row->accountId;
            state.faction = world::PoolFactionOfRace(row->race);
            state.lastLogoutMs = std::int64_t(row->lastLogout) * 1000;
            if (row->lastLogin > row->lastLogout)
                state.offlineUntilMs = now + s.settings.offlineMinMs;                // it logged in after its last logout: it came down with the server
            else if (row->lastLogout > 0)
                state.offlineUntilMs = state.lastLogoutMs + s.settings.offlineMinMs; // the rolled length was not stored: at least the minimum
            s.bots.emplace(guid, std::move(bot));
        }
        else
        {
            found->second.name = row->name;
            found->second.state.accountId = row->accountId;
            found->second.state.faction = world::PoolFactionOfRace(row->race);
        }
    }
}

// The accounts a bind leaves the pool: the policy's cap over the tracks as they stand. Asked only when a bot gets a master (the
// one moment the answer changes), so the pass allocates nothing for it.
std::uint32_t UsableAccounts(State const& s)
{
    std::vector<world::PoolBotState> states;
    states.reserve(s.bots.size());
    for (auto const& tracked : s.bots)
        states.push_back(tracked.second.state);
    return world::PoolAccountCount(states);
}

// The bot manager's answer for one track, turned into the pass's state by the policy (the phase, `bound`, the session clocks):
// the host only gathers the facts and writes the stamp of a logout. A bound bot logged out by the master's grace comes back to
// the pool's count (the policy clears `bound` when it leaves the world).
void Sync(State& s, Tracked& track, std::int64_t now)
{
    world::PoolBotState& bot = track.state;
    ObjectGuid const guid = PlayerGuidOf(bot.guid);
    world::PoolBotFacts facts;
    facts.logged = BotMgr::Instance().IsPoolBot(guid);
    Player* const player = facts.logged ? BotMgr::Instance().FindBot(guid) : nullptr;
    facts.inWorld = player != nullptr;
    if (player)
    {
        facts.alive = player->IsAlive();
        facts.inCombat = player->IsInCombat();
        facts.inBg = player->InBattleground();   // BG: hold the session through a match, not a queue entry
    }
    // The ladder's input: the world's answer while the bot is in it, the character cache (kept on level-up) while it is not.
    facts.level = player ? std::uint32_t(player->GetLevel()) : std::uint32_t(sCharacterCache->GetCharacterLevelByGuid(guid));
    facts.free = facts.logged && BotMgr::Instance().IsFreeBot(guid);
    facts.hasMaster = facts.logged && !BotMgr::Instance().MasterOf(guid).IsEmpty();

    world::PoolSyncOutcome const outcome = world::PoolSyncBot(bot, facts, s.settings, now, [] { return rand32(); });
    if (outcome.wentOffline && Enabled())
        MarkLogout(track.name, UnixNow());   // with the key or the tables gone the schedule still drains, without a stamp
    if (outcome.boundSet)
    {
        // The bound bot keeps its account's session, so the pool can only refill the slot on another account. Say it when there
        // is none left: the count then stays short for as long as the binding lasts, and `.tb pool status` shows the cap.
        std::uint32_t const usable = UsableAccounts(s);
        if (s.settings.targetOnline > usable)
            TC_LOG_INFO("tortoisebots", "TortoiseBots: pool bot {} took a master: it leaves the pool's count and is never logged out by the pool; {} account(s) left for a target of {}, so its slot stays empty while it is bound",
                track.name, usable, s.settings.targetOnline);
        else
            TC_LOG_INFO("tortoisebots", "TortoiseBots: pool bot {} took a master: it leaves the pool's count and is never logged out by the pool", track.name);
    }
    if (outcome.boundCleared)
        TC_LOG_INFO("tortoisebots", "TortoiseBots: pool bot {} is free again: back in the pool's count with a fresh session", track.name);
}

void Trim(std::vector<std::int64_t>& times, std::int64_t now, std::int64_t windowMs)
{
    times.erase(std::remove_if(times.begin(), times.end(), [&](std::int64_t at) { return now - at >= windowMs; }), times.end());
}

std::string SessionWord(std::int64_t startedMs, std::int64_t nowMs)
{
    if (!startedMs || nowMs < startedMs)
        return std::string();
    return std::to_string((nowMs - startedMs) / world::kPoolMinuteMs) + " min";
}

std::vector<world::PoolBotState> StatesOf(State const& s)
{
    // D2: the account is busy when the world's own session is on it or any bot of the module (pool or not, loading or in the
    // world) sits on it; the pool's other characters are in `states` itself (the policy folds them in). The bot manager's
    // accounts are asked for once, not once per track.
    std::vector<std::uint32_t> const botAccounts = BotMgr::Instance().BotAccounts();
    std::vector<world::PoolBotState> states;
    states.reserve(s.bots.size());
    for (auto const& [guid, bot] : s.bots)
    {
        world::PoolBotState state = bot.state;
        state.accountOnline = state.accountId
            && (sWorld->FindSession(state.accountId) != nullptr
                || std::binary_search(botAccounts.begin(), botAccounts.end(), state.accountId));
        states.push_back(state);
    }
    return states;
}

std::uint32_t Counted(State const& s, std::uint32_t* alliance = nullptr, std::uint32_t* horde = nullptr)
{
    std::uint32_t counted = 0, a = 0, h = 0;
    for (auto const& [guid, bot] : s.bots)
    {
        if (bot.state.bound || bot.state.phase == world::PoolPhase::Offline)
            continue;
        ++counted;
        (bot.state.faction == world::PoolFaction::Alliance ? a : h) += 1;
    }
    if (alliance)
        *alliance = a;
    if (horde)
        *horde = h;
    return counted;
}

std::uint32_t Draining(State const& s)
{
    std::uint32_t count = 0;
    for (auto const& [guid, bot] : s.bots)
        if (!bot.state.bound && (bot.state.phase == world::PoolPhase::Online || bot.state.phase == world::PoolPhase::Loading || bot.state.phase == world::PoolPhase::Leaving))
            ++count;
    return count;
}

// The ladder's two lines: a pass line a minute while the pool is short (so a live check can see why nothing logs in), and the
// online/target(pool) count per band every LevelLadderLogMinutes. Both read the tracks once per line, never per login.
void LadderLog(State& s, world::PoolPass const& plan, std::int64_t now)
{
    if (!s.settings.levelLadder)
        return;
    std::uint32_t const bands = world::PoolLadderBandCount(s.settings.maxLevel, s.settings.levelLadderBandSize);
    if (plan.counted < s.settings.targetOnline && now >= s.nextLadderPassLogMs)
    {
        s.nextLadderPassLogMs = now + world::kPoolMinuteMs;
        TC_LOG_INFO("tortoisebots", "TortoiseBots: level ladder pass: online {}/{}, added {}, offline eligible {}", plan.counted,
            s.settings.targetOnline, std::uint32_t(plan.logins.size()), plan.eligible);
    }
    if (now < s.nextLadderBandLogMs)
        return;
    s.nextLadderBandLogMs = now + std::int64_t(std::max<std::uint32_t>(1, s.levelLadderLogMinutes)) * world::kPoolMinuteMs;
    std::vector<std::uint32_t> online(bands, 0), pool(bands, 0);
    std::uint32_t reserve = 0, highest = 0;
    for (auto const& [guid, track] : s.bots)
    {
        world::PoolBotState const& bot = track.state;
        std::uint32_t const band = world::PoolLadderBandOf(bot.level, s.settings.maxLevel, s.settings.levelLadderBandSize);
        ++pool[band];
        highest = std::max(highest, std::uint32_t(bot.level));
        if (!bot.bound && bot.phase != world::PoolPhase::Offline)
            ++online[band];
        else
            ++reserve;
    }
    std::string line;
    for (std::uint32_t b = 0; b < bands; ++b)
    {
        if (b)
            line += ' ';
        line += world::PoolLadderBandName(b, s.settings.maxLevel, s.settings.levelLadderBandSize) + ":" + std::to_string(online[b]) + "/"
            + std::to_string(world::PoolLadderBandTarget(b, s.settings.targetOnline, s.settings.maxLevel,
                s.settings.levelLadderBandSize, s.settings.levelLadderMaxLevelSharePct))
            + "(" + std::to_string(pool[b]) + ")";
    }
    TC_LOG_INFO("tortoisebots", "TortoiseBots: level ladder online/target(pool) per band {} | reserve {}, highest level {}", line,
        reserve, highest);
}

// The ladder's four knobs are the engine config's (AiPlayerbot.LevelLadder*), and the engine reads aiplayerbot.conf lazily on
// its first brain: read them on every pass, not only at LoadConfig, so a key set in that file applies without a pool restart.
void ApplyLadderConfig(State& s)
{
    s.settings.levelLadder = sPlayerbotAIConfig.levelLadder;
    s.settings.levelLadderBandSize = sPlayerbotAIConfig.levelLadderBandSize;
    s.settings.levelLadderMaxLevelSharePct = std::min<std::uint32_t>(100, sPlayerbotAIConfig.levelLadderMaxLevelSharePct);
    s.levelLadderLogMinutes = std::max<std::uint32_t>(1, sPlayerbotAIConfig.levelLadderLogMinutes);
}

// One pass: the states in, the policy's plan out, the core's Login/Logout and the registry's stamps. Returns true when a login
// was started.
bool Pass(State& s)
{
    std::int64_t const now = NowMs();
    if (Enabled())
        Refresh(s, now);                  // a disabled pool keeps its tracks: the drain needs them, the registry does not answer
    for (auto& [guid, bot] : s.bots)
        Sync(s, bot, now);

    std::vector<world::PoolBotState> const states = StatesOf(s);
    world::PoolPassInput in;
    in.bots = &states;
    in.settings = s.settings;
    in.nowMs = now;
    in.stopping = s.stopping;
    in.shuttingDown = sWorld->IsShuttingDown();   // a pending shutdown plans no logins; the logouts still run
    in.nextFaction = s.nextFaction;
    in.recentLoginsMs = &s.logins;
    in.recentLogoutsMs = &s.logouts;
    auto const started = std::chrono::steady_clock::now();
    world::PoolPass const plan = world::PlanPoolPass(in);
    s.lastPassUs = std::uint32_t(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started).count());
    s.nextFaction = plan.nextFaction;

    bool loggedIn = false;
    for (std::uint64_t const guid : plan.logins)
    {
        auto const itr = s.bots.find(guid);
        if (itr == s.bots.end())
            continue;
        Tracked& track = itr->second;
        world::PoolBotState& bot = track.state;
        std::string const error = BotMgr::Instance().Login(PlayerGuidOf(guid), ObjectGuid::Empty, true, true);
        if (!error.empty())
        {
            // The world says no (already in the world by hand, the account is loading, the session limit): one line a minute.
            TC_LOG_WARN("tortoisebots", "TortoiseBots: pool login of {} refused: {}", track.name, error);
            bot.offlineUntilMs = now + world::kPoolMinuteMs;
            continue;
        }
        bot.phase = world::PoolPhase::Loading;
        world::PoolStartSession(bot, s.settings, now, rand32());
        s.logins.push_back(now);
        MarkLogin(track.name, UnixNow());
        if (s.settings.levelLadder)
            TC_LOG_INFO("tortoisebots", "TortoiseBots: pool login {} ({}, level {}, band {})", track.name, FactionName(bot.faction),
                std::uint32_t(bot.level),
                world::PoolLadderBandName(world::PoolLadderBandOf(bot.level, s.settings.maxLevel, s.settings.levelLadderBandSize),
                    s.settings.maxLevel, s.settings.levelLadderBandSize));
        else
            TC_LOG_INFO("tortoisebots", "TortoiseBots: pool login {} ({})", track.name, FactionName(bot.faction));
        loggedIn = true;
    }

    for (std::uint64_t const guid : plan.logouts)
    {
        auto const itr = s.bots.find(guid);
        if (itr == s.bots.end())
            continue;
        Tracked& track = itr->second;
        world::PoolBotState& bot = track.state;
        bool const sessionEnded = bot.sessionEndsMs && now >= bot.sessionEndsMs;
        char const* const reason = s.stopping ? "pool stop" : (sessionEnded ? "pool session" : "pool target");
        char const* const word = s.stopping ? "stop" : (sessionEnded ? "session" : "target");
        if (!BotMgr::Instance().Logout(PlayerGuidOf(guid), reason))
            continue;                     // already gone: the next pass takes it offline and stamps it
        bool const dead = sessionEnded && !bot.alive;
        std::string const length = SessionWord(bot.sessionStartMs, now);
        bot.phase = world::PoolPhase::Leaving;
        s.logouts.push_back(now);
        if (dead)
            TC_LOG_INFO("tortoisebots", "TortoiseBots: pool logout {}: reason={}, session {}, dead (the corpse run had its 10 min)", track.name, word, length);
        else if (!length.empty())
            TC_LOG_INFO("tortoisebots", "TortoiseBots: pool logout {}: reason={}, session {}", track.name, word, length);
        else
            TC_LOG_INFO("tortoisebots", "TortoiseBots: pool logout {}: reason={}", track.name, word);
    }

    Trim(s.logins, now, world::kPoolLoginWindowMs);
    Trim(s.logouts, now, world::kPoolLogoutWindowMs);

    if (s.stopping && !Draining(s))
    {
        s.stopping = false;
        TC_LOG_INFO("tortoisebots", "TortoiseBots: the pool stopped: no pool bot is left in the world");
    }
    LadderLog(s, plan, now);
    return loggedIn;
}

} // namespace

void LoadConfig()
{
    State& s = Get();
    s.configured = true;
    s.settings.targetOnline = std::uint32_t(std::clamp<int32>(sConfigMgr->GetIntDefault("TortoiseBots.Pool.TargetOnline", 0), 0, std::int32_t(world::kPoolTargetMax)));
    s.settings.populationLow = std::uint32_t(std::clamp<int32>(sConfigMgr->GetIntDefault("TortoiseBots.Pool.PopulationLow", 10), 0, std::int32_t(world::kPoolTargetMax)));
    s.settings.populationHigh = std::uint32_t(std::clamp<int32>(sConfigMgr->GetIntDefault("TortoiseBots.Pool.PopulationHigh", 50), 0, std::int32_t(world::kPoolTargetMax)));
    s.settings.loginsPerMinute = std::uint32_t(std::clamp<int32>(sConfigMgr->GetIntDefault("TortoiseBots.Pool.LoginsPerMinute", 20), 0, 600));
    s.settings.sessionMinMs = MinutesMs(sConfigMgr->GetIntDefault("TortoiseBots.Pool.SessionMinutes.Min", 30));
    s.settings.sessionMaxMs = MinutesMs(sConfigMgr->GetIntDefault("TortoiseBots.Pool.SessionMinutes.Max", 90));
    if (s.settings.sessionMaxMs < s.settings.sessionMinMs)
        s.settings.sessionMaxMs = s.settings.sessionMinMs;
    s.settings.offlineMinMs = MinutesMs(sConfigMgr->GetIntDefault("TortoiseBots.Pool.OfflineMinutes.Min", 5));
    s.settings.offlineMaxMs = MinutesMs(sConfigMgr->GetIntDefault("TortoiseBots.Pool.OfflineMinutes.Max", 20));
    if (s.settings.offlineMaxMs < s.settings.offlineMinMs)
        s.settings.offlineMaxMs = s.settings.offlineMinMs;
    // The ladder's band math is built on the server's own level cap, never a hard-coded 60: a server with another cap splits
    // its levels the same way. Its four knobs are the engine's own (AiPlayerbot.LevelLadder*), already read on this build.
    s.settings.maxLevel = std::uint32_t(std::clamp<int32>(sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL), 2, 255));
    s.ladderConfigTried = false;
    ApplyLadderConfig(s);
    s.nextLadderBandLogMs = 0;
    s.nextLadderPassLogMs = 0;
}

bool Update(std::uint32_t diff)
{
    State& s = Get();
    if (!s.configured)
        LoadConfig();
    if (!s.running && !s.stopping)
        return false;
    if (!Enabled())
    {
        // The key went off (or a table vanished): no new logins, and the bots that are in the world are drained rather than left
        // behind with no one to end their sessions.
        if (!s.warnedOff)
        {
            s.warnedOff = true;
            TC_LOG_WARN("tortoisebots", "TortoiseBots: the pool is off (TortoiseBots.Pool.Enable = 0, or its tables are missing): {}",
                Draining(s) ? "draining the bots in the world" : "nothing is scheduled");
        }
        s.running = false;
        s.stopping = Draining(s) != 0;
    }
    else if (!Reset::PoolAvailable())
    {
        // A startup reset is running (or failed): no new logins, but the bots in the world are still drained.
        if (!s.warnedReset)
        {
            s.warnedReset = true;
            TC_LOG_WARN("tortoisebots", "TortoiseBots: the pool reset is in progress or failed: no new logins until the reset finishes or the server restarts; {}",
                Draining(s) ? "draining the bots in the world" : "nothing is scheduled");
        }
        s.running = false;
        s.stopping = Draining(s) != 0;
    }
    else
    {
        s.warnedOff = false;
        s.warnedReset = false;
    }
    s.accMs += diff;
    if (s.accMs < 1000)
        return false;
    s.accMs = 0;                          // one pass a second, whatever the tick's diff was
    // The engine reads aiplayerbot.conf lazily, on its first brain, and a pool with no brain in the world (EngineByDefault = 0)
    // may never build one: load it here, now that the world's stores are up, or the four ladder keys stay at the C++ defaults.
    // Once per config load only: a failed Initialize reads the whole file and the DB, so it must not run once a second.
    if (!s.ladderConfigTried)
    {
        s.ladderConfigTried = true;
        if (!engine::ConfigReady())
            (void)engine::EnsureConfig();
    }
    ApplyLadderConfig(s);
    return Pass(s);
}

SwitchResult Start()
{
    State& s = Get();
    if (!s.configured)
        LoadConfig();
    SwitchResult result;
    if (!Enabled())
    {
        result.message = "the pool is off (TortoiseBots.Pool.Enable = 0) or its tables are missing: see the server log";
        return result;
    }
    if (!Reset::PoolAvailable())
    {
        result.message = "the pool reset is in progress or failed: no new logins until it finishes or the server restarts";
        return result;
    }
    if (s.running)
    {
        std::uint32_t alliance = 0, horde = 0;
        std::uint32_t const counted = Counted(s, &alliance, &horde);
        result.ok = true;
        result.message = Trinity::StringFormat("the pool is already running (target {}, {} online: {} alliance, {} horde)",
            s.settings.targetOnline, counted, alliance, horde);
        return result;
    }
    s.running = true;
    s.stopping = false;
    s.accMs = 0;
    Refresh(s, NowMs());
    result.ok = true;
    result.message = Trinity::StringFormat("pool start: target {}, {} character(s) in the pool, logins {} per minute, sessions {}..{} min, offline {}..{} min",
        s.settings.targetOnline, s.bots.size(), s.settings.loginsPerMinute, s.settings.sessionMinMs / world::kPoolMinuteMs,
        s.settings.sessionMaxMs / world::kPoolMinuteMs, s.settings.offlineMinMs / world::kPoolMinuteMs, s.settings.offlineMaxMs / world::kPoolMinuteMs);
    TC_LOG_INFO("tortoisebots", "TortoiseBots: {}", result.message);
    return result;
}

SwitchResult Stop()
{
    State& s = Get();
    SwitchResult result;
    if (!s.running && !s.stopping)
    {
        result.message = "the pool is not running";
        return result;
    }
    if (s.stopping)
    {
        result.ok = true;
        result.message = Trinity::StringFormat("the pool is already stopping: {} pool bot(s) left", Draining(s));
        return result;
    }
    s.stopping = true;
    s.running = false;
    std::uint32_t const left = Draining(s);
    result.ok = true;
    result.message = Trinity::StringFormat("pool stop: {} pool bot(s) will be logged out, 5 per second", left);
    TC_LOG_INFO("tortoisebots", "TortoiseBots: {}", result.message);
    return result;
}

SwitchResult SetPopulation(std::string const& word)
{
    State& s = Get();
    if (!s.configured)
        LoadConfig();
    SwitchResult result;
    world::PoolPopulationTarget const resolved = world::PoolResolvePopulationTarget(word, s.settings.populationLow, s.settings.populationHigh);
    if (!resolved.ok)
    {
        result.message = resolved.error;
        return result;
    }
    std::uint32_t const previous = s.settings.targetOnline;
    s.settings.targetOnline = resolved.target;
    char const* then = s.stopping ? "the pool is stopping: it applies at the next start"
        : (!s.running ? "the pool is not running: run `.tb pool start`" : "the pool adds or logs out bots gradually");
    result.ok = true;
    result.message = Trinity::StringFormat("pool population target {} (was {}); {}", resolved.target, previous, then);
    TC_LOG_INFO("tortoisebots", "TortoiseBots: {}", result.message);
    return result;
}

std::vector<std::string> Status()
{
    State& s = Get();
    std::vector<std::string> lines;
    std::uint32_t alliance = 0, horde = 0;
    std::uint32_t const counted = Counted(s, &alliance, &horde);
    std::uint32_t due = 0;
    std::uint32_t inBg = 0;
    char const* state = s.stopping ? "stopping" : (s.running ? "running" : "stopped");
    std::vector<world::PoolBotState> states;
    states.reserve(s.bots.size());
    std::int64_t const now = NowMs();
    for (auto const& [guid, bot] : s.bots)
    {
        states.push_back(bot.state);
        if (bot.state.inBg)
            ++inBg;
        if (!bot.state.bound && bot.state.phase == world::PoolPhase::Offline && now >= bot.state.offlineUntilMs)
            ++due;
    }
    // D2: one session per account, so the pool cannot hold more online bots than the accounts its characters sit on.
    std::uint32_t const accounts = world::PoolAccountCount(states);
    std::string line = Trinity::StringFormat(
        "pool: state={} target={} online={} alliance={} horde={} size={} due={} in_bg={} logins_60s={} drain={} last_pass_us={}",
        state, s.settings.targetOnline, counted, alliance, horde, s.bots.size(), due, inBg,
        world::PoolRecentCount(s.logins, now, world::kPoolLoginWindowMs), Draining(s), s.lastPassUs);
    if (s.settings.targetOnline > accounts)
        line += Trinity::StringFormat(" capped by accounts: {}", accounts);
    lines.push_back(std::move(line));
    return lines;
}

} // namespace PoolScheduler
} // namespace pool
} // namespace TortoiseBots

#else

namespace TortoiseBots
{
namespace pool
{
namespace PoolScheduler
{

void LoadConfig() {}

bool Update(std::uint32_t /*diff*/)
{
    return false;
}

SwitchResult Start()
{
    SwitchResult result;
    result.message = "the bot engine is not built in (TORTOISEBOTS_SLICE1 is off): the pool neither starts nor schedules";
    return result;
}

SwitchResult Stop()
{
    SwitchResult result;
    result.message = "the bot engine is not built in (TORTOISEBOTS_SLICE1 is off): the pool is not running";
    return result;
}

SwitchResult SetPopulation(std::string const& /*word*/)
{
    SwitchResult result;
    result.message = "the bot engine is not built in (TORTOISEBOTS_SLICE1 is off): the pool has no target";
    return result;
}

std::vector<std::string> Status()
{
    return { "pool: state=off (the bot engine is not built in: TORTOISEBOTS_SLICE1 is off)" };
}

} // namespace PoolScheduler
} // namespace pool
} // namespace TortoiseBots

#endif
