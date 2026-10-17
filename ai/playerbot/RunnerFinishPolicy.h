#pragma once

// RunnerFinishPolicy - pure rules for finishing a mob that runs away at low
// health (a "runner").
//
// A creature of the party's fight that flees (FleeingMovementGenerator, the
// timed flee, or the flee-for-assistance walk the core starts at low health in
// Creature::DoFleeToGetAssistance) used to be dropped: the DPS pick follows the
// tank's victim first, the tank stays on the pack, and the threat value calls a
// fleeing mob "no threat". In ZF the stair-wave mobs (7787) fled at 2-20 % to a
// spot 15 yd off, the waves never ended and the event failed after 600 s.
//
// Donor behaviour (mod-playerbots, read-only reference,
// src/Ai/Base/Value/SnareTargetValue.cpp and ThreatValues.cpp): a fleeing
// attacker in spell range is the first snare target, and a fleeing mob counts
// as no-threat so DPS may hit it freely; its lifetime-bucket DPS pick then ranks
// a nearly dead mob in range first. mod-playerbots has no dedicated runner
// finisher; the explicit "finish it" priority, the leash and the hold rule below
// are reimplemented here for 1.12 groups.
//
// Unit* glue lives in DpsTargetValue.cpp; everything here is plain data so it
// is tested on its own (tools/test_runner_finish_policy.cpp).

namespace ai
{
    // Below this health a running mob is finished rather than left to fetch help.
    constexpr float kRunnerFinishHealthPct = 30.0f;

    // How far a melee bot whose gap closer is castable now (Intercept: in
    // combat, Berserker Stance, rage, off cooldown) goes after a runner: the
    // gap closer's own reach. Short on purpose so the
    // party does not chase into the next pack.
    constexpr float kRunnerMeleeLeash = 25.0f;

    // Slack a ranged bot gets beyond its spell range when it is free to move a
    // few steps (not holding).
    constexpr float kRunnerRangedSlack = 5.0f;

    struct RunnerFacts
    {
        bool running = false;      // fleeing state or flee / seek-assistance motion
        bool feared = false;       // a fear aura: our own CC, not a runner
        bool ccTarget = false;     // the bot's CC target or a held CC mark
        bool alive = false;
        bool inPartyFight = false; // in the bot's attackers list
        float healthPct = 100.0f;
        float distance = 0.0f;     // bot to mob, yards
    };

    struct RunnerChaser
    {
        bool healer = false;       // healers keep healing
        bool tank = false;         // the tank stays on the pack
        bool ranged = false;
        bool hasGapCloser = false; // melee only: castable on this mob right now
        bool holding = false;      // "stay" in combat: the run leader's hold
        float spellRange = 30.0f;
        float meleeReach = 5.0f;
    };

    // The mob is a runner worth finishing, whoever looks at it.
    inline bool IsFinishableRunner(RunnerFacts const& mob)
    {
        return mob.alive && mob.inPartyFight && mob.running && !mob.feared && !mob.ccTarget &&
            mob.healthPct < kRunnerFinishHealthPct;
    }

    // How far this bot may go after a runner (0 = not at all).
    inline float RunnerLeash(RunnerChaser const& bot)
    {
        if (bot.healer || bot.tank)
            return 0.0f;
        if (bot.ranged)
            return bot.holding ? bot.spellRange : bot.spellRange + kRunnerRangedSlack;
        // A held melee bot does not leave its spot; one without a gap closer
        // only takes a runner it can already hit.
        if (bot.holding || !bot.hasGapCloser)
            return bot.meleeReach;
        return kRunnerMeleeLeash;
    }

    // This bot should go after this runner now.
    inline bool ShouldFinishRunner(RunnerFacts const& mob, RunnerChaser const& bot)
    {
        if (!IsFinishableRunner(mob))
            return false;
        float const leash = RunnerLeash(bot);
        return leash > 0.0f && mob.distance <= leash;
    }

    // Among two finishable runners: the bot's current target first (never drop
    // a nearly dead runner for a fresh one), then the lower health, then the
    // nearer one.
    inline bool RunnerBetter(RunnerFacts const& a, bool aIsCurrent, RunnerFacts const& b, bool bIsCurrent)
    {
        if (aIsCurrent != bIsCurrent)
            return aIsCurrent;
        if (a.healthPct != b.healthPct)
            return a.healthPct < b.healthPct;
        return a.distance < b.distance;
    }
}
