#include "../ai/playerbot/HunterSwitchPolicy.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>

#define CHECK(x) do { \
    if (!(x)) { \
        std::cerr << "Assertion failed at line " << __LINE__ << ": " #x "\n"; \
        std::exit(1); \
    } \
} while (0)

using ai::kHunterMeleeGlueYd;
using ai::kHunterRangedReturnYd;
using ai::ShouldSwitchToMelee;
using ai::ShouldSwitchToRanged;
using ai::HunterKitState;
using ai::HunterDeadZoneStep;
using ai::DecideHunterDeadZoneStep;
using ai::HunterKitDwellElapsed;
using ai::NoteHunterKitSwitch;
using ai::BeginHunterStepOut;
using ai::HunterStepOutBlocked;
using ai::HunterStepOutLength;
using ai::kHunterKitMinDwellMs;
using ai::kHunterStepOutWindowMs;
using ai::kHunterStepOutRetryMs;

int main()
{
    std::cout << "Starting TortoiseBots hunter-switch tests...\n";

    // Edges: the donor's own 8 yd melee edge (melee <= 8; ranged returns on
    // victim-off, not distance); the 10 yd constant is kept for future use.
    CHECK(kHunterMeleeGlueYd == 8.0f);
    CHECK(kHunterRangedReturnYd == 10.0f);

    // Melee side: the donor AND (victim on bot, dist <= 8) plus mobile.
    {
        // Glued victim at the edge: trade (fast or slow - the speed OR is
        // gone, a slow mob glued on the bot is still a dead-zone mob).
        CHECK(ShouldSwitchToMelee(true, true, false, true, 8.0f, true));
        CHECK(ShouldSwitchToMelee(true, true, false, false, 8.0f, true));
        // Just outside the edge: hold ranged even with the victim on bot.
        CHECK(!ShouldSwitchToMelee(true, true, false, true, 8.1f, true));
        // Victim off the bot or rooted: hold (rooted is shot, not charged).
        CHECK(!ShouldSwitchToMelee(true, false, false, true, 3.0f, true));
        CHECK(!ShouldSwitchToMelee(true, true, true, true, 3.0f, true));
        // No ranged kit to trade from: hold.
        CHECK(!ShouldSwitchToMelee(false, true, false, true, 3.0f, true));
        // Out of ammo always trades: nothing to shoot with.
        CHECK(ShouldSwitchToMelee(true, false, true, false, 30.0f, false));
        std::cout << "  [PASS] melee trades only when the victim is glued on the bot\n";
    }

    // Ranged side: off the bot (pet/tank holds it) at any distance, or rooted.
    {
        // Off the bot past the edge: back to ranged.
        CHECK(ShouldSwitchToRanged(true, true, false, false, 8.1f, true));
        // Off the bot inside the dead zone: back to ranged too (the close
        // kit's reach melee would otherwise keep the hunter meleeing a
        // pet-held mob for the whole fight); the ranged kit steps out.
        CHECK(ShouldSwitchToRanged(true, true, false, false, 8.0f, true));
        CHECK(ShouldSwitchToRanged(true, true, false, false, 2.0f, true));
        // Victim still on the bot: hold even far out (melee finishes it).
        CHECK(!ShouldSwitchToRanged(true, false, false, false, 30.0f, true));
        // Rooted: ranged at any distance, even glued on the bot.
        CHECK(ShouldSwitchToRanged(true, false, true, false, 3.0f, true));
        // Merely slow no longer answers at melee distance (the flap source:
        // 83% of melee->ranged pairs landed at dist < 5 yd on this branch).
        CHECK(!ShouldSwitchToRanged(true, false, false, true, 3.0f, true));
        // No close kit to trade back from, or no ammo: hold.
        CHECK(!ShouldSwitchToRanged(false, true, false, false, 30.0f, true));
        CHECK(!ShouldSwitchToRanged(true, true, false, false, 30.0f, false));
        std::cout << "  [PASS] ranged returns once the mob is off the bot, or rooted\n";
    }

    // Kit dwell (REVIEW finding 4): victim on/off alone flips the kit on
    // every aggro trade inside 8 yd; a kit is now held 4 s first.
    {
        CHECK(kHunterKitMinDwellMs == 4000u);
        HunterKitState state;
        // Never switched: free to switch.
        CHECK(HunterKitDwellElapsed(state, 500u));
        NoteHunterKitSwitch(state, 100000u);
        CHECK(!HunterKitDwellElapsed(state, 100000u));
        CHECK(!HunterKitDwellElapsed(state, 103999u));
        CHECK(HunterKitDwellElapsed(state, 104000u));
        // Wrap-safe: a switch just before the ms timer wraps.
        NoteHunterKitSwitch(state, 0xFFFFFF00u);
        CHECK(!HunterKitDwellElapsed(state, 0x00000100u));
        CHECK(HunterKitDwellElapsed(state, 0x00000F00u + 4000u));

        bool const dwellNotElapsed = false;
        // Aggro ping-pong inside 8 yd: the pet Growls the mob off a hunter
        // that just traded into melee -> hold melee until the dwell is up.
        CHECK(!ShouldSwitchToRanged(true, true, false, false, 3.0f, true, false, dwellNotElapsed));
        CHECK(ShouldSwitchToRanged(true, true, false, false, 3.0f, true, false, true));
        // ...and a mob running back onto a hunter that just went ranged.
        CHECK(!ShouldSwitchToMelee(true, true, false, true, 3.0f, true, false, dwellNotElapsed));
        CHECK(ShouldSwitchToMelee(true, true, false, true, 3.0f, true, false, true));
        // Rooted waits for the dwell too; out of ammo never waits.
        CHECK(!ShouldSwitchToRanged(true, false, true, false, 3.0f, true, false, dwellNotElapsed));
        CHECK(ShouldSwitchToMelee(true, false, false, false, 30.0f, false, false, dwellNotElapsed));
        std::cout << "  [PASS] kit switches wait for the 4 s dwell (out of ammo excepted)\n";
    }

    // Dead-zone step-out: one step per mob, hold while it runs, melee when it
    // fails - never the old flee every tick.
    {
        uint64_t const mob = 0xF130001234000042ull;
        uint64_t const otherMob = 0xF130001234000043ull;
        HunterKitState state;
        uint32_t const t0 = 200000u;
        // Out of the dead zone, or the mob is on the hunter: not this row.
        CHECK(DecideHunterDeadZoneStep(state, mob, t0, false, false, false) == HunterDeadZoneStep::None);
        CHECK(DecideHunterDeadZoneStep(state, mob, t0, true, true, false) == HunterDeadZoneStep::None);
        // Pet-held mob in the dead zone, no episode yet: step out.
        CHECK(DecideHunterDeadZoneStep(state, mob, t0, true, false, false) == HunterDeadZoneStep::StepOut);
        BeginHunterStepOut(state, mob, t0);
        // Step under way: hold, do not re-dispatch.
        CHECK(DecideHunterDeadZoneStep(state, mob, t0 + 500u, true, false, true) == HunterDeadZoneStep::Hold);
        // Stopped but still inside (no spot, or the mob came along): melee.
        CHECK(DecideHunterDeadZoneStep(state, mob, t0 + 500u, true, false, false) == HunterDeadZoneStep::Stuck);
        // Still moving after the window: give up, melee.
        CHECK(DecideHunterDeadZoneStep(state, mob, t0 + kHunterStepOutWindowMs, true, false, true) == HunterDeadZoneStep::Stuck);
        // Got out: nothing to do.
        CHECK(DecideHunterDeadZoneStep(state, mob, t0 + 1000u, false, false, false) == HunterDeadZoneStep::None);
        // Back inside within the episode: melee, not a second step.
        CHECK(DecideHunterDeadZoneStep(state, mob, t0 + 6000u, true, false, false) == HunterDeadZoneStep::Stuck);
        // A different mob, or the episode ran out: a fresh step is allowed.
        CHECK(DecideHunterDeadZoneStep(state, otherMob, t0 + 6000u, true, false, false) == HunterDeadZoneStep::StepOut);
        CHECK(DecideHunterDeadZoneStep(state, mob, t0 + kHunterStepOutRetryMs, true, false, false) == HunterDeadZoneStep::StepOut);

        // Stuck trades into melee whoever holds the mob, even inside the dwell.
        CHECK(ShouldSwitchToMelee(true, false, false, false, 3.0f, true, true, false));
        CHECK(!ShouldSwitchToMelee(false, false, false, false, 3.0f, true, true, true));
        // The close kit then holds while that episode stands and the mob is
        // still in the dead zone; out of it, or after the episode, ranged.
        CHECK(HunterStepOutBlocked(state, mob, t0 + 6000u, true));
        CHECK(!HunterStepOutBlocked(state, mob, t0 + 6000u, false));
        CHECK(!HunterStepOutBlocked(state, mob, t0 + kHunterStepOutRetryMs, true));
        CHECK(!HunterStepOutBlocked(state, otherMob, t0 + 6000u, true));
        CHECK(!ShouldSwitchToRanged(true, true, false, false, 3.0f, true, true, true));
        CHECK(ShouldSwitchToRanged(true, true, false, false, 3.0f, true, false, true));
        // A rooted mob is shot from where the hunter stands regardless.
        CHECK(ShouldSwitchToRanged(true, true, true, false, 3.0f, true, true, true));

        // Step length: lands 3 yd past the edge, clamped to 4..15 yd.
        CHECK(HunterStepOutLength(2.0f, 8.0f) == 9.0f);
        CHECK(HunterStepOutLength(7.5f, 8.0f) == 4.0f);
        CHECK(HunterStepOutLength(0.0f, 20.0f) == 15.0f);
        std::cout << "  [PASS] dead zone: one step-out per mob, then melee instead of flee\n";
    }

    std::cout << "All hunter-switch checks PASSED!\n";
    return 0;
}
