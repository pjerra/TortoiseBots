---
id: server-settings-logging
title: Logging & telemetry settings
category: server
summary: Configuration settings for the module's log category, bot log file, CSV event logs, per-bot action logs, action counts, healer and boss diagnostics, and run traces.
tags: [config, logging, telemetry, diagnostics]
relates_to:
  - server-dashboard
  - server-settings-performance
  - server-run-trace
---

# Logging & Telemetry Settings

These settings configure the module's own log output, the bot engine's CSV event files, per-bot action traces, action counters and run traces.

Two configuration files are involved:

- `tortoisebots.conf` holds the server-side switches (the module's log category, the dashboard). A `.reload config` applies changes here.
- `aiplayerbot.conf` holds the bot engine's settings. The engine reads it once, when it first starts a bot, so a change needs a server restart.

The live dashboard in this build is described in [Dashboard](../dashboard.md). The separate observability daemon of the Turtle WoW version is not used, and its `AiPlayerbot.Observability*` keys change nothing.

> **Which bots these settings apply to.** Most of the files below are written by the bot engine. Every bot the pool logs in, and every character a player adds with `.bot add`, runs the engine. A bot a GameMaster logs in with `.tb login <character> <master>` runs it only with `TortoiseBots.Slice.EngineByDefault = 1` in `tortoisebots.conf`, or after `.tb engine <character> on`.

## Summary

| Setting | Default | What it does |
| :--- | :---: | :--- |
| [`Logger.tortoisebots`](#loggertortoisebots) | `3,Console Server` | The core log category for module lines (login/logout, the engine). |
| [`AiPlayerbot.BotLogFile`](#aiplayerbotbotlogfile) | empty | Sends the engine's own log lines to a separate file instead of the server log. |
| [`AiPlayerbot.AllowedLogFiles`](#aiplayerbotallowedlogfiles) | `bot_events.csv,deaths.csv` | Which CSV event logs the engine may write. |
| [`AiPlayerbot.LogRetentionDays`](#aiplayerbotlogretentiondays) | `3` | Days to keep packed CSV history. |
| [`AiPlayerbot.EnableActionLog`](#aiplayerbotenableactionlog) | `1` | Turns per-bot action log files on or off. |
| [`AiPlayerbot.ActionLogScope`](#aiplayerbotactionlogscope) | `commanded` | Which bots get an action log file. |
| [`AiPlayerbot.ActionLogMaxAgeDays`](#aiplayerbotactionlogmaxagedays) | `3` | Age limit for action log files. |
| [`AiPlayerbot.ActionLogMaxTotalMB`](#aiplayerbotactionlogmaxtotalmb) | `2048` | Total size limit for the `logs/bots/` folder. |
| [`AiPlayerbot.ActionLogMaxFileMB`](#aiplayerbotactionlogmaxfilemb) | `100` | Size at which one bot's log file is rotated. |
| [`AiPlayerbot.ActionCountsLog`](#aiplayerbotactioncountslog) | `0` | Writes cumulative action success/failure counts to CSV. |
| [`AiPlayerbot.HealerDiagnostics`](#aiplayerbothealerdiagnostics) | `0` | Writes healer snapshots and failed heal checks to `bot_events.csv`. |
| [`AiPlayerbot.RaidBossDiagnostics`](#aiplayerbotraidbossdiagnostics) | `0` | Writes boss threat samples, boss spell hits and cast cancels to `bot_events.csv`. |
| [`AiPlayerbot.RunTrace.Enabled`](#aiplayerbotruntraceenabled) | `0` | Allows a GM to record a party's run as a trace file. |
| [`AiPlayerbot.RunTrace.IntervalMs`](#aiplayerbotruntraceintervalms) | `500` | Milliseconds between two frames of a trace. |
| [`AiPlayerbot.RunTrace.Dir`](#aiplayerbotruntracedir) | `traces` | Folder for trace files, relative to `LogsDir`. |
| [`AiPlayerbot.RunTrace.MaxFileMb`](#aiplayerbotruntracemaxfilemb) | `64` | Size cap of one trace file. |

## Log destinations

### `Logger.tortoisebots`
The core log category that carries the module's own lines.
**Default:** `3,Console Server` · **Values:** `1` trace, `2` debug, `3` info, `4` warn, `5` error, plus the appenders · **Restart:** not needed (`.reload config` applies it)

<details>
<summary>How it works in detail</summary>

This setting lives in `tortoisebots.conf` and names the logger for everything the module writes through the core log: bot logins and logouts, the engine's messages, and the `slice: missing ...` warnings. Without a logger of its own the category would fall to the root logger, which only prints errors, so the module's information lines would not appear at all.

At `3` (info) the server log shows module warnings, module information and the periodic `BOTPERF` line. Set `5` to keep only errors. The appenders are the usual ones from `worldserver.conf` (`Console`, `Server`).

</details>

### `AiPlayerbot.BotLogFile`
Sends the bot engine's own log lines to a separate file.
**Default:** empty (the core `tortoisebots` log) · **Values:** a file name · **Restart:** needed

<details>
<summary>How it works in detail</summary>

When set, engine lines go to this file in the server's `LogsDir` (for example `bots.log`) instead of the server log, which keeps the server log readable on a busy realm. Empty keeps them in the module category.

`AiPlayerbot.BotLogDebug` (default `0`) additionally lets the engine's debug-level lines through to that file.

</details>

## CSV event logs

### `AiPlayerbot.AllowedLogFiles`
The CSV event logs the engine is allowed to write.
**Default:** `bot_events.csv,deaths.csv` · **Values:** comma-separated file names · **Restart:** needed

<details>
<summary>How it works in detail</summary>

- `bot_events.csv` holds behavioural rows: vendor, trainer, repair, mail and auction activity, revives, and the healer and boss diagnostic rows below.
- `deaths.csv` holds one row per bot death: position, killer and level, other attackers, and where the bot was travelling.
- `pvp_seek.csv` holds the open-world PvP seek-role grants and engagements (bot, target, level, map, zone).

Other diagnostic streams exist and are written only when their name is on the list, for example `travel_route_gate.csv` and `ghost_moves.csv`. The healer and boss diagnostic rows below need `bot_events.csv`. An empty value (`""`) turns off every CSV log; leaving the key out uses the two defaults.

Each file rotates once an hour and packs the previous piece into a `.csv.gz` (see `LogRetentionDays`). If a file cannot be opened, the server logs an error and retries after five minutes.

</details>

### `AiPlayerbot.LogRetentionDays`
How many days packed CSV history is kept.
**Default:** `3` · **Values:** days (`0` keeps everything) · **Restart:** needed

<details>
<summary>How it works in detail</summary>

A live CSV file holds one hour; when the hour changes it is packed into `<name>_<date>_<time>.csv.gz`. At server start, a log opened fresh also packs the previous run's file. Pieces older than this many days are deleted during packing. `0` disables the deletion.

</details>

## Per-bot action logs

### `AiPlayerbot.EnableActionLog`
Turns the per-bot action logs on or off.
**Default:** `1` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

With this on, eligible bots write a file under `logs/bots/`, named `<botname>_acc<account>_<timestamp>.log`, with their decision, target and ability-evaluation lines. Which bots are eligible is set by `ActionLogScope`; the files are pruned by the three limits below.

Logging every bot at once produces gigabytes per day, so keep the scope narrow.

</details>

### `AiPlayerbot.ActionLogScope`
Which bots get an action log file.
**Default:** `commanded` · **Values:** `commanded`, `all`, `none` · **Restart:** needed

<details>
<summary>How it works in detail</summary>

- `commanded` writes files only for bots in a group bound to a commander (`.tb master <bot|party> <commander>`). Masterless pool bots are skipped.
- `all` writes a file for every bot.
- `none` writes none.

An unknown word logs an error and uses `commanded`. The answer is re-checked every 15 seconds, so a bot that becomes commanded starts logging within that time and one that leaves closes its file.

</details>

### `AiPlayerbot.ActionLogMaxAgeDays`
Delete action log files older than this.
**Default:** `3` · **Values:** days (`0` no age limit) · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Files in `logs/bots/` whose last write is older than this are deleted during the periodic cleanup. Only the module's own file names are touched.

</details>

### `AiPlayerbot.ActionLogMaxTotalMB`
Size limit for the `logs/bots/` folder.
**Default:** `2048` · **Values:** megabytes (`0` unlimited) · **Restart:** needed

<details>
<summary>How it works in detail</summary>

When the folder is over the limit, the oldest files are deleted first until it fits. A file a bot is writing right now is never deleted, so the folder can stay over the limit if live files alone exceed it. Cleanup runs at server start, every hour, and early when the bots wrote a quarter of the limit since the last pass.

</details>

### `AiPlayerbot.ActionLogMaxFileMB`
Size at which one bot's log file is rotated.
**Default:** `100` · **Values:** megabytes (`0` unlimited) · **Restart:** needed

<details>
<summary>How it works in detail</summary>

When a file reaches this size it is closed cleanly and logging continues in a new numbered file for the same character.

</details>

## Counters and diagnostics

### `AiPlayerbot.ActionCountsLog`
Records how often each bot action runs and how often it succeeds.
**Default:** `0` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

With this on, every executed action is counted per class, and every five minutes a cumulative snapshot is appended to `logs/action_counts.csv` with the columns `utc_time,class,action,ok_count,fail_count`. The first snapshot of a run starts the file fresh.

To compare two windows, copy the file at the start and the end of a measurement and diff the counts:

```bash
cp logs/action_counts.csv /tmp/actions_early.csv
# wait 30+ minutes of active play
cp logs/action_counts.csv /tmp/actions_late.csv
python3 tools/action_counts_report.py /tmp/actions_early.csv /tmp/actions_late.csv --top 10
```

</details>

### `AiPlayerbot.HealerDiagnostics`
Writes healer snapshots and failed heal attempts to `bot_events.csv`.
**Default:** `0` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Only acts when `bot_events.csv` is allowed, and only for a grouped bot that is alive and has healing spells. At most every 10 seconds it writes one `HealState` row (mana, movement, casting, target health and range) and one `HealSkip` row when a heal was refused (out of range, no line of sight, no mana, moving, bad target). It reads the group only, never the world, and changes no decision.

</details>

### `AiPlayerbot.RaidBossDiagnostics`
Writes boss-fight diagnostics to `bot_events.csv`.
**Default:** `0` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Only acts when `bot_events.csv` is allowed. The rows:
- `BossThreat`: the current victim and the top threat entries of an engaged raid boss, at most once every two seconds per boss.
- `BossSpellHit`: a supported spell-damage hit on a boss, with the health it was hit at.
- `CastCancel`: a module-side cast cancel, with the bot, the spell, how far the cast had got, the site, the target, and how far the bot had moved.

It is observation only; no cast, move or decision changes. Turn it off after collecting the capture you need.

</details>

## Run traces

### `AiPlayerbot.RunTrace.Enabled`
Allows a GameMaster to record a party's run for offline scoring.
**Default:** `0` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

A run trace records a party's run as a `bt-trace` v1 JSONL file plus a `<run_id>.summary.json`. Start and stop it from the console or as a GameMaster:

```text
.tb trace start <run_id> [party [<leader>] | bot <Name>] [driver=<model>:<effort>:<run>]
.tb trace stop [<run_id>]
.tb trace status
```

With `0` the command answers that tracing is off, and a run in progress ends at the next tick. While no run is active the recorder costs almost nothing. Score a trace with `tools/trace_score.py`; the format is described in [Run trace](../run-trace.md).

</details>

### `AiPlayerbot.RunTrace.IntervalMs`
Milliseconds between two frames of a run trace.
**Default:** `500` · **Values:** `250` to `10000` (clamped) · **Restart:** needed

<details>
<summary>How it works in detail</summary>

A smaller interval gives a smoother trace and a bigger file.

</details>

### `AiPlayerbot.RunTrace.Dir`
Folder for run traces and their summaries.
**Default:** `traces` · **Values:** a path · **Restart:** needed

<details>
<summary>How it works in detail</summary>

A relative path is under the server's `LogsDir`; an absolute path is used as given.

</details>

### `AiPlayerbot.RunTrace.MaxFileMb`
Size cap of one run trace file.
**Default:** `64` · **Values:** `1` to `1024` (clamped) · **Restart:** needed

<details>
<summary>How it works in detail</summary>

When the file reaches the cap the run ends with reason `size_cap`, and its end record and summary are still written.

</details>
