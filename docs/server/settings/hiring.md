---
id: server-settings-hiring
title: Mercenary Hiring & Recruiter Settings
category: server
summary: Mercenary hiring, hire prices, gear tiers and recruiter world buffs are not part of the ForeverCore build; bots come from the bot pool or from a player's own characters.
tags: [config, hiring, mercenaries, world-buffs, not-on-forever]
relates_to:
  - server-bot-pool
  - play-commands-roster-and-hiring
  - server-settings-party
---

# Mercenary Hiring & Recruiter Settings

No bot can be hired on ForeverCore. The hire flow behind the mercenary recruiters is not part of this build, no companion has a hire fee, and no recruiter sells a world buff.

**Not on ForeverCore yet.**

The settings that would configure hiring and recruiter buffs (`AiPlayerbot.Hire*` and `AiPlayerbot.WorldBuffs*`) still exist in `aiplayerbot.conf` for compatibility with the Turtle WoW version; a few are not even listed in the shipped file and only work as built-in defaults. On ForeverCore nothing hires, prices or sells a companion, so none of them has a visible effect. Leave them alone.

## How players get bots instead

| What you want | How it works on ForeverCore |
| :--- | :--- |
| Your own characters as party members | `.bot add <name>` logs one of your characters in, and it joins your party. See [Roster and character commands](../../play/commands/roster-and-hiring.md). |
| A population of bot characters | A server admin creates and starts the bot pool from the console: `.tb pool create`, `.tb pool start`. See [The bot pool](../bot-pool.md). |
| Giving a bot an order | `.bot follow`, `.bot stay`, `.bot attack`, `.bot flee` and `.bot summon` for your own bots; a GameMaster can also bind a bot to a commander and use `.tb order`. See [Party actions](../../play/commands/party-actions.md) and [Commander](../../play/commands/commander.md). |

## Settings that do nothing here

The `AiPlayerbot.Hire*` and `AiPlayerbot.WorldBuffs*` settings still exist in `aiplayerbot.conf`. On ForeverCore they are written into memory, but nothing hires and no recruiter can do anything, so none of them has a visible effect:

- hire enable/limits and hire fees (`HireEnabled`, `HireMaxBotsPerPlayer`, `HireBaseCostCopper`, the party multipliers, the raid flat cost);
- hire location and PvP rules (`HireAnywhere`, `HireAllowPvP`, `HireCrossFactionNeutralInns`);
- hire gear (`HireGearTiersEnabled`, `HireGearStartTier`, `HireGearTierCostMultipliers`, `BotGearPresets`, the unlock-quest keys);
- recruiter world buffs (`WorldBuffsEnabled`, the per-buff prices and the unlock fees).
