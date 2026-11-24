---
id: server-settings-queues
title: Queue settings (dungeon fill & battlegrounds)
category: server
summary: Settings for the module's dungeon-party fill and the Warsong Gulch / Arathi Basin bot round.
tags: [config, queues, dungeon, battlegrounds, pvp]
relates_to:
  - world-queues
  - server-settings-performance
  - server-configuration
---

# Queue Settings (Dungeon Fill & Battlegrounds)

These settings configure the bot fill for a player's dungeon party and the battleground round that queues bots into Warsong Gulch and Arathi Basin. The first four live in `aiplayerbot.conf`, the rest in `tortoisebots.conf`; see [How configuration works](../configuration.md).

For what the bots do, see [Dungeon & battleground queues](../../world/queues.md).

## Summary

| Setting | File | Default | What it does |
| :--- | :--- | :---: | :--- |
| [`AiPlayerbot.RandomBotLftEnabled`](#aiplayerbotrandombotlftenabled) | `aiplayerbot.conf` | `1` | Enables the dungeon-party fill. |
| [`AiPlayerbot.RandomBotLftUpdateInterval`](#aiplayerbotrandombotlftupdateinterval) | `aiplayerbot.conf` | `15000` | Milliseconds between fill passes. |
| [`AiPlayerbot.RandomBotLftMaxFillsPerInterval`](#aiplayerbotrandombotlftmaxfillsperinterval) | `aiplayerbot.conf` | `1` | Most bots queued into a party per pass. |
| [`AiPlayerbot.RandomBotLftAllowRoleBorrow`](#aiplayerbotrandombotlftallowroleborrow) | `aiplayerbot.conf` | `0` | Lets the fill pull a bot into a role its own spec does not cover. |
| [`TortoiseBots.Bg.Enable`](#tortoisebotsbgenable) | `tortoisebots.conf` | `0` | Turns the battleground round on. |
| [`TortoiseBots.Bg.Maps`](#tortoisebotsbgmaps) | `tortoisebots.conf` | `"wsg,ab"` | Which maps the round plays. |
| [`TortoiseBots.Bg.FillHumanQueues`](#tortoisebotsbgfillhumanqueues) | `tortoisebots.conf` | `1` | Queues bots into a real player's WSG/AB queue. |
| [`TortoiseBots.Bg.MinPlayersPerTeam`](#tortoisebotsbgminplayersperteam) | `tortoisebots.conf` | `5` | The fill's target per side (it queues one more). |
| [`TortoiseBots.Bg.MaxInstancesWithBots`](#tortoisebotsbgmaxinstanceswithbots) | `tortoisebots.conf` | `2` | Instances that may hold bots at once. |
| [`TortoiseBots.Bg.MaxBots`](#tortoisebotsbgmaxbots) | `tortoisebots.conf` | `50` | Bots held for battlegrounds server-wide. |
| [`TortoiseBots.Bg.MaxMinutes`](#tortoisebotsbgmaxminutes) | `tortoisebots.conf` | `45` | Longest stay of one bot in one instance. |
| [`TortoiseBots.Bg.Autonomous`](#tortoisebotsbgautonomous) | `tortoisebots.conf` | `0` | Whether bot-only matches may start. Keep it `0`. |

## Dungeon fill

### `AiPlayerbot.RandomBotLftEnabled`
Enables the dungeon-party fill.
**Default:** `1` · **Values:** `0` off, `1` on · **Restart:** needed

The fill only answers a player who queues with `.bot lfg join`; it never queues a bot on its own. With the key at `0` the queue refuses joins and no bot is grouped.

### `AiPlayerbot.RandomBotLftUpdateInterval`
Milliseconds between fill passes.
**Default:** `15000` · **Values:** 5000 to 60000 (a value outside is clamped) · **Restart:** needed

Each pass tops up every waiting player by at most the per-pass budget below. The pass also runs at once when a player queues.

### `AiPlayerbot.RandomBotLftMaxFillsPerInterval`
Most bots the fill groups per pass.
**Default:** `1` · **Values:** 0 to 5 (a larger value is read as 5) · **Restart:** needed

`0` keeps the queue running but groups nobody.

### `AiPlayerbot.RandomBotLftAllowRoleBorrow`
Lets the fill pull a bot into a role its own spec does not cover.
**Default:** `0` · **Values:** `0` strict, `1` allow borrowing · **Restart:** needed

At `0` a bot fills only a slot its spec covers. At `1` a missing tank may be a warrior, protection paladin or druid, and a missing healer a paladin, priest, shaman or druid; the bot is rebuilt into that role's kit and gets its own kit back when the party ends. Only the bot's own spec is a natural fit either way.

## Battlegrounds

### `TortoiseBots.Bg.Enable`
Turns the battleground round on.
**Default:** `0` · **Values:** `0` off, `1` on · **Restart:** not needed (`.reload config` re-reads it)

With it off no bot queues for a battleground, no invitation is answered and nothing is cleaned out of a queue. Your own party bots still queue and enter with you through the battlemaster's Join as Group, because the core queues them for you.

### `TortoiseBots.Bg.Maps`
Which battlegrounds the round plays.
**Default:** `"wsg,ab"` · **Values:** a comma-separated list of `wsg`, `ab` · **Restart:** not needed (`.reload config` re-reads it)

Warsong Gulch and Arathi Basin are the only supported maps; any other word is refused with a warning. A map whose start data is missing at boot is turned off with one error line.

### `TortoiseBots.Bg.FillHumanQueues`
Queues bots into a real player's WSG/AB queue.
**Default:** `1` · **Values:** `0` off, `1` on · **Restart:** not needed (`.reload config` re-reads it)

While a player waits, both sides are filled toward the per-side target, at most five bots per side per five-second check. With it off the round only tracks and cleans up queue entries it already made.

### `TortoiseBots.Bg.MinPlayersPerTeam`
The fill's own minimum per side.
**Default:** `5` · **Values:** 0 to 40 · **Restart:** not needed (`.reload config` re-reads it)

The fill aims for this number plus one bot per side, and never more than the map's team size. The extra bot covers one that drops out between checks. At `0` the target is the map's own minimum.

### `TortoiseBots.Bg.MaxInstancesWithBots`
How many battleground instances may hold bots at once.
**Default:** `2` · **Values:** 0 to 50 · **Restart:** not needed (`.reload config` re-reads it)

A bot is not queued while the cap is reached. `0` turns the fill off.

### `TortoiseBots.Bg.MaxBots`
How many bots the module holds for battlegrounds server-wide.
**Default:** `50` · **Values:** 0 to 2000 · **Restart:** not needed (`.reload config` re-reads it)

Queued, invited and inside bots all count. `0` turns the fill off.

### `TortoiseBots.Bg.MaxMinutes`
Longest stay of one bot in one instance.
**Default:** `45` · **Values:** 1 to 240 · **Restart:** not needed (`.reload config` re-reads it)

At the limit the bot leaves without the Deserter debuff. A bot that entered with its own player leaves with that player instead.

### `TortoiseBots.Bg.Autonomous`
Whether bot-only matches may start.
**Default:** `0` · **Values:** `0` off, `1` on · **Restart:** not needed (`.reload config` re-reads it)

Keep it `0`: this round has no seeder that would keep a bot-only queue fed, so the value only decides whether the core may start an instance nobody but bots waits for.

## Keys that do nothing on the Forever build

<a id="aiplayerbotrandombotbgenabled"></a>
<a id="aiplayerbotrandombotbgautonomous"></a>
<a id="aiplayerbotwsgflagroles"></a>
These keys appear in the Turtle version's config, and the Forever template does not ship them because no part of the build reads their values. Setting one changes nothing; the rounds above use the module keys instead. The generated [key census](../config-keys.md) lists each one with where it is read.

| Key | What it was for |
| :--- | :--- |
| `AiPlayerbot.RandomBotBgEnabled` | The old battleground service switch. Use `TortoiseBots.Bg.Enable`. |
| `AiPlayerbot.RandomBotBgAutonomous` | Bot-only match seeding. No seeder exists here. |
| `AiPlayerbot.RandomBotBgAutonomousMaxInstances` | Cap of the old autonomous seeder. |
| `AiPlayerbot.RandomBotBgQueueInterval` | Cadence of the old autonomous seeder. |
| `AiPlayerbot.RandomBotBgMaxQueuePerInterval` | Budget of the old autonomous seeder. |
| `AiPlayerbot.WsgFlagRoles` | The old split of a Warsong Gulch team into attackers, defenders and midfielders. No battleground objective rows exist here. |
| `AiPlayerbot.HireAllowPvP` | The old switch for hired companions in battlegrounds. No bot is hired on this build, so it changes nothing. |
