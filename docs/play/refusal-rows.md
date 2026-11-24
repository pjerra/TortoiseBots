---
id: play-refusal-rows
title: Refusal and Abort Rows
category: play
summary: The rule that a decision not to act says why, the run notes and log rows that follow it on WoW Forever, and where the Turtle-only row inventory went.
tags: [guide, observability, logging, dungeon, tactics]
relates_to:
  - play-boss-tactics
  - play-dungeon-run-leader
  - server-configuration
---

# Refusal and Abort Rows

When the bot code decides **not** to act (a refusal, a hold, a skip, an abort, a "no X" scan result), the note or log row says why in a word a script can count.

## The rule

> A refusal row carries a reason code in a field of its own (`reason=`, or an existing fixed-word field such as `verdict=` or `trigger=`) from a small word set documented next to the emit. When several candidates were rejected, the row carries per-code counts. Adding the reason never changes what the bot does.

Two row shapes follow it:

- **Run notes.** The dungeon run writes its refusal into the run's status note as plain text that names the order and carries the module's own answer after it, for example `its <order> was refused (<answer>)` or `pull refused (<answer>): that unit is left out`. `.tb run status <leader>` shows the note, and so does the addon.
- **Bot log rows.** The AI's event log writes `<event>,<info1>,<info2>` rows. A refusal row puts the fixed word in `info1` (`HealSkip` with `out-of-range` or `execute-refused`), or in a `key=value` pair of `info2`.

An existing row keeps its prefix: new fields are appended as `,key=value` pairs.

## Run notes

These are `r.note` / `r.stragglerFail` texts from `runtime/DungeonRunPolicy.h`, not log rows. Each names the module's answer in parentheses.

| Row family | Example text | Emit |
| :--- | :--- | :--- |
| Walk to the lead refused | `the walk of <bot> to the lead was refused (<answer>)` | `StragglerWalkRefused` |
| Order refused at an island | `cmd <order> refused (<answer>), no reachable point near it either` | `RefusedFromIsland` |
| Straggler order refused | `its <order> was refused (<answer>)` | `OrderRefused` |
| Retreat refused | `retreat refused (<answer>): fighting` | `OrderRefused` |
| Test-speed hold refused | `test speed: the hold for the party was refused (<answer>): walking on` | `OrderRefused` |
| Teleport failed | `the teleport of the stranded lead bot failed (<why>)` | `StrandFailed` |
| Revive summon refused | `<name> summoned to it, refused (<answer>)` | `OrderRefused` |
| Free-for-all loot refused | `loot: free-for-all refused (<answer>): looking for another bot to take it` | `OrderRefused` |
| Generic order refused | `cmd <order> refused: <answer>` | `OrderRefused` |
| Walk to the body refused | `regroup: the walk to the body was refused (<answer>)` | `OrderRefused` |
| Gather refused | `gather refused (<answer>): <sending it once more\|going on without it>` | `OrderRefused` |
| Stand point refused | `back to the stand point refused (<answer>): <going on from here\|...>` | `OrderRefused` |
| Pull refused | `pull refused (<answer>): that unit is left out` | `OrderRefused` |
| Leg refused | `a leg toward <x> <y> <z> was refused too (<answer>)` | `TooFarLegTaken` |
| Plan-point move refused | `step <n>: cmd move to the plan's point <p> refused (<answer>)` | `MoveRefusedUnreachable` |
| Fight forward refused | `fight forward: not to the pack of step <n> (<refused>)` | `FightForwardTick` |
| Boss gate | `<gate note>; not pulling` | `BossGateTick` |
| Party member dead | `<wait note>: no pull while a party member is dead` | `PullUnit` |
| Skipped, not cleared | `<why>: skipped, not cleared (<counts>)` | `StandPullGiveUp`, `FightStep` |
| Skipped step | `skipped step <n>` / `leg <x> skipped: it needs item <id>` | `Skip`, `LegNeedGate` |

## Log rows

| Event | Row | Reason word |
| :--- | :--- | :--- |
| `HealSkip` | `HealSkip,<word>,spell=<id> result=<n> <state>` | `out-of-range`, `line-of-sight`, `no-power`, `casting`, `moving`, `target-invalid`, `not-in-control`, `not-ready`, `not-known`, `cast-check-failed` |
| `HealSkip` | `HealSkip,execute-refused,spell=<id> <state>` | no cause is available: the execute call returns a bool only |
| `HealState` | `HealState,snapshot,...` | not a refusal (a state sample) |
| `LongStuckFallback` | `LongStuckFallback,graveyard\|homebind,<master>` | not a refusal (the fallback moved the bot) |
| `ReachGiveUp` | `ReachGiveUp,<target>,combat-stuck|entry=..|dist=..|los=..` | the `combat-stuck` prefix |
| `UnstuckTrip` | `UnstuckTrip,<source>,<repeats>` | not a refusal (an episode starts) |
| `BotUnstick` | `BotUnstick,<master>,reason=nopath_island secs=<n> master_yd=<n> result=teleported\|refused` | `reason=` |

## Where the Turtle row inventory went

The long catalogue of boss-tactics rows (`seek pull`, `seek take`, the Son meet, Cause Insanity, the tank-swap and spread rows) and their reason-code tables described the Turtle build's `BossTacticsActions.cpp`. That file is not part of the Forever build, so none of those rows is written here: **Not on WoW Forever yet.** The module still compiles the boss-tactics manager and the threat values, but the actions that emitted those rows are absent.

The templates that belonged to that catalogue (walk-out pick tally, take words, meet refusal words, spread verdicts) are therefore gone from this page rather than left to mislead a parser author.
