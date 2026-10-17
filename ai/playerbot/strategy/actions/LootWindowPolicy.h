#pragma once

#include <string>

// Pure policy for what a loot window does to the loot stack's failure memory
// (LootObjectStack: 3 strikes, 120 s TTL). Kept free of core headers so the
// rule is unit-testable (tools/test_loot_window_policy.cpp).
//
// - A window that paid out (an item or money taken) forgets the opens counted
//   so far, so a multi-use ore vein or a loot-then-skin corpse is never cut short.
// - A window that gave this bot nothing, or that the server refused, abandons
//   the object at once: the core keeps such a corpse lootable (an item under a
//   group roll, view-only, unwanted, no bag room), so the "add all loot" sweep
//   re-queued it and the bot re-opened the same empty window on one spot.
// - A player's explicit "loot" order never records that abandon: the object
//   is only unqueued and keeps its open count, so the order behaves as before
//   and the three-open bound still caps any automatic repeat afterwards.

namespace ai
{
    enum class LootWindowVerdict
    {
        ForgetFailures, // the window paid out
        Abandon,        // empty or refused window: leave the object alone for the TTL
        KeepCount       // empty or refused, but player-ordered: unqueue only
    };

    inline LootWindowVerdict DecideAfterLootWindow(bool refused, bool paidOut, bool playerOrdered)
    {
        if (!refused && paidOut)
            return LootWindowVerdict::ForgetFailures;
        if (playerOrdered)
            return LootWindowVerdict::KeepCount;
        return LootWindowVerdict::Abandon;
    }

    // An "add all loot" run is a player's order when it came from the chat
    // command trigger (whispered "loot" / "add all loot", which carries the
    // commanding player). The automatic sweep ("often" trigger), RPG chests and
    // the debug command arrive under other event sources.
    inline bool IsPlayerLootOrder(std::string const& eventSource, bool hasOwner)
    {
        return hasOwner && eventSource == "add all loot";
    }

    // How long the AI sleeps after "open loot" (#1062). A cast on the object
    // (Herb Gathering 5 s, Mining 3.2 s, Opening, Skinning) must outlast the
    // action's duration, or the next tick runs the follow action, the bot
    // walks back into formation and the core cancels the cast on the first
    // step (Spell::update, SPELL_INTERRUPT_FLAG_MOVEMENT): the gather animation
    // plays, the node never opens, no loot. Cast actions already sleep for
    // their cast time (CastSpellAction); the open-loot cast now does the same.
    // Anything else (a CMSG_LOOT on a corpse, a failed attempt) keeps the
    // duration it set, never a cast time left over from an earlier call.
    inline unsigned DecideOpenLootDuration(bool opened, unsigned castMs, unsigned otherMs)
    {
        if (opened && castMs > 0)
            return castMs;
        return otherMs;
    }
}
