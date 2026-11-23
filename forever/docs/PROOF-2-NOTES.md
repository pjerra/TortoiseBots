# PROOF-2-NOTES: RFC step 14 is one planned pull (spec proof-2)

Source: `~/TortoiseBots-audit/notes/milestone-proof-rfc-2026-11-22.md` problem 2. Branch `fix/proof-rfc-step14` off
`tbf/run/wc-r9` merged with `tbf/dev` (the same base as proof-1: the healer gate is on run/wc-r9). Plan data on
forever-agent `data/rfc-proof-2` (commits 9ca1e5bc, the merge, and the export pins).

## What step 14 was

Global step 14 is leg 2 `trogg_cave` seq 4. The plan pulled the trogg pair (11318 x2 at -238.66 -31.56 -56.62 and
-237.99 -40.35 -56.27) from the stand -205 -34 -49.9 (33.6 yd off), then walked to -221.2 -34.7 -54.7 (17.8 yd from
the pair) and pulled the five-pack (11319, 11318, 11318, 11319, 11318 at -253.89 .. -266.61).

The distances the decision rests on (2-D, the way the plan's checks measure them):

| From | To | yd |
|---|---|---|
| the pair's Trogg -238.66 -31.56 | the five-pack's Shaman -253.89 -38.88 | 16.9 |
| the pair's stand -205 -34 -49.9 | the pair's nearest member | 33.6 |
| the pair's stand | the five-pack's Shaman -253.89 -38.88 | 49.1 |
| the pair's stand | the merged group's far Trogg -270.13 -31.88 | 65.2 |
| the merged group's stand | the merged centroid -256.6 -35.0 -59.43 | 51.6 |
| the five-pack's old stand -221.2 -34.7 -54.7 | the pair's nearest member | 17.8 |

A same-level mob aggroes at 20 yd. In all three Forever proofs of step 14 (Run A level 22, 11 deaths; test3 level 28,
8 deaths; the S6 walk level 18) the packs did not stay grey, and the healer died in the tank's own second. So the
pair's pull always woke the five-pack: two shamans' Healing Wave on a fight the run sized for two, with the plan's
`cc_always` Polymorph and `kill_first` Shaman order prepared for the planned units only (the pre-pull CC runs before
the pull). Group 2's own stand was inside the pair's aggro too, so standing there woke the pair.

## Decision

One planned pull of all seven (spec decision B), in forever-agent `dungeons/ragefire_chasm.json` leg `trogg_cave`:

- pull groups 1 and 2 merged: entries `[11318, 11318, 11319, 11318, 11318, 11319, 11318]` (the pair first, then the
  five-pack), positions unchanged, `radius` 20, `approach` the pair's stand `[-205 -34 -49.9]`;
- the five-pack's approach `-221.2 -34.7 -54.7` removed (it stood 17.8 yd from the pair: inside the aggro);
- one `notes` line on the leg saying why (the notes are for the reader; the exporter drops them, so no row changes).

Then the pre-pull Polymorph and the Shaman-first order cover both Shamans, and the fight comes to a stand no other
pack reaches: pull group 3 at -285.5 -20.9 is 31.6 yd from the merged centroid (-257 -35), group 4 is 44 yd, the
patrol group 5 46 yd: all outside radius + 3.

No code change: `TbRunFactsPure.h` on `tbf/run/wc-r9` already sets `f.healerPullGate = true` and `walkBackPullCap` for
every Forever run, so RFC gets the gate (30 yd plus line of sight, held at most 25 s) with the merge of the base.

## Rows

`forever-agent plan-export --forever --mmaps <the 0389 tiles of the test server's mmaps> --rev 9ca1e5bc --out ...
ragefire_chasm`, written over `forever/sql/world/2026_11_20_02_tortoisebots_plan_ragefire_chasm.sql` (the WC r7/r9
precedent: the generated migration is edited in place; the updater re-applies a changed file).

- `--report`: `legs=9 steps=78 pulls=50 moves=24 bosses=4 units=103 boss_rows=4` (was steps=80 pulls=51 moves=25).
- Header: `9 legs, 78 steps, 50 pull groups, 103 unit rows`; `source` names `forever-agent 9ca1e5bc`.
- Diff against the previous file: leg 2 seq 4 is the merged pull (centroid -256.600 -35.000 -59.430, radius 20,
  entries `11318,11319`), seq 5 is the five-pack's approach row gone, later leg-2 pull rows close up by one seq, the
  five-pack's five unit rows move from seq 6 to seq 4 idx 2..6, the header counts and the source sha. The file also
  gained the exporter's two-line object-row header note (a format addition since 63fc3f78; this plan has no object
  rows, so no row changed).
