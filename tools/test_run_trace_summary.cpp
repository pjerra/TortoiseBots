// Standalone tests for the run trace's summary layer (runtime/RunTraceSummary.h,
// issue #986): the fields the dashboard's Dungeons tab reads from
// <run_id>.summary.json (outcome, bosses with kill times, wipes and deaths with
// positions, clear_s, mode, build, driver), and the `driver=` argument check.
//
// The header includes the standard library and the plan table's pure header
// only, so this compiles without a game header.
//
// Build and run:
//   g++ -std=c++17 -Wall -Wextra tools/test_run_trace_summary.cpp -o /tmp/test_run_trace_summary
//   /tmp/test_run_trace_summary

#include "../runtime/RunTraceSummary.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace TortoiseBots::RunTrace;

static int checks = 0;
#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        std::exit(1); \
    } \
} while (0)
#define CHECK_EQ(a, b) do { \
    ++checks; \
    std::string const va = (a); \
    std::string const vb = (b); \
    if (va != vb) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n  got:  %s\n  want: %s\n", __FILE__, __LINE__, #a, va.c_str(), vb.c_str()); \
        std::exit(1); \
    } \
} while (0)

static bool Has(std::string const& s, char const* part)
{
    return s.find(part) != std::string::npos;
}

static RenderArgs Final(char const* reason = "stop")
{
    RenderArgs a;
    a.final = true;
    a.endReason = reason;
    a.startUnix = 1760000000;
    a.endUnix = 1760004300;
    return a;
}

static std::vector<BossProgress> ThreeBosses()
{
    std::vector<BossProgress> v(3);
    v[0].entry = 8127; v[0].name = "Antu'sul";
    v[1].entry = 7272; v[1].name = "Theka the Martyr";
    v[2].entry = 7267; v[2].name = "Velratha";
    return v;
}

static void TestDriver()
{
    CHECK(IsDriverToken("driver=sonnet:medium:zf-1"));
    CHECK(IsDriverToken("DRIVER=x"));
    CHECK(!IsDriverToken("driver"));
    CHECK(!IsDriverToken("party"));
    CHECK(!IsDriverToken("drive=x"));
    CHECK(DriverRefusal("sonnet:medium:zf-ab-1-s2") == nullptr);
    CHECK(DriverRefusal("claude-opus-4.6:high:run_7+x") == nullptr);
    CHECK(DriverRefusal("human") == nullptr);
    CHECK(DriverRefusal("") != nullptr);
    CHECK(DriverRefusal("a b") != nullptr);
    CHECK(DriverRefusal("a\"b") != nullptr);
    CHECK(DriverRefusal("a/b") != nullptr);
    CHECK(DriverRefusal(std::string(kMaxDriverLen, 'a')) == nullptr);
    CHECK(DriverRefusal(std::string(kMaxDriverLen + 1, 'a')) != nullptr);
    // Not given: a player-led run is "human", anything else "unknown".
    CHECK_EQ(EffectiveDriver("", RunMode::Observe), "human");
    CHECK_EQ(EffectiveDriver("", RunMode::Commander), "unknown");
    CHECK_EQ(EffectiveDriver("", RunMode::RunLeader), "unknown");
    CHECK_EQ(EffectiveDriver("haiku:max:r1", RunMode::Observe), "haiku:max:r1");
}

static void TestMode()
{
    CHECK_EQ(ModeName(RunMode::Observe), "observe");
    CHECK_EQ(ModeName(RunMode::Commander), "commander");
    CHECK_EQ(ModeName(RunMode::RunLeader), "run_leader");
    CHECK(StrongerMode(RunMode::Commander, RunMode::Observe) == RunMode::Commander);
    CHECK(StrongerMode(RunMode::Observe, RunMode::RunLeader) == RunMode::RunLeader);
    RunDigest d;
    d.NoteMode(RunMode::RunLeader);
    d.NoteMode(RunMode::Observe);  // a later weaker sign does not downgrade it
    CHECK(d.Mode() == RunMode::RunLeader);
    CHECK(Has(d.Render(Final()), "\"mode\":\"run_leader\""));
}

