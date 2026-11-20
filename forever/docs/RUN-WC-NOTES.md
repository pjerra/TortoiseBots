# Wailing Caverns: the self-led run, its facts, steps, pulls and the Naralex event

Branch `run/wc` (worktree `~/wt-run-wc`), core pin d02645dd21, **zero core changes**. Spec: the brief
`~/TortoiseBots-audit/briefs-deepseek/RUN-WC.txt`; the RFC work this follows is `RUN-S5-FACTS-RFC.md`, `RUN-S6-F-NOTES.md`,
`RUN-S6-C-NOTES.md`, `RUN-S6-O-NOTES.md` and `SLICE9-DOORS-NOTES.md`.

## 1. Which bosses the run needs (decided from the data, not from a list)

| boss | spawn on ForeverCore map 43 | verdict |
|---|---|---|
| **Lady Anacondra 3671** | **pool 10004**, four spawn rows (15.34 297.18 / -53.98 271.39 / -67.78 122.60 / 46.47 183.98), one active at a time | **no boss step**: a boss row would wait on three places where she is not. She is a member of four pull groups (one per pool spot) and `kill_first = 3671` focuses her when her pack is pulled; her smart_script sets Enc 0 = 3 on death |
| **Lord Cobrahn 3669** | 20027380, -151.14 414.37 | boss step (leg `cobrahn_c`) |
| **Lord Pythas 3670** | 20087107, 36.81 -241.06 | boss step |
| **Lord Serpentis 3673** | 20038148, -120.16 -24.62 | boss step |
| **Kresh 3653** | 20027368, -64.41 319.08, waypoint path 800273680 (9 nodes, 525.9 yd, ~210 s a round) | boss step at his third node (-3.641 229.268 -106.189), timeout 300 s, **flags 4096**: a patroller elsewhere after the away-wait ends the step "skipped, not cleared" with a note instead of pausing for good |
| **Skum 3674** | 20087131, -285.65 -313.71 | boss step |
| **Verdan the Everliving 5775** | 20033974, -81.86 32.26 | boss step (last of the mapped route) |
| **Mutanus the Devourer 3654** | **no creature row**: the Disciple's script summons him at 142.7 254 -102.2 (`ClassicDiscipleSummonPos[3]`, subevent 6) | boss step **inside the event**, after the Disciple's escort: the grid search and the corpse certify him (`bossOwnDead` stays not-known: no spawn row, the do-less value) |
| **The Disciple of Naralex 3678** | 20018675, -134.965 125.402 -78.0945 | not a boss: the event's gate, gossip and road (section 3) |
| Vangros 61965, Zandara Windhoof 61968 | Turtle-only custom bosses: not in ForeverCore's map 43 | not in the plan |

