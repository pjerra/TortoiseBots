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

Re-running `python3 forever/tools/wc_plan_rows.py --out <file>` rewrites the same rows from the same inputs. The plan row's
`source` is `forever-agent 109a2496 route export + core scripts` (50 characters, inside the column's varchar(64)); the
generator's every text field is checked against the plan tables' own schema by `test_wc_plan_rows.py`, after the live
updater refused the first, 90-character text with MySQL error 1406.

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

The plan rows record those 21 probed shallow hops with the bit `ai::kPlanFlagWadeOk` (131072): the generator sets it on
exactly those rows, only on `move`, `pull` and `boss` rows (the loader refuses it on any other kind), and only when the
row's own hop still equals the probed pair (leg, seq, both points); a stale probe row flags nothing. Since live run 4 the
bit is plan data, not the gate: any move the run issues for itself may wade on any leg (section 11).
The run judge (`forever/src/host/run/TbRunOrders.cpp`, `forever/src/host/run/TbRunLiquid.h`) skips the liquid refusal for
every move the run issues for itself, on any leg of the run, when every sample of the walked route is dry or at most
`kSwimDepthYd` 2.03128 yd deep (surface minus the liquid's own `depth_level`) and none is `UNDER_WATER` (swimming); the
destination may stand in the shallows, never in swimming water. `kSwimDepthYd` is ForeverCore's own swim band: its liquid
read marks a unit `UNDER_WATER` when the surface is more than its collision height above it (`GridMap.cpp:677-680`,
`delta > collisionHeight`), and the module's liquid read asks with the core's default
`DEFAULT_COLLISION_HEIGHT` 2.03128 (`Object.h:78`); it replaces the 1.5 yd ported from the 1.12 core's `IsSwimmable`.
The order's own origin flag (`Party::fromRun`) is the gate: only the run's self-issued moves carry the permission
(a plan row's walk, a regroup's gather, an approach stand, the return to the plan point, the follow-up after a pull, a
`walk <bot>`), never an order matched by its text, so a player's `.bot cmd move` keeps the RFC rule (the prefix escape,
the dry destination, the pause "unreachable: ... crosses or ends in liquid"). The per-row wet bit
(`ai::kPlanFlagWadeOk`) is plan data now, not the gate: live run 4's step 60 crossed a 0.2 yd cell on a leg with no bit.
Deep water, lava, RFC and every other plan behave exactly as before; `test_run_liquid` pins the shallow, deep and
swim-band cases, the step-60 cell and the cross-leg regroup for a run-issued move (pass) against the same move from a
human commander (pauses), and `test_run_orders` pins the origin gate and the loader's flag kinds. Every liquid refusal logs
the sampled depth, the core's status and the position (section 9).

The first sub-hop of a wet row logs once what it sampled, so the next live run's log says why a river hop walked or
paused:

```
TortoiseBots run: Wctank wade hop step=32 leg=4: depth=0.4 yd status=0x01 at -30.7 281.3 -105.1 (12 of 34 samples wet, waded)
```

(`depth` is the deepest wet sample's surface minus the ground under it, `status` the core's liquid bits there, `refused`
when that sample is swimming water or deeper than 2.03128 yd. One line per row: the step index dedupes the sub-hops.)

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
| `test_run_orders` (the gossip verb; the wade-hop predicate) | PASS, 276 checks (`RowHopWades`: the row's own point, an approach hop's middle point, a `TooFar` leg, a foreign walk, a flagless row, a hold row) |
| `test_run_facts` (Ragefire Chasm, unchanged) | PASS |
| `test_run_facts_s9`, `test_run_liquid`, `test_run_cancel`, `test_run_binding`, `test_run_gather`, `test_run_c6` | PASS (`test_run_liquid` pins the wade rule: shallow wades, deep/swimming refuses, **a shallow middle point of the path wades while a deep one pauses**, and a wet shallow hop without the flag still pauses; the kept log sample is the deepest wet one) |
| `test_dungeon_plan_policy` | PASS (the wade bit on move/pull/boss only, its own bit, and with the skip-unreachable bit) |
| `test_wc_plan_rows.py` | 3505 checks, 0 failed (the committed SQL is the generator output; extent, back-leg continuity, flags, 193 spawns; the base rows pinned to the agent route fixture at 0.15 yd; the 21 wade hops are exactly the rows with the bit; **every generated text field fits its varchar from the plan tables' own schema**) |

## 6. Gates (build VM, core d02645dd21, this worktree)

| gate | result |
|---|---|
| build ON (`slot-sync-build.sh 4 tortoisebots -DTORTOISEBOTS_SLICE1=ON`) | `CMAKE_RC=0`, `MAKE_RC=0`, 710 s (it waited for the shared tree of the same core and options, then recompiled the module); key `core-9b88bd2d6584-opt-85c8cb4e`, 64 changed/new/deleted module files |
| build OFF (`slot-sync-build.sh 4 tortoisebots`) | `CMAKE_RC=0`, `MAKE_RC=0`, 17 s; key `core-9b88bd2d6584-opt-78bf025f` |
| slot 8 (the brief's slot number) | **not a core this branch builds on**: `~/forever-build/src-8` holds the older 25fc7ca core (no `WorldSession::IsKickRequested`, no `ScriptMgr::OnAfterReload(std::string_view)`), so `TbBotSession.cpp:297` and `TbScripts.cpp:152` (both untouched by this round) fail with `MAKE_RC=1`. The pinned d02645dd21 core is in slots 1, 2 and 4, as the last round used (slot 4) |
| `slice_gen_context.py --check`, `slice_registry_check.py` | `SliceAiObjectContext.cpp is up to date`; the registry check unchanged (228 keys to register or stub later) |
| `python3 forever/tools/test_wc_plan_rows.py` | 3505 checks, 0 failed |
| `python3 tools/verify_okf.py` | `OKF verification PASSED (60 nodes validated, graph edges consistent)` |
| `git diff --check` | clean |
| the run/policy/wiring tests incl. the WC ones and RFC's unchanged | PASS (section 5) |

### The fix round after the live run (2026-11-21, this head)

| gate | exact line |
|---|---|
| build ON, slot 4: `~/forever-build/slot-sync-build.sh 4 tortoisebots -DTORTOISEBOTS_SLICE1=ON` | `combined build: slot 4 key core-9b88bd2d6584-opt-85c8cb4e` / `build token 1 of 3 (slot 4)` / `module sync: 5 changed/new/deleted files` / `CMAKE_RC=0` / `MAKE_RC=0` / `TOTAL_SECONDS=44`; re-run on the committed tree (docs only): `build token 2 of 3 (slot 4)` / `module sync: 2 changed/new/deleted files` / `CMAKE_RC=0` / `MAKE_RC=0` / `TOTAL_SECONDS=3` |
| build OFF, slot 5: `~/forever-build/slot-sync-build.sh 5 tortoisebots` | `combined build: slot 5 key core-9b88bd2d6584-opt-78bf025f` / `build token 1 of 3 (slot 5)` / `module sync: 300 changed/new/deleted files` / `CMAKE_RC=0` / `MAKE_RC=0` / `TOTAL_SECONDS=24`; re-run on the committed tree: `build token 2 of 3 (slot 5)` / `module sync: 2 changed/new/deleted files` / `CMAKE_RC=0` / `MAKE_RC=0` / `TOTAL_SECONDS=5` |
| `test_run_orders` | `RESULT: PASS (276 checks)` |
| `test_run_liquid` | `RESULT: PASS (test_run_liquid)` |
| `test_run_facts` (Ragefire Chasm, unchanged rows) | `RESULT: PASS` |
| `test_run_wc` | `test_run_wc: 727 checks, 0 failed (plan: 302 steps, 31 legs, 193 unit rows)` |
| `python3 forever/tools/test_wc_plan_rows.py` | `test_wc_plan_rows: 3505 checks, 0 failed` |
| `python3 tools/verify_okf.py` | `OKF verification PASSED (60 nodes validated, graph edges consistent)` |
| `git diff --check` | clean |

## 7. The live run (run 2, core 25fc7ca, the previous worker's build) and this round

The earlier live run covered 6 of 490 steps (4 pulls, 1 wipe, stopped by hand on a loaded VM): Kresh, the ramp, the whole
event and the gossip were **not** reached live. The route then was the Turtle navmesh route; this round replaces it with
the route the ForeverCore server proved (section 2), adds the escort failure, and fixes the hold facts. **No live run was
made in this round** (the brief: the lead runs it next on a test server away from the build VM). The first live check
should be `.tb plan wailing_caverns check` (expect units 193 matched 193) and then `.tb run start ... wailing_caverns`,
after the setup contract of section 8 (party level and kit, bags, binding, everything logged in and in the instance). The
live run of 2026-11-21 (run 3) used this route on the previous head; what it did, the deaths and the fixes it drove are
section 8.
The river hops are wade rows now (section 2): the run walks its first 21 wet hops instead of pausing, and still pauses
with the unchanged "unreachable: ... crosses or ends in liquid" if the core's own liquid read finds one of them deeper
than 1.5 yd or swimming; the wade permission now covers every sampled point of the hop, and the first wet sub-hop logs
its depth and liquid status.

The server was stopped after the earlier run, the dump restored (`zstd -dc dumps/wc-20261120T212230Z.sql.zst | mysql`),
the previous binary put back, the new plan SQL removed from `sqlsrc` (0 files left), `etc/worldserver.conf` restored
(`MaxCoreStuckTime = 60`), the databases booted once more (the boot-check: `READY`, then `DONE`) and the server stopped
(no `forever-world2` container). The five test characters (`Mwctank`, `Mwcpri`, `Mwcdps`, `Mwcmag`, `Mwcrog`) and their
gear went back with the dump.

**Slots:** builds in this round go only through `~/forever-build/slot-sync-build.sh 4` on yulon-ubuntu2 (the brief's
slot lock); no build ran outside it.

## 8. The live run of 2026-11-21 (run 3, code 3b5b41f76) and the deaths

Result: PARTIAL, 32 of 302 steps, 23 of 87 pulls, 7 deaths, 1 wipe, 4 pauses; Kresh dead (leg 3 boss step), stopped
at leg 4 `cobrahn_b` step 32 (the first river hop, 4/0) by the liquid pause the wade fix in section 2 now removes. The
run's own record (`notes/live-wc-2026-11-21.md`) itemizes two of the seven deaths; the total is its own counter.

| deaths | where | what killed whom | verdict |
|---|---|---|---|
| 5 (the wipe, 03:41-03:44) | step 5 = leg 0 `anacondra_a` seq 5, `pull group 2` at -72.2 214.8 | two **Deviate Ravagers 3636** (18-19 elite, spawns 4.8 and 11.8 yd from the pull point); no other elite pack within 20 yd (a lvl-1 Snake at 8.6 and a lvl-1 Biletoad at 15.7) | not the plan: a 2-unit pull, no missing hold (the run rests between pulls), the spot is the agent's centroid. Five level-25 bots dying to two 18-19 elites is a party failure |
| 1 (Wctank, step 26) | step 26 = leg 2 `cobrahn_a` seq 2, `pull group 1` at -52.33 266.97, radius 23.1 | the **Anacondra group**: Lady Anacondra 3671 + two Druid of the Fang 3840 + one Ravager 3636, 3.5-13 yd from the spot (4 elites, one a boss) | mostly party: a 4-elite pull needs a tank that holds and a healer that sustains. The group is the agent's and the Druids heal, but `kill_first = 3671` (Anacondra first) was kept: moving the Druids ahead of her in `kill_first` was rejected because a kill_first entry also forbids deferring every pull that contains it (`PullMayBeDeferred`), and the evidence does not show the Druids out-healing the damage |
| 1 (not itemized) | not named by the evidence | — | the run's counter is 7; only the wipe's 5 and Wctank's 1 are named, and this one is not claimed |

So both named events say the same thing: the pulls were the agent's own (2 and 4 units, correct radii and entries), so
the plan is not the cause, and the party was under-equipped: 13 gear slots upgraded for the whole five-bot party, and
the priest's bags still held junk (the start was refused once for 11 free slots where 12 are needed). No plan row was
changed for these deaths.

### Party advice for the next run (not class code, a setup contract)

* Level **26-28** before the run (`.tb level 28` + `.tb train` after it), and let each bot's engine settle at that level
  before the start (engine on, one full tick, then the run). Level 25 with the last run's gear is not enough for a 4-elite
  pull.
* A **full kit in the bags first**, then `.tb gear party upgrade`: it only equips what the bots' bags hold, and the last
  run's bags held a weapon and a few pieces, so it filled 13 slots for the whole party. Add every armour slot at the
  party's level (green), a weapon each and a shield for the warrior tank (`.tb additem`), and verify the equipped slots
  before the start.
* Consumables: health/mana potions and bandages on every bot (the healer's mana pool is the Anacondra group's fight).
* Empty the bags (12 free slots on the priest is the start's own gate), and make sure no bot is re-logged after the run
  starts.
* Then `.tb plan wailing_caverns check` (expect `units 193 matched 193 near 0 missing 0`), `.tb run start ...
  wailing_caverns`, and watch the first river hop's new line (section 2): `wade hop step=32 leg=4: depth=... status=...`.

### The setup step the "not-bound" pause was missing

The run binds the members it finds **commanded** to **the lead bot** (`SyncBinding` -> `BindMember`), but the move judge
expected the lead's own `.tb master` commander when the lead had one: with the live setup's `.tb master party <player>`
(which binds the lead too), the first party move after the start paused `not-bound: Wcdpsa is not bound to the lead bot
as its commander`; the second `.tb master party` re-bound the members to the player, which the run then left alone
(`SyncBindingAction`: a commander someone else set is `Other` -> `Keep`), and the run held. The judge's expectation is now
always the lead bot, the binding the run makes and the one the refusal names, so either setup works.

For the live steps: log every member in, group them and have each one **commanded before `.tb run start`** (`.tb master
party <lead bot>` — the run's own expectation — or the player's character, which the run rebinds to the lead); a bot with
no commander is not bound by the run and pauses the first party move. Keep every member in the instance, and re-bind a
member that was re-logged (a re-login loses its commander) before resuming.

## 9. The fix round after live run 2 (2026-11-21)

Evidence: `~/TortoiseBots-audit/notes/live-wc-run2-2026-11-21.md` (run 2, code 366a4a420, core d02645dd21) and run 1's
`live-wc-2026-11-21.md`. Branch `run/wc`, zero core changes, no live run in this round (the lead queues run 3).

### 9.1 The run's own moves wade the leg's wet area (the run-2 pause)

Run 2 paused after the Druid fight on
`cmd move -37.2 291.3 -104.8 refused: unreachable: The lead bot's way to that point crosses or ends in liquid (lava or
water) at -35.9 289.0 -105.4: no walkable path avoids it`, with the lead at -21.6 251.4 -104.2 (`.tb where`: dry). The
move is one the run makes on its own inside the leg (a regroup/approach move after a pull), not a point of a row's own
walk, so `RowHopWades` said no although the leg's flagged hop (leg 4 seq 0, -3.641 229.268 to -61.367 323.721) is the
shallow river crossing there -- both points stand 4-6 yd off that hop's line.

What changed:

* `runtime/DungeonRunPolicy.h`: `kWadeAreaYd` (12.0 yd), `WetAreaSegmentYd` (horizontal point-to-segment),
  `LegWetAreaAt` (within 12 yd of the own hop -- the row and the plan row before it -- of a `kPlanFlagWadeOk` row of the
  current step's leg; move/pull/boss rows only), and `MoveMayWade` = `RowHopWades || LegWetAreaAt`.
* `forever/src/host/run/TbRunOrders.cpp`: `WadeOrder` asks `MoveMayWade` for the lead's `cmd move` and the run's own
  `walk <bot>`; a player's order (`fromRun` false) and a plan whose leg has no flagged row keep the RFC rule. The wade
  rule itself is untouched: every sample of the walked route must be dry or shallow (depth at most `kWadeDepthYd` 1.5 yd,
  never `UNDER_WATER`), so deep water still pauses even inside a wet area.
* Every liquid refusal now logs the core's sample once (`TbRunLiquid.h` `LiquidProbe` / `ProbeOnMap`), for the lead's
  move, a bot walk and a direct step:

```
TortoiseBots run: Wctank move refused: liquid depth=0.5 yd status=0x01 level=-105.9 at -35.9 289.0 -105.4 (step=50 leg=5)
```

(`depth` is surface minus the ground under it, `status` the core's bits, `level` the surface; a move with no terrain to ask
logs `liquid no terrain at ...`.)

Tests (`forever/tools/test_run_liquid.cpp`, composed exactly as the executor chooses): a regroup move across shallow
water on a wading leg passes (`MoveMayWade` true + `PathWadeRefused` false); the same move on a dry leg over water pauses
(`MoveMayWade` false + the RFC prefix rule true); deep water always pauses (permission true + `PathWadeRefused` true).

### 9.2 Step 7 (Deviate Guardians): the pull spot stood between two packs

Run 2's deaths at step 7 (leg 0 `anacondra_a` seq 7, pull group 4): **Wctank, Wcpri, Wcrog, Wcmag, then Wcdpsa, all
five**, at party level 28 with green gear. The row is the agent's group centroid -35.1 203.85 radius 21.4 entries
`3637,3636`; the live spawns around it (map 43 dump) are:

| spawn | entry | position | from the agent's point |
|---|---|---|---|
| 20026217 | 3637 Deviate Guardian | -35.194 198.074 -96.579 | 5.8 yd |
| 20026227 | 3636 Deviate Ravager | -35.020 209.613 -96.363 | 5.8 yd |
| 20026229 | 3640 Evolving Ectoplasm (patrol) | -34.646 211.374 -96.066 | 7.5 yd (1.8 yd from the Ravager) |
| 20026221 | 3637 Deviate Guardian | -32.535 173.325 -96.311 | 30.6 yd (step 8's pack) |

So the agent's point is the centroid of two sub-packs 11.5 yd apart, and a patrolling Evolving Ectoplasm (an entry the row
does not name) stands inside the Ravager's pack: the party stood in the middle and woke all three (the core's family
assistance radius is 10 yd; the third is 13.4 yd from the Guardian but 1.8 yd from the Ravager). That is the "more mobs
than the plan intends" case, not a radius artefact.

The correction (`PULL_SPOT_ADJUST` in `forever/tools/wc_plan_rows.py`, applied after the route is read; the route file
stays the agent's export): the stand moves 11.3 yd south to **-34.700 192.600 -96.370** (on the route's next hop,
step 7 -> step 8; dry; 5.5 yd from the Guardian, 17.0 yd from the Ravager, 18.8 yd from the Ectoplasm) and the reach
drops from 21.4 to **18.0 yd** so the next Guardian spawn at 19.4 yd stays step 8's pack and the unit rows do not move
(still 2 for the row, 193 in all). Both hops (step 6 -> the stand 44.5 yd, the stand -> step 8 24.9 yd) plan `complete` on
the ForeverCore mmaps, and `test_wc_plan_rows.py` pins the plan row to the corrected stand while the fixture keeps the
agent's point (one documented exemption, checked by the test). Pulling the Guardian alone wakes nothing (its assist ring
is empty); the Ravager and the Ectoplasm are fought together afterwards, as they would have been. Entries, units and the
route are unchanged.

### 9.3 The whole-route probe over all 302 steps

Two probes on the server's own ForeverCore v16 mmaps (`~/wcprobe-mmaps`, the same geometry as
`forever/data/wc-wet-hops.tsv`), over the regenerated 302 steps:

* **The hop probe** (every consecutive pair, the run's point rule, path vertices and ground samples every <= 1.5 yd)
  lists the **same 21 wet hops** as `wc-wet-hops.tsv`, i.e. the moved step 7 and its two new hops are dry and no other
  wet hop appeared or disappeared (the file is still exact).
* **The wet-area probe** (every step point, a 25 yd disc at 2.5 yd, the same liquid read at the step's own height -- the
  approach stand's reach): **149 wet cells; 62 inside a flagged hop's 12 yd area, 87 outside**. Every one of the 87 is
  **deep**: no floor within 40 yd under the water (the river and pool cells at the Kresh/cobrahn_b/pythas_a crossings and
  the Skum pit) or 1.9-4.2 yd deep (the Mutanus chamber's pond, 34 cells around the boss row). **Zero shallow wet cells
  lie outside a flagged wet area**, so no regroup/approach move can wade where the leg did not flag -- and every deep one
  still pauses by the unchanged wade rule. Listed by step (outside cells): leg 3 seq 0 (1), seq 1 (1); leg 4 seq 0 (4);
  leg 6 seq 1 (3), seq 3 (8), seq 4 (6), seq 7 (3), seq 8 (8), seq 9 (10), seq 10 (8); leg 9 seq 11 (1); leg 30 seq 0
  (34).

### 9.4 Gates (build VM, core d02645dd21, slot 4)

| gate | exact line |
|---|---|
| build ON, slot 4: `~/forever-build/slot-sync-build.sh 4 tortoisebots -DTORTOISEBOTS_SLICE1=ON` | `combined build: slot 4 key core-9b88bd2d6584-opt-85c8cb4e` / `build token 2 of 3 (slot 4)` / `module sync: 306 changed/new/deleted files` / `CMAKE_RC=0` / `MAKE_RC=0` / `TOTAL_SECONDS=93` |
| build OFF, slot 4: `~/forever-build/slot-sync-build.sh 4 tortoisebots` | `combined build: slot 4 key core-9b88bd2d6584-opt-78bf025f` / `build token 2 of 3 (slot 4)` / `module sync: 370 changed/new/deleted files` / `CMAKE_RC=0` / `MAKE_RC=0` / `TOTAL_SECONDS=15` |
| `test_run_orders` | `RESULT: PASS (276 checks)` |
| `test_run_liquid` | `RESULT: PASS (test_run_liquid)` |
| `test_run_facts` (Ragefire Chasm, unchanged rows) | `RESULT: PASS` |
| `test_run_wc` | `test_run_wc: 727 checks, 0 failed (plan: 302 steps, 31 legs, 193 unit rows)` |
| `python3 forever/tools/test_wc_plan_rows.py` | `test_wc_plan_rows: 3507 checks, 0 failed` |
| `python3 tools/verify_okf.py` | `OKF verification PASSED (60 nodes validated, graph edges consistent)` |
| `git diff --check` | clean |

## 10. The fix round after live run 3 (2026-11-21, code 6c7434317, core d02645dd21)

Evidence: `~/TortoiseBots-audit/notes/live-wc-run3-2026-11-21.md` (live run 3; 52 of 302 steps, 38 of 87 pulls,
15 deaths, 0 wipes, 2 pauses, Kresh dead). Branch `run/wc` first merged `tbf/main` (#97, #99, #100, #101; both sides
kept), zero core changes, no live run in this round. Section 9 is the earlier round's history; the function it names
(`LegWetAreaAt`) is now `PlanWetAreaAt`.

### 10.1 The run's own regroup wades the plan's wet area (the run-3 pause)

The run paused at 09:17:45 on step 52 (leg 5 `cobrahn_c`) with
`cmd move -37.2 291.3 -104.8 refused: unreachable: The lead bot's way to that point crosses or ends in liquid (lava or
water) at -35.9 289.0 -105.4: no walkable path avoids it`, the refusal logging `liquid depth=0.5 yd status=0x01
level=-105.9`. The lead stood dry at -21.6 251.4. The move is the run's own return from the south bank after the lead
was revived away from the party; it crosses the river hop flagged in **leg 4** (seq 0, -3.641 229.268 to -61.367
323.721) while the current step is **leg 5**, so the per-leg `LegWetAreaAt` found no flagged row and the RFC rule
refused a 0.5 yd wade (same move as run 2).

What changed:

* `runtime/DungeonRunPolicy.h`: `LegWetAreaAt` -> **`PlanWetAreaAt`** (within `kWadeAreaYd` 12 yd of the own hop of any
  `kPlanFlagWadeOk` row of the plan, any leg), and `MoveMayWade` now takes the **order's origin flag**:
  `MoveMayWade(runIssued, run, row, x, y) = runIssued && (RowHopWades || PlanWetAreaAt)`. The flag is set where the
  executor builds the order, never read from its text.
* `forever/src/host/run/TbRunOrders.cpp`: `WadeOrder` passes `ctx.fromRun` (the origin flag, `Party::fromRun`) to
  `MoveMayWade`, for the lead's `move`/`move direct` and the run's own `walk <bot>`. A `.tb cmd move` a player sends
  (`fromRun` false) keeps the RFC rule; so does a plan with no flagged row.
* `forever/src/host/run/TbRunOrders.h`: `Party::fromRun` is documented as the order's origin (run-issued vs a player's).
* The wade rule itself is unchanged: every sample dry or shallow (at most `kWadeDepthYd` 1.5 yd, never `UNDER_WATER`),
  the destination dry or in the shallows. Deep water and swimming still pause.
* Safety of the wider area: section 9.3's wet-area probe over the whole plan found 149 wet cells, 62 inside a flagged
  hop's 12 yd area and **87 outside, every one deep** (zero shallow). So the plan-wide area adds exactly the shallow
  cells a flagged river hop already wades; the deep ones still pause by the wade rule.

Tests (`forever/tools/test_run_liquid.cpp`): the run's regroup move at -37.2 291.3 passes (`MoveMayWade(true, ...)` +
`PathWadeRefused` false) on the same leg and on the live cross-leg shape (step in leg 1, hop in leg 0); the same move
from a human commander pauses (`MoveMayWade(false, ...)` false + the RFC prefix rule true); deep water always pauses;
a flagless plan has no wet area and the same move pauses. `test_run_orders` pins `RowHopWades` unchanged.

### 10.2 The 15 deaths (what the evidence names, and the plan rows)

The run's counter is 15 deaths, 0 wipes. The notes name two groups and no individual deaths beyond that:

| deaths | step / pull | killer named by the evidence | plan row the cause? |
|---|---|---|---|
| 1-5 | step 7, leg 0 `anacondra_a` seq 7, pull group 4 (entries 3637, 3636) at the corrected stand -34.700 192.600, r 18.0 | the Deviate Guardians pack ("Deviate Guardians pack"; run 2 named Deviate Guardian 3637 as the killer) | **no**: the row was corrected in section 9.2 (stand moved 11.3 yd south, radius 21.4 -> 18.0; the Ravager 17.0 yd, the patrolling Ectoplasm 18.8 yd stays outside, the next Guardian 19.4 yd is step 8's). Run 3's 5 deaths have no killer/add detail; the row still pulls the same 2 units, so no further row change is justified by this evidence |
| 6-10 | legs 2 and 4, pull rows not named | not named (the notes say "the rest across legs 2, 4 and 5, mostly Wctank") | cannot be judged: no killer, no pull, no victim list in the evidence; **no change** |
| 11-15 | step 50, leg 5 `cobrahn_c` seq 2, pull group 1 (entries 3840, 5048) at 20.100 523.100, r 12.9 | the Druid of the Fang 3840 + Deviate Viper 5048 pack (the notes name "step 50 pull 5048"; run 2 named the Druids) | **no**: the map-43 spawn dump puts exactly 4 units inside the 12.9 yd radius (2x 3840 at 9.2/9.3 yd, 2x 5048 at 5.7/5.0 yd); the next 3840 at 20.5 yd and a 2914 at 19.0 yd are outside, so no linked pack joins by the row's radius. The named pack is a 4-unit pull with `3840` in the plan's CC priority; the evidence shows no add and no row defect |

No plan row was changed this round: the one clear row defect (step 7's stand) was already fixed in section 9.2, and the
step 50 pull is the row's own 4 units (no linked pack) at the agent's point. The remaining deaths have no killer or pull
in the evidence to attribute, and adding a guess would move the plan on no data.

### 10.3 Gates and tests

| gate | exact line |
|---|---|
| build ON, slot 4: `~/forever-build/slot-sync-build.sh 4 tortoisebots -DTORTOISEBOTS_SLICE1=ON` | `combined build: slot 4 key core-9b88bd2d6584-opt-85c8cb4e` / `build token 2 of 3 (slot 4)` / `module sync: 363 changed/new/deleted files` / `CMAKE_RC=0` / `MAKE_RC=0` / `TOTAL_SECONDS=391` (`make.log`: 0 `error:`/`FAILED:`, 1 pre-existing `sTerrainMgr` macro warning) |
| build OFF, slot 5: `~/forever-build/slot-sync-build.sh 5 tortoisebots` | `combined build: slot 5 key core-9b88bd2d6584-opt-78bf025f` / `build token 3 of 3 (slot 5)` / `module sync: 363 changed/new/deleted files` / `CMAKE_RC=0` / `MAKE_RC=0` / `TOTAL_SECONDS=37` (`make.log`: 0 `error:`/`FAILED:`) |
| worldserver link, slot 7: `~/forever-build/slot-sync-build.sh 7 worldserver -DTORTOISEBOTS_SLICE1=ON` | `combined build: slot 7 key core-9b88bd2d6584-opt-85c8cb4e` / `build token 2 of 3 (slot 7)` / `module sync: 363 changed/new/deleted files` / `CMAKE_RC=0` / `MAKE_RC=0` / `TOTAL_SECONDS=90`; `make.log`: `[56/56] Linking CXX executable bin/Release/bin/worldserver`, 0 `error:`/`FAILED:` (73,107,896-byte binary in `build-7/bin/Release/bin/worldserver`) |
| `test_run_orders` | `RESULT: PASS (276 checks)` |
| `test_run_liquid` | `RESULT: PASS (test_run_liquid)` |
| `test_run_facts` (Ragefire Chasm, unchanged rows) | `RESULT: PASS` (`plan=1 leg=9 step=84 boss=4`) |
| `test_run_wc` | `test_run_wc: 727 checks, 0 failed (plan: 302 steps, 31 legs, 193 unit rows)` |
| `python3 forever/tools/test_wc_plan_rows.py` | `test_wc_plan_rows: 3507 checks, 0 failed` |
| `python3 tools/verify_okf.py` | `OKF verification PASSED (60 nodes validated, graph edges consistent)` |
| `git diff --check` | clean |

## 11. The fix round after live run 4 (2026-11-21, core d02645dd21)

Evidence: `~/TortoiseBots-audit/notes/live-wc-run4-2026-11-21.md` (live run 4; furthest step 60 of 302, 43 of 87 pulls,
bosses Kresh 3653 and Cobrahn 3669 dead, 31 deaths, 2 wipes, 1 pause + 1 re-pause). Branch `run/wc` first merged
`tbf/main` (both sides kept), zero core changes, no live run in this round.

### 11.1 A run-issued move wades shallow water on any leg

The run paused on step 60 (leg 6 `pythas_a`) with
`cmd move -30.3 133.5 -105.1 refused: unreachable: ... liquid (lava or water) at -28.5 135.4 -105.4` (sampled depth
0.2 yd, status 0x01, level -105.9; the lead stood dry at -8.7 157.1). The cell lies outside every flagged hop and the leg
carries no wet bit, so the run-3 area rule refused a 0.2 yd puddle.

The binding decision replaces the wet-area gate: a move the **run issues for itself** (`Party::fromRun`) may wade shallow
water on **any** leg of a dungeon run. The per-row wet bit `ai::kPlanFlagWadeOk` stays plan data (the probe's record);
it no longer gates. The wade rule judges every sample of the walked route: dry, or at most `kSwimDepthYd` deep, never
`UNDER_WATER`; deep water and swimming still pause. A human's `.tb cmd move` (`fromRun` false) keeps the RFC liquid rule;
RFC and the other plans keep their rows and their tests.

`kSwimDepthYd` is ForeverCore's own swim threshold, not a guess: `GridMap::GetLiquidStatus` marks a unit `UNDER_WATER`
when the liquid surface is more than its collision height above it (`GridMap.cpp:677-680`, `delta > collisionHeight`), and
the module's liquid read asks with the core's default `DEFAULT_COLLISION_HEIGHT` 2.03128 (`Object.h:78`). It replaces the
1.5 yd ported from the 1.12 core's `IsSwimmable` (ForeverCore has no such call).

What changed:

* `runtime/DungeonRunPolicy.h`: the plan-wide/per-leg wet-area helpers (`kWadeAreaYd`, `WetAreaSegmentYd`, `PlanWetAreaAt`,
  `LegWetAreaAt`, `RowHopWades`) are gone; `MoveMayWade(bool runIssued)` is the origin gate.
* `forever/src/host/run/TbRunOrders.cpp`: `WadeOrder(ctx)` is `ctx.fromRun` with a live plan/step, no point or leg test;
  the lead's `move`/`move direct` and the run's `walk <bot>` use it. The wade trace still logs the deepest wet sample once
  per step.
* `forever/src/host/run/TbRunLiquid.h`: `kSwimDepthYd` 2.03128 (the core's `UNDER_WATER` band) replaces `kWadeDepthYd`
  1.5; the `UNDER_WATER` refusal is unchanged, and a wade may only cross water or ocean (lava, slime and dark water are
  refused at any depth, so RFC's lava rule is unchanged).
* `forever/tools/test_run_liquid.cpp`: the step-60 move (`-8.7 157.1` to `-30.3 133.5`, 0.2 yd at -28.5 135.4) passes for
  a run-issued move on an unflagged leg, the same move over deep water pauses, the same 0.2 yd cell from a human commander
  follows the RFC rule; the depth cases move to the 2.03128 band.
* `forever/tools/test_run_orders.cpp`: the `RowHopWades` section becomes the origin-gate check plus the loader's flag
  kinds (`ai::kPlanFlagWadeOk` still accepted on move/pull/boss, refused on a hold).

### 11.2 The 31 deaths and 2 wipes: per pull, and the plan rows

The step indexes are the plan's 0-based rows; `units in radius` is the map-43 spawn dump inside the row's radius (the
evidence's named pull units plus any unlisted spawn the radius takes). The advised level is 26-28 (section 9); the party
was 28 with the green kit, so the level advice is met on every row and is not the cause of a wipe.

| step | row (leg seq, entries, spot, radius) | units in radius | deaths (victims) | plan row the likely cause? |
|---|---|---|---|---|
| 4 | 0/4, 3636, -106.250 234.450 -91.350, r 16.0 | 2x 3636, 1x 2914 | 2 (Wctank, Wcpri) | no: the row is the two Ravagers; the 2914 inside 4.7 yd is an unlisted add, and 2 deaths on a 2-unit row is the pack strength, not the row |
| 10 | 0/10, 3636, -53.600 119.100 -89.500, r 11.1 | 1x 3636 | 5 (all) = **wipe 1** | no clear defect: the row is the smallest pull of the leg (one unit), its spot is the spawn, and the nearest other pack (3671 at 14.6 yd, 5053 at 15.6 yd) is outside the radius. Why a single Ravager wiped the party is an open question (section 11.2) |
| 14 | 1/1, 3637,3636, -26.450 91.100 -90.950, r 23.0 | 1x 3636, 1x 3637 | 1 (Wctank) | no: a 2-unit row at a wide agent centroid; the deaths are the pack |
| 21 | 1/8, 3637, 30.500 269.400 -87.200, r 11.3 | 1x 3637 | 1 (Wctank) | no: a one-unit row at its spawn |
| 39 | 4/7, 3840,5755, 6.100 447.600 -76.300, r 10.2 | 1x 3840, 1x 5755 | 1 (Wctank) | no: the row's own 2 units |
| 45 | 4/13, 8886,3840, 51.530 400.970 -62.900, r 21.7 | 2x 8886, 1x 2914 | 1 (Wctank) | no: the 3840 sits outside the radius; the row pulls its two 8886 |
| 50 | 5/2, 3840,5048, 20.100 523.100 -57.150, r 12.9 | 2x 3840, 2x 5048 | 3 (Wctank, Wcpri, Wcmag) | no: exactly the row's 4-unit Druid/Viper pack, no add; section 10.2 already checked it |
| 51 | 5/3, 5755, -18.600 493.600 -54.900, r 12.3 | 1x 5755 | 7 (Wcdpsa twice) | no visible row defect: the radius holds one Viper and no listed pack; 7 deaths on a one-unit row needs the lost log to explain (open question) |
| 54 | 5/6, 3840,5048,5755, -80.030 471.570 -66.870, r 17.5 | 1x 3840, 1x 5048, 1x 5755 | 5 (all) | no: the row's own 3-unit pack |
| 55 | 5/7, 8886,5755,3840, -109.020 450.770 -72.400, r 17.5 | 3x 3840, 2x 8886, 1x 5755, 1x 2914 | 6 **wipe 2** | plausible pull size (7 units, the largest pull of the run, with an unlisted 2914 at 4.6 yd), but the step follows a 5-death step 54 and step 51's one-unit row produced 7 deaths: the evidence cannot separate the row from the unrecovered party, so **no change** (open question) |

No plan row or SQL was changed: every wipe falls on a row whose radius holds the row's own listed pack (step 10 even holds
one unit), the advised level is met, and the one large pull (step 55) cannot be reduced by its radius without losing its
listed units (the 2914 at 4.6 yd is between them). A row edit on this evidence would move the plan on a guess.

Open questions (for the next live run's log):

1. **Wipe 1 (step 10):** what engaged the party on a one-unit row? The nearby 3671 (14.6 yd) and 5053 (15.6 yd) are
   outside the 11.1 yd radius; a fear/patrol or a pull started from the previous row's fight could add them. The world log
   was lost (`world.sh stop` after `server shutdown` truncates `logs/world.out`; copy it before the stop).
2. **Wipe 2 (step 55):** is the 7-unit pull (3x 3840, 2x 8886, 1x 5755, 1x 2914) the cause, or was the party already
   broken by step 54's 5 deaths? A split of the row needs the actual engaged set and the party's health at the pull.
3. **Step 51:** 7 deaths (Wcdpsa twice) on a one-unit Viper row; the evidence names no add. Without the log the engaged
   units and the killer are unknown.
4. **Death counts:** the run counter says 31, the per-pull events sum to 32 (the 7 at step 51 include Wcdpsa twice) and
   `bot.log` has 32 `T:death` lines. Is the counter counting a re-death of the same bot once (or one death during a wipe
   as not a new death)? Needs the log to settle.

### 11.3 Gates and tests

| gate | exact line |
|---|---|
| module ON, slot 4: `tar -C ~/wt-run-wc --exclude=.git -cf - . \| ssh yulon-ubuntu2 "~/forever-build/slot-sync-build.sh 4 tortoisebots -DTORTOISEBOTS_SLICE1=ON"` | `combined build: slot 4 key core-9b88bd2d6584-opt-85c8cb4e` / `build token 1 of 3 (slot 4)` / `module sync: 28 changed/new/deleted files` / `CMAKE_RC=0` / `MAKE_RC=0` / `TOTAL_SECONDS=82` |
| module OFF, slot 5: `... slot-sync-build.sh 5 tortoisebots` | `combined build: slot 5 key core-9b88bd2d6584-opt-78bf025f` / `build token 1 of 3 (slot 5)` / `module sync: 475 changed/new/deleted files` / `CMAKE_RC=0` / `MAKE_RC=0` / `TOTAL_SECONDS=14` |
| worldserver link, slot 7: `... slot-sync-build.sh 7 worldserver -DTORTOISEBOTS_SLICE1=ON` | `combined build: slot 7 key core-9b88bd2d6584-opt-85c8cb4e` / `build token 1 of 3 (slot 7)` / `module sync: 1 changed/new/deleted files` (the shared key tree already held the module from slot 4) / `CMAKE_RC=0` / `MAKE_RC=0` / `TOTAL_SECONDS=10`; `make.log`: `[2/2] Linking CXX executable bin/Release/bin/worldserver`, 0 `error:`/`FAILED:` (74,904,128-byte binary) |
| `test_run_orders` (VM, slot 4 tree) | `RESULT: PASS (270 checks)` |
| `test_run_liquid` (VM, slot 4 tree) | `RESULT: PASS (test_run_liquid)` |
| `test_run_facts` (Ragefire Chasm, unchanged rows) | `RESULT: PASS` (`table: 251 fields; config 9, filled 98, forced 27, never 5, notknown 14, skipped 98`) |
| `test_run_wc` | `test_run_wc: 727 checks, 0 failed (plan: 302 steps, 31 legs, 193 unit rows)` |
| `python3 forever/tools/test_wc_plan_rows.py` | `test_wc_plan_rows: 3507 checks, 0 failed` |
| `python3 tools/verify_okf.py` | `OKF verification PASSED (60 nodes validated, graph edges consistent)` |
| `git diff --check` | clean |

Disk before the gates: `/mnt/tb` 59G, 11G available (81% used); `/mnt/forever` 79G, 7.1G available (91% used); no gate refused for space.

