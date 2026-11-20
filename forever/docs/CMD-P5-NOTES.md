# Commander port, package P5: the fight debugger

Spec: `~/TortoiseBots-audit/FOREVER-COMMANDER-PORT-SPEC.md` (P5 row in section 5; sections 6, 7 and 8; decisions D2 and D6). Branch `cmd/p5` from
`tbf/main` 6e8e0494a (core pin 25fc7ca; P1 and P2 merged). The fight tab reads the same v12 datagrams the Turtle dashboard does: nothing on the
dashboard side was edited.

## What it is

The Turtle fight monitor (`runtime/CombatMonitorService.cpp` + `runtime/CombatMonitorPolicy.h` + `runtime/BoundFightPolicy.h`) records one fight
per bound group: damage done and taken, healing and overhealing, casts per spell, deaths with their last hits, the engaged creatures, the
fight's boss and his phases, and (with the timeline key) his health and victims. This package builds that file on ForeverCore, feeds it from the
commander cards, and sends what it builds (`COMBAT_FIGHT`, `COMBAT_EVENTS`, and the `combat` object of `COMMANDERS`) through the dashboard link
the dash-runs collector already uses.

* A **commander card** (P1/P2) is fed once a second while its lead stands in a live group: the members, their combat state, the run's plan
  context, the focus unit's tracked debuffs. A card with no live group (a party-less binding, group counter 0) is not fed: the books are keyed
  by the group's session counter and there is no counter to key it with.
* The **hooks** are the three of ForeverCore's `UnitScript` an aggregate needs (damage, heal outcome, death) and the `PlayerScript` cast hook,
  registered beside the module's other scripts. There is no enter-combat hook and no aura hook on this core: the sample's in-combat state starts
  a fight, and the timeline's aura edges come from a 1 Hz diff of each member's applied auras.
* The **boss** is the run's plan boss when he is among the units being fought, else the boss-flagged engaged creature with the most maximum
  health (the shared `BoundFight::PickBoss`). The engaged set is the members' attackers plus the units their combat managers name: a boss the
  party only attacks is in it, and no grid or world scan runs.
* **Enemy casts** are polled at 250 ms from the engaged units of the two ranks the dashboard draws apart (boss `b`, elite `e`), at most eight per
  card, ranked from the guid's own entry before any unit is resolved.
* The **fight window** (`combat` in `COMMANDERS`) is attached to every card `AppendBindings` appends; nothing else in the card changed.
* **Nothing keeps an object pointer across a tick.** A card's bots are guid counters, the fight's boss is an `ObjectGuid`, and units are
  resolved afresh per pass.

## Files (new unless marked)

| File | What |
|---|---|
| `forever/src/host/cmd/TbCmdCombatPure.h` | pure (stdlib + `runtime/CombatMonitorPolicy.h`): `Feedable`, `Due`, `Rank`/`CastRank`/`CastWatched`, `PickFocus`, `AuraEdges`, `AllDead`, `RoleOf`, `SortUnique`, the pass cadences (1 Hz sample, 250 ms cast poll, 100 ms event drain, 250 ms event gap, 8 units per poll) |
| `forever/src/host/cmd/TbCmdCombat.{h,cpp}` | the ForeverCore side: the two script objects, the sampler, the boss pick and timeline sample, the cast poll, `COMBAT_FIGHT`/`COMBAT_EVENTS` through `DashRuns::link`, `FillWindow`, the cost line. Engine-only; without `TORTOISEBOTS_SLICE1` the file is three no-ops |
| `forever/src/compat/TbCompat_C5.h`, `ai/cmangos-compat-c5.h`, `ai/playerbot/TbCompatC5.h` | the monitor's one world read (`ai_combat_aura_watch`, through `WorldQueryCompat`) and its two log lines, one pair of helpers for both cores |
| `forever/tools/cmd_combat_check.cpp` | the standalone check: the pure half, the golden fight JSON, a wipe and a leaving-combat end that each file a fight and change the id, the live stream's fight id, the timeline sample |
| `forever/tools/test_cmd_p5_wiring.py` | wiring pins; with `CMD_P5_MUTATE=1` four mutations of the pure half must FAIL the check |
| `runtime/CombatMonitorService.cpp` (shared) | the guarded watch read and the two log lines; nothing else |
| `forever/docs/CMD-P5-NOTES.md` | this file |