static void TestOutcomeRule()
{
    // args: planKnown, total, killed, endedInWipe, manualStop
    CHECK(DecideOutcome(true, 3, 3, false, true) == Outcome::Cleared);
    CHECK(DecideOutcome(true, 3, 3, true, false) == Outcome::Cleared);   // cleared wins over a wipe at the end
    CHECK(DecideOutcome(true, 3, 2, false, true) == Outcome::Partial);
    CHECK(DecideOutcome(true, 3, 1, false, false) == Outcome::Partial);
    CHECK(DecideOutcome(true, 3, 2, true, false) == Outcome::WipedOut);
    CHECK(DecideOutcome(true, 3, 0, true, true) == Outcome::WipedOut);
    CHECK(DecideOutcome(true, 3, 0, false, true) == Outcome::Stopped);   // stopped by hand, nothing killed
    CHECK(DecideOutcome(true, 3, 0, false, false) == Outcome::Abandoned);// ended by itself, nothing killed
    CHECK(DecideOutcome(true, 0, 0, false, true) == Outcome::Stopped);
    // without a plan nothing says how far it got: never cleared, never abandoned
    CHECK(DecideOutcome(false, 3, 3, false, true) == Outcome::Stopped);
    CHECK(DecideOutcome(false, 3, 3, false, false) == Outcome::Stopped);
    CHECK(DecideOutcome(false, 0, 0, false, false) == Outcome::Stopped);
    CHECK(DecideOutcome(false, 2, 1, true, false) == Outcome::WipedOut);
    CHECK_EQ(OutcomeLabel(Outcome::Cleared), "cleared");
    CHECK_EQ(OutcomeLabel(Outcome::Partial), "partial");
    CHECK_EQ(OutcomeLabel(Outcome::WipedOut), "wiped_out");
    CHECK_EQ(OutcomeLabel(Outcome::Stopped), "stopped");
    CHECK_EQ(OutcomeLabel(Outcome::Abandoned), "abandoned");
}

static void TestIso()
{
    CHECK_EQ(IsoUtc(0), "1970-01-01T00:00:00Z");
    CHECK_EQ(IsoUtc(1760000000), "2025-10-09T08:53:20Z");
    CHECK_EQ(IsoUtc(951782400), "2000-02-29T00:00:00Z");   // a leap day
    CHECK_EQ(IsoUtc(1760004300), "2025-10-09T10:05:00Z");
}

static void TestRunningSummaryHasNoOutcome()
{
    // The summary written at start is a live run: no outcome, bosses or clear_s yet.
    RunDigest d;
    d.NoteDungeon(209);
    d.NotePlan("zf-full", ThreeBosses());
    RenderArgs running;
    running.startUnix = 1760000000;
    std::string const s = d.Render(running);
    CHECK(!Has(s, "outcome"));
    CHECK(!Has(s, "\"bosses\""));
    CHECK(!Has(s, "clear_s"));
    CHECK(!Has(s, "stop_reason"));
    CHECK(!Has(s, "\"ended\""));
    CHECK(Has(s, "\"started\":\"2025-10-09T08:53:20Z\""));
    CHECK(Has(s, "\"dungeon\":{\"map\":209}"));
    CHECK(Has(d.Render(Final()), "\"outcome\":\"stopped\""));  // the same digest, once final, has one
}

