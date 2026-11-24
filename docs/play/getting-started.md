---
id: play-getting-started
title: Getting Started with TortoiseBots
category: play
summary: How to set up your client, log your own characters in as bots, order them around, and meet the free bots that live on a WoW Forever server.
tags: [getting-started, quickstart, roster, party, alts, addon]
relates_to:
  - play-commands-roster-and-hiring
  - play-commands-party-actions
  - play-commands-addon
  - play-dungeons-and-raids
  - server-settings-party
---

# Getting Started with TortoiseBots

TortoiseBots fills a **WoW Forever** realm with bots and lets you take some of them along: your own characters, or bots that live in the world and may join your party. This page gets your first party going.

## What you need

- A character to play, and one to four more characters on your own account that you want as bots.
- A server that runs TortoiseBots **with the bot engine** and has the module switched on (`TortoiseBots.Enable = 1`, off by default).
- The [TortoiseBotsForeverManager](commands/addon.md) addon (`/tbfm`, also `/tb`, `/tbot` and `/tortoise`) in your client's `Interface/AddOns/` folder. Optional, but easier than typing commands.

The `.bot` command is for players only while `TortoiseBots.Player.Commands = 1` (the default). A server admin can turn it off; then only GameMasters may use `.bot`.

## The kinds of bots

| Kind | What it is | How you get one | When you are done with it |
| :--- | :--- | :--- | :--- |
| **Free bot** (pool bot) | A bot that lives in the world: quests, grinds, loots, sells, trains. Pool bots are free bots that the server's pool logs in and out on a schedule. | You don't. One may join your group for a shared quest or fill your dungeon-fill request. | It carries on with its own life. |
| **Own alt** | A character on your own account, played by the server. | `.bot add <Name>` or the addon's roster. | `.bot logout <Name>` logs it out. |

