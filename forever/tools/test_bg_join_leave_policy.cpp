// Standalone test of BG1's invitation and leave timings (ai/playerbot/policy/bg/BgJoinLeavePolicy.h):
//   g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/test_bg_join_leave_policy.cpp -o /tmp/test_bg_join_leave_policy && /tmp/test_bg_join_leave_policy
//
// The spec's case (BATTLEGROUNDS-SPEC.md section 4, test_bg_join_leave_policy): a free bot is never told to leave an IN_PROGRESS
// match (lease end, pool stop); WAIT_LEAVE gives 10-30 s; MaxMinutes gives leave-without-deserter; invite to a bot that entered a
// run is declined.
#include "ai/playerbot/policy/bg/BgJoinLeavePolicy.h"

#include <cstdio>

namespace bg = TortoiseBots::bg;

static int g_checks = 0, g_fail = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

// BattlegroundStatus, Battleground.h:167-171
constexpr std::int32_t kNone = 0;
constexpr std::int32_t kWaitJoin = 2;
constexpr std::int32_t kInProgress = 3;
constexpr std::int32_t kWaitLeave = 4;

int main()
{
    // The accept roll covers 2-8 s for both ends of the 32-bit range and never steps outside it.
    CHECK(bg::BgAcceptDelayMs(0) == bg::kAcceptMinMs);
    CHECK(bg::BgAcceptDelayMs(0xffffffffu) == bg::kAcceptMaxMs);
    CHECK(bg::BgAcceptDelayMs(0x80000000u) >= bg::kAcceptMinMs && bg::BgAcceptDelayMs(0x80000000u) <= bg::kAcceptMaxMs);
    for (std::uint32_t i = 0; i < 4096; ++i)
    {
        std::uint32_t const ms = bg::BgAcceptDelayMs(i * 1048573u);
        CHECK(ms >= 2000 && ms <= 8000);
    }
    // The spec's 2-8 s, not the core's 90 s invite window: no accepted invite can expire unused.
    CHECK(bg::kAcceptMaxMs < bg::kInviteWindowMs);

    // WAIT_LEAVE gives 10-30 s.
    CHECK(bg::BgMatchOverLeaveDelayMs(0) == bg::kMatchOverLeaveMinMs);
    CHECK(bg::BgMatchOverLeaveDelayMs(0xffffffffu) == bg::kMatchOverLeaveMaxMs);
    for (std::uint32_t i = 0; i < 4096; ++i)
    {
        std::uint32_t const ms = bg::BgMatchOverLeaveDelayMs(i * 2654435761u);
        CHECK(ms >= 10000 && ms <= 30000);
    }
    // A reversed range is swapped, never read as a value outside the two ends.
    CHECK(bg::BgRollDelayMs(5000, 1000, 0) == 1000);
    CHECK(bg::BgRollDelayMs(5000, 1000, 0xffffffffu) == 5000);

    // An invitation: accept when still eligible, decline when a run (or a master) took the bot meanwhile.
    CHECK(bg::BgInviteAnswer(true) == bg::BgPortAction::Accept);
    CHECK(bg::BgInviteAnswer(false) == bg::BgPortAction::Decline);

    // An invitation is answered on human presence, the same rule for a free bot (any human inside its instance) and a bound bot
    // (its own human): an invited human is not inside, so the bot waits for the entry and declines at the core's window;
    // an AFK human's lapsed invite must not leave the bots alone in the instance.
    CHECK(bg::BgHumanEntryInviteAnswer(true, true, true, false) == bg::BgInviteReply::Accept);
    CHECK(bg::BgHumanEntryInviteAnswer(true, true, false, false) == bg::BgInviteReply::Wait);
    CHECK(bg::BgHumanEntryInviteAnswer(true, false, false, false) == bg::BgInviteReply::Wait);
    CHECK(bg::BgHumanEntryInviteAnswer(true, false, true, false) == bg::BgInviteReply::Wait);
    CHECK(bg::BgHumanEntryInviteAnswer(true, false, true, true) == bg::BgInviteReply::Decline);
    CHECK(bg::BgHumanEntryInviteAnswer(true, false, false, true) == bg::BgInviteReply::Decline);
    CHECK(bg::BgHumanEntryInviteAnswer(false, false, true, false) == bg::BgInviteReply::Decline);
    CHECK(bg::BgHumanEntryInviteAnswer(false, true, false, false) == bg::BgInviteReply::Wait);
    CHECK(bg::BgHumanEntryInviteAnswer(false, true, true, false) == bg::BgInviteReply::Decline);

    // The core's invite window is 90 s (INVITE_ACCEPT_WAIT_TIME), and the module answers one margin before the core's own removal.
    CHECK(bg::kInviteWindowMs == 90000);
    CHECK(!bg::BgInviteDeadlinePassed(0, 0));                       // no deadline: never
    CHECK(!bg::BgInviteDeadlinePassed(88999, 90000));
    CHECK(bg::BgInviteDeadlinePassed(89000, 90000));

    // D5: a free bot is never told to leave a running match by itself, however long its lease ran or a stop has waited.
    std::uint32_t const maxStay = bg::kMaxStayDefaultMinutes * 60u * 1000u;
    CHECK(bg::BgLeaveDecision(true, kInProgress, false, 0, maxStay) == bg::BgLeaveAction::Stay);
    CHECK(bg::BgLeaveDecision(true, kInProgress, false, 30u * 60u * 1000u, maxStay) == bg::BgLeaveAction::Stay);
    CHECK(bg::BgLeaveDecision(true, kWaitJoin, false, 30u * 60u * 1000u, maxStay) == bg::BgLeaveAction::Stay);
    CHECK(bg::BgLeaveDecision(true, kNone, false, 30u * 60u * 1000u, maxStay) == bg::BgLeaveAction::Stay);

    // MaxMinutes: leave without the Deserter debuff, at the limit and not before.
    CHECK(bg::BgLeaveDecision(true, kInProgress, false, maxStay - 1, maxStay) == bg::BgLeaveAction::Stay);
    CHECK(bg::BgLeaveDecision(true, kInProgress, false, maxStay, maxStay) == bg::BgLeaveAction::LeaveNoDeserter);
    CHECK(bg::BgLeaveDecision(true, kWaitJoin, false, maxStay + 60000, maxStay) == bg::BgLeaveAction::LeaveNoDeserter);

    // A match that is over: the 10-30 s roll decides, and the plain leave (no Deserter in that state) is the action.
    CHECK(bg::BgLeaveDecision(true, kWaitLeave, false, 60000, maxStay) == bg::BgLeaveAction::Stay);
    CHECK(bg::BgLeaveDecision(true, kWaitLeave, true, 60000, maxStay) == bg::BgLeaveAction::LeaveMatchOver);

    // A bound bot is never sent anywhere by this rule: its human's leave takes it out.
    CHECK(bg::BgLeaveDecision(false, kWaitLeave, true, maxStay + 1, maxStay) == bg::BgLeaveAction::Stay);
    CHECK(bg::BgLeaveDecision(false, kInProgress, false, maxStay + 1, maxStay) == bg::BgLeaveAction::Stay);

    // A bound bot whose human never entered leaves without the Deserter debuff, after the grace and only then: the core's own
    // leave of the human never comes for a lapsed invite, a logout or a cancel after the bot accepted.
    CHECK(bg::kBoundMasterGraceMs == 60000);
    CHECK(!bg::BgMasterLeave(false, true, 0));
    CHECK(!bg::BgMasterLeave(false, true, bg::kBoundMasterGraceMs));
    CHECK(!bg::BgMasterLeave(false, false, bg::kBoundMasterGraceMs - 1));
    CHECK(bg::BgMasterLeave(false, false, bg::kBoundMasterGraceMs));
    CHECK(bg::BgMasterLeave(false, false, bg::kBoundMasterGraceMs + 60000));
    CHECK(!bg::BgMasterLeave(true, false, bg::kBoundMasterGraceMs * 10));   // a free bot is D5's, never this rule

    // The defaults are the spec's.
    CHECK(bg::kMaxStayDefaultMinutes == 45);

    std::printf("%d checks, %d failed\n", g_checks, g_fail);
    return g_fail == 0 ? 0 : 1;
}
