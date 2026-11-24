// Standalone test of the module's bot-provider mapping (forever/src/host/TbLuaBotsPure.h):
//   g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/test_lua_bots_check.cpp -o /tmp/test_lua_bots_check && /tmp/test_lua_bots_check
// The mapping is SPEC-forever-lua-bot-api.md sections 3 and 5 as the engine's BotProvider.h names it; the edge rules
// themselves (BOT_ADDED/REMOVED/DIED/WIPE) are the engine's hub and are pinned by its own test_bots.cpp.
#include "forever/src/host/TbLuaBotsPure.h"

#include <cstdio>
#include <string>
#include <vector>

namespace lb = TortoiseBots::luabots;

static int g_checks = 0, g_fail = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static lb::GuidHalves G(uint64_t hi, uint64_t lo) { return lb::GuidHalves{hi, lo}; }

static lb::BotFacts Bot(uint64_t lo, char const* name)
{
    lb::BotFacts b;
    b.player = lb::HandleRow{G(1, lo), 1, 7};
    b.name = name;
    b.level = 30;
    b.cls = 1;
    b.role = "tank";
    b.roleForced = true;
    b.spec = "Protection";
    b.hasEngine = true;
    b.engineState = "combat";
    b.alive = true;
    b.inCombat = true;
    b.master = lb::HandleRow{G(2, 100), 1, 7};
    b.commander = lb::HandleRow{G(2, 200), 1, 7};
    b.group = lb::HandleRow{G(3, 300), 0, 0};
    b.owner = 12;
    return b;
}

static void TestSnapshotRetention()
{
    // TBF-B1 third review finding 1: a bot on a far teleport stays in the snapshot for the tick it is off its map.
    CHECK(lb::KeepInSnapshot(true, true, false, false));    // in the world
    CHECK(lb::KeepInSnapshot(true, false, true, false));    // mid-teleport: IsInWorld false, IsBeingTeleportedFar true
    CHECK(!lb::KeepInSnapshot(true, false, false, false));  // out of the world and not teleporting
    CHECK(!lb::KeepInSnapshot(false, true, true, false));   // no character object
    CHECK(!lb::KeepInSnapshot(true, true, false, true));    // a logout was asked for: the bot leaves now
    CHECK(!lb::KeepInSnapshot(true, false, true, true));    // ... even mid-teleport
}

static void TestPlayerHandle()
{
    // TBF-B1 third review finding 4: no place -> no handle (never a guid at map 0).
    lb::HandleRow const offline = lb::PlayerHandle(G(1, 11), false, false, 0, 0);
    CHECK(offline.empty() && offline.mapId == 0 && offline.instanceId == 0);
    lb::HandleRow const loading = lb::PlayerHandle(G(1, 11), true, false, 0, 0);
    CHECK(loading.empty());
    lb::HandleRow const here = lb::PlayerHandle(G(1, 11), true, true, 1, 7);
    CHECK(!here.empty() && here.guid == G(1, 11) && here.mapId == 1 && here.instanceId == 7);
}

static void TestRoleWord()
{
    // TBF-B1 third review finding 6: the word is never empty.
    CHECK(lb::RoleWord(true, false) == "tank");
    CHECK(lb::RoleWord(false, true) == "healer");
    CHECK(lb::RoleWord(false, false) == "dps");
    CHECK(lb::RoleWord(true, true) == "tank");   // a tank that can heal reads tank (IsTank is asked first)
}

static void TestReadOrder()
{
    lb::OrderView const none = lb::ReadOrder(0, "none");
    CHECK(none.state == lb::OrderWord::None && none.result.empty() && none.code.empty() && none.message.empty());

    lb::OrderView const queued = lb::ReadOrder(7, "queued");
    CHECK(queued.state == lb::OrderWord::Pending && queued.result.empty() && queued.code.empty());

    lb::OrderView const walking = lb::ReadOrder(8, "pending:walking");
    CHECK(walking.state == lb::OrderWord::Pending && walking.result.empty());

    lb::OrderView const unknown = lb::ReadOrder(9, "something-new");
    CHECK(unknown.state == lb::OrderWord::Pending && unknown.result.empty());   // never guessed as success or failure

    lb::OrderView const ok = lb::ReadOrder(10, "ok");
    CHECK(ok.state == lb::OrderWord::Done && ok.result == "ok" && ok.code.empty() && ok.message.empty());

    lb::OrderView const failed = lb::ReadOrder(11, "fail:expired");
    CHECK(failed.state == lb::OrderWord::Failed && failed.code == "expired");
    CHECK(failed.result == "fail:expired" && failed.message == "fail:expired");

    lb::OrderView const bare = lb::ReadOrder(12, "fail:");
    CHECK(bare.state == lb::OrderWord::Failed && bare.code == "failed");
}

