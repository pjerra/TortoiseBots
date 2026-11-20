// Standalone test of the PURE half of the commander map layers (forever/src/host/cmd/TbCmdFeedsPure.h; package P4, forever/docs/CMD-P4-NOTES.md):
//   g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/cmd_feeds_check.cpp -o /tmp/cmd_feeds_check && /tmp/cmd_feeds_check
// It also builds the COMMANDERS and COMMANDER_POS datagrams with the Turtle builders (runtime/CommanderTelemetry.h), so the wire's null-vs-empty rule and
// the caps are pinned against the code that really serializes them.
#include "forever/src/host/cmd/TbCmdFeedsPure.h"
#include "runtime/ReadyCheckPolicy.h"

#include <chrono>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace FD = TortoiseBots::cmd::feeds;
namespace CT = TortoiseBots::CommanderTelemetry;
namespace RC = TortoiseBots::ReadyCheck;

static int g_checks = 0, g_fail = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

namespace
{
size_t Count(std::string const& hay, std::string const& needle)
{
    size_t n = 0;
    for (size_t at = hay.find(needle); at != std::string::npos; at = hay.find(needle, at + needle.size()))
        ++n;
    return n;
}

// A binding with one bot in the world, no map layer reported.
CT::BindingInfo Binding(char const* commander, uint32_t commanderGuid)
{
    CT::BindingInfo b;
    b.commander = commander;
    b.commanderGuid = commanderGuid;
    b.commanderOnline = true;
    b.lead = "Mtank";
    CT::BotInfo bot;
    bot.name = "Mtank";
    bot.guid = 10;
    bot.className = "warrior";
    bot.role = "tank";
    bot.lead = true;
    bot.level = 18;
    bot.hp = 900;
    bot.maxHp = 1000;
    b.bots.push_back(bot);
    return b;
}

void TestCaps()
{
    // The cap bounds the collection, not only the rows: the sweep holds 24, the next offer is refused (the grid visit stops there), and
    // two equal distances keep a stable order (the guid), so rows do not shuffle between cycles.
    FD::Sweep<int> mobs(FD::kMobCap);
    for (uint64_t i = 0; i < 40; ++i)
        if (!mobs.Offer(1000 + i, float(i), int(i)))
            break;
    CHECK(mobs.Size() == 24);
    CHECK(mobs.Full());
    CHECK(!mobs.Offer(2000, 0.5f, 99));   // a full sweep never takes another candidate, however near
    CHECK(mobs.Size() == 24);
    std::vector<std::pair<FD::Near, int>> rows = mobs.Take();
    CHECK(rows.size() == 24);
    CHECK(rows.front().first.id == 1000 && rows.back().first.id == 1023);
    CHECK(rows.front().second == 0 && rows.back().second == 23);

    // Ties on distance: the guid orders them, so two cycles over the same units keep the same rows.
    FD::Sweep<int> ties(4);
    ties.Offer(1003, 1.0f, 3);
    ties.Offer(1000, 1.0f, 0);
    ties.Offer(1001, 1.0f, 1);
    ties.Offer(1002, 0.5f, 2);
    std::vector<std::pair<FD::Near, int>> sorted = ties.Take();
    CHECK(sorted.size() == 4);
    CHECK(sorted[0].first.id == 1002 && sorted[1].first.id == 1000 && sorted[2].first.id == 1001 && sorted[3].first.id == 1003);

    // A cap of zero keeps nothing and refuses at once; a short list is untouched.
    FD::Sweep<int> one(0);
    CHECK(one.Full());
    CHECK(!one.Offer(7, 1.0f, 0));
    CHECK(one.Take().empty());
    FD::Sweep<int> two(FD::kObjectCap);
    two.Offer(9, 4.0f, 1);
    two.Offer(8, 1.0f, 2);
    std::vector<std::pair<FD::Near, int>> pair = two.Take();
    CHECK(pair.size() == 2 && pair[0].first.id == 8 && pair[1].first.id == 9);
    CHECK(FD::kMobCap == 24);
    CHECK(FD::kObjectCap == 16);

    // The position clock: 100 ms by default, clamped.
    CHECK(FD::ClampPosMs(100) == 100);
    CHECK(FD::ClampPosMs(10) == FD::kMinPosMs);
    CHECK(FD::ClampPosMs(5000) == FD::kMaxPosMs);
    CHECK(FD::kDefaultPosMs == 100);
    // Scan ranges: a nonsensical value falls back, a sane one stays.
    CHECK(FD::ClampRange(0.0f, FD::kDefaultMobRange) == FD::kDefaultMobRange);
    CHECK(FD::ClampRange(500.0f, 60.0f) == 60.0f);
    CHECK(FD::ClampRange(80.0f, 60.0f) == 80.0f);
}

void TestRemainingSeconds()
{
    // The wire's `cl` (cdLongestS) is whole seconds; SpellHistory counts milliseconds, so a 90 s cooldown left is 90 000 ms -> 90, rounded down.
    CHECK(FD::RemainingSeconds(90000) == 90);
    CHECK(FD::RemainingSeconds(90999) == 90);
    CHECK(FD::RemainingSeconds(1000) == 1);
    CHECK(FD::RemainingSeconds(999) == 0);
    CHECK(FD::RemainingSeconds(0) == 0);
    CHECK(FD::RemainingSeconds(-5) == 0);
    CHECK(FD::RemainingSeconds(86400000) == 86400);
}

void TestSweepCost()
{
    // The caps bound the work, not only the rows: faced with a dense grid of 200 000 candidates the collector offers 24 and stops (the visit
    // ends at the first refusal), then sorts 24 rows; collecting everything would allocate a row per unit in range and sort all of them.
    size_t const dense = 200000;
    FD::Sweep<int> sweep(FD::kMobCap);
    size_t offered = 0;
    auto const started = std::chrono::steady_clock::now();
    for (size_t i = 0; i < dense; ++i)
    {
        if (!sweep.Offer(uint64_t(1000 + i), float(dense - i), int(i)))
            break;
        ++offered;
        if (sweep.Full())
            break;
    }
    std::vector<std::pair<FD::Near, int>> rows = sweep.Take();
    auto const us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started).count();
    CHECK(offered == FD::kMobCap);
    CHECK(rows.size() == FD::kMobCap);
    CHECK(rows.front().first.dist2 == float(dense - FD::kMobCap + 1));
    std::printf("cmd_feeds_check: dense sweep of %zu candidates -> %zu rows, %zu offers, %lld us\n", dense, rows.size(), offered,
        static_cast<long long>(us));
}

