// Standalone test of the pool's registry and creation (forever/src/host/pool/TbPoolRegistry.cpp and TbPoolCreate.cpp):
//   g++ -std=c++17 -O1 -Wall -Wextra -DTORTOISEBOTS_SLICE1 -I forever/tools/pool_registry_fakes -I forever/src -I .
//       forever/tools/pool_registry_check.cpp forever/src/host/pool/TbPoolRegistry.cpp forever/src/host/pool/TbPoolCreate.cpp
//       -o /tmp/pool_registry_check && /tmp/pool_registry_check
//
// The two translation units are the module's own, compiled against a fake core: a fake characters database with ForeverCore's
// result-set contract and the registry's SQL shapes, the core's own `characters` table, a fake auth database behind AccountMgr,
// a fake character cache and a fake BotMgr::CreateCharacter whose saves are queued like the core's. What is exercised is the
// behaviour the rounds fix:
//  - a registry row whose account id now belongs to a real account is marked stale and never used (name, id back-lookup and
//    creation stamp each catch a case);
//  - a creation writes its claim row before the irreversible step, and a crash after either step is finished by Reconcile only
//    from the characters database - a cache miss deletes nothing, and an unanswered database decides nothing;
//  - a creation whose saves are queued is not ready: the row stays `creating` until the characters database has it (and then
//    the cache learns of it); a commit that never lands is marked failed;
//  - Reconcile never deletes; `.tb pool prune` is a dry run unless `confirm`, and removes only a proven pool account whose
//    every character is a pool character;
//  - the total cap refuses a create when the pool is full, and clamps a request that asks for more than fits; it counts every
//    row in any state and the characters the database holds (a failed auth lookup is unknown, never stale);
//  - no character goes on an account that changed after the cache loaded (renamed, deleted, id reused): the account is
//    re-verified by database reads before every character;
//  - the name is settled before an account is made, an empty pool account is reused, the pool's accounts are bounded;
//  - a job keeps the config it started with and stops when the key is turned off;
//  - last round: an existing character is adopted only under the pool's own prior claim; prune reconciles first and keeps an
//    account with a `creating` claim; stale rows leave the account cap and a failed row read is unknown; a half-failed account
//    lookup is never a stale mark; a row is ready only after the login commit;
//  - SQL text reaches the fake database escaped (its EscapeString really escapes, its reads parse a literal strictly).
#include "DatabaseEnv.h"
#include "AccountMgr.h"
#include "CharacterCache.h"
#include "Config.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "ObjectGuid.h"
#include "Random.h"
#include "StringFormat.h"
#include "TbBotMgr.h"
#include "runtime/BotNameService.h"

#include "host/pool/TbPoolCreate.h"
#include "host/pool/TbPoolRegistry.h"

#include <algorithm>
#include <cstdio>
#include <string>

namespace pool = TortoiseBots::pool;

static int g_checks = 0, g_fail = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