## Shared lines touched (the brief asked for every one)

| File | Lines | Why |
|---|---|---|
| `CMakeLists.txt` | +4 in the `TORTOISEBOTS_SLICE1` block | `target_sources(... runtime/CombatMonitorService.cpp)`: the Turtle TU is compiled into the engine build (the option-OFF build has neither the file nor its stubs) |
| `forever/src/TbScripts.cpp` | +1 include, +1 tick line after `cmd::Maintain()`, +1 `RegisterScripts()` in `AddSC_tortoisebots` | the world tick and the script rows |
| `forever/src/host/TbHostTracing.cpp` | −1 include, −18 lines (the two stubs), +2 comment lines | the stubs would collide with the compiled TU; the file's head now says the monitor is P5's |
| `forever/src/host/cmd/TbCmdBinding.h` | +5 (one declaration, `StandingCards`) | the sampler reads the same cards `.tb commander status` shows; no other change to P1/P2's files |
| `forever/src/host/cmd/TbCmdBinding.cpp` | +1 include, +1 call in `AppendBindings` (`combat::FillWindow(out.back())`), +46 (the `StandingCards` projection), +1 stub in the option-OFF section | the window and the card read. `Read()` itself is untouched: `StandingCards` repeats its assembly without the view work and writes no state |
| `runtime/CombatMonitorService.cpp` | 1 include swapped (`TbCompatC5.h`), 1 query call (`AuraWatchRowsCompat()`), 2 log calls (`CombatMonitorLogCompat`) | the file is shared with Turtle; its payloads and books are unchanged |
| `ai/playerbot/UnportedFeatures.h` | 1 row's text | `Tracing` no longer names the combat monitor as switched off |

No line of another package's files (`forever/src/dash/`, `forever/src/TbDashboard.*`, `tools/observability/`, `host/run/*`) was edited.

## Rules as built

* **One gate, the Turtle one.** `sPlayerbotAIConfig.combatMonitor && DashRuns::link::Active()`: the engine's own key (off by default in
  `aiplayerbot.conf.dist`) and the dashboard feed. The link is the dash-runs sender shim (D6: the roster sender's socket once its accessors
  land; `TbDashLink.cpp` is the only file that changes then). Off: every book is forgotten and no pass runs.
