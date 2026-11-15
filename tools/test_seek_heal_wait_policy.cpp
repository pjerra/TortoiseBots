// Standalone regression test for the seek's heal wait (ai/playerbot/ReactionCastGuardPolicy.h):
// AiPlayerbot.ReactionHealSeekWaitMs stands `boss tactics seek`'s walk down for a healer's heal that is
// nearly through, while the cast's remainder, the walk there and the margin still fit before the siphon.
//
// The case that fails on the old code (no wait at all, the walk cuts every cast):
// - key 2000, a healer, a Flash Heal with 900 ms of 1500 left, the siphon due in 44 s, a 300 ms walk and the
//   gather (no cloud): the walk must stand down.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_seek_heal_wait_policy.cpp -o /tmp/test_seek_heal_wait
//   /tmp/test_seek_heal_wait

#include "../ai/playerbot/ReactionCastGuardPolicy.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>

using ai::ReactionCastPct;
using ai::ReactionCastRead;
using ai::SeekHealWaitDue;
using ai::SeekHealWaitIn;
using ai::SeekTravelMs;

static int checks = 0;
#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        std::exit(1); \
    } \
} while (0)

static const std::uint32_t kNoWait = std::numeric_limits<std::uint32_t>::max();

// A cast bar (this core counts a cast-time spell down in PREPARING).
static ReactionCastRead Prepare(std::int32_t totalMs, std::uint32_t remainingMs, bool heal = true)
{
    ReactionCastRead cast;
    cast.state = ai::REACTION_CAST_PREPARING;
    cast.totalMs = totalMs;
    cast.remainingMs = remainingMs;
    cast.isHeal = heal;
    return cast;
}

// One seek pass as the live code builds it: the walk value, a healer, the siphon 44 s away, a 300 ms walk, the
// gather (no cloud) and no earlier wait.
static SeekHealWaitIn Base()
{
    SeekHealWaitIn in;
    in.waitMaxMs = 2000;
    in.botIsHealer = true;
    in.cast = nullptr;
    in.dueInMs = 44000;
    in.travelMs = 300;
    in.cloudLeftMs = -1;
    in.inWait = false;
    in.msSinceWaitEnd = kNoWait;
    return in;
}

