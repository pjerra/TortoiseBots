# Slice 5, R0: which fields of the run's `Facts` the Ragefire Chasm plan can read (unknown U1)

Written 2026-11-19 by the S5 R0/P5 worker (branch `run/p5`). Read only on the engine: the numbers come from `runtime/DungeonRunPolicy.h`
as it is in the tree (23,258 lines, `struct Facts` at lines 1561-2071), run on the 84 exported rows of `ragefire_chasm`
(`data/sql/world/20261113203000_world.sql`: 9 legs, 29 move, 51 pull, 4 boss; the four `ai_dungeon_boss` rows of
`20261021114931_world.sql`). Everything below is reproducible: `forever/tools/facts_rfc.sh` (build VM) and
`forever/tools/facts_rfc_report.py` (any machine), see "Rerun".

## Answer (sizes the glue of slice 6)

`Facts` has **246 fields** (222 declarations; the generated list `runtime/DecisionCaptureFields.gen.h` has 245: it leaves out the
`std::function pointLiquid`). Sorted by what an RFC run does with them:

| class | fields | meaning |
|---|---|---|
| **A** read in the simulated RFC run | **83** | an access log on a bot-led run through all 84 rows saw the policy load these bytes: the glue must fill them with real values |
| **C yes** read in code the RFC rows can reach, not reached by the simulation | **42** | boss search and pull-gate reads (`FightStep`, `BossGateTick`), the pack-cleared gate and anchor, fight forward / walk back / approach, the fight guard (`leadAttacker`, `leadHit`, `leadStay*`): fill them or answer the "do less" value |
| **C check** read in stall, stuck, island and tick-body rules | **30** | whether an RFC run reaches them depends on a walk that fails or a note line: answer a cheap "do less" default first |
| **C no** read only by a feature the plan or the config does not use | **86** | doors and leg needs, loot/open/use/gossip/ritual/hold rows (the plan has 0), firing stand and stand-pull (keys off), crowd control, raid, rescue/fetch/return teleports, rez hold, dry walk, aggro estimate notes: never read in an RFC run; value = the field's own default |
| **D** no read site in `DungeonRunPolicy.h` | **5** | `nestUnitsRead seekClockRead standOthers standPackReach holdAheadReach` |

So the glue for RFC must supply **about 125 of the 246 fields for certain (A + C yes), and answer about 30 more with defaults**
(125-155, 51-63 %), not the "about 190" of the lead plan. The biggest groups of the 83 that the run reads: party roster and lead
state 24 (`total alive dead members[] leadX/Y/Z leadAlive leadIsTank minHealthPct minManaPct maxMemberYd eating rezzer ...`), config
copies 20 (`fightForward lightRest packClearMarginYd nearWipeStop* walkBackRescue dryWalk ...`: constants from `aiplayerbot.conf`,
no world read), step read 14 (`seen units hopUnits packResidue patrollers approach* leadPathGood`), boss read 10, fight reads 6.
Per row kind (the kind of the step the tick began on): `Move` 40 fields, `Pull` 67, `Boss` 77; 37 are read on all three, 26 on pull and
boss, 13 only on a boss step (boss corpse / search / gate fields), 4 only on a pull step, 2 only on a move step.

