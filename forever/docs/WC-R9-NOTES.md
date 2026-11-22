# WC run 9: the fix round after live run 8 (2026-11-22)

Base `tbf/run/wc-r8` (run 7's wipe resume and leg-5 camp order, plus `tbf/main`), branch `run/wc-r9`. Evidence: the run 6, 7 and 8
notes (`~/TortoiseBots-audit/notes/live-wc-run{6,7,8}-2026-11-22.md`), run 8's poll log (`~/wc8/poll.log` on forever-test3, one
status line every 25 s), and the test box's own world DB (`creature`, `creature_template`: read only). Zero ForeverCore core changes.

Run 8 (r8 code, level-28 party, 150 min cap): step 82 of 302, 58 of 87 pulls, Kresh and Cobrahn dead, 77 deaths, 5 wipes, 3 pauses.
Deaths by step: 7 (5), 15 (5, wipe), 54 (13, 2 wipes), 56 (3), 69 (11, wipe), 74/75/78 (4), 82 (34, three of the five wipes, pauses 2 and 3).
Victims: Wctank 24, Wcpri 15, Wcmag 15, Wcdpsa 12, Wcrog 11.

## Facts read for this round

* Respawn: the core's `creature` rows of map 43 carry `spawntimesecs` 7200 for every pack unit of the route (Druid of the Fang
  3840, Deviate Shambler 5761, Ravager 3636, Guardian 3637, Adder 5048, Viper 5755, Python 8886, Lasher 5055, Ectoplasm 3640) and
  300 for the Snake 2914. A pack cleared at 15:25 is back at 17:25. Run 8's step-82 walk backs started at 17:20; the run 8 notes
  saw respawned early packs on the way (Deviate Ravagers and Snakes at -73 203 -90) and the poll has `walk fighting` and `the tank
  engages 3636 on arrival` during the walk back at 17:27-17:51.
