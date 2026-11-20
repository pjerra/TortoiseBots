// Commander port, package P5 (the fight debugger): the standalone check of the combat monitor on ForeverCore. FOREVER-COMMANDER-PORT-SPEC.md
// section 7 (P5): the fight id is stable for one fight, a wipe and leaving combat each end it, and the fight JSON is the Turtle format byte for
// byte (the golden below was captured from runtime/CombatMonitorService.cpp, the Turtle file this build compiles unchanged in its payloads).
//
//   g++ -std=c++17 -O0 -DCOMBAT_MONITOR_TEST_CONFIG -Itools/tsan_stubs -I. \
//       forever/tools/cmd_combat_check.cpp runtime/CombatMonitorService.cpp -o /tmp/cmd_combat_check -pthread
//   /tmp/cmd_combat_check            (--print-golden dumps the fixture fight instead of checking it)
//
// The pure half (forever/src/host/cmd/TbCmdCombatPure.h) is checked here too: feedability, the pass clocks, the focus pick, the aura edge walk
// and the rank letters. The wiring test (forever/tools/test_cmd_p5_wiring.py) breaks each of them and expects this check to fail.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <utility>
#include <vector>

struct PlayerbotAIConfig
{
    bool combatMonitor = false;
    bool combatTimeline = false;
};
#define COMBAT_MONITOR_TEST_CONFIG 1
#include "runtime/CombatMonitorService.h"
#undef COMBAT_MONITOR_TEST_CONFIG

#include "forever/src/host/cmd/TbCmdCombatPure.h"

PlayerbotAIConfig sPlayerbotAIConfig;

using TortoiseBots::CombatMonitorService;
using TortoiseBots::CombatMonitor::FightContext;
using TortoiseBots::CombatMonitor::MemberInfo;
using TortoiseBots::CombatMonitor::Role;
namespace P = TortoiseBots::cmd::combat::pure;

// The Turtle fight record of the fixture above (runtime/CombatMonitorService.cpp + CombatMonitorPolicy.h, captured with --print-golden): the
// dashboard's fight debugger reads these keys, so a change to one of them must be a deliberate format change on both cores.
static char const* const kGoldenFight = R"JSON({"v":12,"session":77,"seq":1,"ts":1700013000,"type":"COMBAT_FIGHT","fight":{"id":"70-1","commander":"Mcmd","commander_guid":5,"group":70,"map":389,"plan":"ragefire_chasm","leg":"entrance","step":4,"kind":"boss","start":0,"dur_ms":8000,"outcome":"kill","boss":11517,"boss_name":"Oggleflint","boss_phase":1,"boss_phase_entry":11517,"planned":[11517],"engaged":[{"e":11517,"n":"Oggleflint","c":1}],"dropped":0,"bots":[{"name":"Tanky","guid":7,"class":"warrior","role":"tank","sub":-1,"dmg":150,"heal":0,"over":0,"taken":0,"casts":1,"heal_casts":0,"deaths":0,"spells":[{"n":"Sunder Armor","c":1}],"tail_spells":[]},{"name":"Healy","guid":9,"class":"priest","role":"healer","sub":-1,"dmg":0,"heal":200,"over":50,"taken":80,"casts":0,"heal_casts":0,"deaths":0,"spells":[],"tail_spells":[]}],"debuffs":{"sampled_s":4,"up_s":{"sunder":2}},"deaths":[]}})JSON";

static int checks = 0;
static int failed = 0;

#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        ++failed; \
    } \
} while (0)

#define CHECK_EQ(got, want) do { \
    ++checks; \
    if (!((got) == (want))) { \
        std::fprintf(stderr, "FAIL %s:%d: %s != %s\n", __FILE__, __LINE__, #got, #want); \
        ++failed; \
    } \
} while (0)

static std::vector<MemberInfo> Members()
{
    return { MemberInfo{ 7, "Tanky", "warrior", Role::Tank, -1 },
             MemberInfo{ 9, "Healy", "priest", Role::Healer, -1 } };
}

static FightContext Ctx()
{
    FightContext ctx;
    ctx.plan = "ragefire_chasm";
    ctx.leg = "entrance";
    ctx.kind = "boss";
    ctx.step = 4;
    ctx.bossEntry = 11517;
    ctx.bossName = "Oggleflint";
    ctx.planned = { 11517 };
    return ctx;
}