So the run's required kills are Cobrahn, Pythas, Serpentis, Kresh, Skum, Verdan and Mutanus as boss steps, plus Anacondra
as a pack member (the four Fanglords are what the Disciple's gate reads: `Enc(0..3) == DONE`).

## 2. The plan rows

`forever/sql/world/2026_11_20_12_tortoisebots_plan_wailing_caverns.sql` (31 legs, 302 steps, 193 unit rows, 7 boss rows),
written by `forever/tools/wc_plan_rows.py` from two inputs:

* **the proven route**: `forever/data/wc-route-forevercore.tsv`, the public agent plan `dungeons/wailing_caverns.json` at
  forever-agent 109a2496 exported with `forever-agent plan-export --forever` (13 legs, 126 steps). Every waypoint was
  confirmed by the server's own position in the live route walk (forever-agent `docs/forever/ROUTEWALK-NOTES.md`):
  **3583.0 yd planned, 3582.3 walked, every leg PASS, no NOPATH, STUCK, FELL or LIQUID**. No Turtle row is used: the route
  never goes west of **x = -352.8** (Turtle-only geometry reached x = -515, and ForeverCore has no spawn west of x = -374);
  the dropped Turtle legs 8/11 (vangros, zandara) are absent, and no leg starts at their far ends any more. Every leg starts
  where the previous one ended (or at the previous one's last scroll point: checked in `test_wc_plan_rows.py`).
* **the map 43 spawns** (`forever/data/wc-map43-spawns.tsv`): every pack member at its spawn position, which is what
  `.tb plan check` matches one-to-one. **193 unit rows for 193 spawns** (the same count the last live plan-check matched
  193/193). Three pull rows share spawn rows with a neighbouring row and carry no unit rows: a step without rows is
  "not known" (`StepMatchedMembers` -1) and the run pulls it by entry and radius as before, never skipped.

The way back retraces the proven points in reverse (13 `back_*` legs), the one-way ramp hop as the live-probed pair: the
lip (-287.5 5 -63.6, exact) then `move_hop_exact` to the foot (-293.3 0.5 -58) - probed 3 of 3 from the lip and 3 of 3
from the foot on 2026-10-22 (forever-agent `dungeons/wailing_caverns-extra.json`).

Re-running `python3 forever/tools/wc_plan_rows.py --out <file>` rewrites the same rows from the same inputs.

### The navmesh check of the new legs (offline, the server's own Forever v16 mmaps)

The agent's planner was run on `forever-run2/data/mmaps` (map 43) for every consecutive pair of the 126 route steps:

* 124 of the pairs plan **complete**; the 2 that do not are the ramp hop pairs (`move_hop_exact`: 87.0 yd partial from the
  west column's last point to the lip, and the hop itself): they are the direct jump the mesh has no link for, walked as a
  straight line (the live probe above). No other pair is partial, none is no_path.
* The RFC plan was checked with the module's own `.tb plan check` (unit rows) and its path checks; this round has no live
  server (decision: the lead runs it next), so the offline planner answers stand in for `.tb plan check`. The unit rows are
  the 193 spawn positions, so the check's 193/193 is the same set of points, now in the proven corridor.

### The liquid check (finding 8)

The run's own liquid rule (TbRunLiquid.h: any liquid whose level is within 0.6 yd below the walked point, exactly as on
RFC) was run over every pair of the new route with the ForeverCore water polygons (`navmesh.LiquidAt` on the server's
mmaps, every path vertex and 4-yd ground sample, 126 pairs):

* **dry: 119 pairs**; the whole base route is dry up to the Kresh river.
* **wet: 6 pairs**, all at the rivers: `Kresh -> cobrahn_b` (-41.3 298.7 -105.8), `cobrahn_b` (-61.4 323.7),
  `cobrahn_b -> cobrahn_c` (-64.0 360.5), `cobrahn_c -> pythas_a` (-55.5 310.9, the 778 yd way), the `pythas_a`
  corridor (-106.7 64.5, -107.2 68.7, -154.9 51.7, -201.1 15.5, -277.3 -4.0) and one 1-sample spot on `pythas_c`
  (-85.1 -133.8 -78.9).
  A water-free plan between those areas is **no_path** (the navmesh's water polygons cut the map: tried with the
  water-excluding filter from Anacondra, Cobrahn, Skum and the west column). The run therefore pauses cleanly at the
  first river crossing with "unreachable: ... crosses or ends in liquid at ..." and does not fight or wander; the route
  cannot avoid it without a policy change (water allowed where the core lets the bot wade), which this round does not
  make (decision 8: the liquid policy stays as on RFC). **Open blocker for the live run.**

## 3. The Naralex event (the end of the run)

Core data (d02645dd21, `classic_wailing_caverns.cpp` / `classic_instance_wailing_caverns.cpp`, and the world DB's
`smart_scripts`):

* the gate is four instance values: Enc 0 = Anacondra 3671, Enc 1 = Cobrahn 3669, Enc 2 = Pythas 3670, Enc 3 = Serpentis
  3673. **Checked in the test world DB**: each boss's smart_script runs `SMART_ACTION_SET_INST_DATA` type 0 on EVADE
  (0), ON AGGRO (1) and ON DEATH (3), so `Enc(0..3) = 3` after the four die; Anacondra also sets Enc 0 = 4 (special) 2 s
  after spawn (her own row). The instance script then sets data 4 (Disciple) = SPECIAL and the Disciple offers menu 202.
  **What the run can rely on: Enc 0-3 = 3 on each death.** The gate hold waits for data 4 = 4.
* **data 5 (Mutanus) is never set to 3**: Mutanus's own row (3654) is `SMART_ACTION_SET_INST_DATA` field 4, data 3,
  **type 1 = SetBossState** (the comment says "Data 9 to 3", the row does not), so it sets boss state 4 (Skum's
  encounter), not instance data 5; the Disciple's own "Mutanus DONE -> Disciple DONE" branch therefore never runs either.
  The plan therefore has **no trailing data-5 or data-4=3 hold**: the Mutanus boss step's own kill read ends the run.
* the Disciple's gossip menu is 202, option 1 (the row's `param`); the escort walks `script_waypoint` 3678 (31 points,
  pauses at 7, 15 and 30), and Mutanus is summoned at the ritual's end.

| leg | rows | what |
|---|---|---|
| `back_verdan` … `back_anacondra_a` (13 legs) | the base route's points in reverse, the ramp hop as lip (exact) -> foot (`move_hop_exact`) | Verdan -> the foot -> back along the whole route to the entrance part |
| `disciple` | move to the stand, `hold` data 4 = 4 (60 s), `gossip` 3678 option 1 (60 s) | the gate, then the escort starts |
| `escort_ravagers` | the road's points 1-7, `hold` of 3636 within 40 yd (240 s, flags 65536) | the two summoned Ravagers (and any road Ravager) |
| `escort_vipers` | points 8-15, `hold` of 5755 (240 s, flags 65536) | the three summoned Vipers |
| `escort_ritual` | points 16-30, `hold` of 5762 (180 s), `hold` of 5763 (300 s), both flags 65536 | the Moccasins and the seven Nightmare Ectoplasm |
| `mutanus` | `boss` 3654 at 142.7 254 (300 s) | Mutanus; his death is the last read of the plan |

`flags 65536` (`kPlanFlagEscortNpc`) makes every escort hold read the event's start NPC (the gossip row's 3678) around the
lead bot, wherever the gossip stands before it (`EventGossipStartStep`, which looks through the other holds; `EventStartStep`
stops at a hold, so a later wave hold could not find it):

