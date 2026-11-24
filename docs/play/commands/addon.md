---
id: play-commands-addon
title: TortoiseBotsForeverManager Addon
category: play
summary: Installing the Forever addon, what its tabs do, and the TBM messages the server answers.
tags: [addon, tbm, gui, player-controls]
relates_to:
  - play-commands
  - play-getting-started
  - play-commands-bot-panel
---

# TortoiseBotsForeverManager Addon

**TortoiseBotsForeverManager** is the addon for the Forever build. It gives the bots' orders and state a graphical face: party orders, the roster, inventory, behaviour toggles, loot history, guild orders and the run status.

## Installation

1. Download the addon from <https://github.com/TortoiseBots/TortoiseBotsForeverManager>.
2. Extract it so the file lands at:
   ```text
   World of Warcraft/Interface/AddOns/TortoiseBotsForeverManager/TortoiseBotsForeverManager.toc
   ```
   (not inside a second folder of the same name).
3. Log in and type `/tbfm` in chat to open or close it. `/tb`, `/tbot` and `/tortoise` work too.

It is a different addon from the Turtle build's TortoiseBotsManager. The two share a protocol dialect, not a UI.

## Tabs

The window groups the work the way a party does it. Fight, Camp, Party, Guild, Roster and Log are always shown; Raid appears while you are in a raid, and Dungeon and Loot appear only when the server advertises `dungeon-run` or `loot-log`.

| Tab | What it is for |
| :--- | :--- |
| **Fight** | The party in a pull: attack, pull, crowd control and the combat toggles. |
| **Camp** | Between pulls: rest, loot, repair, sell, the camp and ready orders. |
| **Party** | Who does what: roles, follow and stay, formation, buffs and blessings. |
| **Roster** | Who is logged in: your owned characters, their state and the login, logout, invite and summon actions. |
| **Guild** | The claimed guild bots and their guild orders. |
| **Raid** | The raid layout and raid-only orders; shown only while you are in a raid. |
| **Dungeon** | The dungeon run status and plans; shown only when the server advertises the `dungeon-run` capability. The Forever build does not, so this tab stays hidden. |
| **Loot** | Loot and roll history; shown when the server advertises `loot-log`. |
| **Log** | The protocol log: every `TBM:` line the server sent, for support and checking a panel. |

## What the server answers

The addon sends short verbs over the addon channel, not chat text. The Forever server registers these verbs:

| Verb | What it does |
| :--- | :--- |
| `action <intent>` | The party-wide orders: `attack`, `stop`, `follow`, `stay`, `flee`, `guard`, `stance`, `engage`, `rebuff`, `interrupt`, `pull`, `pullback`, `end pull`, `come`, `hold`, `comestay`, `focus skull`, `cc clear`, `release`, `repair`, `sell`, `learn`, `rest`, `drink`, `eat`. |
| `roster` / `list` | The roster snapshot, the handshake trailer and the party's movement states. |
| `who`, `stats`, `add`, `logout`, `remove`, `summon`, `maintenance`, `autogear` | The same words as the `.bot` commands of those names. |
| `alts` | One line per owned bot playing on its own (free mode). |
| `inv` / `inventory` / `item` | The inventory snapshot, and item moves into and out of a bot's bags. |
| `trade` | Drives a trade window with a bot. |
| `behavior`, `totem`, `blessing`, `choice`, `service` | The class and behaviour orders behind those `.bot` verbs. |
| `professions` | A bot's profession skills, known recipes and the recipes it could learn. |
| `lootlog` | Paged loot and roll history. |
| `order` | Guild orders: list, search, preview, steps and the guild-bot rank report. |
| `ai` | Saved strategy presets. |
| `features` | The server's player-facing switches; `features all <text>` searches the module's settings. |
| `run` | Run status, the nearest plan (`run here`) and the plan list. |

A verb the server cannot run answers one `TBM:ACTION_ERR|<verb>|<code>|<text>` line, so the addon never waits forever. The addon's Free button sends `free`, which parses but is currently refused as `unknown-verb`.

### Capabilities

The handshake sends `TBM:CAPS` with the features this server really has. Always: `claimed-roster`, `inventory`, `professions`, `loot-log`, `behavior`, `service`, `totem`, `blessing`, `blessing-player`, `choice`, `ai`, `pull-seconds`, `flee`, `engage`, `guild-orders`, `guild-order-steps`, `guild-order-nobuy`. For a GameMaster: `tb-gm`. The addon only lights a button whose capability is on the line.

## Transport

