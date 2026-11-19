// Slice 5, host package H5: see TbPlanUnits.h. Built with TORTOISEBOTS_SLICE1 only.
#include "host/run/TbPlanUnits.h"

#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1

#include "ai/playerbot/DungeonPlanMgr.h"

#include "Creature.h"
#include "CreatureData.h"
#include "DatabaseEnv.h"
#include "GameTime.h"
#include "Log.h"
#include "Map.h"
#include "ObjectMgr.h"

#include "compat/TbCompat_S5.h"      // the schema guard WorldQueryCompat and FieldTextCompat

#include <algorithm>
#include <chrono>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

// The schema guard's log line (compat/TbCompat_S5.h declares it; the guard cannot use the core's log macros under the legacy shim).
void WorldGuardLogCompat(std::string const& message)
{
    TC_LOG_ERROR("tortoisebots", "{}", message);
}

namespace TortoiseBots
{
namespace run
{
namespace
{
std::unordered_map<std::string, std::vector<StepUnit>>& UnitStore()
{
    static std::unordered_map<std::string, std::vector<StepUnit>> store;
    return store;
}

StepUnitLoad& LoadState()
{
    static StepUnitLoad state;
    return state;
}

PlanCheckCache& Cache()
{
    static PlanCheckCache cache;
    return cache;
}

long long NowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// The spawn index: (map id, entry) -> the creature spawns of that entry on that map, for every entry some unit row names. One pass over the core's creature data
// builds it (boot, and every successful reload): a check then looks up only its own plan's entries.
struct IndexedSpawn
{
    SpawnRef ref;
    std::vector<Difficulty> difficulties;     // empty = every difficulty
};
using SpawnIndex = std::unordered_map<uint64, std::vector<IndexedSpawn>>;

uint64 IndexKey(uint32 mapId, uint32 entry) { return (uint64(mapId) << 32) | entry; }

SpawnIndex& InstalledIndex()
{
    static SpawnIndex index;
    return index;
}

SpawnIndex BuildSpawnIndex(std::unordered_map<std::string, std::vector<StepUnit>> const& store)
{
    std::unordered_set<uint32> wanted;
    for (auto const& [plan, units] : store)
        for (StepUnit const& unit : units)
            wanted.insert(unit.entry);
    SpawnIndex index;
    uint64 spawns = 0;
    auto const start = std::chrono::steady_clock::now();
    if (!wanted.empty())
    {
        for (auto const& [spawnId, data] : sObjectMgr->GetAllCreatureData())
        {
            if (!wanted.count(data.id))
                continue;
            IndexedSpawn spawn;
            spawn.ref = { uint32(spawnId), data.spawnPoint.GetPositionX(), data.spawnPoint.GetPositionY(), data.spawnPoint.GetPositionZ() };
            spawn.difficulties.assign(data.spawnDifficulties.begin(), data.spawnDifficulties.end());
            index[IndexKey(data.mapId, data.id)].push_back(std::move(spawn));
            ++spawns;
        }
    }
    auto const ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    TC_LOG_INFO("server.loading", ">> Plan unit spawn index: {} spawns of {} entries on maps, {} ms (one pass over the creature data)", spawns, wanted.size(), ms);
    return index;
}

// Reads the table through the schema guard: false when the table or a column does not exist (`missing`), else the rows (an empty table gives none).
constexpr char const* kUnitColumns = "`plan`, `leg`, `seq`, `idx`, `entry`, `x`, `y`, `z`, `role`";
constexpr char const* kUnitSelect = "SELECT `plan`, `leg`, `seq`, `idx`, `entry`, `x`, `y`, `z`, `role` FROM `ai_dungeon_step_unit`";

bool ReadUnitRows(std::vector<RawUnitRow>& rows, bool& missing)
{
    QueryResult result = WorldQueryCompat(kUnitSelect, &missing);
    if (missing)
        return false;
    if (result)
    {
        do
        {
            Field* fields = result->Fetch();
            RawUnitRow row;
            row.plan = FieldTextCompat(fields[0]);
            row.leg = fields[1].GetUInt32();
            row.seq = fields[2].GetUInt32();
            row.idx = fields[3].GetUInt32();
            row.entry = fields[4].GetUInt32();
            row.x = fields[5].GetFloat();
            row.y = fields[6].GetFloat();
            row.z = fields[7].GetFloat();
            row.role = FieldTextCompat(fields[8]);
            rows.push_back(std::move(row));
        } while (result->NextRow());
    }
    return true;
}

struct Installed
{
    StagedUnits units;
    SpawnIndex index;
};

void Install(Installed&& staged)
{
    UnitStore() = std::move(staged.units.store);
    LoadState() = staged.units.load;
    InstalledIndex() = std::move(staged.index);
    ClearPlanChecks();
}
}

StepUnitLoad const& LastStepUnitLoad() { return LoadState(); }

std::vector<StepUnit> const& UnitsOfPlan(std::string const& plan)
{
    static std::vector<StepUnit> const none;
    auto const it = UnitStore().find(plan);
    return it == UnitStore().end() ? none : it->second;
}

void ClearPlanChecks()
{
    Cache().Clear();
}

std::vector<uint8> LegsOf(ai::DungeonPlan const& plan)
{
    std::set<uint8> legs;
    for (ai::PlanLeg const& leg : plan.legs)
        legs.insert(leg.leg);
    for (ai::PlanStep const& step : plan.steps)
        legs.insert(step.leg);
    return std::vector<uint8>(legs.begin(), legs.end());
}

h3::Rec PlanRecord(ai::DungeonPlan const& plan, int64 units, std::string const* source)
{
    uint32 moves = 0, pulls = 0, bosses = 0;
    for (ai::PlanStep const& step : plan.steps)
    {
        if (step.kind == ai::PlanStepKind::Move || step.kind == ai::PlanStepKind::MoveDirect)
            ++moves;
        else if (step.kind == ai::PlanStepKind::Pull)
            ++pulls;
        else if (step.kind == ai::PlanStepKind::Boss)
            ++bosses;
    }
    auto list = [](std::vector<uint32> const& entries)
    {
        std::vector<std::string> items;
        for (uint32 entry : entries)
            items.push_back(std::to_string(entry));
        return items;
    };
    h3::Rec rec("plan");
    rec.S("plan", plan.name).N("map", plan.map).N("legs", int64(LegsOf(plan).size())).N("steps", int64(plan.steps.size())).N("moves", moves).N("pulls", pulls).N("bosses", bosses);
    if (units >= 0)
        rec.N("units", units);
    rec.L("cc_priority", list(plan.ccPriority)).L("kill_first", list(plan.killFirst));
    if (source)
        rec.S("source", *source);
    return rec;
}

h3::Rec LegRecord(ai::DungeonPlan const& plan, uint8 leg)
{
    uint32 steps = 0;
    for (ai::PlanStep const& step : plan.steps)
        if (step.leg == leg)
            ++steps;
    h3::Rec rec("leg");
    rec.N("leg", leg).S("name", plan.LegName(leg)).N("steps", steps);
    return rec;
}

h3::Rec UnitRecord(UnitCheck const& unit, int alive)
{
    h3::Rec rec("unit");
    rec.N("leg", unit.leg).N("seq", unit.seq).N("idx", unit.idx).N("entry", unit.entry).S("role", unit.role);
    switch (unit.result)
    {
        case UnitResult::Matched: rec.S("result", "matched"); break;
        case UnitResult::Near: rec.S("result", "near"); break;
        case UnitResult::Missing: rec.S("result", "missing"); break;
    }
    if (unit.result != UnitResult::Missing)
    {
        rec.F("yd", unit.yd, 2).N("spawn", unit.spawnId);
        if (alive >= 0)
            rec.B("alive", alive != 0);
    }
    else if (unit.optional)
        rec.B("optional", true);
    return rec;
}

bool SpawnAliveOnMap(Map const* map, uint32 spawnId)
{
    auto const range = map->GetCreatureBySpawnIdStore().equal_range(spawnId);
    if (range.first != range.second)
    {
        for (auto it = range.first; it != range.second; ++it)
            if (it->second->IsAlive())
                return true;
        return false;
    }
    return !(map->GetCreatureRespawnTime(spawnId) > GameTime::GetGameTime());
}

std::shared_ptr<PlanCheck const> CheckPlanOnMap(std::string const& planName, Map const* map, bool refresh)
{
    PlanCheckKey const key{ map->GetId(), map->GetInstanceId(), planName };
    long long const now = NowMs();
    if (refresh)
    {
        if (auto young = Cache().FindYoungerThan(key, now, kPlanCheckMinRefreshMs))
            return young;
    }
    else if (auto cached = Cache().Find(key))
        return cached;

    // The spawns of every entry the plan's rows name, on this map and difficulty, from the index.
    Difficulty const difficulty = map->GetDifficultyID();
    SpawnIndex const& index = InstalledIndex();
    std::unordered_map<uint32, std::vector<SpawnRef>> filtered;
    SpawnLookup lookup = [&](uint32 entry) -> std::vector<SpawnRef> const*
    {
        auto const hit = filtered.find(entry);
        if (hit != filtered.end())
            return &hit->second;
        std::vector<SpawnRef>& list = filtered[entry];
        auto const it = index.find(IndexKey(map->GetId(), entry));
        if (it != index.end())
            for (IndexedSpawn const& spawn : it->second)
                if (spawn.difficulties.empty() || std::find(spawn.difficulties.begin(), spawn.difficulties.end(), difficulty) != spawn.difficulties.end())
                    list.push_back(spawn.ref);
        return &list;
    };
    auto check = std::make_shared<PlanCheck>(MatchPlanUnits(UnitsOfPlan(planName), lookup));
    check->plan = planName;
    check->mapId = map->GetId();
    check->instanceId = map->GetInstanceId();
    std::shared_ptr<PlanCheck const> result = check;
    Cache().Put(key, result, now);
    return result;
}

void FillUnitReloadHost(ai::PlanReloadHost& host)
{
    // Nothing to read when the table was never read and still does not exist (a world database without the S5 SQL): the plan tables reload alone.
    bool missing = false;
    if (!LoadState().read)
    {
        (void)WorldQueryCompat("SELECT COUNT(*) FROM `ai_dungeon_step_unit`", &missing);
        if (missing)
            return;
    }
    host.table = "ai_dungeon_step_unit";
    host.columns = kUnitColumns;
    auto staged = std::make_shared<Installed>();
    host.prepare = [staged](ai::DungeonPlanTable const& next, std::uint64_t& rowsRead) -> std::string
    {
        std::vector<RawUnitRow> rows;
        bool tableMissing = false;
        if (!ReadUnitRows(rows, tableMissing))
            return "ai_dungeon_step_unit is missing or lacks a column the module reads: the unit rows were not read";
        rowsRead = rows.size();
        StageUnitRows(rows, next, staged->units);
        if (staged->units.load.refused)
        {
            for (std::string const& line : staged->units.refusals)
                TC_LOG_ERROR("tortoisebots", "{}", line);
            std::string why = std::to_string(staged->units.load.refused) + " step unit rows refused";
            if (!staged->units.refusals.empty())
                why += "; " + staged->units.refusals.front();
            return why + " (the server log names the first ten)";
        }
        staged->index = BuildSpawnIndex(staged->units.store);
        return std::string();
    };
    host.install = [staged]()
    {
        uint32 const kept = staged->units.load.kept;
        size_t const plans = staged->units.store.size();
        Install(std::move(*staged));
        TC_LOG_INFO("server.loading", ">> Reloaded {} dungeon step units for {} plans (0 rows refused)", kept, plans);
    };
}
}
}