static void TestWords()
{
    CHECK(lb::RunWhy("stopped", "outside-order", "pull") == "outside-order");
    CHECK(lb::RunWhy("paused", "ignored", "outside-order") == "outside-order");
    CHECK(lb::RunWhy("running", "ignored", "started").empty());
    CHECK(lb::RunWhy("done", "ignored", "done").empty());
}

static void TestBotMapping()
{
    lb::BotFacts state = Bot(11, "Stank");
    state.orderSeq = 42;
    state.orderVerb = "attack";
    state.orderResult = "fail:expired";
    state.hasRun = true;
    state.run.leader = lb::HandleRow{G(1, 11), 1, 7};
    state.run.plan = "ragefire_chasm";
    state.run.state = "paused";
    state.run.step = 4;
    state.run.steps = 78;
    state.run.stopReason = "unused";
    state.run.note = "outside-order";

    lb::SnapshotRows const rows = lb::BuildSnapshot({state}, {});
    CHECK(rows.bots.size() == 1);
    lb::BotRow const& bot = rows.bots[0];
    CHECK(bot.player.guid == G(1, 11) && bot.player.mapId == 1 && bot.player.instanceId == 7);
    CHECK(bot.name == "Stank" && bot.level == 30 && bot.cls == 1);
    CHECK(bot.role == "tank" && bot.roleForced);
    CHECK(bot.spec == "Protection" && bot.hasEngine && bot.engineState == "combat");
    CHECK(bot.alive && bot.inCombat);
    CHECK(bot.master.guid == G(2, 100) && bot.commander.guid == G(2, 200) && bot.group.guid == G(3, 300));
    CHECK(bot.owner == 12);
    CHECK(bot.lastOrder.seq == 42 && bot.lastOrder.verb == "attack");
    CHECK(bot.lastOrder.state == lb::OrderWord::Failed && bot.lastOrder.code == "expired" &&
          bot.lastOrder.message == "fail:expired" && bot.lastOrder.result == "fail:expired");
    CHECK(bot.hasRun);
    CHECK(bot.run.leader.guid == G(1, 11) && bot.run.plan == "ragefire_chasm" && bot.run.state == "paused");
    CHECK(bot.run.step == 4 && bot.run.steps == 78 && bot.run.why == "outside-order");

    // No order: seq 0, and the module's "none" defaults never reach the engine.
    lb::BotFacts fresh = Bot(12, "Spri");
    fresh.orderSeq = 0;
    fresh.orderVerb = "none";
    fresh.orderResult = "none";
    lb::SnapshotRows const noOrderRows = lb::BuildSnapshot({fresh}, {});
    lb::BotRow const& noOrder = noOrderRows.bots.at(0);
    CHECK(noOrder.lastOrder.state == lb::OrderWord::None);
    CHECK(noOrder.lastOrder.seq == 0 && noOrder.lastOrder.verb.empty() && noOrder.lastOrder.result.empty());
    CHECK(!noOrder.hasRun && noOrder.run.leader.empty());

    // A bot with no engine: the module's word (h3::EngineStateText) travels unchanged.
    fresh.hasEngine = false;
    fresh.engineState = "non-combat";
    fresh.inCombat = false;
    lb::SnapshotRows const noEngineRows = lb::BuildSnapshot({fresh}, {});
    lb::BotRow const& noEngine = noEngineRows.bots.at(0);
    CHECK(!noEngine.hasEngine && noEngine.engineState == "non-combat" && noEngine.role == "tank");

    // A bot that is already dead at its first sight: alive=false and the dead word. The provider invents no event.
    fresh.alive = false;
    fresh.engineState = "dead";
    lb::SnapshotRows const deadRows = lb::BuildSnapshot({fresh}, {});
    lb::BotRow const& dead = deadRows.bots.at(0);
    CHECK(!dead.alive && dead.engineState == "dead");
    CHECK(lb::RunKind("death") == lb::RunKindSkip);   // the snapshot carries the state; the hub owns the edge, no event here
}

