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
| option-ON `worldserver` (`FOREVER_CMAKE_EXTRA=-DTORTOISEBOTS_SLICE1=1`) | `CMAKE_RC=0 MAKE_RC=0`; `nm` on `libtortoisebots.a`: `cmd::FillFeeds`, `UpdateFeeds`, `LoadFeeds`, `FeedsActive` defined |
| option-OFF `worldserver` | `CMAKE_RC=0 MAKE_RC=0`; the archive holds the four no-op stubs only (no boss cache, no sweeps) |
| `forever/tools/cmd_feeds_check.cpp` (VM, g++) | `cmd_feeds_check: 65 checks, 0 failed` |
| `test_cmd_p4_wiring.py` with `CMD_P4_MUTATE=1` (VM) | 10 tests OK; the three mutations (`cap`, `boss`, `window`) each FAIL the check |
| `test_cmd_p1_wiring.py`, `test_cmd_p2_wiring.py`, `test_run_review_wiring.py`, `test_dash_runs_wiring.py`, `test_slice4_d4_wiring.py` | OK |
| `slice_registry_check.py`, `slice_gen_context.py --check` | `summary: (a)=0  (b)=224`; `SliceAiObjectContext.cpp is up to date` |
| `tools/verify_okf.py` | `OKF verification PASSED (60 nodes validated, graph edges consistent).` |
| Turtle `vm-build` / `vm-test tb` | not run: no file outside `forever/` changed |

## Live (run2)

Pending.
