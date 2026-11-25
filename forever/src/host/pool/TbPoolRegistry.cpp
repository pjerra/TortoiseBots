// W0 (FOREVER-W0-W4-SPEC.md, round W0-A, fix round): the pool registry of host/pool/TbPoolRegistry.h.
//
// The DDL is forever/sql/characters/2026_11_20_02_tb_pool_rows.sql, kept here as text and run once when the key is on, so an
// enabled server works without the migration. ForeverCore aborts the world on a statement naming a missing table or column
// (MySQLConnection.cpp), so every statement the registry makes goes through the guard of compat/TbCompat_WL0.h, which asks
// information_schema first. The column check is repeated before every write: a table dropped or altered after the first check
// must switch the pool off, not reach the core.
//
// A row is trusted only when its account still answers the row's username, that username is a pool name, the username still
// resolves to that id, and the account carries the row's creation stamp (the account's e-mail; see
// ai/playerbot/policy/world/PoolRegistryPolicy.h). Every other row is marked `stale` once and never acted on.
#include "host/pool/TbPoolRegistry.h"

#include "compat/TbCompat_WL0.h"

#include "AccountMgr.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "StringFormat.h"

#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <set>
#include <string>

// The guard's one log line: global, as the header declares it (the shim's `sLog` is not the core's logger; this TU sees TC_LOG_ERROR).
void PoolGuardLogCompat(std::string const& message)
{
    TC_LOG_ERROR("tortoisebots", "{}", message);
}