static void TestGroupsAndWipeShape()
{
    lb::GroupState group;
    group.guid = G(3, 300);
    group.members.push_back(lb::MemberState{G(1, 11), true, true, true});
    group.members.push_back(lb::MemberState{G(2, 400), false, true, true});   // the human still alive: no wipe

    lb::GroupState wiped;
    wiped.guid = G(3, 301);
    wiped.members.push_back(lb::MemberState{G(1, 12), true, false, true});
    wiped.members.push_back(lb::MemberState{G(2, 401), false, false, true});  // every in-world member dead
    wiped.members.push_back(lb::MemberState{G(2, 402), false, false, false}); // out of the world: not counted

    lb::GroupState noBot;   // a group with no bot is not needed by any B1 global
    noBot.guid = G(3, 302);
    noBot.members.push_back(lb::MemberState{G(2, 500), false, true, true});

    lb::SnapshotRows const rows = lb::BuildSnapshot({}, {group, wiped, noBot});
    CHECK(rows.groups.size() == 2);
    lb::GroupRow const& live = rows.groups[0];
    CHECK(live.group.guid == G(3, 300) && live.members.size() == 2);
    CHECK(live.members[0].isBot && live.members[0].alive && live.members[0].inWorld);
    CHECK(!live.members[1].isBot && live.members[1].alive && live.members[1].inWorld);

    lb::GroupRow const& dead = rows.groups[1];
    CHECK(dead.group.guid == G(3, 301) && dead.members.size() == 3);
    bool anyInWorldAlive = false, anyInWorld = false, anyBot = false;
    for (lb::MemberRow const& member : dead.members)
    {
        if (member.isBot)
            anyBot = true;
        if (!member.inWorld)
            continue;
        anyInWorld = true;
        if (member.alive)
            anyInWorldAlive = true;
    }
    CHECK(anyBot && anyInWorld);
    CHECK(!anyInWorldAlive);   // the flags the engine's WIPE edge reads: all in-world members dead
    CHECK(!dead.members[2].inWorld && !dead.members[2].alive);
}

static void TestSpecRoleCache()
{
    lb::SpecRoleCache cache;
    CHECK(cache.Size() == 0);
    CHECK(!cache.Fresh(G(1, 11), 1000));   // no entry yet
    CHECK(cache.Find(G(1, 11)) == nullptr);

    cache.Store(G(1, 11), "tank", "Protection", 1000);
    CHECK(cache.Size() == 1);
    CHECK(cache.Fresh(G(1, 11), 1000));    // stored now
    CHECK(cache.Fresh(G(1, 11), 1999));    // right before one second: not refreshed
    CHECK(!cache.Fresh(G(1, 11), 2000));   // one second later: refreshed, not before
    lb::SpecRoleEntry const* entry = cache.Find(G(1, 11));
    CHECK(entry && entry->role == "tank" && entry->spec == "Protection" && entry->refreshedMs == 1000);

    cache.Store(G(1, 11), "healer", "Holy", 2000);   // the refresh
    CHECK(cache.Fresh(G(1, 11), 2000));
    entry = cache.Find(G(1, 11));
    CHECK(entry && entry->role == "healer" && entry->spec == "Holy");
    CHECK(!cache.Fresh(G(1, 11), 2001 + 999));       // the window is stamped by the refresh, not by the first store

    // getMSTime wraps after ~49.7 days: the unsigned difference must still read as fresh across the wrap.
    cache.Store(G(1, 13), "dps", "Fire", 0xFFFFFF00u);
    CHECK(cache.Fresh(G(1, 13), 0x00000010u));       // 0x110 ms later
    CHECK(!cache.Fresh(G(1, 13), 0x00000400u));      // 0x500 ms later

    // A level or talent hook invalidates one bot only.
    cache.Invalidate(G(1, 11));
    CHECK(!cache.Fresh(G(1, 11), 2001) && cache.Find(G(1, 11)) == nullptr);
    CHECK(cache.Find(G(1, 13)) != nullptr);

    // A bot that left the world loses its entry; the rest stay.
    cache.Store(G(1, 12), "dps", "Shadow", 5000);
    cache.Retain({G(1, 12)});
    CHECK(cache.Size() == 1);
    CHECK(cache.Find(G(1, 13)) == nullptr && cache.Find(G(1, 12)) != nullptr);

    cache.Clear();
    CHECK(cache.Size() == 0);
}

static void TestDeadGroupMember()
{
    // An in-world member that is dead is reported alive=false: the WIPE edge reads these two flags, so a dead member must
    // never travel as alive. A member out of the world is alive=false too and is not counted by WIPE.
    lb::MemberState const dead = lb::MemberFrom(G(1, 11), true, true, true, false);
    CHECK(dead.inWorld && !dead.alive);
    lb::MemberState const gone = lb::MemberFrom(G(1, 12), true, true, false, true);
    CHECK(!gone.inWorld && !gone.alive);
    lb::MemberState const offline = lb::MemberFrom(G(1, 13), true, false, true, true);
    CHECK(!offline.inWorld && !offline.alive);
    lb::MemberState const live = lb::MemberFrom(G(1, 14), true, true, true, true);
    CHECK(live.inWorld && live.alive);

    // The same through the snapshot: one group, every in-world member dead -> the rows the hub's WIPE reads.
    lb::GroupState wiped;
    wiped.guid = G(3, 700);
    wiped.members = {dead, lb::MemberFrom(G(2, 400), false, true, true, false), gone};
    lb::SnapshotRows const rows = lb::BuildSnapshot({}, {wiped});
    CHECK(rows.groups.size() == 1);
    lb::GroupRow const& group = rows.groups.at(0);
    CHECK(group.members.size() == 3);
    for (lb::MemberRow const& member : group.members)
        if (member.inWorld)
            CHECK(!member.alive);   // every in-world member dead: the WIPE condition
}

