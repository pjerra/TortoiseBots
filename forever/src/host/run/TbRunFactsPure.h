// Slice 6, package F6 (facts): the PURE half of the run leader's facts reader (forever/docs/RUN-S6-F-NOTES.md).
//
// `DungeonRun::Facts` (runtime/DungeonRunPolicy.h:1561) is what the run leader's decision header sees of the world. The ForeverCore reader
// (TbRunFacts.cpp) is split in two so that everything that is a DECISION about the numbers can be run and checked without a server:
//   * this header: plain data in, `Facts` out. No core type, no legacy type, only the policy header and the standard library. It holds the config
//     rules (what the glue forces), the guid handle table, the party roll-up, the reading of a step's creatures (units, patrollers, residue,
//     boss corpse, the pack-cleared gate's nearest leftover), the path readings (approach hop, island, good path) and the boss search.
//   * TbRunFacts.cpp: the core's public API (Group, Map, PathGenerator, Creature ...) that fills the plain structs below, on the world thread.
// `forever/tools/test_run_facts.cpp` includes this header alone and runs it on a synthetic party (g++ -std=c++17, the build VM).
//
// Every fact the glue cannot read gets the value that makes the run do LESS (findings rule of slice 1): see RUN-S6-F-FIELDS.tsv, one line per field.
#pragma once

