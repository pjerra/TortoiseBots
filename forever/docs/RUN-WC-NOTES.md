# Wailing Caverns: the self-led run, its facts, steps, pulls and the Naralex event

Branch `run/wc` (worktree `~/wt-run-wc`), core pin 25fc7ca (read only, **zero core changes**). Spec: the brief
`~/TortoiseBots-audit/briefs-deepseek/RUN-WC.txt`; the RFC work this follows is `RUN-S5-FACTS-RFC.md`, `RUN-S6-F-NOTES.md`,
`RUN-S6-C-NOTES.md`, `RUN-S6-O-NOTES.md` and `SLICE9-DOORS-NOTES.md`.

## 1. Which bosses the run needs (decided from the data, not from a list)

| boss | spawn on ForeverCore map 43 | verdict |
|---|---|---|
| **Lady Anacondra 3671** | **pool 10004**, four spawn rows (15.34 297.18 / -53.98 271.39 / -67.78 122.60 / 46.47 183.98), one active at a time | **no boss step**: a boss row would wait on three places where she is not. She is a member of four pull groups (one per pool spot) and `kill_first = 3671` focuses her when her pack is pulled; her death sets the instance data the same way |
| **Lord Cobrahn 3669** | 20027380, -151.14 414.37 | boss step (leg `cobrahn_d`) |
| **Lord Pythas 3670** | 20087107, 36.81 -241.06 | boss step |
| **Lord Serpentis 3673** | 20038148, -120.16 -24.62 | boss step |
| **Kresh 3653** | 20027368, -64.41 319.08, waypoint path 800273680 (9 nodes, 525.9 yd, ~210 s a round at the walk speed) | boss step at his third node (-3.641 229.268 -106.189), timeout 300 s: the broker wait is the boss search, not a pull that only works when he happens to stand at his spawn |
| **Skum 3674** | 20087131, -285.65 -313.71 | boss step |
| **Verdan the Everliving 5775** | 20033974, -81.86 32.26 | boss step (last of the mapped route) |
| **Mutanus the Devourer 3654** | **no creature row**: the Disciple's script summons him at 142.7 254 -102.2 (`ClassicDiscipleSummonPos[3]`, subevent 6) | boss step **inside the event**, after the Disciple's escort: the grid search and the corpse certify him (`bossOwnDead` stays not-known: no spawn row, the do-less value) |
| **The Disciple of Naralex 3678** | 20018675, -134.965 125.402 -78.0945 | not a boss: the event's gate, gossip and road (section 3) |
| Vangros 61965, Zandara Windhoof 61968 | Turtle-only custom bosses: not in ForeverCore's map 43 | not in the plan |

