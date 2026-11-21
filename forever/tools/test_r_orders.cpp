// Pure unit tests for W9 guild & guild-order review fixes (PR #97).
// Build & run from repo root:
//   g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/test_r_orders.cpp -o /tmp/test_r_orders && /tmp/test_r_orders

#include "forever/src/host/TbHostGuildPure.h"

#include <cstdio>
#include <string>

static int g_failures = 0;
#define CHECK(cond, msg)                                                                 \
    do                                                                                   \
    {                                                                                    \
        if (!(cond))                                                                     \
        {                                                                                \
            ++g_failures;                                                                \
            std::printf("FAIL: %s (line %d: %s)\n", (msg), __LINE__, #cond);             \
        }                                                                                \
    } while (0)

int main()
{
    using namespace tbhost::guild;

    // Finding 2: guild bank deposit must use NULL_SLOT (255) and reject swaps.
    CHECK(kGuildBankAutoSlot == 255, "F2: kGuildBankAutoSlot must be NULL_SLOT (255)");
    CHECK(VerifyBankDeposit(10, 5, 4234, 4234), "F2: partial/same-item leave allowed");
    CHECK(VerifyBankDeposit(10, 5, 4234, 0), "F2: empty slot after deposit allowed");
    CHECK(!VerifyBankDeposit(10, 0, 4234, 19019), "F2: swapped foreign item in bag slot must fail");

    // Finding 3a: stack delivery must split to exact missing count.
    {
        DeliveryStackPlan const p = PlanDeliveryStack(200, 5);
        CHECK(p.needSplit && p.deliverCount == 5 && p.splitCount == 5,
            "F3a: stack of 200 with missing=5 must split to 5");
        DeliveryStackPlan const exact = PlanDeliveryStack(5, 5);
        CHECK(!exact.needSplit && exact.deliverCount == 5,
            "F3a: stack <= missing needs no split");
    }

    // Finding 3b: never mail a real player (non-bot) by default on console-created orders,
    // unless the requester is the receiver who asked. Personal craft orders (guild 0)
    // always mail the ordering player, even when the console defaulted it to the bot itself.
    {
        MailRecipientDecision const consoleToRealLeader = DecideMailRecipient(101, 101, 5, false, 7);
        CHECK(!consoleToRealLeader.allowed && std::string(consoleToRealLeader.reason) == "real-player-mail-refused",
            "F3b: console order must refuse mailing real-player guild leader");

        MailRecipientDecision const consoleToBotLeader = DecideMailRecipient(101, 101, 102, true, 7);
        CHECK(consoleToBotLeader.allowed && consoleToBotLeader.targetGuidLow == 102,
            "F3b: console order may mail bot guild leader");

        MailRecipientDecision const playerRequested = DecideMailRecipient(101, 5, 5, false, 7);
        CHECK(playerRequested.allowed && playerRequested.targetGuidLow == 5,
            "F3b: player-created order may mail the requester who asked");

        // Live item 10: console-created personal order (giver defaulted to the bot
        // itself) must mail the bot instead of refusing for no recipient.
        MailRecipientDecision const personalSelf = DecideMailRecipient(2846, 2846, 0, true, 0);
        CHECK(personalSelf.allowed && personalSelf.targetGuidLow == 2846,
            "fix2: personal order must mail the giver even when it is the bot itself");

        MailRecipientDecision const personalPlayer = DecideMailRecipient(101, 5, 0, false, 0);
        CHECK(personalPlayer.allowed && personalPlayer.targetGuidLow == 5,
            "fix2: personal order must mail the ordering player");

        MailRecipientDecision const personalNone = DecideMailRecipient(101, 0, 0, false, 0);
        CHECK(!personalNone.allowed && std::string(personalNone.reason) == "no-recipient",
            "fix2: personal order without giver must still refuse");
    }

    // Finding 4: AH gold cap applies across the whole order, not per purchase.
    {
        CHECK(EffectiveAhBudget(true, 1000, 800, 50000) == 200,
            "F4: remaining budget must subtract ahGoldSpent");
        CHECK(EffectiveAhBudget(true, 1000, 1000, 50000) == 0,
            "F4: exhausted gold cap must yield 0 budget");
        CHECK(EffectiveAhBudget(false, 0, 0, 50000) == 50000,
            "F4: uncapped order uses bot money");
    }

    // Finding 9: TickBotOrder throttles per-bot and deduplicates identical failure logs.
    {
        OrderTickThrottleState t;
        CHECK(t.ShouldRun(1000), "F9: initial tick runs");
        CHECK(t.RecordResult(1000, false, false, "no-listing"), "F9: first failure logs");
        CHECK(!t.ShouldRun(1050), "F9: throttled immediately after failure");
        CHECK(!t.ShouldRun(5000), "F9: still backed off at 5s after failure");
        CHECK(t.ShouldRun(11000), "F9: runs again after 10s backoff");
        CHECK(!t.RecordResult(11000, false, false, "no-listing"), "F9: identical failure deduped");
        CHECK(t.RecordResult(21000, false, false, "no-affordable-listing"), "F9: changed failure logs");
        CHECK(!t.RecordResult(31000, true, false, "bought"), "F9: success clears failure");
        CHECK(!t.ShouldRun(31500), "F9: short throttle after success");
        CHECK(t.ShouldRun(33000), "F9: runs again 2s after success");
    }

    if (g_failures > 0)
    {
        std::printf("FAIL: test_r_orders (%d failed)\n", g_failures);
        return 1;
    }
    std::printf("PASS: test_r_orders (all checks passed, 0 failed)\n");
    return 0;
}
