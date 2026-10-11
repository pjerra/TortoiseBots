#pragma once

// Hunter dead-zone kit-switch hysteresis (pool 1v1 fix, Oct 2026).
// Pure decision rules, no core includes: the triggers in
// strategy/hunter/HunterTriggers.h translate game state into these plain
// inputs, so the rules stay testable in tools/test_hunter_switch_policy.cpp
// without the server.
//
// Donor comparison (mod-playerbots, read-only reference):
// SwitchToMeleeTrigger::IsActive is victim-on-bot AND distance<=8, and
// SwitchToRangedTrigger::IsActive is victim-off-bot AND distance>8
// (HunterTriggers.cpp:112-126 - both are ANDs, not ORs). Neither has a level
// gate. The melee rule below is that exact donor AND plus the local no-ammo
// branch; the ranged rule drops the distance half (see ShouldSwitchToRanged)
// and keeps the rooted branch, so a mob pacing the 8 yd line cannot flip the kit
// every tick (fresh pool: inter-switch p50 13 s, 41% within 10 s, each flip
// rebuilding the combat engine's trigger graph). The fresh-pool run is what
// exposed the old extra branches as flap sources:
//  - tooSlowToFollow answered 83% of melee->ranged pairs at dist < 5 yd: at
//    pool levels a walking mob is always "slower than half run speed", so the
//    OR fired right after every melee trade, before the bot moved a step.
//  - fastOrFinisher's pet-gone half let SwitchToMelee fire at 5-8 yd on slow
//    mobs (76% of ranged->melee pairs landed at 5-8 yd), re-trading for a mob
//    the step-back already handles. Both extras are gone; the signatures keep
//    the old parameters (marked void) so the triggers keep compiling.
//
// Fix round (Oct 2026): with the ranged kit back as soon as the mob is off
// the bot, a pet-held mob inside the dead zone sent the hunter into the
// generic flee every tick. For pool bots that flee is a MoveChase at flee
// range, which the core's chase generator turns into "stay put" while the
// mob stands still within 1.4x that range - so the flee re-fired without
// moving and the hunter neither shot nor meleed (flee 57-63 % of combat
// samples). The dead zone now gets ONE step-out per mob (below), the melee
// kit if that step does not get the hunter out, and a minimum dwell on the
// kit so aggro trading inside 8 yd cannot rebuild strategies every tick.

#include <cstdint>

namespace ai
{
    // Glued inside the shot's dead zone: the donor's own melee edge, and the
    // same band the GenericHunterStrategy step-back ("enemy too close for
    // auto shot" -> disengage/flee) uses, so the switch and the step-back
    // agree on where the dead zone ends.
    inline constexpr float kHunterMeleeGlueYd = 8.0f;
    // Return margin past the dead zone for future hysteresis use; the rules
    // below switch on the donor's 8 yd.
    inline constexpr float kHunterRangedReturnYd = 10.0f;

    // Trades into melee once the mob is really on the bot (victim set) and
    // glued inside the dead zone: the donor's AND (victim AND dist<=8) plus
    // mobile (not rooted - a rooted mob at 8 yd is shot, not charged).
    // The old fast-or-finisher OR is gone: it let melee fire at 5-8 yd on
    // slow mobs (76% of ranged->melee pairs landed at 5-8 yd), re-trading
    // for a mob the step-back already handles. A slow mob glued on the bot
    // is what Wing Clip and the step-back are for. Out of ammo always
    // trades: there is nothing to shoot with.
    // Fix round: a failed dead-zone step-out (stuckInDeadZone, see
    // DecideHunterDeadZoneStep) also trades into melee, whoever the mob is
    // on: the hunter cannot shoot from there, so Raptor Strike / Wing Clip
    // beat standing still. Every trade except out-of-ammo and that stuck case
    // waits for the kit dwell (dwellElapsed); the stuck case is already
    // limited to once per step-out episode (kHunterStepOutRetryMs).
    inline bool ShouldSwitchToMelee(bool hasRangedKit, bool victimOnBot, bool immobilized,
        bool fastOrFinisher, float distanceYd, bool hasAmmo,
        bool stuckInDeadZone = false, bool dwellElapsed = true)
    {
        if (!hasAmmo)
            return true;
        (void)fastOrFinisher;
        if (!hasRangedKit)
            return false;
        if (stuckInDeadZone)
            return true;
        return dwellElapsed && victimOnBot && !immobilized &&
            distanceYd <= kHunterMeleeGlueYd;
    }

