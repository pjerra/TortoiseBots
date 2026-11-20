# Commander port, package P4: the card's map layers and the live position report

Spec: `~/TortoiseBots-audit/FOREVER-COMMANDER-PORT-SPEC.md`, the P4 row of section 5, its tests (section 7) and its live row (section 8); decisions D5
and D6 of "Lead decisions". Branch `cmd/p4` from `tbf/main` (P1 and P2 merged). Core pin 25fc7ca.

## What it is

The Commander card's map layers and the live positions, as the Turtle collector built them (T `commands/Commander.cpp:15653-16636`, `:17052-17123`),
against what ForeverCore holds:

* **mobs**: the hostiles near a binding's lead, from one grid sweep (`AnyUnfriendlyUnitInObjectRangeCheck` + `Cell::VisitAllObjects`), nearest first,
  **capped at 24 per binding** (`CommanderTelemetry::kMaxMobsPerBinding`). Each row carries name, entry, health percent, place, victim, reaction to the
  bots, critter/civilian/guard/unattackable, level, rank (boss/elite/rare; a plan boss or an `ai_dungeon_boss` entry reads 'b' through
  `DungeonPlanTable::IsBossEntry`), crowd control and the spell it casts. The first 16 boss/elite rows also carry the overlay: facing, the current
  cast or channel with its time left, total and unit target, the spell's area (`runtime/SpellShape.h`), a breath window when the cast never reaches a
  slot (`runtime/BossWindowSignal.h`), the unit's own auras and its state flags.
* **objects**: doors, chests, buttons/levers, goobers and rituals around the lead (one `AllGameObjectsWithEntryInRange` sweep), nearest first, capped
  at 16, each with type and state (ready/active/used).
* **bosses**: the module's own spawn cache, built once over the core's creature spawn data (`ObjectMgr::GetAllCreatureData()`, one pass) plus the plan
  boss steps for entries with no spawn row. A map's roster is never cut short: over the per-binding cap it is not sent at all. Each boss's state comes
  from its spawn rows on the lead's instance — the creature alive, its corpse, or the respawn time the map still holds for the spawn — and the
  instance script's encounter state (see "GetBossState" below), through the module's `BossKillLatch` so a killed boss read 'u' later stays dead.
* **bot extras**: per bound bot its state (combat/not/dead), the spell it casts and that spell's unit target, crowd control, drinking/eating, moving,
  its target's name, the boss debuffs it carries (negative auras from outside its group) whether it is charmed, and the ready check (raid buff bits, key
  cooldowns ready/known/longest, cached 10 s out of combat, sent as cached in combat).
* **COMMANDER_POS**: one datagram per standing binding every `TortoiseBots.Commander.PosMs` (default **100 ms**), through the dash-runs sender shim
  (`dash/TbDashLink.h`, D6: no second socket): the bots of that binding's last full report (map, x/y/z, facing, health, alive) and the enemies that
  report listed, resolved by guid on the lead's map. Over `kMaxPosBindings` bindings the window rotates.
