// Standalone tests of forever/src/host/run/TbPlanMatch.h (the pure part of `.tb plan check` and of the step unit load, review fixes N5) and of
// forever/src/compat/TbSqlNames.h (the names the schema guard validates).
//   g++ -std=c++17 -Wall -Wextra tools/test_forever_plan_match.cpp -o /tmp/test_forever_plan_match && /tmp/test_forever_plan_match
#include "../forever/src/host/run/TbPlanMatch.h"
#include "../forever/src/compat/TbSqlNames.h"

#include <cstdlib>
#include <iostream>

using namespace TortoiseBots::run;

static int g_checks = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { std::cerr << "FAILED line " << __LINE__ << ": " #cond "\n"; std::exit(1); } } while (0)

static StepUnit Row(int leg, int seq, int idx, unsigned entry, float x, float y, float z, char const* role)
{
    StepUnit u;
    u.leg = std::uint8_t(leg);
    u.seq = std::uint16_t(seq);
    u.idx = std::uint8_t(idx);
    u.entry = entry;
    u.x = x;
    u.y = y;
    u.z = z;
    u.role = role;
    return u;
}

using Spawns = std::map<std::uint32_t, std::vector<SpawnRef>>;
static SpawnLookup Lookup(Spawns const& s)
{
    return [&s](std::uint32_t entry) -> std::vector<SpawnRef> const* { auto it = s.find(entry); return it == s.end() ? nullptr : &it->second; };
}

static UnitCheck const& At(PlanCheck const& c, int leg, int seq, int idx)
{
    for (UnitCheck const& u : c.units)
        if (u.leg == leg && u.seq == seq && u.idx == idx)
            return u;
    std::cerr << "no such unit\n";
    std::exit(1);
}

static void MatcherOrder()
{
    // The exporter's duplicate: a pack row and a patrol row of the SAME creature, one spawn. The patrol row is nearer to the spawn than the pack row.
    Spawns spawns{ { 11318, { { 100, 0.4f, 0, 0 } } } };
    std::vector<StepUnit> rows{ Row(2, 2, 0, 11318, 0, 0, 0, "pack"), Row(2, 2, 1, 11318, 0.5f, 0, 0, "patrol") };
    PlanCheck c = MatchPlanUnits(rows, Lookup(spawns));
    // the old single pass gave the spawn to the patrol row (0.1 yd) and left the PACK row missing; now the pack row takes it and the patrol row shares it
    CHECK(At(c, 2, 2, 0).result == UnitResult::Matched && At(c, 2, 2, 0).spawnId == 100);
    CHECK(At(c, 2, 2, 1).result == UnitResult::Matched && At(c, 2, 2, 1).spawnId == 100);
    CHECK(c.matchedCount == 2 && c.missingCount == 0 && c.steps == 1 && c.stepsOk == 1 && c.stepsShort == 0);
}

static void PatrolTakesAFreeSpawn()
{
    // a patrol that is a creature of its own (a second spawn of the entry, 4.9 yd away) takes the free one, not its pack row's
    Spawns spawns{ { 11321, { { 1, -111.34f, 52.05f, -18.69f }, { 2, -111.0f, 57.0f, -18.69f } } } };
    std::vector<StepUnit> rows{ Row(5, 1, 0, 11321, -111.34f, 52.05f, -18.69f, "pack"), Row(5, 1, 1, 11321, -111.0f, 56.9f, -18.69f, "patrol") };
    PlanCheck c = MatchPlanUnits(rows, Lookup(spawns));
    CHECK(At(c, 5, 1, 0).spawnId == 1);
    CHECK(At(c, 5, 1, 1).spawnId == 2 && At(c, 5, 1, 1).result == UnitResult::Matched);
}

static void PatrolNeverTakesAnotherMembersSpawn()
{
    // pack A (spawn 1 at the origin), pack B (spawn 2 at 0,40); the patrol row stands at 0,20: 20 yd from both, the nearest row is A (tie: lower idx), whose spawn is
    // 20 yd away (> 15): the patrol row stays missing, it does NOT take B's spawn.
    Spawns spawns{ { 7, { { 1, 0, 0, 0 }, { 2, 0, 40, 0 } } } };
    std::vector<StepUnit> rows{ Row(1, 1, 0, 7, 0, 0, 0, "pack"), Row(1, 1, 1, 7, 0, 40, 0, "pack"), Row(1, 1, 2, 7, 0, 20, 0, "patrol") };
    PlanCheck c = MatchPlanUnits(rows, Lookup(spawns));
    CHECK(At(c, 1, 1, 0).spawnId == 1 && At(c, 1, 1, 1).spawnId == 2);
    CHECK(At(c, 1, 1, 2).result == UnitResult::Missing && !At(c, 1, 1, 2).optional);
    CHECK(c.stepsShort == 1);
    // a patrol row nearest to B shares B's spawn
    rows[2] = Row(1, 1, 2, 7, 0, 33, 0, "patrol");
    c = MatchPlanUnits(rows, Lookup(spawns));
    CHECK(At(c, 1, 1, 2).spawnId == 2 && At(c, 1, 1, 2).result == UnitResult::Near);
}

static void PatrolOfAnotherStepOrEntryDoesNotShare()
{
    Spawns spawns{ { 7, { { 1, 0, 0, 0 } } }, { 8, { { 9, 0, 0, 0 } } } };
    // the pack row is in another step: no link
    std::vector<StepUnit> rows{ Row(1, 1, 0, 7, 0, 0, 0, "pack"), Row(1, 2, 0, 7, 0.2f, 0, 0, "patrol") };
    CHECK(At(MatchPlanUnits(rows, Lookup(spawns)), 1, 2, 0).result == UnitResult::Missing);
    // a pack row of another entry in the same step: no link
    rows = { Row(1, 1, 0, 8, 0, 0, 0, "pack"), Row(1, 1, 1, 7, 0.2f, 0, 0, "patrol") };
    PlanCheck c = MatchPlanUnits(rows, Lookup(spawns));
    CHECK(At(c, 1, 1, 1).spawnId == 1);                         // the patrol row finds the free spawn of ITS entry: that is pass 2, not sharing
    // a linked pack row that found nothing leaves the patrol row missing
    Spawns none{ { 7, { { 1, 500, 500, 0 } } } };
    rows = { Row(1, 1, 0, 7, 0, 0, 0, "pack"), Row(1, 1, 1, 7, 0.2f, 0, 0, "patrol") };
    c = MatchPlanUnits(rows, Lookup(none));
    CHECK(At(c, 1, 1, 0).result == UnitResult::Missing && At(c, 1, 1, 1).result == UnitResult::Missing);
}

static void OptionalAndTally()
{
    // entry 8996 has no spawn at all: optional, and its step is ok with the found member
    Spawns spawns{ { 1, { { 10, 0, 0, 0 } } } };
    std::vector<StepUnit> rows{ Row(6, 4, 0, 1, 0, 0, 0, "pack"), Row(6, 4, 1, 8996, 3, 0, 0, "pack") };
    PlanCheck c = MatchPlanUnits(rows, Lookup(spawns));
    CHECK(At(c, 6, 4, 1).optional && At(c, 6, 4, 1).result == UnitResult::Missing);
    CHECK(c.matchedCount == 1 && c.missingCount == 1 && c.optionalCount == 1 && c.stepsOk == 1 && c.stepsShort == 0);
    // the same thing run twice gives the same bytes (deterministic)
    PlanCheck d = MatchPlanUnits(rows, Lookup(spawns));
    CHECK(d.units.size() == c.units.size() && d.units[0].spawnId == c.units[0].spawnId);
    // a step with only missing non-optional members is short
    Spawns far{ { 1, { { 10, 100, 0, 0 } } } };
    c = MatchPlanUnits({ Row(0, 1, 0, 1, 0, 0, 0, "pack") }, Lookup(far));
    CHECK(c.missingCount == 1 && c.stepsShort == 1 && c.stepsOk == 0);
}

static void CacheHoldsAcrossReloadAndEviction()
{
    PlanCheckCache cache;
    auto make = [](char const* plan) { auto c = std::make_shared<PlanCheck>(); c->plan = plan; c->matchedCount = 7; return std::shared_ptr<PlanCheck const>(c); };
    PlanCheckKey first{ 389, 1, "p0" };
    cache.Put(first, make("p0"), 1000);
    std::shared_ptr<PlanCheck const> held = cache.Find(first);
    CHECK(held && held->matchedCount == 7);
    CHECK(cache.FindYoungerThan(first, 1500, 1000) != nullptr);         // a forced refresh inside the bound is answered from the cache
    CHECK(cache.FindYoungerThan(first, 2000, 1000) == nullptr);         // at the bound it recomputes
    for (int i = 1; i <= int(PlanCheckCache::kCap); ++i)                // the 17th key evicts the first
        cache.Put(PlanCheckKey{ 389, std::uint32_t(i), "p" + std::to_string(i) }, make("x"), 3000);
    CHECK(cache.Size() == PlanCheckCache::kCap && cache.Find(first) == nullptr);
    CHECK(held->plan == "p0" && held->matchedCount == 7);               // the holder's copy survived the eviction
    std::shared_ptr<PlanCheck const> second = cache.Find(PlanCheckKey{ 389, 2, "p2" });
    cache.Clear();                                                      // what a successful reload does
    CHECK(cache.Size() == 0 && second && second->plan == "x" && held->plan == "p0");
}

static void MakePlans(ai::DungeonPlanTable& plans)
{
    CHECK(plans.AddPlan("rfc", 389));
    ai::PlanStep move;
    move.leg = 0;
    move.seq = 1;
    move.kind = ai::PlanStepKind::Move;
    CHECK(plans.AddStep("rfc", move) == ai::PlanStepRefusal::None);
    plans.Seal();
}

static RawUnitRow Raw(char const* plan, unsigned leg, unsigned seq, unsigned idx, unsigned entry, float x, char const* role)
{
    RawUnitRow r;
    r.plan = plan;
    r.leg = leg;
    r.seq = seq;
    r.idx = idx;
    r.entry = entry;
    r.x = x;
    r.role = role;
    return r;
}

static void StageRefusesBadRows()
{
    ai::DungeonPlanTable plans;
    MakePlans(plans);
    StagedUnits staged;
    StageUnitRows({ Raw("rfc", 0, 1, 0, 11318, 1, "pack"), Raw("rfc", 0, 1, 1, 11318, 2, "patrol") }, plans, staged);
    CHECK(staged.load.read && staged.load.rows == 2 && staged.load.kept == 2 && staged.load.refused == 0 && staged.store["rfc"].size() == 2);
    // each kind of bad row is refused and counted: the reload turns ANY refused row into `swapped=no`, so a subset can never be installed
    std::vector<RawUnitRow> bad{ Raw("nope", 0, 1, 0, 1, 1, "pack"), Raw("rfc", 0, 9, 0, 1, 1, "pack"), Raw("rfc", 0, 1, 0, 1, 1, "wat"),
        Raw("rfc", 0, 1, 0, 0, 1, "pack"), Raw("rfc", 0, 1, 0, 1, std::nanf(""), "pack"), Raw("rfc", 300, 1, 0, 1, 1, "pack"), Raw("rfc", 0, 1, 0, 1, 1, "pack") };
    StageUnitRows(bad, plans, staged);
    CHECK(staged.load.rows == 7 && staged.load.refused == 6 && staged.load.kept == 1 && staged.refusals.size() == 6);
    // only the first ten refusals are named
    std::vector<RawUnitRow> many(30, Raw("nope", 0, 1, 0, 1, 1, "pack"));
    StageUnitRows(many, plans, staged);
    CHECK(staged.load.refused == 30 && staged.refusals.size() == kMaxLoggedRefusals);
}

static void SqlNames()
{
    std::string table;
    std::set<std::string> cols;
    CHECK(WorldQueryNamesCompat("SELECT `plan`, `leg` FROM `ai_dungeon_step_unit`", table, cols));
    CHECK(table == "ai_dungeon_step_unit" && cols == std::set<std::string>({ "plan", "leg" }));
    // the WHERE clause's columns are named too (a plan name in quotes is not)
    CHECK(WorldQueryNamesCompat("SELECT `source` FROM `ai_dungeon_plan` WHERE `plan` = 'a`b\\'`c' AND `map` = 3", table, cols));
    CHECK(table == "ai_dungeon_plan" && cols == std::set<std::string>({ "source", "plan", "map" }));
    CHECK(WorldQueryNamesCompat("SELECT COUNT(*) FROM `ai_dungeon_leg_need` WHERE `plan` = 'x'", table, cols));
    CHECK(table == "ai_dungeon_leg_need" && cols == std::set<std::string>({ "plan" }));
    // the fingerprint form: literals inside the select list are not columns
    CHECK(WorldQueryNamesCompat("SELECT COUNT(*), CAST(COALESCE(SUM(CRC32(CONCAT_WS('|', COALESCE(CONCAT('v', `plan`), 'n')))), 0) AS UNSIGNED) FROM `t`", table, cols));
    CHECK(table == "t" && cols == std::set<std::string>({ "plan" }));
    // not the single-table form: refused
    CHECK(!WorldQueryNamesCompat("DELETE FROM `t`", table, cols));
    CHECK(!WorldQueryNamesCompat("SELECT 1", table, cols));
    CHECK(!WorldQueryNamesCompat("SELECT `a` FROM `t", table, cols));
}

int main()
{
    MatcherOrder();
    PatrolTakesAFreeSpawn();
    PatrolNeverTakesAnotherMembersSpawn();
    PatrolOfAnotherStepOrEntryDoesNotShare();
    OptionalAndTally();
    CacheHoldsAcrossReloadAndEviction();
    StageRefusesBadRows();
    SqlNames();
    std::cout << "test_forever_plan_match: ok, " << g_checks << " checks passed\n";
    return 0;
}