* **dead or gone for 10 s**: the event fails with its name ("the event's NPC (the Disciple of Naralex) is dead or gone"),
  the run goes back to the gossip and pauses - no 240 s wait, no walk into the Mutanus "not found" pause;
* **alive and still within 5 yd of his stand after 30 s of the hold, with no wave up**: the gossip did not start the
  escort (a refused hello or a select the script ignored); the event fails as "the Disciple did not leave his stand: the
  gossip did not start the escort" and goes back to the gossip. The gossip task itself is answered instantly by the core
  (the choice runs inside the select handler), so this is the observable postcondition the run verifies instead of
  trusting the task's ACK; the instance data cannot serve (data 4 only becomes IN_PROGRESS at the ritual's waypoint 30).

The re-arm is once per event (`Run::eventRearmed`); a respawned Disciple can be gossiped to on `.bot run resume`, a second
failure then pauses with the same words.

The escort does not need a "follow the NPC" rule: `Start(false, ObjectGuid::Empty)` keeps the core's `_despawnAtFar` check
off (an empty player guid), so the party may stand at the next stop while the Disciple walks; the waves run at him and
the party is at the stop they run through.

## 4. What the engine needed (module code, no core change)

1. **`gossip` order** (merged with Commander P6's task engine): the grammar is the shell's own (`gossip <entry> <bot>
   <index | "text">`, `GossipOrder::ParseRequest`), the gate is the run's own plan step (`RunGossipStepOf` +
   `GossipOrder::MayTalkTo`, so no allow-list edit is needed for a plan's gossip) and it is re-checked at act time, the
   act sends the two packets a client sends (`CMSG_GOSSIP_HELLO`, then `CMSG_GOSSIP_SELECT_OPTION` with the menu id and
   the item's own option id) into the bot's session handlers, and the choice is matched on the menu the core built
   (`GossipOrder::MatchOption`). On this core a database option has sender = menu id and action = order index, so
   `MakeGossipOption` maps the option's own `GossipOptionNpc` type to the policy's plain/service word: a make-inn (binder),
   auctioneer, battlemaster, stablemaster, banker, mailbox or talent line is never picked. The act also keeps the Turtle
   guards: `GetNPCIfCanInteractWith`, a facing turn, `npc-busy` when interaction is refused, and the run-step re-check.
2. **Hold facts** (`TbRunFactsPure.h`, `TbRunFacts.cpp`): `eventData` (the instance's data `entry` through
   `Map::GetInstanceScript()->GetData`; -1 and "not known" when the map has no instance script), a hold done by object,
   and a units hold (the living creatures of the step's `entries` within its radius, friendly ones too, plus `eventDown`
   = the dead ones). Every hold unit now carries its **x, y** (HoldStands' "side the event comes from", and the tank's
   target) and its **engaged / victimLead / victimHealer** flags (the tank is sent at a wave unit on the healer), read
   through `CreatureObs` in `ObserveCreature`. A data or object hold reads the approach too (no "no walkable path toward
   the event spot" after a wipe/resume). An old corpse of the entry (from an earlier leg) no longer ends a units hold
   early: the dead count at the hold's first read is the baseline, and only a corpse above it triggers the rule.
3. **Escort failure**: `kPlanFlagEscortNpc` (65536) + `Facts::eventNpcRead/eventNpcAlive` + a named `EventFailed`.

The do-less values stand where the core cannot answer: no instance script -> `eventData` -1 -> the hold pauses saying so;
Mutanus's `bossOwnDead` stays false (no spawn row) and the corpse/grid fields certify the kill.

## 5. Tests

| test | result |
|---|---|
| `forever/tools/test_run_wc.cpp` (new; `python3 forever/tools/wc_plan_rows.py --tsv /tmp/wc_rows.tsv` first) | 705 checks, 0 failed: every row accepted, the plan shape (31 legs, 302 steps, 87 pulls, 5 holds, 2 hop rows), the run walks the whole plan to `Done` with the seven bosses, one `gossip 3678 Mtank 1`, the real unit rows listed at their spawn positions for every pull (within its radius), a wave unit east of the spot puts the ranged row west, a dead Disciple stops the event at the gossip (the first hold and a later one, across the holds between), a gossip that did not start the escort stops there too, and an old corpse does not end a hold (a new one does) |
| `test_run_tasks` (gossip clock, grammar, matcher, service types: binder/auction/battlemaster/stable) | 197 checks, 0 failed |
| `test_run_orders` (the gossip verb) | PASS, 266 checks |
| `test_run_facts` (Ragefire Chasm, unchanged) | PASS |
| `test_run_facts_s9`, `test_run_liquid`, `test_run_cancel`, `test_run_binding`, `test_run_gather`, `test_run_c6` | PASS |
| `test_wc_plan_rows.py` | 365 checks, 0 failed (the committed SQL is the generator output; extent, back-leg continuity, flags, 193 spawns) |

## 6. Gates (build VM, core d02645dd21, this worktree)

| gate | result |
|---|---|
| build ON (`slot-sync-build.sh 4 tortoisebots -DTORTOISEBOTS_SLICE1=ON`) | see the final report: `CMAKE_RC=0`, `MAKE_RC=0`, module objects 0 errors, `worldserver` links |
| build OFF (`slot-sync-build.sh 4 tortoisebots`) | see the final report: `CMAKE_RC=0`, `MAKE_RC=0` |
| census (newest slice list) | see the final report: `files ... compile clean ... with errors 0 ... includes of a header ForeverCore does not have 0` |
| `slice_gen_context.py --check`, `slice_registry_check.py` | see the final report |
| the run/policy/wiring tests incl. the WC ones and RFC's unchanged | PASS (section 5) |

## 7. The live run (run 2, core 25fc7ca, the previous worker's build) and this round

The earlier live run covered 6 of 490 steps (4 pulls, 1 wipe, stopped by hand on a loaded VM): Kresh, the ramp, the whole
event and the gossip were **not** reached live. The route then was the Turtle navmesh route; this round replaces it with
the route the ForeverCore server proved (section 2), adds the escort failure, and fixes the hold facts. **No live run was
made in this round** (the brief: the lead runs it next on a test server away from the build VM). The first live check
should be `.tb plan wailing_caverns check` (expect units 193 matched 193) and then `.tb run start ... wailing_caverns`;
the river crossings (section 2, the liquid check) are the expected first pause.

The server was stopped after the earlier run, the dump restored (`zstd -dc dumps/wc-20261120T212230Z.sql.zst | mysql`),
the previous binary put back, the new plan SQL removed from `sqlsrc` (0 files left), `etc/worldserver.conf` restored
(`MaxCoreStuckTime = 60`), the databases booted once more (the boot-check: `READY`, then `DONE`) and the server stopped
(no `forever-world2` container). The five test characters (`Mwctank`, `Mwcpri`, `Mwcdps`, `Mwcmag`, `Mwcrog`) and their
gear went back with the dump.

**Slots:** builds in this round go only through `~/forever-build/slot-sync-build.sh 4` on yulon-ubuntu2 (the brief's
slot lock); no build ran outside it.
