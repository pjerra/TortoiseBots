---
id: play-known-limitations
title: Known Limitations & Quirks
category: play
summary: What the ForeverCore build does not do yet, which Turtle WoW features are absent, and the behaviour you should expect from the switches and timers.
tags: [limitations, non-goals, quirks, engine, port]
relates_to:
  - play-getting-started
  - play-dungeons-and-raids
  - server-bot-pool
---

# Known Limitations & Quirks

TortoiseBots on **ForeverCore** is a port of the Turtle WoW module. The bot engine is large and most of it runs, but some services of the old build are not connected to ForeverCore yet. This page lists what is missing, what is deliberately different, and the timers you will notice.

## The two switches first

- **The module can be on with no bots.** `TortoiseBots.Enable = 0` is the shipped default: the module loads, its commands refuse, and nothing ticks. A server admin sets it to `1`.
- **The bot engine is a build option.** With `TORTOISEBOTS_SLICE1=OFF` the module has the session, the pool and the `.tb` commands, but no bot brain: `.bot` is unknown to the core and every engine command answers that the engine is not built in.
- **Players can lose `.bot`.** `TortoiseBots.Player.Commands = 0` leaves `.bot` to GameMasters only.

## Features that are not on ForeverCore yet

| Missing | What to do instead |
| :--- | :--- |
| Hired companions, recruiters, contracts and recruiter world buffs | Use your own characters (`.bot add`) or the free bots the pool logs in. |
| Autonomous alt-bots (an alt that leaves your group keeps playing) | Log it out with `.bot logout`; it does not start playing on its own. |
| Claiming a pool bot into your guild as your own companion | A free bot can accept a guild invite, but it keeps its own life and is not yours to order with `.bot`. |
| Boss and raid tactics (per-boss interrupts, positioning, add rules, Fankriss/Patchwerk roles) | Fight the encounter with your own calls. `AiPlayerbot.RaidReactions` is off and the boss-tactic tables are not loaded. |
| Turtle custom raids, dungeons, spells and races | None of that content exists on the Forever client. |
| Travel destinations outside the reduced set (trainer, auction house, mailbox, boss, gathering node) | Bots travel for grind spots, towns, vendors, repairs and leaving an outgrown zone; walk or fly to the other NPCs yourself. |
| Camp spots and Traveler's Tents | None. The camp tables are not loaded. |
| The separate offline auction-house seller (`AiPlayerbot.AhBot.*`) | The organic bot market and its settings still exist. |
| Pool lineages and adopting old bot accounts | The Forever pool creates its own characters and resets only its own rows. |
| Alterac Valley, arenas and the random queue | Battleground fill covers Warsong Gulch and Arathi Basin only, and only when `TortoiseBots.Bg.Enable = 1`. |
| Assigning a party crowd-control mark (`cc <mark>`) | `cc clear` works. A GameMaster can set one mark with `.tb cc`. |
| Herbs and ore looted by the bot brain | The host gathers them when `TortoiseBots.World.Professions = 1` (off by default). |
| Engines that use game objects through the old use packet, item casts without an owned item, and the old taxi/transport model | The host provides its own flight, ride, fishing, lockbox and profession errands where it needs them. |

The `not ported yet` list also covers internals with no player-visible switch: the 1.12 packet tables, random item properties, keyring/bank slot assumptions, creature/game-object loot as a public member, emotes, talent-spec tables, bot texts and help topics, chat filters, and the old pool, hire, claim and guild-order stores. A server admin can see which ones the engine actually reached with `.tb unported`; the first reach also writes one `TortoiseBots: not ported yet: ...` line to the server log.

## Timers and behaviour you will notice

| Situation | What happens | What to do |
| :--- | :--- | :--- |
| Your master character logs out | A bot waits `TortoiseBots.Slice.MasterGraceSeconds` (300 s) for you to come back, then logs out. `0` logs it out at once. | Log back in within the grace, or use `.bot logout`. |
| A commanded bot dies in a dungeon | Its body waits `TortoiseBots.Slice.DeadBackstopSeconds` (180 s); if nothing raises it, it releases and returns at the entrance. | Revive it yourself if you can. |
| You summon the same bot twice | The second summon waits out `AiPlayerbot.OwnedBotSummonCooldown` (300 s). A GameMaster is not held by the wait. | Wait, or walk to the bot. |
| You summon a bot that is dead, in combat, or in another instance | The shared teleport refuses; the reason comes back as a code in the `.bot summon` reply. | Wait until it is alive and out of combat, or enter its instance. |
| A bound (commanded) pool bot | The pool stops counting it and never logs it out; the account stays in use until the binding ends. | End the binding to return the bot to the pool. |
| The pool is reset | Only at world startup, and only with `TortoiseBots.Pool.Reset`. `.reload config` never starts one and there is no live reset command. | Restart the world. |
| A bot leaves your party | Nothing special: your own alt keeps following you, a free bot goes back to its own life. There is no alt-bot mode. | `.bot logout` an alt you no longer want. |
| The engine is not built in | `.tb engine <bot> on` refuses with `the engine is not built in`; every engine command answers the same. | Rebuild with `TORTOISEBOTS_SLICE1=ON`. |

## What bots cannot do

- Turn the character **you are playing** into a bot. Bots are separate sessions on your account or free bots the server runs.
- Chat with players on their own. They answer orders and the questions the engine knows; there is no conversation model.
- Hold a raid together without a player or a GameMaster: the run leader is a GameMaster command, and the boss rules it would need are not ported.
- Play on the opposite faction's side of a battleground queue, or fill Alterac Valley.