void TestLetters()
{
    CHECK(FD::ObjectTypeLetter(FD::GoKind::Door) == 'd');
    CHECK(FD::ObjectTypeLetter(FD::GoKind::Chest) == 'c');
    CHECK(FD::ObjectTypeLetter(FD::GoKind::Button) == 'b');
    CHECK(FD::ObjectTypeLetter(FD::GoKind::Goober) == 'g');
    CHECK(FD::ObjectTypeLetter(FD::GoKind::Ritual) == 'r');
    CHECK(FD::ObjectTypeLetter(FD::GoKind::Other) == 'o');
    CHECK(FD::ObjectTypeLetter(FD::GoKind::Skipped) == 0);
    CHECK(FD::ObjectState(true, true) == 'a');
    CHECK(FD::ObjectState(false, true) == 'r');
    CHECK(FD::ObjectState(false, false) == 'u');
}

void TestBossState()
{
    // A boss read alive is alive; a corpse or a pending respawn is dead; the instance's DONE encounter only fills an unknown.
    CHECK(FD::BossState('a', false) == 'a');
    CHECK(FD::BossState('a', true) == 'a');
    CHECK(FD::BossState('d', false) == 'd');
    CHECK(FD::BossState('u', true) == 'd');
    CHECK(FD::BossState('u', false) == 'u');
    CHECK(FD::BossState(0, false) == 'u');
}