- `103 unit rows` unchanged: 93 pack + 4 boss + 6 patrol (the merge moved five rows, it deleted none).

The plan-check stand window moved with it: `TestRagefireTroggCavePullsHaveApproachStands` keeps `nearest >= 25`
(unchanged: no stand inside a member's 20 yd wake reach) and takes the far edge to 66 yd for this group only, because
a group of seven spans 63 yd and the pair's stand is the only one outside every member's aggro (65.2 yd to the far
Trogg). That is a widened window, recorded here and in the test's comment, not a loosened check.

Docs: the S6 walk notes (`RUN-S6-M-NOTES.md`) and `forever/sql/README.md` restate the shipped plan's counts (78 steps,
50 pulls, 54 plan-check steps, 24 moves; source 9ca1e5bc). `TB-COMMANDS.md`, `RUN-S5-M-NOTES.md` and the slice notes
are labeled captures of earlier exports (63fc3f78) and keep their numbers.

## Tests

forever-agent (`vm-test ta ~/wt-agent-p2`, full):

- `TestRagefireTroggCavePullsHaveApproachStands` (one cave stand, the pair's; 33.6 to 65.2 yd),
  `TestRagefireOnNavmesh`, `TestRagefireRouteHasNoUnplannedPack`, the other `TestRagefire*`,
  `plans_all_test.go` (`PlansAll`), `TestPlanExportForeverRagefire`, `TestPlanExportForeverReport`,
  `TestForeverRagefireGolden` (the golden file regenerated from the same `BuildForever` the test reads).

TBF:

- new `forever/tools/test_rfc_plan_rows.py` (unittest, runs under `python3 -m unittest discover -s forever/tools -p
  'test_*.py'`, where `test_g8_wiring.py` runs). Written against the old SQL first: 3 of its 4 tests failed
  (`radius 12.0 != 20.0`; `80 != 78`; `leg 2 seq 5 (a move) stands 17.8 yd from the merged pull`), then passed on the
  exported rows:
  - `test_trogg_cave_pair_and_fivepack_are_one_pull`: seq 4 is a pull, radius 20, seven unit rows at the plan's own
    positions (11318 x5, 11319 x2), the five-pack's own pull row gone;
  - `test_stand_before_it_is_the_pairs`: seq 3 is the move at -205 -34 -49.9;
  - `test_no_stand_inside_the_pairs_aggro`: no other leg-2 move row within 20 yd of any merged-pull unit; the stand
    is 33.5 yd (2-D) from the nearest and 49.1 from the first Shaman;
  - `test_plan_counts`: 78 steps, 50 pull groups, 103 unit rows, and the same numbers in the header.
- `tools/test_run_wc_r9_policy.cpp` `TestHealerGateHoldsTheRfcTroggPull`: the merged pull row, the party at the
  pair's stand, the facts built by the host's own `fx::ApplyConfig` (so the gate flag is the module's); a healer 35 yd
  behind the tank is held with `pull held: the healer Rfcpri is 35.0 yd from the tank (more than 30.0)`; the healer
  12 yd out in sight and the pull goes out.
- Suites run on this workstation (single translation units, `-I.`): `test_run_wc_r9_policy` `ok`, `test_run_wc`
  `792 checks, 0 failed`, the new python test `4 tests OK`.
- `python3 -m unittest discover -s forever/tools -p 'test_*.py'`: `Ran 594 tests ... FAILED (failures=6, errors=9,
  skipped=50)`; the same suite on the merge base (`47069e42e`, no change of this branch) gives `Ran 590 tests
  ... FAILED (failures=6, errors=9)`: the failures are there before this branch (test_w1_wiring, test_player_cmd_wiring,
  test_cmd_p1_wiring, test_run_review_wiring, test_wl0_wiring, test_wl4_wiring, test_class_quest_spells_wiring).

## Mutations

| Mutation | Result |
|---|---|
| keep group 2's approach row (a leg-2 move at -221.2 -34.7 -54.7 put back) | `test_no_stand_inside_the_pairs_aggro` FAILS: `leg 2 seq 10 (a move) stands 17.8 yd from the merged pull` (and `test_plan_counts`: `79 != 78`) |
| radius 12 on the merged row | `test_trogg_cave_pair_and_fivepack_are_one_pull` FAILS: `12.0 != 20.0` |
| `f.healerPullGate = false` in `TbRunFactsPure.h` | `TestHealerGateHoldsTheRfcTroggPull` FAILS (no gather hold, the pull goes out at once) and `test_run_wc` `TestHostAsksForTheRun9Gates` (`:1129 f.healerPullGate`) and `TestHealerGateAtTheShamblerCamp` (`:1140`, `:1153`, `:1154`, `:1158`) FAIL: 5 checks |

Each mutation was reverted; `git diff` on the mutated files is empty afterwards.

## Gates

TBF: `tbf-premerge ~/wt-proof2 7`:

```
base: tbf/dev
merge: clean (48 commits over tbf/dev)
pytest test_forever_no_direct_queries.py: PASS
COMPILE_SECONDS=13 CMAKE_RC=0 MAKE_RC=0 turtle: PASS
slice1-ON: PASS
slice1-OFF: PASS
PREMERGE: PASS
```

(the run is on the branch at 018d60f32; the commits after it change comments and this note only),

forever-agent: `vm-test ta ~/wt-agent-p2` (full) is **not green: a decision is owed** (see below). The spec's own
checks pass:

```
ok  github.com/pjerra/forever-agent/internal/dungeon       (TestRagefire*, TestRagefireOnNavmesh, -RouteHasNoUnplannedPack)
ok  github.com/pjerra/forever-agent/internal/planexport    (TestForeverRagefireGolden)
ok  github.com/pjerra/forever-agent/cmd/forever-agent      (TestPlanExportForeverRagefire, -Report) with -run "Ragefire|PlanExport|PlansAll"
```

## The full suite's count pins (the decision)

`vm-test ta` over the whole repo fails in `tools/dungeongen` on three tests that pin a per-plan count and fail on any
change, up or down (`pull_size_test.go`: "any change in a count, up or down, fails, so a drop needs the pin updated
with a one-line reason"; `overlap_test.go`, `route_aggro_test.go` the same):

| Test | ragefire_chasm before (origin/main) | after this branch |
|---|---|---|
| `TestNoMobStandsInAPullGroupsRangeUnlisted` | 0 findings | **2** findings (13 mobs: 9 x 11318 and 4 x 11319 stand within the merged group's radius 20 + 3 of its seven positions, listed 5 and 2) |
| `TestPullSizesUnderCeiling` | 27 | 26 |
| `TestNoRouteSegmentPassesALaterPack` | 164 | 153 |

All three pass on `origin/main` 507ee91c (`vm-test ta /tmp/wt-agent-base -- 'go test ./tools/dungeongen/ -run
"TestNoMobStandsInAPullGroupsRangeUnlisted|TestPullSizesUnderCeiling|TestNoRouteSegmentPassesALaterPack"'`: PASS),
so they are this change's. The overlap finding is geometry, not an error: a radius-20 ring round seven positions that
span 63 yd reaches the cave's other packs (the 48648 patrol's spawn rows, the terrace pair). The repo's own path for
a plan edit is to re-pin the three counts with a one-line reason (every entry in those maps carries one; RFC's
pull-size 27 already covers 27 over-ceiling pulls). The spec's step 2 says the opposite ("Update only pins that name
the pull-group count or the step count") and "a failing ... overlap test means STOP and ESCALATE. Do not loosen it",
so this is escalated rather than resolved here. `cmd/forever-agent` `TestFleetChildFailsBeforeWorldTheRestCarryOn`
failed in the full run only (a realmd login rejection; it passes on the same tree alone): see the report.

## Out of scope (as specced)

Live runs; the level-22 tank kit (no shield, axe off-hand, the spell judge's Sunder Armor / Revenge / Heroic Strike
finding: a separate ticket); the stand-pull and aggro-ring policy features (off on the Forever host); any other RFC
leg; `kAutoRegroupsPerStep`. Live check (the lead's): `.tb run events` for the `joiner` lines and the `healer gate`
notes at the new step 14.