Hired companions and claimed guild bots from the Turtle WoW build are not on WoW Forever: there is no `.bot hire`, no recruiter, and a guild invite does not make a pool bot yours. See [What bots can do](features.md#not-on-wow-forever-yet).

## Your first party

| Step | With the addon | With chat commands |
| :--- | :--- | :--- |
| 1. Log in your alt | `/tbfm` → roster → **Login** (the addon sends the same `.bot add`) | `.bot add <Name>` |
| 2. See it | roster list | `.bot who` |
| 3. Bring it to you | **Summon** | `.bot summon <Name>` |
| 4. Give an order | party action buttons | `.bot follow <Name>`, `.bot stay <Name>`, `.bot attack <Name>`, `.bot flee <Name>` |
| 5. Log it out later | **Logout** | `.bot logout <Name>` |

- The bot joins your party when it logs in.
- Up to four of your own characters can be online as bots at once (`TortoiseBots.Player.MaxBots`). An account can hold only one session, so a character already logged in by a person cannot become a bot.
- Want to play the alt yourself? Log into it from the character screen; the bot session ends by itself.
- `.bot stats` shows the fleet in one line; `.bot stats <Name>` shows one bot's level, class, health, mana, money, zone and whether it follows, stays or is free.
- A summoned bot arrives next to you only when neither of you is in combat, the bot is alive, and neither of you is on a taxi or being teleported; the same bot has a short wait between summons (`AiPlayerbot.OwnedBotSummonCooldown`, 300 seconds by default).

`.bot help` prints the commands your server allows. The short list is: `add`, `logout`, `who`, `stats`, `summon`, `follow`, `stay`, `attack`, `flee`, `maintenance`, `autogear`, `totem`, `behavior`, `blessing`, `choice`, `service`, `lfg`, `version`, `features`.

## Ordering bots in the field

The addon's `action` command sends the same party orders a GameMaster can send with `.tb order`. These intents work on WoW Forever:

| What you want | Intent | What the bot does |
| :--- | :--- | :--- |
| Attack your target | `attack` | Attacks the unit you have selected. |
| Pull a pack | `pull` (optionally seconds, `ranged` or `body`) | The tank pulls and brings the pack to the group. |
| Pull around a corner | `pullback` | The tank pulls from range and runs back to your spot. |
| Stop a pull | `end pull` | Gives up the pull and returns. |
| Stop moving | `stay` | Holds position. |
| Follow again | `follow` | Follows you in formation. |
| Come here | `come`, `hold`, `comestay` | Walks to your spot. |
| Free a bot | `free` | Stops following; the bot goes its own way. |
| Flee | `flee` | Runs from the fight. |
| Guard | `guard` | Guards the spot. |
| Interrupt | `interrupt` | Picks a party member who can interrupt and sends it. |
| Focus a target | `focus skull` | Marks the target Skull and sends the party at it. |
| Clear a CC mark | `cc clear` | Clears the bot's crowd-control mark. |
| Train / rest / repair / sell | `learn`, `rest`, `drink`, `eat`, `repair`, `sell` | Runs the errand at a nearby NPC. |
| Release | `release` | Releases the bot's spirit. |

Other actions of the Turtle build — `cc <mark>`, `aoe`, `auto cc`, `sap`, `loot`, `corpse run`, `trade`, `use`, `ready`, `raid ...` — answer `unsupported` on WoW Forever. The technical reason is in [Known limitations](known-limitations.md).

## Free bots and the pool

A fresh install has no bots in the world. The server admin enables the pool and starts it:

1. `TortoiseBots.Pool.Enable = 1` in `modules/tortoise_bots.conf`.
2. `.tb pool create <n>` makes pool characters (up to `TortoiseBots.Pool.MaxCharacters`, 50 by default).
3. `.tb pool start` keeps `TortoiseBots.Pool.TargetOnline` bots online (0 by default; `.tb pool population low|high` uses the 10/50 presets).

Free bots quest, grind, loot, vendor, repair, train, run capital errands, fish, disenchant and may take a guild invite. The full list is in [What bots can do](features.md) and [Bot pool](../server/bot-pool.md).

## Guilds

A free bot may accept `/ginvite` from your guild when it has no guild (`TortoiseBots.World.Guilds = 1`, on by default). It keeps living its own life. A server admin gives it [guild orders](guild-orders.md) with `.tb` (a guild leader who has no GM rights cannot): farm, buy or craft an item for the guild bank, or mail it an item to disenchant.

A guild invite does **not** make the bot your companion: you cannot order it with `.bot` commands unless it is one of your own characters or the server has bound it to you as a commander.

## Levelling, spells and gear

| | Own alt | Free bot / pool bot |
| :--- | :--- | :--- |
| **Class spells** | Learned free on login and every level-up (`TortoiseBots.AutoLearnOnLevel`) | Learned free on login and every level-up |
| **Talents** | Spent automatically on a premade build (`TortoiseBots.AutoTalentsOnLevel`) | Spent automatically on a premade build |
| **Professions** | Not granted automatically | Two professions, First Aid and Cooking at level 5 (`TortoiseBots.AutoProfessions`) |
| **Tools and food** | Not granted automatically | Gathering/crafting tools and level-fitting food and water (`TortoiseBots.AutoTools`, `TortoiseBots.AutoFood`) |
| **Gear** | What you give it; it also equips upgrades it loots | Gear from loot, quests and generated upgrades (`AiPlayerbot.RandomGearProgression`, `AiPlayerbot.AutoEquipUpgradeLoot`) |
| **Mounts** | Yours to buy | Learns to ride and buys its race's mount with its own gold at 40 and 60 (`AiPlayerbot.LevelUpMounts`) |
| **Loot rolls** | Rolls with the group | Rolls with the group |

The bot engine does not use Turtle-only custom spells, races or content.

## Building a balanced 5-man party

One tank, one healer, one melee damage dealer and two ranged damage dealers:

| Role | Good picks | What they bring |
| :--- | :--- | :--- |
| **Tank** | Protection Warrior, Bear Druid, Protection Paladin | Holds threat, taunts, interrupts |
| **Healer** | Holy Priest, Restoration Shaman, Holy Paladin, Restoration Druid | Healing and dispels |
| **Melee DPS** | Combat or Assassination Rogue, Fury Warrior, Cat Druid | Burst, interrupts |
| **Ranged DPS** (2) | Frost or Fire Mage, Marksmanship Hunter, Affliction Warlock | Polymorph, Freezing Trap, Banish, conjured food and water |

## Where next

- [Dungeon & Raid Tactics](dungeons-and-raids.md): pulling, crowd control, wipes, loot.
- [What Bots Can Do](features.md): every feature and whether it is on.
- [Party Action Commands](commands/party-actions.md) and [Whisper Commands](commands/whispers.md): every order.
- [Known Limitations](known-limitations.md): what does not work yet.