**What the A and C-yes fields still hide:** `members[]` (`MemberFact`, 33 fields) and `units[]`/`hopUnits`/`packResidue`/... (`UnitFact`,
59 fields) are vectors: their ELEMENT fields live on the heap and were not logged separately (only that the vector was read). The next
slice must repeat this log on the element structs (the tracer is a page-protection trick on the one `Facts` object; for elements the
vectors' storage would be protected too) before it sizes the readers of `members[]` and `units[]`.

## How it was derived

1. **Field list**: `forever/tools/facts_fields.py` parses `struct Facts` (comments and multi-declarator lines handled) into 246 names with
   their declared line; checked against the generated capture list (245 + `pointLiquid`).
2. **Plan**: `forever/tools/facts_rfc_rows.py` reads the exported SQL rows of the plan into a TSV; the loader's own table API
   (`DungeonPlanTable::AddPlan/AddLeg/AddStep/AddBoss/Seal`, `ParsePlanStepRowKind`, `ParsePlanEntries`) builds it, so the plan the
   policy runs is the plan the module loads (9 legs, 84 steps, 4 bosses).
3. **Access log, dynamic** (`forever/tools/facts_trace.cpp`): the `Facts` object lives in its own pages. For every call of
   `DungeonRun::Tick` (and `ApproachStandFound`, `StallStandIn`, `SpeedRateNow`, which the glue calls after it,
   `commands/Commander.cpp` TickDungeonRun) the pages are protected; the first load of any byte of a field faults, a SIGSEGV
   handler records (field, faulting instruction, return chain, step kind, phase), runs that one instruction (trap flag) and protects
   the pages again. Nothing in the policy is changed or instrumented, so a read is a real read of those bytes. Sites are
   symbolized with addr2line to `DungeonRunPolicy.h:<line>` (first frame of the chain inside the policy header; `-O0 -g`).
4. **Scripted world** (the same file): a party of five (`Mtank` prot lead, `Mpriest`, three dps, level 18) that follows the plan:
   `move x y z` moves the lead toward the point (14 yd a tick), `pull`/`pullback` start a 4-tick fight and then the pack dies and mana
   falls, the boss rows end with the boss corpse in the facts, `rest` refills, `revive party` revives. Config copies use the code defaults of
   `PlayerbotAIConfig.cpp` with the two values the spec forces on ForeverCore (`DungeonRunWalkBackRescue = 0`, `DungeonRunDryWalk = 0`,
   return teleport 0). Seven scenarios, run on every build:

| scenario | ticks | step reached | hits | orders |
|---|---|---|---|---|
| happy | 401 | 84/84 | 658 | move 41, pullback 55 |
| rest | 515 | 84/84 | 678 | move 41, pullback 55, rest 55 |
| death | 437 | 84/84 | 678 | move 41, pullback 55, revive 18 |
| wipe | 428 | 84/84 | 761 | move 46, pullback 56, revive 1 |
| straggler | 401 | 84/84 | 658 | move 41, pullback 55 |
| patrol | 3000 | 76/84 | 661 | move 39, pullback 48 |
| evade | 402 | 84/84 | 658 | move 41, pullback 55 |

   `happy` walks the whole plan (84/84, "plan done"); `rest` drops mana and health after every fight; `death` kills a dps every third
   fight (`deadNames`, `revive party`); `wipe` wipes the party once at the first boss (`revive party`, the walk back, then on to the end); `straggler` puts a member 45 yd away before some pulls (no read beyond `happy`); `patrol` adds a patroller 25 yd from
   every pack (the run waits its 60 s per step, so it ends before the last step); `evade` makes units evade and stealth for three ticks
   in forty.
5. **Coverage and static scan** (what the simulation did not reach): a second build with `--coverage` gives which lines of
   `DungeonRunPolicy.h` ran in any scenario; the static scan lists every `X.field` of a `Facts` parameter in the header, function by
   function, reads and writes told apart. A field with no traced read but a read site in a function that ran (C1) or never ran (C2) is
   then labelled by the function that reads it (rules in `facts_rfc_report.py`, table `FEATURES`) as `yes`, `check` or `no`.

## What could make it incomplete

* **The simulation is a model.** Branches that need facts it never sets (a failed path read, a bot stuck on a ledge, a lead in water, a
  boss that is not at its spot, a fight that does not end) are in C, not in A. A real first run on ForeverCore will read some `check`
  fields; the glue should answer every C field with its `Facts()` default (the T glue starts every tick from `Facts()`, ReadRunFacts
  Commander.cpp:11693) and log the first read of each one it does not fill (the same page-protection trick as a debug switch would do it).
* **Element fields** (see above) and **`Run`** (the policy's own state, not facts) are not covered.
* **Short-circuit operands**: a field behind `a && f.x` is counted as a read only when the left side lets it be evaluated; 18 of the
  C1 fields have a read on a line that ran and were skipped this way.
* **`-O0` return chains** pick the first policy frame; a read inside a lambda reports the lambda's line.
* **Only `DungeonRunPolicy.h` reads `Facts` in `Tick`.** Other readers of the same struct (`PlayerLedPolicy.h`, the decision-capture
  recorder `runtime/DecisionCapture*.h`, `RunItemNeedFacts.h`) are not part of a bot-led RFC tick; the recorder reads all 246 fields when
  `AiPlayerbot.DecisionCapture.Enabled` = 1 (off by default).
* **One copy per tick.** `Tick` copies `Facts` into `view`/`quiet`/`HoldView` in three cases (a member off the plan map, unattackable or nest-only
  combat, a hold); reads on those copies are not logged. `happy` takes none of them; a run that does reads the same fields from the copy.

## Rerun

```
rsync -a --delete --exclude .git --exclude docs --exclude CHANGELOG.md ~/wt-run-p5/ yulon-ubuntu2:forever-build/p5-work/wt/
ssh yulon-ubuntu2 'cd ~/forever-build/p5-work && wt/forever/tools/facts_rfc.sh wt ragefire_chasm res 3000'   # about 2 min
rsync -a yulon-ubuntu2:forever-build/p5-work/res/ /tmp/res/ && python3 forever/tools/facts_rfc_report.py /tmp/res --md tables.md --tsv fields.tsv
```
Another plan: change the plan name (the SQL files are `facts_rfc_rows.py`'s defaults or its arguments).

# The generated tables (`facts_rfc_report.py`)

Counts: 246 fields. A read in the simulated RFC run: 83. C1 read in a function the run executed, in a branch or short-circuit operand it did not take (18 of them on a line that ran): 81. C2 read only in functions the run never called: 77. D no read site in DungeonRunPolicy.h: 5.

Verdict of the 158 not-traced fields by the feature of the function that reads them: yes (RFC rows can reach it) 42, check (stall and tick-body rules, depends on the run) 30, no (a feature the RFC plan or the config does not use) 86.

| group | fields | A run | C1 gated branch | C2 uncalled function | D never read |
|---|---|---|---|---|---|
| players in the group (player-led / mixed runs) | 3 | 1 | 0 | 2 | 0 |
| doors, objects, loot, items, gossip, tasks | 25 | 0 | 13 | 12 | 0 |
| config copy (aiplayerbot.conf key read once per tick) | 31 | 20 | 3 | 8 | 0 |
| party roster and lead state | 30 | 24 | 3 | 3 | 0 |
| raid | 2 | 2 | 0 | 0 | 0 |
| fight reads (engaged units, attackers, lead hit) | 14 | 6 | 6 | 1 | 1 |
| other | 33 | 3 | 10 | 20 | 0 |
| step read: units, pack, approach, path, floor | 30 | 14 | 8 | 8 | 0 |
| crowd control (cc) | 1 | 1 | 0 | 0 | 0 |
| rescue, fetch, return, rejoin, summon | 10 | 1 | 6 | 3 | 0 |
| rez hold, resurrection | 3 | 1 | 2 | 0 | 0 |
| firing stand, shot, stand-pull | 25 | 0 | 6 | 16 | 3 |
| boss read | 36 | 10 | 23 | 2 | 1 |
| hold, ritual, event | 3 | 0 | 1 | 2 | 0 |

### A: read in the simulated RFC run (field, declared line, step kinds, scenarios, first read sites)

| field | decl | kinds (reads) | scenarios | read sites (hits) |
|---|---|---|---|---|
| bossDeadNearSpot | 1830 | Boss 14 | death,evade,happy,patrol,rest,straggler,wipe | runtime/DungeonRunPolicy.h:15426 (271) |
| bossDeadNearLead | 1831 | Boss 7 | death,evade,happy,patrol,rest,straggler,wipe | runtime/DungeonRunPolicy.h:15426 (81) |
| bossCorpseGuid | 1835 | Boss 14 | death,evade,happy,patrol,rest,straggler,wipe | runtime/DungeonRunPolicy.h:15843 (27); runtime/DungeonRunPolicy.h:15844 (27) |
| bossCorpseX | 1836 | Boss 7 | death,evade,happy,patrol,rest,straggler,wipe | runtime/DungeonRunPolicy.h:15844 (27) |
| bossCorpseY | 1836 | Boss 7 | death,evade,happy,patrol,rest,straggler,wipe | runtime/DungeonRunPolicy.h:15844 (27) |
| bossCorpseZ | 1836 | Boss 7 | death,evade,happy,patrol,rest,straggler,wipe | runtime/DungeonRunPolicy.h:15844 (27) |
| bossOwnDead | 1841 | Boss 14 | death,evade,happy,patrol,rest,straggler,wipe | runtime/DungeonRunPolicy.h:15426 (150); runtime/DungeonRunPolicy.h:15847 (27) |
| bossGateStacks | 1878 | Boss 7, Pull 7 | death,evade,happy,patrol,rest,straggler,wipe | runtime/DungeonRunPolicy.h:11439 (379) |
| bossGateMax | 1880 | Boss 7, Pull 7 | death,evade,happy,patrol,rest,straggler,wipe | runtime/DungeonRunPolicy.h:11439 (379) |
| seekSiphonCycle | 1890 | Boss 7, Pull 7 | death,evade,happy,patrol,rest,straggler,wipe | runtime/DungeonRunPolicy.h:22627 (1137) |
| linkedDeath | 1598 | Boss 7, Pull 7 | death,evade,happy,patrol,rest,straggler,wipe | runtime/DungeonRunPolicy.h:18018 (1137) |
| nearWipeStopCount | 1601 | Boss 2 | wipe | runtime/DungeonRunPolicy.h:21785 (1); runtime/DungeonRunPolicy.h:21790 (1) |
| nearWipeStopWindowMs | 1602 | Boss 2 | wipe | runtime/DungeonRunPolicy.h:21785 (1); runtime/DungeonRunPolicy.h:21788 (1) |
| walkBackRescue | 1695 | Boss 7, Move 7, Pull 7 | death,evade,happy,patrol,rest,straggler,wipe | runtime/DungeonRunPolicy.h:20520 (4835) |
| teleportReturnYd | 1702 | Boss 7, Move 7, Pull 7 | death,evade,happy,patrol,rest,straggler,wipe | runtime/DungeonRunPolicy.h:4726 (5584) |
| reviveHoldYd | 1705 | Boss 7, Move 7, Pull 7 | death,evade,happy,patrol,rest,straggler,wipe | runtime/DungeonRunPolicy.h:21941 (5584) |
| fightForward | 1718 | Move 7 | death,evade,happy,patrol,rest,straggler,wipe | runtime/DungeonRunPolicy.h:21620 (91) |
| lightRest | 1721 | Boss 14, Move 14, Pull 14 | death,evade,happy,patrol,rest,straggler,wipe | runtime/DungeonRunPolicy.h:22880 (4408); runtime/DungeonRunPolicy.h:22968 (4294) |
| packClearMarginYd | 1805 | Pull 46 | death,evade,happy,patrol,rest,straggler,wipe | runtime/DungeonRunPolicy.h:14460 (9079); runtime/DungeonRunPolicy.h:14934 (8055); runtime/DungeonRunPolicy.h:14523 (2991) |
| joinerCeiling | 1969 | Boss 21, Move 7, Pull 21 | death,evade,happy,patrol,rest,straggler,wipe | runtime/DungeonRunPolicy.h:8306 (4408); runtime/DungeonRunPolicy.h:15694 (379); runtime/DungeonRunPolicy.h:11738 (379) |
| aggroWalkRing | 1973 | Boss 14, Move 7, Pull 14 | death,evade,happy,patrol,rest,straggler,wipe | runtime/DungeonRunPolicy.h:15959 (651); runtime/DungeonRunPolicy.h:15672 (379); runtime/DungeonRunPolicy.h:11736 (379) |
| pullStand | 1984 | Boss 14, Pull 21 | death,evade,happy,patrol,rest,straggler,wipe | runtime/DungeonRunPolicy.h:10392 (3343); runtime/DungeonRunPolicy.h:11627 (379); runtime/DungeonRunPolicy.h:11719 (379) |
| standClearance | 1988 | Boss 7, Pull 7 | death,evade,happy,patrol,rest,straggler,wipe | runtime/DungeonRunPolicy.h:11624 (379) |
| standDiscipline | 1993 | Boss 14, Pull 14 | death,evade,happy,patrol,rest,straggler,wipe | runtime/DungeonRunPolicy.h:11626 (379); runtime/DungeonRunPolicy.h:11655 (379) |
| walkBackAfterWonFight | 1999 | Move 7 | death,evade,happy,patrol,rest,straggler,wipe | runtime/DungeonRunPolicy.h:21620 (91) |
| retreatAt2Dead | 2002 | Boss 7, Pull 7 | death,evade,happy,patrol,rest,straggler,wipe | runtime/DungeonRunPolicy.h:21702 (1137) |
| standPullPreCc | 2037 | Boss 14, Move 7, Pull 7 | death,evade,happy,patrol,rest,straggler,wipe | runtime/DungeonRunPolicy.h:16095 (630); runtime/DungeonRunPolicy.h:11586 (379); runtime/DungeonRunPolicy.h:15619 (28) |
| autoCc | 2045 | Boss 49, Pull 56 | death,evade,happy,patrol,rest,straggler,wipe | runtime/DungeonRunPolicy.h:9945 (3722); runtime/DungeonRunPolicy.h:10250 (1137); runtime/DungeonRunPolicy.h:9964 (1137) |
| totemFocus | 2052 | Boss 7, Pull 7 | death,evade,happy,patrol,rest,straggler,wipe | runtime/DungeonRunPolicy.h:22481 (1137) |
| packFocusSparesTacticUnits | 2056 | Boss 7, Pull 7 | death,evade,happy,patrol,rest,straggler,wipe | runtime/DungeonRunPolicy.h:10949 (1137) |
| raidIcons | 1667 | Boss 7, Pull 7 | death,evade,happy,patrol,rest,straggler,wipe | runtime/DungeonRunPolicy.h:9820 (3722) |
| engagedUnits | 1617 | Boss 14, Pull 14 | death,evade,happy,patrol,rest,straggler,wipe | runtime/DungeonRunPolicy.h:11702 (3446); runtime/DungeonRunPolicy.h:11702 (3446) |
| engagedRead | 1618 | Boss 42, Pull 56 | death,evade,happy,patrol,rest,straggler,wipe | runtime/DungeonRunPolicy.h:20344 (1137); runtime/DungeonRunPolicy.h:22367 (1137); runtime/DungeonRunPolicy.h:22402 (1137) |
| ownFightRead | 1636 | Boss 7, Move 7, Pull 7 | death,evade,happy,patrol,rest,straggler,wipe | runtime/DungeonRunPolicy.h:22363 (5545) |
| killFirst | 1741 | Boss 14, Pull 14 | death,evade,happy,patrol,rest,straggler,wipe | runtime/DungeonRunPolicy.h:10729 (1137); runtime/DungeonRunPolicy.h:10729 (1137) |
| leadHasAttacker | 1948 | Boss 14, Pull 14 | death,evade,happy,patrol,rest,straggler,wipe | runtime/DungeonRunPolicy.h:19362 (1137); runtime/DungeonRunPolicy.h:19060 (1137) |
| partyAttackers | 1959 | Boss 14, Pull 14 | death,evade,happy,patrol,rest,straggler,wipe | runtime/DungeonRunPolicy.h:19360 (3411); runtime/DungeonRunPolicy.h:19360 (3411) |
| minHealerManaPct | 1746 | Boss 22, Move 14, Pull 22 | death,evade,happy,patrol,rest,straggler,wipe | runtime/DungeonRunPolicy.h:8265 (3661); runtime/DungeonRunPolicy.h:8260 (905) |
| leadIsland | 1776 | Boss 9, Move 14, Pull 7 | death,evade,happy,patrol,rest,straggler,wipe | runtime/DungeonRunPolicy.h:7400 (275) |
| leadReaching | 1964 | Boss 7, Pull 7 | death,evade,happy,patrol,rest,straggler,wipe | runtime/DungeonRunPolicy.h:19646 (1137) |
| total | 1594 | Boss 35, Move 35, Pull 35 | death,evade,happy,patrol,rest,straggler,wipe | runtime/DungeonRunPolicy.h:21245 (28070); runtime/DungeonRunPolicy.h:21273 (5584); runtime/DungeonRunPolicy.h:22010 (5584) |
| alive | 1594 | Boss 16, Move 14, Pull 15 | death,evade,happy,patrol,rest,straggler,wipe | runtime/DungeonRunPolicy.h:21273 (5584); runtime/DungeonRunPolicy.h:22010 (5584); runtime/DungeonRunPolicy.h:19730 (discriminator 7) (134) |
| dead | 1594 | Boss 53, Move 35, Pull 52 | death,evade,happy,patrol,rest,straggler,wipe | runtime/DungeonRunPolicy.h:21235 (16752); runtime/DungeonRunPolicy.h:20122 (4408); runtime/DungeonRunPolicy.h:21125 (1137) |
| leadAlive | 1603 | Boss 131, Move 77, Pull 131 | death,evade,happy,patrol,rest,straggler,wipe | runtime/DungeonRunPolicy.h:20847 (5584); runtime/DungeonRunPolicy.h:19985 (5584); runtime/DungeonRunPolicy.h:19987 (5584) |
| anyInCombat | 1608 | Boss 108, Move 98, Pull 128 | death,evade,happy,patrol,rest,straggler,wipe | runtime/DungeonRunPolicy.h:20438 (5584); runtime/DungeonRunPolicy.h:20324 (5584); runtime/DungeonRunPolicy.h:21435 (5584) |
| leadInCombat | 1609 | Boss 9, Pull 9 | death,evade,happy,patrol,rest,straggler,wipe | runtime/DungeonRunPolicy.h:20441 (1137); runtime/DungeonRunPolicy.h:19780 (765) |
| staleCombat | 1613 | Boss 14, Pull 14 | death,evade,happy,patrol,rest,straggler,wipe | runtime/DungeonRunPolicy.h:21974 (1137); runtime/DungeonRunPolicy.h:22472 (1137) |
| leadX | 1637 | Boss 139, Move 133, Pull 120 | death,evade,happy,patrol,rest,straggler,wipe | runtime/DungeonRunPolicy.h:20847 (5582); runtime/DungeonRunPolicy.h:20001 (5582); runtime/DungeonRunPolicy.h:7365 (5582) |
| leadY | 1637 | Boss 139, Move 133, Pull 120 | death,evade,happy,patrol,rest,straggler,wipe | runtime/DungeonRunPolicy.h:20847 (5582); runtime/DungeonRunPolicy.h:20002 (5582); runtime/DungeonRunPolicy.h:7366 (5582) |
| leadZ | 1637 | Boss 181, Move 133, Pull 162 | death,evade,happy,patrol,rest,straggler,wipe | runtime/DungeonRunPolicy.h:20847 (5582); runtime/DungeonRunPolicy.h:20003 (5582); runtime/DungeonRunPolicy.h:7367 (5582) |
| leadOffFloor | 1641 | Boss 7, Move 7, Pull 7 | death,evade,happy,patrol,rest,straggler,wipe | runtime/DungeonRunPolicy.h:21434 (5584) |
| leadResting | 1646 | Boss 7, Move 7, Pull 7 | death,evade,happy,patrol,rest,straggler,wipe | runtime/DungeonRunPolicy.h:22953 (3549) |
| maxMemberYd | 1664 | Boss 8, Pull 8 | death,evade,happy,patrol,rest,straggler,wipe | runtime/DungeonRunPolicy.h:21268 (398) |
| members | 1669 | Boss 14, Move 14, Pull 14 | death,evade,happy,patrol,rest,straggler,wipe | runtime/DungeonRunPolicy.h:20899 (65682); runtime/DungeonRunPolicy.h:20899 (65682); runtime/DungeonRunPolicy.h:4129 (2633) |
| deadNames | 1677 | Boss 14, Move 14, Pull 14 | death,evade,happy,patrol,rest,straggler,wipe | runtime/DungeonRunPolicy.h:4176 (33577); runtime/DungeonRunPolicy.h:4176 (33577) |
| minHealthPct | 1745 | Boss 23, Move 14, Pull 22 | death,evade,happy,patrol,rest,straggler,wipe | runtime/DungeonRunPolicy.h:8265 (3822); runtime/DungeonRunPolicy.h:8260 (960) |
| minManaPct | 1747 | Boss 22, Move 14, Pull 22 | death,evade,happy,patrol,rest,straggler,wipe | runtime/DungeonRunPolicy.h:8265 (3661); runtime/DungeonRunPolicy.h:8260 (905) |
| eating | 1748 | Boss 7, Move 7, Pull 7 | death,evade,happy,patrol,rest,straggler,wipe | runtime/DungeonRunPolicy.h:22954 (3606) |
| rezzer | 1749 | Boss 1, Pull 1 | death,wipe | runtime/DungeonRunPolicy.h:22181 (19) |
| team | 1758 | Boss 9, Move 7, Pull 9 | death,evade,happy,patrol,rest,straggler,wipe | runtime/DungeonRunPolicy.h:14936 (8055); runtime/DungeonRunPolicy.h:15539 (3623); runtime/DungeonRunPolicy.h:21566 (35) |
| carrier | 1906 | Boss 77, Move 28, Pull 79 | death,evade,happy,patrol,rest,straggler,wipe | runtime/DungeonRunPolicy.h:20741 (105164); runtime/DungeonRunPolicy.h:2086 (4548); runtime/DungeonRunPolicy.h:18276 (4548) |
| leadIsTank | 1938 | Boss 14, Pull 14 | death,evade,happy,patrol,rest,straggler,wipe | runtime/DungeonRunPolicy.h:18276 (1137); runtime/DungeonRunPolicy.h:18881 (1137) |
| leadStaying | 1939 | Boss 14, Pull 14 | death,evade,happy,patrol,rest,straggler,wipe | runtime/DungeonRunPolicy.h:19027 (1137); runtime/DungeonRunPolicy.h:18882 (1137) |
| leadShotYd | 2007 | Pull 7 | death,evade,happy,patrol,rest,straggler,wipe | runtime/DungeonRunPolicy.h:15145 (3343) |
| players | 1564 | Boss 28, Move 28, Pull 28 | death,evade,happy,patrol,rest,straggler,wipe | runtime/DungeonRunPolicy.h:6999 (84408); runtime/DungeonRunPolicy.h:21245 (56140) |
| raid | 1595 | Boss 172, Move 63, Pull 164 | death,evade,happy,patrol,rest,straggler,wipe | runtime/DungeonRunPolicy.h:21245 (28070); runtime/DungeonRunPolicy.h:21250 (28062); runtime/DungeonRunPolicy.h:20890 (5584) |
| raidIconsRead | 1668 | Boss 21, Move 7, Pull 21 | death,evade,happy,patrol,rest,straggler,wipe | runtime/DungeonRunPolicy.h:9226 (5584); runtime/DungeonRunPolicy.h:10797 (1137); runtime/DungeonRunPolicy.h:10829 (1137) |
| rescueRead | 1712 | Boss 1 | wipe | runtime/DungeonRunPolicy.h:13074 (3) |
| rezHold | 1754 | Boss 2, Pull 2 | death,wipe | runtime/DungeonRunPolicy.h:22112 (19); runtime/DungeonRunPolicy.h:22190 (19) |
| anchorPathRead | 1681 | Boss 7, Move 7, Pull 7 | death,evade,happy,patrol,rest,straggler,wipe | runtime/DungeonRunPolicy.h:21936 (5584) |
| directPathCrossesPack | 1686 | Boss 1 | wipe | runtime/DungeonRunPolicy.h:14104 (3) |
| approachIncomplete | 1692 | Boss 1 | wipe | runtime/DungeonRunPolicy.h:14092 (3) |
| seen | 1759 | Boss 22, Move 7, Pull 28 | death,evade,happy,patrol,rest,straggler,wipe | runtime/DungeonRunPolicy.h:15431 (6775); runtime/DungeonRunPolicy.h:15579 (3623); runtime/DungeonRunPolicy.h:8306 (3220) |
| approachKnown | 1763 | Boss 17, Move 14, Pull 14 | death,evade,happy,patrol,rest,straggler,wipe | runtime/DungeonRunPolicy.h:12162 (89); runtime/DungeonRunPolicy.h:12173 (89); runtime/DungeonRunPolicy.h:14092 (3) |
| approachX | 1771 | Boss 23, Move 21, Pull 21 | death,evade,happy,patrol,rest,straggler,wipe | runtime/DungeonRunPolicy.h:12174 (89); runtime/DungeonRunPolicy.h:12221 (89); runtime/DungeonRunPolicy.h:12224 (89) |
| approachY | 1771 | Boss 23, Move 21, Pull 21 | death,evade,happy,patrol,rest,straggler,wipe | runtime/DungeonRunPolicy.h:12174 (89); runtime/DungeonRunPolicy.h:12222 (89); runtime/DungeonRunPolicy.h:12224 (89) |
| approachZ | 1771 | Boss 23, Move 21, Pull 21 | death,evade,happy,patrol,rest,straggler,wipe | runtime/DungeonRunPolicy.h:12174 (89); runtime/DungeonRunPolicy.h:12223 (89); runtime/DungeonRunPolicy.h:12224 (89) |
| leadPathGood | 1777 | Boss 7, Move 7, Pull 7 | death,evade,happy,patrol,rest,straggler,wipe | runtime/DungeonRunPolicy.h:7353 (5582) |
| units | 1782 | Boss 14, Pull 14 | death,evade,happy,patrol,rest,straggler,wipe | runtime/DungeonRunPolicy.h:9110 (36208); runtime/DungeonRunPolicy.h:9110 (36208) |
| patrollers | 1790 | Pull 14 | death,evade,happy,patrol,rest,straggler,wipe | runtime/DungeonRunPolicy.h:15446 (9079); runtime/DungeonRunPolicy.h:15446 (9079) |
| packResidue | 1802 | Pull 14 | death,evade,happy,patrol,rest,straggler,wipe | runtime/DungeonRunPolicy.h:14463 (9429); runtime/DungeonRunPolicy.h:14463 (9429) |
| hopUnits | 1826 | Boss 2, Move 14 | death,evade,happy,patrol,rest,straggler,wipe | runtime/DungeonRunPolicy.h:9083 (670); runtime/DungeonRunPolicy.h:9083 (670) |
| pointLiquid | 2070 | Boss 1 | wipe | runtime/DungeonRunPolicy.h:2078 (3) |

### C1: read in an executed function, branch not taken in the simulation

| field | decl | read sites (count; first) | in functions | RFC verdict: feature |
|---|---|---|---|---|
| bossOwnDeadEntry | 1842 | 3; runtime/DungeonRunPolicy.h:15847 | FightStep | yes: boss rows (4 in the plan) |
| bossSearchRead | 1848 | 5; runtime/DungeonRunPolicy.h:15894 | FightStep | yes: boss rows (4 in the plan) |
| bossSearchAlive | 1849 | 1; runtime/DungeonRunPolicy.h:15904 | FightStep | yes: boss rows (4 in the plan) |
| bossSearchAliveEntry | 1852 | 4; runtime/DungeonRunPolicy.h:11439 | BossPullGated, FightStep | yes: boss rows (4 in the plan) |
| bossAliveX | 1853 | 3; runtime/DungeonRunPolicy.h:15910 | FightStep | yes: boss rows (4 in the plan) |
| bossAliveY | 1853 | 3; runtime/DungeonRunPolicy.h:15910 | FightStep | yes: boss rows (4 in the plan) |
| bossAliveZ | 1853 | 4; runtime/DungeonRunPolicy.h:15911 | FightStep | yes: boss rows (4 in the plan) |
| bossSearchDead | 1854 | 1; runtime/DungeonRunPolicy.h:15924 | FightStep | yes: boss rows (4 in the plan) |
| bossSearchUnloaded | 1856 | 1; runtime/DungeonRunPolicy.h:15945 | FightStep | yes: boss rows (4 in the plan) |
| bossSearchOwnDead | 1860 | 1; runtime/DungeonRunPolicy.h:15894 | FightStep | yes: boss rows (4 in the plan) |
| bossSearchNoSpawn | 1862 | 1; runtime/DungeonRunPolicy.h:15944 | FightStep | yes: boss rows (4 in the plan) |
| bossSearchEventOff | 1868 | 1; runtime/DungeonRunPolicy.h:15937 | FightStep | yes: boss rows (4 in the plan) |
| bossSearchEventId | 1869 | 1; runtime/DungeonRunPolicy.h:15939 | FightStep | yes: boss rows (4 in the plan) |
| bossSearchEventName | 1870 | 1; runtime/DungeonRunPolicy.h:15939 | FightStep | yes: boss rows (4 in the plan) |
| bossSearchCorpseGuid | 1871 | 2; runtime/DungeonRunPolicy.h:15896 | FightStep | yes: boss rows (4 in the plan) |
| bossSearchCorpseX | 1872 | 6; runtime/DungeonRunPolicy.h:15896 | FightStep | yes: boss rows (4 in the plan) |
| bossSearchCorpseY | 1872 | 6; runtime/DungeonRunPolicy.h:15896 | FightStep | yes: boss rows (4 in the plan) |
| bossSearchCorpseZ | 1872 | 6; runtime/DungeonRunPolicy.h:15897 | FightStep | yes: boss rows (4 in the plan) |
| bossGateHoldS | 1881 | 1; runtime/DungeonRunPolicy.h:11461 | BossGateTick | yes: boss rows (4 in the plan) |
| bossHpPct | 1885 | 2; runtime/DungeonRunPolicy.h:22616 | TickBody | check: tick body (run line, rest, hp notes) |
| bossBerserk | 1886 | 1; runtime/DungeonRunPolicy.h:22621 | TickBody | check: tick body (run line, rest, hp notes) |
| seekSiphonDosed | 1891 | 1; runtime/DungeonRunPolicy.h:22630 | TickBody | check: tick body (run line, rest, hp notes) |
| seekSiphonUndosed | 1892 | 1; runtime/DungeonRunPolicy.h:22630 | TickBody | check: tick body (run line, rest, hp notes) |
| botKeysForPlayers | 1592 | 2; runtime/DungeonRunPolicy.h:6965 | DoorKeyOpen, DoorNeedHold | no: doors and leg needs (the RFC plan has 0 such rows) |
| bossTacticNoCc | 1744 | 2; runtime/DungeonRunPolicy.h:9912 | RunAutoCcFightTick, RunAutoCcPickOrder | no: crowd control (a warrior/priest party has none; cc keys off) |
| packClearRepulls | 1806 | 4; runtime/DungeonRunPolicy.h:14530 | PackResidueTick | yes: pack-cleared gate / anchor (margin default 10) |
| legNeedRead | 1571 | 2; runtime/DungeonRunPolicy.h:6780 | DoorNeedHold, LegNeedGate | no: doors and leg needs (the RFC plan has 0 such rows) |
| legNeedHolders | 1576 | 3; runtime/DungeonRunPolicy.h:6792 | DoorNeedHold, DoorQualified, LegNeedGate | no: doors and leg needs (the RFC plan has 0 such rows) |
| doorNeedRead | 1581 | 4; runtime/DungeonRunPolicy.h:6886 | DoorKeyOpen, DoorNear, DoorNeedHold, DoorOpenWhilePaused | no: doors and leg needs (the RFC plan has 0 such rows) |
| doorNeedState | 1582 | 3; runtime/DungeonRunPolicy.h:6969 | DoorKeyOpen, DoorNeedHold, DoorOpenWhilePaused | no: doors and leg needs (the RFC plan has 0 such rows) |
| doorNeedX | 1583 | 3; runtime/DungeonRunPolicy.h:6888 | DoorKeyOpen, DoorNear | no: doors and leg needs (the RFC plan has 0 such rows) |
| doorNeedY | 1583 | 3; runtime/DungeonRunPolicy.h:6888 | DoorKeyOpen, DoorNear | no: doors and leg needs (the RFC plan has 0 such rows) |
| doorNeedZ | 1583 | 3; runtime/DungeonRunPolicy.h:6888 | DoorKeyOpen, DoorNear | no: doors and leg needs (the RFC plan has 0 such rows) |
| doorNeedCarrier | 1586 | 6; runtime/DungeonRunPolicy.h:6965 | DoorKeyOpen, DoorNeedHold | no: doors and leg needs (the RFC plan has 0 such rows) |
| doors | 1607 | 1; runtime/DungeonRunPolicy.h:4359 | LeadInOtherPart | no: doors and leg needs (the RFC plan has 0 such rows) |
| objectState | 1893 | 12; runtime/DungeonRunPolicy.h:7562 | HoldDoneNote, HoldObjectRead, HoldStep, TickBody ... | check: row kinds the RFC plan does not have (loot, open, use, gossip, ritual, hold, wait_object: 0 rows); tick body (run line, rest, hp notes) |
| objectReadable | 1898 | 2; runtime/DungeonRunPolicy.h:7568 | UpdateObjectSighting, WaitObjectGoneIsDone | no: row kinds the RFC plan does not have (loot, open, use, gossip, ritual, hold, wait_object: 0 rows) |
| taskReply | 1931 | 14; runtime/DungeonRunPolicy.h:16594 | GossipStep, LootFfaStep, OpenProofAnswer, OpenProofStep ... | check: row kinds the RFC plan does not have (loot, open, use, gossip, ritual, hold, wait_object: 0 rows); tick body (run line, rest, hp notes) |
| taskText | 1932 | 21; runtime/DungeonRunPolicy.h:16670 | GossipStep, OpenProofAnswer, OpenProofStep, RitualStep ... | check: row kinds the RFC plan does not have (loot, open, use, gossip, ritual, hold, wait_object: 0 rows); tick body (run line, rest, hp notes) |
| ownFightUnits | 1635 | 1; runtime/DungeonRunPolicy.h:22364 | TickBody | check: tick body (run line, rest, hp notes) |
| leadHit | 1942 | 2; runtime/DungeonRunPolicy.h:18807 | GuardPick, HoldEngageTarget | yes: fight guard (the tank engages what hits the lead); row kinds the RFC plan does not have (loot, open, use, gossip, ritual, hold, wait_object: |
| leadAttacker | 1947 | 12; runtime/DungeonRunPolicy.h:18358 | CheckGuard, EngageTarget, ForgetUnitsThatCame, HealerGuardMainTankMayTake ... | yes: fight guard (the tank engages what hits the lead); unclassified |
| leadAttackers | 1952 | 1; runtime/DungeonRunPolicy.h:10096 | RunAutoCcFightTick | no: crowd control (a warrior/priest party has none; cc keys off) |
| ccHeld | 2064 | 6; runtime/DungeonRunPolicy.h:10262 | RunCcBreakTick, TickBody | check: crowd control (a warrior/priest party has none; cc keys off); tick body (run line, rest, hp notes) |
| ccHeldRead | 2065 | 2; runtime/DungeonRunPolicy.h:10250 | RunCcBreakTick, TickBody | check: crowd control (a warrior/priest party has none; cc keys off); tick body (run line, rest, hp notes) |
| standPullOwnAlive | 1799 | 1; runtime/DungeonRunPolicy.h:15093 | StandPullAwayWait | no: firing stand / stand-pull (config keys off; 0 authored stand-pull rows) |
| holdAheadRead | 2039 | 2; runtime/DungeonRunPolicy.h:16104 | MoveStep | no: firing stand / stand-pull (config keys off; 0 authored stand-pull rows) |
| holdAheadKnown | 2040 | 3; runtime/DungeonRunPolicy.h:16104 | MoveStep | no: firing stand / stand-pull (config keys off; 0 authored stand-pull rows) |
| holdAheadX | 2041 | 2; runtime/DungeonRunPolicy.h:16108 | MoveStep | no: firing stand / stand-pull (config keys off; 0 authored stand-pull rows) |
| holdAheadY | 2041 | 2; runtime/DungeonRunPolicy.h:16108 | MoveStep | no: firing stand / stand-pull (config keys off; 0 authored stand-pull rows) |
| holdAheadZ | 2041 | 2; runtime/DungeonRunPolicy.h:16108 | MoveStep | no: firing stand / stand-pull (config keys off; 0 authored stand-pull rows) |
| eventData | 1902 | 15; runtime/DungeonRunPolicy.h:16916 | HoldDoneNote, HoldStep, RearmEventOnWipe, RitualStep ... | check: row kinds the RFC plan does not have (loot, open, use, gossip, ritual, hold, wait_object: 0 rows); tick body (run line, rest, hp notes) |
| leadMoving | 1643 | 5; runtime/DungeonRunPolicy.h:7479 | EscapeWait, ExactWalkTo, HoldForShot, RestWalkResendDue | check: firing stand / stand-pull (config keys off; 0 authored stand-pull rows); stall / stuck / island rules (a walk that fails) |
| leadCasting | 1647 | 1; runtime/DungeonRunPolicy.h:8497 | RestWalkResendDue | check: stall / stuck / island rules (a walk that fails) |
| leadMoveFailed | 1653 | 3; runtime/DungeonRunPolicy.h:8382 | RestWalkResendDue, StallFallback, StuckNote | check: stall / stuck / island rules (a walk that fails) |
| goodSpotCut | 1781 | 1; runtime/DungeonRunPolicy.h:7419 | IslandConfirmed | check: stall / stuck / island rules (a walk that fails) |
| leadStayKnown | 1940 | 18; runtime/DungeonRunPolicy.h:18757 | CheckGuard, RaidAssist, RaidBossLeadJoin, RaidSwarmJoin ... | yes: fight guard (the tank engages what hits the lead); raid |
| leadStayX | 1941 | 6; runtime/DungeonRunPolicy.h:18757 | CheckGuard, RaidAssist, RaidBossLeadJoin, RaidSwarmJoin ... | yes: fight guard (the tank engages what hits the lead); raid |
| leadStayY | 1941 | 6; runtime/DungeonRunPolicy.h:18758 | CheckGuard, RaidAssist, RaidBossLeadJoin, RaidSwarmJoin ... | yes: fight guard (the tank engages what hits the lead); raid |
| leadStayZ | 1941 | 6; runtime/DungeonRunPolicy.h:18759 | CheckGuard, RaidAssist, RaidBossLeadJoin, RaidSwarmJoin ... | yes: fight guard (the tank engages what hits the lead); raid |
| reachAggro | 1965 | 3; runtime/DungeonRunPolicy.h:19646 | 10.0f; | check: unclassified |
| aheadStep | 1978 | 4; runtime/DungeonRunPolicy.h:3466 | AheadNoteText, MoveStep | no: aggro estimate notes (key off by default); firing stand / stand-pull (config keys off; 0 authored stand-pull rows) |
| leadInWater | 1652 | 4; runtime/DungeonRunPolicy.h:8379 | SpeedLeashApplies, StallFallback, StuckNote | check: stall / stuck / island rules (a walk that fails) |
| leadTargetGuid | 2048 | 3; runtime/DungeonRunPolicy.h:9913 | RunAutoCcFightTick, RunAutoCcPickOrder, RunCcReapplyTick | no: crowd control (a warrior/priest party has none; cc keys off) |
| dpsAlive | 2060 | 1; runtime/DungeonRunPolicy.h:22491 | TickBody | check: tick body (run line, rest, hp notes) |
| directJoinRead | 1673 | 3; runtime/DungeonRunPolicy.h:4290 | HoldForDirectJoin, NotWithLead, TickBody | check: direct-join step (move_direct rows only: the RFC export has none); tick body (run line, rest, hp notes) |
| fetchRead | 1708 | 1; runtime/DungeonRunPolicy.h:19912 | FarReturnTick | no: rescue / fetch / return teleports (GM power; walk-back rescue forced 0, return 0) |
| rescueReadX | 1713 | 1; runtime/DungeonRunPolicy.h:13074 | RescueLiveReach | no: rescue / fetch / return teleports (GM power; walk-back rescue forced 0, return 0) |
| rescueReadY | 1713 | 1; runtime/DungeonRunPolicy.h:13074 | RescueLiveReach | no: rescue / fetch / return teleports (GM power; walk-back rescue forced 0, return 0) |
| rescueReadZ | 1713 | 1; runtime/DungeonRunPolicy.h:13074 | RescueLiveReach | no: rescue / fetch / return teleports (GM power; walk-back rescue forced 0, return 0) |
| rescueReach | 1714 | 1; runtime/DungeonRunPolicy.h:13076 | RescueLiveReach | no: rescue / fetch / return teleports (GM power; walk-back rescue forced 0, return 0) |
| rezMembers | 1755 | 7; runtime/DungeonRunPolicy.h:22122 | TickBody | check: tick body (run line, rest, hp notes) |
| rezInProgress | 1756 | 2; runtime/DungeonRunPolicy.h:22119 | TickBody | check: tick body (run line, rest, hp notes) |
| anchorReachable | 1682 | 1; runtime/DungeonRunPolicy.h:5356 | CheckAnchor | yes: pack-cleared gate / anchor (margin default 10) |
| shortcutRead | 1736 | 1; runtime/DungeonRunPolicy.h:14039 | WalkBack | yes: fight forward / walk back / approach hops (key default on) |
| shortcutFor | 1737 | 1; runtime/DungeonRunPolicy.h:14039 | WalkBack | yes: fight forward / walk back / approach hops (key default on) |
| shortcutOk | 1738 | 1; runtime/DungeonRunPolicy.h:14039 | WalkBack | yes: fight forward / walk back / approach hops (key default on) |
| approachRead | 1770 | 2; runtime/DungeonRunPolicy.h:12162 | ApproachHop, WalkBack | yes: fight forward / walk back / approach hops (key default on) |
| standPatrols | 1794 | 1; runtime/DungeonRunPolicy.h:15240 | PatrolStandPullGate | no: firing stand / stand-pull (config keys off; 0 authored stand-pull rows) |
| packNearestLeft | 1824 | 3; runtime/DungeonRunPolicy.h:14506 | NearestLeftNote | yes: pack-cleared gate / anchor (margin default 10) |
| packNearestLeftRead | 1825 | 1; runtime/DungeonRunPolicy.h:14508 | NearestLeftNote | yes: pack-cleared gate / anchor (margin default 10) |

### C2: read only in functions the simulation never called

| field | decl | read sites (count; first) | in functions | RFC verdict: feature |
|---|---|---|---|---|
| bossGateSpell | 1879 | 1; runtime/DungeonRunPolicy.h:11447 | BossGateNote | yes: boss rows (4 in the plan) |
| bossGatePriests | 1882 | 2; runtime/DungeonRunPolicy.h:11449 | BossGateNote | yes: boss rows (4 in the plan) |
| respawnNest | 1625 | 2; runtime/DungeonRunPolicy.h:5385 | GatherAnchorInNest, NestLeavable | no: respawn nest (key off by default) |
| directJoinSupported | 1676 | 1; runtime/DungeonRunPolicy.h:4310 | HoldForDirectJoin | no: direct-join step (move_direct rows only: the RFC export has none) |
| dryWalk | 1698 | 1; runtime/DungeonRunPolicy.h:12338 | DryWalkWanted | no: dry walk / free-for-all loot (keys off or forced off) |
| stragglerPull | 1809 | 1; runtime/DungeonRunPolicy.h:14612 | StragglerNever | no: straggler pull (key off by default) |
| lootFfaOn | 1934 | 1; runtime/DungeonRunPolicy.h:16520 | LootFfaWanted | no: row kinds the RFC plan does not have (loot, open, use, gossip, ritual, hold, wait_object: 0 rows) |
| autoCcRisky | 2046 | 1; runtime/DungeonRunPolicy.h:9662 | RunCcBotFor | no: crowd control (a warrior/priest party has none; cc keys off) |
| autoCcAbove | 2047 | 2; runtime/DungeonRunPolicy.h:9498 | RunAutoCcThreshold | no: crowd control (a warrior/priest party has none; cc keys off) |
| auraGatedTargets | 2053 | 1; runtime/DungeonRunPolicy.h:18340 | GuardSkipsUnit | yes: fight guard (the tank engages what hits the lead) |
| doorNeedLockpick | 1589 | 1; runtime/DungeonRunPolicy.h:6904 | DoorQualified | no: doors and leg needs (the RFC plan has 0 such rows) |
| itemsSplit | 1909 | 4; runtime/DungeonRunPolicy.h:16757 | TaskStep, UseOnStep | no: row kinds the RFC plan does not have (loot, open, use, gossip, ritual, hold, wait_object: 0 rows) |
| taskActStarted | 1913 | 2; runtime/DungeonRunPolicy.h:17130 | UseOnStep | no: row kinds the RFC plan does not have (loot, open, use, gossip, ritual, hold, wait_object: 0 rows) |
| objectReacted | 1914 | 2; runtime/DungeonRunPolicy.h:17147 | UseOnStep | no: row kinds the RFC plan does not have (loot, open, use, gossip, ritual, hold, wait_object: 0 rows) |
| carrierHasItem | 1921 | 5; runtime/DungeonRunPolicy.h:16562 | LootFfaStep, TaskStep, UseOnStep | no: row kinds the RFC plan does not have (loot, open, use, gossip, ritual, hold, wait_object: 0 rows) |
| carrierHasEntryItem | 1922 | 1; runtime/DungeonRunPolicy.h:16782 | TaskStep | no: row kinds the RFC plan does not have (loot, open, use, gossip, ritual, hold, wait_object: 0 rows) |
| carrierBusy | 1923 | 10; runtime/DungeonRunPolicy.h:16569 | GossipStep, LootFfaStep, OpenProofStep, RitualStep ... | no: row kinds the RFC plan does not have (loot, open, use, gossip, ritual, hold, wait_object: 0 rows) |
| itemHolder | 1926 | 2; runtime/DungeonRunPolicy.h:16720 | TaskStep | no: row kinds the RFC plan does not have (loot, open, use, gossip, ritual, hold, wait_object: 0 rows) |
| lootTurn | 1928 | 4; runtime/DungeonRunPolicy.h:16466 | NextLootBot | no: row kinds the RFC plan does not have (loot, open, use, gossip, ritual, hold, wait_object: 0 rows) |
| corpseChecked | 1929 | 2; runtime/DungeonRunPolicy.h:16483 | LootGivenUp, TaskStep | no: row kinds the RFC plan does not have (loot, open, use, gossip, ritual, hold, wait_object: 0 rows) |
| corpseHasItem | 1930 | 2; runtime/DungeonRunPolicy.h:16483 | LootGivenUp, TaskStep | no: row kinds the RFC plan does not have (loot, open, use, gossip, ritual, hold, wait_object: 0 rows) |
| lootMethod | 1935 | 6; runtime/DungeonRunPolicy.h:16521 | LootFfaRestoreTick, LootFfaStart, LootFfaStep, LootFfaWanted | no: row kinds the RFC plan does not have (loot, open, use, gossip, ritual, hold, wait_object: 0 rows) |
| nestUnits | 1622 | 1; runtime/DungeonRunPolicy.h:5396 | GatherAnchorInNest | no: respawn nest (key off by default) |
| standPullOwnRead | 1798 | 1; runtime/DungeonRunPolicy.h:15071 | ScopeStandPullFacts | no: firing stand / stand-pull (config keys off; 0 authored stand-pull rows) |
| shotStandGuid | 2013 | 1; runtime/DungeonRunPolicy.h:11174 | HoldForShot | no: firing stand / stand-pull (config keys off; 0 authored stand-pull rows) |
| shotStandKnown | 2014 | 5; runtime/DungeonRunPolicy.h:11304 | HoldForShot | no: firing stand / stand-pull (config keys off; 0 authored stand-pull rows) |
| shotStandX | 2015 | 1; runtime/DungeonRunPolicy.h:11329 | HoldForShot | no: firing stand / stand-pull (config keys off; 0 authored stand-pull rows) |
| shotStandY | 2015 | 1; runtime/DungeonRunPolicy.h:11330 | HoldForShot | no: firing stand / stand-pull (config keys off; 0 authored stand-pull rows) |
| shotStandZ | 2015 | 1; runtime/DungeonRunPolicy.h:11331 | HoldForShot | no: firing stand / stand-pull (config keys off; 0 authored stand-pull rows) |
| shotStandYd | 2016 | 1; runtime/DungeonRunPolicy.h:11332 | HoldForShot | no: firing stand / stand-pull (config keys off; 0 authored stand-pull rows) |
| shotStandBlocked | 2023 | 3; runtime/DungeonRunPolicy.h:11303 | HoldForShot | no: firing stand / stand-pull (config keys off; 0 authored stand-pull rows) |
| shotStandBestX | 2024 | 1; runtime/DungeonRunPolicy.h:11329 | HoldForShot | no: firing stand / stand-pull (config keys off; 0 authored stand-pull rows) |
| shotStandBestY | 2024 | 1; runtime/DungeonRunPolicy.h:11330 | HoldForShot | no: firing stand / stand-pull (config keys off; 0 authored stand-pull rows) |
| shotStandBestZ | 2024 | 1; runtime/DungeonRunPolicy.h:11331 | HoldForShot | no: firing stand / stand-pull (config keys off; 0 authored stand-pull rows) |
| shotStandBestYd | 2025 | 1; runtime/DungeonRunPolicy.h:11332 | HoldForShot | no: firing stand / stand-pull (config keys off; 0 authored stand-pull rows) |
| shotStandBlockEntry | 2026 | 2; runtime/DungeonRunPolicy.h:11311 | HoldForShot | no: firing stand / stand-pull (config keys off; 0 authored stand-pull rows) |
| shotStandBlockPatrols | 2027 | 2; runtime/DungeonRunPolicy.h:11303 | HoldForShot | no: firing stand / stand-pull (config keys off; 0 authored stand-pull rows) |
| shotStandBlockYd | 2028 | 2; runtime/DungeonRunPolicy.h:11311 | HoldForShot | no: firing stand / stand-pull (config keys off; 0 authored stand-pull rows) |
| shotStandBlockNowClear | 2029 | 1; runtime/DungeonRunPolicy.h:11303 | HoldForShot | no: firing stand / stand-pull (config keys off; 0 authored stand-pull rows) |
| eventDown | 1901 | 2; runtime/DungeonRunPolicy.h:17199 | CountEvent | no: row kinds the RFC plan does not have (loot, open, use, gossip, ritual, hold, wait_object: 0 rows) |
| clearUnits | 1903 | 2; runtime/DungeonRunPolicy.h:8440 | WaitClearCount, WaitClearOffFloorOnly | check: unclassified |
| charmedFighting | 1628 | 2; runtime/DungeonRunPolicy.h:21031 | 6; | check: unclassified |
| leadMoveFailReason | 1654 | 5; runtime/DungeonRunPolicy.h:8382 | StallFallback, StuckNote | check: stall / stuck / island rules (a walk that fails) |
| leadStayWalkFails | 1658 | 1; runtime/DungeonRunPolicy.h:8381 | StuckNote | check: stall / stuck / island rules (a walk that fails) |
| stragglerPathRead | 1814 | 1; runtime/DungeonRunPolicy.h:14820 | StragglerPullTick | no: straggler pull (key off by default) |
| stragglerPathFor | 1815 | 1; runtime/DungeonRunPolicy.h:14820 | StragglerPullTick | no: straggler pull (key off by default) |
| stragglerPathTank | 1816 | 1; runtime/DungeonRunPolicy.h:14820 | StragglerPullTick | no: straggler pull (key off by default) |
| stragglerPathToUnit | 1819 | 1; runtime/DungeonRunPolicy.h:14821 | StragglerPullTick | no: straggler pull (key off by default) |
| stragglerPathOk | 1820 | 1; runtime/DungeonRunPolicy.h:14835 | StragglerPullTick | no: straggler pull (key off by default) |
| stragglerPathBlockEntry | 1821 | 2; runtime/DungeonRunPolicy.h:14838 | StragglerPullTick | no: straggler pull (key off by default) |
| stragglerPathBlockYd | 1822 | 1; runtime/DungeonRunPolicy.h:14841 | StragglerPullTick | no: straggler pull (key off by default) |
| stragglerPathBlockReachYd | 1823 | 1; runtime/DungeonRunPolicy.h:14841 | StragglerPullTick | no: straggler pull (key off by default) |
| proofRead | 1919 | 2; runtime/DungeonRunPolicy.h:16679 | OpenProofStep, TaskStep | no: row kinds the RFC plan does not have (loot, open, use, gossip, ritual, hold, wait_object: 0 rows) |
| proofUp | 1920 | 2; runtime/DungeonRunPolicy.h:16679 | OpenProofStep, TaskStep | no: row kinds the RFC plan does not have (loot, open, use, gossip, ritual, hold, wait_object: 0 rows) |
| partyHasItem | 1925 | 3; runtime/DungeonRunPolicy.h:16562 | LootFfaStep, TaskStep | no: row kinds the RFC plan does not have (loot, open, use, gossip, ritual, hold, wait_object: 0 rows) |
| leadHeldEntries | 1955 | 1; runtime/DungeonRunPolicy.h:18147 | LeadHoldsPlanBoss | check: unclassified |
| aheadBodies | 1976 | 2; runtime/DungeonRunPolicy.h:3470 | AheadNoteText | no: aggro estimate notes (key off by default) |
| aheadCeiling | 1977 | 3; runtime/DungeonRunPolicy.h:3470 | AheadNoteText | no: aggro estimate notes (key off by default) |
| aheadGuids | 1979 | 4; runtime/DungeonRunPolicy.h:3474 | AheadNoteText | no: aggro estimate notes (key off by default) |
| aheadLosBudget | 1980 | 1; runtime/DungeonRunPolicy.h:3482 | AheadNoteText | no: aggro estimate notes (key off by default) |
| aheadUnavailable | 1981 | 1; runtime/DungeonRunPolicy.h:3468 | AheadNoteText | no: aggro estimate notes (key off by default) |
| leadFloorZ | 1642 | 1; runtime/DungeonRunPolicy.h:4483 | 30000; | check: unclassified |
| leadStayWalkResting | 1657 | 1; runtime/DungeonRunPolicy.h:8381 | StuckNote | check: stall / stuck / island rules (a walk that fails) |
| leadLeadsGroup | 1936 | 1; runtime/DungeonRunPolicy.h:16520 | LootFfaWanted | no: row kinds the RFC plan does not have (loot, open, use, gossip, ritual, hold, wait_object: 0 rows) |
| itemPlayer | 1566 | 10; runtime/DungeonRunPolicy.h:16493 | LootGivenUp, TaskStep, UseOnStep | no: row kinds the RFC plan does not have (loot, open, use, gossip, ritual, hold, wait_object: 0 rows) |
| lootTurnPlayer | 1567 | 2; runtime/DungeonRunPolicy.h:16480 | LootGivenUp | no: row kinds the RFC plan does not have (loot, open, use, gossip, ritual, hold, wait_object: 0 rows) |
| fetchReach | 1715 | 2; runtime/DungeonRunPolicy.h:4799 | FetchPoint | no: rescue / fetch / return teleports (GM power; walk-back rescue forced 0, return 0) |
| fetchSpots | 1716 | 8; runtime/DungeonRunPolicy.h:4801 | FetchPoint | no: rescue / fetch / return teleports (GM power; walk-back rescue forced 0, return 0) |
| summonSeen | 1915 | 1; runtime/DungeonRunPolicy.h:17077 | UseOnStep | no: row kinds the RFC plan does not have (loot, open, use, gossip, ritual, hold, wait_object: 0 rows) |
| stallDirectRead | 1662 | 1; runtime/DungeonRunPolicy.h:8524 | StallFallback | check: stall / stuck / island rules (a walk that fails) |
| stallDirectClear | 1663 | 1; runtime/DungeonRunPolicy.h:8524 | StallFallback | check: stall / stuck / island rules (a walk that fails) |
| forwardRead | 1728 | 1; runtime/DungeonRunPolicy.h:21598 | FightForwardReadRefusal | yes: fight forward / walk back / approach hops (key default on) |
| forwardFor | 1729 | 1; runtime/DungeonRunPolicy.h:21598 | FightForwardReadRefusal | yes: fight forward / walk back / approach hops (key default on) |
| forwardPathOk | 1730 | 1; runtime/DungeonRunPolicy.h:21600 | FightForwardReadRefusal | yes: fight forward / walk back / approach hops (key default on) |
| forwardLos | 1731 | 1; runtime/DungeonRunPolicy.h:21602 | FightForwardReadRefusal | yes: fight forward / walk back / approach hops (key default on) |
| forwardHostileNear | 1732 | 1; runtime/DungeonRunPolicy.h:21604 | FightForwardReadRefusal | yes: fight forward / walk back / approach hops (key default on) |
| forwardHopAlive | 1733 | 1; runtime/DungeonRunPolicy.h:21606 | FightForwardReadRefusal | yes: fight forward / walk back / approach hops (key default on) |

### D: never read in DungeonRunPolicy.h

nestUnitsRead, seekClockRead, standOthers, standPackReach, holdAheadReach
