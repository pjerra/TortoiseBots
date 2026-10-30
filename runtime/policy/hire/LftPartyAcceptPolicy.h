#pragma once
// Which of a human's party bots the LFT service accepts an offer for (#1284).
// A random-pool bot is a fill candidate, not the human's companion, unless it is
// hired: a hire is a pool character (record.random = true,
// HireProvisionService) that belongs to its hirer for the length of the hire.
#include <ctime>
#include <string>
#include <vector>

namespace TortoiseBots
{
// Pre-filter: may this bot accept any party offer at all?
inline bool LftPartyBotMayAccept(bool randomPool, bool hired)
{
    return !randomPool || hired;
}

// Final decision for one anchor (a live, network player in an offer that is the
// bot's master or group leader; the caller checks those facts).
// - An alt (not random pool) keeps the pre-#1284 rule: any such anchor will do.
// - A random-pool bot that is not hired never accepts.
// - A hire accepts only with its own hirer as the anchor (HireLifecycle::GetMaster),
//   and only when the two are in the same offer: a hire moved into a stranger's
//   group must not accept the stranger's offer, nor an offer its hirer is not in.
inline bool LftPartyOfferMayAccept(bool randomPool, bool hired, bool anchorIsHireMaster, bool sameOffer)
{
    if (!randomPool)
        return true;
    if (!hired)
        return false;
    return anchorIsHireMaster && sameOffer;
}

// The core LFT module API exposes no offer id per player (IsInOffer is a bool).
// It builds offers from whole queue blocks: a party queued by its leader shares
// one queueLeaderGuid, and TryBuildOfferForInstance puts a block in one offer
// whole or not at all. So two players who are both in an offer and were queued
// in the same block are in the same offer. The block id is not exposed either;
// these are its visible traces: both in one group now, both still listed in
// GetQueuedPlayers (players in an offer stay queued until it completes), with
// the same instance list and join times at most one second apart
// (EnqueueRolecheck queues the whole block in one call, but EnqueuePlayer reads
// the game time per member, so a block can straddle a second boundary).
constexpr std::time_t kLftQueueBlockJoinSlackS = 1;
inline bool LftQueuedTogether(bool sameGroup, bool bothListed,
    std::time_t joinA, std::time_t joinB,
    std::vector<std::string> const& instancesA, std::vector<std::string> const& instancesB)
{
    std::time_t const gap = joinA > joinB ? joinA - joinB : joinB - joinA;
    return sameGroup && bothListed && gap <= kLftQueueBlockJoinSlackS && instancesA == instancesB;
}
} // namespace TortoiseBots