* **Clocks, not work per tick.** The three passes are absolute-time comparisons (`Due`, unsigned, correct across the world timer's wrap). The
  cost line (`bound fights 60s: N samples, avg N us ...`) is logged once a minute while cards are fed.
* **A fallback does less.** A card with no live group is not fed; a lead that leaves its group stops the feed and past
  `BoundFight::kLeadGoneGraceMs` closes its open fight as unfinished; a member that is not in the world is skipped. With the aura watch table
  absent (this core ships none) the timeline records no aura rows and the fight record says so with an empty list.
* **The watch table.** `LoadTimelineWatch` runs at the engine config load and now reads through `WorldQueryCompat`: a missing table logs one
  line and returns no rows. An unguarded read would abort this core (`MySQLConnection.cpp`). No SQL ships with this package (the ids are the
  Turtle ZG ones and were not verified against Classic data).
* **Aura edges without a hook.** ForeverCore's `UnitScript` has no apply/remove. Each member's non-passive aura ids are sampled with the fight
  and diffed (`AuraEdges`); the service filters to its watched list. The fight's first sample is the baseline, so auras already up at the start
  are not rows. An aura shorter than a second is missed (the Turtle hook saw it): less, never invented.
* **Boss and phases.** `NoteBoss`/`AdoptBoss` keep the fight's first pick. The boss unit's entry is re-read every sample; a script-changed
  entry is reported as a phase (`NoteBossForm`), and his state (alive, in combat, evading, full health, home) is reported for the evade
  outcome. `ScriptChangedFrom` reads the guid's own entry, which is ForeverCore's one spawn entry (Turtle also knew a row's `id1..id4`).
* **The heal numbers.** `OnHealOutcome` gives the raw heal and the part that landed: the service gets `effective` and `raw - effective` (the
  Turtle hook derived the overheal from the health before and after).
* **Threads.** The hooks run on map threads and take no lock of this file's: the service's member table (one atomic bitset load) routes them.
  The sampler, the cast poll and the drains run on the world thread.
* **Two commanders, one group.** If two cards resolve to one group counter, they share one book and the later sample's member list wins. P1's
  cards cannot make that happen from `.tb master` (one commander per bot), and the run rebinds its members to the tank; it is named here as a
  limit, not handled.

## What the dashboard needs

Nothing edited on the dashboard side. Three facts for its owner:

1. `COMBAT_FIGHT` and `COMBAT_EVENTS` now come from the module's commander books with `commander` = the card's commander, `group` = the
   group's session counter, and the fight id `<group>-<n>`. The roster sender's own `COMBAT_FIGHT` (the dungeon copy feed, commander
   `dgn:<map>:<instance>`) is untouched; the store keys fights by (commander, session, id), so both lists coexist.
2. `COMBAT_EVENTS` arrives from the dash-runs link's session, at most one datagram per group per 250 ms, with the service's 5 s keepalive while a
   fight is open.
3. The `combat` object of `COMMANDERS` is filled for every card whose group has a book; a fight that ended clears it on the next cycle.

## Limits (each makes the tab show less)

* A party-less binding (group counter 0) has no fight record: nothing to key a book with.
* The timeline's aura rows are sampled at 1 Hz, so a short aura is missed.
* The aura watch table is not shipped: `CombatTimeline` on this core records health, victims and phases, and no aura rows.
* A harvested `ScriptChangedFrom` sees one spawn entry (ForeverCore's spawn data has one), not Turtle's alternative list.
* The cast poll reads `CURRENT_GENERIC_SPELL` and `CURRENT_CHANNELED_SPELL` only, up to eight engaged units per card.
* The 1 Hz sample reads the live cards again (`StandingCards`, a projection of the card read). It does not write the card state.

## Tests

`forever/tools/cmd_combat_check.cpp` (g++, on the build VM): the pure half; the golden fight; a wipe; leaving combat; the live stream's fight id
and the timeline sample. `forever/tools/test_cmd_p5_wiring.py` pins the stub's removal, the compiled TU and its option gating, the tick and the
script registration, the window call, the no-database rule, the gate and the three clocks, the payload path, the guid-only state, the
non-mutating card read and the option-OFF answers; with `CMD_P5_MUTATE=1` its four mutations (`feed0`, `focus`, `applyonly`, `alwaysdue`) must
each FAIL the check.

## Gates (slot 6, core pin 25fc7ca)

| Gate | Result |
|---|---|
| census, `forever/SLICE9.txt` (P5 adds no legacy file) | `files 265  compile clean 265  with errors 0  error places 0 (in 0 files)  includes of a header ForeverCore does not have 0` |
| option-ON `worldserver` (`FOREVER_CMAKE_EXTRA=-DTORTOISEBOTS_SLICE1=1`) | `CMAKE_RC=0 MAKE_RC=0` (72,294,016-byte binary); `nm` on `libtortoisebots.a`: `cmd::combat::{Update,FillWindow,RegisterScripts}` and `CombatMonitorService::{Instance,Refresh,LoadTimelineWatch}` are defined (`T`) and the archive holds `CombatMonitorService.cpp.o` |
| option-OFF `tortoisebots` | `CMAKE_RC=0 MAKE_RC=0`; the archive holds `TbCmdCombat.cpp.o` (the three no-ops) and no `CombatMonitorService.cpp.o` |
| `forever/tools/cmd_combat_check.cpp` (VM, g++) | `cmd_combat_check: 56 checks, 0 failed` |
| `forever/tools/test_cmd_p5_wiring.py` with `CMD_P5_MUTATE=1` (VM) | 12 tests OK; the four mutations (`feed0`, `focus`, `applyonly`, `alwaysdue`) each FAIL the check |
| `test_cmd_p1_wiring.py`, `test_cmd_p2_wiring.py`, `test_dash_runs_wiring.py`, `test_run_review_wiring.py`, `test_slice4_d4_wiring.py` | OK (the P1/P2 mutation runs skipped without their variables) |
| `slice_registry_check.py`, `slice_gen_context.py --check` | `summary: (a)=0  (b)=224`; `SliceAiObjectContext.cpp is up to date` |
| `tools/verify_okf.py` | `OKF verification PASSED (60 nodes validated, graph edges consistent).` |
| `git diff --check` | clean |
| Turtle `vm-build` / `vm-test tb` | `MAKE_RC=0` (compile 148 s, 52 module files changed); `RESULT: PASS` (`tools/verify_all.sh`, rc=0, 908 s) |

## Live (forever-test2, run2, 2026-11-20)

Install: run2 stopped, the four databases dumped first (`/mnt/forever/p5-dump/run2-pre-p5.sql.gz`, `gzip -t` OK), `bin/worldserver` saved as
`bin/worldserver.pre-p5`, both config files as `*.pre-p5`, the slot-6 ON binary copied in (md5 `00f94fc2…`, 72,294,016 bytes).

**The update first.** The first boot of the 25fc7ca binary aborted in the DB updater (`MySQLConnection.cpp:600 … ABORTED`) because run2's
`sqlsrc` was still at 6517af05: exactly the trap of `PIN-MOVE-25FC7CA-NOTES.md` section 9. `git -C sqlsrc fetch origin 25fc7ca && git checkout
25fc7ca` fixed it, and the boot then applied the core's updates and came up.

Config for the test: `AiPlayerbot.CombatMonitor = 1`, `AiPlayerbot.CombatTimeline = 1`, `TortoiseBots.Dashboard.Host = "172.22.0.1"` (the
docker gateway: the world container reaches the dashboard's UDP listener on the host), `Dashboard.Runs = 1`, `RunSummaryDir =
/home/pk/p5-dash-runs`; certs copied and `docker restart forever-bnet2`. The dashboard is this branch's Go daemon (`go build ./cmd/server`),
run on the VM as `bin/tortoise-observability.p5 -core forever -dev-no-auth`, UDP `0.0.0.0:9195`, HTTP `127.0.0.1:8095`, DB `127.0.0.1:3317`.

| Check | Result |
|---|---|
| boot | `ready...`; `grep -c 'ASSERT\|Segmentation\|ABORTED'` = 0 |
| the watch table guard | `TortoiseBots: world table 'ai_combat_aura_watch' is missing, or lacks a column this query names: not read` then `>> Loaded 0 combat timeline aura watch rows`: the missing table costs one line, never an abort |
| the binding | `.tb commander status`: `binding cmd=Mcmd cguid=15 online=yes lead=Mtank group=1 bound=3 bots=3`; the dashboard's `/api/v1/commanders` carries the same card |
| the window | during a fight the card's `combat` object: `{"win_s":10,"fight_ms":11003,"bots":[{"g":10,"dps":7,…,"ls":"Shield Block"},{"g":11,"dps":0,"hps":46,…,"ls":"Power Word: Shield"},{"g":12,"dps":6,…}]}` |
| fight 1 (pull `spawn:20038043`) | `/api/v1/combat/live` opened `1-1` from COMBAT_EVENTS (the live row exists only from that stream); filed as `1-1`, `outcome "end"`, `dur 40004`; the fight detail's meters: Mtank dmg 426, taken 579, 6 casts (Shield Block, Bloodrage, Demoralizing Shout); Mpriest heal 670, over 164, 7 casts (Renew, Heal, Fade, Power Word: Shield); Mdpsa dmg 398, 50 casts; `engaged: [{"e":11320,"n":"Earthborer","c":1}]` |
| fight 2 (pull `spawn:20038042`, then `.tb kill party Mtank`) | the live stream carried `start` and `engage` (`11321 Molten Elemental`) with its own id `1-2`; filed as `1-2`, `outcome "wipe"`, `deaths 3`, `dur 6000`. Two fights, two ids |
| cost | `bound fights 60s: 60 samples, avg 9 us max 81 us` idle, `avg 50–66 us max 164 us` with three bots in a fight, `1 group(s)` |
| what the timeline did not show | the run was a trash pull with no boss and the watch table is absent: `boss_hp`/`victims`/`auras` had nothing to record (the check covers the sample path) |

Restore: world stopped, `bin/worldserver.pre-p5` and both `*.pre-p5` configs put back, started and booted to `ready...` with 0
`ASSERT`/`ABORTED`, then stopped; the dashboard process stopped. Left changed on that server: the databases now carry the 25fc7ca updates the
updater applied (the dump above is the pre-test state) and `sqlsrc` is at 25fc7ca, which is the state the port expects.
