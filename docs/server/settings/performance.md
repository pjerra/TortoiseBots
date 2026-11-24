---
id: server-settings-performance
title: Performance & tick budget settings
category: server
summary: Configuration settings for AI time budgets, activity throttling, the world tick target, and core server tuning for large bot populations.
tags: [config, performance, tick, budget, optimization]
relates_to:
  - server-settings-world-population
  - server-dashboard
---

# Performance & Tick Budget Settings

These settings control how the bot engine schedules its work, how much CPU time bots may use per server tick, and a few core server settings that matter on a bot-heavy realm.

- Engine settings go in `aiplayerbot.conf`. The engine reads that file once, when it first starts a bot, so changing them needs a server restart.
- Core settings go in `worldserver.conf`. Those are the core's own settings; see its comments for the full description.

> **Which bots these settings apply to.** They shape the bot engine. Every bot the pool logs in, and every character a player adds with `.bot add`, runs the engine. A bot a GameMaster logs in with `.tb login <character> <master>` runs it only with `TortoiseBots.Slice.EngineByDefault = 1` in `tortoisebots.conf`, or after `.tb engine <character> on`.

The bot population is the main driver of CPU and memory use. When hosting hundreds or thousands of bots, the per-tick budgets below keep the world tick smooth.

## Summary

