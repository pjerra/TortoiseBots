// W0 (FOREVER-W0-W4-SPEC.md, round W0-B): the pool's scheduler. Once a second, on the world thread, right after BotMgr::Update:
// keep TortoiseBots.Pool.TargetOnline pool bots of both factions online, each for a rolled session (SessionMinutes.Min..Max),
// each back after a rolled offline time (OfflineMinutes.Min..Max), at most LoginsPerMinute logins in any 60 s window and
// alternating factions, the bot offline longest first and only on an account without a session (one session per account, D2).
//
// The decisions are ai/playerbot/policy/world/PoolSchedulePolicy.h (pure, tested by forever/tools/test_pool_schedule_policy.cpp);
// this file only translates: registry rows and BotMgr bots into PoolBotState, the pass's guids back into Login/Logout, and the
// session's two stamps into `tb_pool_character` (last_login, last_logout). A bot that got a master leaves the count and is
// never logged out by the pool (the policy's `bound`, from `IsPoolBot`, `IsFreeBot` and `MasterOf`); no login is planned while
// the core's shutdown countdown runs. The level ladder (`AiPlayerbot.LevelLadder`, on): each bot's level comes from the
// Player in the world or the character cache outside it, and the policy picks the band with the largest shortfall first.
//
// `.tb pool start|stop|status` (TbScripts.cpp): start is explicit (decision: player control first), stop drains every pool bot
// at 5 logouts per second. Everything is off by default (TortoiseBots.Pool.Enable = 0, TargetOnline = 0).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace TortoiseBots
{
namespace pool
{
namespace PoolScheduler
{

// The `# W0 pool` keys of forever/conf/tortoisebots.conf.dist: TargetOnline, LoginsPerMinute, SessionMinutes.Min/.Max and
// OfflineMinutes.Min/.Max. Called beside the registry's LoadConfig at boot and on `.reload config`.
void LoadConfig();

// One world tick, right after BotMgr::Update (TbScripts.cpp): runs the pass when a second has passed. True = this tick started
// a login, and the creation step waits a tick (the spec's budget: never a character creation in the same tick as a login).
bool Update(std::uint32_t diff);

struct SwitchResult
{
    bool ok = false;
    std::string message;
};

// `.tb pool start` starts (or restarts, cancelling a drain in progress) the schedule; `.tb pool stop` starts the drain.
SwitchResult Start();
SwitchResult Stop();

// `.tb pool status`: one line, `state=`, `target=`, the counted online bots and their factions, the pool's size, the bots due to
// return, `in_bg=` (bots whose session a running battleground holds), the logins of the last 60 s, the drain's queue and the
// last pass's cost; ` capped by accounts: N` when the target is
// above the accounts the pool can still log in on (the ones its characters sit on, less the accounts a bound bot holds).
std::vector<std::string> Status();

// `.tb pool population <n|low|high>`: a new online target at runtime (`low`/`high` are the PopulationLow/High presets, a number
// is used as-is). The pool adds or logs out bots gradually from the next pass; `.reload config` puts the configured TargetOnline back.
SwitchResult SetPopulation(std::string const& word);

} // namespace PoolScheduler
} // namespace pool
} // namespace TortoiseBots
