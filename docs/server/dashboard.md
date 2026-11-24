---
id: server-dashboard
title: Observability Dashboard & Telemetry
category: server
summary: How the WoW Forever build feeds the optional web dashboard over UDP, the module settings that control it, and what each tab and API answers.
tags: [server, observability, telemetry, dashboard, metrics, prometheus]
relates_to:
  - server-settings-logging
  - server-bot-pool
  - server-admin-commands
---

# Observability Dashboard & Telemetry

The observability service is an optional standalone program that shows what the bot population is doing: a web dashboard with a live map, plus a Prometheus metrics endpoint. The game server sends it status updates as non-blocking UDP datagrams.

For the module's logging settings, see [Logging & diagnostic settings](settings/logging.md).

## Architecture

The module's dashboard sender (`TortoiseBots.Dashboard.*`) builds the datagrams on the world thread and sends them over UDP (default `127.0.0.1:9195`). The Go daemon in `tools/observability` serves the dashboard and metrics:

```text
Game server (TortoiseBots module)
    │  UDP: HEARTBEAT + BOT_BATCH, SERVER_INFO, BOT_EVENTS, anomalies
    ▼
Go daemon (tools/observability)
    ├──► Web dashboard (:8095/dashboard)
    ├──► Prometheus metrics (:8095/metrics)
    ├──► REST API (/api/v1/status, /api/v1/bots, /api/v1/issues, ...)
    └──► Live stream (/api/v1/live)
```

While `TortoiseBots.Dashboard.Enable = 0` nothing is built or sent.

## Module settings

All keys live in `worldserver.conf.d/tortoisebots.conf`:

| Key | Default | Meaning |
| :--- | :---: | :--- |
| `TortoiseBots.Dashboard.Enable` | 1 | Send the roster to the dashboard. Only while `TortoiseBots.Enable = 1`. |
| `TortoiseBots.Dashboard.Host` | `127.0.0.1` | Where the daemon listens. A host name is resolved at config load. |
| `TortoiseBots.Dashboard.Port` | 9195 | The daemon's `-udp-port`. |
| `TortoiseBots.Dashboard.IntervalMs` | 2000 | How often the roster is sent (at least 500). |
| `TortoiseBots.Dashboard.ItemsFile`, `UiMapsFile`, `SpellsFile`, `TalentsFile`, `NamesFile` | empty | Where the module writes the data tables the daemon reads. Empty = `<LogsDir>/tortoisebots-*.json`. Point the daemon's `-data-dir` at that folder. |
| `TortoiseBots.Dashboard.Runs` | 1 | Send dungeon runs to the Commanders tab. Engine build only, and only while the dashboard is enabled. |
| `TortoiseBots.Dashboard.RunSummaryDir` | empty | Folder for `<run>.summary.json` files (Dungeon runs tab). Empty = no summary files. |
| `TortoiseBots.Dashboard.TraceDir` | empty | Folder for a summary of every dungeon or raid run with bots. Empty = `<LogsDir>/runs`. |

Point the daemon's `-trace-dir` (`TBDASH_TRACE_DIR`) at the same folder.

## Quick setup

What you need:

- The server running (database and worldserver), with the module enabled.
- A game account with GM rank 2 or higher. You sign in to the dashboard with the same name and password you use in the game.
- The daemon binary and the database login the server uses.

### The Forever start script

`tools/observability/run-dashboard-forever.sh` starts the binary with ForeverCore's defaults and accepts extra flags:

```bash
./tools/observability/run-dashboard-forever.sh
```

It expects `tools/observability/tortoise-observability`. Its environment variables and defaults:

| Variable | Default |
| :--- | :--- |
| `TBDASH_CORE` | `forever` |
| `DB_HOST`, `DB_PORT` | `127.0.0.1`, `3310` |
| `DB_USER`, `DB_PASSWORD` | `trinity`, `trinity` |
| `DB_LOGIN`, `DB_CHAR`, `DB_WORLD` | `auth`, `characters`, `world` |
| `HTTP_HOST`, `HTTP_PORT` | `127.0.0.1`, `8095` |
| `UDP_HOST`, `UDP_PORT` | `127.0.0.1`, `9195` |
| `MIN_GM_LEVEL` | `2` |

Set the variables for your server, then open `http://localhost:8095` and sign in. If port 8095 is taken, the daemon says so; pass `-http-port` (or set `HTTP_PORT`) to move it.

### Manual start

```bash
./tortoise-observability -core forever \
  -db-host 127.0.0.1 -db-port 3310 -db-user USER -db-pass PASSWORD \
  -db-name auth -db-char characters -db-world world \
  -http-host 127.0.0.1 -http-port 8095 -udp-host 127.0.0.1 -udp-port 9195 -min-gm-level 2
```

### Sign-in problems

| Symptom | Check |
| :--- | :--- |
| "invalid username or password" | The dashboard shows this for every sign-in problem. Check the database login first, then the GM rank, then that the account belongs to this realm. |
| Blocked after 5 attempts | Wait 5 minutes or restart the daemon. |
| Page opens but stays empty | `TortoiseBots.Enable = 1`, `TortoiseBots.Dashboard.Enable = 1`, and the host/port match the daemon. |
| Page does not open | The daemon is not running; check its log. |

## What the dashboard shows

