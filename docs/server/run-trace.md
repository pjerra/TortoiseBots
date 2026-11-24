---
id: server-run-trace
title: Run Trace (opt-in recorder and scorer)
category: server
summary: Record what a party of bots did during one run as a bt-trace v1 JSONL file plus a per-run summary, and score it offline.
tags: [guide, trace, recorder, oracles, testing, schema]
relates_to:
  - server-configuration
  - play-dungeons-and-raids
  - server-dashboard
  - developers-architecture
---

# Run Trace

The run trace answers "what did the bots actually do in that run" with data instead of a watched replay. A GameMaster starts a **run** with a name (the **run id**); the server writes one JSONL **trace** of the party's bots until the run is stopped, plus a small **summary** file. `tools/trace_score.py` scores a trace offline and writes its verdicts into the summary, so one build can be compared with the next on the same scenario.

It is **off by default** (`AiPlayerbot.RunTrace.Enabled = 0`) and costs almost nothing while no run is active: one atomic check per world tick and one walk over an empty listener list per engine action. The schema is `bt-trace` version 1.

## Start, stop, status

```text
.tb trace start <run_id> [party [<leader>] | bot <Name>] [driver=<model>:<effort>:<run>]
.tb trace stop [<run_id>]
.tb trace status
```

`.tb trace` is a GameMaster command and also works from the server console. One run at a time.

- `party` (the default) follows the group of the sender, or of the party leader you name after `party`. The group must hold at least one bot with an AI. From the console you must name the leader (`party <leader>`).
- `bot <Name>` follows one bot.
- A run also ends by itself: size cap reached (`size_cap`), no bots for 10 s (`scope_lost`), config switch or bot AI off (`disabled`; a `start` with either off is refused), server shutdown (`shutdown`).
- If the starter logs out, the run ends once the group has no bots for 10 s (`scope_lost`).
- `.tb trace stop <run_id>` stops the run only when it is that run's own trace: another run's id answers `other-run` and nothing is stopped. A plain `.tb trace stop` stops whatever trace runs.

`driver=<model>:<effort>:<run>` names what drove the run. It may stand anywhere after the run id, once, and takes 1-96 letters, digits and `. _ - : +`. It is written to the summary as `driver` and shown by the dashboard as self-reported: the server cannot verify it.

A run id is 1-64 characters of letters, digits, `.`, `_` and `-`, and may not start with `.` (it becomes the file name). A run id that already has a trace file is refused.

## Files

Under `AiPlayerbot.RunTrace.Dir` (default `traces`, relative to `LogsDir`):

| File | Content |
| :--- | :--- |
| `<run_id>.jsonl` | The trace: one JSON object per line. |
| `<run_id>.summary.json` | One JSON object per run, written when the run starts (`status: running`) and when it ends; `tools/trace_score.py` adds the verdicts. |

| Key | Default | Meaning |
| :--- | :---: | :--- |
| `AiPlayerbot.RunTrace.Enabled` | 0 | Record traces. |
| `AiPlayerbot.RunTrace.IntervalMs` | 500 | Milliseconds between frames (250-10000). |
| `AiPlayerbot.RunTrace.Dir` | `traces` | Output folder. An absolute path stands as written; a relative one sits under `LogsDir`. |
| `AiPlayerbot.RunTrace.MaxFileMb` | 64 | Size cap of one trace file. |

The keys are read when the engine configuration loads, not by `.reload config`.

## Trace records

Every line is a flat JSON object with the same five leading fields: `"v":1` (schema version), `"k"` (the kind), `"run"` (the run id), `"src":"server"` (the producer), and `"t"` (milliseconds since the run started). GUIDs are strings, `"0x"` plus lower-case hex. Readers ignore unknown kinds and fields, so fields can be added without a version bump.

| `k` | When | Fields (besides the common five) |
| :--- | :--- | :--- |
| `header` | once, first | `schema` (`bt-trace`), `scope` (`party`/`bot`), `scope_name`, `requester`, `tb_version`, `start_unix`, `interval_ms`, `max_bytes`, `map`, `instance`, `caps` (what this producer records: `facing`, `moving`, `interrupts`, `in_fire`; a scorer answers `na` for an oracle whose capability is missing). |
| `party` | per scoped bot, when first seen (also a bot that joins later) | `g`, `name`, `class`, `role` (`tank`/`heal`/`dps`), `level`. |
| `mobs` | per hostile, the first time a bot sees it (registry, at most 1024 per run) | `g`, `entry`, `name`, `level`, `rank` (0 normal, 1 elite, 2 rare elite, 3 world boss, 4 rare), `boss`. |
| `frame` | every interval | `bots`: array of bot samples; `mobs`: array of hostile samples. |
| `event` | on change | `ev`, and when set `a`, `b`, `n`, `s`, `s2`. |
| `action` | an action the engine took out of its queue | `g`, `name`, `res`, `rel`, and when set `src_trigger`, `n`, `tl`. |
| `end` | once, last | `reason`, `counters`. |

**Bot sample** (in `frame.bots`): always `g`, `al` (alive), `cb` (in combat), `st` (engine state: `c` combat, `n` non-combat, `d` dead, `r` reaction), `hp`, `mhp`, `pw`, `mpw`, `pt` (power type: 0 mana, 1 rage, 2 focus, 3 energy), `x`, `y`, `z`, `mv` (movement generator active). When set: `tg` (target), `fa` (facing in degrees), `cs` (spell being cast), `fr` (harmful ground aura spell id), `act` (last executed action), `strat` (strategies, only when they changed).

**Hostile sample** (in `frame.mobs`): `g`, `hp` (% of max), `x`, `y`, `z`, `cb`, and when set `tg`, `cs`, `ev` (evading), `cc` (controlled), `thr` (top three threat entries). Hostiles are what the bots fight, target or are attacked by (at most 24 per frame).