void TestBodyless()
{
    // The failing case of spec section 7: mobs:null when the scan was not run, never [].
    std::vector<CT::BindingInfo> notRun = { Binding("Mcmd", 20) };
    std::string const a = CT::BuildPayload(12, 7, 1, 1700000200, notRun, 1700000200123LL);
    CHECK(a.find("\"mobs\":") == std::string::npos);
    CHECK(a.find("\"objects\":") == std::string::npos);
    CHECK(a.find("\"bosses\":") == std::string::npos);
    CHECK(a.find("\"view\":false") != std::string::npos);

    // The scan ran and found nothing: [], and mobs_map is the lead's map.
    std::vector<CT::BindingInfo> ran = { Binding("Mcmd", 20) };
    ran[0].mobsReported = true;
    ran[0].mobsMap = 389;
    ran[0].objectsReported = true;
    std::string const b = CT::BuildPayload(12, 7, 2, 1700000200, ran, 1700000200123LL);
    CHECK(b.find("\"mobs_map\":389,\"mobs\":[]") != std::string::npos);
    CHECK(b.find("\"objects\":[]") != std::string::npos);
    CHECK(b.find("\"bosses_map\"") == std::string::npos);

    // Bosses: one row per entry with its state, alive and dead alike, and the plan flag.
    std::vector<CT::BindingInfo> bosses = { Binding("Mcmd", 20) };
    bosses[0].bossesReported = true;
    bosses[0].bossesMap = 389;
    CT::BossInfo alive;
    alive.entry = 11517;
    alive.name = "Oggleflint";
    alive.x = -1.5f;
    alive.y = 2.5f;
    alive.state = FD::BossState('a', false);
    alive.inPlan = true;
    CT::BossInfo dead = alive;
    dead.entry = 11519;
    dead.name = "Taragaman";
    dead.state = FD::BossState('d', false);
    bosses[0].bosses = { alive, dead };
    std::string const c = CT::BuildPayload(12, 7, 3, 1700000200, bosses, 0);
    CHECK(c.find("\"bosses_map\":389") != std::string::npos);
    CHECK(c.find("\"e\":11517") != std::string::npos && c.find("\"s\":\"a\"") != std::string::npos);
    CHECK(c.find("\"e\":11519") != std::string::npos && c.find("\"s\":\"d\"") != std::string::npos);
    CHECK(c.find("\"p\":1") != std::string::npos);
}

void TestExtras()
{
    // An extra row rides the bot record: cast and its target, crowd control, rest, the boss debuffs and the ready facts.
    std::vector<CT::BindingInfo> out = { Binding("Mcmd", 20) };
    CT::BotInfo& bot = out[0].bots[0];
    bot.id = 0x00000000000000A1ULL;
    bot.target = 0x00000000000000B2ULL;
    bot.hasExtra = true;
    bot.extra.id = bot.id;
    bot.extra.state = 'c';
    bot.extra.cast = "Healing Touch";
    bot.extra.castTarget = 0x00000000000000A1ULL;
    bot.extra.cc = 's';
    bot.extra.rest = 'd';
    bot.extra.moving = true;
    bot.extra.targetName = "Oggleflint";
    CT::AuraInfo debuff;
    debuff.id = 12345;
    debuff.name = "Curse";
    debuff.dispelType = 'c';
    bot.extra.debuffs = { debuff };
    bot.extra.mc = true;
    bot.extra.hasReady = true;
    bot.extra.readyBuffs = RC::kBuffStamina | RC::kBuffIntellect;
    bot.extra.readyCons = RC::kConsWellFed;
    bot.extra.cdReady = 2;
    bot.extra.cdKnown = 3;
    bot.extra.cdLongestS = 90;
    std::string const a = CT::BuildPayload(12, 7, 4, 1700000200, out, 0);
    CHECK(a.find("\"st\":\"c\"") != std::string::npos);
    CHECK(a.find("\"cast\":\"Healing Touch\"") != std::string::npos);
    CHECK(a.find("\"ctgt\":\"161\"") != std::string::npos);
    CHECK(a.find("\"cc\":\"s\"") != std::string::npos);
    CHECK(a.find("\"rest\":\"d\"") != std::string::npos);
    CHECK(a.find("\"mv\":true") != std::string::npos);
    CHECK(a.find("\"tname\":\"Oggleflint\"") != std::string::npos);
    CHECK(a.find("\"db\":[") != std::string::npos);
    CHECK(a.find("\"mc\":1") != std::string::npos);
    CHECK(a.find("\"rdy\":{\"b\":3,\"c\":4,\"cr\":2,\"cn\":3,\"cl\":90}") != std::string::npos);
    CHECK(a.find("\"id\":\"161\"") != std::string::npos && a.find("\"target\":\"178\"") != std::string::npos);

    // No extra read: none of those keys is written.
    std::vector<CT::BindingInfo> bare = { Binding("Mcmd", 20) };
    std::string const b = CT::BuildPayload(12, 7, 5, 1700000200, bare, 0);
    CHECK(b.find("\"st\":") == std::string::npos && b.find("\"rdy\":") == std::string::npos && b.find("\"id\":") == std::string::npos);
}