static uint64_t g_seq = 0;
static uint64_t NextSeq()
{
    return ++g_seq;
}

static std::string FightId(std::string const& payload);

// The COMBAT_FIGHT datagrams of a group, each fight once: the service hands a finished fight out twice (UDP loss), and the dashboard's store
// files a fight by its id, so the check does the same.
static std::set<std::string> g_seenFights;

static std::vector<std::string> FightsOf(CombatMonitorService& svc, uint32_t group, int64_t ts)
{
    std::vector<std::string> out;
    svc.TakePayloads(out, 12, 77, NextSeq, ts);
    std::vector<std::string> mine;
    for (std::string const& payload : out)
        if (payload.find("\"type\":\"COMBAT_FIGHT\"") != std::string::npos &&
            payload.find("\"group\":" + std::to_string(group) + ",") != std::string::npos &&
            g_seenFights.insert(FightId(payload)).second)
            mine.push_back(payload);
    return mine;
}

static std::vector<std::string> EventsOf(CombatMonitorService& svc, uint32_t group, uint32_t nowMs)
{
    std::vector<std::string> out;
    svc.TakeEventPayloads(out, 12, 77, NextSeq, 1700010000, 1700010000000, nowMs, 0);
    std::vector<std::string> mine;
    for (std::string const& payload : out)
        if (payload.find("\"type\":\"COMBAT_EVENTS\"") != std::string::npos &&
            payload.find("\"group\":" + std::to_string(group) + ",") != std::string::npos)
            mine.push_back(payload);
    return mine;
}

// The fight's `start` is the unix second the sample that opened it ran in: the one wall-clock field of the record. Both sides of the golden are
// compared with it zeroed, so the fixture checks every other byte.
static std::string NormalizeStart(std::string payload)
{
    std::string const tag = "\"start\":";
    std::string::size_type pos = payload.find(tag);
    if (pos == std::string::npos)
        return payload;
    pos += tag.size();
    std::string::size_type end = pos;
    while (end < payload.size() && payload[end] >= '0' && payload[end] <= '9')
        ++end;
    payload.replace(pos, end - pos, "0");
    return payload;
}

static std::string FightId(std::string const& payload)
{
    std::string const tag = "\"id\":\"";
    std::string::size_type pos = payload.find(tag);
    if (pos == std::string::npos)
        return "";
    pos += tag.size();
    return payload.substr(pos, payload.find('"', pos) - pos);
}

// ---- the pure half ----

static void TestPure()
{
    CHECK(!P::Feedable(0));
    CHECK(P::Feedable(3));
    CHECK(P::Due(1000, 0, P::kSampleMs));            // the first pass after a start
    CHECK(!P::Due(1999, 1000, P::kSampleMs));
    CHECK(P::Due(2000, 1000, P::kSampleMs));
    CHECK(!P::Due(5, 4294967290u, P::kCastPollMs));  // the world timer wrapped: 11 ms of 250 elapsed
    CHECK(P::Due(300, 4294967290u, P::kCastPollMs));   // ... and 306 ms of 250 after the same wrap
    CHECK(P::RankOf(true, true) == P::Rank::Boss);
    CHECK(P::RankOf(false, true) == P::Rank::Elite);
    CHECK(P::RankOf(false, false) == P::Rank::None);
    CHECK_EQ(P::CastRank(P::Rank::Boss), 'b');
    CHECK_EQ(P::CastRank(P::Rank::Elite), 'e');
    CHECK_EQ(P::CastRank(P::Rank::None), 0);
    CHECK(!P::CastWatched(P::Rank::None));
    CHECK(P::CastWatched(P::Rank::Elite));
    // the focus: the run's boss when it is engaged, else the biggest health
    std::vector<P::FocusCandidate> engaged = { { 1234, 900 }, { 11517, 400 }, { 5678, 5000 } };
    CHECK_EQ(P::PickFocus(engaged, 11517), 1);
    CHECK_EQ(P::PickFocus(engaged, 0), 2);
    CHECK_EQ(P::PickFocus({}, 11517), -1);
    // the aura edges: the new list applied, the gone list removed, the rest untouched
    auto edges = P::AuraEdges({ 1, 3, 5 }, { 2, 3, 6 });
    CHECK_EQ(edges.size(), size_t(4));
    CHECK(edges.size() == 4 && edges[0] == std::make_pair(1u, false) && edges[1] == std::make_pair(2u, true) &&
          edges[2] == std::make_pair(5u, false) && edges[3] == std::make_pair(6u, true));
    CHECK(P::AuraEdges({}, {}).empty());
    CHECK_EQ(P::AuraEdges({ 4 }, { 4 }).size(), size_t(0));
    CHECK(P::AllDead(2, 0));
    CHECK(!P::AllDead(2, 1));
    CHECK(!P::AllDead(0, 0));
    CHECK(P::RoleOf("tank") == Role::Tank);
    CHECK(P::RoleOf("healer") == Role::Healer);
    CHECK(P::RoleOf("") == Role::Damage);
    std::vector<uint32_t> ids = { 5, 1, 5, 3 };
    P::SortUnique(ids);
    CHECK(ids == std::vector<uint32_t>({ 1, 3, 5 }));
}