    // Hands the ranged kit back as soon as the mob is off the bot (pet, tank
    // or anyone else holds it) or rooted (shot from where the bot stands), at
    // any distance. The old "and past 8 yd" half trapped the hunter: the close
    // kit's own "reach melee" keeps it inside 8 yd, so once the pet took the
    // mob back the hunter meleed it for the rest of the fight. The ranged
    // kit's dead-zone step-back then moves it out to shoot (mod-playerbots
    // 79bd428 has no melee kit at all; its hunter only steps out). Victim on
    // vs off the bot is the hysteresis: melee needs it on, ranged needs it
    // off, so distance pacing cannot flip the kit. The old slow-or-far OR
    // stays gone (it flipped 83% of melee->ranged pairs at dist < 5 yd
    // while the mob was still on the bot). Below level 10 this is what
    // restores ranged after a melee trade.
    //
    // Fix round: victim on/off alone is no hysteresis inside 8 yd (REVIEW
    // finding 4): the two rules are complements there, so pet Growl and the
    // hunter's own shots trading aggro flipped the kit every few seconds,
    // each flip rebuilding the combat engine. The kit now has to have been
    // held kHunterKitMinDwellMs first (dwellElapsed). stepOutBlocked keeps
    // the melee kit while this mob's failed step-out still stands and the
    // mob is still inside the dead zone, so the stuck trade above cannot
    // flip straight back into the same dead zone.
    inline bool ShouldSwitchToRanged(bool hasCloseKit, bool targetOffBot, bool immobilized,
        bool tooSlowToFollow, float distanceYd, bool hasAmmo,
        bool stepOutBlocked = false, bool dwellElapsed = true)
    {
        if (!(hasCloseKit && hasAmmo))
            return false;
        if (!dwellElapsed)
            return false;
        if (immobilized)
            return true;
        (void)tooSlowToFollow;
        (void)distanceYd;
        return targetOffBot && !stepOutBlocked;
    }

    // Minimum time a kit is held before the next victim- or root-driven
    // switch (REVIEW finding 4 asked for 3-5 s).
    inline constexpr uint32_t kHunterKitMinDwellMs = 4000;
    // One dead-zone step-out: the move gets this long to carry the hunter
    // out before it counts as failed.
    inline constexpr uint32_t kHunterStepOutWindowMs = 3000;
    // A step-out episode is per mob and lasts this long: inside it the hunter
    // does not step out again for that mob (a failed or undone step means
    // melee instead), after it a fresh step-out is allowed.
    inline constexpr uint32_t kHunterStepOutRetryMs = 10000;
    // The step lands this far past the dead zone edge.
    inline constexpr float kHunterStepOutMarginYd = 3.0f;
    // Step length bounds: never a shuffle, never a run across the map.
    inline constexpr float kHunterStepOutMinYd = 4.0f;
    inline constexpr float kHunterStepOutMaxYd = 15.0f;

    // Per-bot bookkeeping for the rules above (the "hunter kit state" value).
    // Times are WorldTimer ms, compared with wrap-safe uint32 subtraction;
    // 0 means "never".
    struct HunterKitState
    {
        uint32_t lastKitSwitchMs = 0;
        uint64_t stepOutTarget = 0;
        uint32_t stepOutStartMs = 0;
    };

    inline bool HunterKitDwellElapsed(HunterKitState const& state, uint32_t nowMs)
    {
        return state.lastKitSwitchMs == 0 || nowMs - state.lastKitSwitchMs >= kHunterKitMinDwellMs;
    }

    inline void NoteHunterKitSwitch(HunterKitState& state, uint32_t nowMs)
    {
        state.lastKitSwitchMs = nowMs ? nowMs : 1;
    }

    inline bool HunterStepOutEpisodeOpen(HunterKitState const& state, uint64_t target, uint32_t nowMs)
    {
        return state.stepOutStartMs != 0 && state.stepOutTarget == target &&
            nowMs - state.stepOutStartMs < kHunterStepOutRetryMs;
    }

    inline void BeginHunterStepOut(HunterKitState& state, uint64_t target, uint32_t nowMs)
    {
        state.stepOutTarget = target;
        state.stepOutStartMs = nowMs ? nowMs : 1;
    }

    // Close kit: hold melee while this mob's step-out episode stands and the
    // mob is still inside the dead zone (see ShouldSwitchToRanged).
    inline bool HunterStepOutBlocked(HunterKitState const& state, uint64_t target, uint32_t nowMs,
        bool inDeadZone)
    {
        return inDeadZone && HunterStepOutEpisodeOpen(state, target, nowMs);
    }

    // Ranged kit, mob inside the shot's dead zone:
    //  None    - not in the dead zone, or the mob is on the hunter (the
    //            melee switch / kite flee own that case).
    //  StepOut - first time for this mob in the episode: step out once.
    //  Hold    - the step is under way (moving, inside the window): let it
    //            finish, do not re-dispatch.
    //  Stuck   - the step did not get the hunter out (no spot, arrived still
    //            inside, or the window ran out): melee instead of fleeing again.
    enum class HunterDeadZoneStep { None, StepOut, Hold, Stuck };

    inline HunterDeadZoneStep DecideHunterDeadZoneStep(HunterKitState const& state, uint64_t target,
        uint32_t nowMs, bool inDeadZone, bool victimOnBot, bool moving)
    {
        if (!inDeadZone || victimOnBot)
            return HunterDeadZoneStep::None;
        if (!HunterStepOutEpisodeOpen(state, target, nowMs))
            return HunterDeadZoneStep::StepOut;
        if (moving && nowMs - state.stepOutStartMs < kHunterStepOutWindowMs)
            return HunterDeadZoneStep::Hold;
        return HunterDeadZoneStep::Stuck;
    }

    // Straight-back step that lands kHunterStepOutMarginYd past the dead
    // zone edge from edgeDistYd, clamped to [kHunterStepOutMinYd, kHunterStepOutMaxYd].
    inline float HunterStepOutLength(float edgeDistYd, float minRangeYd)
    {
        float const need = minRangeYd + kHunterStepOutMarginYd - edgeDistYd;
        if (need < kHunterStepOutMinYd)
            return kHunterStepOutMinYd;
        if (need > kHunterStepOutMaxYd)
            return kHunterStepOutMaxYd;
        return need;
    }
}
