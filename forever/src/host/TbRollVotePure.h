#pragma once

// Core-free vote-mask fallback for honest group-loot votes: NEED not offered
// -> GREED, GREED not offered -> PASS.

#include <cstdint>

namespace tbrollvote
{
// Wire-vote numbers shared by both cores (T RollVote and F RollType both
// number PASS=0, NEED=1, GREED=2; F's DISENCHANT=3 has no T counterpart and
// TB never sends it).
inline constexpr int kVotePass = 0;
inline constexpr int kVoteNeed = 1;
inline constexpr int kVoteGreed = 2;

// Mask bits mirror F's RollMask (Loot.h). The mask is F-only: T's core drops
// an off-mask vote silently instead of refusing it, so T needs no fallback.
inline constexpr std::uint32_t kMaskPass = 0x01;
inline constexpr std::uint32_t kMaskNeed = 0x02;
inline constexpr std::uint32_t kMaskGreed = 0x04;
inline constexpr std::uint32_t kMaskAll = 0x0F;

// Constrains a decided vote to what the roll offers. PASS always passes
// (the core never refuses it); anything outside need/greed/pass is left
// alone (the wire never sends it: RollOnItemInSlot normalises to PASS first).
inline int FallbackVote(int vote, std::uint32_t voteMask)
{
    if (vote == kVoteNeed && !(voteMask & kMaskNeed))
        vote = kVoteGreed;
    if (vote == kVoteGreed && !(voteMask & kMaskGreed))
        vote = kVotePass;
    return vote;
}
} // namespace tbrollvote