// ---- the fight the golden pins: one boss fight, a kill ----
static std::string Golden()
{
    CombatMonitorService& svc = CombatMonitorService::Instance();
    sPlayerbotAIConfig.combatMonitor = true;
    sPlayerbotAIConfig.combatTimeline = false;
    svc.SyncConfig(true);
    svc.Refresh(70, Members(), true, false, Ctx(), { "sunder" }, 1000);
    svc.NoteBoss(70, 11517, "Oggleflint", 4242);
    svc.NoteBinding(70, "Mcmd", 5, 389);
    svc.OnDamage(7, 0, 150, "Tanky", 11517, 4242, "Oggleflint");
    svc.OnDamage(0, 9, 80, "Oggleflint", 11517, 4242, "Oggleflint");
    svc.OnCast(7, "Sunder Armor", false);
    svc.OnHeal(9, 200, 50);
    svc.Refresh(70, Members(), true, false, Ctx(), { "sunder" }, 2000);
    svc.OnDeath(0, 7, "Tanky", 0.0f, 0.0f, 0.0f, 11517, 0);
    svc.Refresh(70, Members(), false, false, Ctx(), {}, 9000);
    svc.Refresh(70, Members(), false, false, Ctx(), {}, 13000);
    g_seq = 0;
    std::vector<std::string> fights = FightsOf(svc, 70, 1700013000);
    CHECK_EQ(fights.size(), size_t(1));
    svc.Forget(70);
    return fights.empty() ? std::string() : fights.front();
}

static void TestGoldenFight()
{
    std::string const fight = Golden();
    CHECK(!fight.empty());
    if (fight.empty())
        return;
    CHECK_EQ(FightId(fight), std::string("70-1"));
    CHECK(fight.find("\"outcome\":\"kill\"") != std::string::npos);
    CHECK(fight.find("\"boss\":11517") != std::string::npos);
    CHECK(fight.find("\"boss_name\":\"Oggleflint\"") != std::string::npos);
    CHECK(fight.find("\"commander\":\"Mcmd\"") != std::string::npos);
    CHECK(fight.find("\"plan\":\"ragefire_chasm\"") != std::string::npos);
    // The whole record: the Turtle format (one bot 150 damage done, one 80 taken, 200 healing with 50 overheal, one cast).
    std::string const golden =
        kGoldenFight;
    if (NormalizeStart(fight) != golden)
    {
        ++failed;
        std::fprintf(stderr, "FAIL golden fight\n  got:      %s\n  expected: %s\n", fight.c_str(), golden.c_str());
    }
}

