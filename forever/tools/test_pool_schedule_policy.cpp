// Standalone test of W0's pool scheduler decisions (ai/playerbot/policy/world/PoolSchedulePolicy.h):
//   g++ -std=c++17 -O1 -Wall -Wextra -If. forever/tools/test_pool_schedule_policy.cpp -o /tmp/test_pool_schedule_policy && /tmp/test_pool_schedule_policy
//
// The round's five cases, in the spec's words (FOREVER-W0-W4-SPEC.md, row W0-B):
//   1. 200 due logins with LoginsPerMinute 20 start at most 20 in any 60 s;
//   2. a session past its end logs out once;
//   3. a bound bot never logs out;
//   4. a bot offline less than OfflineMinutes.Min is not picked;
//   5. target lowered from 200 to 50 logs out the newest first.
// Plus the rules the cases rest on: alternation by faction, oldest last_logout first, one session per account (ten characters
// on an account share it), the dead wait and a fight's wait, the shutdown's no-login, the rolled session and offline lengths,
// the track a vanished row must not lose, the level ladder (its band math, rounding and edge cases, the online target split
// over the bands, the level cap's hard share at 0/10/100 percent, the pick order and its off switch), and the pass cost
// (under 0.5 ms over 2000 bots, and over 2500 mixed-level bots with the ladder running).
//
// The simulator applies a pass the way the host scheduler does: a login becomes Loading (it holds a slot) and its session ends
// after a rolled length; a logout becomes Leaving (no second logout). It is the smallest model in which "logs out once" has a
// meaning.
#include "ai/playerbot/policy/world/PoolSchedulePolicy.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <set>

namespace w = TortoiseBots::world;

static int g_checks = 0, g_fail = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

namespace
{

constexpr std::int64_t kSecond = 1000;
constexpr std::int64_t kMinute = w::kPoolMinuteMs;

struct Sim
{
    std::vector<w::PoolBotState> bots;
    w::PoolScheduleSettings settings;
    std::vector<std::int64_t> logins;
    std::vector<std::int64_t> logouts;
    w::PoolFaction next = w::PoolFaction::Alliance;
    std::int64_t now = 0;
    std::uint32_t roll = 1;

    w::PoolPass Pass(bool stopping = false, bool shuttingDown = false)
    {
        w::PoolPassInput in;
        in.bots = &bots;
        in.settings = settings;
        in.nowMs = now;
        in.stopping = stopping;
        in.shuttingDown = shuttingDown;
        in.nextFaction = next;
        in.recentLoginsMs = &logins;
        in.recentLogoutsMs = &logouts;
        w::PoolPass pass = w::PlanPoolPass(in);
        next = pass.nextFaction;
        return pass;
    }

    w::PoolBotState* Find(std::uint64_t guid)
    {
        for (w::PoolBotState& bot : bots)
            if (bot.guid == guid)
                return &bot;
        return nullptr;
    }

    // Loading becomes Online (the session is in the world), as the host's pass does once FindBot answers.
    void Sync()
    {
        for (w::PoolBotState& bot : bots)
            if (bot.phase == w::PoolPhase::Loading)
                bot.phase = w::PoolPhase::Online;
    }

    void Apply(w::PoolPass const& pass)
    {
        for (std::uint64_t const guid : pass.logins)
        {
            w::PoolBotState* bot = Find(guid);
            CHECK(bot != nullptr);
            if (!bot)
                continue;
            CHECK(bot->phase == w::PoolPhase::Offline);
            bot->phase = w::PoolPhase::Loading;
            bot->sessionStartMs = now;
            bot->sessionEndsMs = now + w::PoolSessionLengthMs(settings, roll++);
            bot->offlineUntilMs = 0;
            logins.push_back(now);
        }
        for (std::uint64_t const guid : pass.logouts)
        {
            w::PoolBotState* bot = Find(guid);
            CHECK(bot != nullptr);
            if (!bot)
                continue;
            CHECK(bot->phase != w::PoolPhase::Leaving);
            bot->phase = w::PoolPhase::Leaving;
            logouts.push_back(now);
        }
    }

    std::uint32_t MaxLoginsInWindow() const
    {
        std::uint32_t worst = 0;
        for (std::size_t i = 0; i < logins.size(); ++i)
        {
            std::uint32_t count = 0;
            for (std::size_t j = i; j < logins.size(); ++j)
                if (logins[j] - logins[i] < w::kPoolLoginWindowMs)
                    ++count;
            worst = count > worst ? count : worst;
        }
        return worst;
    }
};

w::PoolBotState OfflineBot(std::uint64_t guid, std::uint8_t race, std::int64_t lastLogoutMs)
{
    w::PoolBotState bot;
    bot.guid = guid;
    bot.faction = w::PoolFactionOfRace(race);
    bot.phase = w::PoolPhase::Offline;
    bot.lastLogoutMs = lastLogoutMs;
    bot.offlineUntilMs = lastLogoutMs;
    return bot;
}

w::PoolBotState OnlineBot(std::uint64_t guid, std::uint8_t race, std::int64_t sessionStartMs, std::int64_t sessionEndsMs)
{
    w::PoolBotState bot;
    bot.guid = guid;
    bot.faction = w::PoolFactionOfRace(race);
    bot.phase = w::PoolPhase::Online;
    bot.sessionStartMs = sessionStartMs;
    bot.sessionEndsMs = sessionEndsMs;
    bot.alive = true;
    return bot;
}

} // namespace

