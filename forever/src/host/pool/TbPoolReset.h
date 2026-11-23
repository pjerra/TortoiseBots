// The managed pool's startup reset: when TortoiseBots.Pool.Reset is `once:<token>` (a token not
// yet applied) or `always`, every `ready` pool character is deleted one per world tick and the pool is refilled.
//
// Ownership is the registry's, never a name or a level: a target is a `ready` tb_pool_character row whose
// tb_pool_account row still passes the pool's name/id/stamp proof (PoolRegistryPolicy.h). A character on a pool
// account without a ready row is not a pool character and is kept (and counted). A normal player account has no row,
// so it can never be in pool::Accounts() or pool::Characters().
//
// Everything runs on the world thread, started only from configuration read during initial world startup (never by
// `.reload config`, and no live command exists). The phases mirror TurtleBots' reset without the auction settle:
//   Planning   - log out pool bots and wait until no target is in the world (bounded);
//   Deleting   - one target per tick, every safety fact re-read first;
//   Verifying  - the core commits asynchronously, so a bounded retry until no target row is left;
//   Rebuilding - record the generation last, clear lineage rows if that table is there, then normal creation refills.
// A refusal before maintenance leaves the pool fully available; a failure after it keeps the pool paused and the
// generation unrecorded, so the next start plans again from the remaining rows.
#pragma once

#include <cstdint>
#include <string>

namespace TortoiseBots
{
namespace pool
{
namespace Reset
{

// Startup only (TbScripts.cpp, OnWorldInitialized, after pool::Reconcile): parses the setting, snapshots the ready pool
// rows and runs every preflight. No character and no row is deleted here.
void PlanAtStartup(std::string const& configuredValue);

// One bounded step on the world tick (TbScripts.cpp, OnBeforeMapsUpdate, before the scheduler): at most one deletion.
void Update(std::uint32_t diff);

// Forgets the plan (a clean shutdown, and a simulated restart in the tests). The recorded generation stays in the
// database; the next start reads it again.
void Shutdown();

// False while the reset is planning, deleting, verifying or rebuilding, and after it failed: the scheduler plans no
// logins and creation does not start. The drain of bots already in the world keeps running.
bool PoolAvailable();

bool Active();

// `.tb pool status`: `reset=once:<token> phase=<phase> targets=<n> applied=<generation>`.
std::string StatusLine();

} // namespace Reset
} // namespace pool
} // namespace TortoiseBots
