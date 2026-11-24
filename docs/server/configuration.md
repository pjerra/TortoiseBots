---
id: server-configuration
title: How Configuration Works
category: server
summary: Configuration files, restart rules, recommended presets, settings index, and removed dead keys.
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

Bot settings live in two files created during installation:

1. `aiplayerbot.conf`: next to `mangosd.conf`. Gameplay flags, party behaviour, AI thresholds, autonomous services. Template: `ai/playerbot/aiplayerbot.conf.dist.in` for the Turtle core build, or the generated `forever/conf/aiplayerbot.conf.dist` for the ForeverCore build (each is installed as `aiplayerbot.conf.dist`). A custom path can be set with `AiPlayerbot.ConfigFile` in `mangosd.conf`. The [key census](config-keys.md) names every key the loader advertises and whether the Forever build consumes it; a key the Forever build does not consume is left out of the Forever template.
2. `modules/tortoise_bots.conf`: in the server configuration directory. Module-level options such as log verbosity. Template: `conf/tortoise_bots.conf.dist`.

## Install and update

The first install creates both files from their templates. Later updates never overwrite them. When updating, compare your files against the `.dist` templates to find new keys. A key missing from your file falls back to its built-in default. A line starting with `#` is a comment and sets nothing.

## Restart versus reload

Most settings load once at startup and need a full server restart. `.reload config` re-reads only these without a restart:

- Run leader keys: `AiPlayerbot.DungeonSelfClear`, the `AiPlayerbot.DungeonRun*` keys, and the `AiPlayerbot.BotLfg*` keys.
- Dashboard keys: `AiPlayerbot.ObservabilityRaids`, `AiPlayerbot.ObservabilityRealtime` and its sub-keys.
- `TortoiseBots.LogLevel` in `modules/tortoise_bots.conf`.

Everything else needs a restart. `.reload config` never starts a pool reset.

## Recommended presets

Out of the box (`AiPlayerbot.Enabled = 1`), a full living world runs 500 bots. Set it to `0` for a plain server with the module built in but inactive.

The block below gives a balanced living world for an average PC. It uses the shipped defaults except the fresh-bot level band: the default spreads new bots over levels 5-60, while 10-15 concentrates them in the early zones.

```ini
# Master bot toggle
AiPlayerbot.Enabled = 1

# Random bot population pool
AiPlayerbot.RandomBotAutologin = 1
AiPlayerbot.RandomBotAutoCreate = 1
AiPlayerbot.MinRandomBots = 500
AiPlayerbot.MaxRandomBots = 500

# Fresh-bot level seed (defaults are 5 and 60)
AiPlayerbot.RandomBotStartLevelMin = 10
AiPlayerbot.RandomBotStartLevelMax = 15

# Social immersion
AiPlayerbot.RandomBotInvitePlayer = 1
AiPlayerbot.RandomBotGroupNearby = 0
AiPlayerbot.RandomBotFormGuild = 0
AiPlayerbot.EnableGreet = 1

# Living economy and queues
AiPlayerbot.AhMarketEnabled = 1
AiPlayerbot.RandomBotLftEnabled = 1
AiPlayerbot.RandomBotBgEnabled = 1
```

## Settings index

Every setting is documented in a settings page:

| Settings Category | Configuration File | What It Covers |
| :--- | :--- | :--- |
| [Party & quality-of-life](settings/party.md) | `aiplayerbot.conf` | Owned bot management, alt-bots, follow catching up, pack holds, and party loot. |
| [Mercenary hiring & recruiters](settings/hiring.md) | `aiplayerbot.conf` | Companion hiring costs, raid composition presets, gear tiers, and world buffs. |
| [Class behaviour](settings/class-behaviour.md) | `aiplayerbot.conf` | Class-specific AI rules for hunters, paladins, rogues, and warlocks. |
| [Dungeons & raids](settings/dungeons-raids.md) | `aiplayerbot.conf` | Creature aura hazard avoidance, bomb dispersal, hazard evasion, and custom raid tactics. |
| [Combat & reaction thresholds](settings/combat.md) | `aiplayerbot.conf` | Health and mana lines, follow distances, casting pacing, and commanded pulls. |
| [World population & immersion](settings/world-population.md) | `aiplayerbot.conf` | Pool sizing, level ladders, wander limits, greetings, guilds, and gear upkeep. |
| [Economy & auction house](settings/economy.md) | `aiplayerbot.conf` | AHBot stock, organic auction house market, bidding, buyouts, and trade limits. |
| [Dungeon finder & battlegrounds](settings/queues.md) | `aiplayerbot.conf` | Looking for Turtles (LFT) autofill and battleground queues. |
| [Performance & tick budget](settings/performance.md) | `aiplayerbot.conf` + `mangosd.conf` | Per-tick AI turn limits, self-tuning tick controllers, and core map caching. |
| [Diagnostic logging](settings/logging.md) | `aiplayerbot.conf` + `tortoise_bots.conf` | Module log levels, telemetry streams, CSV logs, and per-bot action log rotation. |

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

