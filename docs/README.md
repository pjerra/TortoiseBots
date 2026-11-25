---
id: docs-index
title: TortoiseBots Documentation
category: reference
summary: Start page, role-based navigation, task finder, and audience directory for the ForeverCore documentation catalog.
tags: [index, start, navigation, directory]
relates_to:
  - play-getting-started
  - play-features
  - play-commands
  - play-known-limitations
  - server-install
---

# TortoiseBots Documentation

Welcome to the TortoiseBots documentation catalog for **ForeverCore**. This knowledge base covers player commands, class mechanics, the living world, server configuration and developer architecture.

## Start Here

- [Getting started](play/getting-started.md) — install the addon, log your own characters in as bots, and build your party.
- [What bots can do](play/features.md) — catalog of what the Forever build does, with its switches and defaults.
- [Commands](play/commands/README.md) — the player command directory.
- [Known limitations](play/known-limitations.md) — what is not built yet, deliberate gaps, and timers.
- [Glossary](play/glossary.md) — what the abbreviations and bot terms mean.
- [Install (for admins)](server/install.md) — build and configure TortoiseBots for ForeverCore.

## I Want To...

| I want to... | Where to go |
| :--- | :--- |
| Log my own alt in as a bot and summon it | [Getting started](play/getting-started.md) |
| See every bot feature and switch in one place | [What bots can do](play/features.md) |
| Pull a pack around a corner | [Pullback](play/commands/party-actions.md#addon-action-buttons) |
| Stop bots from breaking crowd control | [Toggles](play/commands/party-actions.md#toggles) |
| Direct my party to interrupt a caster | [Interrupt](play/commands/party-actions.md#addon-action-buttons) |
| Inspect a bot's stats | [Bot panel](play/commands/bot-panel.md#inspection) |
| Read the kill orders and mechanics of a 5-man dungeon | [Dungeon reference](play/dungeon-reference.md) (Turtle data: layouts only, not Forever IDs) |
| Handle a wipe and get back to the instance | [Dead bots and wipes](play/dungeons-and-raids.md#dead-bots) |
| Fill a missing tank or healer role with free bots | [Dungeon fill](play/commands/roster-and-hiring.md) and [Queues](world/queues.md) |
| Send a bot in my guild to farm, buy or craft for the bank | [Guild orders](play/guild-orders.md) |
| Join a bot-run guild by whispering one of its bots | [Social](world/social.md#guilds) |
| See how bots use the auction house | [Living AH](world/economy.md#living-auction-house) |
| Summon a lost or stuck companion | [Summon](play/commands/roster-and-hiring.md#summon) |
| Understand the class AI | [Class overview](classes/overview.md) |
| Watch the bot fleet on a live web map | [Observability dashboard](server/dashboard.md) |
| Create, start or reset the bot pool | [Bot pool](server/bot-pool.md) |
| Trace a party's dungeon run | [Run trace](server/run-trace.md) |
| Build the module from source | [Server install](server/install.md) |
| Port a feature or fix a bug | [Developer index](developers/README.md) |

## Players

| Page | What you find there |
| :--- | :--- |
| [Getting started](play/getting-started.md) | Client setup, `/tbfm`, your own alts, first party, free bots. |
| [What bots can do](play/features.md) | The feature catalog, default states and setting names per area, plus what is not on Forever. |
| [Command directory](play/commands/README.md) | All player commands, permissions and channel syntax. |
| [Party actions](play/commands/party-actions.md) | Combat intents: attack, pull, pullback, interrupt, CC clear, toggles and object tasks. |
| [Roster and character commands](play/commands/roster-and-hiring.md) | Adding and logging out your alts, summoning, maintenance and gearing. |
| [Bot panel](play/commands/bot-panel.md) | Inspection, gear, inventory and behaviour toggles. |
| [Whisper commands](play/commands/whispers.md) | One-to-one bot orders grouped by combat, rest, trade and town. |
| [Addon (TortoiseBotsForeverManager)](play/commands/addon.md) | The `/tbfm` UI and the `TBM:` message protocol. |
| [Line of sight query](play/commands/los-query.md) | Ask the server whether two units can see each other, with the distance: `.tb los`. |
| [Guild orders](play/guild-orders.md) | Sending a bot in your guild to farm, buy or craft; order states and delivery. |
| [Dungeons and raids](play/dungeons-and-raids.md) | Pulling, CC, interrupts, hazards, wipes, loot and the GameMaster run leader. |
| [Dungeon reference](play/dungeon-reference.md) | Creature IDs and events of Turtle WoW 5-mans; layouts only for Forever. |
| [Run leader](play/dungeon-run-leader.md) | `.tb run` and the plan tables, for server admins. |
| [Boss tactics](play/boss-tactics.md) | The Turtle-era per-boss table; not applied on Forever. |
| [Resist gear](play/resist-gear.md) | The raid-boss resistance swap, its settings, read-outs and crash-safe store. |
| [Known limitations](play/known-limitations.md) | Missing features, the unported list, timers and workarounds. |
| [Glossary](play/glossary.md) | Abbreviations and TortoiseBots terms. |
| [Classes overview](classes/overview.md) | Shared class AI, spec detection, roles and interrupts. |
| [Warrior](classes/warrior.md), [Paladin](classes/paladin.md), [Hunter](classes/hunter.md), [Rogue](classes/rogue.md), [Priest](classes/priest.md), [Shaman](classes/shaman.md), [Mage](classes/mage.md), [Warlock](classes/warlock.md), [Druid](classes/druid.md) | Per-class rotations and utility. |

## What Bots Do on Their Own

| Page | What you find there |
| :--- | :--- |
| [Living world](world/living-world.md) | Questing, grinding, gear upkeep, professions and travel. |
| [Social](world/social.md) | Invites, guild participation, greetings and trading. |
| [Queues and matchmaking](world/queues.md) | The dungeon fill and WSG/AB battlegrounds. |
| [Economy](world/economy.md) | Vendors, repairs, the auction market and gold flow. |

## Server Admins

| Page | What you find there |
| :--- | :--- |
| [Installation](server/install.md) | CMake build for ForeverCore, config and SQL delivery, version checks. |
| [Configuration](server/configuration.md) | Config files, templates, restart and reload rules, presets. |
| [Admin commands](server/admin-commands.md) | `.tb` commands for the pool, fill, battlegrounds, guild orders, runs and diagnostics. |
| [Bot pool](server/bot-pool.md) | Registry, scheduler, pruning and the startup reset. |
| [Observability dashboard](server/dashboard.md) | UDP telemetry, the web dashboard, metrics and APIs. |
| [Run trace](server/run-trace.md) | The `.tb trace` recorder, the `bt-trace` schema and the offline scorer. |
| [Decision capture](server/decision-capture.md) | The run-leader decision recorder and the replay fixture workflow. |
| [Party settings](server/settings/party.md) | Follow distances, summoning, formations and loot. |
| [Class behavior settings](server/settings/class-behaviour.md) | AI flags for Aspect of the Viper, Judgements, threat drops and demons. |
| [Dungeon and raid settings](server/settings/dungeons-raids.md) | Encounter flags, hazard ranges and aura avoidance. |
| [Combat settings](server/settings/combat.md) | Health and mana thresholds, ranges, GCD delays and pull pacing. |
| [World population settings](server/settings/world-population.md) | Pool sizes, level seeding, wandering, greetings and trade. |
| [Economy settings](server/settings/economy.md) | Auction market pacing, supply and vendor batches. |
| [Guild order settings](server/settings/guild-orders.md) | Bank withdrawals, gold and sub-orders. |
| [Queue settings](server/settings/queues.md) | Dungeon-fill intervals and battleground parameters. |
| [Performance settings](server/settings/performance.md) | Tick budgets, activity and core tuning. |
| [Logging settings](server/settings/logging.md) | Module log level, action logs, traces and telemetry. |
| [Hire gear sets](reference/hire-gear.md) | Generated level-60 hire sets from the Turtle-era generator; the Forever build has no hire system. |

## Developers

| Page | What you find there |
| :--- | :--- |
| [Developer index](developers/README.md) | Contributor workflows, coding standards, tools and tests. |
| [Architecture invariants](developers/architecture.md) | Modularity, headless sessions and the core boundary. |
| [Guild supply chain](developers/guild-supply.md) | Supply ladder, bank withdrawals and sub-orders. |
| [Strategy engine](developers/strategy-engine.md) | AI update loop, action baskets, triggers and multipliers. |
| [Bot mechanics](developers/bot-mechanics.md) | Targeting, threat, movement, interrupts and resting. |
| [Donor hierarchy](developers/donor-hierarchy.md) | Provenance precedence across donors. |
| [Host API contract](developers/host-api.md) | Host seams, session hooks and packet bridges. |
| [Licensing audit](developers/licensing-audit.md) | AGPL/GPL lineage and release gates. |
| [Data tables](developers/data/README.md) | Capabilities, donor map, spell coverage and talent builds. |
