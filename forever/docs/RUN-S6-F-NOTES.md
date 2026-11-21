# Slice S6, package F6 (facts): the run leader's view of the world on ForeverCore

Branch `run/f6` (worktree `~/wt-run-f6`), build slot 3 (`FOREVER_SLOT=3`), on slice 4's host half + S5's host half (H5) + S5's loader (P5), merged; slice 4's final fixes (`tbf/port/forever` 55dff9543) are merged in. Core read only at the pin 6517af05 (zero core changes). No server was started and no SQL was applied: the proof is the two builds, the census, the generator checks and a standalone test that runs the Turtle decision header over the whole Ragefire Chasm plan on a scripted world.
Spec: `~/TortoiseBots-audit/FOREVER-RUN-S5S6-SPEC.md` section 5 (facts responsibilities) and F6's row of the rounds table; the measured field list is `RUN-S5-FACTS-RFC.md`; the host rules are `SLICE4-COMMON.md` C1-C5. Not in this package: O6 (orders, verbs), C6 (tick, `.tb run`, events).

## 1. Files

| File | Lines | What |
|---|---|---|
| `forever/src/host/run/TbRunFacts.h` | 75 | the interface C6 calls: `ReadFacts`, `HandleFor`, `ResolveHandle`, `ForgetRun`, `StepMatchedMembers`, `Totals`, `FactsReport` (light: no policy header, no core header beyond Define/ObjectGuid) |
| `forever/src/host/run/TbRunFactsPure.h` | 1,014 | the PURE half: plain data in, `DungeonRun::Facts` out. No core or legacy type. Config rules, guid handle table, party roll-up, the step read (pull/boss/move/hop/forward/shortcut/anchor/boss search) as one function over a small `StepWorld` interface, lead facts, fight facts |
| `forever/src/host/run/TbRunFacts.cpp` | 978 | the core half (TORTOISEBOTS_SLICE1 only; an empty object in the option-OFF build): reads the world through the core's public API into the plain structs, implements `StepWorld` |
| `forever/tools/test_run_facts.cpp` | 786 | the standalone test of the pure half (below) |
| `forever/tools/facts_f6_table.py` | 76 | generates the coverage table |
| `forever/docs/RUN-S6-F-FIELDS.tsv` | 247 | the coverage table, one line per `Facts` field (246) |

Nothing outside these files changed: no edit in `ai/`, `runtime/`, `forever/src/compat`, `forever/src/slice`, `TbScripts.cpp` or `CMakeLists.txt` (the root CMake globs `forever/src/**`, so the new files build with no registration line; F6 needs none). No `.tb` command was added: the spec lists none for F6 (a `.tb facts selftest` would be a C6 file), so the console proof is replaced by the standalone test.

## 2. Design

`ReadFacts(rec, nowMs, facts, report)` does, on the world thread, in this order (cost in section 5):
1. `facts = Facts()`; config copied or forced (`ApplyConfig`).
2. the lead = `ObjectAccessor::FindPlayer(rec.leader)`; not a player in the world, or no plan: return false with `leadAlive=false` (C6 pauses the run: "Lead off map").
3. the party (`ReadParty`): the lead's group slots (`Group::GetMemberSlots`); a host bot becomes a `PartyEntry` (+ `MemberFact`), a real player a `PlayerFact` (C6 refuses such a run at the start; if one joins later the run waits for him); `RollUpParty` does the counts, minima, `eating`, `rezzer`, `deadNames`.
4. the lead (`ReadLead`), then, only in a fight, the engaged units and members' engaged lists (`ReadEngaged`) and the guard facts (`ReadGuard`, `GuardRead::Lead` only: a bot-led party has no raid).
5. the step (`ReadStepFacts` over `CoreStepWorld`): the policy's current step (`FactStep`) decides what is read.

Why two halves: every DECISION about the numbers (which creature is a unit, a patroller, residue or "left"; the boss corpse rules; what a path that cannot be read means; what the config may switch on) is in the pure half and runs in the standalone test; the core half only asks the world. `StepWorld` is the seam: the core implements it with grid, path and sight calls, the test with a scripted world, so which read happens on which tick is the same code in both.

**Guid handles.** P keys units by `uint64`; an `ObjectGuid` has 128 bits (WIDE-PASS-R1-NOTES.md:101). A per-run table hands out a counter per guid (stable, never reused, 0 = none; capped at 4,096, entries unseen for 10 min dropped first); `ResolveHandle(leader, handle, guid)` turns a handle P wrote into an order (`attack <guid> <bot>`, `focus`, `pullback`) back into a guid, and a dropped handle resolves to nothing ("gone", never a guess). No `Player`/`Unit`/`Map` pointer is kept across ticks: the per-run state is the handle table and the move-probe cache; even `Facts::pointLiquid` holds map id + instance id and looks the map up when called (an unreadable terrain refuses the point).

**Do-less rule (findings of slice 1).** A fact the core cannot answer takes the value that makes the run do less: an unreadable path gives `leadPathGood=false`, `approachKnown=false`, `anchorReachable=false`, and for a walk back's path `approachIncomplete=true` (P hops or pauses, never walks straight); an absent member counts dead and 1,000 yd away (P waits, never pulls); a hop unit whose path was not read is not listed (no pull before the move on an unread path); a step kind the glue does not read reads as "nothing there" (the policy's own timeout pauses it); `offMapReturnable=false`.

