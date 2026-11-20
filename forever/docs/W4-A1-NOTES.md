# W4-A1: the travel node graph on ForeverCore

Round W4-A1 of `FOREVER-W0-W4-SPEC.md` part 2 (round table). Files: `ai/playerbot/TravelNode.cpp` and its both-core pair
(`forever/src/compat/TbCompat_WL4n.h` + `ai/cmangos-compat-wl4n.h`, wrapper `ai/playerbot/TbCompatWL4n.h`); the `TravelNodeMap` /
`TravelPath` stand-ins of the Travel block in `forever/src/slice/SliceLinkStubs.cpp` are gone; `forever/SLICE-W4.txt` is the
merged W1 list plus this round's legacy file, and `CMakeLists.txt` reads it. Zero core changes.

## 1. What runs

| Piece | Where |
|---|---|
| The node store: `ai_playerbot_travelnode`, `_link`, `_path` read through the schema guard | `TravelNodeMap::loadNodeStore` |
| A missing table or column: empty graph, one line, travel does nothing | `emptyGraph` in `loadNodeStore` |
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

- `forever/src/slice/SliceLinkStubs.cpp`: the six TravelNodeMap/TravelPath stand-in lines deleted; one new line,
  `TravelMgr::LoadMapTransfers` (TravelMgr.cpp is W4-A2; TravelNode.cpp's generator calls it). The destination stand-ins, the
  slice-9 block and the W1 block are untouched.
- `CMakeLists.txt`: `TB_SLICE_LIST` -> `forever/SLICE-W4.txt` (comment and FATAL_ERROR text updated).
- `forever/SLICE-W4.txt`: new; the merged W1 list line for line, then `ai/playerbot/TravelNode.cpp`.
- `forever/tools/slice_registry_check.py`: `SLICE_LIST` -> `SLICE-W4.txt` (the list CMake reads).
- `forever/tools/test_w1_wiring.py`, `test_slice9_l9_wiring.py`: the list pointer they pin moved from `SLICE-W1.txt` to
  `SLICE-W4.txt` (the W4 list starts with the W1 list, so their file checks are unchanged).
- `tools/test_forever_no_direct_queries.py`: the saver's one raw write (`TbCompat_WL4n.h`, after the S5 schema probe) is named
  with its count, like the two existing guard files.

## 5. Gates

| Gate | Result |
|---|---|
| Census, `forever/SLICE-W4.txt` (271 files), core 25fc7ca | `files 271  compile clean 271  with errors 0  error places 0 (in 0 files)  includes of a header ForeverCore does not have 0` (run `wt-w4a1-s7-SLICE-W4-20261120T164613Z`) |
| Census after the last edits (TravelNode.cpp only, final) | `files 1  compile clean 1  with errors 0  error places 0` (runs `...T170248Z`, `...T172825Z`) |
| Module ON (`TORTOISEBOTS_SLICE1=ON`, `tortoisebots`), slot 7 | `CMAKE_RC=0 MAKE_RC=0` (final tree, `worldserver` link too) |
| Module OFF (`TORTOISEBOTS_SLICE1=OFF`, `tortoisebots`) | `CMAKE_RC=0 MAKE_RC=0` |
| Worldserver link ON | `CMAKE_RC=0 MAKE_RC=0` (72,340,128 bytes, 18:04; rebuilt after `DirectExecute` too, 58 s) |
| Generator / registry | `slice_gen_context.py --check` up to date; `slice_registry_check.py` `(a)=0` (219 keys to register or stub later, from 218: `home bind` is read only on the Turtle side now) |
| Policy tests | none added this round (no policy file); the standalone suite runs in `verify_all` |
| Wiring | `test_wl4_wiring.py` 14 OK; `test_w1_wiring.py` + `test_slice9_l9_wiring.py` + `test_wl4_wiring.py` 45 OK; `tools.test_forever_no_direct_queries` 5 OK |
| `tools/verify_all.sh` | `RESULT: PASS kind=tb wt=wt-w4a1 sha=177ab5098 dirty=0 files=4015 rc=0 vm_duration=848s` (the standing `action_loop` / `Mage stay` scenario is not a test failure, `forever/docs/SLICE1-R4P-NOTES.md`). The only later code delta is `Execute` -> `DirectExecute` in the pair; the naming test, the census and the ON/OFF builds cover it. |
| Turtle `vm-build` | `CMAKE_RC=0 MAKE_RC=0` (final commit `1875ecee2`, no-op 9 s) |
| Turtle `vm-test tb` | `RESULT: PASS` on `177ab5098` (the same run above; the later commit touches only the ForeverCore pair and these notes) |

## 6. Live (forever-run2, our binary in place, console only)

The node store loader is not reached yet: `TravelMgr.cpp` (and with it `LoadQuestTravelTable`, which calls `loadNodeStore`) is
round W4-A2, so the boot cannot show the graph. What the boot does show, with the final slot-7 build (72,340,128 bytes, 18:04):

- `worldserver-daemon) ready...`, `TortoiseBots (ForeverCore port, spike): enabled`, no `ASSERTION FAILED` (0 in the log).
- `.tb status` -> `TortoiseBots: enabled, 0 bot session(s).`
- `.tb perf` -> `8806 ticks; world update time avg 1.13 ms, worst 2 ms; module (sessions + engines) avg 0 us, worst 313 us`.
- `server shutdown 1` -> clean halt, container gone.
- The world DB has none of the three `ai_playerbot_travel*` tables. The boot does not query them (the loader is not called), so
  the guard's missing-table path is proved by the census/store checks, not by this boot; it will be exercised by A2's loader.

Binary bookkeeping (a mistake, reported in full in the final report): `./world.sh stop` blocked on the console FIFO before the
`cp bin/worldserver bin/worldserver.pre-w4a1` step, so the pre-existing binary was overwritten without a backup. The era's
backups all carry the same 65,678,144-byte build (`worldserver.pre-w0a` among them); that file was put back and boot-checked
(ready, `.tb status` enabled, 0 assertions, clean shutdown). The round that installed the 17:10 binary should reinstall its own
if it was newer than that build.