static void TestFullClear()
{
    RunDigest d;
    d.SetBuild("2026-10-16-v2", "abc12345");
    d.SetDriver("sonnet:medium:zf-ab-1-s2");
    d.NoteMode(RunMode::Commander);
    d.NoteDungeon(209);
    d.NotePlan("zf-full", ThreeBosses());
    CHECK(d.HasPlan());
    CHECK(d.IsTrackedBoss(8127, false));
    CHECK(!d.IsTrackedBoss(1234, true));
    CHECK(d.NoteBossKill(8127, 2280000));
    CHECK(!d.NoteBossKill(8127, 2290000));   // a repeat does not move the time
    CHECK(!d.NoteBossKill(1234, 100));       // not on the list
    CHECK(d.NoteBossKill(7272, 2520000));
    {
        std::string const mid = d.Render(Final());
        CHECK(Has(mid, "\"outcome\":\"partial\""));
        CHECK(!Has(mid, "clear_s"));
        CHECK(Has(mid, "\"killed_at_s\":null"));
    }
    CHECK(d.NoteBossKill(7267, 4260500));
    std::string const s = d.Render(Final());
    CHECK(Has(s, "\"mode\":\"commander\""));
    CHECK(Has(s, "\"build\":{\"image\":\"2026-10-16-v2\",\"module\":\"2026-10-16-v2\",\"core\":\"abc12345\"}"));
    CHECK(Has(s, "\"driver\":\"sonnet:medium:zf-ab-1-s2\""));
    CHECK(Has(s, "\"started\":\"2025-10-09T08:53:20Z\",\"ended\":\"2025-10-09T10:05:00Z\""));
    CHECK(Has(s, "\"stop_reason\":\"stop\""));
    CHECK(Has(s, "\"plan\":\"zf-full\""));
    CHECK(Has(s, "\"dungeon\":{\"map\":209}"));
    CHECK(Has(s, "\"outcome\":\"cleared\""));
    CHECK(Has(s, "{\"entry\":8127,\"name\":\"Antu'sul\",\"killed_at_s\":2280.0}"));
    CHECK(Has(s, "{\"entry\":7267,\"name\":\"Velratha\",\"killed_at_s\":4260.5}"));
    CHECK(Has(s, "\"clear_s\":4260.5"));   // the last boss
    CHECK(d.BossesKilled() == 3);
}

static void TestPartialAndAbandoned()
{
    RunDigest d;
    d.NoteDungeon(209);
    d.NotePlan("zf-full", ThreeBosses());
    CHECK(Has(d.Render(Final("stop")), "\"outcome\":\"stopped\""));
    CHECK(Has(d.Render(Final("scope_lost")), "\"outcome\":\"abandoned\""));
    d.NoteBossKill(7272, 61000);
    std::string const s = d.Render(Final());
    CHECK(Has(s, "\"outcome\":\"partial\""));
    CHECK(!Has(s, "clear_s"));
    CHECK(Has(s, "{\"entry\":8127,\"name\":\"Antu'sul\",\"killed_at_s\":null}"));
}

static void TestNoPlan()
{
    // Bosses the bots met are the list; a full clear is never claimed.
    RunDigest d;
    d.NoteDungeon(70);
    CHECK(d.NoteSeenBoss(2748, "Archaedas"));
    CHECK(!d.NoteSeenBoss(2748, "Archaedas"));
    CHECK(!d.IsTrackedBoss(2748, false));              // no plan: the recorder decides by rank
    d.NoteBossKill(2748, 9000);
    std::string s = d.Render(Final());
    CHECK(Has(s, "\"outcome\":\"stopped\""));   // every boss seen is dead, but nothing says that is all of them
    CHECK(!Has(s, "abandoned"));
    CHECK(!Has(s, "clear_s"));
    CHECK(!Has(s, "\"plan\""));
    // A plan that arrives later keeps the kill already seen, and the seen-boss list is replaced by the plan's.
    std::vector<BossProgress> planned(2);
    planned[0].entry = 2748; planned[0].name = "Archaedas";
    planned[1].entry = 2749; planned[1].name = "Other";
    d.NotePlan("uld", planned);
    CHECK(d.BossesKilled() == 1);
    CHECK(d.BossCount() == 2);
    d.NoteBossKill(2749, 20000);
    s = d.Render(Final());
    CHECK(Has(s, "\"outcome\":\"cleared\""));
    CHECK(Has(s, "\"clear_s\":20.0"));
    // the first plan wins
    d.NotePlan("other", ThreeBosses());
    CHECK(d.BossCount() == 2);
    CHECK(Has(d.Render(Final()), "\"plan\":\"uld\""));
}

static void TestOpenWorldHasNoOutcome()
{
    // A trace that never stood in an instance and met no boss is not a dungeon run.
    RunDigest d;
    d.SetBuild("dev", "");
    std::string const s = d.Render(Final());
    CHECK(!Has(s, "outcome"));
    CHECK(!Has(s, "bosses"));
    CHECK(!Has(s, "dungeon"));
    CHECK(Has(s, "\"mode\":\"observe\""));
    CHECK(Has(s, "\"driver\":\"human\""));
    CHECK(Has(s, "\"wipes\":[]"));
    CHECK(Has(s, "\"deaths\":[]"));
}

