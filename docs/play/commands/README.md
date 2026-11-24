---
id: play-commands
title: Bot Commands Overview
category: play
summary: The channels that command bots on WoW Forever, the player `.bot` verbs, the addon channel and the GameMaster `.tb` host commands.
tags: [commands, player-controls, cli]
relates_to:
  - play-commands-party-actions
  - play-commands-commander
  - play-commands-roster-and-hiring
  - play-commands-bot-panel
  - play-commands-whispers
  - play-commands-addon
  - server-admin-commands
---

# Bot Commands Overview

This page is the index of the command surfaces the Forever build actually has. Everything here is live on the Forever server; Turtle-only words are named where you would look for them.

## Control channels

| Channel | How you use it | What it carries |
| :--- | :--- | :--- |
| **`.bot` chat** | Typed in chat or bound to a macro | Ownership, movement, the party toggles and the panel verbs listed below. Handled by the module's player entry. |
| **Whisper or party chat** | `/w <BotName> <words>`, or the same words in the bot's party | The bot's own command list (loot, vendors, quests, strategies, training). See [Whisper Commands](whispers.md). |
| **Addon (TBM channel)** | The TortoiseBotsForeverManager addon | Party-wide orders (`action`), roster, inventory, behaviour, blessing, choice, totem, service, loot history, guild orders and run status. See [Addon](addon.md). |
| **`.tb` console** | A GameMaster in chat, or the server console | Host commands: bot login and group, pool, plans and runs, orders, gear, talents, traces and test helpers. |

### What `.bot action` is (and is not)

Party-wide orders are the addon channel's `action` verb, not chat text. The addon's Fight, Camp and Party panels send it; a typed `.bot action <intent>` in chat is refused as an unknown verb.

The orders the server runs this way are: `attack`, `stop`, `follow`, `stay`, `flee`, `guard`, `stance`, `engage`, `rebuff`, `interrupt`, `pull`, `pullback`, `end pull`, `come`, `hold`, `comestay`, `free`, `focus skull`, `cc clear`, `release`, `repair`, `sell`, `learn`, `rest`, `drink`, `eat`. A parseable intent the server does not run answers one `TBM:ACTION_ERR` line with its reason (raid tactics, party CC picks, behaviour toggles, trade, use-object and ready checks are named there).

## Player commands (`.bot`)

| Command | What it does | Documented in |
| :--- | :--- | :--- |
| `.bot help` | Prints this command list in chat. | [Bot Panel](bot-panel.md) |
| `.bot version` (also `v`, `about`, `credits`) | Module version and credits. | [Bot Panel](bot-panel.md) |
| `.bot features` | Lists the server's player-facing switches (the addon's Info window). | [Bot Panel](bot-panel.md) |
| `.bot who [all]` (also `roster`, `list`) | Lists the bots you may order, one line each; a GM may add `all`. | [Roster and Hiring](roster-and-hiring.md) |
| `.bot add <name>` | Logs in one of your own characters as a bot; it joins your party. | [Roster and Hiring](roster-and-hiring.md) |
| `.bot logout <bot>` (also `remove`) | Logs your bot out. | [Roster and Hiring](roster-and-hiring.md) |
| `.bot summon <bot>` | Brings the bot to your side; its master only, with a per-bot wait. | [Roster and Hiring](roster-and-hiring.md) |
| `.bot follow <bot> [member]` | The bot follows you, the named member or your target. | [Party Actions](party-actions.md) |
| `.bot stay <bot>` | The bot stands where it is. | [Party Actions](party-actions.md) |
| `.bot attack <bot>` | The bot attacks your selected target. | [Party Actions](party-actions.md) |
| `.bot flee <bot>` | The bot runs from the fight. | [Party Actions](party-actions.md) |
| `.bot stats [<bot>]` | One line about your fleet, or about one bot. | [Bot Panel](bot-panel.md) |
| `.bot maintenance <bot>` | Refreshes the bot's kit and repairs it, when upkeep is enabled. | [Roster and Hiring](roster-and-hiring.md) |
| `.bot autogear <bot> [quality\|ilvl]` | Gears your own bot in level-appropriate items. | [Roster and Hiring](roster-and-hiring.md) |
| `.bot behavior <bot\|party> <toggle> <on\|off>` (also `behaviour`) | Turns a persistent behaviour on or off (loot, AoE, auto CC, mana saving, potions, and more). | [Bot Panel](bot-panel.md) |
| `.bot totem <bot\|party> ...` | Pins a shaman's totems per element, sets them as a set, or shows them. | [Bot Panel](bot-panel.md) |
| `.bot blessing <bot\|party> ...` | Orders a paladin's blessing per class, or for one player. | [Bot Panel](bot-panel.md) |
| `.bot choice <bot\|party> ...` | Orders a paladin aura, hunter aspect, warlock curse, rogue poison or pet. | [Bot Panel](bot-panel.md) |
| `.bot service <bot\|party> [portal <spellId>\|conjure <food\|water\|healthstone>]` | Lists what the bot can conjure or portal (`portal` and `conjure` currently refuse in chat because the `cast` action is not registered; use the addon for mage portals). | [Bot Panel](bot-panel.md) |
| `.bot lfg join <dungeon id> <tank\|healer\|dps>` / `.bot lfg status` / `.bot lfg leave` | The dungeon fill for your own character: queues you, shows the open slots and the bots it added, and releases them. | [Queues](../../world/queues.md) |

