// Piece C (roll won): the decision half of the won-roll poll. Core-free: it sees only the bot's current open
// rolls, the host's reading of each roll that disappeared, and remembers what it already announced, so
// forever/tools/test_roll_won_pure.cpp compiles it with a bare g++.
//
// One State per bot. The host feeds the core's current roll list every tick; Update returns the rolls the
// engine must announce as won this tick. A win is reported exactly once: emitting it drops the record, and a
// later tick can never report it again.
//
// A roll disappears from the core list when it resolves (Loot::Update drives LootRoll::UpdateRoll, which calls
// Finish and then destroys the roll, dropping it from every voter's Player::m_lootRolls). The host reads the
// fate of each disappeared roll from the core's own record (LootItem::rollWinnerGUID, set by Finish before the
// item reaches the winner's bags) and from the bot's bags:
//   won and item in bags -> announce now;
//   won and item still on the corpse (bags were full, StoreLootItem refused it) -> a pending entry; announce
//     when the item reaches the bags, drop it when the loot stops offering the slot;
//   anything else (someone else won, all passed, timed out) -> drop silently.
#pragma once

#include <cstdint>
#include <map>
#include <vector>

namespace TortoiseBots
{
namespace rollwon
{

struct RollId
{
    uint64_t ownerHi = 0;   // the corpse or chest guid, high half
    uint64_t ownerLo = 0;   // the corpse or chest guid, low half
    uint32_t slot = 0;      // the loot list id

    bool operator<(RollId const& other) const
    {
        if (ownerHi != other.ownerHi)
            return ownerHi < other.ownerHi;
        if (ownerLo != other.ownerLo)
            return ownerLo < other.ownerLo;
        return slot < other.slot;
    }
    bool operator==(RollId const& other) const
    {
        return ownerHi == other.ownerHi && ownerLo == other.ownerLo && slot == other.slot;
    }
};

// One roll the core currently waits on this bot for.
struct SeenRoll
{
    RollId id;
    uint32_t itemId = 0;
    uint32_t count = 0;
};

// What the host could learn about a remembered roll that left the core list (or waits, for a pending entry).
struct Fate
{
    bool winnerIsBot = false;   // the loot still names the bot as rollWinnerGUID
    bool held = false;          // the bags hold (more of) the item than when the roll was remembered
    bool lootGone = false;      // the loot is gone or no longer offers the slot
};

struct State
{
    std::map<RollId, SeenRoll> open;     // rolls on the core list (or gone but unresolved)
    std::map<RollId, SeenRoll> pending;  // won, item not in bags yet
};

// The rolls won this tick, each reported once. fateOf is asked once per remembered roll that is no longer
// open, and once per pending entry.
template <class FateOf>
inline std::vector<SeenRoll> Update(State& state, std::vector<SeenRoll> const& live, FateOf&& fateOf)
{
    std::vector<SeenRoll> won;

    std::map<RollId, SeenRoll> still;
    for (SeenRoll const& roll : live)
    {
        auto kept = state.open.find(roll.id);
        still.emplace(roll.id, kept != state.open.end() ? kept->second : roll);
    }

    for (auto const& [id, remembered] : state.open)
    {
        if (still.count(id))
            continue;
        Fate fate = fateOf(remembered);
        if (fate.winnerIsBot || fate.held)
        {
            if (fate.held)
                won.push_back(remembered);
            else
                state.pending.emplace(id, remembered);
        }
    }
    state.open.swap(still);

    for (auto it = state.pending.begin(); it != state.pending.end();)
    {
        Fate fate = fateOf(it->second);
        if (fate.held)
        {
            won.push_back(it->second);
            it = state.pending.erase(it);
        }
        else if (fate.lootGone)
            it = state.pending.erase(it);
        else
            ++it;
    }

    return won;
}

}
}
