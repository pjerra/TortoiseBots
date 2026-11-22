#pragma once

// BG1 (BATTLEGROUNDS-SPEC.md, decisions D5/D6): the timings of an invitation and of leaving a match, as pure decisions. The host
// (forever/src/host/bg/TbBgQueue.cpp) owns the clock, the core status and the calls; this header owns when.
//
// The rules:
//  - an invited bot accepts after a random 2-8 s (never at once: the core's own invitations have a 90 s window), after the host
//    re-checked its eligibility; a bot that became ineligible declines;
//  - a free bot enters only an instance a human is INSIDE: while no human has entered it waits, and it declines at the end of
//    the core's invite window (BgHumanEntryInviteAnswer). An invited human is not presence: his invite can lapse;
//  - a bound bot follows the same rule for its own human: it accepts only after that human has ENTERED the instance (not merely
//    been invited) and declines at the end of the core's invite window if the human never gets in;
//  - a free bot NEVER leaves a match that is running (WAIT_JOIN or IN_PROGRESS) by itself: the pool scheduler holds its session
//    instead. The one exception is Bg.MaxMinutes (45): a bot still inside after it leaves without the Deserter debuff, so a pool
//    stop can finish;
//  - once the match is over (STATUS_WAIT_LEAVE) a free bot leaves after a random 10-30 s (the core casts no Deserter in that
//    state), so the instance can close;
//  - a bound bot is not sent anywhere by the free-bot rule: its human's leave takes it out (the core does that itself). If the
//    human never entered, BgMasterLeave takes the bot out after kBoundMasterGraceMs so it cannot idle in a match alone.

#include <cstdint>