static Spot Fell(uint32_t tMs, float x, float y, float z, char const* bot, char const* role)
{
    Spot s;
    s.tMs = tMs; s.map = 209; s.x = x; s.y = y; s.z = z; s.bot = bot; s.role = role;
    return s;
}

static void TestDeaths()
{
    RunDigest d;
    d.NoteDungeon(209);
    d.NoteDeath(Fell(1612400, 1680.24f, 871.4f, 12.0f, "Aldo", "heal"));
    d.NoteDeath(Fell(1630000, 1678.0f, 874.9f, 12.5f, "Bria", "dps"));
    CHECK(d.DeathCount() == 2);
    std::string const s = d.Render(Final());
    CHECK(Has(s, "{\"t_s\":1612.4,\"map\":209,\"x\":1680.2,\"y\":871.4,\"z\":12.0,\"bot\":\"Aldo\",\"role\":\"heal\"}"));
    CHECK(Has(s, "\"bot\":\"Bria\",\"role\":\"dps\"}"));
    // a name is escaped
    RunDigest e;
    e.NoteDeath(Fell(1000, 1, 2, 3, "Qu\"ote", "tank"));
    CHECK(Has(e.Render(Final()), "\"bot\":\"Qu\\\"ote\""));
    // negative coordinates keep their sign
    RunDigest n;
    n.NoteDeath(Fell(1000, -45.25f, -3.0f, 0.0f, "N", "dps"));
    CHECK(Has(n.Render(Final()), "\"x\":-45.3,\"y\":-3.0,\"z\":0.0"));
    // bounded
    RunDigest big;
    for (size_t i = 0; i < kMaxSpots + 50; ++i)
        big.NoteDeath(Fell(1000, 0, 0, 0, "B", "dps"));
    CHECK(big.DeathCount() == kMaxSpots);
}

static MemberState Member(bool alive, uint32_t tMs, float x, float y, char const* role)
{
    MemberState m;
    m.alive = alive;
    m.lastDeath = Fell(tMs, x, y, 5.0f, "b", role);
    return m;
}

static void TestWipes()
{
    RunDigest d;
    d.NoteDungeon(209);
    // someone alive: no wipe
    CHECK(!d.NoteFrame(1000, { Member(false, 900, 10, 10, "dps"), Member(true, 0, 0, 0, "tank") }));
    // everyone dead: one wipe, at the centroid, role of the first to fall
    CHECK(d.NoteFrame(2000, { Member(false, 900, 10, 20, "heal"), Member(false, 1500, 20, 40, "dps"), Member(false, 1800, 30, 30, "tank") }));
    CHECK(d.WipeCount() == 1);
    // the same wipe is not counted again while they stay dead
    CHECK(!d.NoteFrame(2500, { Member(false, 900, 10, 20, "heal"), Member(false, 1500, 20, 40, "dps"), Member(false, 1800, 30, 30, "tank") }));
    CHECK(d.WipeCount() == 1);
    // back up, then down again: a second wipe
    CHECK(!d.NoteFrame(9000, { Member(true, 0, 0, 0, "heal"), Member(false, 1500, 20, 40, "dps") }));
    CHECK(d.NoteFrame(12000, { Member(false, 11000, 100, 100, "dps"), Member(false, 11500, 120, 100, "tank") }));
    CHECK(d.WipeCount() == 2);
    std::string const s = d.Render(Final());
    CHECK(Has(s, "\"wipes\":[{\"t_s\":2.0,\"map\":209,\"x\":20.0,\"y\":30.0,\"z\":5.0,\"role\":\"heal\",\"dead\":3},"
                 "{\"t_s\":12.0,\"map\":209,\"x\":110.0,\"y\":100.0,\"z\":5.0,\"role\":\"dps\",\"dead\":2}]"));
    // a lone traced bot dying is a death, not a wipe
    RunDigest one;
    CHECK(!one.NoteFrame(1000, { Member(false, 900, 1, 1, "dps") }));
    CHECK(one.WipeCount() == 0);
    // an empty frame changes nothing, and does not end a wipe in progress
    RunDigest e;
    CHECK(e.NoteFrame(1000, { Member(false, 1, 0, 0, "dps"), Member(false, 2, 0, 0, "dps") }));
    CHECK(!e.NoteFrame(1500, {}));
    CHECK(!e.NoteFrame(2000, { Member(false, 1, 0, 0, "dps"), Member(false, 2, 0, 0, "dps") }));
    CHECK(e.WipeCount() == 1);
}