| Setting | Default | What it does |
| :--- | :---: | :--- |
| [`AiPlayerbot.Enabled`](#aiplayerbotenabled) | `1` | Master switch for the bot engine. |
| [`AiPlayerbot.DisableActivityPriorities`](#aiplayerbotdisableactivitypriorities) | `1` | Turns activity throttling off: every bot runs its AI fully. |
| [`AiPlayerbot.botActiveAlone`](#aiplayerbotbotactivealone) | `10` | Percentage of lone bots kept fully active when throttling is on. |
| [`AiPlayerbot.ForceActiveWhenNearPlayer`](#aiplayerbotforceactivewhennearplayer) | `0` | Keeps bots a human can see at full reaction rate. |
| [`AiPlayerbot.PoolTickBudgetUs`](#aiplayerbotpooltickbudgetus) | `22000` | Microseconds per world tick for pool bots. |
| [`AiPlayerbot.CombatTickBudgetUs`](#aiplayerbotcombattickbudgetus) | `15000` | Microseconds per world tick added to the pool budget. |
| [`AiPlayerbot.PoolBudgetWhenTickOverMs`](#aiplayerbotpoolbudgetwhentickoverms) | `0` | Only use the budgets when the previous tick was slower than this. |
| [`AiPlayerbot.TargetWorldTickMs`](#aiplayerbottargetworldtickms) | `100` | The tick length the self-tuning budget aims for. |
| [`AiPlayerbot.ValueRefreshAtInterval`](#aiplayerbotvaluerefreshatinterval) | `0` | Refreshes cached values at their full interval to save CPU. |
| [`AiPlayerbot.TravelPickBudgetUs`](#aiplayerbottravelpickbudgetus) | `4000` | Microseconds one bot may spend picking a travel destination per pass. |
| [`AiPlayerbot.AsyncTravelPartitions`](#aiplayerbotasynctravelpartitions) | `0` | Spreads travel destination searches over background work. |
| [`AiPlayerbot.GenerateTravelNodes`](#aiplayerbotgeneratetravelnodes) | `1` | Builds the bot travel network once, when its tables are empty. |
| [`AiPlayerbot.IterationsPerTick`](#aiplayerbotiterationspertick) | `100` | Action evaluations a bot may do per tick, per queued action. |
| [`AiPlayerbot.MaxWaitForMove`](#aiplayerbotmaxwaitformove) | `3000` | Milliseconds a move may take before it is given up. |
| [`AiPlayerbot.FailedActionRetryBase`](#aiplayerbotfailedactionretrybase) | `250` | First retry delay after an action fails, in milliseconds. |
| [`AiPlayerbot.FailedActionRetryMax`](#aiplayerbotfailedactionretrymax) | `2000` | Longest retry delay after repeated failures. |
| [`AiPlayerbot.FailedActionCacheTtl`](#aiplayerbotfailedactioncachettl) | `30000` | Milliseconds a failed-action record is kept. |
| [`AiPlayerbot.FailedActionCacheMaxEntries`](#aiplayerbotfailedactioncachemaxentries) | `64` | Failed actions remembered per bot. |
| [`AiPlayerbot.LogInGroupOnly`](#aiplayerbotlogingrouponly) | `1` | Restricts detailed per-action logging to grouped bots. |

### Core settings (`worldserver.conf`)

| Setting | Default | What it does |
| :--- | :---: | :--- |
| [`PlayerSaveInterval`](#playersaveinterval) | `90000` | How often characters are saved, in milliseconds. |
| [`GridUnload`](#gridunload--basemaploadallgrids) | `1` | Unload unused grids to save memory. |
| [`BaseMapLoadAllGrids`](#gridunload--basemaploadallgrids) | `0` | Load every grid of the base maps at startup. |
| [`Respawn.DynamicMode`](#respawn-dynamicmode) | `1` | Scale respawn timers with the number of players in the zone. |

## Bot scheduling and budgets

### `AiPlayerbot.Enabled`
Master switch for the bot engine.
**Default:** `1` · **Values:** `0` the engine does not start, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

With `0` the engine never starts. Bots still log in, but with no engine a bot that has a master only follows it on the module's simple follow/assist behaviour, and a masterless bot stands where it is. None of the AI settings on this page or in the other bot settings pages applies. The server log says the engine configuration was not loaded and why.

This is not the module's on/off switch; that is `TortoiseBots.Enable` in `tortoisebots.conf`.

</details>

### `AiPlayerbot.DisableActivityPriorities`
Turns activity throttling off.
**Default:** `1` · **Values:** `0` throttling on, `1` every bot fully active · **Restart:** needed

<details>
<summary>How it works in detail</summary>

- `1`: every bot's AI runs at full speed whether or not a human is nearby. Bots level, gather and fight on their own schedule.
- `0`: bots far from humans and not in combat run their AI less often. Bots with a real player master, in combat, in an instance, in a battleground, or visible to a player always run fully.

Every bot stays logged in either way; only the AI update rate changes. The project's default is full activity.

</details>

### `AiPlayerbot.botActiveAlone`
Percentage of lone bots kept fully active when throttling is on.
**Default:** `10` · **Values:** `0` to `100` percent · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Only used with `DisableActivityPriorities = 0`. It sets the share of bots that are alone (no player nearby, empty zone) allowed to run a full update; that subset rotates slowly, about 1 % per minute, so the work spreads across the pool over time.

</details>

### `AiPlayerbot.ForceActiveWhenNearPlayer`
Keeps bots a human can see at the full reaction rate.
**Default:** `0` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

A bot a player can see already runs its main AI at full rate. This flag additionally lets its reaction engine run fully while visible with throttling on. It does nothing while `DisableActivityPriorities = 1` (the default).

</details>

### `AiPlayerbot.PoolTickBudgetUs`
Microseconds of each world tick reserved for pool bots.
**Default:** `22000` · **Values:** microseconds (`1000` to `100000`, or `0`) · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Bots with a real player master and owned bots run first, every tick, and never count against this budget. Masterless pool bots share the rest of the pass under the combined budget (`PoolTickBudgetUs + CombatTickBudgetUs`, 37 ms at the defaults). The budget is checked before each bot, so a pass can overshoot by at most one bot; the bot the budget cut becomes the first of the next pass, so no bot waits two rounds.

Setting this to `0` switches the whole budget off. See also `TargetWorldTickMs` for the self-tuning controller that may lower the effective budget.

</details>

### `AiPlayerbot.CombatTickBudgetUs`
Microseconds added to the shared pool budget per world tick.
**Default:** `15000` · **Values:** microseconds (`1000` to `100000`, or `0`) · **Restart:** needed

<details>
<summary>How it works in detail</summary>

The second half of the combined pool budget. The two values are added; there is one budget and one rotation order, so this setting only makes the pool's share larger, not a separate combat queue. `0` switches the whole budget off, like `PoolTickBudgetUs = 0`.

</details>

### `AiPlayerbot.PoolBudgetWhenTickOverMs`
Only apply the budgets when the previous world tick was slower than this.
**Default:** `0` · **Values:** milliseconds (`0` = every tick) · **Restart:** needed

<details>
<summary>How it works in detail</summary>

- `0`: the budgets apply on every tick; the self-tuning controller gives unused budget back on healthy ticks.
- `> 0`: the budgets only engage when the previous tick took longer than this (for example `150`).

</details>

### `AiPlayerbot.TargetWorldTickMs`
The world tick length the self-tuning budget aims for.
**Default:** `100` · **Values:** milliseconds (`0` disables the controller) · **Restart:** needed

<details>
<summary>How it works in detail</summary>

The controller smooths the measured world tick over about eight ticks and moves the effective budgets:

- Over `TargetWorldTickMs + 10 ms`, and only when the pool's own turns are what keep the tick over the line, both budgets shrink to 70 % per tick. If the core and the other services alone keep the tick over, the budgets hold instead of starving bots.
- At or under the target, both budgets grow back by 1 ms per tick, up to the configured ceilings.
- Neither budget goes below 2 ms.

Setting this to `0` locks the budgets to their configured ceilings.

</details>

### `AiPlayerbot.ValueRefreshAtInterval`
Refreshes cached AI values at their full interval instead of half.
**Default:** `0` · **Values:** `0` half interval, `1` full interval · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Bots read the world through cached values. By default a value with a 2-second interval is refreshed every second. With this on, it refreshes every 2 seconds, which cuts recalculation work.

Values that sense the fight (attackers, aggro, possible targets, hazards, boss target, AoE heal, enemy players), the free-move values and the grind target keep the faster refresh whatever this says, because the bot's reactions depend on them.

</details>

### `AiPlayerbot.TravelPickBudgetUs`
Microseconds one bot may spend picking a travel destination per AI pass.
**Default:** `4000` · **Values:** microseconds (`0` = no limit) · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Long-distance travel is chosen by scoring candidate destinations. When the search has used this much time, it pauses and resumes on the bot's next pass, so one expensive pick cannot stall the world tick. `0` runs the whole search in one go.

</details>

### `AiPlayerbot.AsyncTravelPartitions`
Runs travel destination searches as background work.
**Default:** `0` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

With this on, the search over map partitions runs asynchronously instead of on the world thread. Off by default; the search is bounded by `TravelPickBudgetUs` either way.

</details>

### `AiPlayerbot.GenerateTravelNodes`
Builds the bot travel network once, when the travel tables are empty.
**Default:** `1` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

On a fresh database the travel-node tables are empty. With this on, the module builds the network once during server startup, before bots log in: nodes at start areas, flight masters, innkeepers, spirit healers, instance entrances, boats, zeppelins, zone centres and portals, linked by pathfinding over the extracted navigation mesh, with unreachable clusters pruned. The build takes minutes and logs progress; later starts load the saved network in seconds.

`0` skips the build: bots use the saved tables if they exist, or move directly when they are empty too.

</details>

### `AiPlayerbot.IterationsPerTick`
How many candidate actions the engine may evaluate per tick.
**Default:** `100` · **Values:** integer · **Restart:** needed

<details>
<summary>How it works in detail</summary>

The engine multiplies this by the number of queued actions and halves it for minimal moves. It stops a long chain of "action not applicable" evaluations from running away in one tick.

</details>

### `AiPlayerbot.MaxWaitForMove`
Milliseconds a movement may take before the bot gives up on it.
**Default:** `3000` · **Values:** milliseconds · **Restart:** needed

<details>
<summary>How it works in detail</summary>

If a move has not arrived within this time, the move action expires and the bot can pick another target or recalculate its path. It also caps how long the bot waits on a travel move.

</details>

### `AiPlayerbot.FailedActionRetryBase`
First retry delay after an action fails.
**Default:** `250` · **Values:** milliseconds (`0` to `2000`; `0` disables the back-off) · **Restart:** needed

<details>
<summary>How it works in detail</summary>

When a low-priority background action fails out of combat (an unreachable node, a vendor out of sight), the bot waits this long before trying again, then doubles the wait on each further failure. Combat, reaction triggers, explicit orders, packets and player-owned bots are never delayed.

</details>

### `AiPlayerbot.FailedActionRetryMax`
The longest such delay.
**Default:** `2000` · **Values:** milliseconds (`0` to `10000`, at least the base) · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Caps the doubling so a persistently failing action still gets retried now and then instead of burning CPU on every tick.

</details>

### `AiPlayerbot.FailedActionCacheTtl`
How long one failed-action record is kept.
**Default:** `30000` · **Values:** milliseconds (`1000` to `300000`) · **Restart:** needed

<details>
<summary>How it works in detail</summary>

After this time without a new failure the record is dropped, so the action starts from the base delay again.

</details>

### `AiPlayerbot.FailedActionCacheMaxEntries`
How many failed actions one bot remembers.
**Default:** `64` · **Values:** `1` to `256` (clamped) · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Bounds the memory the back-off takes per bot; the oldest entry is dropped when the cache is full.

</details>

### `AiPlayerbot.LogInGroupOnly`
Writes detailed action lines only for bots in a party.
**Default:** `1` · **Values:** `0` all bots, `1` grouped bots only · **Restart:** needed

<details>
<summary>How it works in detail</summary>

With this on, the engine's per-action detail log lines are written only for bots that are in a group; masterless pool bots are left out. This gates the engine's log output, not the per-bot action log files described in [Logging](logging.md).

</details>

### Commanding a bot party

There is no setting that turns this on or off. It is a GameMaster command surface: a GameMaster binds a party of up to five bots to a commander with `.tb master <bot|party> <commander>` and then gives orders with `.tb cmd` from anywhere: `move`, `walk`, `pull`, `pullback`, `attack`, `interrupt`, `focus`, `stay`, `follow`, `hold`, `loot`, `open`, `use`, `place`, `rally`, `fallback`, `resume`, `flee`, `guard` and the tactical toggles. The orders name their target by GUID or a 3D point, so the commander needs neither the group nor the bots in sight.

The Turtle-era keys for this feature, `AiPlayerbot.RemoteCommander` and `AiPlayerbot.RemoteCommanderViewMs`, do nothing here: the bindings do not need the first, and the perception view feed of the second is not ported. See [Commander](../../play/commands/commander.md).

## Core server settings (`worldserver.conf`)

The core's own configuration file. These few settings matter most on a bot-heavy realm; the file's comments describe each one fully.

### `PlayerSaveInterval`
How often characters are saved, in milliseconds.
**Default:** `90000` · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Saving hundreds of active bots every 90 seconds can produce write spikes. Raising the interval spreads the writes out; the trade-off is that a crash loses up to that much progress. The core saves a character on logout regardless.

</details>

### `GridUnload` / `BaseMapLoadAllGrids`
Whether unused grids are unloaded, and whether the base maps are loaded whole at startup.
**Defaults:** `GridUnload = 1`, `BaseMapLoadAllGrids = 0` · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Roaming bots move between grids all day. With `GridUnload = 1` the core frees a grid no one is in and loads it again on the next visit. On a server with enough RAM, `GridUnload = 0` plus `BaseMapLoadAllGrids = 1` keeps everything loaded: the core's own comment puts the cost at around 5 GB of memory and a longer startup, in exchange for no reload stalls when bots move around.

</details>

### `Respawn.DynamicMode`
Scales creature respawn timers with the number of players in the zone.
**Default:** `1` · **Restart:** needed

<details>
<summary>How it works in detail</summary>

On Forever this covers gathering nodes, quest targets under 30 minutes and the open-world combat creatures of the dynamic spawn groups. With a bot population, starting zones and popular camps are cleared quickly, and the dynamic scaling is what keeps them stocked. `0` turns it off; `Respawn.DynamicRateCreature` sets how many players are "normal" for the scaling.

</details>

## The BOTPERF line

While the module is enabled (`TortoiseBots.Enable = 1` in `tortoisebots.conf`), it writes one performance line to the `tortoisebots` log about every 30 seconds of world tick time:

```text
TortoiseBots: BOTPERF passUs=<us> maxUs=<us> ticks=<n> coreMs=<ms> bots=<n> engines=<n> owned=<n> mastered=<n> free=<n> pool=<n>
```

<details>
<summary>Field reference</summary>

- `passUs`: average cost of the module's world-tick pass, in microseconds.
- `maxUs`: the worst single pass in the window.
- `ticks`: world ticks in the window.
- `coreMs`: average world tick minus the bot pass: the share spent by the core and other services, which cutting bots cannot reduce.
- `bots`: bots in the module's table.
- `engines`: bots running the bot engine.
- `owned`: bots logged in for a player as their own character.
- `mastered`: bots currently following a master.
- `free`: masterless bots (the pool).
- `pool`: bots counted as the pool.

**Reading it:** a high `passUs` with a lot of `pool` bots means the module is doing a lot of AI work; a high `coreMs` means the bottleneck is elsewhere in the server. If bots feel slow, raise `PoolTickBudgetUs` first, or run fewer bots.

</details>
