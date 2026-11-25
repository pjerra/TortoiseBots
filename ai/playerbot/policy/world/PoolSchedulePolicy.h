#pragma once

// W0 (FOREVER-W0-W4-SPEC.md, round W0-B): the pool scheduler's decisions as pure code. The host side
// (forever/src/host/pool/TbPoolScheduler.cpp) turns the registry's rows and the bot manager's bots into one PoolBotState per
// pool character, asks PlanPoolPass once a second, and carries out the logins and logouts it names.
//
// The rules of the round, in the words of the spec's row:
//  - the online count is the pool's own: a bot that got a master (BindMaster clears its free flag) leaves the count and is never
//    logged out by the pool;
//  - at most LoginsPerMinute logins in any 60 s window, and at most one per pass (a pass is one second): the spec's "50 within
//    3 min" is a floor to reach, not a licence for twenty sessions in one tick;
//  - logins alternate factions; within the faction the bot that has been offline longest goes first (`last_logout`, never
//    logged out first);
//  - an account holds one session (D2): a bot that is Loading, Online or Leaving occupies its account for every other character
//    of it, and the host folds in the world's own sessions and the bot manager's other bots through `accountOnline`; the online
//    count is therefore capped by the number of accounts the pool sits on;
//  - a session ends after a length rolled uniformly in [SessionMinutes.Min, .Max] and comes back after one rolled uniformly in
//    [OfflineMinutes.Min, .Max]; both are decided when they start and never invented later;
//  - a bot whose session has ended logs out once; a living bot in combat waits until the fight ends, and one that is dead waits
//    until it is alive again or ten minutes have passed (W1's corpse run);
//  - a lowered target logs out the newest sessions first, and never a dead or fighting bot; `.tb pool stop` drains everything,
//    both at 5 logouts per second: a burst would put every session's teardown in one world tick;
//  - a pending server shutdown plans no logins (the logouts a session or a stop already asked for still run).
//
// Core-free (the test forever/tools/test_pool_schedule_policy.cpp compiles it on its own). The pass sorts candidate pointers; it
// allocates nothing per bot beyond those lists.

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace TortoiseBots
{
namespace world
{

inline constexpr std::int64_t kPoolMinuteMs = 60000;

// The login window of the spec (per minute) and the window a stop or a lowered target is drained in (per second).
inline constexpr std::int64_t kPoolLoginWindowMs = kPoolMinuteMs;
inline constexpr std::int64_t kPoolLogoutWindowMs = 1000;
inline constexpr std::uint32_t kPoolLogoutsPerSecond = 5;
inline constexpr std::uint32_t kPoolLoginsPerPass = 1;
// The config ceiling of the online target (TortoiseBots.Pool.TargetOnline clamps here) and of `.tb pool population`.
inline constexpr std::uint32_t kPoolTargetMax = 5000;

// A dead pool bot whose session has ended keeps its body this long before the pool logs it out anyway: W1's corpse run has the
// time it needs, and a bot that could not get back waits no longer.
inline constexpr std::int64_t kPoolDeadWaitMs = 10 * kPoolMinuteMs;

// BG (BG1, BATTLEGROUNDS-SPEC.md D5): a bot inside a battleground keeps its session; an ended session is deferred by this much at
// a time until the bot comes out, so the pool never pulls a participant out of a running match. A bot only *waiting* in a queue
// holds nothing: its entry leaves with the session, and the fill queues another. `.tb pool stop` skips a bot inside a match the
// same way; the battleground module leaves it at Bg.MaxMinutes (45 min) without the Deserter debuff, and the next stop pass drains
// it then.
inline constexpr std::int64_t kPoolBgHoldMs = kPoolMinuteMs;

enum class PoolFaction : std::uint8_t
{
    Alliance = 0,
    Horde = 1,
};

// What the pool knows of one of its characters this pass. `Offline` and `Online` are the two real states; `Loading` is a login
// the scheduler started and `Leaving` a logout it asked for: both still hold a slot and neither may be picked for the other
// action.
enum class PoolPhase : std::uint8_t
{
    Offline = 0,
    Loading = 1,
    Online = 2,
    Leaving = 3,
};

inline PoolFaction OtherFaction(PoolFaction faction)
{
    return faction == PoolFaction::Alliance ? PoolFaction::Horde : PoolFaction::Alliance;
}

// Races 1, 3, 4, 7 and 95 are the Alliance's; 96 and every other pool race are the Horde's.
inline PoolFaction PoolFactionOfRace(std::uint8_t race)
{
    switch (race)
    {
        case 1: case 3: case 4: case 7: case 95: return PoolFaction::Alliance;
        default: return PoolFaction::Horde;
    }
}

struct PoolBotState
{
    std::uint64_t guid = 0;
    std::uint32_t accountId = 0;      // 0 = unknown (a test bot): such a bot occupies no account
    PoolFaction faction = PoolFaction::Alliance;
    PoolPhase phase = PoolPhase::Offline;
    bool accountOnline = false;        // the world's session or another bot of the module on the account (the pool's own are in `bots`)
    bool bound = false;                // got a master: out of the count, never logged out, never logged in again
    bool alive = true;
    bool inCombat = false;             // a living bot in a fight is not logged out (stop is the exception)
    bool inBg = false;                 // BG: inside a match; its session is held (nothing logs it out)
    std::int64_t sessionStartMs = 0;
    std::int64_t sessionEndsMs = 0;    // 0 = not known (a session the scheduler did not start)
    std::int64_t lastLogoutMs = 0;     // when it last went offline, 0 = never
    std::int64_t offlineUntilMs = 0;   // lastLogoutMs + the rolled offline time
};

struct PoolScheduleSettings
{
    std::uint32_t targetOnline = 0;
    std::uint32_t loginsPerMinute = 20;
    std::uint32_t populationLow = 10;
    std::uint32_t populationHigh = 50;
    std::int64_t sessionMinMs = 30 * kPoolMinuteMs;
    std::int64_t sessionMaxMs = 90 * kPoolMinuteMs;
    std::int64_t offlineMinMs = 5 * kPoolMinuteMs;
    std::int64_t offlineMaxMs = 20 * kPoolMinuteMs;
};

struct PoolPassInput
{
    std::vector<PoolBotState> const* bots = nullptr;
    PoolScheduleSettings settings;
    std::int64_t nowMs = 0;
    bool stopping = false;             // `.tb pool stop`: everything goes, 5 per second
    bool shuttingDown = false;         // the core's shutdown countdown is running: no new login
    PoolFaction nextFaction = PoolFaction::Alliance;
    std::vector<std::int64_t> const* recentLoginsMs = nullptr;    // the logins this scheduler started
    std::vector<std::int64_t> const* recentLogoutsMs = nullptr;   // the logouts it asked for
};

struct PoolPass
{
    std::vector<std::uint64_t> logins;   // guids, in the order to log in (at most kPoolLoginsPerPass)
    std::vector<std::uint64_t> logouts;  // guids: a session's end first (oldest end first), then a lowered target's newest first
    PoolFaction nextFaction = PoolFaction::Alliance;
    std::uint32_t counted = 0;           // the pool's online count (Loading and Leaving hold their slot, a bound bot does not count)
    std::uint32_t alliance = 0;
    std::uint32_t horde = 0;
    std::uint32_t eligible = 0;          // offline bots due to come back, with a free account
};

// A uniform length in [minMs, maxMs] from a 32-bit roll. A reversed or empty range gives its lower end, never a value outside
// the configured one.
inline std::int64_t RollPoolLengthMs(std::int64_t minMs, std::int64_t maxMs, std::uint32_t roll)
{
    if (minMs < 0)
        minMs = 0;
    if (maxMs < minMs)
        std::swap(minMs, maxMs);
    std::uint64_t const span = std::uint64_t(maxMs - minMs) + 1;
    // The 32-bit roll scaled onto the span: 0 is the lower end and 0xffffffff the upper one (a modulo would never reach it).
    std::uint64_t const offset = (std::uint64_t(roll) * span) >> 32;
    return minMs + std::int64_t(offset < span ? offset : span - 1);
}

inline std::int64_t PoolSessionLengthMs(PoolScheduleSettings const& settings, std::uint32_t roll)
{
    return RollPoolLengthMs(settings.sessionMinMs, settings.sessionMaxMs, roll);
}

inline std::int64_t PoolOfflineLengthMs(PoolScheduleSettings const& settings, std::uint32_t roll)
{
    return RollPoolLengthMs(settings.offlineMinMs, settings.offlineMaxMs, roll);
}

// `.tb pool population <n|low|high>`: the runtime word to an online target. `low` and `high` are the configured presets; a
// number is used as-is. Anything else, an empty word or a number past the ceiling is refused with the words for the reply.
struct PoolPopulationTarget
{
    bool ok = false;
    std::uint32_t target = 0;
    std::string error;
};

inline PoolPopulationTarget PoolResolvePopulationTarget(std::string const& word, std::uint32_t low, std::uint32_t high)
{
    if (word == "low")
        return { true, low, {} };
    if (word == "high")
        return { true, high, {} };
    if (!word.empty() && std::all_of(word.begin(), word.end(), [](char c) { return c >= '0' && c <= '9'; }))
    {
        std::uint64_t value = 0;
        for (char c : word)
        {
            value = value * 10 + std::uint32_t(c - '0');
            if (value > kPoolTargetMax)
                break;
        }
        if (value <= kPoolTargetMax)
            return { true, std::uint32_t(value), {} };
        return { false, 0, "a population of 0 to 5000" };
    }
    return { false, 0, "usage: `.tb pool population <n|low|high>`" };
}

// D2: one session per account. A bot that is Loading, Online or Leaving occupies its account for every other character of it;
// `accountOnline` carries what these tracks cannot show (the world's own session and the module's other bots on the account).
inline std::vector<std::uint32_t> PoolOccupiedAccounts(std::vector<PoolBotState> const& bots)
{
    std::vector<std::uint32_t> accounts;
    accounts.reserve(bots.size());
    for (PoolBotState const& bot : bots)
        if (bot.accountId && (bot.accountOnline || bot.phase != PoolPhase::Offline))
            accounts.push_back(bot.accountId);
    std::sort(accounts.begin(), accounts.end());
    accounts.erase(std::unique(accounts.begin(), accounts.end()), accounts.end());
    return accounts;
}

inline bool PoolAccountBusy(PoolBotState const& bot, std::vector<std::uint32_t> const& occupiedAccounts)
{
    if (bot.accountOnline)
        return true;
    return bot.accountId && std::binary_search(occupiedAccounts.begin(), occupiedAccounts.end(), bot.accountId);
}

// The distinct accounts the pool's characters sit on: one session each caps the online count here. 0 (unknown) is no account.
// An account a bound bot holds is out of reach — the pool never logs that bot out, so its session lasts as long as the binding —
// and leaving it in would promise a target the pool cannot reach.
inline std::uint32_t PoolAccountCount(std::vector<PoolBotState> const& bots)
{
    std::vector<std::uint32_t> accounts;
    accounts.reserve(bots.size());
    for (PoolBotState const& bot : bots)
        if (bot.accountId)
            accounts.push_back(bot.accountId);
    std::sort(accounts.begin(), accounts.end());
    accounts.erase(std::unique(accounts.begin(), accounts.end()), accounts.end());
    for (PoolBotState const& bot : bots)
        if (bot.accountId && bot.bound)
            accounts.erase(std::remove(accounts.begin(), accounts.end(), bot.accountId), accounts.end());
    return std::uint32_t(accounts.size());
}

// The facts the host has of one track this pass: the bot manager's answer and, once it is in the world, the Player's life state.
struct PoolBotFacts
{
    bool logged = false;
    bool inWorld = false;
    bool free = false;
    bool hasMaster = false;
    bool alive = true;
    bool inCombat = false;
    bool inBg = false;                 // BG: Player::InBattleground() only; a queue entry does not hold a session
};

struct PoolSyncOutcome
{
    bool wentOffline = false;     // the row's `last_logout` is the caller's to write
    bool startedSession = false;  // a session began now (a login found, or one restarted after a binding ended)
    bool boundSet = false;
    bool boundCleared = false;
};

inline void PoolStartSession(PoolBotState& bot, PoolScheduleSettings const& settings, std::int64_t now, std::uint32_t roll)
{
    bot.sessionStartMs = now;
    bot.sessionEndsMs = now + PoolSessionLengthMs(settings, roll);
    bot.offlineUntilMs = 0;
    bot.alive = true;
    bot.inCombat = false;
}

// The session is over (ours, the master's grace, the owner's login, a shutdown): the bot comes back after its rolled offline
// time, and a binding the scheduler was holding ends with it (the master may have taken it far longer than a session).
inline void PoolGoOffline(PoolBotState& bot, PoolScheduleSettings const& settings, std::int64_t now, std::uint32_t roll)
{
    bot.phase = PoolPhase::Offline;
    bot.bound = false;
    bot.lastLogoutMs = now;
    bot.offlineUntilMs = now + PoolOfflineLengthMs(settings, roll);
    bot.sessionStartMs = 0;
    bot.sessionEndsMs = 0;
    bot.alive = true;
    bot.inCombat = false;
    bot.inBg = false;
}

// `bound` follows the binding: a master (BindMaster clears `free`) takes the bot out of the count, and `.tb free on` or a
// binding that ended (no master again, `.tb ungroup` and the commander's clears alike) puts it back with a fresh session, so a
// bot that spent hours bound does not end the moment it rejoins.
template <class Roll>
inline void PoolSyncMaster(PoolBotState& bot, PoolBotFacts const& facts, PoolScheduleSettings const& settings,
                           std::int64_t now, Roll&& roll, PoolSyncOutcome& out)
{
    bool const shouldBind = !facts.free && facts.hasMaster;
    if (shouldBind)
    {
        if (!bot.bound)
        {
            bot.bound = true;
            out.boundSet = true;
        }
        return;
    }
    if (bot.bound)
    {
        bot.bound = false;
        PoolStartSession(bot, settings, now, roll());
        out.boundCleared = true;
        out.startedSession = true;
    }
}

// One track between passes: the phase the manager's answer leads to, `bound` from the free flag on every switch to Online (a
// master bound while the bot was loading is seen there), and the session clocks. `roll` is called only when a length is needed.
template <class Roll>
inline PoolSyncOutcome PoolSyncBot(PoolBotState& bot, PoolBotFacts const& facts, PoolScheduleSettings const& settings,
                                   std::int64_t now, Roll&& roll)
{
    PoolSyncOutcome out;
    switch (bot.phase)
    {
        case PoolPhase::Offline:
            if (!facts.logged)
                break;
            if (facts.inWorld)
            {
                bot.phase = PoolPhase::Online;
                PoolStartSession(bot, settings, now, roll());   // a session found at start (a hand login): from now, never a guessed end
                out.startedSession = true;
            }
            else
                bot.phase = PoolPhase::Loading;
            break;
        case PoolPhase::Loading:
            if (!facts.logged)
            {
                PoolGoOffline(bot, settings, now, roll());
                out.wentOffline = true;
            }
            else if (facts.inWorld)
            {
                bot.phase = PoolPhase::Online;
                if (!bot.sessionEndsMs)
                {
                    PoolStartSession(bot, settings, now, roll());
                    out.startedSession = true;
                }
                PoolSyncMaster(bot, facts, settings, now, roll, out);
            }
            break;
        case PoolPhase::Online:
            if (!facts.logged)
            {
                PoolGoOffline(bot, settings, now, roll());
                out.wentOffline = true;
                break;
            }
            PoolSyncMaster(bot, facts, settings, now, roll, out);
            if (!bot.bound)
            {
                bot.alive = facts.alive;
                bot.inCombat = facts.inCombat;
                bot.inBg = facts.inBg;
                // BG: a session that ran out while the bot fights in a match is deferred, not spent.
                if (bot.inBg && bot.sessionEndsMs && now >= bot.sessionEndsMs)
                    bot.sessionEndsMs = now + kPoolBgHoldMs;
            }
            break;
        case PoolPhase::Leaving:
            if (!facts.logged)
            {
                PoolGoOffline(bot, settings, now, roll());
                out.wentOffline = true;
            }
            break;
    }
    return out;
}

// A row that went away takes its track with it only once the bot is offline: an online bot must stay countable (and drainable)
// until it has logged out, even if its row vanished meanwhile.
inline bool PoolTrackKept(PoolBotState const& bot, bool rowPresent)
{
    return rowPresent || bot.phase != PoolPhase::Offline;
}

// How many of `times` fall inside the window that ends now (half-open: an event exactly one window old is out).
inline std::uint32_t PoolRecentCount(std::vector<std::int64_t> const& times, std::int64_t nowMs, std::int64_t windowMs)
{
    std::uint32_t count = 0;
    for (std::int64_t const at : times)
        if (nowMs - at < windowMs)
            ++count;
    return count;
}

// The newest sessions first: a lowered target gives up the bots that have played least (the spec's order).
inline bool PoolNewestSessionFirst(PoolBotState const& a, PoolBotState const& b)
{
    if (a.sessionStartMs != b.sessionStartMs)
        return a.sessionStartMs > b.sessionStartMs;
    return a.guid > b.guid;
}

// The session that ended longest ago first.
inline bool PoolOldestEndFirst(PoolBotState const& a, PoolBotState const& b)
{
    if (a.sessionEndsMs != b.sessionEndsMs)
        return a.sessionEndsMs < b.sessionEndsMs;
    return a.guid < b.guid;
}

// The bot offline longest first: never-logged-out (0) at the head, then the oldest `last_logout`.
inline bool PoolLongestOfflineFirst(PoolBotState const& a, PoolBotState const& b)
{
    if (a.lastLogoutMs != b.lastLogoutMs)
        return a.lastLogoutMs < b.lastLogoutMs;
    return a.guid < b.guid;
}

inline bool PoolNewestSessionFirstPtr(PoolBotState const* a, PoolBotState const* b) { return PoolNewestSessionFirst(*a, *b); }
inline bool PoolOldestEndFirstPtr(PoolBotState const* a, PoolBotState const* b) { return PoolOldestEndFirst(*a, *b); }

inline bool PoolDueOffline(PoolBotState const& bot, std::int64_t nowMs, std::vector<std::uint32_t> const& occupiedAccounts)
{
    return !bot.bound && bot.phase == PoolPhase::Offline && !PoolAccountBusy(bot, occupiedAccounts) && nowMs >= bot.offlineUntilMs;
}

inline PoolPass PlanPoolPass(PoolPassInput const& in)
{
    static std::vector<PoolBotState> const kNoBots;
    static std::vector<std::int64_t> const kNoTimes;
    PoolPass out;
    out.nextFaction = in.nextFaction;
    std::vector<PoolBotState> const& bots = in.bots ? *in.bots : kNoBots;
    std::vector<std::int64_t> const& recentLogins = in.recentLoginsMs ? *in.recentLoginsMs : kNoTimes;
    std::vector<std::int64_t> const& recentLogouts = in.recentLogoutsMs ? *in.recentLogoutsMs : kNoTimes;
    PoolScheduleSettings const& settings = in.settings;
    std::vector<std::uint32_t> const occupiedAccounts = PoolOccupiedAccounts(bots);

    for (PoolBotState const& bot : bots)
    {
        if (bot.bound || bot.phase == PoolPhase::Offline)
            continue;
        ++out.counted;
        (bot.faction == PoolFaction::Alliance ? out.alliance : out.horde) += 1;
    }

    // Logouts. A session's end is not rate limited (the rolled lengths spread it); the drain of a stop and of a lowered target
    // is, at the spec's 5 per second.
    std::uint32_t const recentLogoutsInWindow = PoolRecentCount(recentLogouts, in.nowMs, kPoolLogoutWindowMs);
    std::uint32_t const allowancePerSecond = recentLogoutsInWindow >= kPoolLogoutsPerSecond ? 0u
        : kPoolLogoutsPerSecond - recentLogoutsInWindow;

    std::vector<PoolBotState const*> candidates;
    // A slot still being held by a bot that is on its way out (Leaving) is not excess: it is about to free itself.
    std::uint32_t active = 0;
    for (PoolBotState const& bot : bots)
        if (!bot.bound && (bot.phase == PoolPhase::Online || bot.phase == PoolPhase::Loading))
            ++active;
    std::uint32_t excess = in.stopping ? 0 : (active > settings.targetOnline ? active - settings.targetOnline : 0);
    if (in.stopping)
    {
        for (PoolBotState const& bot : bots)
            if (!bot.bound && !bot.inBg && (bot.phase == PoolPhase::Online || bot.phase == PoolPhase::Loading))
                candidates.push_back(&bot);   // BG: a bot inside a match waits for Bg.MaxMinutes
        std::sort(candidates.begin(), candidates.end(), PoolNewestSessionFirstPtr);
        for (PoolBotState const* bot : candidates)
        {
            if (out.logouts.size() >= allowancePerSecond)
                break;
            out.logouts.push_back(bot->guid);
        }
        return out;
    }

    for (PoolBotState const& bot : bots)
    {
        if (bot.bound || bot.phase != PoolPhase::Online || bot.inBg || !bot.sessionEndsMs || in.nowMs < bot.sessionEndsMs)
            continue;
        // A corpse still gets its ten minutes (W1's corpse run) unless it is alive again; a living bot keeps its session until
        // the fight it is in has ended.
        if (!bot.alive)
        {
            if (in.nowMs - bot.sessionEndsMs < kPoolDeadWaitMs)
                continue;
        }
        else if (bot.inCombat)
            continue;
        candidates.push_back(&bot);
    }
    std::sort(candidates.begin(), candidates.end(), PoolOldestEndFirstPtr);
    for (PoolBotState const* bot : candidates)
        out.logouts.push_back(bot->guid);

    if (excess)
    {
        // A lowered target: the excess goes newest session first, inside the same 5 per second (the sessions that just ended
        // are part of the excess too).
        std::uint32_t already = std::uint32_t(out.logouts.size());
        excess = excess > already ? excess - already : 0;
        std::vector<PoolBotState const*> live;
        for (PoolBotState const& bot : bots)
        {
            if (bot.bound || (bot.phase != PoolPhase::Online && bot.phase != PoolPhase::Loading))
                continue;
            if (!bot.alive || bot.inCombat || bot.inBg)
                continue;                     // a corpse, a fight and a match are left alone; a later pass takes them
            if (std::find(out.logouts.begin(), out.logouts.end(), bot.guid) != out.logouts.end())
                continue;
            live.push_back(&bot);
        }
        std::sort(live.begin(), live.end(), PoolNewestSessionFirstPtr);
        std::uint32_t added = 0;
        for (PoolBotState const* bot : live)
        {
            if (!excess || added >= allowancePerSecond)
                break;
            out.logouts.push_back(bot->guid);
            --excess;
            ++added;
        }
    }

    // Logins: the headroom under the target, the 60 s window and one per pass, whichever is smallest.
    for (PoolBotState const& bot : bots)
        if (PoolDueOffline(bot, in.nowMs, occupiedAccounts))
            ++out.eligible;

    std::uint32_t allowed = settings.targetOnline > out.counted ? settings.targetOnline - out.counted : 0;
    if (settings.loginsPerMinute > 0)
    {
        std::uint32_t const inWindow = PoolRecentCount(recentLogins, in.nowMs, kPoolLoginWindowMs);
        std::uint32_t const room = inWindow >= settings.loginsPerMinute ? 0u : settings.loginsPerMinute - inWindow;
        allowed = allowed < room ? allowed : room;
    }
    else
        allowed = 0;
    if (in.shuttingDown)
        allowed = 0;                      // the server is going down: the logouts still run, no session starts
    allowed = allowed < kPoolLoginsPerPass ? allowed : kPoolLoginsPerPass;
    if (allowed > out.eligible)
        allowed = out.eligible;

    std::vector<std::uint64_t> taken;
    for (std::uint32_t i = 0; i < allowed; ++i)
    {
        PoolBotState const* best = nullptr;
        for (int round = 0; round < 2 && !best; ++round)
        {
            PoolFaction const faction = round == 0 ? out.nextFaction : OtherFaction(out.nextFaction);
            for (PoolBotState const& bot : bots)
            {
                if (!PoolDueOffline(bot, in.nowMs, occupiedAccounts) || bot.faction != faction)
                    continue;
                if (std::find(taken.begin(), taken.end(), bot.guid) != taken.end())
                    continue;
                if (!best || PoolLongestOfflineFirst(bot, *best))
                    best = &bot;
            }
        }
        if (!best)
            break;
        out.logins.push_back(best->guid);
        taken.push_back(best->guid);
        out.nextFaction = OtherFaction(best->faction);
    }

    return out;
}

} // namespace world
} // namespace TortoiseBots
