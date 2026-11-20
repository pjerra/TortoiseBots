// Commander port, package P4 (the map layers): the PURE half. FOREVER-COMMANDER-PORT-SPEC.md section 5 (the P4 row), section 7 (its tests) and the
// live row of section 8.
//
// Standard library and the Turtle telemetry header only (runtime/CommanderTelemetry.h): no core header, no database. Tested standalone by
// forever/tools/cmd_feeds_check.cpp.
//
// What it decides: how much of a grid sweep the card takes in (the cap, which stops the visit), the order the rows keep, the letter a game-object
// type and state get, the state of a boss from its spawn read (and the instance's encounter state where it is known), how a remaining cooldown is
// reported (whole seconds), and the rotation of the live position report over more bindings than one datagram cycle carries. Whether a layer is
// reported at all is the collector's control flow (no lead in the world: the scan does not run and the wire carries null, never an empty list).
#pragma once

#include "runtime/CommanderTelemetry.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <utility>
#include <vector>

namespace TortoiseBots
{
namespace cmd
{
namespace feeds
{
namespace CT = ::TortoiseBots::CommanderTelemetry;

// The scan shapes. The collector's own cycle (2 s) and the position clock are the callers' cadences.
constexpr float kDefaultMobRange = 60.0f;     // yards around the lead: the perception radius its feed used
constexpr float kDefaultObjectRange = 40.0f;  // yards around the lead: the objects a bot walking with it could use
constexpr uint32_t kDefaultPosMs = 100;       // COMMANDER_POS: every 100 ms while a binding stands (the spec's row)
constexpr uint32_t kMinPosMs = 50;
constexpr uint32_t kMaxPosMs = 2000;
constexpr size_t kMobCap = CT::kMaxMobsPerBinding;      // 24, per binding per cycle
constexpr size_t kObjectCap = CT::kMaxObjectsPerBinding; // 16

inline uint32_t ClampPosMs(int64_t ms)
{
    if (ms < int64_t(kMinPosMs))
        return kMinPosMs;
    if (ms > int64_t(kMaxPosMs))
        return kMaxPosMs;
    return uint32_t(ms);
}

inline float ClampRange(float range, float fallback)
{
    if (!(range > 0.0f) || range > 200.0f)
        return fallback;
    return range;
}

// One candidate of a sweep, before it is handed on: its raw guid and its squared distance from the lead.
struct Near
{
    uint64_t id = 0;
    float dist2 = 0.0f;
};

// The capped sweep every map layer collects through: the cap bounds the work, not only the rows. The grid visit offers candidates until the
// sweep is full; a refused offer (false) is where the visit stops, so a dense area costs `cap` offers and one sort of `cap` rows instead of a
// row, a hash entry and a sort of everything in range. The rows come out nearest first, the tie broken by the guid, so two cycles over the
// same units keep the same rows.
template <class Row>
class Sweep
{
public:
    explicit Sweep(size_t cap) : cap_(cap) { rows_.reserve(cap); }

    bool Full() const { return rows_.size() >= cap_; }

    // Keeps `row` while there is room. False once full: the caller stops its visit there.
    bool Offer(uint64_t id, float dist2, Row row)
    {
        if (Full())
            return false;
        rows_.push_back({ Near{ id, dist2 }, std::move(row) });
        return true;
    }

    size_t Size() const { return rows_.size(); }

    std::vector<std::pair<Near, Row>> Take()
    {
        std::sort(rows_.begin(), rows_.end(), [](std::pair<Near, Row> const& a, std::pair<Near, Row> const& b)
        {
            if (a.first.dist2 != b.first.dist2)
                return a.first.dist2 < b.first.dist2;
            return a.first.id < b.first.id;
        });
        return std::move(rows_);
    }

private:
    size_t cap_;
    std::vector<std::pair<Near, Row>> rows_;
};

// A remaining cooldown in whole seconds, rounded down: the wire's `cdLongestS` is seconds and SpellHistory counts milliseconds (a 90 s
// cooldown left is 90 000 ms -> 90); a remainder already elapsed is 0 (none).
inline uint32_t RemainingSeconds(int64_t remainingMs)
{
    return remainingMs > 0 ? uint32_t(remainingMs / 1000) : 0;
}

// The game-object kinds the dashboard draws (its letters: d door, c chest, b button or lever, g goober, r ritual, o other; a kind it does not
// draw is not listed at all). The core's GAMEOBJECT_TYPE_* values map to these in the collector.
enum class GoKind : uint8_t
{
    Other = 0,
    Door,
    Chest,
    Button,
    Goober,
    Ritual,
    Skipped // a kind the dashboard does not draw (mailboxes, fishing holes, traps ...): never listed
};

inline char ObjectTypeLetter(GoKind kind)
{
    switch (kind)
    {
        case GoKind::Door: return 'd';
        case GoKind::Chest: return 'c';
        case GoKind::Button: return 'b';
        case GoKind::Goober: return 'g';
        case GoKind::Ritual: return 'r';
        case GoKind::Other: return 'o';
        default: return 0;
    }
}

// The object's state letter: a active (open, pressed), u used (looted or spent), r ready (closed, untouched).
inline char ObjectState(bool goActive, bool lootReady)
{
    return goActive ? 'a' : lootReady ? 'r' : 'u';
}

// The state of one known boss entry in the lead's instance. `spawnState` is the strongest read over its spawn rows (CT::BossSpawnState: found
// alive 'a', a corpse or a respawn time still pending 'd', nothing loaded 'u'); `encounterDone` is the instance script's own state for the
// creature's DungeonEncounter, when the module can map it. A creature read alive is alive whatever the script says (a respawned boss); a
// killed-but-unloaded boss reads 'd' from either signal.
inline char BossState(char spawnState, bool encounterDone)
{
    if (spawnState == 'a')
        return 'a';
    if (spawnState == 'd')
        return 'd';
    return encounterDone ? 'd' : 'u';
}

// The window of bindings one position cycle reports: at most `cap` from a rotating start, so more bindings than that all get a turn. The caller
// stores `next` back as the following start.
struct PosWindow
{
    size_t first = 0;
    size_t count = 0;
    size_t next = 0;
};

inline PosWindow NextPosWindow(size_t bindings, size_t start, size_t cap = CT::kMaxPosBindings)
{
    PosWindow w;
    if (!bindings || !cap)
        return w;
    w.first = start % bindings;
    w.count = std::min(bindings, cap);
    if (w.first + w.count > bindings)
        w.count = bindings - w.first;
    w.next = (w.first + w.count) % bindings;
    return w;
}
}
}
}