- The client sends addon messages on the prefix `TBM`. The server accepts them when they arrive as your own self-whisper or on the group addon channel.
- On the roster reply the server names the channel it accepts in `TBM:TRANSPORT|<party|none>`: `party` while you are in a group and the server's addon channel is on (a battleground group with no pre-battleground group gets `none`); `none` otherwise. When it says `none`, the addon falls back to plain `.bot` chat for the words that exist as `.bot` commands.
- Commands are queued per requester and drained on the world thread; the cap is 128 pending commands per player. Past the cap, the excess is dropped and one error line says how many.
- A line is at most 255 bytes each way. A longer reply part is cut at 255 bytes (the server logs that once); structured lists are sent as counted `BEGIN`/`END` chunks so each row fits.
- Addon-language chat never commands a bot: a `TBM` message is a request to the addon channel, not an order to a bot's AI.

## Message streams

Every structured line starts with `TBM:`. The ones the Forever server sends:

| Stream | Meaning |
| :--- | :--- |
| `VERSION`, `PROTO`, `LATEST`, `VERBS_*`, `CAPS`, `TRANSPORT` | Handshake: module version, protocol level and minimum addon level, update-check result (`-` when unknown), advertised verbs, capabilities and the command channel. |
| `ROSTER_BEGIN`, `ROSTER`, `ROSTER_END` | The roster: one row per owned character (`guid`, name, class id, state, grouped, location, class name). |
| `ALTBOTS` | Comma-separated owned bots playing on their own (free mode). |
| `ALTSUM`, `ALTSUM_END` | One summary line per such bot, then the count. |
| `CC_ASSIGN_BEGIN`, `CC_ASSIGN`, `CC_ASSIGN_END` | The party bots holding a crowd-control mark. |
| `CLAIMED_BEGIN`, `CLAIMED_END` | The claimed guild-bot snapshot. The Forever build keeps no guild-bot store, so the pair is always empty. |
| `BOTSTATE_BEGIN`, `BOTSTATE`, `BOTSTATE_END` | The party bots' movement and behaviour state (`move=follow\|stay\|free\|...`, `sap=<on|off>`, class toggles and active specs). |
| `INV_BEGIN`, `INV_EQ`, `INV_ITEM`, `INV_END` | One bot's inventory snapshot: money, equipped items with durability, carried items, free and total slots. |
| `PROF_BEGIN`, `PROF`, `RECIPE`, `RECIPE_MISSING`, `PROF_END` | A bot's professions, known recipes (with their skill steps and order verdict) and learnable recipes. |
| `TOTEM`, `BLESSING`, `CHOICE`, `SERVICE` | The shaman totem loadout, paladin blessings, class choices, and what a bot can conjure or portal. |
| `LOOT_BEGIN`, `LOOT`, `LOOT_END` | One page of loot and roll history with the cursor for the next page. |
| `ORDER_BEGIN`, `ORDER`, `ORDER_END` | The guild order list: each row is `TBM:ORDER\|<bot>\|<type>\|<itemId>\|<item>\|<have>\|<want>\|<state>\|<reason>\|<zone>\|<difficulty>\|<hours>\|<keep>\|<parent>\|<goldCap>\|<step>\|<need>\|<carry>\|<bring>`. `<hours>` is `1`–`24` or `inf` (no time limit); `<carry>` is the count in the bot's bags and mailbox not delivered yet (`0` once the hand-off succeeded). |
| `ORDER_ACCESS` | The guild's bot rank and what it reaches: bank tabs, guild gold and whether you may change the rank. |
| `ORDER_STEPS_BEGIN`, `ORDER_STEP`, `ORDER_STEPS_END` | The on-demand step timeline of one order. |
| `ORDER_ENCHANTS_BEGIN`, `ORDER_ENCHANT`, `ORDER_ENCHANTS_END` | An enchant search result. |
| `ORDER_ITEMS_BEGIN`, `ORDER_ITEM`, `ORDER_ITEMS_END` | An item search result. |
| `ORDER_WHO_BEGIN`, `ORDER_WHO`, `ORDER_WHO_END` | A guild-wide craft or enchant search. |
| `ORDER_PREVIEW` | What an order would do, from the same planner the order uses. |
| `RUN`, `RUN_HERE` | The party's run status and the nearest plan on your map. |
| `FEATURE`, `CONFIG_BEGIN`, `CONFIG`, `CONFIG_END` | The server's switches, and the result of a settings search. |
| `ACTION_ACK`, `ACTION_ERR` | One answer to one request: what ran (with the scope and count) or why it did not. |

The Turtle build's perception feed (`VIEW`) and the scout lines are not wired on Forever, so they never arrive.

## See also

- [Bot Commands Overview](README.md) for the `.bot` verbs the panels mirror.
- [Bot Panel](bot-panel.md) for the inspection and gear commands in text form.
