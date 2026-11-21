// W0 (FOREVER-W0-W4-SPEC.md, round W0-A, fix round): creation of the pool's characters. `.tb pool create <n>` asks for n
// characters; they are made one at a time on the world tick, at most TortoiseBots.Pool.CreatePerMinute per minute (the
// scheduler's rate and the login budget of the spec: one call per tick, never a burst), and never beyond
// TortoiseBots.Pool.MaxCharacters in total.
//
// A creation is a two-row claim written before each irreversible step (TbPoolRegistry): the account row `creating` before
// `AccountMgr::CreateAccount`, the character row `creating` before `BotMgr::CreateCharacter`, and each row becomes `ready`
// only when the thing it names is confirmed. A character is confirmed by reading the core's own characters table back on a
// later tick, never when the saves are queued and never from the character cache. A stopped creation therefore either
// finishes (a database read shows what was made) or is marked `failed` with a log line: the reconciliation deletes nothing,
// ever. Removing a failed row or its account is `.tb pool prune`, an explicit GM command that is a dry run unless it is
// given `confirm` and that re-verifies every account against the database first.
//
// Each character: the composition policy's race and class, a name from runtime/BotNameService.cpp (checked against the core's
// name rules and the characters table before any account is made), an account (the job's own while it has room, a registry
// account with room, an empty failed pool account, else a new `TBPOOL0001` ...: created through `AccountMgr::CreateAccount`
// with a random password and the row's stamp as its e-mail, neither logged nor stored; never beyond what the cap needs), that
// account re-verified by database reads (`VerifyAccount`), then the core's own creation sequence through `BotMgr::CreateCharacter`.
// A job keeps the cap and rate it started with and stops when TortoiseBots.Pool.Enable is turned off. A failure stops
// the job and the report names the reason: the spec's U4 (no Battle.net account on this core) is exactly this, and it must
// never retry into a loop.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace TortoiseBots
{
namespace pool
{

struct CreateStart
{
    bool started = false;
    std::string message;     // the refusal (started = false) or what is now running
};

// Finishes or marks the creations that stopped halfway: rows in state `creating`. A row whose account or character the
// characters database proves to exist becomes ready; one it proves was never made becomes `failed` with a log line; one the
// database does not answer for stays as it is. Nothing is deleted. Idempotent, and a no-op without such rows. Called at
// module start (TbScripts.cpp, OnWorldInitialized) and by StartCreate before a job.
void Reconcile();

// Starts a job of `count` characters, bounded by TortoiseBots.Pool.MaxCharacters. A second request while one runs is refused
// (the message says how many are left). The pool at or over its cap refuses with the size and the key.
CreateStart StartCreate(std::uint32_t count);

struct PruneReport
{
    bool enabled = false;                 // false: the pool is off, and `lines` says why
    bool applied = false;                 // true only for `confirm`
    std::uint32_t accounts = 0;           // failed pool accounts that would go (or went)
    std::uint32_t characters = 0;         // characters of those accounts in the characters database
    std::uint32_t rows = 0;               // failed rows that would go (or went) on their own
    std::vector<std::string> lines;       // one per failed row: what would happen, or the fact that kept it
};

// `.tb pool prune` (a dry run: only lists) and `.tb pool prune confirm`: re-reads every failed row from the database and
// removes only what it proves - an account whose username carries the pool prefix, whose row still carries the pool's
// creation stamp, with nothing ready and with every character on it named by a pool character row of that account (and
// none online), or a failed character row whose account is live and whose name the database says does not exist. Anything
// else is listed with the reason and kept.
PruneReport Prune(bool confirm);

// One world tick (TbScripts.cpp, right after BotMgr::Update): makes at most one character, when the rate's interval has passed.
void CreateUpdate(std::uint32_t diff);

bool Creating();

// Why the last job stopped (or its completion line); empty before any job and while one runs.
std::string StopReason();

} // namespace pool
} // namespace TortoiseBots
