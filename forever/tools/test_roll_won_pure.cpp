// Standalone test of the roll-won decision (forever/src/host/TbRollWonPure.h):
//   g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/test_roll_won_pure.cpp -o /tmp/test_roll_won && /tmp/test_roll_won
// A win is announced exactly once; a loss is silent; a bags-full win waits as a pending entry and announces
#include "forever/src/host/TbRollWonPure.h"
#include <map>
#include <utility>
#include <vector>

#include <cstdio>

namespace rw = TortoiseBots::rollwon;

static int g_checks = 0, g_fail = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static rw::SeenRoll Roll(unsigned long long owner, unsigned slot, unsigned item)
{
    rw::SeenRoll roll;
    roll.id.ownerHi = 0;
    roll.id.ownerLo = owner;
    roll.id.slot = slot;
    roll.itemId = item;
    roll.count = 1;
    return roll;
}

struct ScriptedFate
{
    // keyed (ownerLo, slot) -> fate; absent means the loot is gone and the bags are unchanged.
    std::map<std::pair<unsigned long long, unsigned>, rw::Fate> fate;

    rw::Fate operator()(rw::SeenRoll const& roll) const
    {
        auto it = fate.find({ roll.id.ownerLo, roll.id.slot });
        if (it == fate.end())
        {
            rw::Fate lost;
            lost.lootGone = true;
            return lost;
        }
        return it->second;
    }
};
static std::vector<unsigned> WonIds(std::vector<rw::SeenRoll> const& rolls)
{
    std::vector<unsigned> ids;
    for (rw::SeenRoll const& roll : rolls)
        ids.push_back(roll.itemId);
    return ids;
}

static void won_once_event_once()
{
    rw::State state;
    ScriptedFate none;
    // Tick 1: the roll is open.
    CHECK(rw::Update(state, { Roll(7, 0, 1001) }, none).empty());
    // Tick 2: the roll resolved, the core names the bot winner, the item is in bags.
    ScriptedFate won;
    rw::Fate fate;
    fate.winnerIsBot = true;
    fate.held = true;
    won.fate[{7, 0}] = fate;
    CHECK(WonIds(rw::Update(state, {}, won)) == std::vector<unsigned>{ 1001 });
    // Tick 3: nothing left to announce.
    CHECK(rw::Update(state, {}, none).empty());
    CHECK(state.open.empty());
    CHECK(state.pending.empty());
}

static void lost_is_silent()
{
    rw::State state;
    ScriptedFate none;
    CHECK(rw::Update(state, { Roll(7, 0, 1001) }, none).empty());
    // Someone else won (or all passed): the loot may still show the corpse, the bags are unchanged.
    ScriptedFate lost;
    rw::Fate fate;
    fate.lootGone = false;
    lost.fate[{7, 0}] = fate;
    CHECK(rw::Update(state, {}, lost).empty());
    CHECK(state.open.empty());
    CHECK(state.pending.empty());
    CHECK(rw::Update(state, {}, none).empty());
}

static void bags_full_waits_then_announces()
{
    rw::State state;
    ScriptedFate none;
    CHECK(rw::Update(state, { Roll(9, 2, 2002) }, none).empty());
    // Won, but the item stayed on the corpse: no event, one pending entry.
    ScriptedFate full;
    rw::Fate fate;
    fate.winnerIsBot = true;
    fate.held = false;
    full.fate[{9, 2}] = fate;
    CHECK(rw::Update(state, {}, full).empty());
    CHECK(state.pending.size() == 1);
    CHECK(rw::Update(state, {}, full).empty());
    // The bot made room and took it: one event, pending cleared.
    ScriptedFate taken;
    rw::Fate have;
    have.winnerIsBot = true;
    have.held = true;
    taken.fate[{9, 2}] = have;
    CHECK(WonIds(rw::Update(state, {}, taken)) == std::vector<unsigned>{ 2002 });
    CHECK(state.pending.empty());
    CHECK(rw::Update(state, {}, none).empty());
}

static void pending_dropped_when_loot_goes_away()
{
    rw::State state;
    ScriptedFate none;
    CHECK(rw::Update(state, { Roll(9, 2, 2002) }, none).empty());
    ScriptedFate full;
    rw::Fate fate;
    fate.winnerIsBot = true;
    full.fate[{9, 2}] = fate;
    CHECK(rw::Update(state, {}, full).empty());
    CHECK(state.pending.size() == 1);
    // The corpse despawned before the bot took the item: dropped, never announced.
    CHECK(rw::Update(state, {}, none).empty());
    CHECK(state.pending.empty());
}

static void held_without_winner_still_wins()
{
    rw::State state;
    ScriptedFate none;
    CHECK(rw::Update(state, { Roll(7, 1, 3003) }, none).empty());
    // The loot window is gone (a corpse the bot never opened) but the bags gained the item.
    ScriptedFate held;
    rw::Fate fate;
    fate.held = true;
    fate.lootGone = true;
    held.fate[{7, 1}] = fate;
    CHECK(WonIds(rw::Update(state, {}, held)) == std::vector<unsigned>{ 3003 });
}

int main()
{
    won_once_event_once();
    lost_is_silent();
    bags_full_waits_then_announces();
    pending_dropped_when_loot_goes_away();
    held_without_winner_still_wins();
    std::printf("%d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
