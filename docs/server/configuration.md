---
id: server-configuration
title: How Configuration Works
category: server
summary: Configuration files, restart rules, a starting preset, the settings index, and keys the Forever build does not read.
tags: [config, server, administration]
relates_to:
  - server-settings-party
  - server-settings-hiring
  - server-settings-class-behaviour
  - server-settings-dungeons-raids
  - server-settings-combat
  - server-settings-world-population
  - server-settings-economy
  - server-settings-queues
  - server-settings-performance
  - server-settings-logging
---

# How Configuration Works

Bot settings live in two files:

1. `aiplayerbot.conf`: in the folder that holds your `worldserver.conf`. Gameplay flags, party behaviour, AI thresholds and the Auction House market. Template: `forever/conf/aiplayerbot.conf.dist`, generated from the code by `forever/tools/gen_aiplayerbot_conf.py`. The server reads it itself; a custom path can be set with `AiPlayerbot.ConfigFile` in `worldserver.conf`. The [key census](config-keys.md) names every key the loader advertises and whether the Forever build consumes it.
2. `worldserver.conf.d/tortoisebots.conf`: module-level options: the master switch, the pool, world services, automatic training, tools and food, the dashboard. Template: `forever/conf/tortoisebots.conf.dist`. The build installs that template into `worldserver.conf.d/`; rename it to `tortoisebots.conf` (the name the deployment uses) so the core reads it.

## Install and update

The `.dist` templates are not your live files. Create `aiplayerbot.conf` next to `worldserver.conf` and `worldserver.conf.d/tortoisebots.conf` from their templates; later updates never overwrite them. When updating, compare your files against the templates to find new keys. A key missing from your file falls back to its built-in default. A line starting with `#` is a comment and sets nothing.

## Restart versus reload

Most settings load once at startup and need a full server restart. `.reload config` re-reads the module file and the keys read on demand:

- Every `TortoiseBots.*` key read at config load: `TortoiseBots.Enable`, the pool keys, the world services, the automatic training/tools/food keys, the dashboard, the command bindings.
- Keys read straight from the core config on use, such as `TortoiseBots.World.Auction` and `TortoiseBots.World.GuildOrderBotRank`.
- **Not** the keys in `aiplayerbot.conf`: the engine reads that file once and keeps it. Change one and restart.

`.reload config` never starts a pool reset; `TortoiseBots.Pool.Reset` is read at startup only.

## Starting preset

The Forever build is off until `TortoiseBots.Enable` is on.

```ini
# worldserver.conf.d/tortoisebots.conf
TortoiseBots.Enable = 1
TortoiseBots.Pool.Enable = 1
TortoiseBots.Pool.MaxCharacters = 50
TortoiseBots.Pool.TargetOnline = 20
TortoiseBots.World.Guilds = 1
TortoiseBots.World.Auction = 1

# aiplayerbot.conf
AiPlayerbot.AhMarketEnabled = 1
AiPlayerbot.AhMarketBuyer = 1
```

A small pool is the lightest way to a living world: create the characters once with `.tb pool create 50`, then `.tb pool start` keeps the online target. Bots level from play.

## Settings index

Every setting is documented in a settings page:

| Settings Category | Configuration File | What It Covers |
| :--- | :--- | :--- |
| [Party & quality-of-life](settings/party.md) | `aiplayerbot.conf` | Owned bot management, follow catching up, pack holds, and party loot. |
| [Mercenary hiring & recruiters](settings/hiring.md) | `aiplayerbot.conf` | Not on ForeverCore yet: no bot can be hired, so these settings change nothing. |
| [Class behaviour](settings/class-behaviour.md) | `aiplayerbot.conf` | Class-specific AI rules for hunters, paladins, rogues, warlocks, and warriors. |
| [Dungeons & raids](settings/dungeons-raids.md) | `aiplayerbot.conf` | Creature aura hazards, crowd control, bomb runout, raid reactions, and the fight monitor. |
| [Combat & reaction thresholds](settings/combat.md) | `aiplayerbot.conf` | Health and mana lines, follow distances, casting pacing, and commanded pulls. |
| [World population & immersion](settings/world-population.md) | `aiplayerbot.conf` + `tortoisebots.conf` | Pool sizing, wander limits, greetings, guilds, and gear upkeep. |
| [Economy & auction house](settings/economy.md) | `aiplayerbot.conf` | The bot-run Auction House market, vendor trips, mounts and gear upkeep. |
| [Guild orders](settings/guild-orders.md) | `tortoisebots.conf` | The shared bot rank and guild bank access. |
| [Dungeon finder & battlegrounds](settings/queues.md) | `aiplayerbot.conf` + `tortoisebots.conf` | Dungeon-party fill (`.bot lfg`) and battleground queues. |
| [Performance & tick budget](settings/performance.md) | `aiplayerbot.conf` + `worldserver.conf` | Per-tick AI turn limits and self-tuning tick controllers. |
| [Diagnostic logging](settings/logging.md) | `aiplayerbot.conf` + `tortoisebots.conf` | Module log levels, telemetry streams, CSV logs, and per-bot action log rotation. |

## Decision capture

Off by default and meant for test servers: it writes every tick of a running run leader, roughly 12-40 KB a second. What it records, how the files rotate and how a live failure becomes a replay fixture: [Decision capture](decision-capture.md).

