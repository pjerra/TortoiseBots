// Standalone test of the startup pool reset (forever/src/host/pool/TbPoolReset.cpp) against a fake core:
//   g++ -std=c++17 -O0 -DTORTOISEBOTS_SLICE1 -I forever/tools/pool_registry_fakes -I forever/src -I .
//       forever/tools/pool_reset_check.cpp forever/src/host/pool/TbPoolRegistry.cpp
//       forever/src/host/pool/TbPoolCreate.cpp forever/src/host/pool/TbPoolReset.cpp -o /tmp/pool_reset_check
//       && /tmp/pool_reset_check
//
// The three translation units are the module's own; the fake core is pool_registry_fakes (characters and auth
// databases, the session table, guild and auction tables, Player::DeleteFromDB). What is exercised is the safety
// contract the reset exists for:
//  - a guild led by a pool character that holds a real player's character (or a dangling member row) refuses the
//    whole reset before any deletion, with no generation recorded;
//  - an auction listing, a human session, or a character that moved accounts refuses it the same way;
//  - `once:t1` deletes every ready pool row once, keeps a character on a pool account that has no ready row, cleans the
//    module's rows, records t1, and a second plan with the same token deletes nothing;
//  - the real player's character is never deleted and never named by a plan.
#include "DatabaseEnv.h"
#include "AccountMgr.h"
#include "CharacterCache.h"
#include "Config.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "ObjectGuid.h"
#include "Player.h"
#include "Random.h"
#include "StringFormat.h"
#include "TbBotMgr.h"
#include "World.h"
#include "runtime/BotNameService.h"

#include "host/pool/TbPoolCreate.h"
#include "host/pool/TbPoolRegistry.h"
#include "host/pool/TbPoolReset.h"

#include <algorithm>
#include <cstdio>
#include <string>

namespace pool = TortoiseBots::pool;

static int g_checks = 0, g_fail = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