namespace TortoiseBots
{
namespace bg
{

// The core's invite-accept window is 90 s on the pinned core (Battleground.h: INVITE_ACCEPT_WAIT_TIME = 90000); the host reads
// the queue's own RemoveInviteTime when it has it, this is the fallback. 2-8 s is the spec's roll.
inline constexpr std::uint32_t kAcceptMinMs = 2000;
inline constexpr std::uint32_t kAcceptMaxMs = 8000;
inline constexpr std::uint32_t kInviteWindowMs = 90000;
// The core removes a lapsed invitation itself at the end of the window; this margin answers first, so the bot declines on its own
// terms instead of losing the queue slot to the core's timeout.
inline constexpr std::uint32_t kInviteDeclineMarginMs = 1000;
// STATUS_WAIT_LEAVE: the match is decided, nothing waits on the bots.
inline constexpr std::uint32_t kMatchOverLeaveMinMs = 10000;
inline constexpr std::uint32_t kMatchOverLeaveMaxMs = 30000;
// Bg.MaxMinutes: the longest a free bot stays inside one instance.
inline constexpr std::uint32_t kMaxStayDefaultMinutes = 45;

// A uniform length in [minMs, maxMs] from a 32-bit roll (0 is the lower end, 0xffffffff the upper one).
inline std::uint32_t BgRollDelayMs(std::uint32_t minMs, std::uint32_t maxMs, std::uint32_t roll)
{
    if (maxMs < minMs)
    {
        std::uint32_t const swap = minMs;
        minMs = maxMs;
        maxMs = swap;
    }
    std::uint64_t const span = std::uint64_t(maxMs - minMs) + 1;
    std::uint64_t const offset = (std::uint64_t(roll) * span) >> 32;
    return minMs + std::uint32_t(offset < span ? offset : span - 1);
}

inline std::uint32_t BgAcceptDelayMs(std::uint32_t roll)
{
    return BgRollDelayMs(kAcceptMinMs, kAcceptMaxMs, roll);
}

inline std::uint32_t BgMatchOverLeaveDelayMs(std::uint32_t roll)
{
    return BgRollDelayMs(kMatchOverLeaveMinMs, kMatchOverLeaveMaxMs, roll);
}

enum class BgPortAction : std::uint8_t
{
    None,       // nothing to answer yet
    Accept,     // HandleBattleFieldPortOpcode, AcceptedInvite = true
    Decline,    // HandleBattleFieldPortOpcode, AcceptedInvite = false: the queue leaves with it
};

// The answer to an invitation, once its delay has run out. An invited bot that is no longer eligible declines instead of taking a
// place it cannot use.
inline BgPortAction BgInviteAnswer(bool stillEligible)
{
    return stillEligible ? BgPortAction::Accept : BgPortAction::Decline;
}

enum class BgInviteReply : std::uint8_t
{
    Wait = 0,   // keep the invitation pending
    Accept,
    Decline,
};

// A free bot enters only a match a human is INSIDE (D4.3: no bot-only matches). When the instance holds no human, the human who
// created it may be a second from his world-port ack; the bot waits for that entry instead of giving the slot back, and gives it
// back at the end of the core's invite window if no human arrives. `usable` is the eligibility (and the caps) at this moment.
//
// A bound bot enters only the match its human has ENTERED (not merely been invited to), so its accept waits for that; if the
// human has not entered by the end of the core's invite window the invitation is declined instead of taking a place alone.
//
// Both rules are one decision: an invitation is not presence. A human merely invited can let his invite lapse, leaving the bot
// alone in an instance, so `humanInside` is the caller's answer to "has the human this bot needs entered the instance" (any
// human for a free bot, its master for a bound one) and there is no "invited" input to accept on.
inline BgInviteReply BgHumanEntryInviteAnswer(bool usable, bool humanInside, bool acceptDue, bool deadlinePassed)
{
    if (!usable)
        return acceptDue ? BgInviteReply::Decline : BgInviteReply::Wait;
    if (humanInside)
        return acceptDue ? BgInviteReply::Accept : BgInviteReply::Wait;
    return deadlinePassed ? BgInviteReply::Decline : BgInviteReply::Wait;
}

// The core's RemoveInviteTime is a game-time stamp; answer one margin before it so the core's own removal never wins the race.
inline bool BgInviteDeadlinePassed(std::uint32_t nowMs, std::uint32_t deadlineMs)
{
    return deadlineMs && nowMs + kInviteDeclineMarginMs >= deadlineMs;
}

enum class BgLeaveAction : std::uint8_t
{
    Stay,
    LeaveNoDeserter,   // Bg.MaxMinutes: LeaveBattleground(true, true)
    LeaveMatchOver,    // STATUS_WAIT_LEAVE: LeaveBattleground(), the core casts no Deserter in that state
};

// `status` is the core's BattlegroundStatus as an integer (0 none, 1 wait queue, 2 wait join, 3 in progress, 4 wait leave).
// `insideMs` is how long the bot has been in this instance; `maxStayMs` is Bg.MaxMinutes.
inline BgLeaveAction BgLeaveDecision(bool freeBot, std::int32_t status, bool matchOverDelayDone, std::uint32_t insideMs,
                                     std::uint32_t maxStayMs)
{
    if (!freeBot)
        return BgLeaveAction::Stay;              // a bound bot goes only when its human's leave takes it (or BgMasterLeave)
    // BattlegroundStatus, Battleground.h:167-171: NONE 0, WAIT_QUEUE 1, WAIT_JOIN 2, IN_PROGRESS 3, WAIT_LEAVE 4. The pure header
    // mirrors the numbers instead of including the core; the wiring test compares them with the core enum.
    constexpr std::int32_t statusWaitLeave = 4;
    constexpr std::int32_t statusInProgress = 3;
    constexpr std::int32_t statusWaitJoin = 2;
    if (status == statusWaitLeave)
        return matchOverDelayDone ? BgLeaveAction::LeaveMatchOver : BgLeaveAction::Stay;
    if (status == statusInProgress || status == statusWaitJoin)
        return insideMs >= maxStayMs ? BgLeaveAction::LeaveNoDeserter : BgLeaveAction::Stay;
    return BgLeaveAction::Stay;
}

// A bound bot follows its human, and only the core's leave of the human takes it out. When the human never entered (an AFK accept
// that lapsed, a logout, a cancel after the bot accepted) that leave never comes, so a bot whose master has not been in its
// instance for this long leaves without the Deserter debuff.
inline constexpr std::uint32_t kBoundMasterGraceMs = 60000;

inline bool BgMasterLeave(bool freeBot, bool masterHere, std::uint32_t masterAwayMs)
{
    return !freeBot && !masterHere && masterAwayMs >= kBoundMasterGraceMs;
}

} // namespace bg
} // namespace TortoiseBots
