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

### Rivers: the wade flag (finding 8, the live-run blocker)

The run's liquid rule (TbRunLiquid.h: a point refuses any liquid whose surface is at or above the walked height - 0.6 yd,
or swimming water) was run over the plan's **whole** 302-step walk, forward, the 13 return legs and the escort road
included, on the same offline geometry as before (the server's ForeverCore v16 mmaps: `0043.mmap` and its five
`0043_*.mmtile` files from `forever-run2/data/mmaps`, read with forever-agent's forever planner: `Plan` between each
consecutive pair of the generated rows, every path vertex and ground sample every <= 1.5 yd, `navmesh.LiquidAt` with
the rule above asked in the point's own column):

* **dry: 280 of 301 hops**.
* **wet: 21 hops**, all in the rivers, every one a shallow wade (its surface stands 0.0-0.5 yd above the walked point;
  none is deeper than 1.5 yd, so the core's own `IsSwimmable` would walk all of them):

  | leg | seq | kind | from | to |
  | --- | --- | --- | --- | --- |
  | `cobrahn_b` 4 | 0 | move | -3.641 229.268 -106.189 | -61.367 323.721 -106.561 |
  | 4 | 1 | pull | -61.367 323.721 -106.561 | -71.600 348.300 -106.200 |
  | 4 | 2 | pull | -71.600 348.300 -106.200 | -63.800 417.300 -106.700 |
  | `pythas_a` 6 | 0 | move | -151.100 414.400 -72.600 | -21.300 183.400 -103.600 |
  | 6 | 5 | pull | -90.000 70.000 -105.810 | -107.200 68.700 -106.400 |
  | 6 | 6 | move | -107.200 68.700 -106.400 | -142.000 62.000 -105.650 |
  | 6 | 7 | pull | -142.000 62.000 -105.650 | -161.500 51.800 -106.000 |
  | 6 | 8 | pull | -161.500 51.800 -106.000 | -203.700 -5.300 -105.800 |
  | 6 | 11 | pull | -273.700 -7.600 -106.400 | -347.600 23.230 -101.070 |
  | `pythas_c` 8 | 4 | pull | -120.500 -101.000 -72.800 | -68.900 -146.700 -80.430 |
  | `back_pythas_a` 19 | 2 | move | -347.600 23.230 -101.070 | -273.700 -7.600 -106.400 |
  | 19 | 3 | move | -273.700 -7.600 -106.400 | -249.300 -17.200 -105.700 |
  | 19 | 5 | move | -203.700 -5.300 -105.800 | -161.500 51.800 -106.000 |
  | 19 | 6 | move | -161.500 51.800 -106.000 | -142.000 62.000 -105.650 |
  | 19 | 7 | move | -142.000 62.000 -105.650 | -107.200 68.700 -106.400 |
  | 19 | 8 | move | -107.200 68.700 -106.400 | -90.000 70.000 -105.810 |
  | `back_cobrahn_c` 20 | 1 | move | -21.300 183.400 -103.600 | -151.100 414.400 -72.600 |
  | `back_cobrahn_b` 21 | 15 | move | -63.800 417.300 -106.700 | -71.600 348.300 -106.200 |
  | 21 | 16 | move | -71.600 348.300 -106.200 | -61.367 323.721 -106.561 |
  | `back_kresh` 22 | 1 | move | -61.367 323.721 -106.561 | -3.641 229.268 -106.189 |
  | `mutanus` 30 | 0 | boss | 114.600 236.900 -96.000 | 142.700 254.000 -102.200 |

  The same list without the table is `forever/data/wc-wet-hops.tsv` (the probe's output, with the mmaps source and date in
  its header); the probe itself is a one-off forever-agent program (`cmd/wcwet` over `internal/navmesh`) and is not
  committed here, like the route export.
  A water-free plan between those areas is **no_path** (the navmesh's water polygons cut the map: tried with the
  water-excluding filter from Anacondra, Cobrahn, Skum and the west column), so the way through is the shallow water.

The plan rows now carry the opt-in wade bit `ai::kPlanFlagWadeOk` (131072, the next free plan flag): the generator sets it
on exactly those 21 rows, only on `move`, `pull` and `boss` rows (the loader refuses it on any other kind), and only when
the row's own hop still equals the probed pair (leg, seq, both points); a stale probe row flags nothing.
The run judge (`forever/src/host/run/TbRunOrders.cpp`, `forever/src/host/run/TbRunLiquid.h`) skips the liquid refusal for
a wade hop only when every sample of the walked route is dry or wading depth (surface minus the liquid's own
`depth_level` at most `kWadeDepthYd` 1.5 yd) and none is `UNDER_WATER` (swimming); the destination may stand in the
shallows, never in swimming water. Everything else is unchanged: **a row without the bit keeps exactly the RFC rule** (the
prefix escape, the dry destination, the pause "unreachable: ... crosses or ends in liquid"), and a hop that is not the
row's own point (an approach stand, a walk-back, a detour) never wades. Deep water, lava and every other plan (RFC,
Deadmines) behave exactly as before; `test_run_liquid` pins the shallow/deep/flagless cases.

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
| `escort_ritual` | points 16-30, `hold` of 5762 (300 s), `hold` of 5763 (300 s), both flags 65536 | the Moccasins and the seven Nightmare Ectoplasm |
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

A Disciple who dies **mid-fight** is the same stop, not a success: his summons vanish on the tick he dies, and the units
rule ("every unit of it is dead") would otherwise end the hold there. An escort hold now counts that rule as done only
while the start NPC is read alive (`DungeonRunPolicy.h` HoldStep); the 10 s gone timer above is what names him, so the
last hold goes back to the gossip instead of walking on into the Mutanus step's 300 s "not found" pause.

The Moccasin hold's ceiling is 300 s (it was 180): the Disciple needs about 105 s for stops 15 -> 30 plus 12 s to the
summon, and the party may fight on the road; 180 s left about 60 s of margin.

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
| `forever/tools/test_run_wc.cpp` (new; `python3 forever/tools/wc_plan_rows.py --tsv /tmp/wc_rows.tsv` first) | 727 checks, 0 failed: every row accepted, the plan shape (31 legs, 302 steps, 87 pulls, 5 holds, 2 hop rows), the run walks the whole plan to `Done` with the seven bosses, one `gossip 3678 Mtank 1`, the real unit rows listed at their spawn positions for every pull (within its radius), a wave unit east of the spot puts the ranged row west, a dead Disciple stops the event at the gossip (the first hold and a later one, across the holds between), a gossip that did not start the escort stops there too, **a Disciple who dies mid-fight does not end the hold on the units rule (holds 1-3 still end on time with him alive; the last hold names him and goes back to the gossip instead of the Mutanus step)**, and an old corpse does not end a hold (a new one does) |
| `test_run_tasks` (gossip clock, grammar, matcher, service types: binder/auction/battlemaster/stable) | 197 checks, 0 failed |
| `test_run_orders` (the gossip verb) | PASS, 266 checks |
| `test_run_facts` (Ragefire Chasm, unchanged) | PASS |
| `test_run_facts_s9`, `test_run_liquid`, `test_run_cancel`, `test_run_binding`, `test_run_gather`, `test_run_c6` | PASS (`test_run_liquid` now pins the wade rule: shallow wades, deep/swimming refuses, a wet shallow hop without the flag still pauses) |
| `test_dungeon_plan_policy` | PASS (the wade bit on move/pull/boss only, its own bit, and with the skip-unreachable bit) |
| `test_wc_plan_rows.py` | 541 checks, 0 failed (the committed SQL is the generator output; extent, back-leg continuity, flags, 193 spawns; the base rows pinned to the agent route fixture at 0.15 yd; the 21 wade hops are exactly the rows with the bit) |

## 6. Gates (build VM, core d02645dd21, this worktree)

| gate | result |
|---|---|
| build ON (`slot-sync-build.sh 4 tortoisebots -DTORTOISEBOTS_SLICE1=ON`) | `CMAKE_RC=0`, `MAKE_RC=0`, 710 s (it waited for the shared tree of the same core and options, then recompiled the module); key `core-9b88bd2d6584-opt-85c8cb4e`, 64 changed/new/deleted module files |
| build OFF (`slot-sync-build.sh 4 tortoisebots`) | `CMAKE_RC=0`, `MAKE_RC=0`, 17 s; key `core-9b88bd2d6584-opt-78bf025f` |
| slot 8 (the brief's slot number) | **not a core this branch builds on**: `~/forever-build/src-8` holds the older 25fc7ca core (no `WorldSession::IsKickRequested`, no `ScriptMgr::OnAfterReload(std::string_view)`), so `TbBotSession.cpp:297` and `TbScripts.cpp:152` (both untouched by this round) fail with `MAKE_RC=1`. The pinned d02645dd21 core is in slots 1, 2 and 4, as the last round used (slot 4) |
| `slice_gen_context.py --check`, `slice_registry_check.py` | `SliceAiObjectContext.cpp is up to date`; the registry check unchanged (228 keys to register or stub later) |
| `python3 forever/tools/test_wc_plan_rows.py` | 541 checks, 0 failed |
| `python3 tools/verify_okf.py` | `OKF verification PASSED (60 nodes validated, graph edges consistent)` |
| `git diff --check` | clean |
| the run/policy/wiring tests incl. the WC ones and RFC's unchanged | PASS (section 5) |

## 7. The live run (run 2, core 25fc7ca, the previous worker's build) and this round

The earlier live run covered 6 of 490 steps (4 pulls, 1 wipe, stopped by hand on a loaded VM): Kresh, the ramp, the whole
event and the gossip were **not** reached live. The route then was the Turtle navmesh route; this round replaces it with
the route the ForeverCore server proved (section 2), adds the escort failure, and fixes the hold facts. **No live run was
made in this round** (the brief: the lead runs it next on a test server away from the build VM). The first live check
should be `.tb plan wailing_caverns check` (expect units 193 matched 193) and then `.tb run start ... wailing_caverns`.
The river hops are wade rows now (section 2): the run walks its first 21 wet hops instead of pausing, and still pauses
with the unchanged "unreachable: ... crosses or ends in liquid" if the core's own liquid read finds one of them deeper
than 1.5 yd or swimming (the probe could only read the mmaps' water surface, not the core's depth).

The server was stopped after the earlier run, the dump restored (`zstd -dc dumps/wc-20261120T212230Z.sql.zst | mysql`),
the previous binary put back, the new plan SQL removed from `sqlsrc` (0 files left), `etc/worldserver.conf` restored
(`MaxCoreStuckTime = 60`), the databases booted once more (the boot-check: `READY`, then `DONE`) and the server stopped
(no `forever-world2` container). The five test characters (`Mwctank`, `Mwcpri`, `Mwcdps`, `Mwcmag`, `Mwcrog`) and their
gear went back with the dump.

**Slots:** builds in this round go only through `~/forever-build/slot-sync-build.sh 4` on yulon-ubuntu2 (the brief's
slot lock); no build ran outside it.
