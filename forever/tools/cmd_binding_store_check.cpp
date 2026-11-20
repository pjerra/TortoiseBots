// Standalone test of the characters-database half of the commander binding store (forever/src/host/cmd/TbCmdBindingStore.cpp; package P2,
// forever/docs/CMD-P2-NOTES.md). It compiles the store's OWN translation unit against the fakes in cmd_binding_store_fakes/ (the
// information_schema check, the five-column SELECT, the CREATE/INSERT/DELETE, `sConfigMgr` and the log), so the read-back of Save and the load in
// Rows run exactly as they do on the core. The fakes' result set has ForeverCore's row contract: Query has already fetched the first row
// (AdhocStatement.cpp, BasicStatementTask::Query calls NextRow) and answers NULL for no rows, so a reader that advances before its first Fetch
// silently loses a row.
//   g++ -std=c++17 -O1 -Wall -Wextra -I forever/tools/cmd_binding_store_fakes -I forever/src -I .
//       forever/tools/cmd_binding_store_check.cpp forever/src/host/cmd/TbCmdBindingStore.cpp -o /tmp/cmd_binding_store_check
#include "DatabaseEnv.h"
#include "Config.h"
#include "Log.h"
#include "StringFormat.h"

#include "host/cmd/TbCmdBindingStore.h"

#include <cstdio>
#include <string>
#include <vector>

namespace S = TortoiseBots::cmd;
namespace L = TortoiseBots::cmd::life;

static int g_checks = 0, g_fail = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