**Events** (`ev`): `death`, `boss_kill`, `combat_on`, `combat_off`, `loot`, `cast_interrupted`, `action_loop` (the same action failed more than 8 times in 10 s).

**Action outcomes** (`res`): `OK`, `FAILED`, `IMPOSSIBLE`, `USELESS`, `PREREQ`, `VETOED`, `BACKOFF`. `rel` is the relevance after multipliers. A bot repeating the same action with the same outcome and trigger within one second is one record: `t` is the first, `tl` the last, `n` how many.

**End counters**: `frames`, `events`, `actions`, `party`, `mobs`, `deaths`, `loops`, `records`, `bytes`, `dropped`, `truncated`.

## Summary file

`<run_id>.summary.json`, one object:

```json
{"v":1,"schema":"bt-trace","run":"r1","src":"server","status":"complete","end_reason":"stop",
 "scope":"party","scope_name":"Aldo","requester":"Aldo","tb_version":"forever-port",
 "trace_file":"r1.jsonl","start_unix":1760000000,"end_unix":1760000600,"duration_ms":600000,
 "interval_ms":500,"map":70,"instance":12,
 "counters":{"frames":1200,"events":14,"actions":3100,"party":5,"mobs":22,"deaths":0,"loops":1,
             "records":4341,"bytes":2104332,"dropped":0,"truncated":false},
 "oracles":[{"id":"action_loop","name":"action failure loop","verdict":"fail","count":1,
             "first_ms":41200,"detail":"1 loop(s); live detector flagged 1"}],
 "verdict":"pass","scored_unix":1760000700,"scorer":"trace_score.py v1","trace_complete":true,"bad_lines":0}
```

`status` values: `running`, `complete`, `aborted`, `failed`, `incomplete`. A trace cut off by shutdown or crash is marked `incomplete` at the next server start. The scorer fills `oracles`, `verdict`, `scored_unix`, `scorer`, `trace_complete` and `bad_lines`.

### Fields the dashboard reads

The server also writes the fields the Dungeon runs tab reads. They come from events the recorder already sees; nothing reads the database or scans the world.

| Field | Content |
| :--- | :--- |
| `mode` | `run_leader` when the run leader walks a plan for the requester, `commander` when the requester has a standing `.tb master` binding, else `observe`. |
| `build` | `{"image", "module", "core"}`. |
| `driver` | The `driver=` argument; `human` for an observe run without one, `unknown` otherwise. |
| `started`, `ended` | ISO 8601 UTC. |
| `plan`, `dungeon`, `furthest` | The plan, the instance map and the highest step reached. |
| `bosses` | `[{"entry", "name", "killed_at_s"}]`. |
| `outcome`, `stop_reason` | `cleared`, `wiped_out`, `stopped`, `partial`, `abandoned`; the end reason. |
| `clear_s` | Seconds to the last boss kill, when cleared. |
| `wipes`, `deaths` | Position, bot, role and the run leader's step, at most 200 each. |
| `party` | Class, role and level of the traced bots. |

A boss counts as killed when a unit the bots saw alive is found dead. A boss killed before the trace started cannot be counted, so start the trace before the first pull.

## Scoring

```bash
python3 tools/trace_score.py traces/r1.jsonl            # report, and update r1.summary.json
python3 tools/trace_score.py traces/ --no-write          # every trace in a directory, read-only
python3 tools/trace_score.py r1.jsonl --json --set loop_failures=12
```

Ten oracles, all pure functions of the trace (no server needed):

| Id | Fails when |
| :--- | :--- |
| `action_loop` | the same action fails more than `loop_failures` (8) times within `loop_window_ms` (10 s). |
| `threat_off_tank` | a hostile in combat stays on a non-tank for `threat_hold_ms` (3 s) while a tank is alive and in combat. |
| `idle_caster` | a dps mage, warlock or priest in combat with a hostile in view, with at least `idle_min_power_pct` (15) % mana, neither casts nor executes an `OK` action for `idle_hold_ms` (4 s). |
| `deaths` | any party bot died. |
| `oom_healer` | a healer below `oom_pct` (5) % mana while the party fights for `oom_hold_ms` (3 s). |
| `facing` | a standing bot in melee or casting is more than `facing_deg` (90) degrees off its target for `facing_hold_ms` (2 s). |
| `self_interrupt` | one bot has `self_interrupt_count` (3) casts cancelled while moving within `self_interrupt_window_ms` (30 s). |
| `stuck` | movement is active but the bot moves less than `stuck_min_move_yd` (1 yd) for `stuck_hold_ms` (5 s), or paces back and forth in a small area. |
| `in_fire` | a bot stands in a harmful ground or area effect for `fire_hold_ms` (1.5 s). |
| `rest_failure` | eat/drink/rest fails `rest_fail_count` (5) times within `rest_fail_window_ms` (30 s), or a bot below `rest_low_mana_pct` (30) % mana stops recovering for `rest_stall_ms` (20 s). |

Thresholds can be overridden with `--set key=value`.

## Cost and limits

- **Off, or no run active:** one atomic check per world tick, nothing else.
- **During a run:** each interval, per scoped bot: member reads, its auras, its attackers, and a by-guid lookup of each hostile on its own map. No world scan, no database access.
- Lines are built on the world thread; disk writes run on another thread through a queue of at most 8192 lines / 4 MiB. A line that does not fit is dropped and counted.
- Size: roughly 200 bytes per bot and 150 per hostile per frame, plus action records; five bots in a fight at 500 ms is about 0.3-1 MiB a minute (estimate).
- The scope is one party; two parties at once need two servers or two runs one after the other.