static void TestMasterIsNotCommander()
{
    // `master` is BotMgr::MasterOf and `commander` is h3::CommanderGuid: a bot with both must keep each value in its own
    // field (BOT_ADDED/`master` reads the first, the commander-driven commands the second).
    lb::BotFacts bot = Bot(21, "Nia");
    bot.master = lb::HandleRow{G(2, 900), 1, 7};
    bot.commander = lb::HandleRow{G(2, 901), 1, 7};
    lb::SnapshotRows const rows = lb::BuildSnapshot({bot}, {});
    lb::BotRow const& row = rows.bots.at(0);
    CHECK(row.master.guid == G(2, 900));
    CHECK(row.commander.guid == G(2, 901));
    CHECK(row.master.guid != row.commander.guid);
    CHECK(row.master.mapId == 1 && row.master.instanceId == 7);

    // A bot with a commander and no master keeps the master empty (never filled from the commander).
    bot.master = lb::HandleRow{};
    lb::SnapshotRows const commanderOnly = lb::BuildSnapshot({bot}, {});
    lb::BotRow const& only = commanderOnly.bots.at(0);
    CHECK(only.master.empty() && !only.commander.empty() && only.commander.guid == G(2, 901));
}

static void TestRunKinds()
{
    CHECK(lb::RunKind("step") == lb::RunKindStep);
    CHECK(lb::RunKind("done") == lb::RunKindDone);
    CHECK(lb::RunKind("stop") == lb::RunKindStop);
    CHECK(lb::RunKind("start") == lb::RunKindSkip);
    CHECK(lb::RunKind("pause") == lb::RunKindSkip);
    CHECK(lb::RunKind("") == lb::RunKindSkip);
}

static void TestRunCursor()
{
    lb::GuidHalves const leader = G(1, 11);
    lb::RunCursor cursor;
    CHECK(cursor.Seen(leader, 1) == 0);
    CHECK(cursor.Admit(leader, 1, 1));   // start
    CHECK(cursor.Admit(leader, 1, 2));   // step
    CHECK(cursor.Admit(leader, 1, 3));   // pull: skipped by kind but the cursor still advances
    CHECK(cursor.Admit(leader, 1, 4));
    CHECK(!cursor.Admit(leader, 1, 2));  // a replay of an old entry is refused
    CHECK(!cursor.Admit(leader, 1, 1));
    CHECK(cursor.Admit(leader, 1, 5));   // done
    CHECK(cursor.Seen(leader, 1) == 5);
    CHECK(!cursor.Admit(leader, 1, 5));
    for (int i = 0; i < 3; ++i)           // the whole ring is walked every tick: old entries stay refused
        CHECK(!cursor.Admit(leader, 1, 5));

    // A stop and a restart in the same tick: a NEW record, so a NEW runId admits its events from the start even when the world clock is
    // the same and its seqs restart below the old ones (finding 3: the same-tick restart used to keep the old start clock).
    CHECK(cursor.Admit(leader, 2, 1));
    CHECK(cursor.Seen(leader, 2) == 1);
    CHECK(cursor.Seen(leader, 1) == 0);   // the old record's cursor is gone with the key
    CHECK(cursor.Admit(leader, 2, 2));

    // A second leader keeps its own cursor.
    lb::GuidHalves const other = G(1, 12);
    CHECK(cursor.Admit(other, 1, 1));
    CHECK(cursor.Seen(other, 1) == 1 && cursor.Seen(leader, 2) == 2);

    // Prune keeps the live records (leader and runId) and drops the rest.
    cursor.Prune({{leader, 2}, {other, 1}});
    CHECK(cursor.Seen(leader, 2) == 2);
    CHECK(cursor.Seen(other, 1) == 1);
    cursor.Prune({{other, 1}});
    CHECK(cursor.Seen(leader, 2) == 0 && cursor.Seen(other, 1) == 1);

    cursor.Clear();
    CHECK(cursor.Seen(other, 1) == 0);
}

int main()
{
    TestSnapshotRetention();
    TestPlayerHandle();
    TestRoleWord();
    TestReadOrder();
    TestWords();
    TestBotMapping();
    TestGroupsAndWipeShape();
    TestSpecRoleCache();
    TestDeadGroupMember();
    TestMasterIsNotCommander();
    TestRunKinds();
    TestRunCursor();
    std::printf("%d checks, %d failed\n", g_checks, g_fail);
    return g_fail == 0 ? 0 : 1;
}
