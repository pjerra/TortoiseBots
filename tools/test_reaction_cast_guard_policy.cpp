// Standalone regression test for the reaction cast guard policy
// (ai/playerbot/ReactionCastGuardPolicy.h): AiPlayerbot.ReactionHealCastGuardPct stands
// `boss tactics seek` and `raid spread` down for a healer's heal that has not got far, and
// nothing else changes.
//
// The case that fails on the old rule: `guard 1, a yielding reaction, a healer, a heal 4 %
// in` must NOT break the cast (old code broke it on ShouldReactionInterruptCast alone).
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_reaction_cast_guard_policy.cpp -o /tmp/test_reaction_cast_guard
//   /tmp/test_reaction_cast_guard

#include "../ai/playerbot/ReactionCastGuardPolicy.h"

#include <cstdio>
#include <cstdlib>

using ai::ReactionCastBreakDue;
using ai::ReactionCastPct;

static int checks = 0;
#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        std::exit(1); \
    } \
} while (0)

int main()
{
    const bool kYields = true;        // boss tactics seek / raid spread
    const bool kDoesNotYield = false; // every emergency
    const bool kHealer = true;
    const bool kNotHealer = false;
    const bool kHeal = true;          // Flash Heal, Regrowth, Holy Light, Renew
    const bool kNotHeal = false;      // Smite

    // The old rule: ShouldReactionInterruptCast alone. The walk's median cut is at 4 %.
    const bool oldRule = true;

    // 1. The case this change exists for: a healer 4 % into a heal keeps it, with guard 1. The old rule broke it.
    CHECK(oldRule);
    CHECK(!ReactionCastBreakDue(true, kYields, 1, kHealer, kHeal, 4));

    // The guard percent is the only part of the cast a yielding reaction may still cut, from its start.
    // Guard 50: 4 % still breaks (the opening), 60 % keeps (the heal is too far along to drop).
    CHECK(ReactionCastBreakDue(true, kYields, 50, kHealer, kHeal, 4));
    CHECK(!ReactionCastBreakDue(true, kYields, 50, kHealer, kHeal, 60));

    // 2. Each input alone restores today's behaviour (the cast breaks).
    CHECK(ReactionCastBreakDue(true, kYields, 0, kHealer, kHeal, 4));         // key off (shipped default)
    CHECK(ReactionCastBreakDue(true, kDoesNotYield, 1, kHealer, kHeal, 4));   // an emergency (freeze, flee, dispel...)
    CHECK(ReactionCastBreakDue(true, kYields, 1, kNotHealer, kHeal, 4));      // a damage dealer's heal
    CHECK(ReactionCastBreakDue(true, kYields, 1, kHealer, kNotHeal, 4));      // a healer's Smite
    CHECK(ReactionCastBreakDue(true, kYields, 1, kHealer, kHeal, -1));        // no generic cast to read

    // 3. A reaction that does not want the cast broken never breaks it, whatever the guard says.
    CHECK(!ReactionCastBreakDue(false, kYields, 0, kHealer, kHeal, 4));
    CHECK(!ReactionCastBreakDue(false, kDoesNotYield, 50, kNotHealer, kNotHeal, -1));

    // 4. Boundaries: the guard cuts strictly below its percent, so 100 keeps the very end and 1 keeps a 1 % cast.
    CHECK(ReactionCastBreakDue(true, kYields, 100, kHealer, kHeal, 99));
    CHECK(!ReactionCastBreakDue(true, kYields, 100, kHealer, kHeal, 100));
    CHECK(ReactionCastBreakDue(true, kYields, 1, kHealer, kHeal, 0));
    CHECK(!ReactionCastBreakDue(true, kYields, 1, kHealer, kHeal, 1));
    // A negative guard is off, like 0.
    CHECK(ReactionCastBreakDue(true, kYields, -5, kHealer, kHeal, 4));

    // 5. The cast progress read (Spell::GetCastTime / Spell::GetCastedTime), the walk's cast_pct.
    CHECK(ReactionCastPct(1500, 1500) == 0);   // just started
    CHECK(ReactionCastPct(1500, 750) == 50);
    CHECK(ReactionCastPct(1500, 0) == 100);    // done
    CHECK(ReactionCastPct(2500, 2400) == 4);   // the median cut
    CHECK(ReactionCastPct(1500, 2000) == 0);   // a countdown above the total reads as a fresh cast
    CHECK(ReactionCastPct(0, 0) == -1);        // no total: an instant, nothing to lose
    CHECK(ReactionCastPct(-1, 10) == -1);

    // The two together: a Flash Heal (2.5 s) cut 100 ms in is a 4 % cast and keeps casting under guard 1 (the walk
    // value: the median cut sits at 4 %). Under guard 20 only the first 20 % may still be cut, so 10 % breaks and
    // 25 % keeps.
    CHECK(!ReactionCastBreakDue(true, kYields, 1, kHealer, kHeal, ReactionCastPct(2500, 2400)));
    CHECK(ReactionCastBreakDue(true, kYields, 20, kHealer, kHeal, ReactionCastPct(2500, 2250)));
    CHECK(!ReactionCastBreakDue(true, kYields, 20, kHealer, kHeal, ReactionCastPct(2500, 1875)));

    std::printf("OK: %d checks (ReactionCastBreakDue, ReactionCastPct)\n", checks);
    return 0;
}