void TestPositions()
{
    CT::PosRef ref;
    ref.commander = "Mcmd";
    ref.commanderGuid = 20;
    ref.combat = true;
    for (uint32_t i = 0; i < 44; ++i)
    {
        CT::PosBot bot;
        bot.g = 10 + i;
        bot.map = 389;
        bot.x = float(i);
        bot.y = 2.0f;
        bot.z = 3.0f;
        bot.o = 1.25f;
        bot.alive = i != 0;
        bot.hp = i == 0 ? 0 : 900;
        bot.maxHp = 1000;
        ref.bots.push_back(bot);
    }
    for (uint64_t i = 0; i < 30; ++i)
    {
        CT::PosMob mob;
        mob.id = 500 + i;
        mob.x = float(i);
        mob.y = -1.0f;
        mob.hpPct = 80;
        mob.victim = 10;
        ref.mobs.push_back(mob);
    }
    std::string const json = CT::BuildPosPayload(12, 7, 9, 1700000200, 1700000200123LL, ref);
    CHECK(json.find("\"fmt\":1") != std::string::npos);
    CHECK(json.find("\"type\":\"COMMANDER_POS\"") != std::string::npos);
    CHECK(json.find("\"commander\":\"Mcmd\"") != std::string::npos && json.find("\"commander_guid\":20") != std::string::npos);
    CHECK(json.find("\"combat\":true") != std::string::npos);
    // The caps: 40 bots and 24 enemies.
    CHECK(Count(json, "\"g\":") == 40);
    CHECK(Count(json, "\"id\":\"") == 24);
    CHECK(json.find("\"g\":10,\"m\":389,\"x\":0.0,\"y\":2.0,\"z\":3.0,\"o\":1.25,\"hp\":0,\"mhp\":1000,\"a\":0") != std::string::npos);
}

void TestWindow()
{
    FD::PosWindow w = FD::NextPosWindow(25, 0);
    CHECK(w.first == 0 && w.count == 20 && w.next == 20);
    w = FD::NextPosWindow(25, 20);
    CHECK(w.first == 20 && w.count == 5 && w.next == 0);
    w = FD::NextPosWindow(5, 0);
    CHECK(w.first == 0 && w.count == 5 && w.next == 0);
    w = FD::NextPosWindow(0, 3);
    CHECK(w.first == 0 && w.count == 0 && w.next == 0);
    w = FD::NextPosWindow(25, 24);
    CHECK(w.first == 24 && w.count == 1 && w.next == 0);
    CHECK(w.count <= CT::kMaxPosBindings);
}
}

int main()
{
    TestCaps();
    TestRemainingSeconds();
    TestSweepCost();
    TestLetters();
    TestBossState();
    TestBodyless();
    TestExtras();
    TestPositions();
    TestWindow();
    std::printf("cmd_feeds_check: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
