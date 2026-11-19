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

#include <algorithm>
#include <cmath>
#include <deque>
#include <map>
#include <set>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

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

bool RoleKnown(std::string const& role)
{
    return role == "pack" || role == "boss" || role == "first" || role == "patrol";
}

bool StepInPlan(ai::DungeonPlan const& plan, uint8 leg, uint16 seq)
{
    for (ai::PlanStep const& step : plan.steps)
        if (step.leg == leg && step.seq == seq)
            return true;
    return false;
}

// The cache of plan checks: a small FIFO keyed by (map id, instance id, plan name).
struct CacheKey
{
    uint32 mapId;
    uint32 instanceId;
    std::string plan;
    bool operator<(CacheKey const& o) const { return std::tie(mapId, instanceId, plan) < std::tie(o.mapId, o.instanceId, o.plan); }
};
constexpr size_t kCacheCap = 16;

std::map<CacheKey, PlanCheck>& Cache()
{
    static std::map<CacheKey, PlanCheck> cache;
    return cache;
}

std::deque<CacheKey>& CacheOrder()
{
    static std::deque<CacheKey> order;
    return order;
}

struct SpawnRef
{
    uint32 spawnId;
    float x, y, z;
};

float Dist3(float ax, float ay, float az, float bx, float by, float bz)
{
    float const dx = ax - bx, dy = ay - by, dz = az - bz;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
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
    Cache().clear();
    CacheOrder().clear();
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

PlanCheck const& CheckPlanOnMap(std::string const& planName, Map const* map, bool refresh)
{
    CacheKey const key{ map->GetId(), map->GetInstanceId(), planName };
    if (!refresh)
        if (auto const it = Cache().find(key); it != Cache().end())
            return it->second;

    PlanCheck check;
    check.plan = planName;
    check.mapId = map->GetId();
    check.instanceId = map->GetInstanceId();
    std::vector<StepUnit> const& rows = UnitsOfPlan(planName);

    // One pass over the creature spawns of the map (the difficulty filter of `.tb instance info`): the spawns of every entry the unit rows name.
    std::unordered_set<uint32> wanted;
    for (StepUnit const& row : rows)
        wanted.insert(row.entry);
    std::unordered_map<uint32, std::vector<SpawnRef>> spawnsOfEntry;
    Difficulty const difficulty = map->GetDifficultyID();
    for (auto const& [spawnId, data] : sObjectMgr->GetAllCreatureData())
    {
        if (data.mapId != map->GetId() || !wanted.count(data.id))
            continue;
        if (!data.spawnDifficulties.empty() && std::find(data.spawnDifficulties.begin(), data.spawnDifficulties.end(), difficulty) == data.spawnDifficulties.end())
            continue;
        spawnsOfEntry[data.id].push_back({ uint32(spawnId), data.spawnPoint.GetPositionX(), data.spawnPoint.GetPositionY(), data.spawnPoint.GetPositionZ() });
    }

    // Every (row, spawn) pair within kNearYd, nearest first; a row and a spawn are each used once.
    struct Pair { float yd; size_t row; uint32 spawnId; };
    std::vector<Pair> pairs;
    for (size_t i = 0; i < rows.size(); ++i)
    {
        auto const it = spawnsOfEntry.find(rows[i].entry);
        if (it == spawnsOfEntry.end())
            continue;
        for (SpawnRef const& spawn : it->second)
        {
            float const yd = Dist3(rows[i].x, rows[i].y, rows[i].z, spawn.x, spawn.y, spawn.z);
            if (yd <= kNearYd)
                pairs.push_back({ yd, i, spawn.spawnId });
        }
    }
    std::sort(pairs.begin(), pairs.end(), [](Pair const& a, Pair const& b)
    {
        if (a.yd != b.yd)
            return a.yd < b.yd;
        if (a.row != b.row)
            return a.row < b.row;                  // the rows are in (leg, seq, idx) order
        return a.spawnId < b.spawnId;
    });
    std::vector<bool> rowUsed(rows.size(), false);
    std::set<uint32> spawnUsed;
    check.units.resize(rows.size());
    for (size_t i = 0; i < rows.size(); ++i)
    {
        UnitCheck& unit = check.units[i];
        unit.leg = rows[i].leg;
        unit.seq = rows[i].seq;
        unit.idx = rows[i].idx;
        unit.entry = rows[i].entry;
        unit.role = rows[i].role;
        unit.optional = !spawnsOfEntry.count(rows[i].entry);   // D4: no spawn of this entry anywhere on the map
    }
    for (Pair const& pair : pairs)
    {
        if (rowUsed[pair.row] || spawnUsed.count(pair.spawnId))
            continue;
        rowUsed[pair.row] = true;
        spawnUsed.insert(pair.spawnId);
        UnitCheck& unit = check.units[pair.row];
        unit.result = pair.yd <= kMatchYd ? UnitResult::Matched : UnitResult::Near;
        unit.yd = pair.yd;
        unit.spawnId = pair.spawnId;
    }

    // A patrol row (role=patrol) stands at the first point of the patrol's path and names the creature that patrols: in the Ragefire Chasm plan it is
    // the pack member of the same step itself (the exporter writes both, the same spawn). One-to-one matching leaves that second row with no spawn, which
    // is a false "missing": a patrol row may take the spawn a row of its own step already holds. It can only turn a missing row into a match when a
    // spawn of the entry stands within kNearYd of the row, so it never reports a creature that is not there.
    for (size_t i = 0; i < rows.size(); ++i)
    {
        if (rowUsed[i] || rows[i].role != "patrol")
            continue;
        auto const it = spawnsOfEntry.find(rows[i].entry);
        if (it == spawnsOfEntry.end())
            continue;
        UnitCheck const* best = nullptr;
        float bestYd = 0.f;
        for (size_t j = 0; j < rows.size(); ++j)
        {
            UnitCheck const& other = check.units[j];
            if (j == i || !rowUsed[j] || other.leg != rows[i].leg || other.seq != rows[i].seq || other.entry != rows[i].entry)
                continue;
            for (SpawnRef const& spawn : it->second)
            {
                if (spawn.spawnId != other.spawnId)
                    continue;
                float const yd = Dist3(rows[i].x, rows[i].y, rows[i].z, spawn.x, spawn.y, spawn.z);
                if (yd <= kNearYd && (!best || yd < bestYd || (yd == bestYd && other.spawnId < best->spawnId)))
                {
                    best = &other;
                    bestYd = yd;
                }
            }
        }
        if (best)
        {
            UnitCheck& unit = check.units[i];
            unit.result = bestYd <= kMatchYd ? UnitResult::Matched : UnitResult::Near;
            unit.yd = bestYd;
            unit.spawnId = best->spawnId;
        }
    }

    // Totals, and the verdict of each step (every non-optional member matched or near, and at least one member found).
    struct StepTally { bool found = false; bool shortMember = false; };
    std::map<std::pair<uint8, uint16>, StepTally> steps;
    for (UnitCheck const& unit : check.units)
    {
        StepTally& tally = steps[{ unit.leg, unit.seq }];
        switch (unit.result)
        {
            case UnitResult::Matched: ++check.matchedCount; tally.found = true; break;
            case UnitResult::Near: ++check.nearCount; tally.found = true; break;
            case UnitResult::Missing:
                ++check.missingCount;
                if (unit.optional)
                    ++check.optionalCount;
                else
                    tally.shortMember = true;
                break;
        }
    }
    for (auto const& [stepKey, tally] : steps)
    {
        ++check.steps;
        if (tally.found && !tally.shortMember)
            ++check.stepsOk;
        else
            ++check.stepsShort;
    }

    // Keep it (a refresh replaces the entry; the cache holds the newest kCacheCap).
    auto& cache = Cache();
    auto& order = CacheOrder();
    if (!cache.count(key))
    {
        order.push_back(key);
        while (order.size() > kCacheCap)
        {
            cache.erase(order.front());
            order.pop_front();
        }
    }
    return cache[key] = std::move(check);
}

}
}

