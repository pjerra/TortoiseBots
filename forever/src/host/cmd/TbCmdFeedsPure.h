// Commander port, package P4 (the map layers): the PURE half. FOREVER-COMMANDER-PORT-SPEC.md section 5 (the P4 row), section 7 (its tests) and the
// live row of section 8.
//
// Standard library and the Turtle telemetry header only (runtime/CommanderTelemetry.h): no core header, no database. Tested standalone by
// forever/tools/cmd_feeds_check.cpp.
//
// What it decides: which units of a grid sweep the card keeps (the nearest, capped), the letter a game-object type and state get, the state of a
// boss from its spawn read (and the instance's encounter state where it is known), whether a layer may be reported at all (a scan that did not run
// is null on the wire, never an empty list), and the rotation of the live position report over more bindings than one datagram cycle carries.
#pragma once

#include "runtime/CommanderTelemetry.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <vector>

namespace TortoiseBots
{
namespace cmd
{
namespace feeds
{
namespace CT = ::TortoiseBots::CommanderTelemetry;

// The feed cadence and scan shapes. One COMMANDERS report per snapshot cycle (2 s); the position report on its own clock.
constexpr uint32_t kFeedIntervalMs = 2000;
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

// One unit or object of a sweep, before the cut: its raw guid and its squared distance from the lead. The tie is broken by the guid, so two
// cycles over the same units keep the same rows (the dashboard's rows do not shuffle when two distances are equal).
struct Near
{
    uint64_t id = 0;
    float dist2 = 0.0f;
};

// Keeps the `cap` nearest, ordered by distance then guid. A cap of 0 keeps nothing.
inline void KeepNearest(std::vector<Near>& items, size_t cap)
{
    std::sort(items.begin(), items.end(), [](Near const& a, Near const& b)
    {
        if (a.dist2 != b.dist2)
            return a.dist2 < b.dist2;
        return a.id < b.id;
    });
    if (items.size() > cap)
        items.resize(cap);
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

// A layer is reported only when its scan ran: the lead stood in the world and the feed was on (the wire's rule: absent = null, [] = the scan
// found nothing). A scan that did not run must never be sent as an empty list.
inline bool Reported(bool feedOn, bool leadInWorld)
{
    return feedOn && leadInWorld;
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
