#pragma once

#include <cstdint>

namespace ai
{
    // Loot-roll vote gates, donor parity (E08): mod-playerbots gates auto
    // votes by recipe learnability, duplicate uniques, class tokens and the
    // loot method (LootRollAction::Execute, CanBotUseToken, RollUniqueCheck);
    // ours mapped everything to NEED/GREED/PASS by item usage only, so pool
    // bots GREEDed soulbound recipes they cannot learn, NEEDed duplicate
    // uniques they cannot loot, and voted in FFA/master-loot rolls. Pure
    // data rules here, tested on their own; RollAction::CalculateRollVote
    // supplies bot state. 1.12 adaptation: the core has no DISENCHANT vote
    // (Group::CountRollVote only records NEED/GREED/PASS, anything else is
    // silently dropped and the roll never completes), so DISENCHANT usage
    // stays GREED and enchanters disenchant after winning via the existing
    // "disenchant random item" maintenance - recorded divergence, not a gap.
    //
    // Votes mirror RollVote (PASS=0, NEED=1, GREED=2) without including game
    // headers, keeping the standalone test free of the core.
    enum class LootRollVote : int { Pass = 0, Need = 1, Greed = 2 };

    // Shared rolls only happen under group loot / need-before-greed: under
    // free-for-all there is no roll window and under master loot the master
    // distributes, so any auto vote is PASS. Takes the raw LootMethod
    // (FREE_FOR_ALL=0, MASTER_LOOT=2).
    inline bool LootMethodTakesRolls(int lootMethod)
    {
        return lootMethod != 0 && lootMethod != 2;
    }

    // Recipe rolls (pool bots only at the call site; owned bots keep today's
    // vote): a recipe the bot can learn is NEEDed, a soulbound recipe it
    // cannot learn is PASSED (nobody else can use it), a tradeable one is
    // GREEDed for the auction house. Donor puts all three behind
    // LootRollRecipe=0 (default PASS); the pool NEEDs learnable ones because
    // no player competes for them. Learnability arrives as usage==SKILL,
    // which for ITEM_CLASS_RECIPE already means unlearned + usable
    // (ItemUsageValue::IsItemNeededForSkill).
    inline LootRollVote RecipeRollVote(bool canLearn, bool bindOnPickup)
    {
        if (canLearn)
            return LootRollVote::Need;

        return bindOnPickup ? LootRollVote::Pass : LootRollVote::Greed;
    }

    // Duplicate-unique gate (donor RollUniqueCheck at LootNeedRollLevel=1,
    // the donor default: NEED becomes GREED): a unique already equipped, or
    // a stack already at its MaxCount, cannot be looted again, so NEED is
    // pointless. Applies to every bot; explicit FORCE_NEED still wins at
    // the call site. totalCount includes equipped copies (GetItemCount
    // walks equipment + bags).
    inline bool UniqueCopyOwned(bool isUniqueEquipped, bool equippedSameEntry,
        std::uint32_t totalCount, std::uint32_t maxCount)
    {
        if (isUniqueEquipped && equippedSameEntry)
            return true;

        return maxCount > 0 && totalCount >= maxCount;
    }

    // Class-token gate (donor CanBotUseToken): epic junk-class tokens are
    // NEEDed only by a class in their AllowableClass mask. An empty mask
    // means unrestricted (all classes can use it) - small fix inside the
    // port, the donor GREEDs those. Pool bots only at the call site.
    inline bool TokenUsableByClass(std::uint32_t allowableClass, std::uint32_t classMask)
    {
        return allowableClass == 0 || (allowableClass & classMask) != 0;
    }

    // "Never NEED against a player": does one real player in the roll stand
    // between this bot and the item? Only a NEED the bot actually saw
    // competes: the owner's own CMSG_LOOT_ROLL (forwarded to the owner's
    // bots) is the one vote the core shows it, so GREED or PASS steps aside.
    // Everyone else's vote stays private in the core, and "can use it" alone
    // must not demote: in an LFT/pool group every usable drop is usable by
    // someone, so that reading turned every upgrade NEED into GREED (#967).
    // The one exception is a hired companion grouped with its master: it
    // still steps aside for a usable item, because taking the hirer's
    // upgrade from their own companion is always wrong. Alts (the player's
    // own-account bots) wait like pool bots instead: if the player wants to
    // gear the alt, they click greed and the alt needs.
    inline bool RealPlayerCompetes(bool voteSeen, LootRollVote seenVote, bool canUse, bool yieldUnseenUsable = false)
    {
        if (voteSeen)
            return seenVote == LootRollVote::Need;

        return yieldUnseenUsable && canUse;
    }
    // Wire mapping for SMSG_LOOT_ROLL (Group::CountRollVote vs CountTheRoll):
    // a live vote is (number 0, type 0) NEED, (128, 2) GREED, (128, 128)
    // PASS. Anything with a real roll number is a post-resolution echo and
    // maps to Pass so the call site ignores it. Pure data rule, unit-tested.
    inline LootRollVote LootWireVote(std::uint8_t rollNumber, std::uint8_t rollType)
    {
        if (rollNumber == 0 && rollType == 0)
            return LootRollVote::Need;
        if (rollNumber == 128 && rollType == 2)
            return LootRollVote::Greed;
        if (rollNumber == 128 && rollType == 128)
            return LootRollVote::Pass;
        return LootRollVote::Pass;
    }

    // True for a post-resolution echo: any packet carrying a real roll
    // number. The roll is already decided; the vote recorder ignores it.
    inline bool LootVoteIsEcho(std::uint8_t rollNumber, std::uint8_t rollType)
    {
        return LootWireVote(rollNumber, rollType) == LootRollVote::Pass &&
            (rollNumber != 128 || rollType != 128);
    }

    // Roll-wait timing (#967): pool bots in a group with real players hold
    // their vote so the humans get the whole window. They vote this many
    // milliseconds before the core's group-loot window ends
    // (LOOT_ROLL_TIMEOUT = 60 s in Group.cpp): late enough that every human
    // had their chance, early enough that the vote always lands. The margin
    // must clear the worst-case gap between two vote checks: the "very
    // often" roll trigger only re-fires every 5 s (TimeTrigger interval,
    // second-resolution), plus AI tick jitter under load (1000+ bots slow
    // every bot's tick), so a 5 s margin could check at 5.01 s left, wait,
    // and find the window closed on the next tick. 15 s leaves room for a
    // missed check and slow ticks. Pure data rule, tested on its own; the
    // call site reads the live deadline.
    inline std::uint32_t RollWaitVoteMarginMs() { return 15000; }

    // True when the wait is over: the roll ends within the vote margin, or
    // every real player who can roll on the item already voted. remainingMs
    // is the live time left on the roll (0 = unknown: keep waiting, the
    // margin test cannot fire). allVotersVoted arrives precomputed so the
    // per-tick call stays trivial.
    inline bool RollWaitElapsed(std::uint32_t remainingMs, bool allVotersVoted)
    {
        if (allVotersVoted)
            return true;

        return remainingMs > 0 && remainingMs <= RollWaitVoteMarginMs();
    }

    // An automatic NEED becomes GREED while a real player competes. An
    // explicit FORCE_NEED order from the player still NEEDs (player control
    // first); GREED and PASS are never raised.
    inline LootRollVote PoliteRollVote(LootRollVote vote, bool realPlayerCompetes, bool forcedNeed)
    {
        if (vote == LootRollVote::Need && realPlayerCompetes && !forcedNeed)
            return LootRollVote::Greed;

        return vote;
    }
}
