// Standalone regression test for the seek's heal wait (ai/playerbot/ReactionCastGuardPolicy.h):
// AiPlayerbot.ReactionHealSeekWaitMs stands `boss tactics seek`'s walk down for a healer's heal that is
// nearly through, while the cast's remainder, the walk there and the margin still fit before the siphon.
//
// The case that fails on the old code (no wait at all, the walk cuts every cast):
// - key 2000, a healer, a Flash Heal with 900 ms of 1500 left, the siphon due in 44 s, a 300 ms walk and the
//   gather (no cloud): the walk must stand down.
// The case that fails on the first version of the wait (the field does not exist, so the input is ignored):
// - the same pass with an emergency move or escape due (EmergencyMoveDue: the avoid-aoe condition and every trigger
//   in kSeekHealWaitEmergencyMoves): the walk must go, so the escape the seek outranks can hold its own tick.
// The case that fails on the next version (the wait's end was recorded only on a pass that did not wait, so a second
// heal already under way started a fresh wait):
// - a wait that ends at 900 ms is closed at 900, not at the pass that notices (1101), and heal 2 on that pass gets no
//   new wait until 5 s since 900 have passed.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_seek_heal_wait_policy.cpp -o /tmp/test_seek_heal_wait
//   /tmp/test_seek_heal_wait

#include "../ai/playerbot/ReactionCastGuardPolicy.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
// gather (no cloud), no hazard escape and no earlier wait.
static SeekHealWaitIn Base()
{
    SeekHealWaitIn in;
    in.waitMaxMs = 2000;
    in.botIsHealer = true;
    in.cast = nullptr;
    in.dueInMs = 44000;
    in.travelMs = 300;
    in.cloudLeftMs = -1;
    in.hazardDue = false;
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
        SeekHealWaitIn in = Base(); // a due hazard escape: `avoid aoe` comes first
        in.cast = &flashHeal;
        in.hazardDue = true;
        CHECK(!SeekHealWaitDue(in));
        // Even while a wait that already started runs: the emergency still cuts it short.
        in.inWait = true;
        CHECK(!SeekHealWaitDue(in));
        // And it changes nothing when no wait would be due anyway.
        in.hazardDue = false;
        in.inWait = false;
        CHECK(SeekHealWaitDue(in));
    }
    {
        SeekHealWaitIn in = Base(); // the field's default is no hazard
        in.cast = &flashHeal;
        CHECK(!SeekHealWaitIn{}.hazardDue);
        CHECK(SeekHealWaitDue(in));
    }
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

    // 2b. Every emergency move the wait refuses to stand in front of (EmergencyMoveDue, MovementActions.cpp): one
    // policy case per listed trigger kind. The predicate's own wiring is pinned in test_seek_heal_wait_wiring.py;
    // here each kind's due state, the one bool the rule takes, has to cut the wait.
    for (ai::SeekHealWaitEmergencyMove const& move : ai::kSeekHealWaitEmergencyMoves)
    {
        SeekHealWaitIn in = Base();
        in.cast = &flashHeal;
        in.hazardDue = true;
        CHECK(!SeekHealWaitDue(in));
        CHECK(move.trigger && *move.trigger);
        CHECK(move.action && *move.action);
    }
    {
        SeekHealWaitIn in = Base(); // the avoid-aoe condition itself, which has no row in the list
        in.cast = &flashHeal;
        in.hazardDue = true;
        CHECK(!SeekHealWaitDue(in));
    }
    {
        // The list itself: one entry per kind the predicate asks. Dropping a trigger from the predicate's list
        // leaves it unasked and turns this red.
        auto const listed = [](char const* trigger) {
            for (ai::SeekHealWaitEmergencyMove const& move : ai::kSeekHealWaitEmergencyMoves)
            {
                if (std::strcmp(move.trigger, trigger) == 0)
                    return true;
            }
            return false;
        };
        char const* const expected[] = {
            "has area debuff", "immune boss victim", "environmental hazard nearby", "raid bomb debuff",
            "four horsemen mark", "dragon breath risk", "boss tactics flee", "boss tactics avoid cast",
            "razorgore cone", "razorgore ranged", "golemagg splash", "magmadar lava bomb", "geddon inferno",
            "onyxia deep breath", "grobbulus injection ranged", "solnius emerald rot", "viscidus melee danger",
            "viscidus toxin nearby",
            // Two rows share the bomb trigger: the universal runout and Vael's own flee.
            "raid bomb debuff",
        };
        CHECK(sizeof(ai::kSeekHealWaitEmergencyMoves) / sizeof(ai::kSeekHealWaitEmergencyMoves[0]) ==
              sizeof(expected) / sizeof(expected[0]));
        for (char const* trigger : expected)
            CHECK(listed(trigger));
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

    // 3b. A wait that ran out ends at its own untilMs, not at the pass that notices, and a second heal already under
    // way on that pass cannot start a fresh wait inside the rewait spacing (the old caller read "no earlier wait").
    {
        std::uint32_t untilMs = 900; // heal 1: started at 0 with 900 ms left
        bool active = true;
        std::uint32_t endMs = 0;
        bool ended = false;
        // At the end of the slack the wait still stands, and its end does not move.
        ai::SettleSeekHealWait(1100, untilMs, active, endMs, ended);
        CHECK(active);
        CHECK(!ended);
        CHECK(untilMs == 900);
        // One ms later it is closed at the cast's end, not at this pass's clock.
        ai::SettleSeekHealWait(1101, untilMs, active, endMs, ended);
        CHECK(!active);
        CHECK(ended);
        CHECK(endMs == 900);
        // Heal 2 is already casting on that pass: only 201 ms since the end, so no new wait.
        ReactionCastRead const heal2 = Prepare(1500, 900);
        SeekHealWaitIn in = Base();
        in.cast = &heal2;
        in.msSinceWaitEnd = 1101 - endMs;
        CHECK(!SeekHealWaitDue(in));
        // Once the rewait spacing is over, heal 2 earns its own wait.
        in.msSinceWaitEnd = ai::kSeekHealRewaitMs;
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