int main()
{
    // A Flash Heal (1.5 s total) with 900 ms left: the walk-103 shape (a heal 2 s or less from its end).
    ReactionCastRead const flashHeal = Prepare(1500, 900);

    // 1. The case this change exists for: the walk stands down for the heal.
    {
        SeekHealWaitIn in = Base();
        in.cast = &flashHeal;
        CHECK(ReactionCastPct(flashHeal) == 40);
        CHECK(SeekHealWaitDue(in));
    }

    // 2. Every other input alone puts the walk back on the road (the cast breaks as before).
    {
        SeekHealWaitIn in = Base(); // the key off
        in.cast = &flashHeal;
        in.waitMaxMs = 0;
        CHECK(!SeekHealWaitDue(in));
        in.waitMaxMs = -1;
        CHECK(!SeekHealWaitDue(in));
    }
    {
        SeekHealWaitIn in = Base(); // a damage dealer
        in.cast = &flashHeal;
        in.botIsHealer = false;
        CHECK(!SeekHealWaitDue(in));
    }
    {
        ReactionCastRead const smite = Prepare(1500, 900, false); // a healer's Smite
        SeekHealWaitIn in = Base();
        in.cast = &smite;
        CHECK(!SeekHealWaitDue(in));
    }
    {
        SeekHealWaitIn in = Base(); // no generic slot
        CHECK(!SeekHealWaitDue(in));
    }
    {
        ReactionCastRead const instant = Prepare(0, 0); // nothing on a bar to lose
        SeekHealWaitIn in = Base();
        in.cast = &instant;
        CHECK(!SeekHealWaitDue(in));
    }
    {
        ReactionCastRead const tooEarly = Prepare(2500, 2100); // more left than the key allows
        SeekHealWaitIn in = Base();
        in.cast = &tooEarly;
        CHECK(!SeekHealWaitDue(in));
    }
    {
        SeekHealWaitIn in = Base(); // the siphon's clock is unknown
        in.cast = &flashHeal;
        in.dueInMs = 0;
        CHECK(!SeekHealWaitDue(in));
    }
    {
        SeekHealWaitIn in = Base(); // the siphon is due before the cast, the walk and the margin are done
        in.cast = &flashHeal;
        in.dueInMs = 4000; // 900 + 300 + 3000 = 4200
        CHECK(!SeekHealWaitDue(in));
    }
    {
        SeekHealWaitIn in = Base(); // a cloud that runs out before the wait and the walk pay off
        in.cast = &flashHeal;
        in.cloudLeftMs = 3000;
        CHECK(!SeekHealWaitDue(in));
    }
    {
        SeekHealWaitIn in = Base(); // a new wait 4 s after the last one ended
        in.cast = &flashHeal;
        in.msSinceWaitEnd = 4000;
        CHECK(!SeekHealWaitDue(in));
    }

    // 3. Boundaries.
    {
        SeekHealWaitIn in = Base(); // still inside the wait that is running: no rewait spacing
        in.cast = &flashHeal;
        in.inWait = true;
        in.msSinceWaitEnd = 4000;
        CHECK(SeekHealWaitDue(in));
    }
    {
        SeekHealWaitIn in = Base(); // a rooted bot's walk is unbounded: the walk goes
        in.cast = &flashHeal;
        in.travelMs = SeekTravelMs(2.1f, 0.0f);
        CHECK(in.travelMs == std::numeric_limits<std::uint32_t>::max());
        CHECK(!SeekHealWaitDue(in));
        in.travelMs = SeekTravelMs(2.1f, -3.0f); // a speed read below 0 is as unusable
        CHECK(in.travelMs == std::numeric_limits<std::uint32_t>::max());
        CHECK(!SeekHealWaitDue(in));
    }
    {
        ReactionCastRead const overTotal = Prepare(1500, 5000); // a countdown above the total
        SeekHealWaitIn in = Base();
        in.cast = &overTotal;
        CHECK(ReactionCastPct(overTotal) == 0); // reads as a fresh cast
        in.dueInMs = 4700;                      // 1500 + 300 + 3000 = 4800 uncapped: the total caps the remainder
        CHECK(!SeekHealWaitDue(in));
        in.dueInMs = 4800;
        CHECK(SeekHealWaitDue(in));
    }
    {
        ReactionCastRead const atKey = Prepare(2500, 2000); // exactly the key: at most, not over, so it waits
        SeekHealWaitIn in = Base();
        in.cast = &atKey;
        in.dueInMs = 5300; // 2000 + 300 + 3000
        CHECK(SeekHealWaitDue(in));
        in.dueInMs = 5299;
        CHECK(!SeekHealWaitDue(in));
    }
    {
        SeekHealWaitIn in = Base(); // a cloud with exactly the needed time left still is not gone
        in.cast = &flashHeal;
        in.cloudLeftMs = 4200;
        CHECK(SeekHealWaitDue(in));
        in.cloudLeftMs = 4199;
        CHECK(!SeekHealWaitDue(in));
    }
    {
        SeekHealWaitIn in = Base(); // the rewait ends 5 s after the last wait
        in.cast = &flashHeal;
        in.msSinceWaitEnd = 4999;
        CHECK(!SeekHealWaitDue(in));
        in.msSinceWaitEnd = 5000;
        CHECK(SeekHealWaitDue(in));
    }
    {
        ReactionCastRead const renew = Prepare(1500, 1); // the last tick of a heal is still a heal
        SeekHealWaitIn in = Base();
        in.cast = &renew;
        CHECK(ReactionCastPct(renew) == 99);
        CHECK(SeekHealWaitDue(in));
    }

    // 4. The walk's own length, from the bot's run speed.
    CHECK(SeekTravelMs(0.0f, 7.0f) == 0);
    CHECK(SeekTravelMs(-1.0f, 7.0f) == 0);
    CHECK(SeekTravelMs(7.0f, 7.0f) == 1000);
    CHECK(SeekTravelMs(2.1f, 7.0f) == 300);   // 300 ms, rounded up
    CHECK(SeekTravelMs(0.001f, 7.0f) == 1);   // never 0 for a real distance
    CHECK(SeekTravelMs(1.0f, 3.3f) == 304);   // 303.03 -> 304
    CHECK(SeekTravelMs(1.0e30f, 7.0f) == std::numeric_limits<std::uint32_t>::max());

    // 5. The constants the caller and the walk shape read.
    CHECK(ai::kSeekHealWaitMarginMs == 3000);
    CHECK(ai::kSeekHealRewaitMs == 5000);
    CHECK(ai::kSeekHealWaitEndSlackMs == 200);

    std::printf("OK %d checks\n", checks);
    return 0;
}
