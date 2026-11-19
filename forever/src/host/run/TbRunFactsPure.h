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
}
}
}