So the run's required kills are Cobrahn, Pythas, Serpentis, Kresh, Skum, Verdan and Mutanus as boss steps, plus Anacondra
as a pack member (the four Fanglords are what the Disciple's gate reads: `Enc(0..3) == DONE`).

## 2. The plan rows

`forever/sql/world/2026_11_20_12_tortoisebots_plan_wailing_caverns.sql` (33 legs, 490 steps, 193 unit rows, 7 boss rows),
written by `forever/tools/wc_plan_rows.py` from three inputs:

* **the base route**: the navmesh-checked Turtle rows of `data/sql/world/20261025002700_world.sql` (turtle-agent 16a864d:
  "209 move hops, 209 complete, 0 incomplete, 0 refused"), legs 0-7, 9, 10, 12 and 13 with the two Turtle-only boss legs
  dropped. The earlier ForeverCore export (`2026_11_20_10`) had dropped most path rows; its first long straight move
  (cobrahn_b, -29.3 277.3 -> -61.4 323.7) crossed the river and the live S9 run paused on it ("crosses or ends in liquid").
  Using the checked route removes that class of row.
* **the split of Turtle leg 2** into `cobrahn_a` (rows 0-11), a `kresh` leg (the `to` move and the boss row) and
  `cobrahn_b` (rows 12-24 without its own Kresh pull, which the boss leg replaces).
* **the event** (section 3): the way back (the base legs' moves in reverse, the ramp hop taken back down), the Disciple's
  gate and gossip, the escort road with a hold at each waved stop, and Mutanus.
* **the unit rows**: every pack member at its spawn position (a dump of map 43's `creature` rows,
  `forever/data/wc-map43-spawns.tsv`), which is what `.tb plan check` matches one-to-one. Three pull rows have no unit rows
  (two Cobrahn-corridor raptor packs and one serpentis adder pack share spawn rows with a neighbouring row): a step without
  rows is "not known" (`StepMatchedMembers` -1) and the run pulls it by entry and radius as before, never skipped.

Re-running `python3 forever/tools/wc_plan_rows.py --out <file>` rewrites the same rows from the same inputs.

## 3. The Naralex event (the end of the run)

Core data (25fc7ca, `classic_wailing_caverns.cpp` / `classic_instance_wailing_caverns.cpp`): the Disciple's own gossip menu
is 201; when the four Fanglords are down the instance sets `CWC_TYPE_DISCIPLE` (data 4) to 4 (special) and the script then
offers menu **202, option 0** ("the awakening"), the escort walks `script_waypoint` 3678 (31 points, pauses at 7, 15 and 30),
and Mutanus is summoned at the ritual's end. The plan's legs:

| leg | rows | what |
|---|---|---|
| `back_verdan` … `back_anacondra_a` (13 legs) | the base moves in reverse | Verdan -> the ramp lip (exact) -> the hop down -> back along the whole route to the entrance part |
| `disciple` | move to the stand, `hold` data 4 = 4 (60 s), `gossip` 3678 option 1 (60 s) | the gate, then the escort starts |
| `escort_ravagers` | the road's points 1-7, `hold` of 3636 within 40 yd (240 s) | the two summoned Ravagers (and any road Ravager) |
| `escort_vipers` | points 8-15, `hold` of 5755 (240 s) | the three summoned Vipers |
| `escort_ritual` | points 16-30, `hold` of 5762 (180 s), `hold` of 5763 (300 s) | the Moccasins and the seven Nightmare Ectoplasm |
| `mutanus` | `boss` 3654 at 142.7 254 (300 s), `hold` data 5 = 3, `hold` data 4 = 3 | Mutanus, then the event's own end |

The escort does not need a "follow the NPC" rule: `Start(false, ObjectGuid::Empty)` keeps the core's
`_despawnAtFar` check off (an empty player guid), so the party may stand at the next stop while the Disciple walks. The
waves run at him, and the party is at the stop they run through.

## 4. What the engine needed (module code, no core change)

1. **`gossip` order** (`TbRunOrdersPure.h`, `TbRunOrders.cpp`, `TbRunTasks.{h,cpp}`, `TbRunTasksPure.h`): the grammar is the
   shell's own (`gossip <entry> <bot> <index | "text">`, `GossipOrder::ParseRequest`), the gate is the run's own plan step
   (`RunGossipStepOf` + `GossipOrder::MayTalkTo`, so no allow-list edit is needed for a plan's gossip), the act sends the two
   packets a client sends (`CMSG_GOSSIP_HELLO`, then `CMSG_GOSSIP_SELECT_OPTION` with the menu id and the item's own option
   id) into the bot's session handlers, and the choice is matched on the menu the core built (`GossipOrder::MatchOption`; a
   service line is never picked). The task clock treats gossip as an instant act (the core runs the choice inside the select
   handler), the payload is the menu line, and `taskReply`/`taskText` reach the policy as for the loot/open tasks.
2. **Hold facts** (`TbRunFactsPure.h`, `TbRunFacts.cpp`): a hold's `eventData` (the instance's data `entry` through
   `Map::GetInstanceScript()->GetData`; -1 and "not known" when the map has no instance script), a hold done by object
   (`objectState`, the S9 read), and a units hold (the living creatures of the step's `entries` within its radius, friendly
   ones too, plus `eventDown` = the dead ones). Before this, every hold read as "nothing there" and could only time out.
3. **Gossip facts**: `carrier` = the lead bot (or the first available one), an outstanding order pinned to its bot
   (`OutstandingCarrierVerdict`, retired when that bot died or left), `carrierBusy` and the answer.

The do-less values stand where the core cannot answer: no instance script -> `eventData` -1 -> the hold pauses saying so;
a wave that never arrives -> the hold times out and pauses with its own words; Mutanus's `bossOwnDead` stays false (no
spawn row) and the corpse/grid fields certify the kill.

