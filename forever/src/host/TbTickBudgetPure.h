#pragma once

// Scheduling rules of the per-tick bot-update budget (BotMgr::Update, the
// AiPlayerbot.PoolTickBudgetUs/CombatTickBudgetUs cut-off).
//
// TB's turn queue orders every due bot by how late it is relative to its
// situation's tolerance, so a bot the budget did not reach keeps its old due
// time, is later than every bot that ran, and sorts first next pass
// (runtime/BotTurnScheduler.h OrderDueTurns). The host loop visits every bot
// each pass and has no due times, so the same contract is a rotation: the
// first bot the budget cut becomes the first of the next pass. A bot waits at
// most one full round of turns, never two, whatever share of the pool a pass
// serves.
//
// Core-free: the tests drive it with plain integers (forever/tools/test_tick_budget.cpp).

#include <algorithm>
#include <cstdint>
#include <vector>

namespace TortoiseBots
{
namespace tickbudget
{
// Whether this pass may cut the pool: a budget is set (0 = off) and the
// previous world tick is over the gate (0 = every tick).
inline bool PassIsBudgeted(uint64_t budgetUs, uint32_t gateMs, uint32_t tickMs)
{
    return budgetUs > 0 && (gateMs == 0 || tickMs > gateMs);
}

// Move `first` to the front of `keys`: the bot the previous pass cut off runs
// first now, the rest keep their order. A key no longer in `keys` (the bot
// logged out, or a master bound it and it left the pool) leaves the order as
// it is.
template <typename Key>
void RotateToFirst(std::vector<Key>& keys, Key const& first)
{
    auto it = std::find(keys.begin(), keys.end(), first);
    if (it != keys.end() && it != keys.begin())
        std::rotate(keys.begin(), it, keys.end());
}
} // namespace tickbudget
} // namespace TortoiseBots
