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
#include <iterator>
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

// The hard bound on one hostile visit: candidates that passed the filters (in the radius, hostile or attackable, not a critter) the visit may take
// before it stops, however dense the area. The rows kept are the nearest of the ones met, plus every boss (see NearestSelector).
constexpr size_t kMobVisitBound = 512;

// The id a unit, object or player carries on the wire (a decimal string, 64 bits): the guid's type (6 bits) over its counter (40 bits). The counter
// alone is not unique across kinds: a Creature, a Pet or Vehicle and a Player can hold the same counter, and the dashboard keys its rows (and the
// overlay a boss row takes) by this id. The 128-bit guid's other fields (realm, map, entry) are not needed: the counter is unique within a type.
inline uint64_t WireId(uint32_t guidType, uint64_t counter)
{
    return (uint64_t(guidType & 0x3F) << 40) | (counter & 0xFFFFFFFFFFULL);
}

// The hostiles of one binding: the nearest `cap` of what a bounded visit met, and every boss. The visit offers candidates until the bound is
// reached (`Exhausted`); the others are kept in a bounded max-heap (the farthest kept row sits on top and is replaced by a nearer one: no
// allocation per candidate, no sort of everything), the bosses in their own heap of `cap`. A boss the visit did not reach (beyond the bound, or in a
// grid the visit stopped before) is added afterwards by `AddBoss`, from the boss cache. `Take` returns at most `cap` rows nearest first: all the
// bosses first claimed, the rest of the room filled with the nearest others (the wire cuts a binding's mob list at the same cap, so a boss
// that was kept is never the row cut).
template <class Row>
class NearestSelector
{
public:
    NearestSelector(size_t cap, size_t visitBound) : cap_(cap), bound_(visitBound)
    {
        others_.reserve(cap);
        bosses_.reserve(cap);
    }

    bool Exhausted() const { return visited_ >= bound_; }
    size_t Visited() const { return visited_; }

    // Takes a candidate of the visit. False once the bound was reached: the visit stops there.
    bool Offer(uint64_t id, float dist2, Row row, bool boss)
    {
        if (Exhausted())
            return false;
        ++visited_;
        Keep(boss ? bosses_ : others_, Entry{ Near{ id, dist2 }, std::move(row) });
        return true;
    }

    // A boss the visit did not meet. False when it is already held (no row twice).
    bool AddBoss(uint64_t id, float dist2, Row row)
    {
        for (Entry const& e : bosses_)
            if (e.near.id == id)
                return false;
        Keep(bosses_, Entry{ Near{ id, dist2 }, std::move(row) });
        return true;
    }

    std::vector<std::pair<Near, Row>> Take()
    {
        std::sort(bosses_.begin(), bosses_.end(), Less);
        std::sort(others_.begin(), others_.end(), Less);
        size_t const room = cap_ > bosses_.size() ? cap_ - bosses_.size() : 0;
        if (others_.size() > room)
            others_.erase(others_.begin() + room, others_.end());
        std::vector<Entry> all;
        all.reserve(bosses_.size() + others_.size());
        for (Entry& e : bosses_)
            all.push_back(std::move(e));
        for (Entry& e : others_)
            all.push_back(std::move(e));
        std::sort(all.begin(), all.end(), Less);
        std::vector<std::pair<Near, Row>> out;
        out.reserve(all.size());
        for (Entry& e : all)
            out.emplace_back(e.near, std::move(e.row));
        return out;
    }

private:
    struct Entry
    {
        Near near;
        Row row;
    };

    static bool Less(Entry const& a, Entry const& b)
    {
        if (a.near.dist2 != b.near.dist2)
            return a.near.dist2 < b.near.dist2;
        return a.near.id < b.near.id;
    }

    // Keeps the nearest cap_ entries in a max-heap: the farthest kept entry is the front.
    void Keep(std::vector<Entry>& heap, Entry entry)
    {
        if (!cap_)
            return;
        if (heap.size() < cap_)
        {
            heap.push_back(std::move(entry));
            std::push_heap(heap.begin(), heap.end(), Less);
            return;
        }
        if (!Less(entry, heap.front()))
            return;
        std::pop_heap(heap.begin(), heap.end(), Less);
        heap.back() = std::move(entry);
        std::push_heap(heap.begin(), heap.end(), Less);
    }

    size_t cap_;
    size_t bound_;
    size_t visited_ = 0;
    std::vector<Entry> bosses_;
    std::vector<Entry> others_;
};

// The standing bindings' guids kept in a state map: every other entry is dropped. An empty `standing` empties the map (the last binding was
// released: nothing is watched and nothing is sent).
template <class Map, class Set>
void KeepStanding(Map& state, Set const& standing)
{
    for (auto it = state.begin(); it != state.end();)
        it = standing.count(it->first) ? std::next(it) : state.erase(it);
}

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
