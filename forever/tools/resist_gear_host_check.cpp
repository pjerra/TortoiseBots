// The resistance-gear hold's own translation unit (forever/src/host/TbResistGear.cpp) against a fake core
// (forever/tools/resist_gear_host_fakes: Player equipment/bags with the core's in-place swap, a characters database
// with ForeverCore's row and transaction contract, the item and spell mirrors, the logger). It runs the paths a review
// found: a recovery record that cannot be written before the swap, a fresh set the world tick must leave on (and a
// wipe that keeps it), a full bag that outlasts the fast retries, switching the feature off while the bot cannot put its
// pieces back, an own piece parked in the bank, a store with the feature off, and a logout that hands its hold to the
// next login.
//
//   g++ -std=c++17 -O1 -Wall -Wextra -DTB_RESIST_GEAR_TEST -I forever/tools/resist_gear_host_fakes -I forever/src -I .
//       forever/tools/resist_gear_host_check.cpp forever/src/host/TbResistGear.cpp -o /tmp/resist_gear_host_check
//   /tmp/resist_gear_host_check
#include "host/TbResistGear.h"

#include "Bag.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "Group.h"
#include "Item.h"
#include "Log.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "ObjectGuid.h"
#include "Player.h"

#include "TbBotMgr.h"
#include "compat/TbItemPrototype.h"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace R = TortoiseBots::resistgear;