namespace
{
char const* const kStamp = "0123456789ABCDEF";
char const* const kOtherStamp = "FEDCBA9876543210";

void Reset()
{
    CharacterDatabase = FakePoolDatabase();
    fakeAccounts.clear();
    fakeNextAccountId = 1;
    fakeCharacterCache.clear();
    fakeBotMgrState = FakeBotMgrState();
    fakeNextCharacterGuid = 1;
    fakeFailNextCreates = 0;
    fakePendingCreations.clear();
    fakeLoginHeld.clear();
    fakePoolEvents.clear();
    TortoiseBots::fakeBotNames.clear();
    TortoiseBots::fakeBotNameIndex = 0;
    fakeAuthDown = false;
    fakeEmailDown = false;
    fakeRefusedNames.clear();
    fakeConfig.bools.clear();
    fakeConfig.ints.clear();
    fakeConfig.bools["TortoiseBots.Pool.Enable"] = true;
    fakeConfig.ints["TortoiseBots.Pool.CreatePerMinute"] = 600;
    fakeConfig.ints["TortoiseBots.Pool.MaxCharacters"] = 50;
    pool::LoadConfig();          // the first call only reads the keys
    pool::LoadConfig();          // the second one is a reload: the rows and the schema cache start empty
}

void Name(std::string name)
{
    TortoiseBots::fakeBotNames = { std::move(name) };
    TortoiseBots::fakeBotNameIndex = 0;
}

void ManyNames(int count)
{
    TortoiseBots::fakeBotNames.clear();
    for (int i = 0; i < count; ++i)
        TortoiseBots::fakeBotNames.push_back("Alarick" + std::to_string(i));
    TortoiseBots::fakeBotNameIndex = 0;
}

void RunJob()
{
    for (int i = 0; i < 200 && pool::Creating(); ++i)
        pool::CreateUpdate(1000000);
}

FakePoolDatabase::AccountRow AccountRow(std::string username, bool hasId, uint32 id, std::string stamp, std::string state)
{
    FakePoolDatabase::AccountRow row;
    row.username = std::move(username);
    row.hasId = hasId;
    row.id = id;
    row.stamp = std::move(stamp);
    row.state = std::move(state);
    row.created = 1;
    return row;
}

void SeedAccount(std::string username, uint32 id, std::string stamp, std::string state)
{
    fakeAccounts.push_back(FakeAccount{ id, username, stamp });
    fakeNextAccountId = std::max(fakeNextAccountId, id + 1);
    CharacterDatabase.accounts.push_back(AccountRow(username, true, id, stamp, state));
}

// A pool registry row only (no cache entry): the rows the database holds, whether or not anything cached them.
void SeedRow(std::string name, uint32 accountId, std::string state, bool hasGuid = false, uint64 guid = 0)
{
    FakePoolDatabase::CharacterRow row;
    row.name = std::move(name);
    row.accountId = accountId;
    row.race = 1;
    row.klass = 1;
    row.gender = 0;
    row.state = std::move(state);
    row.created = 1;
    row.hasGuid = hasGuid;
    row.guid = guid;
    CharacterDatabase.characters.push_back(row);
}

// A character of the core's own `characters` table, which the character cache does not necessarily know.
void SeedDatabaseCharacter(std::string name, uint64 guid, uint32 accountId, bool online = false)
{
    CharacterDatabase.coreCharacters.push_back(FakePoolDatabase::CoreCharacterRow{ std::move(name), guid, accountId, online });
}

// A pool character that exists: its registry row, the core's `characters` row and the cache entry.
void SeedCharacter(std::string name, uint64 guid, uint32 accountId, std::string state)
{
    fakeCharacterCache.push_back(FakeCachedCharacter{ guid, accountId, name, 0, 1, 1 });
    SeedDatabaseCharacter(name, guid, accountId);
    SeedRow(std::move(name), accountId, std::move(state), true, guid);
}

// A pool of `characters` ready characters in an existing account layout (ten per account), every row and account carrying
// its stamp: the cap and prune tests read this shape; new creation places one character per account.
void SeedPool(uint32 characters)
{
    uint32 made = 0;
    for (uint32 number = 1; made < characters; ++number)
    {
        std::string const username = "TBPOOL" + std::to_string(1000 + number);
        uint32 const id = number;
        SeedAccount(username, id, kStamp, "ready");
        for (uint32 onAccount = 0; onAccount < 10 && made < characters; ++onAccount, ++made)
            SeedCharacter("Bot" + std::to_string(made), 1000 + made, id, "ready");
    }
}

template <class T>
T const& At(std::vector<T> const& rows, std::size_t index)
{
    static T const none{};
    return index < rows.size() ? rows[index] : none;
}

int IndexOf(std::string const& event)
{
    for (std::size_t i = 0; i < fakePoolEvents.size(); ++i)
        if (fakePoolEvents[i] == event)
            return int(i);
    return -1;
}

bool found_line(std::vector<std::string> const& lines, std::string const& text)
{
    for (std::string const& line : lines)
        if (line.find(text) != std::string::npos)
            return true;
    return false;
}

std::string const& OnlyAccountStamp()
{
    static std::string const none;
    return CharacterDatabase.accounts.empty() ? none : CharacterDatabase.accounts.front().stamp;
}

void TestIdReuseIsStale()
{
    Reset();
    // The id the registry names now belongs to a real account.
    fakeAccounts.push_back(FakeAccount{ 3, "REALPLAYER", "REAL@EXAMPLE" });
    CharacterDatabase.accounts.push_back(AccountRow("TBPOOL0001", true, 3, kStamp, "ready"));
    SeedCharacter("Poolbot", 500, 3, "ready");

    CHECK(pool::Accounts().empty());
    CHECK(pool::Characters().empty());
    CHECK(pool::AccountWithRoom() == 0);
    CHECK(CharacterDatabase.accounts.size() == 1);
    CHECK(At(CharacterDatabase.accounts, 0).state == "stale");
    // And the real account was not touched.
    CHECK(fakeAccounts.size() == 1 && At(fakeAccounts, 0).name == "REALPLAYER");
}

void TestRenamedRealAccountIsStale()
{
    Reset();
    // Even renamed to the pool's own name, the account's id and name match but its stamp does not.
    fakeAccounts.push_back(FakeAccount{ 3, "TBPOOL0001", "REAL@EXAMPLE" });
    CharacterDatabase.accounts.push_back(AccountRow("TBPOOL0001", true, 3, kStamp, "ready"));

    CHECK(pool::Accounts().empty());
    CHECK(At(CharacterDatabase.accounts, 0).state == "stale");
}

void TestReusedNameIsStale()
{
    Reset();
    // The registry's username now resolves to another account: the row names an id that no longer exists.
    fakeAccounts.push_back(FakeAccount{ 9, "TBPOOL0001", kStamp });
    CharacterDatabase.accounts.push_back(AccountRow("TBPOOL0001", true, 3, kStamp, "ready"));

    CHECK(pool::Accounts().empty());
    CHECK(At(CharacterDatabase.accounts, 0).state == "stale");
}

void TestTrustedRowIsUsed()
{
    Reset();
    SeedAccount("TBPOOL0001", 3, kStamp, "ready");

    CHECK(pool::Accounts().size() == 1);
    CHECK(pool::AccountWithRoom() == 3);                       // empty: room for the pool's one character
    SeedCharacter("Poolbot", 500, 3, "ready");
    pool::LoadConfig();                                        // re-read the rows after the seed
    CHECK(pool::Characters().size() == 1);
    CHECK(pool::AccountWithRoom() == 0);                       // one character is the whole account
    CHECK(pool::AccountRegistered(3));
    CHECK(At(CharacterDatabase.accounts, 0).state == "ready");
}

void TestCreationWritesTheClaimFirst()
{
    Reset();
    Name("Alarick");

    pool::CreateStart const start = pool::StartCreate(1);
    CHECK(start.started);
    RunJob();

    CHECK(fakeAccounts.size() == 1);
    CHECK(At(fakeAccounts, 0).name == "TBPOOL0001");
    CHECK(CharacterDatabase.accounts.size() == 1);
    CHECK(At(CharacterDatabase.accounts, 0).state == "ready");
    CHECK(At(CharacterDatabase.accounts, 0).hasId);
    CHECK(At(CharacterDatabase.accounts, 0).stamp == OnlyAccountStamp());
    CHECK(At(fakeAccounts, 0).email == At(CharacterDatabase.accounts, 0).stamp);   // the stamp is the account's e-mail
    CHECK(CharacterDatabase.characters.size() == 1);
    CHECK(At(CharacterDatabase.characters, 0).name == "Alarick");
    CHECK(At(CharacterDatabase.characters, 0).state == "ready");
    CHECK(At(CharacterDatabase.characters, 0).hasGuid);
    CHECK(pool::PoolCharacters() == 1);

    // The write-ahead order: the account claim is stored before the account is made, the character claim before the character.
    int const accountClaim = IndexOf("INSERT tb_pool_account TBPOOL0001");
    int const accountMade = IndexOf("CreateAccount TBPOOL0001");
    int const characterClaim = IndexOf("INSERT tb_pool_character Alarick");
    int const characterMade = IndexOf("CreateCharacter Alarick");
    CHECK(accountClaim >= 0 && accountMade >= 0 && accountClaim < accountMade);
    CHECK(characterClaim >= 0 && characterMade >= 0 && characterClaim < characterMade);
}

void TestCrashAfterAccountCreationKeepsTheAccount()
{
    Reset();
    Name("Alarick");
    CharacterDatabase.failCharacterInserts = true;    // the character claim cannot land: the account is left without one

    pool::CreateStart start = pool::StartCreate(1);
    CHECK(start.started);
    RunJob();
    CHECK(fakeAccounts.size() == 1);                  // the account was made
    CHECK(CharacterDatabase.accounts.size() == 1);
    CHECK(At(CharacterDatabase.accounts, 0).state == "creating");
    CHECK(CharacterDatabase.characters.empty());

    // The next create reconciles first: the pool's account with no character is marked failed and kept (only
    // `.tb pool prune confirm` may remove one), and the job reuses that empty account instead of making another.
    CharacterDatabase.failCharacterInserts = false;
    Name("Bromley");
    start = pool::StartCreate(1);
    CHECK(start.started);
    RunJob();

    CHECK(fakeAccounts.size() == 1);                  // the orphaned account is the one used
    CHECK(CharacterDatabase.accounts.size() == 1);
    CHECK(At(CharacterDatabase.accounts, 0).state == "ready");
    CHECK(CharacterDatabase.characters.size() == 1);
    CHECK(At(CharacterDatabase.characters, 0).name == "Bromley");
    CHECK(At(CharacterDatabase.characters, 0).state == "ready");
    CHECK(At(CharacterDatabase.characters, 0).accountId == At(fakeAccounts, 0).id);
    CHECK(pool::PoolCharacters() == 1);
}

void TestCrashAfterCharacterCreationIsFinished()
{
    Reset();
    Name("Alarick");
    CharacterDatabase.failCharacterUpdate = true;     // the character is made, its row's `ready` write does not land

    pool::CreateStart start = pool::StartCreate(1);
    CHECK(start.started);
    RunJob();

    CHECK(fakeBotMgrState.createCalls == 1);
    // The cache entry follows the database row's `ready` write, which failed: the creation is not published.
    CHECK(fakeCharacterCache.empty());
    CHECK(CharacterDatabase.characters.size() == 1);
    CHECK(At(CharacterDatabase.characters, 0).name == "Alarick");
    CHECK(At(CharacterDatabase.characters, 0).state == "creating");
    CHECK(!At(CharacterDatabase.characters, 0).hasGuid);
    CHECK(CharacterDatabase.coreCharacters.size() == 1);   // the character itself is in the characters database

    // The next start adopts the character (the characters database has it on the claim's account), does not make it
    // again, and the account row becomes ready; the new job makes one more.
    CharacterDatabase.failCharacterUpdate = false;
    Name("Bromley");
    start = pool::StartCreate(1);
    CHECK(start.started);
    CHECK(fakeBotMgrState.createCalls == 1);          // reconciliation adopted, did not create
    RunJob();

    CHECK(fakeBotMgrState.createCalls == 2);
    CHECK(CharacterDatabase.characters.size() == 2);
    for (FakePoolDatabase::CharacterRow const& row : CharacterDatabase.characters)
    {
        CHECK(row.state == "ready");
        CHECK(row.hasGuid);
        CHECK(row.guid == sCharacterCache->GetCharacterGuidByName(row.name).GetCounter());
    }
    CHECK(pool::Characters().size() == 2);
}

void TestTheTotalCapRefuses()
{
    Reset();
    SeedPool(50);
    CHECK(pool::PoolCharacters() == 50);

    pool::CreateStart const start = pool::StartCreate(1);
    CHECK(!start.started);
    CHECK(start.message.find("cap is 50") != std::string::npos);
    CHECK(start.message.find("MaxCharacters") != std::string::npos);
    CHECK(fakeBotMgrState.createCalls == 0);
}

void TestTheTotalCapClamps()
{
    Reset();
    SeedPool(48);
    TortoiseBots::fakeBotNames = { "Newone", "Newtwo" };
    TortoiseBots::fakeBotNameIndex = 0;

    pool::CreateStart const start = pool::StartCreate(10);
    CHECK(start.started);
    CHECK(start.message.find(" 2 ") != std::string::npos || start.message.find("the 2 that fit") != std::string::npos);
    RunJob();

    CHECK(pool::PoolCharacters() == 50);
    CHECK(CharacterDatabase.characters.size() == 50);
    CHECK(fakeBotMgrState.createCalls == 2);
}

void TestAForeignAccountUnderAClaimIsNeverDeleted()
{
    Reset();
    // A claim row, but the account of that name is somebody else's (different stamp).
    fakeAccounts.push_back(FakeAccount{ 3, "TBPOOL0005", "REAL@EXAMPLE" });
    CharacterDatabase.accounts.push_back(AccountRow("TBPOOL0005", true, 3, kStamp, "creating"));

    pool::Reconcile();

    CHECK(fakeAccounts.size() == 1);                  // never deleted
    CHECK(At(fakeAccounts, 0).name == "TBPOOL0005");
    CHECK(CharacterDatabase.accounts.size() == 1);
    CHECK(At(CharacterDatabase.accounts, 0).state == "stale");
}

void TestAClaimWithNoIdIsResolvedByItsStamp()
{
    Reset();
    // A crash between CreateAccount and the id write: the row has no id, the account's name and stamp are the row's.
    fakeAccounts.push_back(FakeAccount{ 9, "TBPOOL0007", kStamp });
    CharacterDatabase.accounts.push_back(AccountRow("TBPOOL0007", false, 0, kStamp, "creating"));

    pool::Reconcile();

    // The id is resolved; the account has no ready character, so the row is marked failed. The account itself is kept:
    // only `.tb pool prune confirm` removes one.
    CHECK(fakeAccounts.size() == 1);
    CHECK(CharacterDatabase.accounts.size() == 1);
    CHECK(At(CharacterDatabase.accounts, 0).state == "failed");
    CHECK(At(CharacterDatabase.accounts, 0).hasId);
    CHECK(At(CharacterDatabase.accounts, 0).id == 9);
    CHECK(pool::Accounts().empty());
}

void TestAClaimWithNoAccountIsMarkedFailed()
{
    Reset();
    // The claim was written and the account creation never ran (or never landed): the row is marked, never deleted.
    CharacterDatabase.accounts.push_back(AccountRow("TBPOOL0008", false, 0, kStamp, "creating"));

    pool::Reconcile();

    CHECK(fakeAccounts.empty());
    CHECK(CharacterDatabase.accounts.size() == 1);
    CHECK(At(CharacterDatabase.accounts, 0).state == "failed");
    CHECK(pool::Accounts().empty());
}

void TestAClaimWithAStrangersStampIsNotAdopted()
{
    Reset();
    fakeAccounts.push_back(FakeAccount{ 9, "TBPOOL0007", kOtherStamp });
    CharacterDatabase.accounts.push_back(AccountRow("TBPOOL0007", false, 0, kStamp, "creating"));

    pool::Reconcile();

    CHECK(fakeAccounts.size() == 1);                  // never deleted, never used
    CHECK(CharacterDatabase.accounts.size() == 1);
    CHECK(At(CharacterDatabase.accounts, 0).state == "stale");
}

void TestStaleRowsAreNotCounted()
{
    Reset();
    CharacterDatabase.accounts.push_back(AccountRow("TBPOOL0001", true, 3, kStamp, "stale"));
    CHECK(pool::Accounts().empty());
    CHECK(pool::PoolCharacters() == 0);
    CHECK(pool::NextAccountName() == "TBPOOL0002");
}

// The critical finding of the round: a character the cache does not have is not a character that does not exist, and an
// account is never deleted on a claim's word.
void TestACacheMissDoesNotDeleteTheAccount()
{
    Reset();
    // The account and its character are in the characters database; the character row is still `creating` (a crash before its
    // `ready` write), and the cache has no entry for it (a transient miss, or a fresh start).
    SeedAccount("TBPOOL0001", 3, kStamp, "creating");
    SeedRow("Poolbot", 3, "creating");
    SeedDatabaseCharacter("Poolbot", 500, 3);
    CHECK(fakeCharacterCache.empty());

    pool::Reconcile();

    CHECK(fakeAccounts.size() == 1);                          // never deleted on a cache miss
    CHECK(CharacterDatabase.accounts.size() == 1);
    CHECK(At(CharacterDatabase.accounts, 0).state == "ready");
    CHECK(CharacterDatabase.characters.size() == 1);
    CHECK(At(CharacterDatabase.characters, 0).state == "ready");
    CHECK(At(CharacterDatabase.characters, 0).hasGuid);
    CHECK(At(CharacterDatabase.characters, 0).guid == 500);
    CHECK(fakeCharacterCache.size() == 1);                    // the cache follows the database, not the other way round
}

void TestAnUnansweredDatabaseKeepsTheClaim()
{
    Reset();
    SeedAccount("TBPOOL0001", 3, kStamp, "creating");
    SeedRow("Poolbot", 3, "creating");
    CharacterDatabase.failCoreReads = true;                   // the characters database does not answer

    pool::Reconcile();

    CHECK(CharacterDatabase.characters.size() == 1);
    CHECK(At(CharacterDatabase.characters, 0).state == "creating");    // no guess, no verdict
    CHECK(At(CharacterDatabase.accounts, 0).state == "creating");
    CHECK(fakeAccounts.size() == 1);
}

// A creation whose saves are queued is not a creation: the row stays `creating`, nothing is published, and only the
// characters database's answer finishes it.
void TestAQueuedCommitIsNotReady()
{
    Reset();
    Name("Alarick");
    fakeBotMgrState.commitLands = false;                      // the saves are still in flight

    pool::CreateStart const start = pool::StartCreate(1);
    CHECK(start.started);
    pool::CreateUpdate(100);                                  // the tick that creates and queues
    pool::CreateUpdate(100);                                  // the first read-back: too early, nothing has landed

    CHECK(fakeBotMgrState.createCalls == 1);
    CHECK(CharacterDatabase.accounts.size() == 1);
    CHECK(At(CharacterDatabase.accounts, 0).state == "creating");
    CHECK(CharacterDatabase.characters.size() == 1);
    CHECK(At(CharacterDatabase.characters, 0).state == "creating");
    CHECK(!At(CharacterDatabase.characters, 0).hasGuid);
    CHECK(fakeCharacterCache.empty());                        // the cache is not told before the database
    CHECK(pool::PoolCharacters() == 1);                       // the claim counts toward the cap

    // The commit lands: the read-back finds the row on the account and the creation becomes ready.
    fakeCommitPendingCharacters();
    pool::CreateUpdate(500);

    CHECK(CharacterDatabase.characters.size() == 1);
    CHECK(At(CharacterDatabase.characters, 0).state == "ready");
    CHECK(At(CharacterDatabase.characters, 0).hasGuid);
    CHECK(At(CharacterDatabase.accounts, 0).state == "ready");
    CHECK(fakeCharacterCache.size() == 1);
    CHECK(!pool::Creating());
}

void TestACommitThatNeverLandsIsMarkedFailed()
{
    Reset();
    Name("Alarick");
    fakeBotMgrState.commitLands = false;

    pool::CreateStart const start = pool::StartCreate(1);
    CHECK(start.started);
    for (int i = 0; i < 100 && pool::Creating(); ++i)
        pool::CreateUpdate(1000);                             // past kConfirmTimeoutMs: the job stops

    CHECK(CharacterDatabase.characters.size() == 1);
    CHECK(At(CharacterDatabase.characters, 0).state == "failed");
    CHECK(!At(CharacterDatabase.characters, 0).hasGuid);
    CHECK(fakeCharacterCache.empty());
    CHECK(fakeAccounts.size() == 1);                          // the account is kept, only its row is marked
    CHECK(At(CharacterDatabase.accounts, 0).state == "creating");

    // And a reconciliation does not bring it back.
    pool::Reconcile();
    CHECK(At(CharacterDatabase.characters, 0).state == "failed");
    CHECK(At(CharacterDatabase.accounts, 0).state == "failed");
}

// `.tb pool prune`: a failed row is not an invitation to delete an account somebody else owns.
void TestPruneRefusesANonPoolAccount()
{
    Reset();
    // The row names account #3, but #3 is somebody else's (a real e-mail, not the row's stamp).
    fakeAccounts.push_back(FakeAccount{ 3, "TBPOOL0001", "REAL@EXAMPLE" });
    CharacterDatabase.accounts.push_back(AccountRow("TBPOOL0001", true, 3, kStamp, "failed"));
    SeedDatabaseCharacter("Realplayer", 700, 3);

    pool::PruneReport const dry = pool::Prune(false);
    CHECK(dry.enabled && !dry.applied);
    CHECK(dry.accounts == 0);
    CHECK(fakeAccounts.size() == 1);
    CHECK(found_line(dry.lines, "kept"));

    pool::PruneReport const done = pool::Prune(true);
    CHECK(done.applied);
    CHECK(done.accounts == 0 && done.rows == 0);
    CHECK(fakeAccounts.size() == 1);                          // an account that is not the pool's is never removed
    CHECK(CharacterDatabase.accounts.size() == 1);
    CHECK(CharacterDatabase.coreCharacters.size() == 1);
    CHECK(At(fakeAccounts, 0).name == "TBPOOL0001");

    // Even when every character on the account is named by pool rows, an account that does not carry the row's stamp is
    // never removed.
    Reset();
    fakeAccounts.push_back(FakeAccount{ 4, "TBPOOL0002", "REAL@EXAMPLE" });
    CharacterDatabase.accounts.push_back(AccountRow("TBPOOL0002", true, 4, kStamp, "failed"));
    SeedRow("Poolbot", 4, "failed");
    SeedDatabaseCharacter("Poolbot", 700, 4);

    pool::PruneReport const stolen = pool::Prune(true);
    CHECK(stolen.applied);
    CHECK(stolen.accounts == 0);
    CHECK(fakeAccounts.size() == 1);
    CHECK(CharacterDatabase.coreCharacters.size() == 1);
    CHECK(CharacterDatabase.accounts.size() == 1);
}

void TestPruneKeepsAnAccountWithAForeignCharacter()
{
    Reset();
    SeedAccount("TBPOOL0001", 3, kStamp, "failed");
    SeedDatabaseCharacter("Manual", 700, 3);                  // no pool character row names it

    pool::PruneReport const done = pool::Prune(true);
    CHECK(done.applied);
    CHECK(done.accounts == 0);
    CHECK(fakeAccounts.size() == 1);
    CHECK(CharacterDatabase.coreCharacters.size() == 1);
    CHECK(CharacterDatabase.accounts.size() == 1);
    CHECK(found_line(done.lines, "not a pool character"));
}

void TestPruneRemovesOnlyAProvenPoolAccount()
{
    Reset();
    SeedAccount("TBPOOL0001", 3, kStamp, "failed");
    SeedRow("Alarick", 3, "failed");
    SeedRow("Bromley", 3, "failed");
    SeedDatabaseCharacter("Alarick", 500, 3);
    SeedDatabaseCharacter("Bromley", 501, 3);

    pool::PruneReport const dry = pool::Prune(false);
    CHECK(dry.enabled && !dry.applied);
    CHECK(dry.accounts == 1 && dry.characters == 2);
    CHECK(found_line(dry.lines, "would remove"));
    CHECK(fakeAccounts.size() == 1);                          // a dry run removes nothing
    CHECK(CharacterDatabase.accounts.size() == 1);
    CHECK(CharacterDatabase.characters.size() == 2);
    CHECK(CharacterDatabase.coreCharacters.size() == 2);

    pool::PruneReport const done = pool::Prune(true);
    CHECK(done.applied);
    CHECK(done.accounts == 1 && done.characters == 2);
    CHECK(fakeAccounts.empty());
    CHECK(CharacterDatabase.accounts.empty());
    CHECK(CharacterDatabase.characters.empty());
    CHECK(CharacterDatabase.coreCharacters.empty());          // the core's DeleteAccount removed its characters
    CHECK(pool::Accounts().empty());
}

void TestPruneDropsAFailedRowOfALiveAccount()
{
    Reset();
    SeedAccount("TBPOOL0001", 3, kStamp, "ready");
    SeedCharacter("Poolbot", 500, 3, "ready");
    SeedRow("Ghosty", 3, "failed");                           // a claim the database says never became a character

    pool::PruneReport const done = pool::Prune(true);
    CHECK(done.applied);
    CHECK(done.rows == 1);
    CHECK(fakeAccounts.size() == 1);                          // the live account stays
    CHECK(CharacterDatabase.accounts.size() == 1);
    CHECK(CharacterDatabase.characters.size() == 1);
    CHECK(At(CharacterDatabase.characters, 0).name == "Poolbot");
}

void TestPruneKeepsAFailedRowTheDatabaseDisagreesWith()
{
    Reset();
    SeedAccount("TBPOOL0001", 3, kStamp, "ready");
    SeedCharacter("Poolbot", 500, 3, "ready");
    SeedRow("Ghosty", 3, "failed");
    SeedDatabaseCharacter("Ghosty", 501, 3);                  // the character does exist

    pool::PruneReport const done = pool::Prune(true);
    CHECK(done.rows == 0);
    CHECK(CharacterDatabase.characters.size() == 2);          // the row stays: the database holds the character
    CHECK(found_line(done.lines, "holds the name"));
}

// ---------------------------------------------------------------------------------------------------------------------------
// Fix round W0-A r3: the review's findings, as behaviour.
// ---------------------------------------------------------------------------------------------------------------------------

// Finding 1: the account is re-verified by a database read before EVERY character, never taken from the cached list.
void TestNoCharacterGoesOnAnAccountThatChangedAfterTheLoad()
{
    for (int variant = 0; variant < 4; ++variant)
    {
        Reset();
        SeedAccount("TBPOOL0001", 3, kStamp, "ready");
        CHECK(pool::AccountWithRoom() == 3);                   // the cache is loaded and trusts the account
        std::size_t const accountsBefore = fakeAccounts.size();
        std::size_t const rowsBefore = CharacterDatabase.characters.size();

        if (variant == 0)
            fakeAccounts[0].name = "REALPLAYER";               // renamed after the load
        else if (variant == 1)
            fakeAccounts.clear();                              // `.account delete` after the load
        else if (variant == 2)
            fakeAccounts[0] = FakeAccount{ 3, "REALPLAYER", "REAL@EXAMPLE" };     // the id reused by a real player
        else
            fakeAccounts[0].email = kOtherStamp;               // the name re-registered by somebody else: another stamp

        Name("Alarick");
        pool::CreateStart const start = pool::StartCreate(1);
        (void)start;
        RunJob();

        CHECK(fakeBotMgrState.createCalls == 0);               // no character is made on it
        CHECK(CharacterDatabase.characters.size() == rowsBefore);        // and none is even claimed
        CHECK(CharacterDatabase.coreCharacters.empty());
        CHECK(fakeAccounts.size() <= accountsBefore);          // and no replacement account is made either: the job refuses
        CHECK(!pool::Creating());
        CHECK(pool::StopReason().find("no longer the pool's") != std::string::npos);
        CHECK(!pool::AccountRegistered(3));                    // dropped from the cache
    }
}

// Finding 2: the cap counts every row in any state and the characters that are really there.
void TestTheCapCountsFailedAndStaleRowsAndPresentCharacters()
{
    // (a) 5 accounts of 10: 20 ready, 10 failed and 20 stale character rows; every character exists in the database.
    Reset();
    for (uint32 account = 1; account <= 5; ++account)
        SeedAccount("TBPOOL000" + std::to_string(account), account, kStamp, "ready");
    for (uint32 index = 0; index < 50; ++index)
        SeedCharacter("Bot" + std::to_string(index), 1000 + index, 1 + index / 10, index < 20 ? "ready" : index < 30 ? "failed" : "stale");
    CHECK(pool::PoolCharacters() == 50);
    Name("Newone");
    CHECK(!pool::StartCreate(1).started);
    CHECK(fakeBotMgrState.createCalls == 0);

    // (b) characters on a pool account that no registry row names still count (a database count, not the rows).
    Reset();
    SeedAccount("TBPOOL0001", 3, kStamp, "ready");
    SeedCharacter("Poolbot", 500, 3, "ready");
    for (uint32 index = 0; index < 49; ++index)
        SeedDatabaseCharacter("Unlisted" + std::to_string(index), 2000 + index, 3);
    CHECK(pool::PoolCharacters() == 50);
    CHECK(!pool::StartCreate(1).started);

    // (c) rows the migration marked stale still count: 50 stale accounts' worth of characters is a full pool.
    Reset();
    SeedPool(50);
    for (FakePoolDatabase::AccountRow& row : CharacterDatabase.accounts)
        row.state = "stale";
    for (FakePoolDatabase::CharacterRow& row : CharacterDatabase.characters)
        row.state = "stale";
    CHECK(pool::PoolCharacters() == 50);
    CHECK(!pool::StartCreate(1).started);

    // (d) a commit that lands after the 30 s confirmation timeout: the row was marked failed, the character exists.
    Reset();
    fakeConfig.ints["TortoiseBots.Pool.MaxCharacters"] = 1;
    pool::LoadConfig();
    Name("Alarick");
    fakeBotMgrState.commitLands = false;
    CHECK(pool::StartCreate(1).started);
    for (int i = 0; i < 100 && pool::Creating(); ++i)
        pool::CreateUpdate(1000);
    CHECK(At(CharacterDatabase.characters, 0).state == "failed");
    fakeCommitPendingCharacters();                             // the commit lands late
    CHECK(CharacterDatabase.coreCharacters.size() == 1);
    CHECK(pool::PoolCharacters() == 1);
    Name("Bromley");
    CHECK(!pool::StartCreate(1).started);
    CHECK(fakeBotMgrState.createCalls == 1);
}

// Finding 2: a failed auth lookup is unknown, never a reason to mark rows stale or to count them as absent.
void TestAFailedAuthLookupMarksNothingStale()
{
    Reset();
    SeedPool(50);
    fakeAuthDown = true;
    CHECK(pool::Accounts().empty());                           // nothing is usable while the auth database does not answer
    for (FakePoolDatabase::AccountRow const& row : CharacterDatabase.accounts)
        CHECK(row.state == "ready");                           // and nothing was marked stale
    CHECK(pool::PoolCharacters() == 50);                       // the pool is not "empty": the cap holds
    CHECK(!pool::StartCreate(1).started);
    CHECK(fakeBotMgrState.createCalls == 0);

    fakeAuthDown = false;
    CHECK(pool::Accounts().size() == 5);                       // the next read trusts them again
    for (FakePoolDatabase::AccountRow const& row : CharacterDatabase.accounts)
        CHECK(row.state == "ready");

    // The reconciliation's claims: an unanswered account lookup leaves a claim whose character exists as it is (never stale), and
    // the next start finishes it.
    Reset();
    SeedAccount("TBPOOL0001", 3, kStamp, "creating");
    SeedRow("Poolbot", 3, "creating");
    SeedDatabaseCharacter("Poolbot", 500, 3);
    fakeAuthDown = true;
    pool::Reconcile();
    CHECK(At(CharacterDatabase.characters, 0).state == "creating");
    CHECK(At(CharacterDatabase.accounts, 0).state == "creating");
    fakeAuthDown = false;
    pool::Reconcile();
    CHECK(At(CharacterDatabase.characters, 0).state == "ready");
    CHECK(At(CharacterDatabase.accounts, 0).state == "ready");
}

// Finding 3: the name is checked before an account is made.
void TestTheNameIsCheckedBeforeAnAccountIsMade()
{
    // (a) a real character already has the name.
    Reset();
    SeedDatabaseCharacter("Taken", 900, 77);
    TortoiseBots::fakeBotNames = { "Taken", "Taken", "Taken", "Taken", "Taken", "Taken" };
    TortoiseBots::fakeBotNameIndex = 0;
    CHECK(pool::StartCreate(1).started);
    RunJob();
    CHECK(fakeAccounts.empty());
    CHECK(CharacterDatabase.accounts.empty());
    CHECK(CharacterDatabase.characters.empty());
    CHECK(fakeBotMgrState.createCalls == 0);
    CHECK(pool::StopReason().find("already exists") != std::string::npos);

    // (b) the core's own name rules refuse it.
    Reset();
    fakeRefusedNames.insert("Badname");
    Name("Badname");
    CHECK(pool::StartCreate(1).started);
    RunJob();
    CHECK(fakeAccounts.empty());
    CHECK(CharacterDatabase.accounts.empty());
    CHECK(fakeBotMgrState.createCalls == 0);

    // (c) a core that refuses the creation: every retried `.tb pool create 1` reuses the one empty account.
    Reset();
    fakeBotMgrState.failCreate = true;
    for (int attempt = 0; attempt < 4; ++attempt)
    {
        Name("Alarick" + std::to_string(attempt));
        pool::StartCreate(1);
        RunJob();
    }
    CHECK(fakeBotMgrState.createCalls == 4);
    CHECK(fakeAccounts.size() == 1);
    CHECK(CharacterDatabase.accounts.size() == 1);
}

// A composition the core refuses (a race the realm's client data cannot create) is skipped, not fatal: the race is retired
// for the rest of the job and the request finishes another way. Only a core that refuses everything stops the job, and then
// after more than one attempt.
void TestARefusedCompositionIsSkippedNotFatal()
{
    // (a) one refused creation: the job retires that composition's race, tries another, and finishes the request.
    Reset();
    ManyNames(300);
    fakeFailNextCreates = 1;
    CHECK(pool::StartCreate(1).started);
    RunJob();
    CHECK(!pool::Creating());
    CHECK(pool::StopReason() == "the request is complete");
    CHECK(fakeBotMgrState.createCalls == 2);
    CHECK(CharacterDatabase.characters.size() == 1);
    CHECK(At(CharacterDatabase.characters, 0).state == "ready");

    // (b) a core that refuses every creation: the job keeps trying (the first refusal is not fatal) and then stops.
    Reset();
    ManyNames(300);
    fakeBotMgrState.failCreate = true;
    CHECK(pool::StartCreate(1).started);
    RunJob();
    CHECK(!pool::Creating());
    CHECK(pool::StopReason().find("refused") != std::string::npos);
    CHECK(fakeBotMgrState.createCalls >= 5);
    CHECK(CharacterDatabase.characters.empty());
    CHECK(CharacterDatabase.accounts.size() == 1);            // the one account is reused, never multiplied
}

// One character per account, so the account rows follow the characters: a pool of three is three accounts and the
// character cap is also the account cap; an empty pool account is reused first.
void TestThePoolAccountsFollowTheCharacters()
{
    Reset();
    fakeConfig.ints["TortoiseBots.Pool.MaxCharacters"] = 3;
    pool::LoadConfig();
    TortoiseBots::fakeBotNames = { "Alarick", "Bromley", "Cedric" };
    TortoiseBots::fakeBotNameIndex = 0;
    CHECK(pool::StartCreate(3).started);
    RunJob();
    CHECK(fakeAccounts.size() == 3);                           // one account per character
    CHECK(CharacterDatabase.accounts.size() == 3);
    CHECK(CharacterDatabase.characters.size() == 3);
    CHECK(At(CharacterDatabase.characters, 0).accountId != At(CharacterDatabase.characters, 1).accountId);
    CHECK(At(CharacterDatabase.characters, 0).accountId != At(CharacterDatabase.characters, 2).accountId);
    CHECK(At(CharacterDatabase.characters, 1).accountId != At(CharacterDatabase.characters, 2).accountId);
    Name("Darianne");
    CHECK(!pool::StartCreate(1).started);                      // the character cap is the account cap
    CHECK(fakeBotMgrState.createCalls == 3);

    // An empty failed pool account (nothing on it) is reused before any account is made.
    Reset();
    SeedAccount("TBPOOL0001", 3, kStamp, "failed");
    Name("Alarick");
    CHECK(pool::StartCreate(1).started);
    RunJob();
    CHECK(fakeAccounts.size() == 1);
    CHECK(At(CharacterDatabase.characters, 0).accountId == 3);
    CHECK(At(CharacterDatabase.accounts, 0).state == "ready");
}

// Finding 6: a job runs on the config it started with, stops cleanly when the pool is turned off, and a cache reload does not
// open a second account.
void TestAJobSnapshotsItsConfigAndStopsWhenTheKeyIsTurnedOff()
{
    Reset();
    TortoiseBots::fakeBotNames = { "Alarick", "Bromley", "Cedric" };
    TortoiseBots::fakeBotNameIndex = 0;
    CHECK(pool::StartCreate(3).started);
    pool::CreateUpdate(1000000);                               // the first character is queued
    CHECK(fakeBotMgrState.createCalls == 1);
    fakeConfig.bools["TortoiseBots.Pool.Enable"] = false;
    pool::LoadConfig();                                        // `.reload config` turns the pool off
    pool::CreateUpdate(1000000);
    CHECK(!pool::Creating());
    CHECK(pool::StopReason().find("turned off") != std::string::npos);
    pool::CreateUpdate(1000000);
    pool::CreateUpdate(1000000);
    CHECK(fakeBotMgrState.createCalls == 1);                   // nothing more is made

    // The cap and rate of the start hold for the whole job.
    Reset();
    TortoiseBots::fakeBotNames = { "Alarick", "Bromley" };
    TortoiseBots::fakeBotNameIndex = 0;
    CHECK(pool::StartCreate(2).started);
    fakeConfig.ints["TortoiseBots.Pool.MaxCharacters"] = 1;
    pool::LoadConfig();
    RunJob();
    CHECK(CharacterDatabase.characters.size() == 2);
    CHECK(fakeBotMgrState.createCalls == 2);
}

void TestACacheReloadDuringAJobKeepsOneAccountPerCharacter()
{
    Reset();
    TortoiseBots::fakeBotNames = { "Alarick", "Bromley", "Cedric" };
    TortoiseBots::fakeBotNameIndex = 0;
    CHECK(pool::StartCreate(3).started);
    pool::CreateUpdate(1000000);                               // the first account is made and its character queued
    pool::LoadConfig();                                        // a reload clears the registry's cache mid-job
    RunJob();
    CHECK(fakeAccounts.size() == 3);
    CHECK(CharacterDatabase.characters.size() == 3);
    CHECK(fakeBotMgrState.createCalls == 3);
    CHECK(At(CharacterDatabase.characters, 0).accountId != At(CharacterDatabase.characters, 1).accountId);
    CHECK(At(CharacterDatabase.characters, 0).accountId != At(CharacterDatabase.characters, 2).accountId);
    CHECK(At(CharacterDatabase.characters, 1).accountId != At(CharacterDatabase.characters, 2).accountId);
}

// The test fake's EscapeString really escapes, so text that reaches SQL is tested as text.
void TestSqlTextIsEscaped()
{
    Reset();
    SeedDatabaseCharacter("O'Brien", 600, 3);
    SeedDatabaseCharacter("Back\\slash", 601, 4);
    bool found = false;
    uint64 guid = 0;
    uint32 account = 0;
    CHECK(pool::CharacterOwner("O'Brien", found, guid, account) && found && guid == 600 && account == 3);
    CHECK(pool::CharacterOwner("Back\\slash", found, guid, account) && found && guid == 601 && account == 4);
    // An injection attempt is a name nobody has, and the database answers: not found, not an error.
    CHECK(pool::CharacterOwner("x' OR '1'='1", found, guid, account) && !found);
    CHECK(pool::CharacterOwner("x\\' OR \\'1\\'=\\'1", found, guid, account) && !found);

    // The registry's own writes escape the same way and read back the same row.
    pool::Character row;
    row.name = "D'Arc";
    row.accountId = 3;
    row.race = 1;
    row.klass = 1;
    row.state = TortoiseBots::world::PoolRowState::Creating;
    CHECK(pool::StoreCharacter(row));
    pool::Character back;
    CHECK(pool::FindCharacterRow("D'Arc", back) && back.name == "D'Arc");
    CHECK(pool::DeleteCharacterRow("D'Arc"));
    CHECK(CharacterDatabase.characters.empty());
}

// ===== Last fix round (codex-w0a-r4, claude-w0a-r2) =====

// Codex high: an existing character on a trusted pool account is adopted only when this pool's own `creating` row for that exact
// name and account claimed it first. Anything else is logged, skipped, and never gets a row of its own.
void TestAnExistingCharacterIsNeverAdoptedWithoutItsOwnClaim()
{
    Reset();
    SeedAccount("TBPOOL0001", 3, kStamp, "ready");
    SeedDatabaseCharacter("Stranger", 700, 3);               // an administrator's character on a pool account: no claim row
    TortoiseBots::fakeBotNames = { "Stranger", "Stranger", "Stranger", "Stranger", "Stranger", "Stranger" };
    TortoiseBots::fakeBotNameIndex = 0;
    CHECK(pool::StartCreate(1).started);
    RunJob();
    CHECK(CharacterDatabase.characters.empty());              // no claim was manufactured for it, no row was made ready
    CHECK(fakeBotMgrState.createCalls == 0);
    CHECK(fakeCharacterCache.empty());
    CHECK(CharacterDatabase.coreCharacters.size() == 1);      // the character itself is untouched
    CHECK(pool::Characters().empty());
    CHECK(pool::StopReason().find("already exists") != std::string::npos);

    // A claim of the same name on ANOTHER account does not license it either.
    Reset();
    SeedAccount("TBPOOL0001", 3, kStamp, "ready");
    SeedAccount("TBPOOL0002", 4, kStamp, "ready");
    SeedRow("Stranger", 4, "creating");
    SeedDatabaseCharacter("Stranger", 701, 3);
    TortoiseBots::fakeBotNames = { "Stranger", "Stranger", "Stranger", "Stranger", "Stranger", "Stranger" };
    TortoiseBots::fakeBotNameIndex = 0;
    CHECK(pool::StartCreate(1).started);
    RunJob();
    for (FakePoolDatabase::CharacterRow const& row : CharacterDatabase.characters)
        CHECK(row.state != "ready" && !row.hasGuid);
    CHECK(pool::Characters().empty());
}

// Review finding 3: prune finishes unfinished creations first and never touches an account that still has a `creating` claim.
void TestPruneReconcilesFirstAndKeepsAnAccountWithAClaim()
{
    // (a) the claim's character has landed: Reconcile makes it a ready row before prune looks, so the account stays.
    Reset();
    SeedAccount("TBPOOL0001", 3, kStamp, "failed");
    SeedRow("Late", 3, "creating");
    SeedDatabaseCharacter("Late", 800, 3);
    pool::PruneReport const first = pool::Prune(true);
    (void)first;
    CHECK(fakeAccounts.size() == 1);
    CHECK(CharacterDatabase.coreCharacters.size() == 1);
    CHECK(fakePoolEvents.end() == std::find(fakePoolEvents.begin(), fakePoolEvents.end(), "DeleteAccount 3"));

    // (b) the claim cannot be finished (its `ready` write does not land): the row stays `creating` and its account is kept.
    Reset();
    SeedAccount("TBPOOL0001", 3, kStamp, "failed");
    SeedRow("Later", 3, "creating");
    SeedDatabaseCharacter("Later", 801, 3);
    CharacterDatabase.failCharacterUpdate = true;
    pool::PruneReport const second = pool::Prune(true);
    (void)second;
    CHECK(fakeAccounts.size() == 1);
    CHECK(CharacterDatabase.coreCharacters.size() == 1);
    CHECK(At(CharacterDatabase.characters, 0).state == "creating");
}

// Review finding 4: stale rows do not count toward the account cap; a failed row read is unknown, never an empty table.
void TestStaleAccountRowsDoNotBlockANewAccountAndAFailedReadIsUnknown()
{
    Reset();
    fakeConfig.ints["TortoiseBots.Pool.MaxCharacters"] = 10;
    pool::LoadConfig();
    fakeAccounts.push_back(FakeAccount{ 3, "TBPOOL0001", "REAL@EXAMPLE" });
    CharacterDatabase.accounts.push_back(AccountRow("TBPOOL0001", true, 3, kStamp, "stale"));
    CHECK(pool::PoolAccountRows() == 0);                       // a stale row cannot hold a character
    Name("Alarick");
    CHECK(pool::StartCreate(1).started);
    RunJob();
    CHECK(fakeBotMgrState.createCalls == 1);                   // a new account was made after the migration
    CHECK(fakeAccounts.size() == 2);

    // A read of the registry tables that does not answer is unknown, never an empty table (a full pool, and an empty one).
    Reset();
    SeedPool(50);
    CharacterDatabase.failPoolReads = true;
    CHECK(pool::PoolCharacters() == pool::kPoolCountUnknown);
    CHECK(!pool::StartCreate(1).started);
    CHECK(fakeBotMgrState.createCalls == 0);
    CharacterDatabase.failPoolReads = false;
    CHECK(pool::PoolCharacters() == 50);
    Reset();
    CHECK(pool::PoolCharacters() == 0);                        // an answered empty table is zero
    CharacterDatabase.failPoolReads = true;
    CHECK(pool::PoolCharacters() == pool::kPoolCountUnknown);
}

// Review finding 5: a lookup that fails halfway (the e-mail read) is unknown and retried, never a permanent stale mark.
void TestAHalfFailedAccountLookupIsNeverAStaleMark()
{
    Reset();
    SeedPool(50);
    fakeEmailDown = true;
    CHECK(pool::Accounts().empty());
    for (FakePoolDatabase::AccountRow const& row : CharacterDatabase.accounts)
        CHECK(row.state == "ready");
    std::string why;
    CHECK(pool::VerifyAccount(At(fakeAccounts, 0).id, At(fakeAccounts, 0).name, why) != TortoiseBots::world::PoolAccountCheck::Trusted);
    for (FakePoolDatabase::AccountRow const& row : CharacterDatabase.accounts)
        CHECK(row.state == "ready");
    fakeEmailDown = false;
    CHECK(pool::Accounts().size() == 5);                       // the next read trusts them again
    CHECK(pool::VerifyAccount(At(fakeAccounts, 0).id, At(fakeAccounts, 0).name, why) == TortoiseBots::world::PoolAccountCheck::Trusted);
}

// Review finding 7: a row becomes ready only after the login commit too.
void TestARowIsNotReadyUntilTheLoginCommitRan()
{
    Reset();
    Name("Alarick");
    fakeBotMgrState.loginCommitHeld = true;                    // the character row landed; the login side has not run
    CHECK(pool::StartCreate(1).started);
    for (int i = 0; i < 20; ++i)
        pool::CreateUpdate(1000000);
    CHECK(CharacterDatabase.coreCharacters.size() == 1);
    CHECK(CharacterDatabase.characters.size() == 1);
    CHECK(At(CharacterDatabase.characters, 0).state == "creating");
    CHECK(fakeCharacterCache.empty());
    CHECK(pool::Creating());

    fakeLoginHeld.clear();                                     // the login commit and OnPlayerCreate ran
    RunJob();
    CHECK(At(CharacterDatabase.characters, 0).state == "ready");
    CHECK(fakeCharacterCache.size() == 1);
    CHECK(!pool::Creating());
}
} // namespace

