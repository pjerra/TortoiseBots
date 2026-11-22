#pragma once

// Host seam for honest group-loot votes: constrains the vote TB decided to
// what the roll's own vote mask offers (NEED not offered -> GREED, GREED not
// offered -> PASS), because Forever's LootRoll::PlayerVote refuses a vote the
// mask does not offer. Called once, from RollVoteCompat (TbCompat_S9.h); the
// rule itself lives in TbRollVotePure.h so it stays testable without the core.

#include <cstdint>

// Vote numbers are the wire votes shared by both cores (PASS=0, NEED=1,
// GREED=2); the mask is F's RollMask. Returns the vote to send.
int TbRollVoteMaskFallback(int vote, std::uint32_t voteMask);