static void TestPlanBossEntries()
{
    ai::DungeonPlan plan;
    auto step = [&plan](ai::PlanStepKind kind, uint32_t entry, uint32_t flags)
    {
        ai::PlanStep s;
        s.kind = kind;
        s.entry = entry;
        s.flags = flags;
        plan.steps.push_back(s);
    };
    step(ai::PlanStepKind::Move, 0, 0);
    step(ai::PlanStepKind::Pull, 111, 0);          // a pack, not a boss
    step(ai::PlanStepKind::Boss, 8127, 0);
    step(ai::PlanStepKind::Boss, 8127, 0);         // the same boss twice: once
    step(ai::PlanStepKind::Boss, 7272, ai::kPlanFlagAlliance);
    step(ai::PlanStepKind::Boss, 7273, ai::kPlanFlagHorde);
    step(ai::PlanStepKind::Loot, 5000, 0);
    std::vector<uint32_t> const both = PlanBossEntries(plan, 0);
    CHECK(both.size() == 3 && both[0] == 8127 && both[1] == 7272 && both[2] == 7273);
    std::vector<uint32_t> const alliance = PlanBossEntries(plan, ai::kPlanFlagAlliance);
    CHECK(alliance.size() == 2 && alliance[0] == 8127 && alliance[1] == 7272);
    std::vector<uint32_t> const horde = PlanBossEntries(plan, ai::kPlanFlagHorde);
    CHECK(horde.size() == 2 && horde[0] == 8127 && horde[1] == 7273);
    CHECK(PlanBossEntries(ai::DungeonPlan(), 0).empty());
}

static void TestSummaryMembers()
{
    // The digest's members go into the summary after the counters, before "oracles", and never ahead of
    // "status" (MarkSummaryIncomplete reads the first "status" key).
    Builder b("run-9");
    Header h;
    h.runId = "run-9";
    h.tbVersion = "2026-10-16-v2";
    h.startUnix = 100;
    RunDigest d;
    d.SetBuild(h.tbVersion, "abc12345");
    d.SetDriver("opus:high:zf-1");
    d.NoteDungeon(209);
    d.NotePlan("zf-full", ThreeBosses());
    d.NoteBossKill(8127, 5000);
    Counters c;
    std::string const s = b.BuildSummary(h, "complete", "stop", 160, 60000, c, "run-9.jsonl", std::string(), d.Render(Final()));
    CHECK(s.front() == '{' && s.back() == '}');
    CHECK(Has(s, "\"oracles\":[]}"));
    size_t const status = s.find("\"status\"");
    size_t const mode = s.find("\"mode\"");
    size_t const counters = s.find("\"counters\"");
    size_t const oracles = s.find("\"oracles\"");
    CHECK(status != std::string::npos && mode != std::string::npos);
    CHECK(status < counters && counters < mode && mode < oracles);
    CHECK(s.find("\"status\"", status + 1) == std::string::npos);
    CHECK(s.find("\"end_reason\"", s.find("\"end_reason\"") + 1) == std::string::npos);
    CHECK(Has(s, "\"tb_version\":\"2026-10-16-v2\""));  // the old key stays
    CHECK(Has(s, "\"build\":{\"image\":\"2026-10-16-v2\",\"module\":\"2026-10-16-v2\",\"core\":\"abc12345\"}"));
    CHECK(Has(s, "\"driver\":\"opus:high:zf-1\""));
    // Old callers pass no members and get the old summary byte for byte.
    std::string const old = b.BuildSummary(h, "complete", "stop", 160, 60000, c, "run-9.jsonl");
    CHECK(!Has(old, "\"mode\""));
    CHECK(Has(old, "\"dropped\":0,\"truncated\":false},\"oracles\":[]}"));
    // Members of an error summary come after the error, still before oracles.
    std::string const err = b.BuildSummary(h, "failed", "stop", 160, 60000, c, "run-9.jsonl", "disk full", d.Render(Final()));
    CHECK(err.find("\"error\"") < err.find("\"mode\""));
}