namespace
{
char const* const kStampOne = "0123456789ABCDEF";
char const* const kStampTwo = "FEDCBA9876543210";

void Reset()
{
    CharacterDatabase = FakePoolDatabase();
    fakeAccounts.clear();
    fakeNextAccountId = 1;
    fakeCharacterCache.clear();
    fakeBotMgrState = FakeBotMgrState();
    fakeNextCharacterGuid = 1;
    fakePendingCreations.clear();
    fakeLoginHeld.clear();
    fakePoolEvents.clear();
    fakeBots.clear();
    fakeLogouts.clear();
    fakeDeletes.clear();
    fakeConnectedPlayers.clear();
    fakeSessions.clear();
    TortoiseBots::fakeBotNames.clear();
    TortoiseBots::fakeBotNameIndex = 0;
    fakeAuthDown = false;
    fakeEmailDown = false;
    fakeConfig.bools.clear();
    fakeConfig.ints.clear();
    fakeConfig.strings.clear();
    fakeConfig.bools["TortoiseBots.Pool.Enable"] = true;
    fakeConfig.ints["TortoiseBots.Pool.CreatePerMinute"] = 600;
    fakeConfig.ints["TortoiseBots.Pool.MaxCharacters"] = 6;
    fakeConfig.strings["TortoiseBots.Pool.Reset"] = "off";
    CharacterDatabase.extraTables["tb_pool_state"] = { "pool_name", "applied_generation", "completed_at" };
    CharacterDatabase.extraTables["ai_playerbot_db_store"] = { "guid" };
    CharacterDatabase.extraTables["ai_playerbot_custom_strategy"] = { "owner" };
    CharacterDatabase.extraTables["tb_commander_binding"] = { "commander_guid" };
    CharacterDatabase.extraTables["tortoise_bots_guild_order"] = { "bot_guid" };
    CharacterDatabase.extraTables["tortoise_bots_guild_disenchant"] = { "bot_guid" };
    pool::LoadConfig();
    pool::LoadConfig();          // a reload: keys, schema cache and rows start from the fake database
    pool::Reset::Shutdown();
}

void SeedPoolCharacter(std::string const& username, uint32 accountId, std::string const& name, uint64 guid, std::string const& stamp)
{
    fakeAccounts.push_back(FakeAccount{ accountId, username, stamp });
    fakeNextAccountId = std::max(fakeNextAccountId, accountId + 1);
    FakePoolDatabase::AccountRow account;
    account.username = username;
    account.hasId = true;
    account.id = accountId;
    account.stamp = stamp;
    account.state = "ready";
    account.created = 1;
    CharacterDatabase.accounts.push_back(account);

    FakePoolDatabase::CharacterRow row;
    row.name = name;
    row.hasGuid = true;
    row.guid = guid;
    row.accountId = accountId;
    row.race = 1;
    row.klass = 1;
    row.gender = 0;
    row.state = "ready";
    row.created = 1;
    CharacterDatabase.characters.push_back(row);

    CharacterDatabase.coreCharacters.push_back(FakePoolDatabase::CoreCharacterRow{ name, guid, accountId, false });
}

// A real player's account and character: no registry row anywhere.
void SeedRealPlayer(uint32 accountId, std::string const& username, std::string const& name, uint64 guid)
{
    fakeAccounts.push_back(FakeAccount{ accountId, username, "REALPLAYERSTAMP00" });
    CharacterDatabase.coreCharacters.push_back(FakePoolDatabase::CoreCharacterRow{ name, guid, accountId, false });
}

void SeedTwoPoolCharacters()
{
    SeedPoolCharacter("TBPOOL0001", 1, "Poolone", 101, kStampOne);
    SeedPoolCharacter("TBPOOL0002", 2, "Pooltwo", 102, kStampTwo);
    SeedRealPlayer(900, "REALPLAYER", "Realchar", 900);
}

void RunToCompletion()
{
    for (int i = 0; i < 40 && pool::Reset::Active(); ++i)
        pool::Reset::Update(1000);
}

bool HasCoreCharacter(uint64 guid)
{
    for (FakePoolDatabase::CoreCharacterRow const& row : CharacterDatabase.coreCharacters)
        if (row.guid == guid)
            return true;
    return false;
}

bool HasStateRow(std::string const& generation)
{
    for (FakePoolDatabase::StateRow const& row : CharacterDatabase.stateRows)
        if (row.poolName == "random" && row.appliedGeneration == generation)
            return true;
    return false;
}

void TestRealPlayerGuildRefusesBeforeAnyDeletion()
{
    Reset();
    SeedTwoPoolCharacters();
    FakePoolDatabase::GuildRow guild;
    guild.guildid = 5;
    guild.name = "X";
    guild.leaderGuid = 101;
    CharacterDatabase.guilds.push_back(guild);
    CharacterDatabase.guildMembers.push_back(FakePoolDatabase::GuildMemberRow{ 5, 101 });
    CharacterDatabase.guildMembers.push_back(FakePoolDatabase::GuildMemberRow{ 5, 900 });

    pool::Reset::PlanAtStartup("once:t-guild");
    CHECK(fakeDeletes.empty());
    CHECK(CharacterDatabase.stateRows.empty());
    CHECK(!pool::Reset::Active());
    CHECK(pool::Reset::PoolAvailable());          // a refusal before maintenance leaves the pool available
    CHECK(HasCoreCharacter(900));                 // the real player's character is untouched
    CHECK(HasCoreCharacter(101) && HasCoreCharacter(102));
    CHECK(pool::Reset::StatusLine().find("phase=failed") != std::string::npos);
    CHECK(pool::Reset::StatusLine().find("targets=2") != std::string::npos);

    // Even a second tick cannot delete anything: the plan is dead, and the pool is usable.
    pool::Reset::Update(1000);
    CHECK(fakeDeletes.empty());
}

void TestDanglingGuildMemberRefuses()
{
    Reset();
    SeedTwoPoolCharacters();
    FakePoolDatabase::GuildRow guild;
    guild.guildid = 6;
    guild.name = "Dangling";
    guild.leaderGuid = 101;
    CharacterDatabase.guilds.push_back(guild);
    CharacterDatabase.guildMembers.push_back(FakePoolDatabase::GuildMemberRow{ 6, 101 });
    CharacterDatabase.guildMembers.push_back(FakePoolDatabase::GuildMemberRow{ 6, 777 });   // no characters row

    pool::Reset::PlanAtStartup("once:t-dangling");
    CHECK(fakeDeletes.empty());
    CHECK(CharacterDatabase.stateRows.empty());
    CHECK(pool::Reset::StatusLine().find("phase=failed") != std::string::npos);
}

void TestAnUnprovenReadyRowRefuses()
{
    Reset();
    SeedTwoPoolCharacters();
    fakeAuthDown = true;                          // the registry cannot prove either account this start

    pool::Reset::PlanAtStartup("once:t-auth");
    CHECK(fakeDeletes.empty());
    CHECK(CharacterDatabase.stateRows.empty());
    CHECK(pool::Reset::StatusLine().find("phase=failed") != std::string::npos);
    CHECK(HasCoreCharacter(101) && HasCoreCharacter(102));
}

void TestAuctionRefuses()
{
    Reset();
    SeedTwoPoolCharacters();
    CharacterDatabase.auctionOwners.insert(102);

    pool::Reset::PlanAtStartup("once:t-auction");
    CHECK(fakeDeletes.empty());
    CHECK(CharacterDatabase.stateRows.empty());
}

void TestHumanSessionRefuses()
{
    Reset();
    SeedTwoPoolCharacters();
    fakeSessions[1] = WorldSession(false);        // a player, not a bot

    pool::Reset::PlanAtStartup("once:t-human");
    CHECK(fakeDeletes.empty());
    CHECK(CharacterDatabase.stateRows.empty());
}

void TestMovedAccountRefuses()
{
    Reset();
    SeedTwoPoolCharacters();
    for (FakePoolDatabase::CoreCharacterRow& row : CharacterDatabase.coreCharacters)
        if (row.guid == 101)
            row.accountId = 2;                    // the character moved off its pool account

    pool::Reset::PlanAtStartup("once:t-moved");
    CHECK(fakeDeletes.empty());
    CHECK(CharacterDatabase.stateRows.empty());
}

void TestOnceResetRunsOnceAndKeepsUntrackedCharacters()
{
    Reset();
    SeedTwoPoolCharacters();
    // A character on a pool account with no ready pool row: somebody's, kept and counted.
    CharacterDatabase.coreCharacters.push_back(FakePoolDatabase::CoreCharacterRow{ "Untracked", 103, 1, false });
    // The module's own rows for a target, all of which the reset cleans.
    CharacterDatabase.moduleDbStoreGuids = { 101 };
    CharacterDatabase.moduleCustomStrategyOwners = { 101 };
    CharacterDatabase.moduleCommanderGuids = { 101 };
    CharacterDatabase.moduleOrderBotGuids = { 101 };
    CharacterDatabase.moduleDisenchantBotGuids = { 101 };

    pool::Reset::PlanAtStartup("once:t-ok");
    CHECK(pool::Reset::Active());
    CHECK(!pool::Reset::PoolAvailable());
    CHECK(pool::Reset::StatusLine().find("phase=planning") != std::string::npos);
    CHECK(pool::Reset::StatusLine().find("targets=2") != std::string::npos);

    RunToCompletion();
    CHECK(!pool::Reset::Active());
    CHECK(pool::Reset::PoolAvailable());
    CHECK(pool::Reset::StatusLine().find("phase=complete") != std::string::npos);
    CHECK(pool::Reset::StatusLine().find("applied=t-ok") != std::string::npos);
    CHECK(fakeDeletes.size() == 2);
    CHECK(std::find(fakeDeletes.begin(), fakeDeletes.end(), 101) != fakeDeletes.end());
    CHECK(std::find(fakeDeletes.begin(), fakeDeletes.end(), 102) != fakeDeletes.end());
    CHECK(!HasCoreCharacter(101) && !HasCoreCharacter(102));
    CHECK(HasCoreCharacter(103));                 // kept: not a pool row
    CHECK(HasCoreCharacter(900));                 // the real player's character
    CHECK(HasStateRow("t-ok"));
    CHECK(CharacterDatabase.moduleDbStoreGuids.empty());
    CHECK(CharacterDatabase.moduleCustomStrategyOwners.empty());
    CHECK(CharacterDatabase.moduleCommanderGuids.empty());
    CHECK(CharacterDatabase.moduleOrderBotGuids.empty());
    CHECK(CharacterDatabase.moduleDisenchantBotGuids.empty());
    // Every ready pool row is gone; the kept character has no row.
    for (FakePoolDatabase::CharacterRow const& row : CharacterDatabase.characters)
        CHECK(row.state != "ready");

    // A restart with the same token: the generation is recorded, so nothing is deleted.
    pool::Reset::Shutdown();
    pool::LoadConfig();
    pool::LoadConfig();
    std::size_t const deletesBefore = fakeDeletes.size();
    pool::Reset::PlanAtStartup("once:t-ok");
    CHECK(!pool::Reset::Active());
    CHECK(pool::Reset::PoolAvailable());
    RunToCompletion();
    CHECK(fakeDeletes.size() == deletesBefore);
    CHECK(pool::Reset::StatusLine().find("applied=t-ok") != std::string::npos);
}

void TestAnEmptyPoolStillRecordsTheGeneration()
{
    Reset();
    SeedRealPlayer(900, "REALPLAYER", "Realchar", 900);

    pool::Reset::PlanAtStartup("once:t-empty");
    CHECK(pool::Reset::Active());
    RunToCompletion();
    CHECK(pool::Reset::StatusLine().find("phase=complete") != std::string::npos);
    CHECK(HasStateRow("t-empty"));
    CHECK(fakeDeletes.empty());
    CHECK(HasCoreCharacter(900));
}

} // namespace

int main()
{
    TestRealPlayerGuildRefusesBeforeAnyDeletion();
    TestDanglingGuildMemberRefuses();
    TestAnUnprovenReadyRowRefuses();
    TestAuctionRefuses();
    TestHumanSessionRefuses();
    TestMovedAccountRefuses();
    TestOnceResetRunsOnceAndKeepsUntrackedCharacters();
    TestAnEmptyPoolStillRecordsTheGeneration();

    if (g_fail)
    {
        std::printf("FAILED: %d of %d checks\n", g_fail, g_checks);
        return 1;
    }
    std::printf("RESULT: PASS (%d checks)\n", g_checks);
    return 0;
}