| Tab | Key questions | What it reads |
| :--- | :--- | :--- |
| **Overview** | Is the server healthy? | Population, world tick rate, population graphs, state census. |
| **Bots** | Who is online and what are they doing? | Roster with level, class, role, gold, gear and guild; real players are shown too. |
| **Map** | Where is the fleet? | 2D world and zone map with player and bot markers and trails. |
| **Commanders** | Who leads a bot party? | Bound commanders, party health, orders, live fight view and map layers. |
| **Dungeon runs** | How did the traced runs go? | Run summaries from `-trace-dir`: success rate, clear times, boss kills, wipes, deaths. |
| **Dungeons** | What is running now? | Instance copies with bots, member health, player badges. |
| **Raids** | What raid copies hold bots? | Raid copies by group; a bound group links to its Commanders card. |
| **Raid coverage** | Which boss abilities have an answer? | The coded answers and the gaps. |
| **Progress** | Are bots levelling? | XP per hour, level-up timeline, average gear item level. |
| **Activity** | What happened? | Quests, rare loot, gathered nodes, kills. |
| **Guild orders** | What are guild bots doing? | Orders, progress and destinations. |
| **Auction House** | What is on the market? | Listings by bots and players. |
| **Issues** | Which bots are stuck? | Navigation stalls, long deaths, unreachable targets. |
| **Server** | What settings are live? | Rates and the engine's live configuration flags. |

**What the Forever sender does not send:** per-bot AI visit counters and pool lineage/level-band health. Those panels read empty or say so; the Forever pool has no lineages and no AI visit counter. Everything else in the roster is filled from the live engine and the core.

### State census

| State | Meaning |
| :--- | :--- |
| **Combat** | In combat. |
| **Moving** | A movement generator owns the bot. |
| **Busy** | Standing still but doing real work: looting, casting, sitting to eat/drink, movement within 45 s, or a fight/XP gain within 60 s. |
| **Stalled** | Standing still for 45+ s with only churn: an active travel target or a changing action name, no movement, loot, cast, sit, fight or XP gain. |
| **Resting** | The rest flag is set. |
| **Idle** | No movement, action, cast, loot or target for 45+ s. |
| **Dead** | A corpse. |

### Bot profile

Clicking a bot opens a profile read from the character database: vitals, location, strategies, gear and item level, talents, spells, professions, skills, bags, and recent kills, quests and loot.

### Dungeons and raids

- **Dungeons** groups live copies by instance id, so two runs of the same dungeon stay apart. Selecting one shows the map, the party and the first living tank as the anchor.
- **Raids** groups copies by raid and group. An unbound group shows roster data only; a bound group links to its Commanders card and its run line.

### Commanders and maps

Each Commanders card draws a live map of its party with a **Map layers** panel (map picture, party, enemies, route, threat, events, objects). The choice is kept in the browser.

With a folder of client map pictures the maps draw the game's map under the party; without one the markers still work and the map says the picture is missing.

### Dungeon runs tab

The **Dungeon runs** tab reads the run summaries the server writes to `TortoiseBots.Dashboard.RunSummaryDir` or `TraceDir` (see [Run trace](run-trace.md)). It shows per dungeon: runs, success rate, best and median clear time, boss kill times, wipes and deaths. Build and driver filters narrow every number. This is not the live **Dungeons** tab: that one shows the instance copies right now, this one the finished and running traced runs.

## Issue tracking

The daemon turns persistent problems into episodes:

- **Stuck navigation (`STUCK`)**: a moving bot whose position stays frozen for 60+ seconds.
- **Prolonged death (`DEAD_LONG`)**: a dead bot that neither releases nor revives for more than 2 minutes.
- **Unreachable target (`UNREACHABLE_TARGET`)**: a bot keeps a hostile target more than 45 yards away or out of line of sight for 10 seconds.
- **Action loop (`ACTION_LOOP`)**: the same action fails five times within 2 seconds (a counter only, not an episode).

Episodes pass an age gate before alerting (5 minutes by default; `UNREACHABLE_TARGET` after 2 minutes), so transient pathing pauses do not alert.

## Telemetry APIs and metrics

Prometheus metrics at `http://localhost:8095/metrics` include:

- `tortoisebots_active_count{class,role}`
- `tortoisebots_state_ratio{state}`
- `tortoisebots_grinding_xp_per_hour_total`
- `tortoisebots_issues_active{type}`
- `tortoisebots_anomalies_total{type}`
- `tortoisebots_snapshots_total`

Selected REST endpoints (login required, same GM rank):

- `GET /api/v1/status`, `/api/v1/bots`, `/api/v1/issues`
- `GET /api/v1/armory/bots`, `/api/v1/armory/bot/{guid}`
- `GET /api/v1/commanders`, `/api/v1/dungeons`, `/api/v1/raids`
- `GET /api/v1/dungeon-runs`, `/api/v1/dungeon-runs/<map>/runs`
- `GET /api/v1/activity`, `/api/v1/activity/loot`, `/api/v1/activity/quests`
- `GET /api/v1/auctions`
- `WS /api/v1/stream`

For the fight debugger, map layers, agent maps and per-endpoint fields, see [`tools/observability/README.md`](../../tools/observability/README.md).

## Pool KPI report

For offline analysis of a long run:

```bash
python3 tools/pool_kpi_report.py \
  --bots-json path/to/bots.json \
  --logs /path/to/logs \
  --reset "2026-10-02 12:54" \
  --baseline tools/pool_kpi_baseline_2026-10-02.json
```

It reports level progression and XP rate, deaths per minute and prolonged ghosts, and whether bots move to level-appropriate zones. Other flags: `--db-cmd`, `--save-baseline`, `--out`, `--anomaly-totals-json`.

## Map artwork

Map images are client assets and are not bundled:

```bash
python3 -m pip install mpyq Pillow
python3 tools/observability/scripts/gen_zone_maps.py \
  --data "/path/to/WoW/Data" \
  --out tools/observability/maps
```

Point the Map tab and the daemon's `-map-dir` (or `TBDASH_MAP_DIR`) at that folder.
