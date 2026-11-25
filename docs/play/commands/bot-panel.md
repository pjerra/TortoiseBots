---
id: play-commands-bot-panel
title: Bot Panel and Inspection
category: play
summary: Chat and addon commands that inspect a bot, show its bags and professions, and set behaviours, totems, blessings and choices.
tags: [commands, inspection, bot-panel, gear]
relates_to:
  - play-commands
  - play-commands-addon
  - play-commands-roster-and-hiring
---

# Bot Panel and Inspection

On ForeverCore, the bot panel is split between chat commands (`.bot ...`) and addon requests the TortoiseBotsForeverManager GUI sends over the addon channel. Chat replies are plain system lines; addon requests get `TBM:` lines plus an ACK or ERR.

## Summary

| Surface | What it covers |
| :--- | :--- |
| `.bot who` / `.bot stats` | Who you can order, and one line of state per bot. |
| `.bot version` / `.bot features` / `.bot help` | Module metadata, the server's switches, and the command list. |
| Addon `inv`, `professions`, `item`, `trade` | Bags, equipped gear, recipes, equip/unequip and trading. |
| `.bot behavior`, `.bot totem`, `.bot blessing`, `.bot choice`, `.bot service` | Per-bot settings and the mage/lock service query. |
| Addon `lootlog` | The last day of loot and roll rows. |
| `.tb why`, `.tb trace` | GameMaster diagnosis: one bot's AI decisions, or a run recorder. |

## Inspection

### `.bot who` / `.bot roster` / `.bot list`

```text
.bot who [all]
```

Prints the count and one line per online bot you may order: name, level, class, map id, movement (`follow`, `stay`, `free`) and brain (`engine`, or `spike` for a bot without an AI). `roster` and `list` are aliases. A GameMaster's `all` lists every bot in the world.

### `.bot stats`

```text
.bot stats
.bot stats <bot>
```

Without a name: one fleet line with the online bots you can see, how many you own, and how many run an engine. With a name: the bot's level, class, health and mana percent, money, zone, alive/dead and movement.

### `.bot version`

```text
.bot version
```

Aliases: `.bot v`, `.bot about`, `.bot credits`. Prints the build line, the `TBM:VERSION` line the addon handshakes, and the author credit.

### `.bot features`

```text
.bot features
```

Prints one `TortoiseBots: feature <key>=<value> (<label>)` line per server switch the addon's Info window reads: engine by default, pool, battlegrounds, tactical orders, player commands, the add-on hint, bots per player, ritual fill (and its range and hold) and object use, follow distance, master grace, auto-learn, auto-talents, auto-professions, auto-tools, hunter pet, auto food, class quest spells, the free-bot world switches (gathering, fishing, disenchanting, gear enchanting, auction house), group quests, capital errands, guild mail, guild invites by whisper, lockbox by guild mail, rogue pickpocket and lockboxes, and the rest thresholds.

The addon's `features` request adds `features all <text>`, a search of the `TortoiseBots.*` and `AiPlayerbot.*` keys with values masked for secrets.

### `.bot help`

```text
.bot help
```

Prints the usage rows for the commands you may use: version, features, who, follow, stay, attack, flee, stats, summon, add, logout, maintenance, totem, behavior, blessing, choice, service and the dungeon-finder fill.

### `.tb state` (GameMaster)

```text
.tb state <bot|party [leader]>
```

The detailed state record: role, class, level, health, power, target, movement and hold, stuck reason, engine, position, map and instance, last order, strategies, rest flags, life state, corpse, free flag, kills/deaths/XP, gear and more. It is the inspection view for a GameMaster; players get `.bot stats <bot>`.

## Bags, Gear and Recipes (Addon)

These requests come from the addon; the replies are `TBM:` lines.

```text
inv <bot>
item <bot> trade
item <bot> equip|unequip|give <bag> <slot> [mainhand|offhand|finger1|finger2|trinket1|trinket2]
professions <bot>
trade [<bot>]
```

- `inv` emits `TBM:INV_BEGIN|<bot>|<copper>`, one `TBM:INV_EQ` per equipped slot (with the bag size for worn bags), one `TBM:INV_ITEM` per carried item with flags (`e` can equip, `u` an upgrade, `b` cannot be traded now; `-` for none), and `TBM:INV_END|<bot>|<free>|<total>`.
- `item ... equip`/`unequip`/`give` moves one item and normally follows with a fresh `inv` snapshot. An explicit slot is checked by the core's own equip rules.
- `professions` emits the skill values, the known recipes with their colour steps and whether each may be ordered, and the missing recipes the bot could still learn.
- `trade` opens a trade window with the named bot or your selected bot; `item <bot> trade` does the same from the inventory view. The bot accepts on its own side.

There is no `.bot inv`, `.bot item` or `.bot professions` chat command on Forever.

## Loot History

```text
lootlog [cursor]
```

The addon request replays the last 24 hours of loot and roll rows for the bots in your current group (or your own bots when ungrouped), newest first, at most 40 rows per page. Send the returned cursor back for the next page; `expired` means the pointed-at piece was pruned. A GameMaster may run the same read in chat with `.tb lootlog [cursor]`; the output is `TBM:` lines meant for the addon.

## Behaviours

