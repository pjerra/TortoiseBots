// Standalone check of the relist ledger's guarded characters store
// (forever/src/host/TbHostGuildOrderQueuesStore.cpp, TBF #155). It compiles
// the store's OWN translation unit against forever/tools/guild_relist_store_fakes
// (the information_schema probe, the single-table SELECT, REPLACE/UPDATE/DELETE
// and ForeverCore's "Query stands on the first row" contract), so the load, the
// checked claim/receive writes, the deposit list and the expiry sweep run
// exactly as on the core.
//   g++ -std=c++17 -O1 -Wall -Wextra -I forever/tools/guild_relist_store_fakes -I forever/src -I .
//       forever/tools/guild_relist_store_check.cpp
//       forever/src/host/TbHostGuildOrderQueuesStore.cpp -o /tmp/guild_relist_store_check
#include "DatabaseEnv.h"

#include "host/TbHostGuildOrderQueues.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

namespace G = tbhost::guild_orders;
using TortoiseBots::GuildOrders::RelistBotPresence;
using TortoiseBots::GuildOrders::RelistLedgerEntry;
using TortoiseBots::GuildOrders::RelistState;

static int g_checks = 0, g_fail = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

namespace
{
void Reset(int columns = 15)
{
    CharacterDatabase.rows.clear();
    CharacterDatabase.columns = columns;
    CharacterDatabase.failWrites = false;
    CharacterDatabase.selects = 0;
    CharacterDatabase.writes = 0;
    CharacterDatabase.creates = 0;
    G::LoadGuildOrderQueues();
}

FakeDatabase::Row MakeRow(uint32_t bot, uint32_t seq, uint64_t state = 0)
{
    FakeDatabase::Row row;
    row.bot = bot;
    row.seq = seq;
    row.guild = 42;
    row.item = 2770;
    row.count = 5;
    row.unitPrice = 100;
    row.bid = 300;
    row.buyout = 500;
    row.deposit = 20;
    row.postedAt = 1000;
    row.expiresAt = 1000 + 8 * 3600;
    row.state = state;
    return row;
}

bool TableHas(uint32_t bot, uint32_t seq)
{
    for (FakeDatabase::Row const& row : CharacterDatabase.rows)
        if (row.bot == bot && row.seq == seq)
            return true;
    return false;
}

FakeDatabase::Row const* TableRow(uint32_t bot, uint32_t seq)
{
    for (FakeDatabase::Row const& row : CharacterDatabase.rows)
        if (row.bot == bot && row.seq == seq)
            return &row;
    return nullptr;
}
}

