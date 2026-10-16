#include "../ai/playerbot/SetBehindPolicy.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using namespace ai;

int main()
{
    std::cout << "Starting TortoiseBots set-behind policy tests...\n";

    // Issue #911: outside melee reach the action stays off, so "reach melee"
    // (lower priority) closes the distance instead of being starved.
    CHECK(!SetBehindUseful(false, false, false));
    CHECK(SetBehindUseful(true, false, false));
    CHECK(!SetBehindUseful(true, true, false));
    CHECK(!SetBehindUseful(true, false, true));
    std::cout << "  [PASS] useful only in melee reach, not behind, streak alive\n";

    // A streak that keeps polling one target expires after the give-up window.
    SetBehindStreak s;
    SetBehindStreakPoll(s, 7u, 1000u);
    CHECK(!SetBehindStreakExpired(s, 1000u));
    for (uint32_t t = 1500u; t < 1000u + SET_BEHIND_GIVE_UP_MS; t += 500u)
    {
        SetBehindStreakPoll(s, 7u, t);
        CHECK(!SetBehindStreakExpired(s, t));
    }
    uint32_t const expireAt = 1000u + SET_BEHIND_GIVE_UP_MS;
    SetBehindStreakPoll(s, 7u, expireAt);
    CHECK(SetBehindStreakExpired(s, expireAt));
    // Still expired while the bot keeps polling the same target.
    SetBehindStreakPoll(s, 7u, expireAt + 500u);
    CHECK(SetBehindStreakExpired(s, expireAt + 500u));
    std::cout << "  [PASS] give-up after the window, stays given up\n";

    // A new target restarts the streak.
    SetBehindStreakPoll(s, 8u, expireAt + 1000u);
    CHECK(!SetBehindStreakExpired(s, expireAt + 1000u));
    std::cout << "  [PASS] new target restarts the streak\n";

    // A pause longer than the gap (bot was behind or out of reach) restarts it.
    SetBehindStreak p;
    SetBehindStreakPoll(p, 5u, 0u);
    SetBehindStreakPoll(p, 5u, 4000u);
    SetBehindStreakPoll(p, 5u, 5900u);
    CHECK(SetBehindStreakExpired(p, 6100u) == false); // gap restarted at 4000
    SetBehindStreakPoll(p, 5u, 6100u);
    SetBehindStreakPoll(p, 5u, 7000u);
    SetBehindStreakPoll(p, 5u, 8000u);
    SetBehindStreakPoll(p, 5u, 9000u);
    SetBehindStreakPoll(p, 5u, 10000u);
    CHECK(SetBehindStreakExpired(p, 10000u));
    std::cout << "  [PASS] idle gap restarts the streak\n";

    // Polls stop while the mob attacks the bot or moves (isUseful returns
    // before polling). After 3+ s without polls the streak is fresh, so a
    // taunted-then-released rogue gets the full window again.
    SetBehindStreak a;
    SetBehindStreakPoll(a, 4u, 0u);
    SetBehindStreakPoll(a, 4u, 5000u);
    CHECK(!SetBehindStreakExpired(a, 5000u));
    for (uint32_t t = 5500u; t < 5000u + SET_BEHIND_GIVE_UP_MS; t += 500u)
        SetBehindStreakPoll(a, 4u, t);
    CHECK(!SetBehindStreakExpired(a, 10900u));
    uint32_t const resume = 20000u; // long aggro pause, no polls
    SetBehindStreakPoll(a, 4u, resume);
    CHECK(!SetBehindStreakExpired(a, resume));
    std::cout << "  [PASS] paused polls (aggro on the bot) start a fresh streak\n";

    // The 32-bit millisecond clock wrapping does not trip or hide the expiry.
    SetBehindStreak w;
    uint32_t const base = 0xFFFFFFFFu - 2000u;
    SetBehindStreakPoll(w, 9u, base);
    SetBehindStreakPoll(w, 9u, base + 2500u);
    CHECK(!SetBehindStreakExpired(w, base + 2500u));
    SetBehindStreakPoll(w, 9u, base + 5000u);
    SetBehindStreakPoll(w, 9u, base + 6000u);
    CHECK(SetBehindStreakExpired(w, base + 6000u));
    std::cout << "  [PASS] clock wrap safe\n";

    std::cout << "All set-behind policy tests passed.\n";
    return 0;
}