| Key | Default | Meaning |
| :--- | :--- | :--- |
| `AiPlayerbot.DecisionCapture.Enabled` | `0` | Capture the run leader's ticks. |
| `AiPlayerbot.DecisionCapture.Dir` | `decisions` | Output folder, relative to `LogsDir` unless it is an absolute path. |
| `AiPlayerbot.DecisionCapture.SegmentMiB` | `32` | A segment rotates past this many MiB (1..256). |
| `AiPlayerbot.DecisionCapture.CaptureMiB` | `512` | Per capture: segments past this many MiB go oldest first, never the open one. |
| `AiPlayerbot.DecisionCapture.TotalMiB` | `4096` | Per folder: the oldest ended captures go past this many MiB, never a running one. |
| `AiPlayerbot.DecisionCapture.RecordKiB` | `512` | A larger record becomes a stub (64..4096). |
| `AiPlayerbot.DecisionCapture.KeyframeEvery` | `60` | A full keyframe every this many records (1..600). |

## Removed and dead keys

An old `aiplayerbot.conf` that still sets a key the Forever build does not read keeps loading, because the config reader ignores unknown keys. Nothing those keys appeared to control ever changes on this build; delete the lines. The [key census](config-keys.md) marks every such key.

Raid diagnostics stay opt-in through
[`AiPlayerbot.RaidBossDiagnostics`](settings/logging.md#aiplayerbotraidbossdiagnostics)
(default `0`); event CSV files alone do not enable boss observations.

## ForeverCore module keys (`tortoisebots.conf`)

| Key | Default | Meaning |
| :--- | :--- | :--- |
| `TortoiseBots.Enable` | `0` | Master switch. With it off the module loads and answers nothing; free bots and host services stay dormant. |
| `TortoiseBots.AutoLearnOnLevel` | `1` | Bots learn class trainer and skill-line spells for free on login and level-up. |
| `TortoiseBots.AutoTalentsOnLevel` | `1` | Bots spend free talent points on login and level-up along a spec build (`.tb spec` wins); spent points are never reset. `.tb talents <bot>` shows and applies. The builds (`forever/data/waterys/talent-builds.csv`, 51 points per spec) follow the class trait trees the server validates; `python3 forever/tools/talent_builds.py --check` checks them. |
| `TortoiseBots.AutoProfessions` | `1` | Pool bots reaching level 5 learn two primary professions that fit together, plus First Aid and Cooking, for free. |
| `TortoiseBots.AutoTools` | `1` | Pool and free bots get the missing tool of a profession they know (pick, knife, hammer, rod, spanner, fishing pole) into their bags. |
| `TortoiseBots.AutoHunterPet` | `1` | Hunter bots at level 10 without a pet get their race's starter pet and the pet-control spells; an existing pet is never replaced. |
| `TortoiseBots.AutoFood` | `1` | Pool and free bots carry level-fitting food (and water for mana users), topped up when they run low. |
| `TortoiseBots.World.Auction` | `0` | Lets bots use the Auction House: posting, buying and the bot-run market. Off, the market sweep and the buyer do nothing. |
| `TortoiseBots.World.Fishing` | `1` | Free bots fish an occasional session at a fishing hole or nearby open water: one session per hour, at most 5 casts or 5 minutes. An unskilled bot is taught the secondary skill and given a plain pole; the catch goes into its own bags. |
| `TortoiseBots.World.GuildOrderBotRank` | `1` | Bots in a guild with a real player share one rank for guild bank access; open the bank tabs to that rank. |
| `TortoiseBots.Pool.Reset` | `off` | Rebuild the pool on world startup (`off`, `once:<token>` or `always`). Startup only; `.reload config` never starts it. Deletes only `ready` pool rows, keeps other characters on pool accounts, and refuses on a pool-led guild with an outsider, an owned auction listing or a player session. See [Managing & Resetting the Bot Pool](bot-pool.md). |
| `TortoiseBots.World.Pickpocket` | `1` | Free and pool rogue bots that know Stealth and Pick Pocket pick the pockets of a nearby humanoid that still has pickpocket loot, then take the coins and items. A creature is not retried before its own refill timer; bots without the two spells are skipped. |
| `TortoiseBots.World.Lockboxes` | `1` | Free and pool rogue bots with enough Lockpicking open the lockboxes in their own bags and take the loot. |
| `TortoiseBots.World.LockboxMail` | `1` | A bot holding a lockbox it cannot open mails it to the best rogue bot in its own guild (online or offline), which opens it and mails the contents, coins minus the return postage, back to the sender. Only bots are served; a real player's mail is never taken. |
| `TortoiseBots.World.Disenchant` | `1` | Free bots with Enchanting break down disenchantable armor and weapons they carry (uncommon and above; rare and better only when no real player shares their guild) and keep the materials. |
| `TortoiseBots.World.EnchantGear` | `1` | Free bots with Enchanting apply the best permanent enchant they know and can pay for to their worn gear, when it scores higher than the enchant already there. |
| `TortoiseBots.Player.AddonHint` | `0` | Tell a real player whose client sent no add-on message in that session where to get TortoiseBotsForeverManager, once per character. Off by default; a server turns it on with `1`. GameMasters and higher security levels never get the line. The flag is a row in the characters table `tortoise_bots_player_flag`; `.tb addonhint reset <character>` clears it. Read at world startup. |
| `TortoiseBots.Player.AddonHintDelay` | `45` | Seconds between entering the world and that line; a value outside `10`..`300` is clamped. Read at world startup. |
| `TortoiseBots.Player.AddonHintUrl` | `https://github.com/TortoiseBots/TortoiseBotsForeverManager/releases` | The download page the line names. Empty drops the `Get it at ...` part and keeps the rest of the sentence. Read at world startup. |