namespace
{
// guid counters of the fixture (the store holds the `characters.guid` counters)
constexpr uint64_t Tank = 10, Heal = 11, Dps = 12, Cmd = 20, Cmd2 = 21;

FakeDatabase::Row DbRow(uint64_t commander, uint64_t lead, std::string const& members, uint32_t flags = 0, int64_t boundAt = 1700000000)
{
    FakeDatabase::Row row;
    row.commander = commander;
    row.lead = lead;
    row.members = members;
    row.flags = flags;
    row.boundAt = boundAt;
    return row;
}

L::Stored Stored(uint64_t commander, uint64_t lead, std::vector<uint64_t> members, uint32_t flags = 0, int64_t boundAt = 1700000000)
{
    L::Stored stored;
    stored.commander = commander;
    stored.lead = lead;
    stored.members = std::move(members);
    stored.flags = flags;
    stored.boundAt = boundAt;
    return stored;
}

L::Stored const Party = Stored(Cmd, Tank, { Tank, Heal, Dps }, L::FlagInParty | L::FlagPlayerLed);

bool Same(L::Stored const& left, L::Stored const& right)
{
    return left.commander == right.commander && left.lead == right.lead && left.members == right.members && left.flags == right.flags &&
        left.boundAt == right.boundAt;
}

// A fresh store: the fake table empty, the key on (or off), the full schema (or `columns` columns), every statement counter at zero. LoadConfig
// after the first call is the store's own reload, so the cached rows, load state and schema flags all start over.
void Reset(bool keepBindings = true, int columns = 5)
{
    CharacterDatabase.rows.clear();
    CharacterDatabase.columns = columns;
    CharacterDatabase.failInserts = false;
    CharacterDatabase.failDeletes = false;
    CharacterDatabase.selects = 0;
    CharacterDatabase.inserts = 0;
    CharacterDatabase.deletes = 0;
    CharacterDatabase.creates = 0;
    sConfigMgr->bools.clear();
    sConfigMgr->ints.clear();
    if (keepBindings)
        sConfigMgr->bools["TortoiseBots.Commander.KeepBindings"] = true;
    S::store::LoadConfig();
    // A pending delete left by a test is retried against this fresh empty table: it never leaks into the next fixture.
    S::store::RetryErases();
}

// The core's QueryResult contract (see the header). A one-row table is the case a reader that advances before its first Fetch cannot see at all.
void TestOneRow()
{
    Reset();
    CharacterDatabase.rows.push_back(DbRow(Cmd, Tank, "10,11,12", L::FlagInParty | L::FlagPlayerLed, 1700000000));
    std::vector<L::Stored> const rows = S::store::Rows();
    CHECK(rows.size() == 1);
    if (rows.size() == 1)
        CHECK(Same(rows[0], Party));
}

void TestEveryRowInResultOrder()
{
    Reset();
    L::Stored const first = Stored(Cmd, Tank, { Tank, Heal });
    L::Stored const second = Stored(Cmd2, Dps, { Dps }, L::FlagRaid, 1700000100);
    L::Stored const third = Stored(30, Heal, { Heal, Dps }, L::FlagSystem, 1700000200);
    CharacterDatabase.rows.push_back(DbRow(first.commander, first.lead, L::EncodeMembers(first.members), first.flags, first.boundAt));
    CharacterDatabase.rows.push_back(DbRow(second.commander, second.lead, L::EncodeMembers(second.members), second.flags, second.boundAt));
    CharacterDatabase.rows.push_back(DbRow(third.commander, third.lead, L::EncodeMembers(third.members), third.flags, third.boundAt));
    std::vector<L::Stored> const rows = S::store::Rows();
    CHECK(rows.size() == 3);
    if (rows.size() == 3)
    {
        CHECK(Same(rows[0], first));
        CHECK(Same(rows[1], second));
        CHECK(Same(rows[2], third));
    }
}

void TestAnEmptyTableIsNoRow()
{
    Reset();
    CHECK(S::store::Rows().empty());
    CHECK(CharacterDatabase.selects > 0);      // the guarded read ran, and the core's Query answered null
}

void TestARefusedRowDoesNotHideTheOthers()
{
    Reset();
    // The middle row is the only valid one: an invalid first or last row must not change what is returned.
    CharacterDatabase.rows.push_back(DbRow(Cmd, Dps, "10,11"));             // the lead is not a member
    CharacterDatabase.rows.push_back(DbRow(Cmd2, Tank, "10,11,12"));
    CharacterDatabase.rows.push_back(DbRow(30, Heal, "10"));                // the lead is not a member either
    std::vector<L::Stored> const rows = S::store::Rows();
    CHECK(rows.size() == 1);
    if (rows.size() == 1)
        CHECK(rows[0].commander == Cmd2);
}

// Save's own read-back: the in-memory view changes only after the row is really in the table, so a successful Save proves the single-row read
// sees the row it just wrote.
void TestSaveReadsItBack()
{
    Reset();
    CHECK(S::store::Save(Party));
    CHECK(CharacterDatabase.inserts == 1);
    CHECK(CharacterDatabase.rows.size() == 1);
    if (CharacterDatabase.rows.size() == 1)
    {
        CHECK(CharacterDatabase.rows[0].commander == Cmd);
        CHECK(CharacterDatabase.rows[0].lead == Tank);
        CHECK(CharacterDatabase.rows[0].members == "10,11,12");
        CHECK(CharacterDatabase.rows[0].flags == Party.flags);
        CHECK(CharacterDatabase.rows[0].boundAt == Party.boundAt);
    }
    // Replacing the same commander keeps one row and the new values (the upsert), and the read-back follows.
    L::Stored const moved = Stored(Cmd, Dps, { Dps, Tank }, L::FlagRaid, 1700000300);
    CHECK(S::store::Save(moved));
    CHECK(CharacterDatabase.rows.size() == 1);
    if (CharacterDatabase.rows.size() == 1)
        CHECK(CharacterDatabase.rows[0].lead == Dps);
    std::vector<L::Stored> const rows = S::store::Rows();
    CHECK(rows.size() == 1);
    if (rows.size() == 1)
        CHECK(Same(rows[0], moved));
}

void TestASaveThatDoesNotLandIsRefused()
{
    Reset();
    CharacterDatabase.failInserts = true;
    CHECK(!S::store::Save(Party));
    CHECK(CharacterDatabase.rows.empty());
    // ... and the in-memory view did not take it (Rows() reloads nothing: the load is done for this config).
    CHECK(S::store::Rows().empty());
}

void TestEraseReadsTheRowBack()
{
    Reset(true);
    L::Stored const other = Stored(Cmd2, Heal, { Heal }, L::FlagRaid);
    CharacterDatabase.rows.push_back(DbRow(Party.commander, Party.lead, L::EncodeMembers(Party.members), Party.flags, Party.boundAt));
    CharacterDatabase.rows.push_back(DbRow(other.commander, other.lead, L::EncodeMembers(other.members), other.flags, other.boundAt));
    CHECK(S::store::Rows().size() == 2);
    CHECK(S::store::Erase(Cmd));
    CHECK(CharacterDatabase.deletes == 1);
    CHECK(CharacterDatabase.rows.size() == 1);
    std::vector<L::Stored> const rows = S::store::Rows();
    CHECK(rows.size() == 1);
    if (rows.size() == 1)
        CHECK(Same(rows[0], other));
}

// A DELETE that did not land must be seen. With one row left the read-back has to return that row: a reader that skips the first row reports the
// delete as landed when the binding is still stored for the next start.
void TestAnEraseThatDoesNotLandIsRefused()
{
    Reset();
    CharacterDatabase.rows.push_back(DbRow(Party.commander, Party.lead, L::EncodeMembers(Party.members), Party.flags, Party.boundAt));
    CHECK(S::store::Rows().size() == 1);
    CharacterDatabase.failDeletes = true;
    CHECK(!S::store::Erase(Cmd));
    CHECK(CharacterDatabase.rows.size() == 1);
}

// A DELETE that did not land must leave a pending mark: the row is still in the table, but it must never be offered to a restore again, and the next
// maintenance pass retries until it is gone. A release that only logged and forgot the row would bring the binding back after the next start.
void TestAFailedEraseIsPendingNotGone()
{
    Reset();
    CHECK(S::store::Save(Party));
    CharacterDatabase.failDeletes = true;
    CHECK(!S::store::Erase(Cmd));
    CHECK(CharacterDatabase.rows.size() == 1);        // the delete really did not land: the durable row is the danger
    CHECK(S::store::PendingErase(Cmd));
    CHECK(S::store::Rows().empty());                  // a pending delete is never offered to the restore
    // A config reload re-reads the table from the database; it must not turn the row back into a restorable binding.
    S::store::LoadConfig();
    CHECK(S::store::PendingErase(Cmd));
    CHECK(S::store::Rows().empty());
    // The upkeep pass retries; once the database takes the statement the row and its mark are gone.
    CharacterDatabase.failDeletes = false;
    CHECK(S::store::RetryErases());
    CHECK(!S::store::PendingErase(Cmd));
    CHECK(CharacterDatabase.rows.empty());
    CHECK(S::store::Rows().empty());
    // A bind written after a failed release wins over the pending delete: the retry must never delete the new row.
    CharacterDatabase.failInserts = false;
    CHECK(S::store::Save(Party));
    CharacterDatabase.failDeletes = true;
    CHECK(!S::store::Erase(Cmd));
    CHECK(S::store::PendingErase(Cmd));
    CharacterDatabase.failDeletes = false;
    L::Stored const moved = Stored(Cmd, Dps, { Dps, Tank }, L::FlagRaid, 1700000300);
    CHECK(S::store::Save(moved));
    CHECK(!S::store::PendingErase(Cmd));
    CHECK(S::store::RetryErases());
    CHECK(CharacterDatabase.rows.size() == 1);
    if (CharacterDatabase.rows.size() == 1)
        CHECK(CharacterDatabase.rows[0].lead == Dps);
}

void TestTheKeyOffDoesNothing()
{
    Reset(false);
    CharacterDatabase.rows.push_back(DbRow(Party.commander, Party.lead, L::EncodeMembers(Party.members), Party.flags, Party.boundAt));
    CHECK(S::store::Rows().empty());
    CHECK(!S::store::Save(Party));
    CHECK(!S::store::Erase(Cmd));
    CHECK(CharacterDatabase.selects == 0);     // no statement at all, not even the schema check
    CHECK(CharacterDatabase.inserts == 0);
    CHECK(CharacterDatabase.deletes == 0);
    CHECK(CharacterDatabase.creates == 0);
}

void TestAMissingColumnTurnsPersistenceOff()
{
    Reset(true, 3);
    CharacterDatabase.rows.push_back(DbRow(Party.commander, Party.lead, L::EncodeMembers(Party.members), Party.flags, Party.boundAt));
    CHECK(S::store::Rows().empty());
    CHECK(!S::store::Save(Party));
    CHECK(CharacterDatabase.creates == 1);     // the module's own CREATE ran once ...
    CHECK(CharacterDatabase.inserts == 0);     // ... and no statement naming the lacking column was sent
    CHECK(CharacterDatabase.deletes == 0);
}
}

int main()
{
    TestOneRow();
    TestEveryRowInResultOrder();
    TestAnEmptyTableIsNoRow();
    TestARefusedRowDoesNotHideTheOthers();
    TestSaveReadsItBack();
    TestASaveThatDoesNotLandIsRefused();
    TestEraseReadsTheRowBack();
    TestAnEraseThatDoesNotLandIsRefused();
    TestAFailedEraseIsPendingNotGone();
    TestTheKeyOffDoesNothing();
    TestAMissingColumnTurnsPersistenceOff();
    std::printf("cmd_binding_store_check: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
