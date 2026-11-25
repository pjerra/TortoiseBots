// W0 (FOREVER-W0-W4-SPEC.md, round W0-A, fix round): the pool's registry in the characters database. `tb_pool_account` lists
// the accounts the module itself created (`TBPOOL0001` ...), `tb_pool_character` the characters on them; only a registered
// account or character is ever touched (decision D2). The migration is forever/sql/characters/2026_11_20_02_tb_pool_rows.sql
// (it upgrades the first round's tables and creates the current shape on a database without them), and the module runs the
// current CREATE TABLE IF NOT EXISTS statements itself, once, when TortoiseBots.Pool.Enable = 1.
//
// A row carries what proves it is still about the module's account: the username, the random creation stamp (written as the
// account's e-mail, the one field `AccountMgr` reads back) and a state. `creating` is written before the account or character
// exists; only `ready` rows are used. A row whose account id/name/stamp no longer agree (auth restore, deleted and reused id,
// a manual repair) is marked `stale` and never acted on: ai/playerbot/policy/world/PoolRegistryPolicy.h holds that decision.
// A creation row the reconciliation cannot finish becomes `failed`: it is never used, and only `.tb pool prune` (an explicit
// GM command that re-verifies against the database) removes it or the account it names.
//
// Every statement goes through the characters-database guard of compat/TbCompat_WL0.h: a missing table or column is one error
// line and the pool is off (it does less), never the core's abort (MySQLConnection.cpp).
//
// The rows are read once per config load, on the world thread. `last_login` / `last_logout` are the scheduler's (round W0-B).
#pragma once

#include "ai/playerbot/policy/world/PoolRegistryPolicy.h"

#include <cstdint>
#include <string>
#include <vector>

