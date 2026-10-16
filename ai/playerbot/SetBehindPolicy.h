#pragma once

#include <cstdint>

// Pure policy for "set behind" (issue #911): a hired assassination rogue sat
// in "set behind" about 9.7 yd from its target for ~90 s with full energy.
// The action fired from up to 15 yd at ACTION_HIGH and re-issued a move
// without checking headway, so it starved "reach melee" (lower priority) and
// a blocked "behind" spot (the hunter's pet held the mob) was never given up.
//
// Donor shape (mod-playerbots SetBehindTargetAction): positioning only
// happens once the bot is in melee range; closing the distance belongs to
// the reach action. On top of that a bounded per-target streak: when the bot
// has been trying to get behind the same target for SET_BEHIND_GIVE_UP_MS
// without getting there, set-behind stays off for that target and the bot
// attacks from where it stands (the front is fine, only the backstab-class
// openers lose their bonus). The world-facing parts (target guid, melee
// reach test, clock) live in SetBehindTargetAction.
namespace ai
{
    // How long one streak of "not behind, in melee reach" may last before
    // set-behind gives up on that target.
    constexpr uint32_t SET_BEHIND_GIVE_UP_MS = 6000u;

    // A streak is the run of consecutive polls for one target. A pause longer
    // than this (the bot was behind, or out of reach, so the trigger was off)
    // starts a fresh streak instead of counting the idle time.
    constexpr uint32_t SET_BEHIND_STREAK_GAP_MS = 3000u;

    struct SetBehindStreak
    {
        uint64_t targetGuid = 0;
        uint32_t startMs = 0;
        uint32_t lastPollMs = 0;
    };

    // Update the streak for this poll. A new target, or a pause longer than
    // the gap, restarts it. Unsigned subtraction keeps it correct across the
    // 32-bit millisecond clock wrapping.
    inline void SetBehindStreakPoll(SetBehindStreak& s, uint64_t targetGuid, uint32_t nowMs)
    {
        if (s.targetGuid != targetGuid || nowMs - s.lastPollMs > SET_BEHIND_STREAK_GAP_MS)
        {
            s.targetGuid = targetGuid;
            s.startMs = nowMs;
        }
        s.lastPollMs = nowMs;
    }

    inline bool SetBehindStreakExpired(SetBehindStreak const& s, uint32_t nowMs)
    {
        return nowMs - s.startMs >= SET_BEHIND_GIVE_UP_MS;
    }

    // Set-behind is useful only inside melee reach of a target the bot is not
    // behind, and only until the streak for that target expires.
    inline bool SetBehindUseful(bool inMeleeReach, bool alreadyBehind, bool streakExpired)
    {
        return inMeleeReach && !alreadyBehind && !streakExpired;
    }
}
