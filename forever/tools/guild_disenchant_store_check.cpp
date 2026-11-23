// Standalone check of the disenchant service's guarded characters store
// (forever/src/host/TbHostGuildOrderDisenchantStore.cpp, TBF #155). It
// compiles the store's OWN translation unit against
// forever/tools/guild_disenchant_store_fakes (the information_schema probe,
// the single-table SELECT, REPLACE/DELETE, and ForeverCore's "Query stands
// on the first row" contract), so the load, the write read-back and the
// delete confirmation run exactly as on the core.
//   g++ -std=c++17 -O1 -Wall -Wextra -I forever/tools/guild_disenchant_store_fakes -I forever/src -I .
//       forever/tools/guild_disenchant_store_check.cpp
//       forever/src/host/TbHostGuildOrderDisenchantStore.cpp -o /tmp/guild_disenchant_store_check
#include "DatabaseEnv.h"

#include "host/TbHostGuildOrderDisenchant.h"

#include <cstdio>
#include <string>
#include <vector>

namespace G = tbhost::guild_orders;
using Entry = TortoiseBots::GuildOrders::DisenchantQueueEntry;

static int g_checks = 0, g_fail = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

namespace
{
Entry Make(uint32_t bot, uint32_t sender, uint64_t guid, uint32_t itemId, std::string name = "Copper Axe")
{
    Entry entry;
    entry.botGuidLow = bot;
    entry.senderGuidLow = sender;
    entry.itemGuidLow = guid;
    entry.itemId = itemId;
    entry.itemName = std::move(name);
    return entry;
}

void Reset()
{
    CharacterDatabase.rows.clear();
    CharacterDatabase.columns = 8;
    CharacterDatabase.failWrites = false;
    CharacterDatabase.selects = 0;
    CharacterDatabase.writes = 0;
    G::LoadDisenchantQueue();
}

bool Has(uint32_t bot, uint64_t guid)
{
    for (Entry const& entry : G::DisenchantQueued(bot))
        if (entry.itemGuidLow == guid)
            return true;
    return false;
}
}

int main()
{
    // 1. Failing case first: a missing table must disable the service, not
    //    throw and not attempt a write.
    Reset();
    CharacterDatabase.columns = 7;   // one of the eight required columns is gone
    G::LoadDisenchantQueue();
    CHECK(!G::IsDisenchantStoreAvailable());
    CHECK(!G::NoteDisenchantQueued(Make(10, 20, 30, 40)));
    CHECK(CharacterDatabase.writes == 0);

    // 2. A single stored row survives the load: the first row the core's
    //    Query already fetched must be read, not skipped.
    Reset();
    CharacterDatabase.rows.push_back({ 10, 20, 5000000000ULL, 5191, "Copper Axe", 0, "" });
    G::LoadDisenchantQueue();
    CHECK(G::IsDisenchantStoreAvailable());
    {
        std::vector<Entry> const rows = G::DisenchantQueued(10);
        CHECK(rows.size() == 1);
        CHECK(rows.size() == 1 && rows[0].itemGuidLow == 5000000000ULL);
        CHECK(rows.size() == 1 && rows[0].itemId == 5191);
        CHECK(rows.size() == 1 && rows[0].senderGuidLow == 20);
        CHECK(rows.size() == 1 && rows[0].itemName == "Copper Axe");
        CHECK(rows.size() == 1 && !rows[0].pendingResults);
    }

    // 3. A write lands, is read back and shows up in memory and in the table.
    {
        Entry const entry = Make(11, 21, 5000000001ULL, 2772);
        CHECK(G::NoteDisenchantQueued(entry));
        CHECK(CharacterDatabase.rows.size() == 2);
        CHECK(G::DisenchantQueued(11).size() == 1);
        CHECK(Has(11, 5000000001ULL));
        bool stored = false;
        for (FakeDatabase::Row const& row : CharacterDatabase.rows)
            stored = stored || (row.bot == 11 && row.itemGuid == 5000000001ULL && row.itemName == "Copper Axe");
        CHECK(stored);
    }

    // 4. Results rewrite the same key; pending flips and the format survives.
    {
        Entry entry = Make(11, 21, 5000000001ULL, 2772);
        entry.pendingResults = true;
        entry.results = "10940:2;10938:1";
        CHECK(G::SaveDisenchantRow(entry));
        CHECK(CharacterDatabase.rows.size() == 2);
        std::vector<Entry> const rows = G::DisenchantQueued(11);
        CHECK(rows.size() == 1);
        CHECK(rows.size() == 1 && rows[0].pendingResults);
        CHECK(rows.size() == 1 && rows[0].results == "10940:2;10938:1");
    }

    // 5. A write the table refuses changes nothing in memory and answers false.
    {
        size_t const before = CharacterDatabase.rows.size();
        CharacterDatabase.failWrites = true;
        Entry const entry = Make(12, 22, 5000000002ULL, 1205);
        CHECK(!G::SaveDisenchantRow(entry));
        CHECK(!Has(12, 5000000002ULL));
        CHECK(CharacterDatabase.rows.size() == before);
        CharacterDatabase.failWrites = false;
    }

    // 6. Delete removes the row from the table and from memory...
    CHECK(G::DeleteDisenchantRow(11, 5000000001ULL));
    CHECK(!Has(11, 5000000001ULL));
    for (FakeDatabase::Row const& row : CharacterDatabase.rows)
        CHECK(!(row.bot == 11 && row.itemGuid == 5000000001ULL));

    // ... and a refused delete keeps it (the row is still queued).
    {
        Entry const entry = Make(13, 23, 5000000003ULL, 1205);
        CHECK(G::SaveDisenchantRow(entry));
        CharacterDatabase.failWrites = true;
        CHECK(!G::DeleteDisenchantRow(13, 5000000003ULL));
        CHECK(Has(13, 5000000003ULL));
        CharacterDatabase.failWrites = false;
        CHECK(G::DeleteDisenchantRow(13, 5000000003ULL));
    }

    // 7. Two bots never see each other's rows.
    {
        CHECK(G::SaveDisenchantRow(Make(14, 24, 5000000004ULL, 2589)));
        CHECK(G::SaveDisenchantRow(Make(15, 25, 5000000005ULL, 2592)));
        CHECK(G::DisenchantQueued(14).size() == 1);
        CHECK(G::DisenchantQueued(15).size() == 1);
        CHECK(Has(14, 5000000004ULL) && !Has(14, 5000000005ULL));
        CHECK(Has(15, 5000000005ULL) && !Has(15, 5000000004ULL));
    }

    // 8. A restart (a fresh load from the table) re-queues what was saved.
    G::LoadDisenchantQueue();
    CHECK(G::DisenchantQueued(14).size() == 1);
    CHECK(G::DisenchantQueued(15).size() == 1);

    // 9. An invalid row is never written.
    {
        int const before = CharacterDatabase.writes;
        CHECK(!G::SaveDisenchantRow(Make(0, 20, 30, 40)));
        CHECK(!G::SaveDisenchantRow(Make(10, 0, 30, 40)));
        CHECK(!G::SaveDisenchantRow(Make(10, 20, 0, 40)));
        CHECK(!G::SaveDisenchantRow(Make(10, 20, 30, 0)));
        CHECK(CharacterDatabase.writes == before);
    }

    std::printf("%d checks, %d failed\n", g_checks, g_fail);
    return g_fail == 0 ? 0 : 1;
}