// ---- a wipe ends the fight, and the next fight has a new id ----
static void TestWipeEndsFight()
{
    CombatMonitorService& svc = CombatMonitorService::Instance();
    uint32_t const group = 71;
    svc.Refresh(group, Members(), true, false, Ctx(), {}, 1000);
    svc.OnDamage(7, 0, 30, "Tanky", 11517, 4242, "Oggleflint");
    CHECK(!svc.WindowJson(group).empty());
    svc.Refresh(group, Members(), true, false, Ctx(), {}, 4000);
    svc.Refresh(group, Members(), true, false, Ctx(), {}, 6000);
    // The last member's death ends the fight at the last sample: 5 s, the shortest fight that is kept.
    svc.OnDeath(7, 0, "Oggleflint", 1.0f, 2.0f, 3.0f, 0, 0);
    svc.OnDeath(9, 0, "Oggleflint", 4.0f, 5.0f, 6.0f, 0, 0);
    CHECK(!svc.InFight(group) || !svc.WindowJson(group).empty());
    svc.Refresh(group, Members(), false, true, Ctx(), {}, 7000);  // out of combat
    std::vector<std::string> fights = FightsOf(svc, group, 1700017000);
    CHECK_EQ(fights.size(), size_t(1));
    if (!fights.empty())
    {
        CHECK_EQ(FightId(fights.front()), std::string("71-1"));
        CHECK(fights.front().find("\"outcome\":\"wipe\"") != std::string::npos);
        CHECK(fights.front().find("\"deaths\":[{") != std::string::npos);
    }
    // The next fight of the same group: a new number, never the same id twice.
    svc.Refresh(group, Members(), true, false, Ctx(), {}, 8000);
    svc.Refresh(group, Members(), false, false, Ctx(), {}, 15000);
    svc.Refresh(group, Members(), false, false, Ctx(), {}, 19000);
    fights = FightsOf(svc, group, 1700019000);
    CHECK_EQ(fights.size(), size_t(1));
    if (!fights.empty())
        CHECK_EQ(FightId(fights.front()), std::string("71-2"));
    svc.Forget(group);
}

// ---- leaving combat ends it, with no death on either side ----
static void TestLeavingCombatEndsFight()
{
    CombatMonitorService& svc = CombatMonitorService::Instance();
    uint32_t const group = 72;
    svc.Refresh(group, Members(), true, false, Ctx(), {}, 1000);
    svc.OnDamage(7, 0, 40, "Tanky", 11321, 9001, "Ragefire Trogg");
    svc.Refresh(group, Members(), false, false, Ctx(), {}, 6000);
    svc.Refresh(group, Members(), false, false, Ctx(), {}, 10000);
    std::vector<std::string> fights = FightsOf(svc, group, 1700020000);
    CHECK_EQ(fights.size(), size_t(1));
    if (!fights.empty())
    {
        CHECK_EQ(FightId(fights.front()), std::string("72-1"));
        CHECK(fights.front().find("\"outcome\":\"end\"") != std::string::npos);
    }
    svc.Forget(group);
}

// ---- the live stream carries the fight's own id, and the timeline sample lands in it ----
static void TestEventsAndTimeline()
{
    CombatMonitorService& svc = CombatMonitorService::Instance();
    uint32_t const group = 73;
    svc.SetTimelineWatch({ 24321 });
    sPlayerbotAIConfig.combatTimeline = true;
    svc.Refresh(group, Members(), true, false, Ctx(), {}, 1000);
    svc.NoteBoss(73, 11517, "Oggleflint", 4242);
    svc.SampleTimeline(group, 400, 400, 7, "Tanky", 1200);
    svc.OnTimelineAura(7, 24321, true, 1300);
    std::vector<std::string> events = EventsOf(svc, group, 2000);
    CHECK(!events.empty());
    if (!events.empty())
    {
        CHECK(events.front().find("\"type\":\"COMBAT_EVENTS\"") != std::string::npos);
        CHECK(events.front().find("\"id\":\"73-1\"") != std::string::npos);
    }
    svc.Refresh(group, Members(), false, false, Ctx(), {}, 6000);
    svc.Refresh(group, Members(), false, false, Ctx(), {}, 10000);
    std::vector<std::string> fights = FightsOf(svc, group, 1700030000);
    CHECK_EQ(fights.size(), size_t(1));
    if (!fights.empty())
    {
        CHECK_EQ(FightId(fights.front()), std::string("73-1"));
        CHECK(fights.front().find("\"boss_hp\":[") != std::string::npos);
        CHECK(fights.front().find("\"auras\":[") != std::string::npos);
        CHECK(fights.front().find(",24321,") != std::string::npos);
    }
    sPlayerbotAIConfig.combatTimeline = false;
    svc.SetTimelineWatch({});
    svc.Forget(group);
}

int main(int argc, char** argv)
{
    bool const print = argc > 1 && std::strcmp(argv[1], "--print-golden") == 0;
    if (print)
    {
        std::string const fight = Golden();
        std::printf("%s\n", NormalizeStart(fight).c_str());
        return fight.empty() ? 1 : 0;
    }
    TestPure();
    TestGoldenFight();
    TestWipeEndsFight();
    TestLeavingCombatEndsFight();
    TestEventsAndTimeline();
    std::printf("cmd_combat_check: %d checks, %d failed\n", checks, failed);
    return failed == 0 ? 0 : 1;
}
