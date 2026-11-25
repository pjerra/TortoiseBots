---
id: play-commands-roster-and-hiring
title: Roster and Character Commands
category: play
summary: Commands for logging your own characters in as bots, listing them, summoning, gearing and maintaining them.
tags: [commands, roster, alts, gear]
relates_to:
  - play-commands
  - play-getting-started
  - server-settings-party
---

# Roster and Character Commands

On ForeverCore you bring your own characters into the world as bots, or a GameMaster logs any bot in. There is no mercenary hiring, no recruiter service and no autonomous alt-bot that plays while you are offline.

## Summary

| Command | Who | What it does |
| :--- | :--- | :--- |
| [`.bot who`](#roster) | player | Lists the bots you may order (aliases `roster`, `list`). |
| [`.bot add`](#logging-in-and-out) | player | Logs one of your own characters in as a bot in your party. |
| [`.bot logout`](#logging-in-and-out) | owner | Logs a bot out (alias `remove`). |
| [`.bot summon`](#summon) | master or GM | Brings a bot to your side. |
| [`.bot maintenance`](#maintenance) | owner | Tops up reagents, bandages and ammo, repairs and saves. |
| [`.bot autogear`](#gearing) | owner | Equips better gear up to a quality or item-level cap. |
| [`.autogear`](#gearing-a-player) | GM, or self | Gears a real player for a spec. |
| [`.tb login`](#game-master-roster-commands) | GM | Logs a character in, with a master or as a free bot. |
| [`.tb gear`](#gearing) | GM | Runs an upgrade pass or a generated gear set on bots. |
| [`.tb role`](#roles-and-specs) | GM | Sets `tank`, `healer`, `dps` or `none`. |
| [`.tb spec`](#roles-and-specs) | GM | Chooses the talent spec the bot's kit is built from. |

## Roster

```text
.bot who [all]
.bot roster
.bot list
```

Prints the count, then one line per online bot you may order: name, level, class, map id, movement (`follow`, `stay` or `free`) and brain (`engine` or `spike`). A GameMaster's `.bot who all` lists every bot in the world.

The addon's Roster window asks `roster` over the addon channel and gets the same visible bots as structured rows: location, class, grouped flag, the free bots, the crowd-control assignments, and each grouped bot's movement state. It also gets an empty claimed-guild list, because the Forever build has no claimed-bot store.

There is no away-summary store: the addon's `alts` request lists the free bots you can see, each with the line "<name> is playing on its own (free mode)".

## Logging In and Out

```text
.bot add <CharacterName>
.bot logout <CharacterName>
.bot remove <CharacterName>
```

- `.bot add` logs one of your own characters (same Battle.net account; a GameMaster may add any character) in as a bot and joins it to your party. You must lead your group if you are grouped, the character must be offline and not already a bot, the party must have room (five members, no raid), and `TortoiseBots.Player.MaxBots` (default 4) caps how many of your characters are in the world.
- Refusals: `not-found`, `already-online`, `not-leader`, `too-many`, `not-yours`, `login-refused`.
- `.bot logout` and its alias `.bot remove` log an owned bot out; the character keeps all progress. `not-found` means no such online bot.
- To play the character yourself, log into it from the character screen; the bot session is disconnected.

## GameMaster Roster Commands

```text
.tb login <character> [<master>|free]
.tb logout <character>
.tb group <bot> <master>
.tb ungroup <bot>
.tb tele <character> to <character> [revive]
.tb tele <bot|party [leader]> <map> <x> <y> <z> [o] [revive]
.tb free <bot> on|off
```

- `.tb login` logs any character in. Without a master it follows the typing GameMaster; `free` logs in a masterless bot that runs the free kit; from the console without a master it stands still.
- `.tb group` puts a bot into the party of a bot or your own character and makes that character its master; `.tb ungroup` removes it and clears the master.
- `.tb tele` is the test setup teleport, including into a character's instance; `revive` raises a dead source at the landing point.
- `.tb free <bot> on|off` turns an ungrouped, masterless bot into a free bot; `off` takes the free kit away (without a master it then stands).

## Free Bots

A free bot has no master and runs the free kit: grind, loot, corpse-run, gathering and the world services the `TortoiseBots.World.*` switches allow. `.bot who` shows it as `free`. It is the closest thing to an "alt playing on its own"; it has no progress summary and no separate login for it.

## Summon

```text
.bot summon <CharacterName>
```

- You must be the bot's master (a GameMaster may summon any bot). The bot must be alive, out of combat, not on a taxi and not already teleporting; in an instance the shared entry rules apply.
- The wait is `AiPlayerbot.OwnedBotSummonCooldown` (default 300 s, `0` = no wait, capped at 3600). Only a summon whose teleport started counts against it; a GameMaster is not held by it.
- Refusals name a code: `not-found`, `not-a-bot`, `not-yours`, `not-master`, `teleporting`, `in-flight`, `in-combat`, `dead`, `already-on-map`, `not-in-group`, `teleport-refused`, `on-cooldown`.
- There is no revive, repair or "summon on group accept" behaviour: the other `OwnedBotSummon*` keys are not read.

## Maintenance

```text
.bot maintenance <CharacterName>
```

Set `AiPlayerbot.OwnedBotMaintenanceEnabled = 1` first; it is off by default and answers `disabled` otherwise. An owner or GameMaster may run it on a live owned bot; pool bots are refused.

It tops up the class reagent table, First Aid bandages and hunter ammo, repairs all durability for free, and saves the character. Potions, food, thrown weapons and paid consumables are not topped up.

## Gearing

### `.bot autogear`

```text
.bot autogear <CharacterName> [quality|ilvl]
```

Dresses one of your own bots (or any bot, for a GameMaster) in better gear. Set `AiPlayerbot.OwnedBotAutogearEnabled = 1` first; off by default, and the answer says so. Pool bots, dead bots and bots inside a running dungeon run are refused. The option is a quality word (`poor`/`gray`, `white`, `green`, `blue`, `purple`, `legendary` and their synonyms) or an item-level number; the caps default to `AiPlayerbot.OwnedBotAutogearQualityCap` and `AiPlayerbot.OwnedBotAutogearIlvlCap`. Ranged weapons follow the ammo in the bot's bags; autogear hands out no ammo.

### `.autogear`

```text
.autogear [player name] <spec> [ilvl]
```

Gears a real player (or yourself when no name is given): upgrades only, replaced pieces to the bags (or mailed when the bags are full), and nothing destroyed. The target must be online, alive and out of combat; a druid must leave its shapeshift form. With `AiPlayerbot.PlayerAutogearForAll = 0` (default) it is GameMaster-only; with `1` every player may gear only themselves. Challenge-mode characters stay GameMaster-only.

| Class | Spec words (aliases) |
| :--- | :--- |
| Warrior | `arms`, `fury` (`dw`, `dualwield`), `protection` (`prot`, `tank`) |
| Paladin | `holy` (`healer`, `heal`), `protection` (`prot`, `tank`), `retribution` (`ret`, `retri`) |
| Hunter | `beast` (`beast mastery`, `bm`), `marksmanship` (`marks`, `mm`), `survival` (`surv`) |
| Rogue | `assassination` (`assas`, `sin`, `daggers`), `combat` (`swords`), `subtlety` (`subtle`, `sub`) |
| Priest | `discipline` (`disc`), `holy` (`healer`, `heal`), `shadow` |
| Shaman | `elemental` (`elem`, `ele`, `caster`), `enhancement` (`enhance`, `enh`), `enhancement tank` (`enh tank`, `tank`), `restoration` (`resto`, `healer`, `heal`) |
| Mage | `arcane`, `fire`, `frost` |
| Warlock | `affliction` (`afflic`, `affli`), `demonology` (`demo`), `destruction` (`destro`) |
| Druid | `balance` (`moonkin`, `boomkin`), `feral` (`feral dps`, `cat`), `feral tank` (`bear`, `tank`), `restoration` (`resto`, `healer`, `heal`) |

The default ceiling is what a spawned bot of that level gets (rare at most); `ilvl` can only lower it until level 60, where it may raise it, which also opens epic quality.

### `.tb gear`

```text
.tb gear <bot|party [leader]> upgrade
.tb gear <bot|party [leader]> set [<spec>] [level=<n>] [quality=<word>]
.tb gear audit [level]
```

- `upgrade` runs the bot's own equip-upgrades pass and reports the slots it changed. It needs a logged-in host bot with a running engine and no combat.
- `set` builds a fresh gear set for the level and quality (default: the bot's level and the level's quality cap). The spec word is optional; without one the bot's current spec is used. It also accepts your own character as the target.
- `audit` reports the generated gear index for the level (defaults: 10, 30 and 60).

## Roles and Specs

```text
.tb role <bot|party [leader]> tank|healer|dps|none
.tb spec <bot|party [leader]> <tab 0|1|2|spec name|none>
.tb talents <bot>
```

`.tb role` sets the forced role and rebuilds the kit. `tank` needs a class with a tank kit: warrior, bear druid, or a protection paladin (recorded only); a shaman, priest, mage or rogue is refused `no-tank-kit`. `none` restores automatic role inference.

`.tb spec` chooses the talent spec the kit is built from (`none` = automatic). `.tb talents <bot>` shows and applies the talent build (the spec's preset, built from the level-60 picks of Forever players); see [Configuration](../../server/configuration.md).

## Guild Orders

Guild orders are not a `.bot` chat command on Forever. Use `.tb guildorder create|list|cancel|status|work|meet|search|who|preview|steps|access|rank|reassign ...`, or the addon's Guild tab, which sends `order ...` over the addon channel (no hours given (or `inf`) means no time limit). The full guide is [Guild Orders](../guild-orders.md).

## Other Character Actions

- Release a dead bot: `.tb order <bot> release` (refused `no-entrance` inside an instance without an entrance point), or `.tb cmd revive party` to raise bound bots.
- Corpse run: `.tb cmd corpserun [bot]`.
- Train at a trainer: `.tb order <bot> train`.
- Trade: the addon's trade verb (`trade [<bot>]`).

## Not on ForeverCore Yet

Mercenary hiring (`.bot hire`, `.bot hire raid`), recruiter menus, capital world-buff services, mercenary contracts, autonomous alt-bots (`.bot alts` summaries, alt progress, `AiPlayerbot.AltBots`), `.bot invite` / `.bot uninvite`, `.bot gear` tier and preset names, `.bot role` spec ids, and the chat `.bot release` / `.bot corpse run` / `.bot learn` / `.bot trade` commands are not in this build. Their settings stay in `aiplayerbot.conf.dist`, but no gameplay code reads them.