static void TestEndedInWipe()
{
    RunDigest d;
    d.NoteDungeon(209);
    d.NotePlan("zf-full", ThreeBosses());
    d.NoteBossKill(8127, 1000);
    CHECK(d.NoteFrame(2000, { Member(false, 1900, 1, 1, "dps"), Member(false, 1950, 2, 2, "tank") }));
    CHECK(d.WipeActive());
    std::string s = d.Render(Final("stop"));
    CHECK(Has(s, "\"outcome\":\"wiped_out\""));
    CHECK(Has(s, "\"stop_reason\":\"stop\""));
    // back up before the end: an ordinary partial
    CHECK(!d.NoteFrame(9000, { Member(true, 0, 0, 0, "dps"), Member(false, 1950, 2, 2, "tank") }));
    CHECK(!d.WipeActive());
    CHECK(Has(d.Render(Final("stop")), "\"outcome\":\"partial\""));
}

static void TestRunLeaderStep()
{
    RunDigest d;
    d.NoteDungeon(209);
    d.NoteStep(4, 5);
    d.NoteDeath(Fell(1612400, 1680.2f, 871.4f, 12.0f, "Aldo", "heal"));
    d.NoteStep(8, 9);
    d.NoteStep(2, 1);   // furthest keeps the highest step reached
    d.NoteFrame(3055000, { Member(false, 3000000, 5, 5, "heal"), Member(false, 3001000, 6, 6, "dps") });
    std::string const s = d.Render(Final());
    CHECK(Has(s, "\"leg\":\"4\",\"pull_group\":\"5\",\"bot\":\"Aldo\""));
    CHECK(Has(s, "\"leg\":\"2\",\"pull_group\":\"1\",\"role\":\"heal\",\"dead\":2"));
    CHECK(Has(s, "\"furthest\":{\"leg\":\"8\",\"pull_group\":\"9\"}"));
    // without a run leader no leg is written
    RunDigest n;
    n.NoteDeath(Fell(1000, 1, 2, 3, "N", "dps"));
    CHECK(!Has(n.Render(Final()), "pull_group"));
    CHECK(!Has(n.Render(Final()), "furthest"));
}

static void TestPartyAndKnownBosses()
{
    RunDigest d;
    d.NoteParty({ "warrior", "tank", 45 });
    d.NoteParty({ "priest", "heal", 44 });
    CHECK(d.PartySize() == 2);
    CHECK(Has(d.Render(Final()), "\"party\":[{\"class\":\"warrior\",\"role\":\"tank\",\"level\":45},{\"class\":\"priest\",\"role\":\"heal\",\"level\":44}]"));
    // Several plans on the map and none chosen: their bosses are tracked, rank-1 elites included.
    CHECK(!d.IsTrackedBoss(2748, false));
    CHECK(d.IsTrackedBoss(2748, true));      // world-boss rank
    d.NoteKnownBosses({ 2748, 2749, 2748 });
    CHECK(d.IsTrackedBoss(2748, false));
    CHECK(d.IsTrackedBoss(2749, false));
    CHECK(!d.IsTrackedBoss(1, false));
    // With a plan only its own list counts.
    d.NotePlan("uld", std::vector<BossProgress>(1, BossProgress{ 2749, "Other", -1 }));
    CHECK(d.IsTrackedBoss(2749, false));
    CHECK(!d.IsTrackedBoss(2748, true));
}

int main()
{
    TestDriver();
    TestMode();
    TestOutcomeRule();
    TestIso();
    TestRunningSummaryHasNoOutcome();
    TestFullClear();
    TestPartialAndAbandoned();
    TestNoPlan();
    TestOpenWorldHasNoOutcome();
    TestDeaths();
    TestWipes();
    TestPlanBossEntries();
    TestEndedInWipe();
    TestRunLeaderStep();
    TestPartyAndKnownBosses();
    TestSummaryMembers();
    std::printf("test_run_trace_summary: %d checks passed\n", checks);
    return 0;
}
