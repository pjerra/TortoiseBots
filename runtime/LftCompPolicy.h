#pragma once

// LFT group composition: count the waiting queue the way the core matcher
// will assign it (fill and core disagreed, bots duplicated humans).
//
// The core (tortoise-wow src/game/LFT/LFTQeueue.cpp, read-only) forms an
// offer only for exactly 1 tank / 1 healer / 3 damage. At offer time it walks
// the queue in queue order and hands each entrant a slot with
// PickRole(mask, tanks, healers, dps): tank if ticked and still free, else
// healer, else damage. A party (one queueLeaderGuid) is evaluated atomically
// at its first member's position: a single member with no free slot sinks the
// whole block. Members already committed to an offer never join a new one.
//
// The fill replayed a weaker copy of that match over the queued snapshot:
// bare joinTime order with unspecified std::sort ties (and a rolecheck party
// ALWAYS shares one joinTime second), no party blocks, and committed
// (in-offer) entries counted as covering new offers. Whenever the replay
// disagreed with the offer-time assignment, the fill queued a bot into a
// role the core hands to a human (duplicate) while the real gap stayed
// empty: the core can never complete 1/1/3 from that set, yet the fill
// believed the needs were met, so humans waited forever behind stranded
// bots and no proper group ever formed.
//
// This kernel replays the core match over exactly the entries that can join
// a NEW offer. The caller excludes committed entries first: a partition with
// nobody outside committed sets has nothing to fill. Order is
// (joinTime, guidLow), with group-slot order inside a block, which equals
// core queue order whenever the roster has not changed since queueing
// (rolecheck parties enqueue back-to-back in slot order). The result is the
// missing 1/1/3 slots, plus a blocked flag: a multi-member block holding a
// human that fits no slot combination can never join any offer from this
// set, so the fill must wait (log) instead of stranding bots around it.
// Solo entrants that fit nowhere simply sit out; the fill still completes
// the formable set around them, exactly like the core does without the fill.
//
// Pure policy: no core/engine headers, unit-tested standalone
// (tools/test_lft_comp_policy.cpp).

#include <algorithm>
#include <cstdint>
#include <set>
#include <vector>

#include "LftRoleKitPolicy.h"

namespace TortoiseBots
{

// One queued entrant that may join a new offer (committed entries excluded
// by the caller).
struct LftCompEntry
{
    uint32_t guidLow = 0;   // tiebreak: approximates core queue order within one second
    uint32_t joinTime = 0;  // primary order: core queue insertion order
    uint8_t roleMask = 0;   // LFT_ROLE_* bits the entrant signed up with
    uint32_t blockId = 0;   // party block (core queueLeaderGuid): leader guidLow, own guidLow solo
    uint32_t blockSeq = 0;  // intra-block order (core group-slot order): slot index, 0 solo
    bool human = false;     // real player (module IsBot == false)
};

struct LftCompResult
{
    uint8_t needTank = 0;
    uint8_t needHeal = 0;
    uint8_t needDps = 0;
    uint32_t covered = 0;       // simulated entries holding a slot
    uint32_t members = 0;       // simulated entries
    bool blockedHumans = false; // a human party fits no slot: wait, do not fill
};

// Core PickRole verbatim (LFTQeueue.cpp): first free ticked slot wins.
inline uint8_t LftCompPickRole(uint8_t mask, uint8_t tanks, uint8_t healers, uint8_t dps)
{
    if ((mask & kLftRoleTank) && tanks < 1)
        return kLftRoleTank;
    if ((mask & kLftRoleHealer) && healers < 1)
        return kLftRoleHealer;
    if ((mask & kLftRoleDamage) && dps < 3)
        return kLftRoleDamage;
    return 0;
}

inline LftCompResult LftComputeMissingRoles(std::vector<LftCompEntry> entries)
{
    LftCompResult result;
    result.members = static_cast<uint32_t>(entries.size());
    // Core queue order approximation: insertion time, guidLow tiebreak
    // (deterministic; bare joinTime ties used to sort in unspecified order).
    // Blocks gather whole wherever their first member sorts, like the core
    // evaluating the block at its first candidate.
    std::sort(entries.begin(), entries.end(), [](LftCompEntry const& a, LftCompEntry const& b)
    {
        if (a.joinTime != b.joinTime)
            return a.joinTime < b.joinTime;
        return a.guidLow < b.guidLow;
    });

    uint8_t tanks = 0, healers = 0, dps = 0;
    std::set<uint32_t> done;
    for (LftCompEntry const& first : entries)
    {
        if (!done.insert(first.blockId).second)
            continue;
        // Block members in core order (group-slot order at rolecheck time).
        std::vector<LftCompEntry const*> block;
        for (LftCompEntry const& m : entries)
            if (m.blockId == first.blockId)
                block.push_back(&m);
        std::sort(block.begin(), block.end(), [](LftCompEntry const* a, LftCompEntry const* b)
        {
            if (a->blockSeq != b->blockSeq)
                return a->blockSeq < b->blockSeq;
            return a->guidLow < b->guidLow;
        });
        // Trial fit: every member needs a free slot, or the whole block
        // sits out (core blockFits).
        uint8_t t = tanks, h = healers, d = dps;
        bool fits = true;
        for (LftCompEntry const* m : block)
        {
            uint8_t role = LftCompPickRole(m->roleMask, t, h, d);
            if (!role)
            {
                fits = false;
                break;
            }
            if (role == kLftRoleTank)
                ++t;
            else if (role == kLftRoleHealer)
                ++h;
            else
                ++d;
        }
        if (!fits)
        {
            // A solo entrant that fits nowhere sits out while the fill
            // completes the formable set (core does the same). A
            // multi-member block holding a human fits no offer from this
            // set at all: filling around it only strands bots.
            if (block.size() > 1)
                for (LftCompEntry const* m : block)
                    if (m->human)
                    {
                        result.blockedHumans = true;
                        break;
                    }
            continue;
        }
        tanks = t;
        healers = h;
        dps = d;
        result.covered += static_cast<uint32_t>(block.size());
    }

    // Blocked human party: report no needs so the caller waits instead of
    // queueing bots the core can never place.
    if (result.blockedHumans)
        return result;
    if (tanks < 1)
        result.needTank = 1;
    if (healers < 1)
        result.needHeal = 1;
    if (dps < 3)
        result.needDps = static_cast<uint8_t>(3 - dps);
    return result;
}

} // namespace TortoiseBots