namespace ai
{
void LoadDungeonStepUnits()
{
    using namespace TortoiseBots::run;
    ai::DungeonPlanTable const& plans = ai::GetDungeonPlans();
    StepUnitLoad none;
    if (!plans.Sealed())
    {
        // Nothing can match an empty plan table: every row would be refused as "step missing". Say it once, keep the (empty) answer.
        Installed empty;
        Install(std::move(empty));
        TC_LOG_INFO("server.loading", ">> ai_dungeon_step_unit: not read, the dungeon plans are not loaded");
        return;
    }
    std::vector<RawUnitRow> rows;
    bool missing = false;
    if (!ReadUnitRows(rows, missing))
    {
        Installed empty;
        empty.units.load.tableMissing = true;       // read stays false: the units are not known, not zero
        Install(std::move(empty));
        TC_LOG_ERROR("tortoisebots", ">> ai_dungeon_step_unit: the table or a column the module reads is missing in the world database: no unit rows read (apply forever/sql/world); `.tb plan check` answers no-units");
        return;
    }
    Installed staged;
    StageUnitRows(rows, plans, staged.units);
    for (std::string const& line : staged.units.refusals)
        TC_LOG_ERROR("tortoisebots", "{}", line);
    staged.index = BuildSpawnIndex(staged.units.store);
    uint32 const kept = staged.units.load.kept, refused = staged.units.load.refused;
    size_t const planCount = staged.units.store.size();
    Install(std::move(staged));
    TC_LOG_INFO("server.loading", ">> Loaded {} dungeon step units for {} plans ({} rows refused)", kept, planCount, refused);
}
}

#endif
