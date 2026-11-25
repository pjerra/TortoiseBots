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
// the track a vanished row must not lose, and the pass cost (under 0.5 ms over 2000 bots).
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

    if (g_fail)
    {
        std::printf("RESULT: FAIL (%d of %d checks)\n", g_fail, g_checks);
        return 1;
    }
    std::printf("RESULT: PASS (%d checks)\n", g_checks);
    return 0;
}