int main()
{
    // `.tb pool population <n|low|high>`: the word to a target. `low` and `high` are the presets, a number is used as-is,
    // anything else and a number past the ceiling are refused.
    {
        w::PoolPopulationTarget low = w::PoolResolvePopulationTarget("low", 10, 50);
        CHECK(low.ok && low.target == 10);
        w::PoolPopulationTarget high = w::PoolResolvePopulationTarget("high", 10, 50);
        CHECK(high.ok && high.target == 50);
        w::PoolPopulationTarget number = w::PoolResolvePopulationTarget("25", 10, 50);
        CHECK(number.ok && number.target == 25);
        w::PoolPopulationTarget zero = w::PoolResolvePopulationTarget("0", 10, 50);
        CHECK(zero.ok && zero.target == 0);
        w::PoolPopulationTarget ceiling = w::PoolResolvePopulationTarget("5000", 10, 50);
        CHECK(ceiling.ok && ceiling.target == 5000);
        w::PoolPopulationTarget past = w::PoolResolvePopulationTarget("5001", 10, 50);
        CHECK(!past.ok);
        w::PoolPopulationTarget huge = w::PoolResolvePopulationTarget("99999999999999999999", 10, 50);
        CHECK(!huge.ok);
        w::PoolPopulationTarget empty = w::PoolResolvePopulationTarget("", 10, 50);
        CHECK(!empty.ok);
        w::PoolPopulationTarget word = w::PoolResolvePopulationTarget("medium", 10, 50);
        CHECK(!word.ok);
        w::PoolPopulationTarget mixed = w::PoolResolvePopulationTarget("12a", 10, 50);
        CHECK(!mixed.ok);
        w::PoolPopulationTarget negative = w::PoolResolvePopulationTarget("-5", 10, 50);
        CHECK(!negative.ok);
        w::PoolPopulationTarget spaced = w::PoolResolvePopulationTarget(" 25", 10, 50);
        CHECK(!spaced.ok);
    }
    // The ids the policy relies on: the client's races, the Alliance's five (the two Skyborne are 95 and 96).
    CHECK(w::PoolFactionOfRace(1) == w::PoolFaction::Alliance);
    CHECK(w::PoolFactionOfRace(3) == w::PoolFaction::Alliance);
    CHECK(w::PoolFactionOfRace(4) == w::PoolFaction::Alliance);
    CHECK(w::PoolFactionOfRace(7) == w::PoolFaction::Alliance);
    CHECK(w::PoolFactionOfRace(95) == w::PoolFaction::Alliance);
    CHECK(w::PoolFactionOfRace(2) == w::PoolFaction::Horde);
    CHECK(w::PoolFactionOfRace(5) == w::PoolFaction::Horde);
    CHECK(w::PoolFactionOfRace(6) == w::PoolFaction::Horde);
    CHECK(w::PoolFactionOfRace(8) == w::PoolFaction::Horde);
    CHECK(w::PoolFactionOfRace(96) == w::PoolFaction::Horde);
    CHECK(w::OtherFaction(w::PoolFaction::Alliance) == w::PoolFaction::Horde);
    CHECK(w::OtherFaction(w::PoolFaction::Horde) == w::PoolFaction::Alliance);

    // The rolled lengths: both ends are reachable, nothing outside them, and a reversed range is its lower end.
    {
        w::PoolScheduleSettings s;
        CHECK(w::PoolSessionLengthMs(s, 0) == 30 * kMinute);
        CHECK(w::PoolSessionLengthMs(s, 0xffffffffu) == 90 * kMinute);
        for (std::uint32_t roll : { 0u, 1u, 12345u, 0x7fffffffu, 0xfffffffeu, 0xffffffffu })
        {
            std::int64_t const ms = w::PoolSessionLengthMs(s, roll);
            CHECK(ms >= 30 * kMinute && ms <= 90 * kMinute);
        }
        CHECK(w::RollPoolLengthMs(90 * kMinute, 30 * kMinute, 0) == 30 * kMinute);
        CHECK(w::RollPoolLengthMs(0, 0, 7) == 0);
        std::set<std::int64_t> lengths;
        for (std::uint32_t roll = 0; roll < 100; ++roll)
            lengths.insert(w::RollPoolLengthMs(30 * kMinute, 90 * kMinute, roll * 42949672u));
        CHECK(lengths.size() >= 90);
        w::PoolScheduleSettings o;
        CHECK(w::PoolOfflineLengthMs(o, 0) == 5 * kMinute);
        CHECK(w::PoolOfflineLengthMs(o, 0xffffffffu) == 20 * kMinute);
        for (std::uint32_t roll : { 0u, 99u, 0xffffffffu })
        {
            std::int64_t const ms = w::PoolOfflineLengthMs(o, roll);
            CHECK(ms >= 5 * kMinute && ms <= 20 * kMinute);
        }
    }

    // (1) 200 due logins with LoginsPerMinute 20: at most 20 in any 60 s.
    {
        Sim sim;
        sim.settings.targetOnline = 200;
        sim.settings.loginsPerMinute = 20;
        for (std::uint64_t i = 0; i < 200; ++i)
            sim.bots.push_back(OfflineBot(i + 1, std::uint8_t(1 + i % 8), 0));
        for (int second = 0; second < 200; ++second)
        {
            sim.now = second * kSecond;
            sim.Sync();
            sim.Apply(sim.Pass());
        }
        CHECK(sim.MaxLoginsInWindow() <= 20);
        CHECK(sim.logins.size() >= 60);                       // the rate is used, not just capped
        CHECK(sim.logins.size() <= 80);
        // No burst: one login per pass, whatever the window allows.
        for (std::size_t i = 1; i < sim.logins.size(); ++i)
            CHECK(sim.logins[i] != sim.logins[i - 1]);
    }

    // (2) a session past its end logs out once.
    {
        Sim sim;
        sim.settings.targetOnline = 10;
        sim.now = 5000;
        sim.bots.push_back(OnlineBot(1, 1, 0, 1000));         // ended at 1 s
        w::PoolPass first = sim.Pass();
        CHECK(first.logouts.size() == 1 && first.logouts[0] == 1);
        CHECK(first.counted == 1);
        sim.Apply(first);
        sim.now = 6000;
        w::PoolPass second = sim.Pass();
        CHECK(second.logouts.empty());
        CHECK(second.counted == 1);                            // Leaving still holds its slot
        // And a session that has not ended is not touched.
        Sim later;
        later.settings.targetOnline = 10;
        later.now = 5000;
        later.bots.push_back(OnlineBot(2, 2, 0, 9000));
        CHECK(later.Pass().logouts.empty());
    }

    // (3) a bound bot never logs out: not on its session's end, not on a lowered target, not on a stop; and it does not count.
    {
        Sim sim;
        sim.settings.targetOnline = 0;
        sim.now = 100000;
        w::PoolBotState bound = OnlineBot(7, 1, 0, 1000);
        bound.bound = true;
        sim.bots.push_back(bound);
        CHECK(sim.Pass().logouts.empty());
        CHECK(sim.Pass(true).logouts.empty());
        CHECK(sim.Pass().counted == 0);
        // A bound bot that is offline is never logged in again either.
        w::PoolBotState away = OfflineBot(8, 2, 0);
        away.bound = true;
        sim.bots.push_back(away);
        sim.settings.targetOnline = 10;
        w::PoolPass pass = sim.Pass();
        CHECK(pass.logins.empty());
        CHECK(pass.eligible == 0);
    }

    // (3a) a bind frees the pool's slot: the next pass logs another bot in (one login inside the 60 s window), on an account no
    // pool bot holds, and the bound bot is never logged out — not at its session's end, not on a lowered target, not on a stop.
    {
        Sim sim;
        sim.settings.targetOnline = 10;
        sim.now = 10 * kMinute;
        // Ten accounts with a character online each, ten characters per account, and ten more accounts the pool can still use.
        for (std::uint64_t account = 1; account <= 20; ++account)
            for (std::uint64_t character = 0; character < 10; ++character)
            {
                std::uint64_t const guid = account * 100 + character;
                std::uint8_t const race = std::uint8_t(1 + account % 8);
                w::PoolBotState bot = (account <= 10 && character == 0)
                    ? OnlineBot(guid, race, sim.now - kMinute, sim.now + 30 * kMinute)
                    : OfflineBot(guid, race, 0);
                bot.accountId = std::uint32_t(account);
                sim.bots.push_back(bot);
            }
        CHECK(sim.Pass().logins.empty());                    // at the target: nothing to do
        w::PoolBotState* bound = sim.Find(100);              // `.tb group` takes the bot on account 1
        CHECK(bound != nullptr);
        bound->bound = true;
        w::PoolPass pass = sim.Pass();
        CHECK(pass.counted == 9);                            // the bound bot leaves the count
        CHECK(pass.logins.size() == 1);                      // one more, at once (the window has room)
        w::PoolBotState const* replacement = sim.Find(pass.logins[0]);
        CHECK(replacement != nullptr && replacement->accountId > 10);   // on an account no pool bot holds
        sim.Apply(pass);
        CHECK(sim.logins.back() == sim.now);
        bound->sessionEndsMs = sim.now - 1;                  // its session ends; the target drops to 0
        sim.settings.targetOnline = 0;
        sim.now += kSecond;
        w::PoolPass lowered = sim.Pass();
        CHECK(!lowered.logouts.empty());                     // the free bots go ...
        CHECK(std::find(lowered.logouts.begin(), lowered.logouts.end(), std::uint64_t(100)) == lowered.logouts.end());   // ... it does not
        w::PoolPass stopped = sim.Pass(true);
        CHECK(std::find(stopped.logouts.begin(), stopped.logouts.end(), std::uint64_t(100)) == stopped.logouts.end());
    }

    // (3b) the account cap is the one thing that can block the refill: the live pool's shape (ten accounts, ten characters
    // each, target 10) has every account held when one bot gets bound, so nine pool bots are the most it can run and the cap
    // says 9 — `.tb pool status`'s ` capped by accounts: 9`, not a silent shortfall.
    {
        Sim sim;
        sim.settings.targetOnline = 10;
        sim.now = 10 * kMinute;
        for (std::uint64_t account = 1; account <= 10; ++account)
            for (std::uint64_t character = 0; character < 10; ++character)
            {
                std::uint64_t const guid = account * 100 + character;
                std::uint8_t const race = std::uint8_t(1 + account % 8);
                w::PoolBotState bot = character == 0
                    ? OnlineBot(guid, race, sim.now - kMinute, sim.now + 30 * kMinute)
                    : OfflineBot(guid, race, 0);
                bot.accountId = std::uint32_t(account);
                sim.bots.push_back(bot);
            }
        CHECK(sim.Pass().counted == 10);
        CHECK(w::PoolAccountCount(sim.bots) == 10);
        sim.Find(100)->bound = true;
        w::PoolPass pass = sim.Pass();
        CHECK(pass.counted == 9);
        CHECK(pass.eligible == 0);                           // every account holds a session: the bound bot's own too
        CHECK(pass.logins.empty());
        CHECK(w::PoolAccountCount(sim.bots) == 9);           // the cap the status line reports
        // Unbound, the account is the pool's again.
        sim.Find(100)->bound = false;
        CHECK(w::PoolAccountCount(sim.bots) == 10);
    }

    // (4) a bot offline less than OfflineMinutes.Min is not picked.
    {
        Sim sim;
        sim.settings.targetOnline = 10;
        sim.now = 10 * kMinute;
        w::PoolBotState a = OfflineBot(1, 1, sim.now);              // went offline now: at least 5 min to wait
        a.offlineUntilMs = sim.now + 5 * kMinute;
        w::PoolBotState b = OfflineBot(2, 2, sim.now - 6 * kMinute);   // six minutes away
        b.offlineUntilMs = sim.now - 1 * kMinute;
        sim.bots = { a, b };
        w::PoolPass pass = sim.Pass();
        CHECK(pass.eligible == 1);
        CHECK(pass.logins.size() == 1 && pass.logins[0] == 2);
        sim.bots = { a };
        CHECK(sim.Pass().logins.empty());
    }

    // (5) target lowered from 200 to 50: the newest first, five per second, each bot once.
    {
        Sim sim;
        sim.settings.targetOnline = 50;
        sim.now = 10 * kMinute;
        for (std::uint64_t i = 0; i < 200; ++i)
            sim.bots.push_back(OnlineBot(i + 1, std::uint8_t(1 + i % 8), std::int64_t(i) * kSecond, sim.now + 10 * kMinute));
        w::PoolPass first = sim.Pass();
        CHECK(first.counted == 200);
        CHECK(first.logouts.size() == 5);
        for (std::size_t i = 0; i < first.logouts.size(); ++i)
            CHECK(first.logouts[i] == 200 - i);                 // guid 200 has the newest session, then 199 ...
        sim.Apply(first);
        std::vector<std::uint64_t> order = first.logouts;
        for (int second = 1; order.size() < 150 && second < 200; ++second)
        {
            sim.now += kSecond;
            w::PoolPass pass = sim.Pass();
            CHECK(pass.logouts.size() <= 5);
            sim.Apply(pass);
            for (std::uint64_t const guid : pass.logouts)
            {
                CHECK(std::find(order.begin(), order.end(), guid) == order.end());
                order.push_back(guid);
            }
        }
        CHECK(order.size() == 150);
        for (std::size_t i = 1; i < order.size(); ++i)
            CHECK(order[i] < order[i - 1]);                     // strictly newest first, all the way down
        sim.now += kSecond;
        CHECK(sim.Pass().logouts.empty());                      // the 50 left are the pool
    }

    // (5b) `.tb pool stop` drains every pool bot at 5 per second (a burst of sessions in one tick would stall the world); a
    // bound bot is not drained, even here.
    {
        Sim sim;
        sim.settings.targetOnline = 20;
        sim.now = 10 * kMinute;
        for (std::uint64_t i = 0; i < 20; ++i)
            sim.bots.push_back(OnlineBot(i + 1, std::uint8_t(1 + i % 8), std::int64_t(i) * kSecond, sim.now + 10 * kMinute));
        w::PoolBotState bound = OnlineBot(21, 1, 0, sim.now + 10 * kMinute);
        bound.bound = true;
        sim.bots.push_back(bound);
        w::PoolPass first = sim.Pass(true);
        CHECK(first.logouts.size() == 5);
        sim.Apply(first);
        std::vector<std::uint64_t> order = first.logouts;
        for (int second = 1; second < 20 && order.size() < 20; ++second)
        {
            sim.now += kSecond;
            w::PoolPass pass = sim.Pass(true);
            CHECK(pass.logouts.size() <= 5);
            for (std::uint64_t const guid : pass.logouts)
                CHECK(guid != 21);
            sim.Apply(pass);
            for (std::uint64_t const guid : pass.logouts)
            {
                CHECK(std::find(order.begin(), order.end(), guid) == order.end());
                order.push_back(guid);
            }
        }
        CHECK(order.size() == 20);                             // every pool bot, none of them twice
        CHECK(std::find(order.begin(), order.end(), std::uint64_t(21)) == order.end());
    }

    // (6) the dead wait: a corpse keeps ten minutes after its session's end, then the pool logs it out.
    {
        Sim sim;
        sim.settings.targetOnline = 10;
        sim.now = 100 * kMinute;
        w::PoolBotState dead = OnlineBot(3, 1, 0, sim.now - kMinute);
        dead.alive = false;
        sim.bots.push_back(dead);
        CHECK(sim.Pass().logouts.empty());
        sim.now = 100 * kMinute + 8 * kMinute;
        CHECK(sim.Pass().logouts.empty());
        sim.now = 110 * kMinute;                                // 10 min after the end
        w::PoolPass pass = sim.Pass();
        CHECK(pass.logouts.size() == 1 && pass.logouts[0] == 3);
    }

    // (7) alternation by faction, oldest last_logout first; the other faction only when the due one has no candidate.
    {
        Sim sim;
        sim.settings.targetOnline = 10;
        sim.settings.loginsPerMinute = 60;
        sim.now = 0;
        sim.bots.push_back(OfflineBot(1, 1, 0));      // Alliance
        sim.bots.push_back(OfflineBot(2, 2, 0));      // Horde
        sim.bots.push_back(OfflineBot(3, 3, 0));      // Alliance
        sim.bots.push_back(OfflineBot(4, 8, 0));      // Horde
        std::vector<std::uint64_t> picked;
        for (int i = 0; i < 4; ++i)
        {
            sim.now = i * kSecond;
            w::PoolPass pass = sim.Pass();
            CHECK(pass.logins.size() == 1);
            if (!pass.logins.empty())
                picked.push_back(pass.logins[0]);
            sim.Apply(pass);
        }
        CHECK(picked.size() == 4);
        if (picked.size() == 4)
        {
            CHECK(picked[0] == 1 && picked[1] == 2 && picked[2] == 3 && picked[3] == 4);
        }
        // A pool with only Horde bots logs them in a row (the due faction falls back, the alternation does not pretend).
        Sim horde;
        horde.settings.targetOnline = 2;
        horde.settings.loginsPerMinute = 60;
        horde.bots.push_back(OfflineBot(10, 2, 0));
        horde.bots.push_back(OfflineBot(11, 5, 0));
        horde.now = 0;
        w::PoolPass one = horde.Pass();
        horde.Apply(one);
        CHECK(one.logins.size() == 1 && one.logins[0] == 10);   // race 2, guid 10: Alliance is due, falls back
        CHECK(one.nextFaction == w::PoolFaction::Alliance);     // flipped from the bot that was picked
        horde.now = kSecond;
        w::PoolPass two = horde.Pass();
        CHECK(two.logins.size() == 1 && two.logins[0] == 11);
        // Oldest last_logout first inside a faction: 20 played before 21.
        Sim old;
        old.settings.targetOnline = 5;
        old.settings.loginsPerMinute = 60;
        old.now = 100 * kMinute;
        w::PoolBotState a = OfflineBot(20, 1, 10 * kMinute);
        a.offlineUntilMs = 0;
        w::PoolBotState b = OfflineBot(21, 3, 30 * kMinute);
        b.offlineUntilMs = 0;
        old.bots = { a, b };
        w::PoolPass oldest = old.Pass();
        CHECK(oldest.logins.size() == 1 && oldest.logins[0] == 20);
    }

    // (8) an account with a session is never picked, and the login window holds at the configured rate.
    {
        Sim sim;
        sim.settings.targetOnline = 10;
        sim.settings.loginsPerMinute = 20;
        sim.now = 0;
        w::PoolBotState busy = OfflineBot(1, 1, 0);
        busy.accountOnline = true;
        w::PoolBotState freeBot = OfflineBot(2, 2, 0);
        sim.bots = { busy, freeBot };
        w::PoolPass pass = sim.Pass();
        CHECK(pass.eligible == 1);
        CHECK(pass.logins.size() == 1 && pass.logins[0] == 2);
        // The rate 0 closes the logins; a session's end still logs out.
        Sim off;
        off.settings.targetOnline = 10;
        off.settings.loginsPerMinute = 0;
        off.now = 5000;
        off.bots.push_back(OfflineBot(1, 1, 0));
        off.bots.push_back(OnlineBot(2, 1, 0, 1000));
        w::PoolPass stopped = off.Pass();
        CHECK(stopped.logins.empty());
        CHECK(stopped.logouts.size() == 1 && stopped.logouts[0] == 2);
        // The target 0 closes the logins too.
        Sim zero;
        zero.settings.targetOnline = 0;
        zero.now = 0;
        zero.bots.push_back(OfflineBot(1, 1, 0));
        CHECK(zero.Pass().logins.empty());
    }

    // (8b) D2: one session per account. Twenty accounts hold ten characters each: the pass gives each account one session and
    // the count stops at the accounts, never at the 200 characters.
    {
        Sim sim;
        sim.settings.targetOnline = 200;
        sim.settings.loginsPerMinute = 60;
        for (std::uint64_t i = 0; i < 200; ++i)
        {
            w::PoolBotState bot = OfflineBot(i + 1, std::uint8_t(1 + i % 8), 0);
            bot.accountId = std::uint32_t(1 + i / 10);          // ten characters on each of twenty accounts
            sim.bots.push_back(bot);
        }
        bool twoOnOne = false;
        std::uint32_t started = 0;
        for (int pass = 0; pass < 120; ++pass)
        {
            sim.now = std::int64_t(pass) * kSecond;
            sim.Sync();
            w::PoolPass const plan = sim.Pass();
            for (std::uint64_t const guid : plan.logins)
            {
                w::PoolBotState* const bot = sim.Find(guid);
                CHECK(bot != nullptr);
                for (w::PoolBotState const& other : sim.bots)
                    if (bot && other.guid != guid && other.accountId == bot->accountId && other.phase != w::PoolPhase::Offline)
                        twoOnOne = true;
            }
            sim.Apply(plan);
            started += std::uint32_t(plan.logins.size());
            std::vector<std::uint32_t> running(21, 0);
            for (w::PoolBotState const& bot : sim.bots)
                if (bot.phase != w::PoolPhase::Offline)
                    ++running[bot.accountId];
            for (std::uint32_t const on : running)
                CHECK(on <= 1);
        }
        CHECK(!twoOnOne);                                        // never a second character of an account while one runs
        CHECK(started == 20);                                    // the target was 200: the accounts cap it at 20
        CHECK(sim.Pass().counted == 20);
        CHECK(w::PoolAccountCount(sim.bots) == 20);
    }

    // (8c) a living bot in combat is not logged out at its session's end; the pass after the fight is.
    {
        Sim sim;
        sim.settings.targetOnline = 10;
        sim.now = 100 * kMinute;
        w::PoolBotState bot = OnlineBot(1, 1, 0, sim.now - kMinute);
        bot.inCombat = true;
        sim.bots.push_back(bot);
        CHECK(sim.Pass().logouts.empty());
        sim.bots[0].inCombat = false;
        w::PoolPass const after = sim.Pass();
        CHECK(after.logouts.size() == 1 && after.logouts[0] == 1);
    }

    // (8d) a lowered target leaves a corpse and a fighting bot alone and takes the next newest; a stop takes them all.
    {
        Sim sim;
        sim.settings.targetOnline = 1;
        sim.now = 100 * kMinute;
        w::PoolBotState old = OnlineBot(1, 1, 0, sim.now + 10 * kMinute);
        w::PoolBotState fight = OnlineBot(2, 3, kSecond, sim.now + 10 * kMinute);
        fight.inCombat = true;
        w::PoolBotState dead = OnlineBot(3, 4, 2 * kSecond, sim.now + 10 * kMinute);
        dead.alive = false;
        sim.bots = { old, fight, dead };
        w::PoolPass const cut = sim.Pass();
        CHECK(cut.logouts.size() == 1 && cut.logouts[0] == 1);
        CHECK(std::find(cut.logouts.begin(), cut.logouts.end(), std::uint64_t(2)) == cut.logouts.end());
        CHECK(std::find(cut.logouts.begin(), cut.logouts.end(), std::uint64_t(3)) == cut.logouts.end());
        CHECK(sim.Pass(true).logouts.size() == 3);                // an explicit stop is unconditional
    }

    // (8e) a pending shutdown plans no logins; a session's end still logs out.
    {
        Sim sim;
        sim.settings.targetOnline = 10;
        sim.settings.loginsPerMinute = 60;
        sim.now = 5000;
        sim.bots.push_back(OfflineBot(1, 1, 0));
        sim.bots.push_back(OnlineBot(2, 2, 0, 1000));
        w::PoolPass const pass = sim.Pass(false, true);
        CHECK(pass.logins.empty());
        CHECK(pass.eligible == 1);                               // due, but the shutdown holds it back
        CHECK(pass.logouts.size() == 1 && pass.logouts[0] == 2);
    }

    // (8f) the phase transitions the host asks the policy for: a master seen on the switch to Online, a bot freed again with a
    // fresh session, and a bound bot that left the world (the master's grace) back in the pool's count.
    {
        w::PoolScheduleSettings settings;
        std::uint32_t roll = 0;
        auto nextRoll = [&roll] { return roll++; };

        w::PoolBotState bot = OnlineBot(1, 1, 0, 1000);
        bot.phase = w::PoolPhase::Loading;                       // it just came into the world
        bot.sessionStartMs = 0;
        bot.sessionEndsMs = 0;
        w::PoolBotFacts facts;
        facts.logged = true;
        facts.inWorld = true;
        facts.free = false;                                      // and has a master
        facts.hasMaster = true;
        w::PoolSyncOutcome outcome = w::PoolSyncBot(bot, facts, settings, 5000, nextRoll);
        CHECK(bot.phase == w::PoolPhase::Online);
        CHECK(bot.bound);
        CHECK(outcome.boundSet);
        CHECK(bot.sessionEndsMs > 5000);

        facts.logged = false;                                    // the master's grace logged it out
        facts.inWorld = false;
        facts.hasMaster = false;
        outcome = w::PoolSyncBot(bot, facts, settings, 5500, nextRoll);
        CHECK(outcome.wentOffline);
        CHECK(bot.phase == w::PoolPhase::Offline);
        CHECK(!bot.bound);                                       // eligible again for the pool
        CHECK(bot.lastLogoutMs == 5500);
        CHECK(bot.offlineUntilMs >= 5500 + settings.offlineMinMs);

        w::PoolBotState freed = OnlineBot(2, 3, 1000, 20000);
        w::PoolBotFacts online;
        online.logged = true;
        online.inWorld = true;
        online.free = false;
        online.hasMaster = true;
        outcome = w::PoolSyncBot(freed, online, settings, 6000, nextRoll);
        CHECK(freed.bound && outcome.boundSet);                  // a master takes it out of the count

        online.hasMaster = false;                                // `.tb ungroup`: the binding ended
        outcome = w::PoolSyncBot(freed, online, settings, 7000, nextRoll);
        CHECK(!freed.bound);
        CHECK(outcome.boundCleared);
        CHECK(freed.sessionStartMs == 7000);                     // a fresh session, not the one that ran while bound
        CHECK(freed.sessionEndsMs > 7000);

        online.hasMaster = true;                                 // a master again, then `.tb free on`
        outcome = w::PoolSyncBot(freed, online, settings, 7500, nextRoll);
        CHECK(freed.bound && outcome.boundSet);
        online.hasMaster = false;
        online.free = true;
        outcome = w::PoolSyncBot(freed, online, settings, 8000, nextRoll);
        CHECK(!freed.bound);
        CHECK(outcome.boundCleared);
        CHECK(freed.sessionStartMs == 8000);
    }

    // (8g) a row that went away keeps the track of a bot that is still in the world.
    {
        w::PoolBotState online = OnlineBot(1, 1, 0, 1000);
        CHECK(w::PoolTrackKept(online, false));
        w::PoolBotState offline = OfflineBot(2, 2, 0);
        CHECK(!w::PoolTrackKept(offline, false));
        CHECK(w::PoolTrackKept(offline, true));
    }

    // (8h) BG (BG1, BATTLEGROUNDS-SPEC.md D5): a session that ends while the bot waits in a battleground queue or is inside a match
    // is deferred, the pass logs it out only after it comes out, and `.tb pool stop` skips it (the battleground module's
    // Bg.MaxMinutes bring it out, the next stop pass drains it).
    {
        w::PoolScheduleSettings settings;
        settings.targetOnline = 1;
        auto roll = [] { return 0u; };

        w::PoolBotState bot = OnlineBot(21, 1, 0, 10 * kMinute);
        w::PoolBotFacts facts;
        facts.logged = true;
        facts.inWorld = true;
        facts.free = true;
        facts.alive = true;
        facts.inBg = true;
        std::int64_t now = 10 * kMinute + 1;
        w::PoolSyncOutcome const outcome = w::PoolSyncBot(bot, facts, settings, now, roll);
        CHECK(!outcome.wentOffline);
        CHECK(bot.inBg);
        CHECK(bot.sessionEndsMs > now);                        // held: the end moved forward
        CHECK(bot.sessionEndsMs == now + w::kPoolBgHoldMs);

        std::vector<w::PoolBotState> bots{ bot };
        w::PoolPassInput in;
        in.bots = &bots;
        in.settings = settings;
        in.nowMs = now;
        in.recentLoginsMs = nullptr;
        in.recentLogoutsMs = nullptr;
        CHECK(w::PlanPoolPass(in).logouts.empty());            // a session end alone does not take it out
        in.stopping = true;
        CHECK(w::PlanPoolPass(in).logouts.empty());            // `.tb pool stop` while it is inside
        in.stopping = false;
        in.nowMs = bot.sessionEndsMs;                          // the held end is due
        CHECK(w::PlanPoolPass(in).logouts.empty());            // still inside at that end: carried on next pass by the host sync

        // It comes out (facts.inBg false): the deferred end is still ahead, then passes and the bot logs out once.
        facts.inBg = false;
        w::PoolSyncBot(bot, facts, settings, bot.sessionEndsMs - 1, roll);
        CHECK(!bot.inBg);
        bots[0] = bot;
        in.nowMs = bot.sessionEndsMs - 1;
        CHECK(w::PlanPoolPass(in).logouts.empty());
        in.nowMs = bot.sessionEndsMs;
        w::PoolPass const leaving = w::PlanPoolPass(in);
        CHECK(leaving.logouts.size() == 1 && leaving.logouts.front() == bot.guid);

        // Baseline: the same bot without a battleground logs out at its end.
        w::PoolBotState plain = OnlineBot(22, 1, 0, 10 * kMinute);
        std::vector<w::PoolBotState> plainBots{ plain };
        in.bots = &plainBots;
        in.nowMs = 10 * kMinute;
        CHECK(w::PlanPoolPass(in).logouts.size() == 1);
    }

    // (8g) the level ladder's band math: bands of BandSize, the level cap alone in the last band, and every band's target a
    // share of the online target that adds up to it. MaxLevelShare is a hard cap at 0, 10 and 100 percent.
    {
        CHECK(w::PoolLadderBandCount(60, 5) == 13);
        CHECK(w::PoolLadderBandOf(1, 60, 5) == 0);
        CHECK(w::PoolLadderBandOf(5, 60, 5) == 0);
        CHECK(w::PoolLadderBandOf(6, 60, 5) == 1);
        CHECK(w::PoolLadderBandOf(55, 60, 5) == 10);
        CHECK(w::PoolLadderBandOf(59, 60, 5) == 11);
        CHECK(w::PoolLadderBandOf(60, 60, 5) == 12);
        CHECK(w::PoolLadderBandOf(0, 60, 5) == 0);               // unknown or below one reads as level 1
        CHECK(w::PoolLadderBandOf(255, 60, 5) == 12);            // above the cap reads as the cap
        CHECK(w::PoolLadderBandName(0, 60, 5) == "1-5");
        CHECK(w::PoolLadderBandName(1, 60, 5) == "6-10");
        CHECK(w::PoolLadderBandName(11, 60, 5) == "56-59");
        CHECK(w::PoolLadderBandName(12, 60, 5) == "60");

        // 100 online, 10 percent: ten max-level, ninety over the twelve low bands (six of eight, six of seven).
        std::uint32_t total = 0;
        for (std::uint32_t band = 0; band < 13; ++band)
        {
            std::uint32_t const target = w::PoolLadderBandTarget(band, 100, 60, 5, 10);
            CHECK(target == (band == 12 ? 10u : (band < 6 ? 8u : 7u)));
            total += target;
        }
        CHECK(total == 100);                                     // the bands add up to the online target, always
        for (std::uint32_t band = 0; band < 12; ++band)
            CHECK(w::PoolLadderBandTarget(band, 100, 60, 5, 0) == (band < 4 ? 9u : 8u));
        CHECK(w::PoolLadderBandTarget(12, 100, 60, 5, 0) == 0);   // 0 percent: the cap is never logged in
        for (std::uint32_t band = 0; band < 12; ++band)
            CHECK(w::PoolLadderBandTarget(band, 100, 60, 5, 100) == 0);
        CHECK(w::PoolLadderBandTarget(12, 100, 60, 5, 100) == 100);
        CHECK(w::PoolLadderBandTarget(12, 100, 60, 5, 250) == 100);   // past 100 clamps
        CHECK(w::PoolLadderBandTarget(0, 0, 60, 5, 10) == 0);     // a zero target logs nobody in
        CHECK(w::PoolLadderBandTarget(0, 9, 60, 5, 10) == 1);     // rounding: the cap's share floors, the rest goes low

        // BandSize edge cases: 0 and 1, the widest band, and a single low band beside the cap.
        CHECK(w::PoolLadderBandCount(60, 1) == 60);
        CHECK(w::PoolLadderBandOf(60, 60, 1) == 59);
        CHECK(w::PoolLadderBandName(0, 60, 1) == "1-1");
        CHECK(w::PoolLadderBandName(58, 60, 1) == "59-59");
        CHECK(w::PoolLadderBandName(59, 60, 1) == "60");
        CHECK(w::PoolLadderBandCount(60, 0) == 60);              // 0 clamps to one level a band
        CHECK(w::PoolLadderBandCount(60, 59) == 2);
        CHECK(w::PoolLadderBandCount(60, 500) == 2);             // past the cap-1 clamps: 1-59 and 60
        CHECK(w::PoolLadderBandName(0, 60, 59) == "1-59");
        CHECK(w::PoolLadderBandCount(2, 5) == 2);               // a two-level server still has a low band and the cap
        CHECK(w::PoolLadderBandOf(1, 2, 5) == 0);
        CHECK(w::PoolLadderBandOf(2, 2, 5) == 1);
        CHECK(w::PoolLadderLevel(0, 60) == 1);
        CHECK(w::PoolLadderLevel(60, 60) == 60);
        CHECK(w::PoolLadderLevel(99, 60) == 60);
    }

    // (8h) the ladder's pick order: the band with the largest shortfall first, and inside it the highest level first; a band
    // already at its target yields to a band still short. One login per pass, as the spec caps it, so a simulated pass per pick.
    {
        Sim sim;
        sim.settings.targetOnline = 24;                          // 2 max-level, 22 over twelve low bands
        sim.now = 100 * kMinute;
        sim.bots.push_back(OfflineBot(1, 1, 0));                 // level 3, band 0
        sim.bots.push_back(OfflineBot(2, 1, 0));                 // level 4, band 0
        sim.bots.push_back(OfflineBot(3, 1, 0));                 // level 12, band 2
        sim.bots[0].level = 3;
        sim.bots[1].level = 4;
        sim.bots[2].level = 12;
        w::PoolPass first = sim.Pass();
        CHECK(first.logins.size() == 1 && first.logins[0] == 3); // bands 0 and 2 are both short by two: the higher level goes
        sim.Apply(first);
        sim.Sync();
        w::PoolPass second = sim.Pass();
        CHECK(second.logins.size() == 1 && second.logins[0] == 2);   // band 0 is still short by two: its highest level
        sim.Apply(second);
        sim.Sync();
        w::PoolPass third = sim.Pass();
        CHECK(third.logins.size() == 1 && third.logins[0] == 1);

        // Highest level inside one band, and among equal levels the bot offline longest (the old ring's order).
        Sim inside;
        inside.settings.targetOnline = 24;
        inside.now = 100 * kMinute;
        inside.bots.push_back(OfflineBot(11, 1, 1 * kMinute));   // the oldest of the three, but the lowest level
        inside.bots.push_back(OfflineBot(12, 1, 5 * kMinute));
        inside.bots.push_back(OfflineBot(13, 1, 2 * kMinute));
        inside.bots[0].level = 6;                                // band 1
        inside.bots[1].level = 10;                               // band 1
        inside.bots[2].level = 10;                               // band 1
        w::PoolPass const pick = inside.Pass();
        CHECK(pick.logins.size() == 1 && pick.logins[0] == 13);  // the level-6 bot waits; of the two level 10s, guid 13 is older
    }

    // (8i) the cap's band is a hard cap: two level-60 slots for a target of 20 at 10 percent, the rest stay offline as the
    // reserve; a level-60 slot only opens while its band is short.
    {
        Sim sim;
        sim.settings.targetOnline = 20;
        sim.now = 100 * kMinute;
        for (std::uint64_t i = 0; i < 3; ++i)
        {
            sim.bots.push_back(OfflineBot(20 + i, 1, 0));
            sim.bots.back().level = 60;
        }
        sim.bots.push_back(OfflineBot(10, 2, 0));
        sim.bots.back().level = 30;                              // band 5, target 2
        w::PoolPass cap1 = sim.Pass();
        CHECK(cap1.logins.size() == 1 && cap1.logins[0] == 20);  // the cap's band ties with band 5 and its level wins
        sim.Apply(cap1);
        sim.Sync();
        w::PoolPass cap2 = sim.Pass();
        CHECK(cap2.logins.size() == 1 && cap2.logins[0] == 10);  // band 5 is now the largest shortfall
        sim.Apply(cap2);
        sim.Sync();
        w::PoolPass cap3 = sim.Pass();
        CHECK(cap3.logins.size() == 1 && cap3.logins[0] == 21);  // the cap's second slot
        sim.Apply(cap3);
        sim.Sync();
        sim.bots.push_back(OfflineBot(30, 1, 0));
        sim.bots.back().level = 1;                               // a low band: the overflow goes there, not to a third 60
        w::PoolPass after = sim.Pass();
        CHECK(after.logins.size() == 1 && after.logins[0] == 30);
        // With only max-level characters left and the cap full, the pass plans no login at all.
        std::vector<w::PoolBotState> capped;
        for (std::uint64_t i = 0; i < 3; ++i)
        {
            w::PoolBotState bot = OfflineBot(40 + i, 1, 0);
            bot.level = 60;
            capped.push_back(bot);
        }
        std::vector<w::PoolBotState> twoOnline{ OnlineBot(50, 1, 0, sim.now + kMinute), OnlineBot(51, 1, 0, sim.now + kMinute) };
        twoOnline[0].level = 60;
        twoOnline[1].level = 60;
        capped.insert(capped.end(), twoOnline.begin(), twoOnline.end());
        w::PoolPassInput in;
        in.bots = &capped;
        in.settings = sim.settings;
        in.nowMs = sim.now;
        in.recentLoginsMs = nullptr;
        in.recentLogoutsMs = nullptr;
        CHECK(w::PlanPoolPass(in).logins.empty());

        // 0 percent: no max-level character ever logs in; 100 percent: only they do.
        w::PoolScheduleSettings noSixty = sim.settings;
        noSixty.levelLadderMaxLevelSharePct = 0;
        in.settings = noSixty;
        CHECK(w::PlanPoolPass(in).logins.empty());
        w::PoolScheduleSettings allSixty = sim.settings;
        allSixty.levelLadderMaxLevelSharePct = 100;
        in.settings = allSixty;
        w::PoolPass const only = w::PlanPoolPass(in);
        CHECK(only.logins.size() == 1);                          // the cap's band holds the whole target
        CHECK(only.counted == 2);                                // and the target is already met by the two online
    }

    // (8j) the ladder off is the round's old order: the faction's bot offline longest, whatever its level.
    {
        Sim sim;
        sim.settings.targetOnline = 24;
        sim.settings.levelLadder = false;
        sim.now = 100 * kMinute;
        sim.bots.push_back(OfflineBot(1, 1, 0));
        sim.bots.push_back(OfflineBot(2, 1, 50 * kMinute));
        sim.bots[0].level = 3;
        sim.bots[1].level = 60;
        w::PoolPass const pick = sim.Pass();
        CHECK(pick.logins.size() == 1 && pick.logins[0] == 1);
    }

    // (8k) the level the host feeds the track: a known level is clamped into 1..maxLevel, an unknown one keeps the track's.
    {
        w::PoolScheduleSettings settings;
        w::PoolBotState bot = OfflineBot(1, 1, 0);
        bot.level = 7;
        w::PoolBotFacts facts;
        facts.level = 42;
        w::PoolSyncBot(bot, facts, settings, 0, [] { return 1u; });
        CHECK(bot.level == 42);
        facts.level = 0;
        w::PoolSyncBot(bot, facts, settings, 0, [] { return 1u; });
        CHECK(bot.level == 42);
        facts.level = 200;
        w::PoolSyncBot(bot, facts, settings, 0, [] { return 1u; });
        CHECK(bot.level == 60);
    }

    // (9) the pass over a 2000-bot pool stays under 0.5 ms (the spec's budget: one pass per second).
    {
        Sim sim;
        sim.settings.targetOnline = 1000;
        sim.settings.loginsPerMinute = 20;
        sim.now = 1000 * kMinute;
        for (std::uint64_t i = 0; i < 2000; ++i)
        {
            if (i % 2)
                sim.bots.push_back(OnlineBot(i + 1, std::uint8_t(1 + i % 8), sim.now - std::int64_t(i) * kSecond, sim.now + std::int64_t(i % 600) * kSecond));
            else
                sim.bots.push_back(OfflineBot(i + 1, std::uint8_t(1 + i % 8), sim.now - std::int64_t(i % 300) * kSecond));
        }
        for (std::uint32_t roll = 0; roll < 20; ++roll)
            sim.logins.push_back(sim.now - std::int64_t(roll) * kSecond);
        double worst = 0.0;
        double total = 0.0;
        int const passes = 200;
        for (int i = 0; i < passes; ++i)
        {
            auto const started = std::chrono::steady_clock::now();
            w::PoolPass pass = sim.Pass();
            double const us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - started).count();
            total += us;
            worst = us > worst ? us : worst;
            CHECK(pass.counted == 1000);
            // Apply only the logouts (the state stays the same shape; the loop measures the planning, not the world).
            for (std::uint64_t const guid : pass.logouts)
                if (w::PoolBotState* bot = sim.Find(guid))
                    bot->phase = w::PoolPhase::Leaving;
            sim.now += kSecond;
        }
        double const average = total / passes;
        std::printf("pass cost over 2000 bots: average %.1f us, worst %.1f us\n", average, worst);
        CHECK(average < 500.0);
    }

    // (10) the same pass over a larger, mixed-level pool with the ladder actually running: the online count is under the
    // target and the login window is empty, so every pass builds the band view and picks a login. (9) never did: at its
    // target the policy returns before the ladder.
    {
        Sim sim;
        sim.settings.targetOnline = 1500;
        sim.settings.loginsPerMinute = 20;
        sim.now = 2000 * kMinute;
        for (std::uint64_t i = 0; i < 2500; ++i)
        {
            w::PoolBotState bot = i % 2
                ? OnlineBot(i + 1, std::uint8_t(1 + i % 8), sim.now - std::int64_t(i) * kSecond, sim.now + std::int64_t(i % 600) * kSecond)
                : OfflineBot(i + 1, std::uint8_t(1 + i % 8), sim.now - 10 * kMinute - std::int64_t(i) * kSecond);
            bot.level = std::uint8_t(1 + i % 60);       // every band, both ends of the cap included
            sim.bots.push_back(bot);
        }
        w::PoolLadderView const view = w::PoolLadderViewOf(sim.bots, sim.settings);
        CHECK(w::PoolLadderBandCount(view.maxLevel, view.bandSize) > 1);
        double worst = 0.0;
        double total = 0.0;
        int const passes = 200;
        for (int i = 0; i < passes; ++i)
        {
            auto const started = std::chrono::steady_clock::now();
            w::PoolPass pass = sim.Pass();
            double const us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - started).count();
            total += us;
            worst = us > worst ? us : worst;
            CHECK(pass.counted == 1250);
            CHECK(pass.logins.size() == 1);            // allowed == 1: the ladder's view and pick ran
            if (!pass.logins.empty())
            {
                w::PoolBotState const* pick = sim.Find(pass.logins[0]);
                CHECK(pick != nullptr && w::PoolLadderEligible(*pick, view));
            }
            sim.now += kSecond;                        // the login is not applied: the same shape every pass
        }
        double const average = total / passes;
        std::printf("pass cost over 2500 bots with the ladder: average %.1f us, worst %.1f us\n", average, worst);
        CHECK(average < 500.0);
    }

    if (g_fail)
    {
        std::printf("RESULT: FAIL (%d of %d checks)\n", g_fail, g_checks);
        return 1;
    }
    std::printf("RESULT: PASS (%d checks)\n", g_checks);
    return 0;
}