* The kit (#196 class kits, `ai/playerbot/strategy/warrior/ProtectionWarriorStrategy.cpp`): the protection warrior already keeps
  Shield Block on cooldown while its target hits it (`ShieldBlockTrigger`), uses Shield Wall at low health and Last Stand at
  critical health, taunts at once when a mob peels onto a non-tank (`lose aggro`, `taunt on attacker`), and Intercepts or runs in
  to a mob on the healer (`docs/classes/warrior.md`, Protection). Nothing is missing there for the "defensive cooldowns and taunt"
  half of fix 1, so the kit is NOT changed. The gap is at the pull: nothing checks where the healer is.
* The run's existing rests (`RestLines`: start at 50 % healer mana or 80 % health, end at 80 %), the "no pull while a bot is dead"
  rule and the gather (every bot within `kGatherYd` = 20 yd of the tank) already cover mana and the dead. The gather is a distance
  test: it never reads line of sight, and the tank walks on to its pull stand after it.

## Fix 1: the pull waits for a healer that can heal the tank

Run 8: Wctank is the top victim (24 of 77) and dies first, then the pack finishes the others. Decision: the policy gets a healer
gate in `PullUnit` (`HealerOutOfReach`, `kPullHealerMaxYd` = 30 yd, inside the 40 yd heal range with room to run in): no pull goes
out (the first pull, a hop pull and a walk-back pull alike; not a body-pull retry, which is a fight already on) while the party has
a living healer other than the lead bot and none of them stands within 30 yd of the tank in its line of sight. The party forms up
on the tank (the gather phase and the gather order) and the pull waits. The hold is bounded: `kPullHealerHoldMs` = 25 s in the
gather phase on its own clock (`Run::healerHoldMs`, reset when a healer covers the tank, when the pull goes out and with the step), then the pull goes out with `healer gate spent: the healer ... ` in its note, so a healer that
never gets sight (a corner of the cave) cannot stop an unattended run. A party with no healer, a healer that is the lead, a second
healer that covers the tank, a frozen healer and a raid are not affected (the raid has its own healer rules).

The host asks for it: `ApplyConfig` sets `Facts::healerPullGate`, and `TbRunFacts.cpp` reads each healer's sight line to the lead
bot once a tick (`MemberFact::healerSight`, one LOS query a healer). The Turtle glue never sets the flag, so the Turtle run leader
does not change. Mana threshold: the existing rest rule stays the threshold (50 % start, 80 % end); a second one would fight it.
New fields: `Facts::healerPullGate`, `Facts::walkBackPullCap`, `MemberFact::healerSight`, `Run::healerHoldMs`; the decision-capture schema and its
fixture are regenerated and upgraded (`tools/gen_decision_capture.py`, `replay_decisions upgrade`).

Tests: `tools/test_run_wc_r9_policy.cpp` `TestHealerGateHoldsThePull` (a healer 40 yd off holds with the note naming it; 12 yd and
out of sight holds with "no line of sight"; 30 yd exactly and in sight pulls; no gate asked for pulls as before),
`TestHealerGateLeavesOtherPartiesAlone`, `TestHealerGateIsBounded` (the pull goes out after 25 s with "healer gate spent", also when an unrelated gather had already run 40 s,
and the clock starts afresh once a healer covers the tank),
`TestHealerGateHoldsTheWalkBackPull`; `forever/tools/test_run_wc.cpp` `TestHostAsksForTheRun9Gates` and
`TestHealerGateAtTheShamblerCamp` (the real plan, step 82).

## Fix 2: the walk back after a wipe pulls the respawned packs one at a time

Run 8, step 82: after each wipe the party was released at the entrance, rested (the rest phase is there: `rest resting` 17:26,
17:34, 17:45, 17:50), and walked toward step 81 (`walk back: approaching the point of step 81`, the last-cleared floor of run 7's
fix, which works). Walking, it was caught by the packs that had respawned on the way (7200 s after they were cleared) and died to
them in mid-walk: 34 deaths at step 82, 2+ wipes, two pauses, the loop that ended the run at the 150 minute stop.

The policy already says what to do with a pack of a step that is done: `OwnerFrom` makes a walk back list "a pack of a step already
done (back after a respawn)" as hop units, pulled one at a time by `PullUnit(walkBack)` with the gather, the rest and (now) the
healer gate in front. What kept it from working is the host's read: a hop unit had to stand within `kHopPullYd` = 15 yd of the
walk-back POINT, and the straight walk (navmesh hops from the entrance) passes few points, so the packs beside the hop were never
listed. Decision: while `Run::walkBack`, `ReadHopUnits` also lists every attackable, in-sight, reachable hostile within
`kWalkBackSeeYd` = 30 yd of the lead bot (`HopCandidates` is asked for that range too). A forward move reads what it read before.
The pulls stay one pack at a time (`PickPull` picks the nearest, the fight ends before the next pull), and the walk back's own pull
cap (`kMaxWalkBackPulls` = 8 a step, then a pause) is raised by the host to 40 (`Facts::walkBackPullCap`): a walk from the entrance
past twenty respawned packs would otherwise pause at the ninth. The policy default stays 8 for the Turtle glue.

Not done, and why: no extra regroup point is added. The release spot is the entrance and the rest and gather already happen there
(the poll lines above), and the straight walk's target is already the safe point before the last cleared step (`StraightWalkBackTarget`).

Tests: `forever/tools/test_run_wc.cpp` `TestWalkBackPullsRespawnedPacks` (a respawned Ravager pack 18 yd from the lead bot, hundreds
of yards from the walk-back point, is a hop unit in a walk back and not in a forward move; one 45 yd off is not; the current step's
own pack is not; a unit in combat is not), `tools/test_run_wc_r9_policy.cpp` `TestWalkBackPullCapIsTheHostsToRaise` (default pauses at
8, the raised cap pulls the ninth and pauses at 40, the text carries the number).

## Fix 3: the Deviate Shambler camps (5761)

The rows (plan indices; leg/seq): index 69 = leg 6 seq 11 `5761,3840,3640` r17.1 (11 deaths, a wipe); index 82 = leg 8 seq 2
`5761,3840` r14.9 (34 deaths); the other rows that list 5761 are leg 7 seq 2, 4, 5 (indices 72, 74, 75), leg 8 seq 3, 4, 5 (83, 84,
85), leg 9 seq 3, 5 and the Pythas boss row (87). Checked against `forever/data/wc-map43-spawns.tsv` (the core's `creature` dump):

| step | unit | distance from the row's point |
|---|---|---|
| 69 | Shambler 5761 | 2.0 yd |
| 69 | Druid 3840 | 5.0 yd |
| 69 | Ectoplasm 3640 (patrols) | 8.5 yd |
| 69 | Snake 2914 | 15.4 yd |
| 82 | Druid 3840 | 1.3 yd |
| 82 | Shambler 5761 | 4.6 yd |
| 82 | Snake 2914 | 5.9 yd |
| 82 | Shambler 5761 (waypoints) | 14.0 yd |

Split: not expressible. The units of a camp stand 1.3-5.9 yd apart, inside the core's assistance radius of each other, so no pull
point and no radius separates them; the only separable units are the two patrollers, which the patroller handling already waits
for. A LOS pull point needs the firing stand and stand-pull rules, which `ApplyConfig` forces off on this host ("not ported"), and
the plan format has no per-row pull point beyond the row's own point and its approach move (index 81 for the step-82 camp, 18 yd
out, already the stand the pull goes from).

The lever the format has is the kill order: `kill_first = '3840,3671,5048,5755,5761'` (run 7's list is a prefix, the Shambler is last
because it ranks behind the Druid that heals and sleeps, Anacondra and the leg-5 poisoners, and none of those shares a row with it).
Effect: the pull target of a camp stays the Druid (rank 0), then the whole party takes the Shambler before the Snake and the
patroller. `kill_first` is plan-wide, so the other rows that list 5761 get the same order, and (as for 5048 and 5755 in run 7) a
pull row that lists a kill_first entry is never deferred and a kill_first unit is never crowd-controlled (`cc_priority` is empty).

Tests: `forever/tools/test_run_wc.cpp` `TestShamblerCampsKillFirstOrder` (the rows of 69 and 82, the rank of 5761 behind 5755, ten pull
rows carry it, `PickPackPull` takes the Druid with the Shambler nearer, `PickKillFirst` goes Druid, Shambler, the Snake has no rank)
and `TestPlanShape` (the list, pinned whole); `python3 forever/tools/test_wc_plan_rows.py` regenerates the migration byte for byte.

## Review (Codex adversarial against `tbf/run/wc-r8`, one round)

* High, the first version of the hold ran on `Run::phaseMs`, the gather phase's clock: a gather that had already run 25 s let the
  pull go out at once with the healer round the corner. FIXED: the gate has its own clock, `Run::healerHoldMs`; the regression is in
  `TestHealerGateIsBounded` (a gather at 40 s still holds the pull for the 25 s; a covered tick resets the clock). The decision
  capture schema and its synthetic fixture were regenerated again (`replay_decisions synth`).
* Medium, the generated Wailing Caverns migration is edited in place. NOT A DEFECT, as in run 7 (round 13.2b): the updater applies a
  changed file again by hash, the file deletes and inserts the plan's rows in one transaction, and `forever/sql/README.md` names
  "edit the plan, export again, replace the file" as the way to change generated plan rows; a second migration that only updates
  `kill_first` would leave the generator and the shipped rows disagreeing (`test_wc_plan_rows.py` pins the file to the generator).
* High (round 2), when the run is already in the gather phase for another reason the hold sent no gather order, so the healer could
  stay round the corner until the 25 s ran out. FIXED: the hold sends its own gather order once, on entry, whatever the phase;
  `TestHealerGateIsBounded` starts in the gather phase and requires the order (mutation: no order when already gathering, caught).

## Mutations (each caught)

Run on the build VM against the tests above; each mutation was applied to a scratch copy and fails at least one test.

| mutation | caught by |
|---|---|
| healer gate off (`HealerOutOfReach` returns false) | `TestHealerGateHoldsThePull` (r9), `TestHealerGateAtTheShamblerCamp` (run_wc) |
| sight ignored (distance only) | `TestHealerGateHoldsThePull` "no line of sight" case |
| hold never spent | `TestHealerGateIsBounded` |
| hold on the gather phase's clock again | `TestHealerGateIsBounded` (slow-gather case) |
| no gather order when the run is already gathering | `TestHealerGateIsBounded` (gather-phase case) |
| range three times as wide (90 yd) | `TestHealerGateHoldsThePull` (40 yd case) |
| walk-back cap ignores the host's raised value | `TestWalkBackPullCapIsTheHostsToRaise`, and the walk-back healer test |
| walk back reads only units near the point (no lead radius) | `TestWalkBackPullsRespawnedPacks`, `TestWalkBackAsksForHostilesAroundTheLead` |
| hop range not widened in a walk back | `TestWalkBackAsksForHostilesAroundTheLead` |
| host does not ask for the gate (`ApplyConfig`) | `TestHostAsksForTheRun9Gates`, `TestHealerGateAtTheShamblerCamp` |
| `kill_first` without 5761 | `TestShamblerCampsKillFirstOrder`, `TestPlanShape`, the plan-rows pin |

Side fixes the new flag needed: the party builders of the facts tests (`test_run_wc.cpp` x2, `test_run_facts.cpp`) set
`healerSight`, as the host does for a healer standing with the tank; `RUN-S6-F-FIELDS.tsv` carries the two new Facts fields.
Suites run green on the VM: test_run_wc (773 checks), test_run_wc_r9_policy, test_wc_plan_rows (3507), test_run_facts, _s9, _c6,
_binding, test_tactics_pure, sentinel, time_sinks, leader_live, healer, wipe_count, test_dungeon_run_policy, the decision replay
and capture tests (schema upgraded: `1e7499a0f90850f7` -> `ddd57f691bc09002`).

## Gates

(filled in below at the end of the round)