A refused `.bot` line answers `TortoiseBots: <verb>: refused <code>: <text>`.

### Ownership and tiers

- **Your bots** are the characters on your Battle.net account; a GameMaster may name any bot. Ownership is read fresh on every command, so a moved game account stops being yours at once.
- **Tactical verbs** (`follow`, `stay`, `attack`, `flee`, `who`, `roster`/`list`, `stats`) also accept the bot's master, the player whose binding commands it, and the leader of a group the bot is in. Everything else needs the owner or a GM.
- **A running dungeon run refuses every bot verb, GMs included.** Pause or stop the run instead.
- The switch `TortoiseBots.Player.Commands` (default `1`) turns the player `.bot` verbs off; `TortoiseBots.Player.MaxBots` (default `4`, capped at `4`) caps how many owned bots one account logs in.

## GameMaster commands (`.tb`)

`.tb` is the module's host command table, and every row needs the GameMaster permission (some rows ask for more, such as `.tb create` or `.tb pool create`, which ask what `.account create` asks). Groups:

| Group | Examples |
| :--- | :--- |
| Bots and groups | `.tb login <character> [master\|free]`, `.tb logout`, `.tb group` / `.tb ungroup`, `.tb free <bot> on\|off`, `.tb status`, `.tb where <bot>` |
| Pool and population | `.tb pool create\|start\|stop\|status\|population\|prune` |
| Dungeon plans and runs | `.tb plan list`, `.tb plan <plan> info`, `.tb plan <plan> check <character>`, `.tb plan reload`, `.tb run start\|stop\|pause\|resume\|status\|events` |
| Commander bonds | `.tb commander status [<commander>]`, `.tb cmd ...`, `.tb master ...`, `.tb party ...` |
| Orders, gear and talents | `.tb order`, `.tb craftorder`, `.tb guildorder`, `.tb gear`, `.tb equip`, `.tb spec`, `.tb talents` |
| Diagnostics | `.tb perf`, `.tb state`, `.tb threat`, `.tb trace`, `.tb why`, `.tb unported`, `.tb onyxia` |
| Test helpers | `.tb level`, `.tb train`, `.tb mount`, `.tb cast`, `.tb additem`, `.tb setskill`, `.tb knock`, `.tb speed` |

The full row-by-row list, with every argument and refusal, is `forever/docs/TB-COMMANDS.md`. `.tb plan` and `.tb run` are documented on the [Dungeon Run Leader](../dungeon-run-leader.md) page.

## Addon channel (TBM)

The addon talks to the server as addon messages prefixed `TBM`, and the server answers on the TBM lines listed in the [Addon](addon.md) page. The verbs the server registers are `action`, `add`, `ai`, `alts`, `autogear`, `behavior`, `blessing`, `choice`, `features`, `inv`, `inventory`, `item`, `list`, `logout`, `lootlog`, `maintenance`, `order`, `professions`, `remove`, `roster`, `run`, `service`, `stats`, `summon`, `totem`, `trade` and `who`. Anything else answers `TBM:ACTION_ERR|<verb>|unsupported|not available on this server yet`.

## Whispers

The words a bot obeys by whisper or party chat, their tiers and their parameters are on the [Whisper Commands](whispers.md) page. The `.bot command <BotName> ...` proxy of the Turtle build is not on WoW Forever yet.

## Party-wide commands elsewhere

The party action, commander and hiring pages describe the full design. Where a page describes a command you cannot find in this index, this page is the current list for the WoW Forever build.
