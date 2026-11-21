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
#include "TbBotMgr.h"

#include "ai/playerbot/policy/world/PoolSchedulePolicy.h"

#include "Config.h"
#include "Log.h"
#include "ObjectGuid.h"
#include "Player.h"
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
    std::uint32_t accMs = 0;
    world::PoolScheduleSettings settings;
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
    }
    facts.free = facts.logged && BotMgr::Instance().IsFreeBot(guid);
    facts.hasMaster = facts.logged && !BotMgr::Instance().MasterOf(guid).IsEmpty();

    world::PoolSyncOutcome const outcome = world::PoolSyncBot(bot, facts, s.settings, now, [] { return rand32(); });
    if (outcome.wentOffline && Enabled())
        MarkLogout(track.name, UnixNow());   // with the key or the tables gone the schedule still drains, without a stamp
    if (outcome.boundSet)
        TC_LOG_INFO("tortoisebots", "TortoiseBots: pool bot {} took a master: it leaves the pool's count and is never logged out by the pool", track.name);
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
    return loggedIn;
}

} // namespace

void LoadConfig()
{
    State& s = Get();
    s.configured = true;
    s.settings.targetOnline = std::uint32_t(std::clamp<int32>(sConfigMgr->GetIntDefault("TortoiseBots.Pool.TargetOnline", 0), 0, 5000));
    s.settings.loginsPerMinute = std::uint32_t(std::clamp<int32>(sConfigMgr->GetIntDefault("TortoiseBots.Pool.LoginsPerMinute", 20), 0, 600));
    s.settings.sessionMinMs = MinutesMs(sConfigMgr->GetIntDefault("TortoiseBots.Pool.SessionMinutes.Min", 30));
    s.settings.sessionMaxMs = MinutesMs(sConfigMgr->GetIntDefault("TortoiseBots.Pool.SessionMinutes.Max", 90));
    if (s.settings.sessionMaxMs < s.settings.sessionMinMs)
        s.settings.sessionMaxMs = s.settings.sessionMinMs;
    s.settings.offlineMinMs = MinutesMs(sConfigMgr->GetIntDefault("TortoiseBots.Pool.OfflineMinutes.Min", 5));
    s.settings.offlineMaxMs = MinutesMs(sConfigMgr->GetIntDefault("TortoiseBots.Pool.OfflineMinutes.Max", 20));
    if (s.settings.offlineMaxMs < s.settings.offlineMinMs)
        s.settings.offlineMaxMs = s.settings.offlineMinMs;
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
    else
        s.warnedOff = false;
    s.accMs += diff;
    if (s.accMs < 1000)
        return false;
    s.accMs = 0;                          // one pass a second, whatever the tick's diff was
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

std::vector<std::string> Status()
{
    State& s = Get();
    std::vector<std::string> lines;
    std::uint32_t alliance = 0, horde = 0;
    std::uint32_t const counted = Counted(s, &alliance, &horde);
    std::uint32_t due = 0;
    char const* state = s.stopping ? "stopping" : (s.running ? "running" : "stopped");
    std::vector<world::PoolBotState> states;
    states.reserve(s.bots.size());
    std::int64_t const now = NowMs();
    for (auto const& [guid, bot] : s.bots)
    {
        states.push_back(bot.state);
        if (!bot.state.bound && bot.state.phase == world::PoolPhase::Offline && now >= bot.state.offlineUntilMs)
            ++due;
    }
    // D2: one session per account, so the pool cannot hold more online bots than the accounts its characters sit on.
    std::uint32_t const accounts = world::PoolAccountCount(states);
    std::string line = Trinity::StringFormat(
        "pool: state={} target={} online={} alliance={} horde={} size={} due={} logins_60s={} drain={} last_pass_us={}",
        state, s.settings.targetOnline, counted, alliance, horde, s.bots.size(), due,
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

std::vector<std::string> Status()
{
    return { "pool: state=off (the bot engine is not built in: TORTOISEBOTS_SLICE1 is off)" };
}

} // namespace PoolScheduler
} // namespace pool
} // namespace TortoiseBots

#endif
