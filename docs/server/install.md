---
id: server-install
title: Building, Installing & Updating TortoiseBots
category: server
summary: How to build the module for WoW Forever (ForeverCore), install its configuration and database files, enable the bot engine, and check the running build.
tags: [server, install, build, cmake, compile, update, version]
relates_to:
  - server-configuration
  - server-bot-pool
  - server-dashboard
---

# Building, Installing & Updating TortoiseBots

TortoiseBots compiles as an in-tree module for **ForeverCore**, the TrinityCore-based server for WoW Forever (Classic 1.60.1 client). Unlike the Turtle WoW build, the module has two layers:

- the session, host services, pool and `.tb` commands, which always build;
- the legacy bot engine, which builds only with `TORTOISEBOTS_SLICE1=ON`. Without it bots have no brain and `.bot` is unknown to the core.

The pinned core commit is in `forever/PINNED_CORE`. The module does not patch the core: it uses the public game API and `ScriptMgr` hooks.

For what the settings mean, see [How configuration works](configuration.md).

## Prerequisites

A working build environment for the pinned ForeverCore checkout (CMake, a C++ compiler, MySQL client libraries). Build the server itself first; the module is one more static library in that build.

## Quick build

From the root of the ForeverCore checkout:

### 1. Put the module in the tree

```bash
git clone https://github.com/Sagiroth/TortoiseBots.git src/modules/TortoiseBots
```

The core discovers it through `trinity_add_module`; no core file is edited.

### 2. Configure

```bash
cmake -S . -B build \
  -DBUILD_TORTOISEBOTS=1 \
  -DTORTOISEBOTS_SLICE1=ON
```

| CMake option | Default | Purpose |
| :--- | :---: | :--- |
| `BUILD_TORTOISEBOTS` | OFF | Builds the module at all. |
| `TORTOISEBOTS_SLICE1` | OFF | Also builds the legacy bot engine, the engine's config and `.bot`. OFF gives the session/host-only module. |
| `BUILD_PLAYERBOT_V2` | OFF | Must stay OFF: the configure stops when both bot systems are asked for. |
| `BUILD_FOREVER_LUA` | OFF | Builds the module's provider for the ForeverLua engine. It needs the `ForeverLua` module beside this one (`src/modules/ForeverLua`), otherwise configure stops with the expected path. |
| `TORTOISEBOTS_DEBUG_INFO` | OFF | Compiles the module with debug info (`-g`) for backtraces. |

### 3. Build and install

```bash
cmake --build build -j"$(nproc)"
cmake --install build
```

## Configuration files

The install writes the module's templates and never overwrites a live file:

| File | Installed from | Read by |
| :--- | :--- | :--- |
| `worldserver.conf.d/tortoisebots.conf` (rename the installed `.dist`) | `forever/conf/tortoisebots.conf.dist` | The module itself (`TortoiseBots.*` keys: enable, pool, battleground, dashboard, automation, player commands). |
| `aiplayerbot.conf` | `forever/conf/aiplayerbot.conf.dist`, only in a `TORTOISEBOTS_SLICE1` build | The legacy engine, at engine start, from **the same folder as `worldserver.conf`**. The installed `.dist` is a template; the deployment copies it next to `worldserver.conf` as `aiplayerbot.conf`. |

- Set `TortoiseBots.Enable = 1` in `worldserver.conf.d/tortoisebots.conf`. The shipped default is `0`: the module loads, its commands refuse and nothing ticks.
- The engine's `aiplayerbot.conf` is read through the engine's own config reader, not the core's. If it is missing, the server logs `TortoiseBots: engine configuration not loaded (...)` and the next engine start tries again.
- `TortoiseBots.Pool.Enable = 0`, `TortoiseBots.Bg.Enable = 0` and `AiPlayerbot.RunTrace.Enabled = 0` are off by default because they are big features; see [Bot pool](bot-pool.md), [Queues](../world/queues.md) and [Run trace](run-trace.md).

## Database

The module ships SQL in two folders, both idempotent and both delivered by the core's updater or by hand:

| Folder | Contents |
| :--- | :--- |
| `forever/sql/world/` | Dungeon plan tables and plan rows, boss rows, spec weight scales, travel-node tables, the zone-level cache. |
| `forever/sql/characters/` | Module tables: pool registry and reset state, commander bindings, guild orders and guild bot rank, disenchant queue, AI value store, custom strategies. |

### Delivering it through the core updater

1. Copy `forever/sql/world/` to `<SourceDirectory>/sql/tortoisebots/world/` and `forever/sql/characters/` to `<SourceDirectory>/sql/tortoisebots/characters/`.
2. Once per database, add the include row:
   ```sql
   INSERT INTO updates_include (path, state) VALUES ('$/sql/tortoisebots/world', 'RELEASED');
   INSERT INTO updates_include (path, state) VALUES ('$/sql/tortoisebots/characters', 'RELEASED');
   ```
   The first row goes into the world database, the second into the characters database.
3. Restart the worldserver. The updater applies every file it has not seen (or whose hash changed) in file-name order.

### Applying a file by hand

On a server without the include rows, run the file against the right database, for example:

```bash
mysql -u USER -p world < forever/sql/world/2026_11_20_00_tortoisebots_plans.sql
mysql -u USER -p characters < forever/sql/characters/2026_11_20_01_tb_pool.sql
```

Most SQL files do not name their database in their statements, so always pipe each file into the right database (`world` or `characters`). Only the travel-node and zone-level files name `world` themselves.

**Self-created tables.** Several character tables are also created by the module itself the first time the feature is enabled, when the database user may create tables: the pool tables, the commander binding, the guild bot rank, the disenchant queue and the AI store. That path is for a server whose user may not. A missing table disables that feature with one logged line; it never aborts the server.

## Checking the build

| Where | What you should see |
| :--- | :--- |
| Server startup log | `TortoiseBots (ForeverCore port, spike): enabled` (or `disabled` when `TortoiseBots.Enable = 0`). |
| Server log | `TortoiseBots: engine configuration loaded in <n> ms` in a `TORTOISEBOTS_SLICE1` build; a warning naming the reason when it is missing. |
| Server log | The first `not ported yet` line, when the engine reaches one of the unported services. |
| `.tb status` | `TortoiseBots: enabled, <n> bot session(s).` |
| `.bot version` | `TortoiseBots: version: forever-port (core <core version>)`, the addon line `TBM:VERSION|forever-port`, and the author line. |
| `.tb unported` | The closed list of unported features with the number of times the engine reached each. |

There is no release semver in the module itself: the build identifies itself as `forever-port` plus the core revision the module was compiled against.

## Updating

1. Pull the module:
   ```bash
   cd src/modules/TortoiseBots
   git pull
   ```
2. Re-run configure, build and install from the core root:
   ```bash
   cmake -S . -B build -DBUILD_TORTOISEBOTS=1 -DTORTOISEBOTS_SLICE1=ON
   cmake --build build -j"$(nproc)"
   cmake --install build
   ```
3. Copy any new SQL and restart. Your `worldserver.conf.d/tortoisebots.conf` and `aiplayerbot.conf` are kept; merge new keys from the `.dist` templates by hand.
4. Check `.tb unported` if you want to know which engine services the new build still lacks.
