---
id: play-glossary
title: Glossary
category: play
summary: Short explanations of the abbreviations and TortoiseBots terms used across the WoW Forever documentation.
tags: [glossary, terms, abbreviations]
relates_to:
  - play-getting-started
  - play-features
---

# Glossary

Short explanations of the abbreviations and project terms used in these docs. Each term links to the page that covers it.

## TortoiseBots terms

| Term | Meaning |
| :--- | :--- |
| **Free bot** | A bot that lives in the world on its own: it quests, grinds, loots, sells and trains. The pool logs free bots in and out. See [The kinds of bots](getting-started.md#the-kinds-of-bots). |
| **Pool bot** | A free bot created by the server's bot pool (`TortoiseBots.Pool.Enable`). Its account is one the module made; the pool logs it in for a rolled session. See [Bot pool](../server/bot-pool.md). |
| **Own alt** | One of your own characters logged in as a bot with `.bot add`. It follows you and takes your orders. |
| **Master** | The player an own alt follows and takes orders from. |
| **Commander binding** | A server-side link between a player (the commander) and a bot party, made with `.tb master`. A GameMaster uses it to give party orders and to start a run. See [Run leader](dungeon-run-leader.md). |
| **Run leader** | The bot that walks a stored dungeon plan with a bound party. Started by a GameMaster with `.tb run`. |
| **Dungeon fill** | The module's own group filler behind `.bot lfg join <dungeon id> <role>`: it adds free bots to your group when a role is missing. It is not the Turtle WoW dungeon finder. See [Queues](../world/queues.md). |
| **Setting / feature flag** | A line in `tortoisebots.conf` (`TortoiseBots.*`) or `aiplayerbot.conf` (`AiPlayerbot.*`) that turns a feature on or off. See [How configuration works](../server/configuration.md). |
| **TBM** | The TortoiseBotsForeverManager addon, opened with `/tbfm` (aliases `/tb`, `/tbot`, `/tortoise`). It talks to the server with `TBM:` addon messages. See [Addon](commands/addon.md). |
| **Pullback** | A pull where the tank tags a pack and runs back around a corner, so the mobs come to the group out of line of sight. See the addon's `pullback` action in [Party actions](commands/party-actions.md#addon-action-buttons). |
| **`.tb`** | The GameMaster and server-console command family. Players cannot use it. See [Admin commands](../server/admin-commands.md). |

## Abbreviations

| Short | Meaning |
| :--- | :--- |
| **AH** | Auction House. |
| **AB / AV / WSG** | Arathi Basin, Alterac Valley, Warsong Gulch (battlegrounds). |
| **AoE** | Area of effect: a spell that hits everything in an area. |
| **BG** | Battleground. |
| **CC** | Crowd control: keeping a mob out of the fight with Polymorph, Sap, Shackle and similar spells. |
| **CD** | Cooldown. |
| **DND** | "Do not disturb" chat status (`/dnd`). |
| **DPS** | Damage dealer (damage per second). |
| **GCD** | Global cooldown. |
| **GM** | GameMaster: a server administrator account. |
| **GUID** | The unique number the server gives every character, creature and object. |
| **LFG** | Looking for group: here, the module's dungeon fill, not a Turtle WoW feature. |
| **LoS** | Line of sight. |
| **NPC** | A creature or character controlled by the server, not by a player. |
| **XP** | Experience points. |