**Config.** Copied from `PlayerbotAIConfig` (the `DungeonRun*` keys): `fightForward`, `lightRest`, `walkBackAfterWonFight`, `retreatAt2Dead`, `packClearMarginYd`, `packClearRepulls`, `linkedDeath`, the near-wipe stop. FORCED OFF whatever the conf says, because the reader of the feature is not ported or it is a GM power: the pull-aggro estimate (`joinerCeiling`, `aggroWalkRing`), the firing stand (`pullStand`, `standClearance`, `standDiscipline`, `standPullPreCc`), crowd control (`autoCc`, `autoCcRisky`), `totemFocus`, `auraGatedTargets`, `packFocusSparesTacticUnits`, `respawnNest`, `stragglerPull`, `dryWalk`, `rezHold`, `lootFfaOn`, `botKeysForPlayers`, and the teleports (`walkBackRescue=false`, `teleportReturnYd=0`, `reviveHoldYd=0`). The conf file therefore needs no special values for F6 (spec 5.2 "Config forced on F" is done here, in code, so a wrong conf cannot switch a rescue teleport on).

## 3. Where each fact group comes from (core at the pin 6517af05)

| Group | Core API (file:line) |
|---|---|
| roster, leader | `Group::GetMemberSlots` Groups/Group.h:341, `GetLeaderGUID` :311, `ObjectAccessor::FindPlayer` Globals/ObjectAccessor.h:82; presence = `Player::IsInWorld`, `IsBeingTeleported` Entities/Player/Player.h:2522, `WorldObject::FindMap` Entities/Object/Object.h:412 (`GetMap()` :411 ASSERTs on a player with no map, so it is never called before the presence test) |
| positions, distance, height | `WorldObject::GetDistance`, position getters; `Map::GetHeight` Maps/Map.h:330 through `GetHeightCompat`; navmesh presence `ai::WorldPosition::isMmapLoaded` |
| health, mana, meal, rez | `Unit::GetHealth/GetMaxHealth` Unit.h:813, `GetPower`; `PlayerbotAI::HasSeatedMeal` (PlayerbotAI.h:1060); class priest/paladin/shaman |
| combat, attackers, victims | `Unit::getAttackers` Unit.h:741, `GetVictim` :743, `IsInCombat` :1075, `Pet`s of the bots, `CanReachWithMeleeAutoAttack` |
| attackable, friendly, stealth | `WorldObject::IsValidAttackTarget` Object.h:486 of the judge (the lead, else a living uncharmed bot), `sServerFacade.IsFriendlyTo` (the engine's own), `HasAuraType` Unit.h:1394 |
| lead state | `Unit::isMoving` Unit.h:1829, `IsNonMeleeSpellCast` :1490, `IsInWater`/`IsUnderWater` :1114, `GetStandState` :917; the engine's `last movement` value for the failed move; `'stay'` strategy and `position` value for the stay point |
| raid marks | `Group::GetTargetIcon` Group.h:362 |
| paths | `PathGenerator` Movement/PathGenerator.h:86 `CalculatePath`, `GetPathType` :102, `GetActualEndPosition` :97, `GetPath` :99, through the TbCompat `PathFinder` helpers; type bits NOPATH 8 / INCOMPLETE 4 / SHORTCUT 2 / NOT_USING_PATH 0x10 are the Turtle ones |
| sight | `WorldObject::IsWithinLOSInMap` Object.h:364 (`IsWithinLOSInMapCompat`), `IsWithinLOS` |
| liquid | `Map::GetTerrain` Map.h:302 and `TerrainInfo::GetLiquidStatus` through `LiquidStatusCompat` (water, under water; any unreadable terrain refuses the point) |
| step creatures | `WorldObject::GetCreatureListWithEntryInGrid` Object.h:500 (the Turtle glue's own call); `Creature::GetCreatureData` Creature.h:275 + `SpawnData.h:136 spawnPoint` for the DB row, `GetDefaultMovementType` :169 (WAYPOINT), `GetFormation` :416 + `CreatureGroup::GetLeader` CreatureGroups.h:94 (waypoint follower), `CanFly`/`IsFlying`/`IsHovering`, `IsInEvadeMode` :224, `GetAttackDistance` :335 (aggro reach), `GetCreatureType`, rank through `CreatureRankCompat` |
| hop units | `Trinity::AnyUnfriendlyUnitInObjectRangeCheck` + `UnitListSearcher` + `Cell::VisitAllObjects` (Grids/Notifiers/GridNotifiers.h:1190; the same call `BotMgr::AttackNearest` makes, TbBotMgr.cpp:674) |
| boss own spawn, boss search | the S5 unit rows (`CheckPlanOnMap`, TbPlanUnits.h) give the spawn id of each member; `Map::GetCreatureBySpawnId` Map.h:453, `GetCreatureRespawnTime` :537 (a future time on a spawn that is not loaded = dead, body gone), `IsGridLoaded(x,y)` :278, `ObjectMgr::GetCreatureData` |
| config | `PlayerbotAIConfig` members (the module's `aiplayerbot.conf`) |

The boss `GetBossState` cross-check of spec 5.3 is NOT implemented (see section 7): the creature is the source, as the spec says to do when they disagree.

## 4. Coverage of the measured fields (`RUN-S6-F-FIELDS.tsv`)

`Facts` has 246 fields; the RFC measured 83 read (A), 42 reachable (C yes), 30 stall/tick (C check), 86 never (C no), 5 with no read site.

| Of the 125 fields A + C yes | count |
|---|---|
| filled from the core's API (or constants of the plan/step reads) | 88 |
| copied from the config | 9 |
| forced by the glue (off / zero / no gate) | 22 |
| "not known" (the core cannot answer: the do-less default stands, named) | 6 |
| skipped | 0 |
| **total** | **125** |

Of the 30 stall/tick fields: 10 filled, 8 not known, 12 skipped. Of the 86 "never" fields: 81 skipped and 5 forced off (`respawnNest`, `autoCcRisky`, `stragglerPull`, `lootFfaOn`, `dryWalk`); the 5 with no read site: never. Every skipped, never and not-known field stays its `Facts()` default (the test checks it after every tick of six scenarios). The 6 not known: `directPathCrossesPack`, `leadShotYd`, `bossSearchNoSpawn`, `bossSearchEventOff`, `bossSearchEventId`, `bossSearchEventName`. Element fields of `members[]` (33 `MemberFact` fields) and the unit lists (59 `UnitFact` fields), which the RFC's access log did not cover: filled are the member's name, yd, dz, x/y/z, inCombat, attackers, offPlanMap, otherCopy, healer, tank, core, level, actedMs (approximation, section 7), engaged/engagedRead, offMapReturnable (false) and, for units, guid, entry, distLead, x/y/z, hostile, stealthed, evading, attackable, selectable, caster, normalRank, level, creatureType, distSpot, spawnOwned, spawnYd, aloft, health, maxHealth, partyHits, hitsParty, engaged, hitting, castingAtRange, victimHealer/Pet/Tank (each list fills the fields its policy reads). Left at their default: the cc fields (`polymorph`, `cc`, `ccImmune`, `ccRemainMs`, `fleeRoomYd`), `held`/`frozen`, `targetGuid`, `withLead`, the wedge reach fields (`losLead`, `pathToLead`, `pathLenYd`), `summonCancels`, `formation`/`patrols`/`loop`/`cycleMs`, `packGuids`, `respawnSec`/`homeYd`, the estimate fields, `campIdle`, `owner`/`ownerEngaged`.

## 5. Cost per tick (spec 5.3: "at most one path query per step point plus one per hop")

One read at 1 Hz per run. Worst case, by construction (the caps are constants in `TbRunFactsPure.h`): path queries at most 8 (anchor 1 on the tick after a gather asked, approach or move probe 1, hop units at most `kMaxHopPaths`=4, fight forward 1, shortcut 1; typically 0-2: the move probe is cached 5 s by `MoveProbeCache`); creature grid searches at most 1 per entry of a pull step (a pull row has 1-5 entries) + 1 hop search + 1 forward search; line-of-sight tests at most `kMaxHopUnits`=8 + 1; creature observations at most 192; every list capped at 48 (a cut is counted in `FactsReport::listsCut`). The party read is 5 members x ~30 core calls, and in a fight each member's attacker set. No database access anywhere (the unit rows are read at boot by H5; a missing table is guarded by `WorldQueryCompat`). Measured today: the pure half costs 2-3 us a tick on the scripted RFC run (mean over 455-743 ticks; max under 90 us); the core half has no live number yet: every read fills `FactsReport` (costUs, members, pathQueries, gridScans, losQueries, heightQueries, creaturesSeen, listsCut) and `Totals()` keeps reads, total and max microseconds, path queries and grid scans since boot, for the live observation (spec gate k: module average < 60 us with five engines).

## 6. The standalone test

```
python3 forever/tools/facts_rfc_rows.py ragefire_chasm /tmp/rfc_rows.tsv
g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/test_run_facts.cpp -o /tmp/test_run_facts
/tmp/test_run_facts /tmp/rfc_rows.tsv forever/docs/RUN-S6-F-FIELDS.tsv [--table]
```
It loads the exported 84-step plan, runs the real `DungeonRun::Tick` with Facts built only by the glue's pure half over a scripted world and checks: six scenarios (happy, rest, death, wipe, pack = patroller/residue/left units around every pull, hop = a hostile at a hop point) walk the plan to `Done` at step 84/84; direct checks of the reads the policy asks for rarely (fight forward, shortcut, anchor, an unreadable path, boss search alive/dead/own dead/unloaded, island, walk back ending short, lead off the floor, party roll-up); the table covers all 246 fields; no skipped/never/not-known field changed from its default; `ApplyConfig` forces the unported features off; the handle table is stable, resolves back, capped and never reuses a handle; an absent member counts dead and far. Result in section 8. What it does NOT prove: the core calls themselves (that is the build, and a live run).

## 7. Not done / limits (said, not hidden)

* No live proof (no server in this package): every core call is compiled (option ON worldserver), not run. First things to watch live: `FactsReport` costs, `notKnown`, `listsCut`, and the first read of each "skipped" field in a real run (the RFC's advice of a page-protection debug switch was not built).
* `bossState` cross-check with `InstanceScript::GetBossState` (spec 5.3, U5): not read; the boss is the creature (alive, dead, or respawn time pending) and the S5 unit rows. The boss index -> entry map of the classic script is private (U5).
* No navmesh feet probe for `leadOffFloor`: `leadFloorZ` is the map height read; `leadOffFloor` also needs `isMmapLoaded`, else it stays false (T: "a map without the mesh cannot judge").
* `members[].actedMs`: no cast/swing counters exist on ForeverCore, so a bot casting or meleeing now reads "acted now" and any other reads "none": approximation, named "members.actedMs(exact)" in `FactsReport::notKnown`.
* `formOf` (a script changing a creature's entry) is always 0: ForeverCore keeps no such record (Thekal-type bosses are not in Ragefire Chasm).
* Threat-list holders are not read into a member's `engaged` list (T read `HostileRefManager`); attackers and victims are.
* A pull step whose unit rows matched no member is not skipped by the facts: `StepMatchedMembers` tells C6 (-1 unknown, 0 none), which skips it as "not cleared" (spec 5.3).
* Raid, crowd control, doors, loot, items, gossip, rez hold, firing stand, rescue/fetch/return: not read (the plan has none, or forced off); a step of such a kind reads as "nothing there" and the policy pauses itself.

## 8. Needs from O6 / C6 / merge

**C6 (tick, run commands)**
1. Call `run::ReadFacts(rec, GameTime::GetGameTimeMS(), facts, &report)` once per run per tick (1 Hz, `OnBeforeMapsUpdate`); on `false` pause the run (lead not in the world), as T's `PauseLeadOffMap`. Keep no `Facts` between ticks (`pointLiquid` is safe, the rest is plain data).
2. Call `run::ForgetRun(leader)` when a run is removed (stop/done), else its handle table stays until a 64th run evicts it.
3. After `DungeonRun::Tick` the T glue also calls `ApproachStandFound`, `StallStandIn`, `SpeedRateNow` (commands/Commander.cpp TickDungeonRun; the RFC tracer does the same); the policy asks for them through `Run::approachStandWanted`/`stallStandInWanted`.
4. For a pull step call `run::StepMatchedMembers(rec, step)`; 0 = skip the step as not cleared (never guess a pack). -1 = not known (no unit rows / not on the plan's map).
5. A player joining the group mid-run appears in `Facts::players`; the run pauses on `player-in-group`.
6. Surface `FactsReport`/`Totals()` in `.tb run status` or the run line if wanted (cost, notKnown).

**O6 (orders)**
1. P's orders carry unit guids as handles: resolve them with `run::ResolveHandle(leader, handle, guid)` before queuing (`attack <guid> <bot>`, `focus <guid>`, `pull`/`pullback <guid>`); an unresolved handle = refuse with `unreachable`/`target-gone`, never a guess.
2. `leadStaying`/`leadStayKnown`/`leadStay{X,Y,Z}` read the bot's `stay` strategy and `position` value "stay" (the entries `TbOrders.cpp:273-282` writes for `come`). O6's `move`, `move-direct` and `pullback` must write the stay position the same way, or the guard's walk-back has no stand point.
3. `leadIsTank` reads `PlayerbotAI::IsTank(lead)`: the tank role (`.tb role`) must be set on the leader before `.tb run start`.

**merge**
1. `forever/SLICE5.txt` still lacks the four slice-4 files and the root `CMakeLists.txt` (line 73, from slice 4) points `TB_SLICE_LIST` at `forever/SLICE4.txt`, so the option-ON build does not yet compile `DungeonPlanMgr.cpp`/`BossTacticsMgr.cpp` (P5's notes, "Needs at merge"). F6 needs none of that (it includes the plan headers and H5's `TbPlanUnits.h`); the M5 package owns it.
2. No registration line is needed for F6 (the CMake glob picks up `forever/src/host/run/*.cpp`); C6's `TbRun.cpp` includes `host/run/TbRunFacts.h`.
3. `forever/tools/test_run_facts.cpp` needs `forever/tools/facts_rfc_rows.py` and `data/sql/world/20261113203000_world.sql` (both in the tree); add it to the VM policy-test list next to `test_dungeon_run_policy`.

## 9. Gates (final tree)

All in slot 3 of the build VM, on the final tree (the last code commit is `F6: no GetMap() on a player without a map`; later commits are notes).

| Gate | Result |
|---|---|
| option-ON build (`FOREVER_SLOT=3 FOREVER_CMAKE_EXTRA=-DTORTOISEBOTS_SLICE1=1 build.sh 12 worldserver`) | `CMAKE_RC=0 MAKE_RC=0`, `worldserver` links; `nm -C libtortoisebots.a`: `ReadFacts`, `HandleFor`, `ResolveHandle`, `ForgetRun`, `StepMatchedMembers`, `Totals` present (the linker does not pull them into `worldserver` until C6 calls them: nothing references them yet) |
| option-OFF module build (`-DTORTOISEBOTS_SLICE1=OFF`, target `tortoisebots`) | `CMAKE_RC=0 MAKE_RC=0`; `libtortoisebots.a` holds `TbRunFacts.cpp.o` as an empty object (the file is behind `TORTOISEBOTS_SLICE1`), so the OFF build is unchanged in behaviour |
| census SLICE5 / SLICE4 / SLICE3 / SLICE2 / SLICE1 (`FOREVER_CENSUS_LIST`) | `files 172 / 174 / 170 / 160 / 133, compile clean = files, error places 0`, includes of a missing header 0 (F6 adds no legacy file, so the counts are the branch's) |
| `slice_gen_context.py --check`, `slice_registry_check.py` | `SliceAiObjectContext.cpp is up to date`; `(a)=0  (b)=215` (slice 4's merge raised (b) from 207; not F6's) |
| `test_run_facts` (g++ -std=c++17 -Wall -Wextra, VM) | `RESULT: PASS`: six scenarios walk the plan 84/84 to `done` (455, 675, 743, 476, 455, 471 ticks); direct checks ok; table 246 fields (filled 98, config 9, forced 27, notknown 14, skipped 93, never 5); 101 filled/config/forced fields changed from their default by the scenarios, the other 33 are core-only reads or equal to the default; no skipped/never/notknown field ever changed; pure-half cost 2-3 us a tick mean, 83 us max |
| `test_dungeon_run_policy`, `test_system_run_policy` (unchanged) | `ok`, `109 checks passed` |
| Turtle `vm-build` / `vm-test tb` | not needed: nothing outside `forever/` changed |
| server / SQL | none started, none applied |


## 10. The coverage table (generated from `RUN-S6-F-FIELDS.tsv`)

Status: filled = read from the core; config = copied from `aiplayerbot.conf`; forced = the glue sets the value whatever the conf says; notknown = the core cannot tell, the `Facts()` default (the do-less value) stands; skipped = not filled, default stands.

### A: read in the simulated RFC run (83)

| field | status | from where / why |
|---|---|---|
| `players` | filled | slots of the lead's group that are not host bots (Group.h:341, ObjectAccessor::FindPlayer) |
| `total` | filled | the lead's group slots: a slot with no player in the world counts dead and 1000 yd away |
| `alive` | filled | the lead's group slots: a slot with no player in the world counts dead and 1000 yd away |
| `dead` | filled | the lead's group slots: a slot with no player in the world counts dead and 1000 yd away |
| `raid` | forced | raid.on stays false: a bot-led party is no raid binding (DungeonRunRaid not read) |
| `linkedDeath` | config | PlayerbotAIConfig (aiplayerbot.conf keys DungeonRun*), copied once per read |
| `nearWipeStopCount` | config | PlayerbotAIConfig (aiplayerbot.conf keys DungeonRun*), copied once per read |
| `nearWipeStopWindowMs` | config | PlayerbotAIConfig (aiplayerbot.conf keys DungeonRun*), copied once per read |
| `leadAlive` | filled | the lead Player (Player::IsAlive, position, GetName, GetTeam) |
| `anyInCombat` | filled | Unit::getAttackers Unit.h:741, GetVictim :743, IsInCombat :1075 of the bots and their pets |
| `leadInCombat` | filled | Unit::getAttackers Unit.h:741, GetVictim :743, IsInCombat :1075 of the bots and their pets |
| `staleCombat` | filled | Unit::getAttackers Unit.h:741, GetVictim :743, IsInCombat :1075 of the bots and their pets |
| `engagedUnits` | filled | Unit::getAttackers Unit.h:741, GetVictim :743, IsInCombat :1075 of the bots and their pets |
| `engagedRead` | filled | Unit::getAttackers Unit.h:741, GetVictim :743, IsInCombat :1075 of the bots and their pets |
| `ownFightRead` | filled | one ObjectAccessor::GetUnit per remembered guid handle (Alive/Dead/Gone; a dropped handle stays Unknown) |
| `leadX` | filled | the lead Player (Player::IsAlive, position, GetName, GetTeam) |
| `leadY` | filled | the lead Player (Player::IsAlive, position, GetName, GetTeam) |
| `leadZ` | filled | the lead Player (Player::IsAlive, position, GetName, GetTeam) |
| `leadOffFloor` | filled | Map height read (GetHeightCompat, Map.h:330) and the navmesh presence (WorldPosition::isMmapLoaded); no feet probe: leadFloorZ is the height read |
| `leadResting` | filled | PlayerbotAI seated meal, Unit::GetStandState, IsNonMeleeSpellCast, isMoving, IsInWater; 'last movement' value |
| `maxMemberYd` | filled | the lead's group slots: a slot with no player in the world counts dead and 1000 yd away |
| `raidIcons` | filled | Group::GetTargetIcon Group.h:362, each mark a guid handle |
| `raidIconsRead` | filled | Group::GetTargetIcon Group.h:362, each mark a guid handle |
| `members` | filled | the lead's group slots: a slot with no player in the world counts dead and 1000 yd away |
| `deadNames` | filled | the lead's group slots: a slot with no player in the world counts dead and 1000 yd away |
| `anchorPathRead` | filled | one PathGenerator query (PathGenerator.h:86), only on the tick after a gather asked |
| `directPathCrossesPack` | notknown | false: the test needs the live list of uncleared packs along the path |
| `approachIncomplete` | filled | one PathGenerator query toward the step point (PathGenerator.h:86/100/102); a path that cannot be read: leadPathGood=false, approachKnown=false |
| `walkBackRescue` | forced | forced off: the rescue and return teleports are GM powers (spec 5.2), the revive stand-still hold and the dry walk have no reader |
| `teleportReturnYd` | forced | forced off: the rescue and return teleports are GM powers (spec 5.2), the revive stand-still hold and the dry walk have no reader |
| `reviveHoldYd` | forced | forced off: the rescue and return teleports are GM powers (spec 5.2), the revive stand-still hold and the dry walk have no reader |
| `rescueRead` | forced | forced off: the rescue and return teleports are GM powers (spec 5.2), the revive stand-still hold and the dry walk have no reader |
| `fightForward` | config | PlayerbotAIConfig (aiplayerbot.conf keys DungeonRun*), copied once per read |
| `lightRest` | config | PlayerbotAIConfig (aiplayerbot.conf keys DungeonRun*), copied once per read |
| `killFirst` | filled | the engaged units whose entry the plan lists in kill_first (the grid read of a totem key is not ported) |
| `minHealthPct` | filled | Unit::GetHealth/GetPower of the living bots; priest, paladin or shaman out of combat |
| `minHealerManaPct` | filled | Unit::GetHealth/GetPower of the living bots; priest, paladin or shaman out of combat |
| `minManaPct` | filled | Unit::GetHealth/GetPower of the living bots; priest, paladin or shaman out of combat |
| `eating` | filled | PlayerbotAI::HasSeatedMeal(food/drink) PlayerbotAI.h:1060 |
| `rezzer` | filled | Unit::GetHealth/GetPower of the living bots; priest, paladin or shaman out of combat |
| `rezHold` | forced | forced off: the rescue and return teleports are GM powers (spec 5.2), the revive stand-still hold and the dry walk have no reader |
| `team` | filled | the lead Player (Player::IsAlive, position, GetName, GetTeam) |
| `seen` | filled | one PathGenerator query toward the step point (PathGenerator.h:86/100/102); a path that cannot be read: leadPathGood=false, approachKnown=false |
| `approachKnown` | filled | one PathGenerator query toward the step point (PathGenerator.h:86/100/102); a path that cannot be read: leadPathGood=false, approachKnown=false |
| `approachX` | filled | one PathGenerator query toward the step point (PathGenerator.h:86/100/102); a path that cannot be read: leadPathGood=false, approachKnown=false |
| `approachY` | filled | one PathGenerator query toward the step point (PathGenerator.h:86/100/102); a path that cannot be read: leadPathGood=false, approachKnown=false |
| `approachZ` | filled | one PathGenerator query toward the step point (PathGenerator.h:86/100/102); a path that cannot be read: leadPathGood=false, approachKnown=false |
| `leadIsland` | filled | one PathGenerator query toward the step point (PathGenerator.h:86/100/102); a path that cannot be read: leadPathGood=false, approachKnown=false |
| `leadPathGood` | filled | one PathGenerator query toward the step point (PathGenerator.h:86/100/102); a path that cannot be read: leadPathGood=false, approachKnown=false |
| `units` | filled | grid search of the step's entries, classified by the policy's own ClassifyPullRead; Creature::GetCreatureData spawnPoint, GetDefaultMovementType, GetFormation |
| `patrollers` | filled | grid search of the step's entries, classified by the policy's own ClassifyPullRead; Creature::GetCreatureData spawnPoint, GetDefaultMovementType, GetFormation |
| `packResidue` | filled | grid search of the step's entries, classified by the policy's own ClassifyPullRead; Creature::GetCreatureData spawnPoint, GetDefaultMovementType, GetFormation |
| `packClearMarginYd` | config | PlayerbotAIConfig (aiplayerbot.conf keys DungeonRun*), copied once per read |
| `hopUnits` | filled | Trinity::AnyUnfriendlyUnitInObjectRangeCheck Grids/Notifiers/GridNotifiers.h:1190 + Cell::VisitAllObjects, IsWithinLOSInMap Object.h:364, at most 4 path queries |
| `bossDeadNearSpot` | filled | grid search of the boss entry, WorldObject::GetCreatureListWithEntryInGrid Object.h:500; Creature::IsAlive |
| `bossDeadNearLead` | filled | grid search of the boss entry, WorldObject::GetCreatureListWithEntryInGrid Object.h:500; Creature::IsAlive |
| `bossCorpseGuid` | filled | grid search of the boss entry, WorldObject::GetCreatureListWithEntryInGrid Object.h:500; Creature::IsAlive |
| `bossCorpseX` | filled | grid search of the boss entry, WorldObject::GetCreatureListWithEntryInGrid Object.h:500; Creature::IsAlive |
| `bossCorpseY` | filled | grid search of the boss entry, WorldObject::GetCreatureListWithEntryInGrid Object.h:500; Creature::IsAlive |
| `bossCorpseZ` | filled | grid search of the boss entry, WorldObject::GetCreatureListWithEntryInGrid Object.h:500; Creature::IsAlive |
| `bossOwnDead` | filled | spawn id of the S5 unit row (role boss) -> Map::GetCreatureBySpawnId Map.h:453; not loaded: Map::GetCreatureRespawnTime Map.h:537 in the future = dead. No unit row matched: not known (false) |
| `bossGateStacks` | forced | no pull gate: ai_boss_tactic has no row for map 389 and boss tactics are not ported (stacks -1, rest 0/empty) |
| `bossGateMax` | forced | no pull gate: ai_boss_tactic has no row for map 389 and boss tactics are not ported (stacks -1, rest 0/empty) |
| `seekSiphonCycle` | forced | 0: the seek-aura clock is Hakkar only (map 309) |
| `carrier` | filled | the lead Player (Player::IsAlive, position, GetName, GetTeam) |
| `leadIsTank` | filled | getAttackers of the lead and of the party bots within kGuardYd, Unit::CanReachWithMeleeAutoAttack, PlayerbotAI::IsTank, the 'stay' strategy and 'position' value, PullStrategy |
| `leadStaying` | filled | getAttackers of the lead and of the party bots within kGuardYd, Unit::CanReachWithMeleeAutoAttack, PlayerbotAI::IsTank, the 'stay' strategy and 'position' value, PullStrategy |
| `leadHasAttacker` | filled | getAttackers of the lead and of the party bots within kGuardYd, Unit::CanReachWithMeleeAutoAttack, PlayerbotAI::IsTank, the 'stay' strategy and 'position' value, PullStrategy |
| `partyAttackers` | filled | getAttackers of the lead and of the party bots within kGuardYd, Unit::CanReachWithMeleeAutoAttack, PlayerbotAI::IsTank, the 'stay' strategy and 'position' value, PullStrategy |
| `leadReaching` | filled | getAttackers of the lead and of the party bots within kGuardYd, Unit::CanReachWithMeleeAutoAttack, PlayerbotAI::IsTank, the 'stay' strategy and 'position' value, PullStrategy |
| `joinerCeiling` | forced | forced off whatever the conf says: the readers of these features (aggro estimate, firing stand, crowd control, totems, nests, straggler pull) are not ported |
| `aggroWalkRing` | forced | forced off whatever the conf says: the readers of these features (aggro estimate, firing stand, crowd control, totems, nests, straggler pull) are not ported |
| `pullStand` | forced | forced off whatever the conf says: the readers of these features (aggro estimate, firing stand, crowd control, totems, nests, straggler pull) are not ported |
| `standClearance` | forced | forced off whatever the conf says: the readers of these features (aggro estimate, firing stand, crowd control, totems, nests, straggler pull) are not ported |
| `standDiscipline` | forced | forced off whatever the conf says: the readers of these features (aggro estimate, firing stand, crowd control, totems, nests, straggler pull) are not ported |
| `walkBackAfterWonFight` | config | PlayerbotAIConfig (aiplayerbot.conf keys DungeonRun*), copied once per read |
| `retreatAt2Dead` | config | PlayerbotAIConfig (aiplayerbot.conf keys DungeonRun*), copied once per read |
| `leadShotYd` | notknown | 0: the tank's ranged shot range is not read (pull stand is forced off) |
| `standPullPreCc` | forced | forced off whatever the conf says: the readers of these features (aggro estimate, firing stand, crowd control, totems, nests, straggler pull) are not ported |
| `autoCc` | forced | forced off whatever the conf says: the readers of these features (aggro estimate, firing stand, crowd control, totems, nests, straggler pull) are not ported |
| `totemFocus` | forced | forced off whatever the conf says: the readers of these features (aggro estimate, firing stand, crowd control, totems, nests, straggler pull) are not ported |
| `packFocusSparesTacticUnits` | forced | forced off whatever the conf says: the readers of these features (aggro estimate, firing stand, crowd control, totems, nests, straggler pull) are not ported |
| `pointLiquid` | filled | terrain liquid status at the point (TbCompat LiquidStatusCompat over TerrainInfo::GetLiquidStatus); an unreadable terrain refuses the point |

### C yes: reachable by the RFC rows (42)

| field | status | from where / why |
|---|---|---|
| `anchorReachable` | filled | one PathGenerator query (PathGenerator.h:86), only on the tick after a gather asked |
| `forwardRead` | filled | PathGenerator query and a hostile scan on the tick after the policy asked (Run::fightForwardWanted, shortcutWanted) |
| `forwardFor` | filled | PathGenerator query and a hostile scan on the tick after the policy asked (Run::fightForwardWanted, shortcutWanted) |
| `forwardPathOk` | filled | PathGenerator query and a hostile scan on the tick after the policy asked (Run::fightForwardWanted, shortcutWanted) |
| `forwardLos` | filled | PathGenerator query and a hostile scan on the tick after the policy asked (Run::fightForwardWanted, shortcutWanted) |
| `forwardHostileNear` | filled | PathGenerator query and a hostile scan on the tick after the policy asked (Run::fightForwardWanted, shortcutWanted) |
| `forwardHopAlive` | filled | PathGenerator query and a hostile scan on the tick after the policy asked (Run::fightForwardWanted, shortcutWanted) |
| `shortcutRead` | filled | PathGenerator query and a hostile scan on the tick after the policy asked (Run::fightForwardWanted, shortcutWanted) |
| `shortcutFor` | filled | PathGenerator query and a hostile scan on the tick after the policy asked (Run::fightForwardWanted, shortcutWanted) |
| `shortcutOk` | filled | PathGenerator query and a hostile scan on the tick after the policy asked (Run::fightForwardWanted, shortcutWanted) |
| `approachRead` | filled | one PathGenerator query toward the step point (PathGenerator.h:86/100/102); a path that cannot be read: leadPathGood=false, approachKnown=false |
| `packClearRepulls` | config | PlayerbotAIConfig (aiplayerbot.conf keys DungeonRun*), copied once per read |
| `packNearestLeft` | filled | grid search of the step's entries, classified by the policy's own ClassifyPullRead; Creature::GetCreatureData spawnPoint, GetDefaultMovementType, GetFormation |
| `packNearestLeftRead` | filled | grid search of the step's entries, classified by the policy's own ClassifyPullRead; Creature::GetCreatureData spawnPoint, GetDefaultMovementType, GetFormation |
| `bossOwnDeadEntry` | filled | spawn id of the S5 unit row (role boss) -> Map::GetCreatureBySpawnId Map.h:453; not loaded: Map::GetCreatureRespawnTime Map.h:537 in the future = dead. No unit row matched: not known (false) |
| `bossSearchRead` | filled | the matched spawns of the boss entry (S5 unit rows): Map::GetCreatureBySpawnId, respawn time, Map::IsGridLoaded Map.h:278 |
| `bossSearchAlive` | filled | the matched spawns of the boss entry (S5 unit rows): Map::GetCreatureBySpawnId, respawn time, Map::IsGridLoaded Map.h:278 |
| `bossSearchAliveEntry` | filled | the matched spawns of the boss entry (S5 unit rows): Map::GetCreatureBySpawnId, respawn time, Map::IsGridLoaded Map.h:278 |
| `bossAliveX` | filled | the matched spawns of the boss entry (S5 unit rows): Map::GetCreatureBySpawnId, respawn time, Map::IsGridLoaded Map.h:278 |
| `bossAliveY` | filled | the matched spawns of the boss entry (S5 unit rows): Map::GetCreatureBySpawnId, respawn time, Map::IsGridLoaded Map.h:278 |
| `bossAliveZ` | filled | the matched spawns of the boss entry (S5 unit rows): Map::GetCreatureBySpawnId, respawn time, Map::IsGridLoaded Map.h:278 |
| `bossSearchDead` | filled | the matched spawns of the boss entry (S5 unit rows): Map::GetCreatureBySpawnId, respawn time, Map::IsGridLoaded Map.h:278 |
| `bossSearchUnloaded` | filled | the matched spawns of the boss entry (S5 unit rows): Map::GetCreatureBySpawnId, respawn time, Map::IsGridLoaded Map.h:278 |
| `bossSearchOwnDead` | filled | the matched spawns of the boss entry (S5 unit rows): Map::GetCreatureBySpawnId, respawn time, Map::IsGridLoaded Map.h:278 |
| `bossSearchNoSpawn` | notknown | false/0: no spawn-less or game-event read (the RFC bosses are plain spawns with unit rows) |
| `bossSearchEventOff` | notknown | false/0: no spawn-less or game-event read (the RFC bosses are plain spawns with unit rows) |
| `bossSearchEventId` | notknown | false/0: no spawn-less or game-event read (the RFC bosses are plain spawns with unit rows) |
| `bossSearchEventName` | notknown | false/0: no spawn-less or game-event read (the RFC bosses are plain spawns with unit rows) |
| `bossSearchCorpseGuid` | filled | the matched spawns of the boss entry (S5 unit rows): Map::GetCreatureBySpawnId, respawn time, Map::IsGridLoaded Map.h:278 |
| `bossSearchCorpseX` | filled | the matched spawns of the boss entry (S5 unit rows): Map::GetCreatureBySpawnId, respawn time, Map::IsGridLoaded Map.h:278 |
| `bossSearchCorpseY` | filled | the matched spawns of the boss entry (S5 unit rows): Map::GetCreatureBySpawnId, respawn time, Map::IsGridLoaded Map.h:278 |
| `bossSearchCorpseZ` | filled | the matched spawns of the boss entry (S5 unit rows): Map::GetCreatureBySpawnId, respawn time, Map::IsGridLoaded Map.h:278 |
| `bossGateSpell` | forced | no pull gate: ai_boss_tactic has no row for map 389 and boss tactics are not ported (stacks -1, rest 0/empty) |
| `bossGateHoldS` | forced | no pull gate: ai_boss_tactic has no row for map 389 and boss tactics are not ported (stacks -1, rest 0/empty) |
| `bossGatePriests` | forced | no pull gate: ai_boss_tactic has no row for map 389 and boss tactics are not ported (stacks -1, rest 0/empty) |
| `leadStayKnown` | filled | getAttackers of the lead and of the party bots within kGuardYd, Unit::CanReachWithMeleeAutoAttack, PlayerbotAI::IsTank, the 'stay' strategy and 'position' value, PullStrategy |
| `leadStayX` | filled | getAttackers of the lead and of the party bots within kGuardYd, Unit::CanReachWithMeleeAutoAttack, PlayerbotAI::IsTank, the 'stay' strategy and 'position' value, PullStrategy |
| `leadStayY` | filled | getAttackers of the lead and of the party bots within kGuardYd, Unit::CanReachWithMeleeAutoAttack, PlayerbotAI::IsTank, the 'stay' strategy and 'position' value, PullStrategy |
| `leadStayZ` | filled | getAttackers of the lead and of the party bots within kGuardYd, Unit::CanReachWithMeleeAutoAttack, PlayerbotAI::IsTank, the 'stay' strategy and 'position' value, PullStrategy |
| `leadHit` | filled | getAttackers of the lead and of the party bots within kGuardYd, Unit::CanReachWithMeleeAutoAttack, PlayerbotAI::IsTank, the 'stay' strategy and 'position' value, PullStrategy |
| `leadAttacker` | filled | getAttackers of the lead and of the party bots within kGuardYd, Unit::CanReachWithMeleeAutoAttack, PlayerbotAI::IsTank, the 'stay' strategy and 'position' value, PullStrategy |
| `auraGatedTargets` | forced | forced off whatever the conf says: the readers of these features (aggro estimate, firing stand, crowd control, totems, nests, straggler pull) are not ported |

### C check: stall / stuck / tick-body rules (30)

| field | status | from where / why |
|---|---|---|
| `charmedFighting` | skipped | not read by an RFC run (other): the Facts() default stands |
| `ownFightUnits` | filled | one ObjectAccessor::GetUnit per remembered guid handle (Alive/Dead/Gone; a dropped handle stays Unknown) |
| `leadFloorZ` | filled | Map height read (GetHeightCompat, Map.h:330) and the navmesh presence (WorldPosition::isMmapLoaded); no feet probe: leadFloorZ is the height read |
| `leadMoving` | filled | PlayerbotAI seated meal, Unit::GetStandState, IsNonMeleeSpellCast, isMoving, IsInWater; 'last movement' value |
| `leadCasting` | filled | PlayerbotAI seated meal, Unit::GetStandState, IsNonMeleeSpellCast, isMoving, IsInWater; 'last movement' value |
| `leadInWater` | filled | PlayerbotAI seated meal, Unit::GetStandState, IsNonMeleeSpellCast, isMoving, IsInWater; 'last movement' value |
| `leadMoveFailed` | filled | PlayerbotAI seated meal, Unit::GetStandState, IsNonMeleeSpellCast, isMoving, IsInWater; 'last movement' value |
| `leadMoveFailReason` | filled | PlayerbotAI seated meal, Unit::GetStandState, IsNonMeleeSpellCast, isMoving, IsInWater; 'last movement' value |
| `leadStayWalkResting` | notknown | default: the stall fallback reads (second island sign, stay-walk back-off, swim line) are not ported |
| `leadStayWalkFails` | notknown | default: the stall fallback reads (second island sign, stay-walk back-off, swim line) are not ported |
| `stallDirectRead` | notknown | default: the stall fallback reads (second island sign, stay-walk back-off, swim line) are not ported |
| `stallDirectClear` | notknown | default: the stall fallback reads (second island sign, stay-walk back-off, swim line) are not ported |
| `directJoinRead` | skipped | not read by an RFC run (rescue, fetch, return, rejoin, summon): the Facts() default stands |
| `rezMembers` | skipped | not read by an RFC run (rez hold, resurrection): the Facts() default stands |
| `rezInProgress` | skipped | not read by an RFC run (rez hold, resurrection): the Facts() default stands |
| `goodSpotCut` | notknown | default: the stall fallback reads (second island sign, stay-walk back-off, swim line) are not ported |
| `bossHpPct` | filled | Unit::GetHealthPct of the living plan boss |
| `bossBerserk` | notknown | false/0: Hakkar's enrage and siphon are map 309 only |
| `seekSiphonDosed` | notknown | false/0: Hakkar's enrage and siphon are map 309 only |
| `seekSiphonUndosed` | notknown | false/0: Hakkar's enrage and siphon are map 309 only |
| `objectState` | skipped | not read by an RFC run (doors, objects, loot, items, gossip, tasks): the Facts() default stands |
| `eventData` | skipped | not read by an RFC run (hold, ritual, event): the Facts() default stands |
| `clearUnits` | skipped | not read by an RFC run (hold, ritual, event): the Facts() default stands |
| `taskReply` | skipped | not read by an RFC run (doors, objects, loot, items, gossip, tasks): the Facts() default stands |
| `taskText` | skipped | not read by an RFC run (doors, objects, loot, items, gossip, tasks): the Facts() default stands |
| `leadHeldEntries` | skipped | not read by an RFC run (other): the Facts() default stands |
| `reachAggro` | filled | getAttackers of the lead and of the party bots within kGuardYd, Unit::CanReachWithMeleeAutoAttack, PlayerbotAI::IsTank, the 'stay' strategy and 'position' value, PullStrategy |
| `dpsAlive` | filled | living bots that are neither tank nor healer |
| `ccHeld` | skipped | not read by an RFC run (fight reads (engaged units, attackers, lead hit)): the Facts() default stands |
| `ccHeldRead` | skipped | not read by an RFC run (fight reads (engaged units, attackers, lead hit)): the Facts() default stands |

