# W4-A1: the travel node graph on ForeverCore

Round W4-A1 of `FOREVER-W0-W4-SPEC.md` part 2 (round table). Files: `ai/playerbot/TravelNode.cpp` and its both-core pair
(`forever/src/compat/TbCompat_WL4n.h` + `ai/cmangos-compat-wl4n.h`, wrapper `ai/playerbot/TbCompatWL4n.h`); the `TravelNodeMap` /
`TravelPath` stand-ins of the Travel block in `forever/src/slice/SliceLinkStubs.cpp` are gone; `forever/SLICE-W4.txt` is the
merged W1 list plus this round's legacy file, and `CMakeLists.txt` reads it. Zero core changes.

Round W4-A2 (travel destinations and zone levels, `forever/docs/W4-A2-NOTES.md`) is merged into this branch: `TravelMgr.cpp` and
`TravelValues.cpp` are in the slice with it, so `TravelMgr::LoadQuestTravelTable` - the loader the graph hangs off - runs on
ForeverCore too. This round wires the node store into it (`TravelGraphLoadCompat`, section 5), makes the path-point load
linear (section 6) and adds the runtime checks the review asked for (section 7).

## 0. The graph data itself is round B, not this one

This branch ships the **schema and the loader of the node graph, not the graph**: `data/sql/world/20260824090000_world.sql`
creates the three `ai_playerbot_travelnode*` tables empty, and no migration carries nodes, links or path points. The data is
the generation boot's output - round B of the plan (decision D3), which runs the generator once on a server that has the map
data and writes the rows with the saver this round ports - and it is deliberately not started here (`generatePaths` refuses
on the pinned core's PathInfo area filter, section 2).

Until that boot runs, a deployed server has **no graph**, and that is a clean no-op: `loadNodeStore` asks the schema guard
first, sees the tables missing or the datasets empty, logs one line, and leaves the graph empty; `generateAll` returns on the
empty node list; travel has no route and picks no target. No query ever reaches a missing table (ForeverCore aborts the
server on one), no error is logged, and the destination build (round A2) still runs. Section 7's runtime check covers this
state at every boot. When round B's rows exist, the same code path loads them.

## 1. What runs

| Piece | Where |
|---|---|
| The node store: `ai_playerbot_travelnode`, `_link`, `_path` read through the schema guard | `TravelNodeMap::loadNodeStore` |
| A missing table or column: empty graph, one line, travel does nothing | `emptyGraph` in `loadNodeStore` |
| The loader reads the store at boot (the graph is round B's data) | `TravelGraphLoadCompat` in `forever/src/compat/TbCompat_WL4.cpp` |
| One ordered path point per row, appended in place: a path loads in O(its points) | `TravelNodePath::appendPathPoint` |
| A*, with `TravelRoutePolicy` costs | `TravelNodeMap::getRoute`, `TravelNodePath::getCost` |
| Taxi and transport links loaded, never used | `TravelLinkUsableCompat` at the top of `getCost` (F: false for 3/4) |
| No portal edge in the A* (D4: the rescue is the only travel teleport) | `TravelTeleportLinksCompat` around the hearthstone and teleport-spell edges |
| The generator's map passes on the world thread on F | `TravelGenParallelCompat` at the four `std::async` sites |
| The saver asks the schema once, then writes synchronously (DELETE before INSERTs) | `TravelNodeStoreWritableCompat` + `TravelNodeExecuteCompat` |
| Loading the map data of a far tile (round B uses it) | `MmapTileLoadCompat` (F: `TerrainInfo::LoadMapAndVMap` + `LoadMMap`) |

The other helpers of the pair are the spellings the two cores differ in (area trigger fields and the teleport condition, creature
template and spawn fields and the NPC service flags, game-object spellcaster/transport/faction, the taxi tables, PlayerInfo's
start position, the high-elf race, the hold's fight gates, the spell name and target position, talent-free logging `SIZEFMTD`).
Every F answer keeps the legacy rule where it exists; where F has no data (template level, area-trigger zone test, GO faction,
teleport condition) the answer refuses or ignores LESS.

## 2. The cut (each one makes the bot do LESS)

| Legacy | W4-A1 on F |
|---|---|
| A taxi (`flightPath`) or transport (`transport`) link | edge cost -1, the A* never takes it, the route is refused |
| Hearthstone and teleport-spell edges | not added (D4); Turtle keeps them |
| A missing node table or column | empty graph, one line; the save is skipped with one line |
| Generator passes on worker threads | one thread (the generation boot's world thread); Turtle unchanged |
| A creature template level (path cost) | 0 = no threat modifier |
| An area trigger's zone test, its condition, a game object's faction | not refused here (the core decides when the bot crosses); Turtle unchanged |

## 3. Turtle behaviour

Every helper is the expression the file had, or an answer that leaves it untouched (`true`, the legacy check). Two places needed
care: the area-trigger existence check in `TravelPath::UpcommingSpecialMovement` is the legacy `GetAreaTrigger` test
(`AreaTriggerKnownCompat`), and `AreaTriggerZoneContainsCompat` is the legacy `IsPointInAreaTriggerZone`. `vm-build` and
`vm-test tb` pass.

## 4. Shared lines touched

- `forever/src/slice/SliceLinkStubs.cpp`: the whole Travel block is gone - the six TravelNodeMap/TravelPath stand-ins (this
  round) and the TravelMgr/TravelTarget/destination stand-ins (round A2) with `NoteTravel`, since all three owner files are in
  the slice now. The slice-9 block and the W1 block are untouched.
- `CMakeLists.txt`: `TB_SLICE_LIST` -> `forever/SLICE-W4.txt` (comment and FATAL_ERROR text updated); the OFF build filters
  `TbCompat_WL4.cpp` (round A2).
- `forever/SLICE-W4.txt`: the merged W1 list line for line (round A2 appended `TravelMgr.cpp` and `TravelValues.cpp` to it),
  then `ai/playerbot/TravelNode.cpp` (273 paths).
- `forever/tools/test_wl4_wiring.py`: round A2's file and this round's classes in one file (the A2 pair is `COMPAT_F`,
  `COMPAT_T`; this round's `COMPAT_FN`, `COMPAT_TN`).
- `ai/playerbot/TravelPointKey.h`: new; the destination point key (section 8, round A2's review).
- `forever/tools/slice_registry_check.py`: `SLICE_LIST` -> `SLICE-W4.txt` (the list CMake reads).
- `forever/tools/test_w1_wiring.py`, `test_slice9_l9_wiring.py`: the list pointer they pin moved from `SLICE-W1.txt` to
  `SLICE-W4.txt` (the W4 list starts with the W1 list, so their file checks are unchanged).
- `tools/test_forever_no_direct_queries.py`: the saver's one raw write (`TbCompat_WL4n.h`, after the S5 schema probe) is named
  with its count, like the two existing guard files.
- `forever/tools/travel_graph_boot_check.sh`: new; the runtime check (section 7).

## 5. The boot path (the wiring)

`TravelMgr::LoadQuestTravelTable` calls `TravelGraphLoadCompat()` where Turtle's loader always did. On ForeverCore that is the
one function in `forever/src/compat/TbCompat_WL4.cpp` (declared there for `TravelMgr.cpp`): `loadNodeStore()`, then
`generateAll()`, the two print helpers and (only when `generateTravelNodes` is set) the saver - the same order as the Turtle
pair's inline body. `loadNodeStore` is the only reader of the three tables and it asks `TravelNodeQueryCompat` (the
`WorldQueryCompat` guard) first, so an absent schema never reaches a query. With no rows, `generateAll` returns at its
`m_nodes.empty()` gate: no map offsets, no transfer table, no taxi pass, no coverage scan.

## 6. Path-point loading is linear

`loadNodeStore`'s path loop used to copy the path grown so far for every row (`std::vector<WorldPosition> ppath = path->GetPath();
ppath.push_back(...); path->setPath(ppath);`), so a path of N points copied O(N^2) positions and allocated on every row. The
rows arrive `ORDER BY node_id, to_node_id, nr`, so each is `TravelNodePath::appendPathPoint(point)` now - one `push_back` per
row, one growth sequence per path. The `setPath` copy is still there for the callers that replace a whole path.

## 7. Runtime checks (the harness, and what it fails on before this round)

`forever/tools/travel_graph_boot_check.sh` boots the installed run2 server once per scenario and asserts the log lines (it
installs nothing; the caller holds the server lock and restores the binary):

| Scenario | Database | Must show |
|---|---|---|
| `a` | the three tables dropped | one guard line (`world table `ai_playerbot_travelnode` is missing, or lacks a column this query names: not read`), exactly one `the travel-node tables are missing or incomplete; the travel graph stays empty and travel does nothing`; no node row read; no `Calculating mapoffset`; the A2 destination index still built (`>> travel destinations keep ... spawn points`) |
| `b` | the three tables, empty | one `travel-node dataset is empty or unavailable; skipping startup graph generation`; no guard refusal; no graph generation |
| `c` | three nodes, a link to a node id with no row (the out-of-map link) and its path points, one real link with three ordered points | `>> Loaded 3 travelNodes.`, both link rows read, the graph's derived data built (`Calculating mapoffset`), boot ready - the unknown target is skipped, never dereferenced |
| `final` | the three tables dropped again | the shipped state: one absent-graph line, `tb status` enabled, RSS recorded |

Every scenario also asserts the ready line, zero `ASSERTION FAILED`, and that the loader was reached (`Loading travel nodes.`).
Module lines land in two files: the bot brain logs through `BotLog` (`logs/bot.log`, where the node-store lines and the
empty-graph line go) and the `forever/src` compat code through the core logger (`logs/world.out`, where the destination index
lines go), so the check reads both and truncates both per boot.

On the reviewed tree (`07a57c8b2`) the same check fails: `TravelMgr.cpp` was not in the slice, so `LoadQuestTravelTable` never
ran, `Loading travel nodes.` and the guard line never appear, and scenario a fails five checks (run on the `07a57c8b2`
slot-8 build, `HARNESS_RC=1`: loader not reached, no destination index, no point map, no guard refusal, no absent-graph line).
That build was code no boot exercised.
## 8. Round A2's review findings, fixed here

The merge also carries the two findings of round A2's review (`codex-w4a2.txt`), because they live in the same files:

- **The destination point key dropped the guid's high half.** `TravelMgr::pointsMap` was keyed by
  `GuidKeyWL4` = `ObjectGuid::GetRawValue(0)`, which on ForeverCore is only the spawn counter; creatures and game objects count
  their spawns in separate namespaces, so two unrelated spawns collided and a destination pointed at the other object's
  coordinates and cached area. The key is `ai::TravelPointKey` (`ai/playerbot/TravelPointKey.h`: both raw halves, the hash folds
  the high one in); ForeverCore passes `GetRawValue(1)` and `GetRawValue(0)`, Turtle passes `0` and its one 64-bit guid and
  keeps the legacy identity exactly. `tools/test_travel_point_key.cpp` puts a creature and a game object with the same counter
  and different type/map/entry into the map and checks both points and their coordinates survive; before the fix the file does
  not compile (the key was one `uint64`).
- **The boot kept redundant full-world indexes.** `TravelEntryGuidpsData` held every spawn of maps 0/1 (123,558 spawns,
  15,684 entries) for the whole process while the destination build copied the points it keeps into `pointsMap` (67,420
  points); and the purpose map kept a row for every template of the world (224,997), most of which have no spawn and are
  skipped by the loader anyway. Now the purpose map only takes templates with an entry in the spawn index (both loops) - live:
  7,792 entries instead of 224,997 - and the loader calls `TravelEntryGuidpsReleaseCompat()` after `GetPopulatedGrids()`: on
  ForeverCore the boot index is dropped (live line `>> travel destination index released: 15684 entries, 123558 spawn
  points`), on Turtle the release is a no-op because that index is a shared value other code reads. One index remains per
  process: `pointsMap`, the one the destinations point into (67,420 points live).

  Memory, before and after, boots on run2 against the same databases, read at the ready line (worldserver RSS, `/proc`):

  | Build | Purpose entries | Boot index | RSS at ready |
  |---|---|---|---|
  | `tbf/world/w4a2` as reviewed (72,303,248 bytes) | 224,997 | kept for the process | 4,543,836 KiB |
  | this tree (72,588,184 bytes), quiet boot | 7,792 | released after the build | 4,533,464 KiB |
  | this tree, boot under build load | 7,792 | released after the build | 4,547,572 KiB |

  The data volumes are exact: 217,205 purpose rows gone, and a boot index of 123,558 points freed after the build in place of
  the 67,420 the destinations keep. The RSS at the ready line does **not** show that difference: the two fixed runs alone span
  14 MiB with load (they are above and below the A2 reading), so the freed copy is not visible in process RSS at that point -
  the allocator keeps the pages. A heap-accounting probe (`mallinfo2`) was tried first and reads 0 under this core's allocator
  setup, so it was dropped rather than quoted.

## 9. Gates

| Gate | Result |
|---|---|
| Census, `forever/SLICE-W4.txt` (273 files), core 25fc7ca | `files 273  compile clean 273  with errors 0  error places 0 (in 0 files)  includes of a header ForeverCore does not have 0` (run folder `census-runs/w4a1-fix-s8-SLICE-W4-20261120T190128Z`, also in `forever/docs/CENSUS.md`) |
| Module ON (`TORTOISEBOTS_SLICE1=ON`, `tortoisebots`), slot 7 | `CMAKE_RC=0 MAKE_RC=0` (44 s after the last code edit) |
| Worldserver link ON | `CMAKE_RC=0 MAKE_RC=0`, 72,588,184 bytes (the binary installed for the live check) |
| Module OFF (`TORTOISEBOTS_SLICE1=OFF`, `tortoisebots`) | `CMAKE_RC=0 MAKE_RC=0` (11 steps: `TbCompat_WL4.cpp` filtered, no slice file compiled) |
| Generator / registry | `slice_gen_context.py --check` up to date; `slice_registry_check.py` `(a)=0` (223 keys to register or stub later) |
| Policy tests (standalone, g++ C++17) | `test_zone_level_policy` 13 checks 0 failed; `test_travel_purpose_gate_policy` 22 checks 0 failed; `tools/test_travel_point_key.cpp` all checks passed (fails to compile before the key fix) |
| Wiring | `forever/tools/test_wl4_wiring.py` 30 tests OK; all `forever/tools/test_*wiring.py` 179 tests OK (6 skipped); `tools.test_forever_no_direct_queries` + `tools.test_guild_founding_wiring` 8 OK |
| `tools/verify_all.sh` (laptop) | OK through step 6 (OKF, command table, surface, wiring, unit tests, 483 standalone tests, decision trail); step 7 fails with `TortoiseBots host-contract check failed: required core file is missing: src/game/SessionTransport.h`, the same failure `tbf/main` (10efad5bb) gives against the same `../tortoise-wow` checkout - no new failure |
| Turtle `vm-build` | `COMPILE_SECONDS=1318 CMAKE_RC=0 MAKE_RC=0`, mangosd and realmd written (sha256 `449c92c6e48794e5...`/`bbb438846082b349...`), then `COMPILE_SECONDS=19 CMAKE_RC=0 MAKE_RC=0` on the committed tree (`sha=712c80ae7`, statics identical) |
| Turtle `vm-test tb` | `RESULT: PASS kind=tb wt=wt-w4a1 sha=91f94b5cb dirty=0 files=4027 rc=0 vm_duration=1204s` and again `... sha=b84e28b84 dirty=0 ... vm_duration=1515s` (logs `wt-w4a1-20261120-200941-1153988-a64d.log`, `wt-w4a1-20261120-203011-1313980-9a9e.log`); the only later change is one word of the runtime check's scenario label |
| Runtime check (section 7) | `RESULT: PASS` in one run on the slot-7 binary: scenarios a, b, c and final, 33 checks, rev `91f94b5cb`'s harness; the reviewed tree (`07a57c8b2`) fails scenario a with `HARNESS_RC=1` (5 checks) |

## 10. Live (forever-run2, our binary in place, console only)

The slot-7 build (72,588,184 bytes) installed over `bin/worldserver` (backup `bin/worldserver.before-w4a1-graph`, restored and
boot-checked afterwards), the server lock held for the whole run, four boots through `forever/tools/travel_graph_boot_check.sh`:

| Scenario | Result |
|---|---|
| `a` no tables | ready, 0 assertions; guard refused the node query; one absent-graph line; no node read; no `Calculating mapoffset`; destinations still built |
| `b` empty tables | ready, 0 assertions; one `travel-node dataset is empty` line; no guard refusal; no generation |
| `c` 3 nodes + a link to a missing node id + its 5 path points | ready, 0 assertions; `>> Loaded 3 travelNodes.`, `>> Loaded 2 travelNode paths.`, `>> Loaded 5 travelNode paths points.`, `-Calculating mapoffset`; the unknown target skipped |
| `final` no tables | ready, 0 assertions; one absent-graph line; `tb status` -> `TortoiseBots: enabled, 0 bot session(s)`; worldserver RSS at ready 4,533,464-4,547,572 KiB across runs (~4.3 GiB, the core's own world load included; the A2 build measured 4,543,836 KiB, section 8) |

The fixed boot's destination figures (unloaded server, bot logging off):

```
>> travel destination index: 20000 spawns, 1552 ms ... 120000 spawns, 1978 ms
>> Loaded 15684 travel destination spawn entries (123558 spawns on maps 0/1, 24551 skipped on other maps) in 1978 ms
>> Loaded 7792 travel destination purpose entries in 34 ms          (was 224,997)
>> travel destination index released: 15684 entries, 123558 spawn points
>> travel destinations keep 67420 spawn points
Loading travel nodes.
TortoiseBots: world table `ai_playerbot_travelnode` is missing, or lacks a column this query names: not read
TortoiseBots: the travel-node tables are missing or incomplete; the travel graph stays empty and travel does nothing
```

The two absent-graph lines are the guard's refusal (so no query reached a missing table) and the outcome line; both at INFO,
the second in `logs/bot.log` (the bot brain's `BotLog`), which is why section 7's check reads both log files. The boot is
ready in about 60 s including the destination build; nothing about the absent graph delays or fails it.