namespace
{
int g_checks = 0;
int g_fail = 0;

#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

uint64 g_nextItem = 1000;
uint32 g_nextEntry = 9000;

FakeConfigMgr g_config;
Map g_raid(true);
Group g_group;

std::vector<std::unique_ptr<Player>> g_players;
std::vector<std::unique_ptr<Item>> g_items;
std::vector<std::unique_ptr<ItemTemplate>> g_templates;

uint32 const kFireNeckEntry = 5001;   // a fire-resist neck (40 fire resistance)
uint32 const kJunkEntry = 5099;       // an item with no resistance: it only takes a bag row

void Reset()
{
    CharacterDatabase.rows.clear();
    CharacterDatabase.availableColumns = 6;
    CharacterDatabase.failWrites = false;
    CharacterDatabase.selects = 0;
    CharacterDatabase.probes = 0;
    CharacterDatabase.holdReads = 0;
    CharacterDatabase.writes = 0;
    LogLines().clear();
    ConnectedPlayers().clear();
    TortoiseBots::BotMgr::Bots().clear();
    R::ResetForTest();
    g_players.clear();
    g_items.clear();
    g_templates.clear();
    g_group = Group();

    auto neck = std::make_unique<ItemTemplate>();
    neck->equipSlot = 1;   // neck
    g_templates.push_back(std::move(neck));
    auto junk = std::make_unique<ItemTemplate>();
    junk->equipSlot = 1;
    g_templates.push_back(std::move(junk));

    ItemPrototype fire;
    fire.FireRes = 40;
    tbcompat::ItemProtoTable()[kFireNeckEntry] = fire;
}

ItemTemplate const* NeckTemplate() { return g_templates[0].get(); }
ItemTemplate const* JunkTemplate() { return g_templates[1].get(); }

Item* NewItem(uint32 entry, ItemTemplate const* tmpl)
{
    g_items.push_back(std::make_unique<Item>(g_nextItem++, entry, tmpl));
    return g_items.back().get();
}

Item* ResistNeck(Player* bot, uint8 slot)
{
    Item* item = NewItem(kFireNeckEntry, NeckTemplate());
    bot->Place(item, INVENTORY_SLOT_BAG_0, slot);
    return item;
}

Item* Junk(Player* bot, uint8 slot)
{
    Item* item = NewItem(kJunkEntry + g_nextEntry++, JunkTemplate());
    bot->Place(item, INVENTORY_SLOT_BAG_0, slot);
    return item;
}

// A grouped host bot (a real player in the group, on a raid map): the shape `Apply` arms for.
Player* MakeBot(uint64 key, char const* name = "Bot")
{
    g_players.push_back(std::make_unique<Player>(key, name));
    Player* bot = g_players.back().get();
    bot->SetMap(&g_raid);
    bot->SetGroup(&g_group);
    ConnectedPlayers()[key] = bot;
    TortoiseBots::BotMgr::Bots().insert(key);
    return bot;
}

Player* MakeRealPlayer(uint64 key)
{
    g_players.push_back(std::make_unique<Player>(key, "Human"));
    return g_players.back().get();
}

uint64 nextHuman() { return 900 + g_nextItem; }

void JoinGroup(Player* bot)
{
    g_group.AddMember(bot->GetGUID());
    g_group.AddMember(MakeRealPlayer(nextHuman())->GetGUID());
}

void Enable(int value)
{
    g_config.Set("TortoiseBots.RaidResistGear.Enable", value);
    R::LoadConfig();
}

void Tick(uint32 seconds)
{
    for (uint32 second = 0; second < seconds; ++second)
        R::Update(1000);
}

int CountLogs(std::string const& needle)
{
    int count = 0;
    for (LogLine const& line : LogLines())
        if (line.message.find(needle) != std::string::npos)
            ++count;
    return count;
}

// The normal path: a resist neck goes on, the module holds it and stores it, and the release puts the bot's own piece
// back and clears the store. Every case below leans on this working.
void BaselineSwapAndRestore()
{
    Reset();
    Enable(1);
    Player* bot = MakeBot(101, "Base");
    JoinGroup(bot);

    Item* own = NewItem(5000, NeckTemplate());
    bot->Place(own, INVENTORY_SLOT_BAG_0, 1);
    Item* resist = ResistNeck(bot, 23);

    CHECK(R::Apply(bot, 2));
    CHECK(R::Holding(bot));
    CHECK(R::HeldCount(bot) == 1);
    CHECK(bot->GetItemByPos(INVENTORY_SLOT_BAG_0, 1) == resist);
    CHECK(bot->GetItemByPos(INVENTORY_SLOT_BAG_0, 23) == own);
    CHECK(CharacterDatabase.rows.size() == 1);

    CHECK(R::Restore(bot, "release"));
    CHECK(!R::Holding(bot));
    CHECK(bot->GetItemByPos(INVENTORY_SLOT_BAG_0, 1) == own);
    CHECK(CharacterDatabase.rows.empty());
}

// A store that is not there (missing migration, no CREATE right) refuses the swap before any piece moves: nothing is
// worn without a durable record of it. An applied store is not cached as missing either.
void MissingStoreRefusesTheSwap()
{
    Reset();
    Enable(1);
    Player* bot = MakeBot(201, "NoStore");
    JoinGroup(bot);
    Item* resist = ResistNeck(bot, 23);

    CharacterDatabase.availableColumns = 0;
    CHECK(!R::Apply(bot, 2));
    CHECK(!R::Holding(bot));
    CHECK(bot->GetItemByPos(INVENTORY_SLOT_BAG_0, 1) == nullptr);
    CHECK(bot->GetItemByGuid(resist->GetGUID()) != nullptr);
    CHECK(CharacterDatabase.rows.empty());
    CHECK(CountLogs("could not be written") == 1);

    // The migration was applied and the config reloaded: the one more probe sees it.
    CharacterDatabase.availableColumns = 6;
    Enable(1);
    CHECK(R::Apply(bot, 2));
    CHECK(bot->GetItemByPos(INVENTORY_SLOT_BAG_0, 1) == resist);
    CHECK(CharacterDatabase.rows.size() == 1);
    CHECK(R::Restore(bot, "release"));
}

// The write itself does not land: the read-back catches it, so the swap is refused and no item moves.
void RefusedWriteBlocksTheSwap()
{
    Reset();
    Enable(1);
    Player* bot = MakeBot(301, "Refused");
    JoinGroup(bot);
    Item* resist = ResistNeck(bot, 23);

    CharacterDatabase.failWrites = true;
    CHECK(!R::Apply(bot, 2));
    CHECK(!R::Holding(bot));
    CHECK(bot->GetItemByPos(INVENTORY_SLOT_BAG_0, 1) == nullptr);
    CHECK(bot->GetItemByGuid(resist->GetGUID()) != nullptr);
    CHECK(CharacterDatabase.rows.empty());
    CHECK(CountLogs("could not be written") == 1);
    CHECK(CharacterDatabase.writes >= 1);    // the record (and, the first time, the CREATE) was attempted...
    CHECK(CharacterDatabase.selects >= 1);   // ...and read back, which is how the drop was noticed

    CharacterDatabase.failWrites = false;
    CHECK(R::Apply(bot, 2));
    CHECK(R::HeldCount(bot) == 1);
}

// A full bag outlasts the fast retries: the hold waits, and the bag row that frees up later still gets the set back.
void BagSpaceFreesAfterTheRetriesRanOut()
{
    Reset();
    Enable(1);
    Player* bot = MakeBot(401, "Bagged");
    JoinGroup(bot);

    for (uint8 slot = 24; slot < 39; ++slot)
        Junk(bot, slot);
    Item* resist = ResistNeck(bot, 23);
    CHECK(R::Apply(bot, 2));
    CHECK(bot->GetItemByPos(INVENTORY_SLOT_BAG_0, 1) == resist);
    CHECK(bot->GetItemByPos(INVENTORY_SLOT_BAG_0, 23) == nullptr);   // the swap freed its bag row

    // Loot takes the row the swap freed: the set can no longer come off. The release is asked for and cannot finish,
    // so the hold goes to RELEASING and the tick keeps trying.
    Item* loot = Junk(bot, 23);
    CHECK(!R::Restore(bot, "release"));
    CHECK(R::Holding(bot));

    // Far past the fast retries (5 s each): the hold is still ours and the piece is still worn.
    Tick(200);
    CHECK(R::Holding(bot));
    CHECK(R::HeldCount(bot) == 1);
    CHECK(bot->GetItemByPos(INVENTORY_SLOT_BAG_0, 1) == resist);
    CHECK(CountLogs("no free bag slot") >= 6);   // the fast passes, then the slow ones
    CHECK(CharacterDatabase.rows.size() == 1);

    // The loot leaves again: the next pass (a minute apart at this point) takes the piece off.
    bot->Remove(loot);
    Tick(61);
    CHECK(bot->GetItemByPos(INVENTORY_SLOT_BAG_0, 1) == nullptr);
    CHECK(bot->GetItemByGuid(resist->GetGUID()) != nullptr);
    CHECK(!R::Holding(bot));
    CHECK(CharacterDatabase.rows.empty());
}

// Switching the feature off while the bot is fighting or dead does not drop the recovery record: the hold stays until
// the normal set is really back, and then it clears.
void DisableKeepsAnUnrestorableHold()
{
    Reset();
    Enable(1);
    Player* bot = MakeBot(501, "Fighting");
    JoinGroup(bot);
    Item* resist = ResistNeck(bot, 23);
    CHECK(R::Apply(bot, 2));

    bot->SetInCombat(true);
    Enable(0);
    CHECK(R::Holding(bot));                                  // in combat: the release could not run
    CHECK(R::HeldCount(bot) == 1);
    CHECK(bot->GetItemByPos(INVENTORY_SLOT_BAG_0, 1) == resist);
    CHECK(CharacterDatabase.rows.size() == 1);               // still recoverable from the characters store

    bot->SetInCombat(false);
    Tick(61);
    CHECK(!R::Holding(bot));
    CHECK(bot->GetItemByPos(INVENTORY_SLOT_BAG_0, 1) == nullptr);
    CHECK(CharacterDatabase.rows.empty());

    Enable(1);
    Player* dead = MakeBot(502, "Dead");
    JoinGroup(dead);
    Item* deadResist = ResistNeck(dead, 23);
    CHECK(R::Apply(dead, 2));

    dead->SetAlive(false);
    Enable(0);
    CHECK(R::Holding(dead));                                 // dead: every piece stays recorded
    CHECK(R::HeldCount(dead) == 1);
    CHECK(dead->GetItemByPos(INVENTORY_SLOT_BAG_0, 1) == deadResist);
    CHECK(CharacterDatabase.rows.size() == 1);

    dead->SetAlive(true);
    Tick(61);
    CHECK(!R::Holding(dead));
    CHECK(dead->GetItemByPos(INVENTORY_SLOT_BAG_0, 1) == nullptr);
    CHECK(CharacterDatabase.rows.empty());
}
// A fresh set is HELD: the world tick must not take it off, however long the bot wears it. This is the pull window
// (the bot arms before the pull and the set must stay through it), and a wipe with the boss still up keeps it too.
// Only a release that was asked for writes the state and is retried.
void TheTickLeavesAFreshSetAlone()
{
    Reset();
    Enable(1);
    Player* bot = MakeBot(601, "Armed");
    JoinGroup(bot);
    Item* own = NewItem(5000, NeckTemplate());
    bot->Place(own, INVENTORY_SLOT_BAG_0, 1);
    Item* resist = ResistNeck(bot, 23);

    CHECK(R::Apply(bot, 2));
    CHECK(R::Holding(bot));
    CHECK(CharacterDatabase.rows.size() == 1);
    CHECK(!CharacterDatabase.rows[0].releasing);   // the durable record says HELD
    int const writes = CharacterDatabase.writes;
    int const statements = CharacterDatabase.selects;

    Tick(30);
    CHECK(R::Holding(bot));
    CHECK(R::HeldCount(bot) == 1);
    CHECK(bot->GetItemByPos(INVENTORY_SLOT_BAG_0, 1) == resist);
    CHECK(CharacterDatabase.writes == writes);     // no on/off cycle, no database write, no state change
    CHECK(CharacterDatabase.selects == statements);
    CHECK(CountLogs("put back") == 0);

    // A wipe: dead through the tick, then raised. The set stays on.
    bot->SetAlive(false);
    Tick(30);
    bot->SetAlive(true);
    Tick(30);
    CHECK(R::Holding(bot));
    CHECK(bot->GetItemByPos(INVENTORY_SLOT_BAG_0, 1) == resist);
    CHECK(CharacterDatabase.writes == writes);
    CHECK(CharacterDatabase.rows.size() == 1);

    // The release is the only thing that takes it off.
    CHECK(R::Restore(bot, "release"));
    CHECK(!R::Holding(bot));
    CHECK(bot->GetItemByPos(INVENTORY_SLOT_BAG_0, 1) == own);
    CHECK(CharacterDatabase.rows.empty());
}

// The bot's own piece sits somewhere a swap cannot take it from (the bank): the hold is kept, the log says why, and
// putting the piece back in the bags lets the next pass restore the set.
void ABankPieceKeepsTheHold()
{
    Reset();
    Enable(1);
    Player* bot = MakeBot(701, "Banked");
    JoinGroup(bot);
    Item* own = NewItem(5000, NeckTemplate());
    bot->Place(own, INVENTORY_SLOT_BAG_0, 1);
    Item* resist = ResistNeck(bot, 23);
    CHECK(R::Apply(bot, 2));

    // The bot's own neck goes to the bank: a position outside the bags (the backpack and the four worn bags).
    bot->Remove(own);
    bot->Place(own, INVENTORY_SLOT_BAG_0, 50);

    CHECK(!R::Restore(bot, "release"));
    CHECK(R::Holding(bot));
    CHECK(bot->GetItemByPos(INVENTORY_SLOT_BAG_0, 1) == resist);
    CHECK(CountLogs("not in the bags") == 1);
    CHECK(CharacterDatabase.rows.size() == 1);

    // Back in a bag row: the next retry puts the set back.
    bot->Remove(own);
    bot->Place(own, INVENTORY_SLOT_BAG_0, 23);
    Tick(6);
    CHECK(!R::Holding(bot));
    CHECK(bot->GetItemByPos(INVENTORY_SLOT_BAG_0, 1) == own);
    CHECK(CharacterDatabase.rows.empty());
}

// With the feature off and no store, startup and logins make one existence question and nothing else: no CREATE, no
// row read, no query per login, no warning.
void DisabledWithoutAStoreDoesNothing()
{
    Reset();
    Enable(0);
    CharacterDatabase.availableColumns = 0;

    R::LoadAtStartup();
    CHECK(CharacterDatabase.writes == 0);       // never a CREATE when off
    CHECK(CharacterDatabase.holdReads == 0);
    CHECK(CharacterDatabase.probes == 1);       // "is there a table?" once, then remembered
    CHECK(CountLogs("tortoise_bots_resist_hold") == 0);

    int const statements = CharacterDatabase.selects;
    Player* bot = MakeBot(801, "Idle");
    JoinGroup(bot);
    R::RecoverLogin(bot);
    Tick(30);
    CHECK(CharacterDatabase.selects == statements);   // no login query, no tick query
    CHECK(CharacterDatabase.writes == 0);
    CHECK(!R::Holding(bot));
}

// A hold stored while the feature was on is still restored with the feature off: one startup read, then the login
// takes the snapshot (never a query of its own) and puts the bot's pieces back.
void DisabledStillRestoresAStoredHold()
{
    Reset();
    Enable(0);

    Item* own = NewItem(5000, NeckTemplate());
    Item* resist = NewItem(kFireNeckEntry, NeckTemplate());
    CharacterDatabase.rows.push_back(HoldRow{ 901, 1, 2, own->GetGUID().GetCounter(), resist->GetGUID().GetCounter(), true });

    R::LoadAtStartup();
    CHECK(CharacterDatabase.holdReads == 1);     // the one startup read
    CHECK(CharacterDatabase.writes == 0);        // off: no CREATE, nothing written yet
    CHECK(CharacterDatabase.probes == 1);
    int const statements = CharacterDatabase.selects;

    Player* bot = MakeBot(901, "Stored");
    JoinGroup(bot);
    bot->Place(resist, INVENTORY_SLOT_BAG_0, 1);   // the bot logs in still wearing the set
    bot->Place(own, INVENTORY_SLOT_BAG_0, 23);

    R::RecoverLogin(bot);
    CHECK(!R::Holding(bot));
    CHECK(bot->GetItemByPos(INVENTORY_SLOT_BAG_0, 1) == own);
    CHECK(bot->GetItemByGuid(resist->GetGUID()) != nullptr);
    CHECK(CharacterDatabase.holdReads == 1);     // the login asked nothing: the snapshot answered
    CHECK(CharacterDatabase.selects == statements);
    CHECK(CharacterDatabase.rows.empty());
}

// A hold written after startup whose bot logs out with pieces left is kept for that character's next login, which
// restores it from memory (never a query). The hold that could not finish is not lost and no login pays a read.
void ALogoutHandsTheHoldToTheNextLogin()
{
    Reset();
    Enable(1);
    Player* bot = MakeBot(951, "Leaving");
    JoinGroup(bot);
    Item* own = NewItem(5000, NeckTemplate());
    bot->Place(own, INVENTORY_SLOT_BAG_0, 1);
    Item* resist = ResistNeck(bot, 23);
    CHECK(R::Apply(bot, 2));

    // The logout pass cannot put the piece back (its own neck is in the bank), so the hold is handed to the next login.
    bot->Remove(own);
    bot->Place(own, INVENTORY_SLOT_BAG_0, 50);
    CHECK(!R::Restore(bot, "logout"));
    CHECK(!R::Holding(bot));
    CHECK(CharacterDatabase.rows.size() == 1);
    int const reads = CharacterDatabase.holdReads;

    // The same character logs in again, its own neck back in the bags.
    Player* again = MakeBot(951, "Leaving");
    JoinGroup(again);
    again->Place(resist, INVENTORY_SLOT_BAG_0, 1);
    bot->Remove(own);
    again->Place(own, INVENTORY_SLOT_BAG_0, 23);

    R::RecoverLogin(again);
    CHECK(!R::Holding(again));
    CHECK(again->GetItemByPos(INVENTORY_SLOT_BAG_0, 1) == own);
    CHECK(again->GetItemByGuid(resist->GetGUID()) != nullptr);
    CHECK(CharacterDatabase.holdReads == reads);   // the login asked nothing
    CHECK(CharacterDatabase.rows.empty());
}

} // namespace

int main()
{
    sConfigMgr = &g_config;

    // First: ResetForTest forgets the store probe, so every scenario asks its own question; the missing-store one runs
    // first anyway, with no store at all.
    MissingStoreRefusesTheSwap();
    BaselineSwapAndRestore();
    RefusedWriteBlocksTheSwap();
    TheTickLeavesAFreshSetAlone();
    BagSpaceFreesAfterTheRetriesRanOut();
    DisableKeepsAnUnrestorableHold();
    ABankPieceKeepsTheHold();
    DisabledWithoutAStoreDoesNothing();
    DisabledStillRestoresAStoredHold();
    ALogoutHandsTheHoldToTheNextLogin();

    std::printf("resist_gear_host_check: %d checks, %d failed\n", g_checks, g_fail);
    std::printf("RESULT: %s\n", g_fail == 0 ? "PASS" : "FAIL");
    return g_fail == 0 ? 0 : 1;
}