int main()
{
    // 1. Failing case first: a missing column must leave the ledger off, not
    //    throw and not attempt a row write.
    Reset(14);
    CHECK(!G::IsRelistStoreAvailable());
    CHECK(CharacterDatabase.creates == 1);   // the module's own guarded CREATE
    CHECK(G::RelistClaimed(10, 1, 99, 5, 0) == false);
    CHECK(G::RelistReceived(10, 1, 5, 0) == false);
    CHECK(CharacterDatabase.writes == 0);
    CHECK(CharacterDatabase.rows.empty());

    // 2. A single stored row survives the load: the first row the core's Query
    //    already fetched must be read, not skipped.
    Reset();
    CHECK(G::IsRelistStoreAvailable());
    CharacterDatabase.rows.push_back(MakeRow(10, 1, 1));
    CharacterDatabase.rows.back().claimMail = 77;
    CharacterDatabase.rows.back().heldGold = 120;
    G::LoadGuildOrderQueues();
    {
        std::vector<RelistLedgerEntry> const rows = G::RelistLedger(10);
        CHECK(rows.size() == 1);
        CHECK(rows.size() == 1 && rows[0].state == RelistState::Claimed);
        CHECK(rows.size() == 1 && rows[0].claimMailId == 77);
        CHECK(rows.size() == 1 && rows[0].heldGold == 120);
        CHECK(rows.size() == 1 && rows[0].buyout == 500);
    }

    // 3. A claim names the mail and the value; the table and memory agree.
    CHECK(G::RelistClaimed(10, 1, 900, 480, 0));
    {
        FakeDatabase::Row const* row = TableRow(10, 1);
        CHECK(row && row->state == 1 && row->claimMail == 900 && row->heldGold == 480);
        std::vector<RelistLedgerEntry> const rows = G::RelistLedger(10);
        CHECK(rows.size() == 1 && rows[0].claimMailId == 900 && rows[0].heldGold == 480);
    }
    // A claim without a mail is refused.
    CHECK(G::RelistClaimed(10, 1, 0, 480, 0) == false);

    // 4. Received settles the claim and keeps the value owed.
    CHECK(G::RelistReceived(10, 1, 480, 0));
    {
        FakeDatabase::Row const* row = TableRow(10, 1);
        CHECK(row && row->state == 2 && row->claimMail == 0 && row->heldGold == 480);
        std::vector<RelistLedgerEntry> const deposits = G::RelistDeposits(10);
        CHECK(deposits.size() == 1);
        CHECK(deposits.size() == 1 && deposits[0].heldGold == 480);
    }
    // A posted row is not a deposit.
    CharacterDatabase.rows.push_back(MakeRow(10, 2, 0));
    G::LoadGuildOrderQueues();
    CHECK(G::RelistDeposits(10).size() == 1);

    // 5. A write the table does not keep is reported and memory stays put.
    CharacterDatabase.failWrites = true;
    CHECK(G::RelistReceived(10, 1, 7, 0) == false);
    {
        std::vector<RelistLedgerEntry> const rows = G::RelistLedger(10);
        bool found = false;
        for (RelistLedgerEntry const& row : rows)
            if (row.seq == 1)
            {
                found = true;
                CHECK(row.heldGold == 480);
            }
        CHECK(found);
    }
    CharacterDatabase.failWrites = false;

    // 6. Ledger row writes: save, then delete.
    {
        RelistLedgerEntry row;
        row.botGuidLow = 20;
        row.seq = 3;
        row.guildId = 42;
        row.itemId = 2589;
        row.count = 2;
        row.unitPrice = 50;
        row.postedAtSec = 2000;
        row.expiresAtSec = 2000 + 8 * 3600;
        row.state = RelistState::Received;
        row.heldCount = 2;
        CHECK(G::SaveRelistRow(row));
        CHECK(TableHas(20, 3));
        CHECK(G::RelistDeposits(20).size() == 1);
        CHECK(G::DeleteRelistRow(20, 3));
        CHECK(!TableHas(20, 3));
        CHECK(G::RelistLedger(20).empty());
    }

    // 7. The expiry sweep drops a stale posted row only with the bot seen
    //    online after the auction ended; a fresh row stays.
    {
        Reset();
        uint64_t const now = 30ull * 24 * 60 * 60;
        FakeDatabase::Row stale = MakeRow(30, 1, 0);
        stale.postedAt = now - 3ull * 24 * 60 * 60 - 10;
        stale.expiresAt = stale.postedAt + 8 * 3600;
        CharacterDatabase.rows.push_back(stale);
        FakeDatabase::Row fresh = MakeRow(31, 1, 0);
        fresh.postedAt = now - 100;
        fresh.expiresAt = fresh.postedAt + 8 * 3600;
        CharacterDatabase.rows.push_back(fresh);
        G::LoadGuildOrderQueues();
        CHECK(G::RelistLedger(30).size() == 1);
        CHECK(G::RelistLedger(31).size() == 1);

        std::vector<uint32_t> const aged = G::AgedRelistBots(now);
        CHECK(std::find(aged.begin(), aged.end(), 30u) != aged.end());
        CHECK(std::find(aged.begin(), aged.end(), 31u) == aged.end());

        std::unordered_map<uint32_t, RelistBotPresence> presence;
        RelistBotPresence online;
        online.online = true;
        online.loginSec = now - 4000;   // after the auction end, past the settle hour
        presence[30] = online;
        RelistBotPresence off;
        presence[31] = off;
        G::ExpireRelistRows(now, presence);
        CHECK(G::RelistLedger(30).empty());
        CHECK(!TableHas(30, 1));
        CHECK(G::RelistLedger(31).size() == 1);
        CHECK(TableHas(31, 1));
    }

    // 8. The giver-attribution stash drains one item at a time.
    {
        G::StashTakenMailSender(5, 2770, 2);
        G::StashTakenMailSender(6, 2770, 3);
        G::StashTakenMailSender(7, 2589, 1);
        std::vector<TortoiseBots::GuildOrders::TakenMailSender> const ore = G::TakeTakenMailSenders(2770);
        CHECK(ore.size() == 2);
        CHECK(ore.size() == 2 && ore[0].senderLow == 5 && ore[0].count == 2);
        CHECK(G::TakeTakenMailSenders(2770).empty());
        CHECK(G::TakeTakenMailSenders(2589).size() == 1);
    }

    std::printf("%d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