namespace TortoiseBots
{
namespace pool
{

struct Account
{
    std::uint32_t id = 0;                 // 0 while a claim has no account yet
    std::string username;
    std::string stamp;                    // the creation stamp (16 uppercase hex digits), empty on a row from before it
    world::PoolRowState state = world::PoolRowState::Creating;
    std::int64_t created = 0;
};

struct Character
{
    std::uint64_t guid = 0;               // 0 while the row has no character yet
    std::string name;
    std::uint32_t accountId = 0;
    std::uint8_t race = 0;
    std::uint8_t klass = 0;
    std::uint8_t gender = 0;
    world::PoolRowState state = world::PoolRowState::Creating;
    std::int64_t created = 0;
    std::int64_t lastLogin = 0;
    std::int64_t lastLogout = 0;
};

// One character per pool account (world::kPoolCharactersPerAccount): one session per account means a second character on
// an account could never be online.
constexpr std::uint32_t kCharactersPerAccount = world::kPoolCharactersPerAccount;

// All account and character rows as the database holds them, with no trust filter: the reconciliation's read.
struct Rows
{
    std::vector<Account> accounts;
    std::vector<Character> characters;
};

// TortoiseBots.Pool.Enable, .CreatePerMinute and .MaxCharacters, read now and again on `.reload config` (the rows are re-read
// then too). Also called at boot from TbScripts.cpp.
void LoadConfig();

// True when the key is on AND both tables are there. One error line when an enabled pool has no tables.
bool Enabled();

// How many characters one creation tick may make, per minute (1..600; 0 = the key is off).
std::uint32_t CreatePerMinute();

// TortoiseBots.Pool.MaxCharacters: the ceiling on the pool's characters, ready or being created.
std::uint32_t MaxCharacters();

// TortoiseBots.Pool.CensusComposition (default 1): new characters follow the live server's class, race and gender shares
// (PoolCompositionPolicy.h); 0 keeps the balanced vanilla sequence.
bool CensusComposition();

// The rows that are usable: ready rows whose account still is the pool's (the stamp, name and id agree), and the ready
// characters of those accounts. Read on first use, and again while a row's account could not be checked (the auth database gave
// no answer: such a row is neither used nor marked stale). A creation never trusts this list for an account: VerifyAccount.
std::vector<Account> const& Accounts();
std::vector<Character> const& Characters();

bool AccountRegistered(std::uint32_t accountId);
std::uint32_t CharactersOnAccount(std::uint32_t accountId);
// The first account with room for another character (< 10 of the spec's cap, by the database's own count), or 0.
std::uint32_t AccountWithRoom();

// `TBPOOL<nnnn>`: one past the highest number any row holds (0001 when there is none), so a stale or half-made name is
// never reused.
std::string NextAccountName();

// The number the cap counts, from the database (see the definition): the characters present on the pool's accounts, plus every
// non-ready character row whose character is not there. kPoolCountUnknown = the characters database did not answer; a caller
// refuses on it and never treats it as zero.
constexpr std::uint32_t kPoolCountUnknown = 0xffffffffu;
std::uint32_t PoolCharacters();

// How many account rows the registry holds, in any state (the cap bounds the number of pool accounts too).
std::uint32_t PoolAccountRows();

// The characters the database holds on one account, now. False = no answer.
bool CharactersOnAccountNow(std::uint32_t accountId, std::uint32_t& count);

// A failed pool account that is proven the pool's and has nothing on it (no ready or claimed row, none in the database): a
// creation uses it before it makes another account. False when there is none.
bool FindReusableAccount(Account& out);

// The account a character is about to go on, re-read from the database (the registry row and the account manager), never from
// the cached list: Trusted only when the row names this id, is not stale, the id still answers the row's pool username, that
// name still resolves to the id, and the account carries the row's stamp. `why` says what failed.
world::PoolAccountCheck VerifyAccount(std::uint32_t accountId, std::string const& username, std::string& why);

// Drops an account (and its characters) from the cached usable lists, after a verification that failed.
void ForgetAccount(std::uint32_t accountId);

// TortoiseBots.Pool.Enable as last read, without the schema check (a running job asks it every tick).
bool KeyOn();

// The database's rows, no trust filter; and the next Accounts()/Characters()/ReadRows() re-reads everything.
Rows ReadRows();
void Invalidate();

// The trust decision of one row (ai/playerbot/policy/world/PoolRegistryPolicy.h), from the account manager's answers: the
// reconciliation of unfinished creations uses it before it adopts or removes anything.
world::PoolAccountCheck CheckAccount(Account const& account);

// The core's own `characters` table, through compat/TbCompat_WL0.h: never the character cache, which can miss a row a
// queued commit has not written. `false` = the characters database did not answer, which a caller must never read as "no
// character": it keeps its claim and retries.
bool CharacterOwner(std::string const& name, bool& found, std::uint64_t& guid, std::uint32_t& accountId);

struct OwnedCharacter
{
    std::uint64_t guid = 0;
    std::uint32_t accountId = 0;
    std::string name;
    bool online = false;
};

// Every character the database lists for the account. False = it did not answer (or answered short).
bool AccountCharacters(std::uint32_t accountId, std::vector<OwnedCharacter>& out);

// Account rows. A write reads its own row back: true only when the database really holds it, and the logged row list is
// changed only then.
bool FindAccountRow(std::string const& username, Account& out);
bool StoreAccount(Account const& account);         // INSERT (a claim in `creating`)
bool UpdateAccount(Account const& account);        // UPDATE by username
bool DeleteAccountRow(std::string const& username);

// Character rows, keyed by name while the character does not exist yet (the name is the pool's own reservation).
bool FindCharacterRow(std::string const& name, Character& out);
bool StoreCharacter(Character const& character);   // INSERT (a claim in `creating`, guid 0)
bool UpdateCharacter(Character const& character);  // UPDATE by name
bool DeleteCharacterRow(std::string const& name);

// The scheduler's stamps, written when a pool bot logs in and when its session has ended (unix seconds). Each writes its one
// column through the guard and reads the row back; false = the database does not hold it (one error line), and the scheduler
// carries on with the time it knows.
bool MarkLogin(std::string const& name, std::int64_t when);
bool MarkLogout(std::string const& name, std::int64_t when);

} // namespace pool
} // namespace TortoiseBots