namespace TortoiseBots
{
namespace pool
{
namespace
{
char const* const kAccountTable = "tb_pool_account";
char const* const kCharacterTable = "tb_pool_character";
char const* const kAccountColumns = "`username`, `account_id`, `stamp`, `state`, `created`";
char const* const kCharacterColumns = "`name`, `guid`, `account_id`, `race`, `class`, `gender`, `state`, `created`, `last_login`, `last_logout`";
char const* const kSelectAccounts = "SELECT `username`, `account_id`, `stamp`, `state`, `created` FROM `tb_pool_account`";
char const* const kSelectCharacters = "SELECT `name`, `guid`, `account_id`, `race`, `class`, `gender`, `state`, `created`, `last_login`, `last_logout` FROM `tb_pool_character`";
char const* const kCreateAccountTable =
    "CREATE TABLE IF NOT EXISTS `tb_pool_account` ("
    "`username` varchar(16) NOT NULL,"
    "`account_id` int unsigned DEFAULT NULL,"
    "`stamp` char(16) NOT NULL DEFAULT '',"
    "`state` varchar(8) NOT NULL DEFAULT 'creating',"
    "`created` bigint NOT NULL DEFAULT '0',"
    "PRIMARY KEY (`username`),"
    "UNIQUE KEY `account_id` (`account_id`)"
    ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4";
char const* const kCreateCharacterTable =
    "CREATE TABLE IF NOT EXISTS `tb_pool_character` ("
    "`name` varchar(12) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL,"
    "`guid` bigint unsigned DEFAULT NULL,"
    "`account_id` int unsigned NOT NULL,"
    "`race` tinyint unsigned NOT NULL,"
    "`class` tinyint unsigned NOT NULL,"
    "`gender` tinyint unsigned NOT NULL,"
    "`state` varchar(8) NOT NULL DEFAULT 'creating',"
    "`created` bigint NOT NULL DEFAULT '0',"
    "`last_login` bigint NOT NULL DEFAULT '0',"
    "`last_logout` bigint NOT NULL DEFAULT '0',"
    "PRIMARY KEY (`name`),"
    "UNIQUE KEY `guid` (`guid`)"
    ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4";

struct State
{
    bool configured = false;
    bool enabled = false;
    bool tableReady = false;
    bool tableWarned = false;
    bool tableTried = false;    // the module's own two CREATEs ran once for this config
    bool loadTried = false;
    bool unknownRows = false;       // a ready row the auth database gave no answer for: not stale, not usable, asked again on the next read
    std::uint32_t createPerMinute = 30;
    std::uint32_t maxCharacters = world::DefaultMaxPoolCharacters;
    bool censusComposition = true;
    std::vector<Account> accounts;
    std::vector<Character> characters;
    std::uint32_t pendingCharacters = 0;    // rows whose character is not made (or not registered) yet: they count toward the cap
};

State& Get()
{
    static State state;
    return state;
}

// SQL text for one of the pool's own values: every value here is a pool name, a stamp or a number, so the two escapes are
// the whole of it, and no caller ever passes user text.
std::string SqlText(std::string const& value)
{
    std::string out = "'";
    for (char const c : value)
    {
        if (c == '\'' || c == '\\')
            out += '\\';
        out += c;
    }
    out += '\'';
    return out;
}

// The information_schema check of both tables' columns. Runs on a synchronous connection: a queued statement could race it. Called
// before the load and again before EVERY write (a cached `true` is never trusted, as in the commander store).
bool CheckSchema(State& s)
{
    if (!s.enabled)
        return false;
    if (!s.tableTried)
    {
        s.tableTried = true;
        PoolCharacterExecuteCompat(kCreateAccountTable);      // on a database whose user may not create tables the rows are simply not stored
        PoolCharacterExecuteCompat(kCreateCharacterTable);
    }
    std::set<std::string> const accountColumns = { "username", "account_id", "stamp", "state", "created" };
    std::set<std::string> const characterColumns = { "name", "guid", "account_id", "race", "class", "gender", "state", "created", "last_login", "last_logout" };
    s.tableReady = CharacterSchemaHasCompat(kAccountTable, accountColumns) && CharacterSchemaHasCompat(kCharacterTable, characterColumns);
    if (!s.tableReady && !s.tableWarned)
    {
        s.tableWarned = true;
        TC_LOG_ERROR("tortoisebots", "TortoiseBots: TortoiseBots.Pool.Enable is on but `{}` or `{}` is missing or lacks a column: the bot pool is off. Apply forever/sql/characters/2026_11_20_01_tb_pool.sql and 2026_11_20_02_tb_pool_rows.sql.",
            kAccountTable, kCharacterTable);
    }
    return s.tableReady;
}

// The guard answers a null result for an empty table and for a read that failed alike. A caller that must tell them apart passes
// `answered`: a null result is then confirmed by a COUNT of the table, and only a count of 0 is an empty table.
bool PoolTableEmpty(char const* table)
{
    std::uint64_t count = 1;
    return PoolRowCountCompat(table, count) && count == 0;
}

std::vector<Account> ReadAccounts(bool* answered = nullptr)
{
    std::vector<Account> rows;
    QueryResult result = PoolCharacterQueryCompat(kSelectAccounts);
    if (answered)
        *answered = bool(result) || PoolTableEmpty(kAccountTable);
    if (!result)
        return rows;
    // The core's Query hands the result over standing on its first row (an empty answer is a null result), so that row is read
    // before the loop advances.
    do
    {
        Field* fields = result->Fetch();
        Account row;
        row.username = fields[0].GetString();
        row.id = fields[1].GetUInt32();          // NULL (a claim) reads as 0
        row.stamp = fields[2].GetString();
        row.state = world::PoolRowStateFromText(fields[3].GetString());
        row.created = std::int64_t(fields[4].GetUInt64());
        if (!row.username.empty())
            rows.push_back(std::move(row));
    } while (result->NextRow());
    return rows;
}

std::vector<Character> ReadCharacters(bool* answered = nullptr)
{
    std::vector<Character> rows;
    QueryResult result = PoolCharacterQueryCompat(kSelectCharacters);
    if (answered)
        *answered = bool(result) || PoolTableEmpty(kCharacterTable);
    if (!result)
        return rows;
    do
    {
        Field* fields = result->Fetch();
        Character row;
        row.name = fields[0].GetString();
        row.guid = fields[1].GetUInt64();        // NULL (a claim) reads as 0
        row.accountId = fields[2].GetUInt32();
        row.race = fields[3].GetUInt8();
        row.klass = fields[4].GetUInt8();
        row.gender = fields[5].GetUInt8();
        row.state = world::PoolRowStateFromText(fields[6].GetString());
        row.created = std::int64_t(fields[7].GetUInt64());
        row.lastLogin = std::int64_t(fields[8].GetUInt64());
        row.lastLogout = std::int64_t(fields[9].GetUInt64());
        if (!row.name.empty() && row.accountId)
            rows.push_back(std::move(row));
    } while (result->NextRow());
    return rows;
}

// The account side of a row, from the core's account manager: no auth SQL of the module's own.
// `unanswered` is set when a read that has to answer for a found account did not: the e-mail, or the id of the row's own username
// (a name the account manager just gave for this very id). That is never a fact about the row, so it never marks one stale.
world::PoolAccountFacts FactsOf(std::uint32_t accountId, std::string const& username, bool* unanswered = nullptr)
{
    world::PoolAccountFacts facts;
    std::string name;
    if (accountId && AccountMgr::GetName(accountId, name) && !name.empty())
    {
        facts.found = true;
        facts.name = name;
    }
    facts.idOfName = AccountMgr::GetId(username);
    std::string email;
    bool const haveEmail = accountId && AccountMgr::GetEmail(accountId, email);
    if (haveEmail)
        facts.email = email;
    if (unanswered && facts.found)
    {
        bool const sameName = world::NormalizePoolAccountName(facts.name) == world::NormalizePoolAccountName(username);
        if (!haveEmail || (sameName && !facts.idOfName))
            *unanswered = true;
    }
    return facts;
}

world::PoolAccountCheck Check(Account const& account)
{
    world::PoolAccountClaim claim;
    claim.accountId = account.id;
    claim.username = account.username;
    claim.stamp = account.stamp;
    bool unanswered = false;
    world::PoolAccountFacts const facts = FactsOf(account.id, account.username, &unanswered);
    if (unanswered)
        return world::PoolAccountCheck::NoAccount;       // unknown, handled like an account the lookup did not find: never stale
    return world::CheckPoolAccount(claim, facts);
}

bool SameAccount(Account const& a, Account const& b)
{
    return a.id == b.id && a.username == b.username && a.stamp == b.stamp && a.state == b.state && a.created == b.created;
}

bool SameCharacter(Character const& a, Character const& b)
{
    return a.guid == b.guid && a.name == b.name && a.accountId == b.accountId && a.race == b.race && a.klass == b.klass &&
        a.gender == b.gender && a.state == b.state && a.created == b.created;
}

bool Writable(State& s)
{
    if (!s.configured)
        LoadConfig();
    return CheckSchema(s);
}

// The memory the ready lists are built from: a trusted ready account, and the ready characters of trusted accounts.
bool KnownAccount(State const& s, std::uint32_t accountId)
{
    return std::any_of(s.accounts.begin(), s.accounts.end(), [&](Account const& row) { return row.id == accountId; });
}

void RememberAccount(State& s, Account const& account)
{
    auto const found = std::find_if(s.accounts.begin(), s.accounts.end(), [&](Account const& row) { return row.username == account.username; });
    if (account.state == world::PoolRowState::Ready && Check(account) == world::PoolAccountCheck::Trusted)
    {
        if (found == s.accounts.end())
            s.accounts.push_back(account);
        else
            *found = account;
    }
    else if (found != s.accounts.end())
        s.accounts.erase(found);
}

void RememberCharacter(State& s, Character const& character, bool beforeWasCreating)
{
    if (beforeWasCreating && s.pendingCharacters)
        --s.pendingCharacters;
    if (character.state == world::PoolRowState::Creating)
    {
        ++s.pendingCharacters;
        return;
    }
    auto const found = std::find_if(s.characters.begin(), s.characters.end(), [&](Character const& row) { return row.name == character.name; });
    // Only a row a caller wrote after trusting its account reaches here (creation's own account, or the reconciliation's
    // finished claim); the read path filters the database's rows by account trust in LoadRows.
    if (character.state == world::PoolRowState::Ready && character.guid)
    {
        if (found == s.characters.end())
            s.characters.push_back(character);
        else
            *found = character;
    }
    else if (found != s.characters.end())
        s.characters.erase(found);
}

// One UPDATE of an account row, no memory change: the read-back of a write. False = the database does not hold the update.
bool WriteAccount(Account const& account)
{
    State& s = Get();
    if (account.username.empty() || !Writable(s))
        return false;
    PoolCharacterExecuteCompat(Trinity::StringFormat(
        "UPDATE `tb_pool_account` SET `account_id` = {}, `stamp` = {}, `state` = {} WHERE `username` = {}",
        account.id ? std::to_string(account.id) : std::string("NULL"), SqlText(account.stamp), SqlText(world::PoolRowStateText(account.state)),
        SqlText(account.username)));
    std::vector<Account> const rows = ReadAccounts();
    auto const now = std::find_if(rows.begin(), rows.end(), [&](Account const& row) { return row.username == account.username; });
    if (now == rows.end() || !SameAccount(*now, account))
    {
        TC_LOG_ERROR("tortoisebots", "TortoiseBots: pool account row {} was not updated (the row is not there as written after the write)", account.username);
        return false;
    }
    return true;
}

bool WriteCharacter(Character const& character)
{
    State& s = Get();
    if (character.name.empty() || !Writable(s))
        return false;
    PoolCharacterExecuteCompat(Trinity::StringFormat(
        "UPDATE `tb_pool_character` SET `guid` = {}, `state` = {} WHERE `name` = {}",
        character.guid ? std::to_string(character.guid) : std::string("NULL"), SqlText(world::PoolRowStateText(character.state)), SqlText(character.name)));
    std::vector<Character> const rows = ReadCharacters();
    auto const now = std::find_if(rows.begin(), rows.end(), [&](Character const& row) { return row.name == character.name; });
    if (now == rows.end() || !SameCharacter(*now, character))
    {
        TC_LOG_ERROR("tortoisebots", "TortoiseBots: pool character row {} was not updated (the row is not there as written after the write)", character.name);
        return false;
    }
    return true;
}

// One read of both tables: every ready row is checked against the account manager; a row that is not the pool's any more is
// marked stale (once) and dropped from use, and so are the characters of such accounts. A claim (`creating`) is left to the
// reconciliation (TbPoolCreate's Reconcile) and never used.
void LoadRows(State& s)
{
    s.accounts.clear();
    s.characters.clear();
    s.pendingCharacters = 0;
    s.unknownRows = false;
    std::vector<Account> accounts = ReadAccounts();
    std::size_t staleAccounts = 0;
    std::size_t unknownAccounts = 0;
    std::size_t claims = 0;
    for (Account& account : accounts)
    {
        // `failed` and `stale` rows are never used: the reconciliation's verdicts wait for `.tb pool prune`.
        if (account.state == world::PoolRowState::Stale || account.state == world::PoolRowState::Failed)
            continue;
        if (account.state == world::PoolRowState::Creating)
        {
            ++claims;
            continue;
        }
        world::PoolAccountCheck check = Check(account);
        if (check == world::PoolAccountCheck::NoAccount)
        {
            // The account manager gave no answer for the id. That is "not there" for a deleted account and "unknown" for a lookup
            // that failed, and it cannot tell them apart, so it never marks a row stale. Only a positive fact does: the row's
            // username resolving to a different id (a re-created account) is a reuse.
            world::PoolAccountFacts const facts = FactsOf(account.id, account.username);
            if (facts.idOfName && facts.idOfName != account.id)
                check = world::PoolAccountCheck::NameReused;
            else
            {
                ++unknownAccounts;
                s.unknownRows = true;
                continue;
            }
        }
        if (check != world::PoolAccountCheck::Trusted)
        {
            TC_LOG_ERROR("tortoisebots", "TortoiseBots: pool account row {} (id #{}) is stale: {}. It is marked and its account and characters are never touched.",
                account.username, account.id, world::PoolAccountCheckText(check));
            account.state = world::PoolRowState::Stale;
            WriteAccount(account);
            ++staleAccounts;
            continue;
        }
        s.accounts.push_back(std::move(account));
    }
    std::vector<Character> characters = ReadCharacters();
    std::size_t droppedCharacters = 0;
    for (Character& character : characters)
    {
        if (character.state == world::PoolRowState::Creating)
        {
            ++s.pendingCharacters;
            continue;
        }
        if (character.state == world::PoolRowState::Stale || character.state == world::PoolRowState::Failed || !character.guid)
            continue;
        if (!KnownAccount(s, character.accountId))
        {
            ++droppedCharacters;
            continue;
        }
        s.characters.push_back(std::move(character));
    }
    if (claims || staleAccounts || droppedCharacters || unknownAccounts)
        TC_LOG_ERROR("tortoisebots", "TortoiseBots: the pool registry holds {} unfinished creation row(s) (finished or removed at start and before a create), marked {} stale row(s), could not check {} account(s) (asked again at the next read) and ignored {} character(s) of an untrusted account",
            claims, staleAccounts, unknownAccounts, droppedCharacters);
}
} // namespace

void LoadConfig()
{
    State& s = Get();
    bool const reload = s.configured;
    s.configured = true;
    s.enabled = sConfigMgr->GetBoolDefault("TortoiseBots.Pool.Enable", false);
    s.createPerMinute = std::uint32_t(std::clamp<int32>(sConfigMgr->GetIntDefault("TortoiseBots.Pool.CreatePerMinute", 30), 0, 600));
    s.maxCharacters = std::uint32_t(std::clamp<int32>(sConfigMgr->GetIntDefault("TortoiseBots.Pool.MaxCharacters", int32(world::DefaultMaxPoolCharacters)), 0, 100000));
    s.censusComposition = sConfigMgr->GetBoolDefault("TortoiseBots.Pool.CensusComposition", true);
    if (!reload)
        return;
    // A config reload re-reads everything: the keys, the two tables and the rows. A server started with the key off and turned on
    // with `.reload config` must create and read the rows then.
    s.tableReady = false;
    s.tableWarned = false;
    s.tableTried = false;
    s.loadTried = false;
    s.unknownRows = false;
    s.accounts.clear();
    s.characters.clear();
    s.pendingCharacters = 0;
}

bool KeyOn()
{
    State& s = Get();
    if (!s.configured)
        LoadConfig();
    return s.enabled;
}

bool Enabled()
{
    State& s = Get();
    if (!s.configured)
        LoadConfig();
    if (!s.enabled)
        return false;
    return CheckSchema(s);
}

std::uint32_t CreatePerMinute()
{
    State& s = Get();
    if (!s.configured)
        LoadConfig();
    return s.enabled ? s.createPerMinute : 0;
}

std::uint32_t MaxCharacters()
{
    State& s = Get();
    if (!s.configured)
        LoadConfig();
    return s.enabled ? s.maxCharacters : 0;
}

bool CensusComposition()
{
    State& s = Get();
    if (!s.configured)
        LoadConfig();
    return s.censusComposition;
}

std::vector<Account> const& Accounts()
{
    State& s = Get();
    if (!s.configured)
        LoadConfig();
    if (!s.enabled || (!s.tableReady && !CheckSchema(s)))
    {
        static std::vector<Account> const empty;
        return empty;
    }
    if (!s.loadTried || s.unknownRows)
    {
        s.loadTried = true;
        LoadRows(s);
    }
    return s.accounts;
}

std::vector<Character> const& Characters()
{
    State& s = Get();
    if (!s.configured)
        LoadConfig();
    if (!s.enabled || (!s.tableReady && !CheckSchema(s)))
    {
        static std::vector<Character> const empty;
        return empty;
    }
    if (!s.loadTried || s.unknownRows)
    {
        s.loadTried = true;
        LoadRows(s);
    }
    return s.characters;
}

bool AccountRegistered(std::uint32_t accountId)
{
    for (Account const& account : Accounts())
        if (account.id == accountId)
            return true;
    return false;
}

std::uint32_t CharactersOnAccount(std::uint32_t accountId)
{
    std::uint32_t count = 0;
    for (Character const& character : Characters())
        if (character.accountId == accountId)
            ++count;
    return count;
}

bool CharactersOnAccountNow(std::uint32_t accountId, std::uint32_t& count)
{
    std::uint64_t n = 0;
    count = 0;
    if (!accountId || !PoolCharactersOnAccountsCompat(std::set<std::uint32_t>{ accountId }, n))
        return false;
    count = std::uint32_t(std::min<std::uint64_t>(n, 0xffffffffull));
    return true;
}

std::uint32_t AccountWithRoom()
{
    // The first account with room is the one creation fills. A copy: the reads below can re-read the registry.
    std::vector<Account> const accounts = Accounts();
    for (Account const& account : accounts)
    {
        std::uint32_t present = 0;
        if (!CharactersOnAccountNow(account.id, present))
            continue;                                     // no answer is no room
        if (world::PoolHasRoomForCharacter(std::max(CharactersOnAccount(account.id), present), kCharactersPerAccount))
            return account.id;
    }
    return 0;
}

bool FindReusableAccount(Account& out)
{
    State& s = Get();
    if (!s.configured)
        LoadConfig();
    if (!s.enabled || (!s.tableReady && !CheckSchema(s)))
        return false;
    std::vector<Character> const characters = ReadCharacters();
    for (Account const& row : ReadAccounts())
    {
        if (row.state != world::PoolRowState::Failed || !row.id || Check(row) != world::PoolAccountCheck::Trusted)
            continue;
        bool used = false;
        for (Character const& character : characters)
            if (character.accountId == row.id && (character.state == world::PoolRowState::Ready || character.state == world::PoolRowState::Creating))
                used = true;
        std::uint32_t present = 0;
        if (used || !CharactersOnAccountNow(row.id, present) || present)
            continue;
        out = row;
        return true;
    }
    return false;
}

std::uint32_t PoolAccountRows()
{
    State& s = Get();
    if (!s.configured)
        LoadConfig();
    if (!s.enabled || (!s.tableReady && !CheckSchema(s)))
        return 0;
    // A stale row can never hold a character (it is never used), so it does not count toward the account cap: a migration that
    // turned the first rounds' accounts stale must not leave the pool unable to make a new one.
    std::vector<Account> const accounts = ReadAccounts();
    return std::uint32_t(std::count_if(accounts.begin(), accounts.end(), [](Account const& row) { return row.state != world::PoolRowState::Stale; }));
}

world::PoolAccountCheck VerifyAccount(std::uint32_t accountId, std::string const& username, std::string& why)
{
    Account row;
    if (!accountId || !FindAccountRow(username, row))
    {
        why = "it has no registry row";
        return world::PoolAccountCheck::NoAccount;
    }
    if (row.id != accountId)
    {
        why = "its registry row names another account id";
        return world::PoolAccountCheck::NoAccount;
    }
    if (row.state == world::PoolRowState::Stale)
    {
        why = "its registry row is stale";
        return world::PoolAccountCheck::NoAccount;
    }
    world::PoolAccountCheck const check = Check(row);
    why = world::PoolAccountCheckText(check);
    return check;
}

void ForgetAccount(std::uint32_t accountId)
{
    State& s = Get();
    s.accounts.erase(std::remove_if(s.accounts.begin(), s.accounts.end(), [&](Account const& row) { return row.id == accountId; }), s.accounts.end());
    s.characters.erase(std::remove_if(s.characters.begin(), s.characters.end(), [&](Character const& row) { return row.accountId == accountId; }), s.characters.end());
}

std::string NextAccountName()
{
    State& s = Get();
    if (!s.configured)
        LoadConfig();
    std::uint32_t highest = 0;
    if (s.enabled && (!s.tableReady || CheckSchema(s)))
    {
        // Every row counts, stale and half-made ones too: a name the registry ever wrote is never handed out again.
        for (Account const& account : ReadAccounts())
        {
            if (!world::IsPoolAccountName(account.username))
                continue;
            std::string const upper = world::NormalizePoolAccountName(account.username);
            std::uint32_t const number = std::uint32_t(std::strtoul(upper.c_str() + 6, nullptr, 10));
            highest = std::max(highest, number);
        }
    }
    return world::PoolAccountName(highest + 1);
}

std::uint32_t PoolCharacters()
{
    State& s = Get();
    if (!s.configured)
        LoadConfig();
    if (!s.enabled || (!s.tableReady && !CheckSchema(s)))
        return 0;
    // What the cap counts is what the database holds, not what a list remembers: every character the database has on an
    // account the registry names (any state of its row), plus every character row that is not ready and whose character the
    // database does not show (a claim, or a failed or stale row: counted until `.tb pool prune` removes it). An account
    // that the auth database says is not a pool account is somebody else's and is not counted; one it gave no answer for is
    // unknown and is counted. No answer from the characters database is "unknown", never "empty".
    bool accountsAnswered = false, charactersAnswered = false;
    std::vector<Account> const accounts = ReadAccounts(&accountsAnswered);
    std::vector<Character> const characters = ReadCharacters(&charactersAnswered);
    if (!accountsAnswered || !charactersAnswered)
        return kPoolCountUnknown;                          // a failed row read is unknown, never an empty table
    std::set<std::uint32_t> ids;
    auto const consider = [&ids](std::uint32_t id)
    {
        if (!id)
            return;
        std::string name;
        if (AccountMgr::GetName(id, name) && !name.empty() && !world::IsPoolAccountName(name))
            return;
        ids.insert(id);
    };
    for (Account const& account : accounts)
        consider(account.id);
    for (Character const& character : characters)
        consider(character.accountId);
    std::uint64_t total = 0;
    if (!PoolCharactersOnAccountsCompat(ids, total))
        return kPoolCountUnknown;
    for (Character const& character : characters)
    {
        if (character.state == world::PoolRowState::Ready)
            continue;
        bool found = false;
        std::uint64_t guid = 0;
        std::uint32_t owner = 0;
        if (!PoolCharacterOwnerCompat(character.name, found, guid, owner) || !found)
            ++total;
    }
    return std::uint32_t(std::min<std::uint64_t>(total, kPoolCountUnknown - 1));
}

Rows ReadRows()
{
    State& s = Get();
    Rows rows;
    if (!s.configured)
        LoadConfig();
    if (!s.enabled || (!s.tableReady && !CheckSchema(s)))
        return rows;
    rows.accounts = ReadAccounts();
    rows.characters = ReadCharacters();
    return rows;
}

world::PoolAccountCheck CheckAccount(Account const& account)
{
    return Check(account);
}

bool CharacterOwner(std::string const& name, bool& found, std::uint64_t& guid, std::uint32_t& accountId)
{
    return PoolCharacterOwnerCompat(name, found, guid, accountId);
}

bool AccountCharacters(std::uint32_t accountId, std::vector<OwnedCharacter>& out)
{
    std::vector<PoolCoreCharacter> rows;
    out.clear();
    if (!PoolAccountCharactersCompat(accountId, rows))
        return false;
    for (PoolCoreCharacter& row : rows)
    {
        OwnedCharacter character;
        character.guid = row.guid;
        character.accountId = row.accountId;
        character.name = std::move(row.name);
        character.online = row.online;
        out.push_back(std::move(character));
    }
    return true;
}

void Invalidate()
{
    State& s = Get();
    s.loadTried = false;
    s.accounts.clear();
    s.characters.clear();
    s.pendingCharacters = 0;
}

bool FindAccountRow(std::string const& username, Account& out)
{
    State& s = Get();
    if (!Writable(s))
        return false;
    for (Account const& row : ReadAccounts())
    {
        if (row.username == username)
        {
            out = row;
            return true;
        }
    }
    return false;
}

bool StoreAccount(Account const& account)
{
    State& s = Get();
    if (account.username.empty() || !Writable(s))
        return false;
    PoolCharacterExecuteCompat(Trinity::StringFormat(
        "INSERT INTO `tb_pool_account` ({}) VALUES ({}, {}, {}, {}, {})", kAccountColumns, SqlText(account.username),
        account.id ? std::to_string(account.id) : std::string("NULL"), SqlText(account.stamp), SqlText(world::PoolRowStateText(account.state)),
        account.created));
    // A one-way statement cannot answer, so the row is read back: memory changes only after the database really holds it.
    std::vector<Account> const rows = ReadAccounts();
    auto const now = std::find_if(rows.begin(), rows.end(), [&](Account const& row) { return row.username == account.username; });
    if (now == rows.end() || !SameAccount(*now, account))
    {
        TC_LOG_ERROR("tortoisebots", "TortoiseBots: pool account row {} was not stored (the row is not there after the write)", account.username);
        return false;
    }
    RememberAccount(s, *now);
    return true;
}

bool UpdateAccount(Account const& account)
{
    State& s = Get();
    if (!WriteAccount(account))
        return false;
    RememberAccount(s, account);
    return true;
}

bool DeleteAccountRow(std::string const& username)
{
    State& s = Get();
    if (username.empty() || !Writable(s))
        return false;
    PoolCharacterExecuteCompat(Trinity::StringFormat("DELETE FROM `tb_pool_account` WHERE `username` = {}", SqlText(username)));
    Account left;
    if (FindAccountRow(username, left))
    {
        TC_LOG_ERROR("tortoisebots", "TortoiseBots: pool account row {} was not deleted (the row is still there after the write)", username);
        return false;
    }
    auto const found = std::find_if(s.accounts.begin(), s.accounts.end(), [&](Account const& row) { return row.username == username; });
    if (found != s.accounts.end())
        s.accounts.erase(found);
    return true;
}

bool FindCharacterRow(std::string const& name, Character& out)
{
    State& s = Get();
    if (!Writable(s))
        return false;
    for (Character const& row : ReadCharacters())
    {
        if (row.name == name)
        {
            out = row;
            return true;
        }
    }
    return false;
}

bool StoreCharacter(Character const& character)
{
    State& s = Get();
    if (character.name.empty() || !character.accountId || !Writable(s))
        return false;
    PoolCharacterExecuteCompat(Trinity::StringFormat(
        "INSERT INTO `tb_pool_character` ({}) VALUES ({}, {}, {}, {}, {}, {}, {}, {}, {}, {})", kCharacterColumns, SqlText(character.name),
        character.guid ? std::to_string(character.guid) : std::string("NULL"), character.accountId, uint32(character.race), uint32(character.klass),
        uint32(character.gender), SqlText(world::PoolRowStateText(character.state)), character.created, character.lastLogin, character.lastLogout));
    std::vector<Character> const rows = ReadCharacters();
    auto const now = std::find_if(rows.begin(), rows.end(), [&](Character const& row) { return row.name == character.name; });
    if (now == rows.end() || !SameCharacter(*now, character))
    {
        TC_LOG_ERROR("tortoisebots", "TortoiseBots: pool character row {} was not stored (the row is not there after the write)", character.name);
        return false;
    }
    RememberCharacter(s, *now, false);
    return true;
}

bool UpdateCharacter(Character const& character)
{
    State& s = Get();
    Character before;
    bool const had = FindCharacterRow(character.name, before);
    if (!WriteCharacter(character))
        return false;
    RememberCharacter(s, character, had && before.state == world::PoolRowState::Creating);
    return true;
}

// W0-B: the scheduler's two stamps (`last_login`, `last_logout`), one column each and read back like every other write. The
// scheduler owns them; nothing else writes them.
bool WriteCharacterStamp(std::string const& name, char const* column, bool login, std::int64_t when)
{
    State& s = Get();
    if (name.empty() || !Writable(s))
        return false;
    PoolCharacterExecuteCompat(Trinity::StringFormat("UPDATE `tb_pool_character` SET `{}` = {} WHERE `name` = {}", column, when, SqlText(name)));
    Character row;
    if (!FindCharacterRow(name, row))
    {
        TC_LOG_ERROR("tortoisebots", "TortoiseBots: pool character row {} was not stamped (the row is not there after the write)", name);
        return false;
    }
    std::int64_t const stored = login ? row.lastLogin : row.lastLogout;
    if (stored != when)
    {
        TC_LOG_ERROR("tortoisebots", "TortoiseBots: pool character row {} did not keep `{}` = {} (it reads {})", name, column, when, stored);
        return false;
    }
    auto const found = std::find_if(s.characters.begin(), s.characters.end(), [&](Character const& cached) { return cached.name == name; });
    if (found != s.characters.end())
        *found = row;
    return true;
}

bool MarkLogin(std::string const& name, std::int64_t when)
{
    return WriteCharacterStamp(name, "last_login", true, when);
}

bool MarkLogout(std::string const& name, std::int64_t when)
{
    return WriteCharacterStamp(name, "last_logout", false, when);
}

bool DeleteCharacterRow(std::string const& name)
{
    State& s = Get();
    if (name.empty() || !Writable(s))
        return false;
    Character before;
    bool const had = FindCharacterRow(name, before);
    PoolCharacterExecuteCompat(Trinity::StringFormat("DELETE FROM `tb_pool_character` WHERE `name` = {}", SqlText(name)));
    Character left;
    if (FindCharacterRow(name, left))
    {
        TC_LOG_ERROR("tortoisebots", "TortoiseBots: pool character row {} was not deleted (the row is still there after the write)", name);
        return false;
    }
    if (had && before.state == world::PoolRowState::Creating && s.pendingCharacters)
        --s.pendingCharacters;
    auto const found = std::find_if(s.characters.begin(), s.characters.end(), [&](Character const& row) { return row.name == name; });
    if (found != s.characters.end())
        s.characters.erase(found);
    return true;
}

} // namespace pool
} // namespace TortoiseBots