## Removed keys

`aiplayerbot.conf.dist.in` once carried keys that were read into configuration and never used; they are gone now. An older `aiplayerbot.conf` that still sets one keeps loading, because the config reader ignores unknown keys. Nothing these keys appeared to control ever changed, so there is nothing to migrate: delete the lines. One pair that now has an effect is `RandomBotShowHelmet` / `RandomBotShowCloak`: they hide the helmet and cloak of pool bots (default: shown).

Raid diagnostics stay opt-in through
[`AiPlayerbot.RaidBossDiagnostics`](settings/logging.md#aiplayerbotraidbossdiagnostics)
(default `0`); event CSV files alone do not enable boss observations.

## ForeverCore module keys (`tortoisebots.conf`)

| Key | Default | Meaning |
| :--- | :--- | :--- |
| `TortoiseBots.AutoLearnOnLevel` | `1` | Bots learn class trainer and skill-line spells for free on login and level-up. |
| `TortoiseBots.AutoTalentsOnLevel` | `1` | Bots spend free talent points on login and level-up along a spec build (`.tb spec` wins); spent points are never reset. `.tb talents <bot>` shows and applies. The builds (`forever/data/waterys/talent-builds.csv`, 51 points per spec) follow the class trait trees the server validates; `python3 forever/tools/talent_builds.py --check` checks them. |
| `TortoiseBots.AutoProfessions` | `1` | Pool bots reaching level 5 learn two primary professions that fit together, plus First Aid and Cooking, for free. |
| `TortoiseBots.AutoTools` | `1` | Pool and free bots get the missing tool of a profession they know (pick, knife, hammer, rod, spanner, fishing pole) into their bags. |
| `TortoiseBots.AutoHunterPet` | `1` | Hunter bots at level 10 without a pet get their race's starter pet and the pet-control spells; an existing pet is never replaced. |
| `TortoiseBots.AutoFood` | `1` | Pool and free bots carry level-fitting food (and water for mana users), topped up when they run low. |
| `TortoiseBots.World.Fishing` | `1` | Free bots fish an occasional session at a fishing hole or nearby open water: one session per hour, at most 5 casts or 5 minutes. An unskilled bot is taught the secondary skill and given a plain pole; the catch goes into its own bags. |
| `TortoiseBots.Pool.Reset` | `off` | Rebuild the pool on world startup (`off`, `once:<token>` or `always`). Startup only; `.reload config` never starts it. Deletes only `ready` pool rows, keeps other characters on pool accounts, and refuses on a pool-led guild with an outsider, an owned auction listing or a player session. See [Managing & Resetting the Bot Pool](bot-pool.md). |
| `TortoiseBots.World.Pickpocket` | `1` | Free and pool rogue bots that know Stealth and Pick Pocket pick the pockets of a nearby humanoid that still has pickpocket loot, then take the coins and items. A creature is not retried before its own refill timer; bots without the two spells are skipped. |
| `TortoiseBots.World.Lockboxes` | `1` | Free and pool rogue bots with enough Lockpicking open the lockboxes in their own bags and take the loot. |
| `TortoiseBots.World.LockboxMail` | `1` | A bot holding a lockbox it cannot open mails it to the best rogue bot in its own guild (online or offline), which opens it and mails the contents, coins minus the return postage, back to the sender. Only bots are served; a real player's mail is never taken. |
| `TortoiseBots.World.Disenchant` | `1` | Free bots with Enchanting break down disenchantable armor and weapons they carry (uncommon and above; rare and better only when no real player shares their guild) and keep the materials. |
| `TortoiseBots.World.EnchantGear` | `1` | Free bots with Enchanting apply the best permanent enchant they know and can pay for to their worn gear, when it scores higher than the enchant already there. |