* **DUNGEON_MOBS `mobs`**: already filled by the roster sender (`TbDashboard.cpp` `SendDungeonMobs`, the dashboard owner's file, untouched); nothing to
  add here.

A layer is reported only when its scan ran. A binding whose lead is not in the world keeps `mobs`, `objects` and `bosses` unreported, so the wire
carries null (the dashboard greys the layer out), never `[]` — the failing case of spec section 7. `[]` means the scan ran and found nothing.

## Files

| File | What |
|---|---|
| `forever/src/host/cmd/TbCmdFeedsPure.h` | pure: `KeepNearest` (nearest first, guid tie), the game-object kind/state letters, `BossState(spawnState, encounterDone)`, `NextPosWindow` (rotation), the clamps. Standard library + `runtime/CommanderTelemetry.h` only |
| `forever/src/host/cmd/TbCmdFeeds.{h,cpp}` | the collector: the two clocks (`FillFeeds` in the dash-runs cycle, `UpdateFeeds` on the position clock), the boss spawn cache and its states, the sweeps, the bot extras, the position datagrams. With the option OFF every function is a no-op |
| `forever/tools/cmd_feeds_check.cpp` | the standalone check (65 checks): the caps, the letters, the boss states, the rotation, and the real COMMANDERS/COMMANDER_POS builders for the null-vs-empty rule, the extras' keys and the position caps |
| `forever/tools/test_cmd_p4_wiring.py` | wiring pins + three mutations (`cap`, `boss`, `window`) under `CMD_P4_MUTATE=1` (run on the VM) |
| `forever/conf/tortoisebots.conf.dist` | `TortoiseBots.Commander.Feeds` (1), `.MobRange` (60), `.ObjectRange` (40), `.PosMs` (100), documented with the defaults the code falls back to |

## Shared lines touched (appended only)

| File | Line | Why |
|---|---|---|
| `forever/src/TbScripts.cpp` | the include of `host/cmd/TbCmdFeeds.h`; `cmd::LoadFeeds()` in `OnConfigLoad` and in `OnWorldInitialized` (after `run::LoadPlans()`); `cmd::UpdateFeeds(diff)` in `OnBeforeMapsUpdate` after `DashRuns::Update` | the two clocks and the config |
| `forever/src/dash/TbDashRuns.cpp` | the include; `cmd::FillFeeds(bindings);` after `cmd::FillRunOrders(bindings);` | the card's layers before the datagram is built |
| `forever/conf/tortoisebots.conf.dist` | the four keys | configuration |

No other file was changed; `TbCmdFeeds.cpp` is new and owns everything else.

## Rules as built

* **No database call.** The feeds make none (the wiring test strips comments and greps six forbidden names in the three files); the boss roster rides
  the core's in-memory spawn data, read once.
* **No raw pointer across a tick.** The state holds guids (`std::vector<ObjectGuid> mobs`), guid counters, names and flags; every unit is resolved
  again on the tick that reads it. The `ObjectGuid`s of the last report are the only unit reference kept, and a guid that left the world resolves to
  null.
* **The sweeps are the only scans, and they are the collector's.** One hostile sweep (radius `MobRange`, 60) and one object sweep (`ObjectRange`, 40)
  per binding per 2 s cycle; the position clock resolves the last report's guids only (no sweep). Both clocks log their cost every 60 s.
* **The plan generation rebuilds the roster.** `.tb run reloadplans` replaces the plan table; the feed notices `DungeonPlanTableGeneration()` change on
  the next cycle and rebuilds the spawn cache once (not in the reload command: that file belongs to the run package).
* **The two clocks are independent.** `FillFeeds` is the dash-runs collector's cycle (2 s, `TortoiseBots.Dashboard.IntervalMs`); `UpdateFeeds` has its
  own `PosMs` clock and sends nothing while no binding stands or the sender is closed.

## GetBossState: what the pin lets the module map

The spec's P4 row names `InstanceScript::GetBossState`. At this pin the call is **index-based** (`InstanceScript.h:270`, `id < bosses.size()`) and the
core exposes no entry -> boss-index map: `GetBossDungeonEncounter(Creature const*)` only answers for a creature whose AI is a `BossAI`, and a boss
whose grid is unloaded has no creature at all — exactly the case the encounter state would help. The roster is kept by creature entry (that is what
the dashboard matches plan steps and mobs by), so the module reads the encounter state only where it can: for a loaded boss creature it asks
`GetBossDungeonEncounter(creature)` and `IsEncounterCompleted`, and a DONE encounter marks the boss dead when the spawn read did not see it alive.
The killed-but-unloaded case is covered by the respawn time the map holds for the spawn (`Map::GetCreatureRespawnTime`), the H4-proven rule, plus the
kill latch. This is the same "alive beats dead beats unknown" order the Turtle builder uses.

## What the dashboard side still needs (requests, not edits)

`tools/observability` is the dashboard owner's and was not edited.

1. **The daemon drops `bosses`/`bosses_map`.** `internal/model/types.go` `CommanderBinding` has no such fields, so the roster the module now sends is
   decoded away. To draw the bosses the live row names, add the two fields (a list of `{e,n,x,y,s,p}`; the JSON is already written by the Turtle
   builder) and expose them on the Commanders tab.
2. **The dash-runs collector's `Active()` and the feeds.** The position clock follows the card's cycle (`Dashboard.Runs`), because the daemon only
   merges positions into a binding the COMMANDERS report already carries. With `Dashboard.Runs = 0` there is no card and no position report.

## Limits (they make the card show LESS)

* The Turtle view snapshot is cut (D5), so a bot's newest action and its verdict (`act`/`res`) and the AI state 'r' (reaction) are not sent; the state
  letter is combat/not/dead only.
* The ready check's **consumable bits are always 0**: `SpellMgr::GetSpellElixirMask` is a Turtle table and ForeverCore has none, so a flask, elixir or
  well-fed aura cannot be recognised (the potion family still sets the elixir bit). The row under-reports consumables; it never invents them.
* The mob list is the grid around the lead, not the bots' perception lists (the Turtle build read those). A hostile the bots see but the sweep does
  not (out of `MobRange`) is not listed.
* A boss whose grid is unloaded, whose corpse is gone and whose respawn time has expired reads 'u' (unknown) once its latch expired (10 min without a
  binding on the instance).
* The position report carries at most 20 bindings per cycle (a rotation) and at most 40 bots and 24 enemies per binding; the readers see every binding
  within a few cycles.
* The first position report of a binding waits for the next card cycle (the report's bindings come from that cycle's list).

## Gates (slot 5 on the build VM, core pin 25fc7ca; `src-5` seeded from the pin-move tree)

| Gate | Result |
|---|---|
| census, list `forever/SLICE5.txt` (P4 adds no legacy file) | `files 176  compile clean 176  with errors 0  error places 0` |
| option-ON `worldserver` (`FOREVER_CMAKE_EXTRA=-DTORTOISEBOTS_SLICE1=1`) | `CMAKE_RC=0 MAKE_RC=0` (72,066,032-byte binary, md5 `e24b68ba…`); `nm` on `libtortoisebots.a`: `cmd::FillFeeds`, `UpdateFeeds`, `LoadFeeds`, `FeedsActive` defined |
| option-OFF `worldserver` | `CMAKE_RC=0 MAKE_RC=0` (55,972,168 bytes); the archive holds the four no-op stubs only (no boss cache, no sweeps) |
| `forever/tools/cmd_feeds_check.cpp` (VM, g++) | `cmd_feeds_check: 65 checks, 0 failed` |
| `test_cmd_p4_wiring.py` with `CMD_P4_MUTATE=1` (VM) | 10 tests OK; the three mutations (`cap`, `boss`, `window`) each FAIL the check |
| `test_cmd_p1_wiring.py`, `test_cmd_p2_wiring.py`, `test_run_review_wiring.py`, `test_dash_runs_wiring.py`, `test_slice4_d4_wiring.py` | OK |
| `slice_registry_check.py`, `slice_gen_context.py --check` | `summary: (a)=0  (b)=224`; `SliceAiObjectContext.cpp is up to date` |
| `tools/verify_okf.py` | `OKF verification PASSED (60 nodes validated, graph edges consistent).` |
| Turtle `vm-build` / `vm-test tb` | not run: no file outside `forever/` changed |

## Live (run2, ForeverCore test server 2)

The ON binary of slot 5 was installed as `bin/worldserver` (the release saved as `bin/worldserver.pre-cmdp4`), `sqlsrc` checked out at
25fc7ca (the pin's SQL), the databases dumped to `/mnt/forever/p4-dump/run2-free-20261120T155215Z.sql.gz` before the test. Boot: `ready`, 0
`ASSERT`/`ABORT`/`Segmentation`, `TortoiseBots: commander boss spawn cache built in 9 ms for 12 maps`. The module's own notices named the four new
keys as missing from the file and used their defaults (Feeds 1, MobRange 60, ObjectRange 40, PosMs 100).

A UDP listener inside the world container (the module's sender writes 127.0.0.1:9195; the container has its own loopback) captured:

| Check | Result |
|---|---|
| binding | `commander=Mcmd lead=Mtank group=1 bound=5 bots=5`, 5 `member` records, all five with an extra row (`st`, `rdy` with `cr`/`cn` = 5..8) |
| COMMANDERS cadence | every 2.0 s (30 datagrams in 60 s) |
| mobs, RFC instance | `mobs_map=389`, 4 rows at the instance entrance (Earthborer 11320, Molten Elemental 11321; `rank=e`, `o`, `au`), 21 rows next to Oggleflint |
| mobs of a boss | Oggleflint 11517 read `rank=b`, alive, in the list beside its pack |
| bosses, RFC | `(11517,'a',p=1) (11520,'a',p=1) (11518,'a',p=1) (11519,'a',p=1)` next to Oggleflint; at the instance entrance the four are `'u'` (their grids are not loaded and no respawn time is held: unknown, as designed) |
| objects, open world | map 0 next to the Food Crates near (-706,-1683): `('c','r','Food Crate',3696)`, `(3698)`, `(3696)`; `objects:[]` at the RFC spots (the RFC game objects in the world DB are types 5 and 8, which the dashboard does not draw) |
| bosses, open world | `bosses` absent (null): the lead is not on a dungeon map |
| COMMANDER_POS | 595 datagrams in 60 s for the one binding: **median gap 100 ms, p90 101 ms**, 5 bots and 4 enemies per frame |
| feed cost (the module's own 60 s line) | fill 47-103 us each on average, 204 us at most; positions 112-166 us each on average (one 2.66 ms spike while the party logged in) — the fill stays under the spec's 300 us |
| run tick with the feeds on | `.tb run status all`: `ticks=25 tick_avg_us=343 tick_max_us=4078 facts_avg_us=230` — inside the 326-366 us band of the run package's own reports (S6-M, S8-M), and the feed runs after `run::Update` in the same tick |

Found live and fixed in the last commit: the boss state read the creature by a rebuilt `ObjectGuid`, which this core does not match (the guid carries a
realm id); `(11517..11519)` all read 'u' beside a live Oggleflint. `Map::GetCreatureBySpawnId(spawnId)` (the API built for exactly this) fixed it, and
the next run showed all four `'a'` with `p=1`.

Restore: the release binary (`bin/worldserver.pre-cmdp4`, md5 `52364138…`) back in `bin/`, `sqlsrc` back at 6517af05 and the databases re-imported
from the dump above (`IMPORT_RC=0`; 28 characters, no `auctionhousebot*` tables, no 70959 `build_info` row, no `tb_commander_binding`: the state the
server had before the test). The boot check of the restored server could not be made: another worker installed their own binary (`00f94fc2…`, 17:12)
and started `forever-world2` during the restore, so the server now runs their build. Nothing else on that server was changed by this package.