## 5. Tests

| test | result |
|---|---|
| `forever/tools/test_run_wc.cpp` (new; `python3 forever/tools/wc_plan_rows.py --tsv /tmp/wc_rows.tsv` first) | 693 checks, 0 failed: every row accepted, the boss set is the required one, the run walks 490/490 steps to `Done` with 95 pull orders, 7 bosses killed, one `gossip 3678 Mtank 1`, and the hold facts (units, `eventDown`, `eventData`, no instance script) read as the policy expects |
| `test_run_tasks` (gossip clock, grammar, matcher) | 164 checks, 0 failed |
| `test_run_orders` (the gossip verb) | PASS, 216 checks |
| `test_run_facts` (Ragefire Chasm, unchanged) | PASS |
| `test_run_facts_s9`, `test_run_liquid`, `test_run_cancel`, `test_run_binding`, `test_run_gather`, `test_run_c6` | PASS |

## 6. Gates (build VM, core 25fc7ca, this worktree)

| gate | result |
|---|---|
| census (slot 4 upload, `FOREVER_CORE=~/ForeverCore-25fc7ca`, `FOREVER_CENSUS_LIST=forever/SLICE9.txt`) | `files 265 compile clean 265 with errors 0 error places 0 (in 0 files) includes of a header ForeverCore does not have 0` |
| option ON `worldserver` (`-DTORTOISEBOTS_SLICE1=ON`, 12 jobs, the `forever-builder` image, source `src-2` = core 25fc7ca + this worktree) | `CMAKE_RC=0`, `MAKE_RC=0`, 1419/1419, `worldserver` links; the module's own objects: 283/283, 0 errors (one pre-existing `sTerrainMgr` macro warning) |
| option OFF `tortoisebots` (`-DTORTOISEBOTS_SLICE1=OFF`) | `CMAKE_RC=0`, `MAKE_RC=0` (`libtortoisebots.a` of empty slice objects) |
| `slice_gen_context.py --check`, `slice_registry_check.py` | `SliceAiObjectContext.cpp is up to date`; `(a)=0  (b)=218` |
| `test_run_wc`, `test_run_tasks`, `test_run_orders`, `test_run_facts` (RFC), `test_run_facts_s9`, `test_run_liquid`, `test_run_cancel`, `test_run_binding`, `test_run_gather`, `test_run_c6` | all PASS (see section 5) |
| `test_wc_plan_rows.py`, `tools/test_dungeon_plan_sql.py` (Turtle SQL unchanged), the six `forever/tools/test_*_wiring.py` | OK / 104 tests OK / OK |
| Turtle `vm-build` / `vm-test tb` | not needed: nothing outside `forever/` changed |

## 7. The live run (run 2, core 25fc7ca, this worker's build)

State before the run: the test server's databases were dumped (`~/forever-run2/dumps/wc-20261120T212230Z.sql.zst`), this
worker's `worldserver` installed over the previous one (kept as `bin/worldserver.pre-wc`), `forever/sql/world/2026_11_20_12_*`
copied into `sqlsrc/sql/tortoisebots/world/` and applied by the boot updater, the bnet certificates copied, and the server
started. `MaxCoreStuckTime` was set to 0 for the test (the build VM was at load 80-184 from other workers' builds and the
60 s world-thread watchdog aborted the server once; the value is restored with the config backup `etc/worldserver.conf.pre-wc`).