```text
.bot behavior <bot|party> <key> <on|off>
.bot behavior <bot|party> status
```

Keys: `loot`, `aoe`, `autocc`, `savemana`, `boost`, `threat`, `potions`, `mimic`, `spread`, `hold`, `noblink`, `offheal`, `offdps`. `offheal` and `offdps` exist only on paladins, priests, shamans and druids (other classes answer `class`); a strategy this build does not register answers `not-in-build`, and a change the engine does not hold answers `failed`. Values live until logout.

The addon's behavior verb takes `bot`, `party` or `raid` (raid means the same party scope) and refreshes each changed bot's `TBM:BOTSTATE` line.

## Totems

```text
.bot totem <bot|party> <earth|fire|water|air> <totem|auto|none>
.bot totem <bot|party> set <earth>,<fire>,<water>,<air>
.bot totem <bot|party> mode <auto|prepull|manual>
.bot totem <bot|party> status
```

A shaman's per-slot orders; `auto` keeps the automatic pick, `none` leaves the slot to the automatic pick. `set` validates all four slots before changing any. A totem the bot has not trained is refused `not-known`. Values live until logout.

The addon's totem verb adds `drop`, which queues the configured totems now, and reads `TBM:TOTEM|<bot>|earth=...,fire=...,water=...,air=...,mode=...,known=...`.

## Blessings

```text
.bot blessing <bot|party> <class> <blessing|auto>
.bot blessing <bot|party> player <name> <blessing|auto>
.bot blessing <bot|party> status
```

Classes are `warrior`, `paladin`, `hunter`, `rogue`, `priest`, `shaman`, `mage`, `warlock`, `druid`; blessings are `might`, `wisdom`, `kings`, `salvation`, `sanctuary`, `light` or `auto`. A non-paladin is refused `not-paladin`; an untrained blessing is refused `not-known` and not stored. A `player` exception wins over that character's class row and is capped at 20 exceptions per paladin. Values live until logout.

The addon's blessing verb also accepts `raid` as a party scope and reads `TBM:BLESSING|<bot>|warrior=...,...,known=...,players=...`.

## Choices

```text
.bot choice <bot|party> <aura|aspect|curse|poison_mh|poison_oh|sting|pet> <value|auto>
.bot choice <bot|party> status
```

| Kind | Values |
| :--- | :--- |
| `aura` (paladin) | `devotion`, `retribution`, `concentration`, `sanctity`, `shadow`, `fire`, `frost` |
| `aspect` (hunter) | `hawk`, `monkey`, `cheetah`, `pack`, `wild`, `beast`, `wolf`, `viper` |
| `curse` (warlock) | `agony`, `elements`, `shadow`, `recklessness`, `weakness`, `tongues`, `doom`, `exhaustion` |
| `poison_mh` / `poison_oh` (rogue) | `instant`, `deadly`, `crippling`, `mind`, `wound` |
| `sting` (hunter) | `serpent`, `scorpid`, `viper` |
| `pet` (warlock) | `imp`, `voidwalker`, `succubus`, `felhunter` |

`auto` clears the order. A bot whose class does not have the kind is refused `class` (a party summary counts it as refused), and an untrained value is refused `not-known`. Values live until logout.

## Service

```text
.bot service <bot|party>
.bot service <bot|party> portal <spellId>
.bot service <bot|party> conjure <food|water|healthstone>
```

The bare query answers `food=`, `water=`, `healthstone=` (1 when any rank of the conjure line is known, else 0) and `portals=` (`spellId:City` pairs, or `-`). In chat, `portal` and `conjure` currently refuse with `action-not-registered` because the `cast` action is not registered in this build; use the addon's `service` verb to cast a learned mage portal (out of combat, not dead, not casting, grouped with you; the rune is granted). Writes are owner tier; the query is tactical.

## Diagnostics (GameMaster)

### `.tb why`

```text
.tb why <bot> [seconds]
```

Writes every AI tick of one bot to the server log for up to 120 seconds (default 30), one trace at a time. Lines start `TBWHY|`, and a tick line carries the bot name, elapsed time, tick number, engine state, the chosen action and relevance, the target and its victim, and the actions that lost with their reason (`lower-relevance`, `not-possible`, `not-useful`, `cooldown`, `gcd`, `out-of-range`, `no-target`, `strategy-inactive`, `vetoed`, `backoff`, `prereq`, `failed`). At most 20 lines per second.

### `.tb trace`

```text
.tb trace start <run_id> [party [<leader>] | bot <Name>] [driver=<model>:<effort>:<run>]
.tb trace stop [<run_id>]
.tb trace status
```

The opt-in run recorder (`AiPlayerbot.RunTrace.Enabled`). Schema, files and the scorer: [Run Trace](../../server/run-trace.md).

## Not on ForeverCore Yet

`.bot status`, `.bot lease`, `.bot inv`, `.bot professions`, `.bot lootlog`, `.bot item`, `.bot trace` and `.bot why` as player chat commands do not exist here; the addon serves the inventory and profession panels, and the GameMaster uses `.tb` for loot history and diagnosis. There is no `.bot totem ... drop` in chat (the addon sends it), and no persistence for behaviour, totem, blessing or choice values across a logout.
