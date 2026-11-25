// Putting the bot's own pieces back after a resist set (issue #20 round 1): WHICH held pieces can go back on one
// Restore call, when the world tick may try again, how long it waits between passes, and when switching the feature
// off must release the set. The pure rules, with no core include (fully testable on the toolchain alone); the host
// (forever/src/host/TbResistGear.cpp) asks them and makes the moves with the core's Player::SwapItem.
//
// A hold is never dropped while it still names pieces that could not move: a dead bot, a full bag, a piece the bot no
// longer has or a refused swap keeps the hold (and, through the characters store, survives the restart), so the next
// try has something to put back. The automatic retry never gives up either: the first passes are seconds apart and a
// hold that outlasts them waits a minute between passes for as long as its bot lives, so bag space that frees up an
// hour later still gets the set back.
//
// Tests: forever/tools/test_resist_gear_policy.cpp (RestoreLeavesADeadBotsHold, PartialRestoreKeepsTheRest,
// RetryNeverStopsWhileTheBotCanMove, BackoffWaitsUntilTheBagFrees, DisableWaitsForABotThatCanMove,
// DisableReleasesTheHold, AStoredHoldIsRecoveredOnLogin, OnlyARequestedReleaseIsRetried); the whole hold against a
// fake core, forever/tools/resist_gear_host_check.cpp.
#pragma once

#include <cstddef>
#include <cstdint>

namespace ai
{
// Where a hold stands. HELD is a set this module just put on and nobody asked to take off: the world tick leaves it
// alone, so a bot that arms before the pull keeps the set through the pull. RELEASING is a restore that was asked for
// (a release, a disable, a logout, a recovery after a restart) and has not put every piece back yet: only this state
// is retried, and Apply never creates it.
enum class HoldState
{
    Held,
    Releasing
};

inline bool ShouldRetryHold(HoldState state)
{
    return state == HoldState::Releasing;
}

// One restore pass tries every held piece once; a piece that cannot move stays in the hold. The first
// kMaxRestoreAttempts passes are the fast ones (kRetrySeconds apart); after that the hold waits kBackoffRetrySeconds
// between passes and keeps waiting while its bot lives and is out of combat. There is no limit on the total: a hold
// with pieces left is never abandoned.
constexpr std::uint32_t kMaxRestoreAttempts = 5;
constexpr std::uint32_t kRetrySeconds = 5;
constexpr std::uint32_t kBackoffRetrySeconds = 60;

// A dead bot's hold is not touched at all: the pieces stay recorded and a later, living retry puts them back.
inline bool MayRestoreHold(bool alive)
{
    return alive;
}

// A restore pass may run for a living bot out of combat. The number of passes already spent only changes the spacing
// (RetryDelaySeconds), never the answer: a full bag is waited out, not given up on.
inline bool ShouldRetryRestore(bool alive, bool inCombat)
{
    return alive && !inCombat;
}

// The wait before the next automatic pass, after `attempts` passes left pieces behind.
inline std::uint32_t RetryDelaySeconds(std::uint32_t attempts)
{
    return attempts < kMaxRestoreAttempts ? kRetrySeconds : kBackoffRetrySeconds;
}

// How one held piece can go back on this pass.
enum class PieceBack
{
    SwapOwn,   // the bot's own piece is in the bags and the core accepts it in the slot: swap the two in place
    BagResist, // the slot was empty when the set went on: the resist piece comes off into a free bag position
    DropStale, // the slot no longer holds the resist piece: nothing of this hold is there, forget the piece
    Hold       // the piece cannot move yet (own piece gone, core refuses, no free bag position): keep it
};

inline PieceBack PlanPieceBack(bool slotHoldsResist, bool ownWasEmpty, bool ownFound, bool canEquipOwn, bool freeBagPos)
{
    if (!slotHoldsResist)
        return PieceBack::DropStale;
    if (ownWasEmpty)
        return freeBagPos ? PieceBack::BagResist : PieceBack::Hold;
    if (!ownFound)
        return PieceBack::Hold;
    return canEquipOwn ? PieceBack::SwapOwn : PieceBack::Hold;
}

// After a pass: a hold with pieces left is kept for the tick and for the store; an empty one is dropped.
inline bool HoldHasPieces(std::size_t remaining)
{
    return remaining > 0;
}

// Switching the feature off while a set is held releases it (the set comes off even though no raid is pending).
// `wasEnabled && !nowEnabled` is the reload edge; a world that starts with the key at 0 holds nothing.
inline bool ReleaseOnDisable(bool wasEnabled, bool nowEnabled, bool holding)
{
    return wasEnabled && !nowEnabled && holding;
}

// A bot with stored pieces and no hold in memory (a crash, or a logout that could not put every piece back): the login
// reads the store and restores it.
inline bool RecoverOnLogin(bool anyStoredPieces, bool alreadyHolding)
{
    return anyStoredPieces && !alreadyHolding;
}
}
