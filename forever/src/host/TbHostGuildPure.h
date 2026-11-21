#pragma once

#include <algorithm>
#include <cstdint>
#include <string>

namespace tbhost
{
namespace guild
{

// Automatic guild bank deposit slot (NULL_SLOT in ForeverCore = 255).
inline constexpr uint8_t kGuildBankAutoSlot = 255;

// Verify that a guild bank deposit moved items out of the bot's bag and did not
// swap a foreign item back into the source bag slot.
inline bool VerifyBankDeposit(uint32_t beforeCount, uint32_t afterCount, uint32_t expectedItemId, uint32_t slotAfterEntry)
{
    if (afterCount >= beforeCount)
        return false;
    return slotAfterEntry == 0 || slotAfterEntry == expectedItemId;
}

struct DeliveryStackPlan
{
    uint32_t deliverCount = 0;
    bool needSplit = false;
    uint32_t splitCount = 0;
};

inline DeliveryStackPlan PlanDeliveryStack(uint32_t stackCount, uint32_t missingCount)
{
    if (stackCount == 0 || missingCount == 0)
        return {};
    if (stackCount > missingCount)
        return { missingCount, true, missingCount };
    return { stackCount, false, 0 };
}

struct MailRecipientDecision
{
    bool allowed = false;
    uint32_t targetGuidLow = 0;
    char const* reason = "";
};

// Never mail a real player (non-bot) by default (e.g. console-created orders
// that fall back to the guild leader), unless that player is the requester
// who explicitly gave the order.
inline MailRecipientDecision DecideMailRecipient(uint32_t botGuidLow, uint32_t giverGuidLow, uint32_t leaderGuidLow, bool targetIsBot)
{
    if (giverGuidLow != 0 && giverGuidLow != botGuidLow)
        return { true, giverGuidLow, "ok" };

    if (!leaderGuidLow || leaderGuidLow == botGuidLow)
        return { false, 0, "no-recipient" };

    if (!targetIsBot)
        return { false, 0, "real-player-mail-refused" };

    return { true, leaderGuidLow, "ok" };
}

struct OrderGoldBudget
{
    bool capped = false;
    uint32_t remainingCopper = 0;
};

inline OrderGoldBudget RemainingOrderGoldCap(bool hasGoldCap, uint32_t goldCap, uint32_t ahGoldSpent)
{
    if (!hasGoldCap || goldCap == 0)
        return { false, 0 };
    if (ahGoldSpent >= goldCap)
        return { true, 0 };
    return { true, goldCap - ahGoldSpent };
}

inline uint64_t EffectiveAhBudget(bool hasGoldCap, uint32_t goldCap, uint32_t ahGoldSpent, uint64_t botMoney)
{
    OrderGoldBudget const b = RemainingOrderGoldCap(hasGoldCap, goldCap, ahGoldSpent);
    if (!b.capped)
        return botMoney;
    return std::min<uint64_t>(botMoney, b.remainingCopper);
}

struct OrderTickThrottleState
{
    static constexpr uint64_t kActiveIntervalMs = 2000;
    static constexpr uint64_t kFailureBackoffMs = 10000;

    uint64_t nextTryMs = 0;
    std::string lastFailure;

    bool ShouldRun(uint64_t nowMs) const
    {
        return nowMs >= nextTryMs;
    }

    // Updates nextTryMs and returns true if a failure should be logged at INFO
    // (only when the failure reason changed since the last logged failure).
    bool RecordResult(uint64_t nowMs, bool succeeded, bool inProgress, std::string const& failureReason)
    {
        nextTryMs = nowMs + ((succeeded || inProgress) ? kActiveIntervalMs : kFailureBackoffMs);
        if (succeeded || inProgress)
        {
            if (succeeded)
                lastFailure.clear();
            return false;
        }
        if (failureReason == lastFailure)
            return false;
        lastFailure = failureReason;
        return true;
    }
};

} // namespace guild
} // namespace tbhost