#include "runtime/DungeonRunPolicy.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace TortoiseBots
{
namespace run
{
namespace facts
{
using DungeonRun::Facts;
using DungeonRun::MemberFact;
using DungeonRun::UnitFact;

// ---- the bounded work of one read (the per-bot budget of the spec, section 5.3) ----
// Lists are capped so one read never grows with the world: a list past its cap is cut (nearest first where the reader sorts) and `Cut` counts it.
constexpr std::size_t kMaxMembers = 40;         // the group's slots (a dungeon party is 5)
constexpr std::size_t kMaxListed = 48;          // units in `units`, `patrollers`, `packResidue`, `engagedUnits`, `partyAttackers`
constexpr std::size_t kMaxHopUnits = 8;         // hop-pull candidates examined
constexpr std::size_t kMaxHopPaths = 4;         // path queries for them (the rest are not listed: a hop unit not read is not pulled first)
constexpr std::size_t kHandleCap = 4096;        // guid handles kept per run

// ---- configuration ----
// What the reader takes from the module's `aiplayerbot.conf` (the keys of PlayerbotAIConfig). Plain numbers, filled by TbRunFacts.cpp.
struct Config
{
    bool linkedDeath = false;
    bool fightForward = true;
    bool lightRest = true;
    bool walkBackAfterWonFight = false;
    bool retreatAt2Dead = false;
    float packClearMarginYd = 0.0f;
    std::uint32_t packClearRepulls = 0;
    std::uint32_t nearWipeStop = 0;
    std::uint32_t nearWipeStopMinutes = 0;
    bool nearWipeStopParty = false;
};

// Copies what the glue implements and FORCES the rest to its OFF value, whatever the conf says (a feature whose reader is not ported must not
// run on facts it never gets): the firing stand, the pull-aggro estimate, crowd control, totem focus, the aura-gated skip, the respawn nest,
// the straggler pull, the rez hold, loot free-for-all, the dry walk, the rescue and return teleports (GM powers: walk-back rescue, teleport return,
// the revive stand-still hold) and the raid.
inline void ApplyConfig(Facts& f, Config const& c)
{
    f.fightForward = c.fightForward;
    f.lightRest = c.lightRest;
    f.walkBackAfterWonFight = c.walkBackAfterWonFight;
    f.retreatAt2Dead = c.retreatAt2Dead;
    f.packClearMarginYd = c.packClearMarginYd;
    f.packClearRepulls = c.packClearRepulls;
    f.linkedDeath = c.linkedDeath;
    f.nearWipeStopCount = DungeonRun::NearWipeStopCount(false, c.nearWipeStopParty, c.nearWipeStop);
    f.nearWipeStopWindowMs = c.nearWipeStopMinutes * 60000u;
    // forced off
    f.joinerCeiling = 0;
    f.aggroWalkRing = false;
    f.pullStand = false;
    f.standClearance = false;
    f.standDiscipline = false;
    f.standPullPreCc = false;
    f.autoCc = false;
    f.autoCcRisky = false;
    f.totemFocus = false;
    f.auraGatedTargets = false;
    f.packFocusSparesTacticUnits = false;
    f.respawnNest = false;
    f.stragglerPull = false;
    f.dryWalk = false;
    f.walkBackRescue = false;
    f.teleportReturnYd = 0.0f;
    f.reviveHoldYd = 0.0f;
    f.lootFfaOn = false;
    f.rezHold = false;
    f.botKeysForPlayers = false;
}

// ---- guid handles ----
// P keys units by `std::uint64_t` (UnitFact::guid); a ForeverCore ObjectGuid has 128 bits and a hash cannot be turned back (WIDE-PASS-R1-NOTES.md:101).
// A handle is a counter handed out once per guid and kept for the run: stable across ticks, never reused, 0 = none. A handle the table dropped
// (older than `kKeepMs` with the table at its cap) no longer resolves: an unresolved handle is "gone", never a guess.
template <class Key>
class HandleTable
{
public:
    static constexpr std::uint32_t kKeepMs = 600000;

    std::uint64_t Handle(Key const& key, std::uint32_t nowMs)
    {
        auto const it = _byKey.find(key);
        if (it != _byKey.end())
        {
            it->second.seenMs = nowMs;
            return it->second.handle;
        }
        if (_byKey.size() >= kHandleCap)
            Trim(nowMs);
        std::uint64_t const handle = _next++;
        _byKey.emplace(key, Entry{handle, nowMs});
        _byHandle.emplace(handle, key);
        return handle;
    }
    bool Resolve(std::uint64_t handle, Key& out) const
    {
        auto const it = _byHandle.find(handle);
        if (it == _byHandle.end())
            return false;
        out = it->second;
        return true;
    }
    std::size_t Size() const { return _byKey.size(); }
    std::uint64_t Issued() const { return _next - 1; }

private:
    struct Entry
    {
        std::uint64_t handle;
        std::uint32_t seenMs;
    };
    // Drops what was not seen for kKeepMs; when that frees nothing the older half goes (a handle stays valid for ten minutes at least).
    void Trim(std::uint32_t nowMs)
    {
        for (auto it = _byKey.begin(); it != _byKey.end();)
        {
            if (nowMs - it->second.seenMs > kKeepMs)
            {
                _byHandle.erase(it->second.handle);
                it = _byKey.erase(it);
            }
            else
                ++it;
        }
        if (_byKey.size() < kHandleCap)
            return;
        std::vector<std::uint32_t> ages;
        for (auto const& kv : _byKey)
            ages.push_back(kv.second.seenMs);
        std::nth_element(ages.begin(), ages.begin() + ages.size() / 2, ages.end());
        std::uint32_t const cut = ages[ages.size() / 2];
        for (auto it = _byKey.begin(); it != _byKey.end();)
        {
            if (it->second.seenMs <= cut)
            {
                _byHandle.erase(it->second.handle);
                it = _byKey.erase(it);
            }
            else
                ++it;
        }
    }
    std::map<Key, Entry> _byKey;
    std::unordered_map<std::uint64_t, Key> _byHandle;
    std::uint64_t _next = 1;
};

// ---- the party ----
// One slot of the lead's group, as the world gave it. `present` = a player in the world on a map (not logging in or out, not being teleported):
// an absent member counts as dead and far (spec 5.3 "Roster, roles"), so P waits and never pulls.
struct PartyEntry
{
    std::string name;
    bool present = false;
    bool alive = false;
    bool isLead = false;
    bool inCombat = false;          // the bot or its living pet
    bool usesMana = false;
    bool healer = false;
    bool dps = false;               // not a tank, not a healer
    bool eating = false;            // a seated meal or drink
    bool canResurrect = false;      // priest, paladin or shaman
    std::uint32_t healthPct = 100;
    std::uint32_t manaPct = 100;
    MemberFact fact;                // name, yd, dz, x, y, z, inCombat, attackers, offPlanMap, ... (living members only)
};

// The party loop of ReadRunFacts (commands/Commander.cpp:11936-12075), without the raid and cc branches.
inline void RollUpParty(Facts& f, std::vector<PartyEntry> const& party)
{
    for (PartyEntry const& e : party)
    {
        ++f.total;
        if (!e.present || !e.alive)
        {
            ++f.dead;
            f.deadNames.push_back(e.name);
            if (!e.present)
                f.maxMemberYd = std::max(f.maxMemberYd, 1000.0f);
            continue;
        }
        ++f.alive;
        if (e.inCombat)
            f.anyInCombat = true;
        if (e.isLead)
            f.leadInCombat = e.inCombat;
        f.maxMemberYd = std::max(f.maxMemberYd, e.fact.yd);
        f.members.push_back(e.fact);
        if (e.eating)
            ++f.eating;
        if (e.canResurrect && !e.inCombat)
            f.rezzer = true;
        f.minHealthPct = std::min(f.minHealthPct, e.healthPct);
        if (e.usesMana)
        {
            if (e.healer)
                f.minHealerManaPct = std::min(f.minHealerManaPct, e.manaPct);
            else
                f.minManaPct = std::min(f.minManaPct, e.manaPct);
        }
        if (e.dps)
            ++f.dpsAlive;
    }
}

// ---- creatures of a step ----
// One creature the grid search found, with what the policy classifies it by. `handle` is the guid handle.
struct CreatureObs
{
    std::uint64_t handle = 0;
    std::uint32_t entry = 0;
    std::uint32_t formOf = 0;       // the entry a script changed it away from: ForeverCore has no such record, so always 0 (not known)
    bool alive = true;
    float x = 0.0f, y = 0.0f, z = 0.0f;
    float distLead = 0.0f;
    bool hasSpawn = false;          // a database spawn row (CreatureData)
    float spawnX = 0.0f, spawnY = 0.0f, spawnZ = 0.0f;
    bool ownWaypoint = false;       // its default movement type is WAYPOINT
    bool followsWaypointLeader = false;
    bool airborne = false;
    bool hostile = true;
    bool stealthed = false;
    bool evading = false;
    bool attackable = true;
    bool selectable = true;
    bool caster = false;
    bool normalRank = false;
    std::uint32_t level = 0;
    std::uint32_t creatureType = 0;
    float healthPct = -1.0f;        // -1: not read
};

// The floor under a flier: found + z. Asked only when AirborneFloorAsked says (one height query).
using GroundFn = std::function<bool(CreatureObs const&, float& groundZ)>;

inline float Dist3(float ax, float ay, float az, float bx, float by, float bz)
{
    return ai::PlanDistance(ax, ay, az, bx, by, bz);
}

// The boss step's own spawn row (nearest to the step's point) looked up by spawn id: loaded creature, or not loaded.
struct OwnBossObs
{
    bool known = false;             // the glue has a spawn id for it (a matched unit row of the plan check)
    bool loaded = false;            // the creature object exists on the map
    bool alive = false;
    bool respawnPending = false;    // not loaded, and the map holds a future respawn time for the spawn: he died and the corpse is gone
    std::uint32_t entry = 0;
    std::uint32_t formOf = 0;
    std::uint64_t handle = 0;
    float x = 0.0f, y = 0.0f, z = 0.0f;
    float rowX = 0.0f, rowY = 0.0f; // where the spawn row puts him
};

// The pull / boss case of ReadRunFacts (Commander.cpp:12211-12440): `creatures` are the grid search's finds for the step's entries (every entry
// once, dead and alive), `ground` answers the flier floor query. Writes units, patrollers, packResidue, packNearestLeft(+Read), hopUnits is the
// move case below, and for a boss step the corpse and boss fields. `bossOwnBossHpDone` is internal.
inline void ReadStepCreatures(Facts& f, DungeonRun::Run const& run, ai::PlanStep const& step, float toPoint,
    std::vector<CreatureObs> const& creatures, OwnBossObs const& own, GroundFn const& ground)
{
    using namespace DungeonRun;
    bool const boss = step.kind == ai::PlanStepKind::Boss;
    bool const pullStep = step.kind == ai::PlanStepKind::Pull;
    float bossCorpseFromSpot = 0.0f;
    if (boss && own.known)
    {
        if (own.loaded && !own.alive)
        {
            float const ownFromSpot2d = std::sqrt((own.rowX - step.x) * (own.rowX - step.x) + (own.rowY - step.y) * (own.rowY - step.y));
            if (OwnSpawnIsTheBoss(own.entry, own.formOf, step.entry, ownFromSpot2d))
            {
                f.bossOwnDead = true;
                f.bossOwnDeadEntry = own.entry;
                f.bossCorpseGuid = own.handle;
                f.bossCorpseX = own.x;
                f.bossCorpseY = own.y;
                f.bossCorpseZ = own.z;
                bossCorpseFromSpot = Dist3(own.x, own.y, own.z, step.x, step.y, step.z);
            }
        }
        else if (!own.loaded && own.respawnPending)
        {
            // He died and his body is gone (despawned): down, wherever it lay. No corpse place is known, so the corpse fields stay 0.
            f.bossOwnDead = true;
            f.bossOwnDeadEntry = step.entry;
        }
    }
    float packNearestYd = -1.0f;
    float const marginYd = f.packClearMarginYd;
    auto addPatroller = [&](CreatureObs const& c, float fromPoint, PullReadIn const& read)
    {
        bool const spawnOwned = marginYd > 0.0f && SpawnOwned(read, step.radius);
        if (!spawnOwned && run.plan && ai::OwnedByLaterStep(*run.plan, run.step, c.entry, c.x, c.y, c.z, f.team))
            return;
        for (UnitFact const& have : f.patrollers)
            if (have.guid == c.handle)
                return;
        if (f.patrollers.size() >= kMaxListed)
            return;
        UnitFact u;
        u.guid = c.handle;
        u.entry = c.entry;
        u.distLead = c.distLead;
        u.distSpot = fromPoint;
        u.x = c.x;
        u.y = c.y;
        u.z = c.z;
        u.hostile = c.hostile;
        u.evading = c.evading;
        u.spawnOwned = spawnOwned;
        u.spawnYd = read.spawnYd;
        f.patrollers.push_back(u);
    };
    for (CreatureObs const& c : creatures)
    {
        float const fromPoint = Dist3(c.x, c.y, c.z, step.x, step.y, step.z);
        bool const bossEntry = boss && (c.entry == step.entry || (c.formOf != 0 && c.formOf == step.entry));
        if (!c.alive)
        {
            if (bossEntry && fromPoint <= step.radius + kSeeYd)
                f.bossDeadNearSpot = true;
            if (bossEntry && c.distLead <= kSeeYd)
                f.bossDeadNearLead = true;
            if (!f.bossOwnDead && bossEntry && (fromPoint <= step.radius + kSeeYd || c.distLead <= kSeeYd) &&
                (!f.bossCorpseGuid || fromPoint < bossCorpseFromSpot))
            {
                f.bossCorpseGuid = c.handle;
                f.bossCorpseX = c.x;
                f.bossCorpseY = c.y;
                f.bossCorpseZ = c.z;
                bossCorpseFromSpot = fromPoint;
            }
            continue;
        }
        if (bossEntry && f.bossHpPct < 0 && c.healthPct >= 0.0f)
            f.bossHpPct = static_cast<int>(c.healthPct);
        PullReadIn read;
        bool residue = false;
        if (pullStep)
        {
            read.fromPoint = fromPoint;
            float const dx = c.x - step.x, dy = c.y - step.y;
            read.fromPoint2d = std::sqrt(dx * dx + dy * dy);
            read.dz = c.z - step.z;
            if (c.hasSpawn)
            {
                read.hasSpawn = true;
                read.spawnYd = Dist3(c.spawnX, c.spawnY, c.spawnZ, step.x, step.y, step.z);
            }
            read.ownWaypoint = c.ownWaypoint;
            read.followsWaypointLeader = c.followsWaypointLeader;
            read.airborne = c.airborne;
            if (AirborneFloorAsked(read, step.radius, marginYd) && ground)
            {
                float groundZ = 0.0f;
                bool const found = ground(c, groundZ);
                read.airborneSameFloor = SameFloorUnder(found, groundZ, step.z);
            }
            switch (ClassifyPullRead(read, step.radius, marginYd, kPatrolSeeYd))
            {
                case PullRead::Patroller:
                    addPatroller(c, fromPoint, read);
                    continue;
                case PullRead::Left:
                    if (packNearestYd < 0.0f || fromPoint < packNearestYd)
                    {
                        packNearestYd = fromPoint;
                        f.packNearestLeft = UnitFact();
                        f.packNearestLeft.guid = c.handle;
                        f.packNearestLeft.entry = c.entry;
                        f.packNearestLeft.distSpot = read.fromPoint2d;
                        f.packNearestLeft.x = c.x;
                        f.packNearestLeft.y = c.y;
                        f.packNearestLeft.z = c.z;
                        f.packNearestLeftRead = read;
                    }
                    continue;
                case PullRead::Unit:
                    break;
                case PullRead::Residue:
                    residue = true;
                    break;
            }
        }
        else if (fromPoint > step.radius)
            continue; // a boss step lists no patrollers
        bool known = false;
        for (UnitFact const& have : f.units)
            known = known || have.guid == c.handle;
        if (residue)
            for (UnitFact const& have : f.packResidue)
                known = known || have.guid == c.handle;
        if (known)
            continue;
        UnitFact u;
        u.guid = c.handle;
        u.entry = c.entry;
        u.formOf = c.formOf;
        u.distLead = c.distLead;
        u.z = c.z;
        u.hostile = c.hostile;
        u.stealthed = c.stealthed;
        u.evading = c.evading;
        u.creatureType = c.creatureType;
        u.normalRank = c.normalRank;
        u.caster = c.caster;
        u.level = c.level;
        u.attackable = c.attackable;
        if (residue)
        {
            u.x = c.x;
            u.y = c.y;
            u.distSpot = read.fromPoint2d;
            u.spawnOwned = SpawnOwned(read, step.radius);
            u.spawnYd = read.spawnYd;
            u.aloft = read.airborne && read.airborneSameFloor && std::fabs(read.dz) > kPackResidueZYd;
            if (f.packResidue.size() < kMaxListed)
                f.packResidue.push_back(u);
            continue;
        }
        if (f.units.size() < kMaxListed)
            f.units.push_back(u);
    }
    (void)toPoint;
}

// A hostile near the step's point on a move step: what is read about it before it is a hop unit.
struct HopObs
{
    std::uint64_t handle = 0;
    std::uint32_t entry = 0;
    float x = 0.0f, y = 0.0f, z = 0.0f;
    float distLead = 0.0f;
    bool inCombat = false;
    bool stealthed = false;
    bool evading = false;
    bool attackable = true;
    bool los = false;
    // The lead's path to it (one query each, at most kMaxHopPaths): valid false = not read, the unit is NOT listed (a pull before the move is
    // never ordered on a path nobody read).
    bool pathRead = false;
    bool noPath = false, incomplete = false, shortcut = false, notUsingPath = false;
    float endX = 0.0f, endY = 0.0f, endZ = 0.0f;
    float pathYd = 0.0f;
    float straightYd = 0.0f;
};

// The move case's hostile loop (Commander.cpp:12594-12648).
inline void ReadHopUnits(Facts& f, DungeonRun::Run const& run, float cx, float cy, float cz, std::vector<HopObs> const& around)
{
    using namespace DungeonRun;
    for (HopObs const& h : around)
    {
        if (Dist3(h.x, h.y, h.z, cx, cy, cz) > kHopPullYd || h.inCombat)
            continue;
        if (run.plan && ai::PlanNamesEventUnit(*run.plan, h.entry))
            continue;
        if (run.plan && ai::OwnedByLaterStep(*run.plan, OwnerFrom(run), h.entry, h.x, h.y, h.z, f.team))
            continue;
        if (h.stealthed || h.evading || !h.attackable || !h.los || !h.pathRead)
            continue;
        if (!HopUnitReachable(h.los, h.noPath, h.incomplete, h.shortcut, h.notUsingPath, h.endX, h.endY, h.endZ, h.x, h.y, h.z, h.pathYd,
                h.straightYd))
            continue;
        UnitFact u;
        u.guid = h.handle;
        u.entry = h.entry;
        u.distLead = h.distLead;
        u.stealthed = h.stealthed;
        u.evading = h.evading;
        u.attackable = h.attackable;
        f.hopUnits.push_back(u);
    }
}

// ---- the boss search (spec 5.3 "Boss"; Commander.cpp:10113-10170) ----
struct BossSpawnObs
{
    std::uint64_t handle = 0;       // guid handle of the loaded creature (0 when not loaded)
    bool isOwn = false;             // the step's own spawn
    bool loaded = false;
    bool gridLoaded = true;         // the spawn's grid is loaded
    bool alive = false;
    bool respawnPending = false;    // not loaded and a future respawn time: dead, body gone
    std::uint32_t entry = 0;
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

inline void ReadBossSearch(Facts& f, ai::PlanStep const& step, std::vector<BossSpawnObs> const& spawns)
{
    f.bossSearchRead = true;
    float corpseFromSpot = 0.0f;
    for (BossSpawnObs const& s : spawns)
    {
        if (!s.loaded && !s.respawnPending)
        {
            if (!s.gridLoaded)
                f.bossSearchUnloaded = true;
            continue;
        }
        if (!s.loaded)
        {
            // Dead and gone: counts as his own corpse when it is the step's own spawn; no place is known.
            if (s.isOwn)
                f.bossSearchOwnDead = true;
            continue;
        }
        if (s.alive)
        {
            if (!f.bossSearchAlive)
            {
                f.bossSearchAlive = true;
                f.bossSearchAliveEntry = s.entry;
                f.bossAliveX = s.x;
                f.bossAliveY = s.y;
                f.bossAliveZ = s.z;
            }
            continue;
        }
        float const fromSpot = Dist3(s.x, s.y, s.z, step.x, step.y, step.z);
        if (s.isOwn)
            f.bossSearchOwnDead = true;
        if (s.isOwn || (!f.bossSearchOwnDead && (!f.bossSearchDead || fromSpot < corpseFromSpot)))
        {
            f.bossSearchDead = true;
            f.bossSearchCorpseGuid = s.handle;
            f.bossSearchCorpseX = s.x;
            f.bossSearchCorpseY = s.y;
            f.bossSearchCorpseZ = s.z;
            corpseFromSpot = fromSpot;
        }
    }
}

// ---- paths ----
// A navmesh path the lead's own search gave (PathGenerator): `valid` false = no query could be made (no map, no lead in the world).
struct PathObs
{
    bool valid = false;
    bool noPath = false;            // PATHFIND_NOPATH
    bool incomplete = false;        // PATHFIND_INCOMPLETE
    bool shortcut = false;          // PATHFIND_SHORTCUT
    bool notUsingPath = false;      // PATHFIND_NOT_USING_PATH
    float endX = 0.0f, endY = 0.0f, endZ = 0.0f;
    std::vector<DungeonRun::PathPoint> points;
    float lengthYd = 0.0f;
};

// The island reading and the good-path reading of a path toward (x,y,z) (Commander.cpp NoteLeadWay). No path read: both false (P then hops or pauses,
// and never walks straight).
inline void NoteLeadWay(Facts& f, PathObs const& p, float leadX, float leadY, float leadZ, float x, float y, float z)
{
    if (!p.valid)
    {
        f.leadIsland = false;
        f.leadPathGood = false;
        return;
    }
    float const endFromLead = Dist3(leadX, leadY, leadZ, p.endX, p.endY, p.endZ);
    f.leadIsland = DungeonRun::LeadOnIsland(p.noPath, p.incomplete, endFromLead, Dist3(leadX, leadY, leadZ, x, y, z));
    f.leadPathGood = DungeonRun::LeadPathGood(p.noPath, p.incomplete, endFromLead);
}

// The next approach hop along a path (Commander.cpp ApproachHopOn).
inline bool ApproachHop(std::vector<DungeonRun::PathPoint> const& points, float x, float y, float z, float& ox, float& oy, float& oz)
{
    if (points.size() < 2)
        return false;
    float walked = 0.0f;
    for (std::size_t i = 1; i < points.size(); ++i)
    {
        auto const& from = points[i - 1];
        auto const& to = points[i];
        walked += Dist3(from.x, from.y, from.z, to.x, to.y, to.z);
        if (Dist3(to.x, to.y, to.z, x, y, z) <= DungeonRun::kApproachYd || walked >= DungeonRun::kApproachHopYd || i + 1 == points.size())
        {
            ox = to.x;
            oy = to.y;
            oz = to.z;
            return true;
        }
    }
    return false;
}

// ApproachOnPath's reading of one path (Commander.cpp:7266): the island/good-path facts when `way`, `approachIncomplete` when `incomplete`, and the hop.
// Returns whether a hop is known. `whole` gets the path's points (for the walk back's cross-pack test; not used on ForeverCore, the pack check needs
// the live creatures and is answered false = "not known" by the glue).
inline bool ReadApproach(Facts& f, PathObs const& p, float leadX, float leadY, float leadZ, float x, float y, float z, bool way, bool incomplete)
{
    if (way)
        NoteLeadWay(f, p, leadX, leadY, leadZ, x, y, z);
    if (incomplete)
    {
        // A path that could not be read is a path a walk may not take.
        f.approachIncomplete = !p.valid || !DungeonRun::PathWalkable(p.noPath, p.incomplete, leadX, leadY, leadZ, p.endX, p.endY, p.endZ, x, y, z);
    }
    if (!p.valid || p.noPath)
        return false;
    return ApproachHop(p.points, x, y, z, f.approachX, f.approachY, f.approachZ);
}

// The fight-forward and replay-shortcut reads (Commander.cpp DirectNavPath): a genuine navmesh route that is no detour.
inline bool DirectNav(PathObs const& p, float leadX, float leadY, float leadZ, float x, float y, float z)
{
    if (!p.valid)
        return false;
    if (!DungeonRun::NavPathReaches(p.noPath, p.incomplete, p.shortcut, p.notUsingPath, p.endX, p.endY, p.endZ, x, y, z))
        return false;
    return p.points.size() >= 2 && DungeonRun::PathFitsStraight(p.lengthYd, Dist3(leadX, leadY, leadZ, x, y, z));
}

// ---- the lead's ground (Commander.cpp:11796-11830) ----
struct LeadGround
{
    bool mmapLoaded = false;        // a navmesh exists for the lead's map (without it the floor cannot be judged: leadOffFloor stays false)
    float mapFloorZ = 0.0f;
    bool probeFound = false;
    float probeZ = 0.0f;
    bool surfaceFound = false;
    float surfaceZ = 0.0f;
    bool onTransport = false;
    bool inWater = false;
};

inline void ReadLeadGround(Facts& f, float leadZ, LeadGround const& g)
{
    bool const onSurface = DungeonRun::LeadOnSurface(leadZ, g.onTransport, g.surfaceFound, g.surfaceZ);
    f.leadFloorZ = DungeonRun::LeadFloorZ(g.mapFloorZ, g.probeFound, g.probeZ);
    f.leadOffFloor = g.mmapLoaded && DungeonRun::LeadOffFloor(leadZ, g.mapFloorZ, g.probeFound, g.probeZ, g.inWater, onSurface);
}

// ---- the lead and the guard reads ----
// Everything the glue reads of the lead bot itself in one plain struct (Commander.cpp:11796-11935 and ReadGuardFacts' Lead mode, :9661).
struct LeadObs
{
    bool alive = true;
    float x = 0.0f, y = 0.0f, z = 0.0f;
    std::string name;
    bool alliance = true;
    bool moving = false;
    bool casting = false;
    bool inWater = false;
    bool resting = false;           // a seated meal, or not standing
    bool moveFailed = false;
    std::string moveFailReason;
    LeadGround ground;
    std::uint64_t icons[8] = {};    // the group's raid marks as guid handles (0: empty)
    bool iconsRead = false;
    // the guard read (only in a fight, GuardRead::Lead)
    bool guardRead = false;
    bool isTank = false;
    bool staying = false;
    bool stayKnown = false;
    float stayX = 0.0f, stayY = 0.0f, stayZ = 0.0f;
    bool hit = false;               // a mob with the lead as its victim in melee reach
    bool hasAttacker = false;
    UnitFact attacker;              // the nearest such mob
    bool reaching = false;          // the lead's own pull is still reaching its target
    std::uint32_t reachAggro = 0;
};

inline void ReadLeadFacts(Facts& f, LeadObs const& l)
{
    f.leadAlive = l.alive;
    f.leadX = l.x;
    f.leadY = l.y;
    f.leadZ = l.z;
    f.carrier = l.name;
    f.team = l.alliance ? ai::kPlanFlagAlliance : ai::kPlanFlagHorde;
    f.leadMoving = l.moving;
    f.leadCasting = l.casting;
    f.leadInWater = l.inWater;
    f.leadResting = l.resting;
    f.leadMoveFailed = l.moveFailed;
    f.leadMoveFailReason = l.moveFailReason;
    ReadLeadGround(f, l.z, l.ground);
    for (int i = 0; i < 8; ++i)
        f.raidIcons[i] = l.icons[i];
    f.raidIconsRead = l.iconsRead;
    if (l.guardRead)
    {
        f.leadIsTank = l.isTank;
        f.leadStaying = l.staying;
        f.leadStayKnown = l.stayKnown;
        f.leadStayX = l.stayX;
        f.leadStayY = l.stayY;
        f.leadStayZ = l.stayZ;
        f.leadHit = l.hit;
        f.leadHasAttacker = l.hasAttacker;
        if (l.hasAttacker)
            f.leadAttacker = l.attacker;
        f.leadReaching = l.reaching;
        f.reachAggro = l.reachAggro;
    }
}

// ---- the fight reads, shared shape ----
// A unit hostile to the party that is in a fight with it (the engaged read) as the world gave it; the reader turns it into a UnitFact.
struct EngagedObs
{
    std::uint64_t handle = 0;
    std::uint32_t entry = 0;
    float x = 0.0f, y = 0.0f, z = 0.0f;
    float distLead = 0.0f;
    bool attackable = true;
    bool evading = false;
    std::uint32_t health = 0, maxHealth = 0;
    bool partyHits = false;         // a party bot or pet has it as its victim
    bool hitsParty = false;         // its victim is a party bot or pet
    bool caster = false;
    std::uint32_t creatureType = 0;
    bool normalRank = false;
    std::uint32_t level = 0;
};

inline UnitFact EngagedFact(EngagedObs const& e)
{
    UnitFact u;
    u.guid = e.handle;
    u.entry = e.entry;
    u.distLead = e.distLead;
    u.x = e.x;
    u.y = e.y;
    u.z = e.z;
    u.engaged = true;
    u.attackable = e.attackable;
    u.evading = e.evading;
    u.health = e.health;
    u.maxHealth = e.maxHealth;
    u.partyHits = e.partyHits;
    u.hitsParty = e.hitsParty;
    u.caster = e.caster;
    u.creatureType = e.creatureType;
    u.normalRank = e.normalRank;
    u.level = e.level;
    return u;
}

// The fight reads of one tick, as plain lists: the engaged units (one entry each), the fight's own units resolved by the glue and the plan's kill_first.
inline void ReadFightFacts(Facts& f, ai::DungeonPlan const* plan, std::vector<EngagedObs> const& engaged,
    std::vector<DungeonRun::OwnUnitFact> const* ownFight, std::size_t* cut)
{
    for (EngagedObs const& e : engaged)
    {
        if (f.engagedUnits.size() >= kMaxListed)
        {
            if (cut)
                ++*cut;
            break;
        }
        f.engagedUnits.push_back(EngagedFact(e));
    }
    f.engagedRead = true;
    f.staleCombat = f.engagedUnits.empty();
    if (plan && !plan->killFirst.empty())
        for (EngagedObs const& e : engaged)
            if (plan->KillFirstRank(e.entry) >= 0)
            {
                UnitFact u = EngagedFact(e);
                u.hostile = true;
                f.killFirst.push_back(u);
            }
    if (ownFight)
    {
        f.ownFightUnits = *ownFight;
        f.ownFightRead = true;
    }
}

// ---- objects, waits, tasks (slice 9, package F9) ----
// A wait_object step's object (Commander.cpp:12674-12700): the plan's words for its state are 0 ready (closed), 1 active (open), 2 active_alt; -1 is "not found".
// The core's GOState numbers are the same three (GO_STATE_ACTIVE 0, GO_STATE_READY 1, GO_STATE_DESTROYED 2, SharedDefines.h:3278), but the plan's 0 is the
// core's READY: the map is by NAME, as the Turtle glue does it.
inline int PlanObjectState(std::uint32_t coreState)
{
    switch (coreState)
    {
        case 1: return 0;           // GO_STATE_READY: a closed door
        case 0: return 1;           // GO_STATE_ACTIVE: an open door, a pulled lever
        default: return 2;          // GO_STATE_DESTROYED and the rest
    }
}

struct ObjectObs
{
    bool found = false;             // a spawned object of the step's entry within the search radius of the step's point
    std::uint32_t coreState = 1;
    // "Not found" is authoritative only when the search could have found it: the lead alive, in the world on the plan's map, the step's point within
    // ReactDistance of it (the by-entry search covers only that) and the point's grid loaded. An unreadable read never counts as gone.
    bool readable = false;
};

inline void ReadWaitObject(Facts& f, ObjectObs const& o)
{
    f.objectReadable = o.readable;
    if (o.found)
        f.objectState = PlanObjectState(o.coreState);
}

// A hostile creature near the lead, for wait_clear: where it stands and whether it is engaged with the party.
struct ClearObs
{
    float x = 0.0f, y = 0.0f, z = 0.0f;
    bool engaged = false;
};

// Commander.cpp:12651-12672: the living hostiles within the step's 2D radius of its point; the wait counts every one (WaitClearCount); the engaged flag and
// the z are for the timeout boundary only.
inline void ReadWaitClear(Facts& f, ai::PlanStep const& step, std::vector<ClearObs> const& hostiles)
{
    for (ClearObs const& h : hostiles)
    {
        float const dx = h.x - step.x, dy = h.y - step.y;
        if (std::sqrt(dx * dx + dy * dy) > step.radius)
            continue;
        if (f.clearUnits.size() >= kMaxListed)
            break;
        DungeonRun::HostileSpot spot;
        spot.x = h.x;
        spot.y = h.y;
        spot.z = h.z;
        spot.engaged = h.engaged;
        f.clearUnits.push_back(spot);
    }
}

// One party bot's bags, for a loot / open / use_item step (Commander.cpp RunTaskBags and the loot case, :9867, :12790-12836). `fact.held` lists the step's
// INPUT items (open: its item and `param`; use_item: the entry item and `param`) the bot carries; `hasStepItem` / `hasEntryItem` are the plain HasItemCount reads.
struct BagObs
{
    DungeonRun::BagFact fact;
    bool hasStepItem = false;
    bool hasEntryItem = false;
};

// What a loot step reads from the remembered corpse after a refusal (no scan).
struct CorpseObs
{
    bool checked = false;           // the corpse (by handle) is in the map
    bool hasItem = false;           // ... and still holds the step's item, not looted
    std::string turn;               // the party bot whose round-robin turn it is ("" none)
};

struct TaskView
{
    std::vector<BagObs> bags;                       // every party bot, dead ones too (an unavailable one carries `fact.away`)
    std::vector<std::uint32_t> inputs;              // the step's input items
    std::uint64_t taskCarrierKey = 0;               // the task module's newest carrier (0 none)
    std::vector<std::uint64_t> busyKeys;            // bots a task runs for
    int reply = 0;                                  // the task module's answer since the order: 0 none, 1 ACK, 2 ERR
    std::string replyText;
    CorpseObs corpse;
};

// The bot an open / use_item / loot goes to, and what is read about it (Commander.cpp:12777-12836). Returns true when the order the run has out must be
// RETIRED first (its bot left, died or is off the map: DungeonRun::RetireOrder, and the facts are not read from a bot the order did not go to).
inline bool ReadTaskFacts(Facts& f, DungeonRun::Run const& run, ai::PlanStep const& step, std::string const& leadName, TaskView const& v)
{
    using namespace DungeonRun;
    std::vector<BagFact> bags;
    for (BagObs const& b : v.bags)
        bags.push_back(b.fact);
    std::string name;
    bool const loot = step.kind == ai::PlanStepKind::Loot;
    if (loot)
    {
        // The lead bot, or the one a loot refusal moved the order to (Run::taskBot).
        name = leadName;
        if (!run.taskBot.empty())
            for (BagFact const& b : bags)
                if (b.name == run.taskBot)
                    name = b.name;
    }
    else
    {
        CarrierVerdict const verdict = OutstandingCarrierVerdict(run.orderSent, v.taskCarrierKey, bags, &name);
        if (verdict == CarrierVerdict::Retire)
            return true;
        if (verdict == CarrierVerdict::NoOrder)
        {
            std::string split;
            name = PickItemCarrier(bags, v.inputs, leadName, &split);
            f.itemsSplit = split;
        }
    }
    f.carrier = name;
    BagObs const* carrier = nullptr;
    for (BagObs const& b : v.bags)
        if (b.fact.name == name)
            carrier = &b;
    f.carrierHasItem = step.item && carrier && carrier->hasStepItem;
    f.carrierHasEntryItem = step.kind == ai::PlanStepKind::UseItem && carrier && carrier->hasEntryItem;
    if (carrier)
    {
        for (std::uint64_t key : v.busyKeys)
            f.carrierBusy = f.carrierBusy || key == carrier->fact.guid;
        // The answer belongs to the bot the newest task order went to.
        if (v.taskCarrierKey && v.taskCarrierKey == carrier->fact.guid)
        {
            f.taskReply = v.reply;
            f.taskText = v.replyText;
        }
    }
    if (loot && step.item)
    {
        // Any bot with the item counts for a loot step (a quest item is personal loot: its member took it, or the lead bot never can).
        for (BagObs const& b : v.bags)
            if (f.itemHolder.empty() && b.hasStepItem)
            {
                f.partyHasItem = true;
                f.itemHolder = b.fact.name;
            }
        // After a refusal, from the remembered corpse: whose round-robin turn it is and whether the item is still on it.
        auto const corpse = run.corpses.find(step.entry);
        if (!run.lootRefusal.empty() && corpse != run.corpses.end() && corpse->second.guid)
        {
            f.corpseChecked = v.corpse.checked;
            f.corpseHasItem = v.corpse.hasItem;
            f.lootTurn = v.corpse.turn;
        }
    }
    return false;
}

// ---- the step read: the dispatch of ReadRunFacts (Commander.cpp:12170-12440 and :7367-7440) as ONE function over a small world interface ----
// The core implements StepWorld with its grid, path and line-of-sight calls (TbRunFacts.cpp); the unit test implements it with a scripted world. Which
// reads happen on which tick, in which order and with which radius is therefore the same code in both.
struct LeadView
{
    float x = 0.0f, y = 0.0f, z = 0.0f;
    bool alive = true;
    std::uint32_t mapId = 0;
    std::string name;               // S9: the carrier of a loot step is the lead bot unless a refusal moved the order
};

// A hostile creature near the lead, for the fight-forward read: its entry, place and the distance its aggro reaches (Creature::GetAttackDistance).
struct ForwardObs
{
    std::uint32_t entry = 0;
    float x = 0.0f, y = 0.0f, z = 0.0f;
    float aggroReachYd = 0.0f;
};

class StepWorld
{
public:
    virtual ~StepWorld() = default;
    // One navmesh path from the lead to a point (a "valid=false" answer when no query can be made).
    virtual PathObs PathFromLead(float x, float y, float z) = 0;
    // Line of sight from the lead to a point.
    virtual bool LeadSees(float x, float y, float z) = 0;
    // The living and dead creatures of one entry within `range` of the lead (the grid search), each observed.
    virtual std::vector<CreatureObs> CreaturesOfEntry(std::uint32_t entry, float range) = 0;
    // The boss step's own spawn; false = not known (no unit row of the plan matched it).
    virtual bool OwnBoss(ai::PlanStep const& step, OwnBossObs& out) = 0;
    // The matched spawns of the boss entry on the map; false = none known.
    virtual bool BossSpawns(ai::PlanStep const& step, std::vector<BossSpawnObs>& out) = 0;
    // Hostile creatures within `range` of the lead that may be hop-pulled around (cx,cy,cz): the world applies the budget (line of sight, at most
    // kMaxHopPaths path queries) and leaves `pathRead` false on the rest.
    virtual void HopCandidates(float range, float cx, float cy, float cz, std::vector<HopObs>& out) = 0;
    // The floor under a flier at its x,y.
    virtual bool GroundUnder(CreatureObs const& c, float stepZ, float& groundZ) = 0;
    // Living hostile creatures within `range` of the lead (not pets), for the fight-forward read.
    virtual void HostilesNear(float range, std::vector<ForwardObs>& out) = 0;
    // S9 (F9): the wait_object step's object, the wait_clear step's hostiles and the party's bags for a task step. A world that cannot answer says so
    // (NotKnown) and the facts keep the "nothing there" value.
    virtual ObjectObs ObjectAt(ai::PlanStep const& /*step*/) { NotKnown("objectState"); return ObjectObs(); }
    virtual void ClearHostiles(float /*range*/, std::vector<ClearObs>& /*out*/) { NotKnown("clearUnits"); }
    virtual bool TaskBags(ai::PlanStep const& /*step*/, TaskView& /*out*/) { NotKnown("carrier"); return false; }
    // The task module's order for the retire (an order whose bot is gone): the world drops its task and its answer.
    virtual void RetireTask() {}
    // A fact the world could not answer this tick (the "do less" value stands).
    virtual void NotKnown(char const* /*field*/) {}
};

inline void ReadStepFacts(Facts& f, DungeonRun::Run const& run, LeadView const& lead, DungeonRun::MoveProbeCache& probe, StepWorld& w)
{
    using namespace DungeonRun;
    if (!run.plan)
        return;
    // The lead's way back to the run's anchor: one path, only on a tick after a gather asked (Commander.cpp:12079).
    if (run.anchorPathWanted && run.anchorKnown && lead.alive && lead.mapId == run.plan->map)
    {
        PathObs const path = w.PathFromLead(run.anchorX, run.anchorY, run.anchorZ);
        f.anchorPathRead = true;
        float const offset = Dist3(path.endX, path.endY, path.endZ, run.anchorX, run.anchorY, run.anchorZ);
        // RemoteCommander::kMaxMoveEndOffset is 3 yd; a path that could not be read is "not reachable".
        f.anchorReachable = path.valid && !path.noPath && (path.incomplete || offset <= 3.0f);
    }
    ai::PlanStep const* const step = FactStep(run);
    if (!step)
        return;
    float const toPoint = Dist3(lead.x, lead.y, lead.z, step->x, step->y, step->z);
    f.seen = toPoint <= kSeeYd;
    bool const wantsApproach = !f.seen && run.phase != Phase::Walk && !run.regroup && lead.mapId == run.plan->map;
    f.approachRead = f.seen || wantsApproach;
    bool const ownStep = run.step < run.plan->steps.size() && step == &run.plan->steps[run.step];

    auto approach = [&](float x, float y, float z, bool way, bool incomplete)
    {
        f.approachKnown = ReadApproach(f, w.PathFromLead(x, y, z), lead.x, lead.y, lead.z, x, y, z, way, incomplete);
    };

    switch (step->kind)
    {
        case ai::PlanStepKind::Pull:
        case ai::PlanStepKind::Boss:
        {
            bool const boss = step->kind == ai::PlanStepKind::Boss;
            if (boss && run.bossSearchWanted && ownStep)
            {
                std::vector<BossSpawnObs> list;
                if (!w.BossSpawns(*step, list))
                    w.NotKnown("bossSearchNoSpawn");
                ReadBossSearch(f, *step, list);
            }
            if (!f.seen)
            {
                if (wantsApproach)
                    approach(step->x, step->y, step->z, true, false);
                break;
            }
            std::vector<std::uint32_t> entries = step->entries;
            if (step->entry)
                entries.push_back(step->entry);
            float const range = std::min(toPoint + step->radius, 2.0f * kSeeYd);
            float const packRange = std::min(toPoint + step->radius + (f.packClearMarginYd > 0.0f ? kSpawnPatrolSeeYd : kPatrolSeeYd), 2.0f * kSeeYd);
            std::vector<CreatureObs> creatures;
            for (std::uint32_t entry : entries)
            {
                bool const bossRange = boss && entry == step->entry;
                float const reach = bossRange ? std::min(toPoint + step->radius + kSeeYd, 2.0f * kSeeYd) : !boss ? packRange : range;
                for (CreatureObs const& c : w.CreaturesOfEntry(entry, reach))
                {
                    bool dup = false;
                    for (CreatureObs const& have : creatures)
                        dup = dup || have.handle == c.handle;
                    if (!dup && creatures.size() < 4 * kMaxListed)
                        creatures.push_back(c);
                }
            }
            OwnBossObs own;
            if (boss && !w.OwnBoss(*step, own))
                w.NotKnown("bossOwnDead");
            GroundFn ground = [&](CreatureObs const& o, float& z) { return w.GroundUnder(o, step->z, z); };
            ReadStepCreatures(f, run, *step, toPoint, creatures, own, ground);
            break;
        }
        case ai::PlanStepKind::Move:
        case ai::PlanStepKind::MoveDirect:
        {
            float cx = step->x, cy = step->y, cz = step->z;
            if (!f.seen)
            {
                if (wantsApproach)
                {
                    bool const direct = run.walkBack && run.walkBackDirect;
                    approach(step->x, step->y, step->z, step->kind == ai::PlanStepKind::Move, direct);
                    if (direct && f.approachKnown)
                        w.NotKnown("directPathCrossesPack");    // the cross-pack test needs the live pack list: false = not known
                }
                if (!run.walkBack)
                    break;
                if (f.approachKnown)
                {
                    cx = f.approachX;
                    cy = f.approachY;
                    cz = f.approachZ;
                }
                else if (run.phase == Phase::Walk)
                {
                    cx = run.walkX;
                    cy = run.walkY;
                    cz = run.walkZ;
                }
                else
                    break;
            }
            else if (step->kind == ai::PlanStepKind::Move && run.phase != Phase::Walk && !run.regroup && lead.alive &&
                lead.mapId == run.plan->map && toPoint > kArriveYd &&
                (run.walkBack || ForwardMoveProbeWanted(run, true, f.seen, f.anyInCombat, toPoint)))
            {
                bool const forward = !run.walkBack;
                if (forward && MoveProbeHit(probe, run.step, lead.x, lead.y, step->x, step->y, step->z, run.runMs))
                {
                    f.leadIsland = probe.island;
                    f.leadPathGood = probe.good;
                }
                else
                {
                    NoteLeadWay(f, w.PathFromLead(step->x, step->y, step->z), lead.x, lead.y, lead.z, step->x, step->y, step->z);
                    if (forward)
                        StoreMoveProbe(probe, run.step, lead.x, lead.y, step->x, step->y, step->z, run.runMs, f.leadIsland, f.leadPathGood);
                }
            }
            float const toCentre = Dist3(lead.x, lead.y, lead.z, cx, cy, cz);
            std::vector<HopObs> hops;
            w.HopCandidates(std::min(toCentre + kHopPullYd, 2.0f * kSeeYd), cx, cy, cz, hops);
            if (hops.size() > kMaxHopUnits)
                hops.resize(kMaxHopUnits);
            ReadHopUnits(f, run, cx, cy, cz, hops);
            break;
        }
        case ai::PlanStepKind::WaitClear:
        {
            if (!f.seen)
            {
                if (wantsApproach)
                    approach(step->x, step->y, step->z, true, false);
                break;
            }
            std::vector<ClearObs> hostiles;
            w.ClearHostiles(std::min(toPoint + step->radius, 2.0f * kSeeYd), hostiles);
            ReadWaitClear(f, *step, hostiles);
            break;
        }
        case ai::PlanStepKind::WaitObject:
            ReadWaitObject(f, w.ObjectAt(*step));
            break;
        case ai::PlanStepKind::Loot:
        case ai::PlanStepKind::Open:
        case ai::PlanStepKind::UseItem:
        {
            TaskView view;
            if (!w.TaskBags(*step, view))
                break;
            // An order whose bot is gone is retired and the step is read again from a fresh pick (a policy that retires keeps no stale answer).
            if (ReadTaskFacts(f, run, *step, lead.name, view))
            {
                w.RetireTask();
                f.itemsSplit.clear();
                TaskView again;
                if (w.TaskBags(*step, again))
                    ReadTaskFacts(f, run, *step, lead.name, again);
            }
            break;
        }
        default:
            // hold, ritual, gossip, use_on: not read (no plan of the two dungeons has one); such a step reads as "nothing there" and the policy waits its
            // own timeout and pauses.
            w.NotKnown("stepKind");
            break;
    }
    // The way-reads the policy asked for last tick (fight forward, replay shortcut): one path each (Commander.cpp:7367).
    std::size_t const npos = static_cast<std::size_t>(-1);
    if (run.fightForwardWanted != npos && run.fightForwardWanted < run.plan->steps.size() && lead.alive)
    {
        ai::PlanStep const& pull = run.plan->steps[run.fightForwardWanted];
        PathObs const way = w.PathFromLead(pull.x, pull.y, pull.z);
        f.forwardRead = true;
        f.forwardFor = run.fightForwardWanted;
        f.forwardPathOk = DirectNav(way, lead.x, lead.y, lead.z, pull.x, pull.y, pull.z);
        f.forwardLos = w.LeadSees(pull.x, pull.y, pull.z + 1.0f);
        std::vector<ForwardObs> around;
        w.HostilesNear(kFightForwardYd + 40.0f, around);
        for (ForwardObs const& u : around)
        {
            bool own = false;
            for (std::uint32_t entry : pull.entries)
                own = own || (u.entry == entry && Dist3(u.x, u.y, u.z, pull.x, pull.y, pull.z) <= pull.radius);
            own = own || (pull.entry && u.entry == pull.entry);
            if (!own && way.points.size() >= 2)
                for (std::size_t i = 1; i < way.points.size() && !f.forwardHostileNear; ++i)
                    f.forwardHostileNear = SegmentDistance(way.points[i - 1], way.points[i], u.x, u.y, u.z) <= u.aggroReachYd + kForwardAggroMarginYd;
            for (std::size_t k = run.step; k < run.fightForwardWanted && !f.forwardHopAlive; ++k)
            {
                ai::PlanStep const& p = run.plan->steps[k];
                f.forwardHopAlive = !own && Dist3(u.x, u.y, u.z, p.x, p.y, p.z) <= kHopPullYd;
            }
        }
    }
    if (run.shortcutWanted != npos && run.shortcutWanted < run.plan->steps.size() && lead.alive)
    {
        ai::PlanStep const& p = run.plan->steps[run.shortcutWanted];
        f.shortcutRead = true;
        f.shortcutFor = run.shortcutWanted;
        f.shortcutOk = DirectNav(w.PathFromLead(p.x, p.y, p.z), lead.x, lead.y, lead.z, p.x, p.y, p.z);
    }
}

}
}
}