int main()
{
    TestIdReuseIsStale();
    TestRenamedRealAccountIsStale();
    TestReusedNameIsStale();
    TestTrustedRowIsUsed();
    TestCreationWritesTheClaimFirst();
    TestCrashAfterAccountCreationKeepsTheAccount();
    TestCrashAfterCharacterCreationIsFinished();
    TestTheTotalCapRefuses();
    TestTheTotalCapClamps();
    TestAForeignAccountUnderAClaimIsNeverDeleted();
    TestAClaimWithNoIdIsResolvedByItsStamp();
    TestAClaimWithNoAccountIsMarkedFailed();
    TestAClaimWithAStrangersStampIsNotAdopted();
    TestStaleRowsAreNotCounted();
    TestACacheMissDoesNotDeleteTheAccount();
    TestAnUnansweredDatabaseKeepsTheClaim();
    TestAQueuedCommitIsNotReady();
    TestACommitThatNeverLandsIsMarkedFailed();
    TestPruneRefusesANonPoolAccount();
    TestPruneKeepsAnAccountWithAForeignCharacter();
    TestPruneRemovesOnlyAProvenPoolAccount();
    TestPruneDropsAFailedRowOfALiveAccount();
    TestPruneKeepsAFailedRowTheDatabaseDisagreesWith();
    TestNoCharacterGoesOnAnAccountThatChangedAfterTheLoad();
    TestTheCapCountsFailedAndStaleRowsAndPresentCharacters();
    TestAFailedAuthLookupMarksNothingStale();
    TestTheNameIsCheckedBeforeAnAccountIsMade();
    TestARefusedCompositionIsSkippedNotFatal();
    TestThePoolAccountsFollowTheCharacters();
    TestAJobSnapshotsItsConfigAndStopsWhenTheKeyIsTurnedOff();
    TestACacheReloadDuringAJobKeepsOneAccountPerCharacter();
    TestSqlTextIsEscaped();
    TestAnExistingCharacterIsNeverAdoptedWithoutItsOwnClaim();
    TestPruneReconcilesFirstAndKeepsAnAccountWithAClaim();
    TestStaleAccountRowsDoNotBlockANewAccountAndAFailedReadIsUnknown();
    TestAHalfFailedAccountLookupIsNeverAStaleMark();
    TestARowIsNotReadyUntilTheLoginCommitRan();

    if (g_fail)
        std::printf("pool_registry_check: %d checks, %d FAILED\n", g_checks, g_fail);
    else
        std::printf("RESULT: PASS (%d checks)\n", g_checks);

    return g_fail ? 1 : 0;
}
