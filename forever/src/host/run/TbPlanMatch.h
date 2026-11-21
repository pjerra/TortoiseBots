// Slice 5 (host package H5, review fixes N5): the pure part of `.tb plan check` and of the step unit load: no core type, only the standard library and the
// plan table's own header, so tools/test_forever_plan_match.cpp compiles it on its own.
//
//  * StageUnitRows: validates the rows of `ai_dungeon_step_unit` against a plan table into TEMPORARIES (the reload installs them only with the plan tables).
//  * MatchPlanUnits: finds every unit row among the creature spawns of an entry (one-to-one; non-patrol rows first, then patrol rows).
//  * PlanCheckCache: the checks by value (shared_ptr) so a holder (S6 reads a check across ticks) keeps its result through a reload or an eviction.
#pragma once

#include "../../../../ai/playerbot/DungeonPlanPolicy.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace TortoiseBots
{
namespace run
{
// One row of `ai_dungeon_step_unit`: the member number `idx` of the step (plan, leg, seq); role = pack | boss | first (boss.first) | patrol.
struct StepUnit
{
    std::uint8_t leg = 0;
    std::uint16_t seq = 0;
    std::uint8_t idx = 0;
    std::uint32_t entry = 0;
    float x = 0.0f, y = 0.0f, z = 0.0f;
    std::string role;
};

// What the last load of the unit rows saw.
struct StepUnitLoad
{
    bool read = false;          // false: nothing was read (the plan table was not loaded, or the table is missing): every count below is then unknown, not zero
    bool tableMissing = false;  // the table or a column the module reads does not exist in the world database
    std::uint32_t rows = 0;     // rows the query handed over
    std::uint32_t kept = 0;
    std::uint32_t refused = 0;  // unknown plan, step missing from the plan, a bad role, a zero entry or a non-finite position
};

// A row as the query hands it over, before any check.
struct RawUnitRow
{
    std::string plan;
    std::uint32_t leg = 0, seq = 0, idx = 0, entry = 0;
    float x = 0.0f, y = 0.0f, z = 0.0f;
    std::string role;
};

// The rows of every plan, validated, and what the validation saw; nothing here is installed.
struct StagedUnits
{
    std::unordered_map<std::string, std::vector<StepUnit>> store;   // (leg, seq, idx) order
    StepUnitLoad load;
    std::vector<std::string> refusals;                              // the first ten, for the log and the reload's `why`
};

constexpr std::uint32_t kMaxLoggedRefusals = 10;

inline bool UnitRoleKnown(std::string const& role)
{
    return role == "pack" || role == "boss" || role == "first" || role == "patrol";
}

inline bool PlanHasStep(ai::DungeonPlan const& plan, std::uint8_t leg, std::uint16_t seq)
{
    for (ai::PlanStep const& step : plan.steps)
        if (step.leg == leg && step.seq == seq)
            return true;
    return false;
}

// Every row is checked against `plans` (a sealed table): its plan and step must exist, the role must be known, the entry non-zero, the position finite
// and the numbers in range of their columns. Refused rows are counted (`load.refused`) and the first ten named; the others are kept. `load.read` is set.
inline void StageUnitRows(std::vector<RawUnitRow> const& rows, ai::DungeonPlanTable const& plans, StagedUnits& out)
{
    out = StagedUnits();
    out.load.read = true;
    auto refuse = [&](RawUnitRow const& r, char const* why)
    {
        ++out.load.refused;
        if (out.refusals.size() < kMaxLoggedRefusals)
            out.refusals.push_back("ai_dungeon_step_unit: plan '" + r.plan + "' leg " + std::to_string(r.leg) + " seq " + std::to_string(r.seq) + " idx " +
                std::to_string(r.idx) + " refused (" + why + ")");
    };
    for (RawUnitRow const& r : rows)
    {
        ++out.load.rows;
        ai::DungeonPlan const* owner = plans.Find(r.plan);
        if (!owner)
            refuse(r, "no such plan");
        else if (r.leg > 255 || r.seq > 65535 || r.idx > 255)
            refuse(r, "a number out of range");
        else if (!PlanHasStep(*owner, std::uint8_t(r.leg), std::uint16_t(r.seq)))
            refuse(r, "the plan has no such step");
        else if (!UnitRoleKnown(r.role))
            refuse(r, "role is not pack, boss, first or patrol");
        else if (r.entry == 0)
            refuse(r, "entry 0");
        else if (!std::isfinite(r.x) || !std::isfinite(r.y) || !std::isfinite(r.z))
            refuse(r, "a position that is not finite");
        else
        {
            StepUnit unit;
            unit.leg = std::uint8_t(r.leg);
            unit.seq = std::uint16_t(r.seq);
            unit.idx = std::uint8_t(r.idx);
            unit.entry = r.entry;
            unit.x = r.x;
            unit.y = r.y;
            unit.z = r.z;
            unit.role = r.role;
            ++out.load.kept;
            out.store[r.plan].push_back(std::move(unit));
        }
    }
    for (auto& [plan, units] : out.store)
        std::sort(units.begin(), units.end(), [](StepUnit const& a, StepUnit const& b)
        {
            return std::tie(a.leg, a.seq, a.idx) < std::tie(b.leg, b.seq, b.idx);
        });
}

// ---- the match ----
enum class UnitResult : std::uint8_t { Matched, Near, Missing };

constexpr float kMatchYd = 5.0f;        // a spawn within this of the row is `matched`
constexpr float kNearYd = 15.0f;        // ... within this `near`; farther, or none: `missing`

struct UnitCheck
{
    std::uint8_t leg = 0;
    std::uint16_t seq = 0;
    std::uint8_t idx = 0;
    std::uint32_t entry = 0;
    std::string role;
    UnitResult result = UnitResult::Missing;
    float yd = 0.0f;            // the distance to the spawn (matched / near only)
    std::uint32_t spawnId = 0;  // the ForeverCore spawn id (matched / near only)
    bool optional = false;      // D4: the entry has no spawn at all on the plan's map, so a step does not need this member (a summoned pet, entry 8996)
};

struct PlanCheck
{
    std::string plan;
    std::uint32_t mapId = 0;
    std::uint32_t instanceId = 0;
    std::vector<UnitCheck> units;           // (leg, seq, idx) order
    std::uint32_t matchedCount = 0, nearCount = 0, missingCount = 0, optionalCount = 0;   // `optionalCount` counts the missing units that are optional (they are also in `missingCount`)
    std::uint32_t steps = 0, stepsOk = 0, stepsShort = 0;             // steps that have at least one unit row
};

struct SpawnRef
{
    std::uint32_t spawnId;
    float x, y, z;
};

// The spawns of an entry on the checked map and difficulty, or null/empty when it has none.
using SpawnLookup = std::function<std::vector<SpawnRef> const*(std::uint32_t entry)>;

inline float Dist3(float ax, float ay, float az, float bx, float by, float bz)
{
    float const dx = ax - bx, dy = ay - by, dz = az - bz;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// One-to-one, deterministic (every pair within kNearYd, nearest first; ties by row order = (leg, seq, idx), then spawn id), in three passes:
//  1. every row that is NOT a patrol row (pack, boss, first), so a patrol row can never take the spawn of a pack member;
//  2. the patrol rows, on the spawns still free (a patrolling unit that is a creature of its own);
//  3. a patrol row still without a spawn is the exporter's second row of the pack member it duplicates (the exporter writes both: the pack row at the
//     member's position, the patrol row at the first path point of the same creature): it takes the spawn that ONE row of its own step holds, the row of
//     the same entry nearest to it (ties: the lower idx), when that spawn stands within kNearYd of the patrol row. Never another member's spawn.
// A row is `optional` when its entry has no spawn at all (D4). A step is `ok` when every non-optional member is matched or near and at least one was found.
inline PlanCheck MatchPlanUnits(std::vector<StepUnit> const& rows, SpawnLookup const& spawnsOf)
{
    PlanCheck check;
    check.units.resize(rows.size());
    auto spawnsFor = [&](std::uint32_t entry) -> std::vector<SpawnRef> const*
    {
        std::vector<SpawnRef> const* list = spawnsOf ? spawnsOf(entry) : nullptr;
        return list && !list->empty() ? list : nullptr;
    };
    for (size_t i = 0; i < rows.size(); ++i)
    {
        UnitCheck& unit = check.units[i];
        unit.leg = rows[i].leg;
        unit.seq = rows[i].seq;
        unit.idx = rows[i].idx;
        unit.entry = rows[i].entry;
        unit.role = rows[i].role;
        unit.optional = !spawnsFor(rows[i].entry);
    }

    struct Pair { float yd; size_t row; std::uint32_t spawnId; };
    std::vector<bool> rowUsed(rows.size(), false);
    std::set<std::uint32_t> spawnUsed;
    auto pass = [&](bool patrolRows)
    {
        std::vector<Pair> pairs;
        for (size_t i = 0; i < rows.size(); ++i)
        {
            if ((rows[i].role == "patrol") != patrolRows || rowUsed[i])
                continue;
            std::vector<SpawnRef> const* list = spawnsFor(rows[i].entry);
            if (!list)
                continue;
            for (SpawnRef const& spawn : *list)
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
                return a.row < b.row;
            return a.spawnId < b.spawnId;
        });
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
    };
    pass(false);
    pass(true);

    for (size_t i = 0; i < rows.size(); ++i)
    {
        if (rowUsed[i] || rows[i].role != "patrol")
            continue;
        std::vector<SpawnRef> const* list = spawnsFor(rows[i].entry);
        if (!list)
            continue;
        // The member this row duplicates: the nearest non-patrol row of the same step and entry.
        size_t link = rows.size();
        float linkYd = 0.0f;
        for (size_t j = 0; j < rows.size(); ++j)
        {
            if (j == i || rows[j].role == "patrol" || rows[j].leg != rows[i].leg || rows[j].seq != rows[i].seq || rows[j].entry != rows[i].entry)
                continue;
            float const yd = Dist3(rows[i].x, rows[i].y, rows[i].z, rows[j].x, rows[j].y, rows[j].z);
            if (link == rows.size() || yd < linkYd)
            {
                link = j;
                linkYd = yd;
            }
        }
        if (link == rows.size() || check.units[link].result == UnitResult::Missing)
            continue;
        for (SpawnRef const& spawn : *list)
        {
            if (spawn.spawnId != check.units[link].spawnId)
                continue;
            float const yd = Dist3(rows[i].x, rows[i].y, rows[i].z, spawn.x, spawn.y, spawn.z);
            if (yd <= kNearYd)
            {
                UnitCheck& unit = check.units[i];
                unit.result = yd <= kMatchYd ? UnitResult::Matched : UnitResult::Near;
                unit.yd = yd;
                unit.spawnId = spawn.spawnId;
            }
            break;
        }
    }

    struct StepTally { bool found = false; bool shortMember = false; };
    std::map<std::pair<std::uint8_t, std::uint16_t>, StepTally> steps;
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
    return check;
}

// ---- the cache ----
struct PlanCheckKey
{
    std::uint32_t mapId = 0;
    std::uint32_t instanceId = 0;
    std::string plan;
    bool operator<(PlanCheckKey const& o) const { return std::tie(mapId, instanceId, plan) < std::tie(o.mapId, o.instanceId, o.plan); }
};

// A small FIFO of the newest kCap checks. Results are shared_ptr<const>: eviction, Clear() and a reload drop the cache's reference only, a holder keeps its
// own copy (S6 reads a check across ticks). `stampMs` is the caller's clock (milliseconds) of the moment the check was made.
class PlanCheckCache
{
public:
    static constexpr size_t kCap = 16;

    std::shared_ptr<PlanCheck const> Find(PlanCheckKey const& key) const
    {
        auto const it = _entries.find(key);
        return it == _entries.end() ? nullptr : it->second.check;
    }

    // The cached check when it is younger than `minAgeMs` (a forced refresh inside that time is answered from the cache), else null.
    std::shared_ptr<PlanCheck const> FindYoungerThan(PlanCheckKey const& key, std::int64_t nowMs, std::int64_t minAgeMs) const
    {
        auto const it = _entries.find(key);
        if (it == _entries.end() || nowMs - it->second.stampMs >= minAgeMs)
            return nullptr;
        return it->second.check;
    }

    void Put(PlanCheckKey const& key, std::shared_ptr<PlanCheck const> check, std::int64_t stampMs)
    {
        if (!_entries.count(key))
        {
            _order.push_back(key);
            while (_order.size() > kCap)
            {
                _entries.erase(_order.front());
                _order.pop_front();
            }
        }
        _entries[key] = Entry{ std::move(check), stampMs };
    }

    void Clear()
    {
        _entries.clear();
        _order.clear();
    }

    size_t Size() const { return _entries.size(); }

private:
    struct Entry
    {
        std::shared_ptr<PlanCheck const> check;
        std::int64_t stampMs = 0;
    };
    std::map<PlanCheckKey, Entry> _entries;
    std::deque<PlanCheckKey> _order;
};
}
}