namespace ai
{
void LoadDungeonStepUnits()
{
    using namespace TortoiseBots::run;
    StepUnitLoad load;
    std::unordered_map<std::string, std::vector<StepUnit>> store;
    ai::DungeonPlanTable const& plans = ai::GetDungeonPlans();
    if (!plans.Sealed())
    {
        // Nothing can match an empty plan table: every row would be refused as "step missing". Say it once, keep the (empty) answer.
        UnitStore().clear();
        LoadState() = load;
        TC_LOG_INFO("server.loading", ">> ai_dungeon_step_unit: not read, the dungeon plans are not loaded");
        return;
    }
    load.read = true;
    QueryResult result = WorldDatabase.Query(
        "SELECT `plan`, `leg`, `seq`, `idx`, `entry`, `x`, `y`, `z`, `role` FROM `ai_dungeon_step_unit`");
    uint32 logged = 0;
    auto refuse = [&](std::string const& plan, uint32 leg, uint32 seq, uint32 idx, char const* why)
    {
        ++load.refused;
        if (++logged <= 10)
            TC_LOG_ERROR("tortoisebots", "ai_dungeon_step_unit: plan '{}' leg {} seq {} idx {} refused ({})", plan, leg, seq, idx, why);
    };
    if (result)
    {
        do
        {
            Field* fields = result->Fetch();
            ++load.rows;
            std::string const plan = fields[0].GetString();
            StepUnit unit;
            unit.leg = fields[1].GetUInt8();
            unit.seq = fields[2].GetUInt16();
            unit.idx = fields[3].GetUInt8();
            unit.entry = fields[4].GetUInt32();
            unit.x = fields[5].GetFloat();
            unit.y = fields[6].GetFloat();
            unit.z = fields[7].GetFloat();
            unit.role = fields[8].GetString();
            ai::DungeonPlan const* owner = plans.Find(plan);
            if (!owner)
                refuse(plan, unit.leg, unit.seq, unit.idx, "no such plan");
            else if (!StepInPlan(*owner, unit.leg, unit.seq))
                refuse(plan, unit.leg, unit.seq, unit.idx, "the plan has no such step");
            else if (!RoleKnown(unit.role))
                refuse(plan, unit.leg, unit.seq, unit.idx, "role is not pack, boss, first or patrol");
            else if (unit.entry == 0)
                refuse(plan, unit.leg, unit.seq, unit.idx, "entry 0");
            else if (!std::isfinite(unit.x) || !std::isfinite(unit.y) || !std::isfinite(unit.z))
                refuse(plan, unit.leg, unit.seq, unit.idx, "a position that is not finite");
            else
            {
                ++load.kept;
                store[plan].push_back(std::move(unit));
            }
        } while (result->NextRow());
    }
    for (auto& [plan, units] : store)
        std::sort(units.begin(), units.end(), [](StepUnit const& a, StepUnit const& b)
        {
            return std::tie(a.leg, a.seq, a.idx) < std::tie(b.leg, b.seq, b.idx);
        });
    UnitStore() = std::move(store);
    LoadState() = load;
    ClearPlanChecks();
    TC_LOG_INFO("server.loading", ">> Loaded {} dungeon step units for {} plans ({} rows refused)", load.kept, UnitStore().size(), load.refused);
}
}

#endif