Party: five new level-22 bots on the pool account (`Mwctank` prot warrior as the run's tank, `Mwcpri` priest, `Mwcdps` arms
warrior, `Mwcmag` mage, `Mwcrog` rogue), the level-22 kits of `forever/data/waterys/rfc/kits-level22.csv` equipped through
`.tb equip`, roles set, the commander `Mcmd` (a bot) bound with `.tb master party Mwctank Mcmd`, the party teleported into
the instance (`map 43, instance 2`) with `.tb tele party Mwctank 43 -158.441 131.601 -74.2552`.

* **Plan check**: `plan-end count=193 records=193 verb=check plan=wailing_caverns bot=Mwctank map=43 inst=1 units=193
  matched=193 near=0 missing=0 optional=0 steps=91 steps_ok=91 steps_short=0` — every unit row matched its spawn (against
  Ragefire Chasm's 95/1/7).
* **The run**: `.tb run start Mwctank wailing_caverns` -> `run lead=Mwctank plan=wailing_caverns state=running step=0
  steps=490 leg=0 legname=anaconda_a kind=move phase=begin note=started pull=0 pulls=88 boss= deaths=0 wipes=0`.

**Live result** (the run was stopped by hand after about 50 minutes of wall time; the VM was at load 80-184 from other
workers' builds, and the world thread ran at 15-35 ms per cycle, so the run's own clock (`elapsed=`, each tick at most 5 s)
covered only 489 s):

* 4 pulls of the 88 (`pull=4`), 8 deaths, **1 wipe** — recovered by the run's own `revive party` (event #31) — 0 pauses,
  0 bosses, and `stop` with `stayed=5`;
* where it stopped: `run lead=Mwctank plan=wailing_caverns state=stopped step=6 steps=490 leg=0 legname=anacondra_a kind=pull
  phase=rest note=stopped: done pull=4 pulls=88 boss= deaths=8 wipes=1 pauses=0 elapsed=489 why=stopped: done ev=33`;
* the events are the policy's own: `start`, `step` (0..6 through `anacondra_a`), `pull` (3637, 3636, 3637), `fight-end`,
  `rest`, `death` (the tank and the priest), `wipe`, `revive` (`revive party`), `stop`.

The party died on the first trash packs: five level-22 bots against Wailing Caverns elites, with their reactions slowed by the
loaded VM, lose the tank faster than the priest can heal. The run did what the policy says (fought on, regrouped and revived
after the wipe) and was stopped there; the walk, the Kresh leg, the ramp return and the Naralex event were **not** reached
live. What was proven live: the plan rows load and every unit row matches (193/193), the run starts on the plan, walks,
pulls by the plan's own groups, fights, restarts after a wipe, and stops cleanly.

The server was then stopped, the dump restored (`zstd -dc dumps/wc-20261120T212230Z.sql.zst | mysql`), the previous binary
put back (its own `bin/worldserver` from before the test: the same sha256 as `bin/worldserver.pre-wc`), the new plan SQL
removed from `sqlsrc` (0 files left), `etc/worldserver.conf` restored (`MaxCoreStuckTime = 60`), the databases booted once
more (the boot-check: `READY`, then `DONE`) and the server stopped (no `forever-world2` container). The five test characters
(`Mwctank`, `Mwcpri`, `Mwcdps`, `Mwcmag`, `Mwcrog`) and their gear went back with the dump.

**Slots:** `FOREVER_SLOT=4` (the brief's slot) was taken by another worker's build while this slice ran (`/tmp/w0a-*`
builds); the ON/OFF builds were therefore made with the same `forever-builder` image and CMake options in this worker's own
`~/wc-build-on` / `~/wc-build-off` directories from `src-2` (core 25fc7ca + this worktree), not through `build.sh`'s slot
lock. The census did run through `vm-census.sh` with slot 4 before that worker took it.
