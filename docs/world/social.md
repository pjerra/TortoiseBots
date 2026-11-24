---
id: world-social
title: Social interaction & guilds
category: world
summary: How pool bots greet players, buff them, group for shared quests, join and run guilds, and trade with their owner.
tags: [world, social, guilds, trading, greetings, buffs]
relates_to:
  - world-living-world
  - play-commands-roster-and-hiring
  - server-settings-world-population
---

# Social interaction & guilds

This page describes how free pool bots greet players, buff them on the road, group up and take part in guilds, and how you trade with a bot you own. Social settings live in `aiplayerbot.conf` and `tortoisebots.conf`; see [World population & immersion settings](../server/settings/world-population.md).

## Greetings & emotes

Bots acknowledge a passing player with the `hello` text emote:

- **Who greets:** a bot with a real player as its master greets without a setting. A masterless pool bot greets only when [`AiPlayerbot.RandomBotGreet`](../server/settings/world-population.md#aiplayerbotrandombotgreet) is on, because hundreds of pool bots in one starting zone would otherwise answer a visitor with a wall of waves.
- **Never bot to bot:** a bot never greets another bot. Only real players count as a new face.
- **Once per meeting:** a player already in sight is not greeted again. Leaving and coming back counts as a new meeting, and [`AiPlayerbot.GreetCooldown`](../server/settings/world-population.md#aiplayerbotgreetcooldown) (600 s by default) spaces repeat greetings to the same player.
- **Switch:** the greet row is enabled by [`AiPlayerbot.EnableGreet`](../server/settings/world-population.md#aiplayerbotenablegreet).
- Bots also emote on their own now and then while idle, unless a real player is their master.

## Passing buffs

A pool bot passing a real player casts a class buff on them and keeps going, like a friendly player running past. Only instant casts: the bot never stops, turns around or leaves its travel.

- **Who buffs:** masterless pool bots only (no group with the player, no master, out of combat, alive, not on a taxi, standing up, enough mana left). Targets are real players only, same faction, alive, not grouped with the bot, in line of sight and within about 30 yards. Invisible and hidden GameMaster players are skipped.
- **Which buff:** the class buff the bot hands out — priest Power Word: Fortitude, druid Mark of the Wild, mage Arcane Intellect (mana users only), paladin a Blessing that fits the player's class (Kings for tanks, Wisdom for healers and hunters, Might for melee). Highest rank the bot knows that is allowed on the player's level, and only when the player lacks it.
- **Chance and cooldown:** 1 in 5 per meeting, then ten minutes before the same bot may buff the same player again.
- **Passing heal:** a player below 35% health gets Renew (priest) or Rejuvenation (druid) when they lack it. Chance 1 in 2, 60-second cooldown per bot and player. A passing bot never resurrects anyone.
- **Setting:** [`AiPlayerbot.PassingBuffs`](../server/settings/world-population.md#aiplayerbotpassingbuffs), on by default.

<a id="grouping--party-invites"></a>
## Grouping

Bots do not invite players into groups, and they do not start roaming parties of their own. What they do:

| What happens | Details | Setting |
| :--- | :--- | :--- |
| **Quest parties** | Free bots standing near each other (within 120 yards, levels within five) that hold the same group or elite quest form a party of up to five, and disband it when the quest is done or after 15 minutes. Only bots join these parties. | `TortoiseBots.World.GroupQuests` (on) |
| **Food and water** | A mage in your party conjures food and water and trades it to members who have none (water only to mana users), when its mana is above the low-mana line. | Always on with the mage's buff kit |
| **Your group orders** | In your party a bot follows and fights for you; see [Party actions](../play/commands/party-actions.md). | — |

## Guilds

Bots take part in guild life when `TortoiseBots.World.Guilds` is on (it is on by default):

- **Accepting invites:** a free bot accepts a guild invite from its own faction, and declines one from the other side or one whose guild already holds 1000 members. An unreadable invitation is cleared rather than left hanging.
- **The 1000-member line:** at 1000 members a pool bot leaves the guild, so bots never fill the last seats a real player could use. The number is built in, not a setting.
- **Founding their own guilds:** off by default (`TortoiseBots.World.GuildForming`). When it is on, a free bot above level 10 with the charter price in its own purse, no guild and no pending invite buys a charter at a nearby petitioner, gathers signatures from nearby free bots of its own faction and turns it in, founding `<Bot> Guild`.
- **Guild whispers:** the bot's master (or a GameMaster) can whisper it `guild invite <name>`, `guild join`, `guild leave`, `guild promote <name>`, `guild demote <name>`, `guild leader <name>` and `guild remove <name>`. Inviting, promoting, demoting and removing need the bot to hold the matching rank right; passing leadership needs the bot to be the guild leader. Anybody else is told to ask the owner. See [Whisper commands](../play/commands/whispers.md#guild-management).
- **Guild mail:** with `TortoiseBots.World.GuildMail` on (default), free bots in a bot guild mail surplus gold to the poorest online bot member and unused crafting materials to the bot crafters who use them. Real players' mail is never read or touched, and a guild that also holds real players is skipped unless `TortoiseBots.World.GuildMail.AllowMixedGuild` is turned on.
- **Pool resets keep player guilds:** a pool reset stops when a guild led by a pool bot has even one member outside the pool. See [Pool reset safety](../server/bot-pool.md#safety-guarantees--multi-phase-execution).

<a id="trading-with-pool-bots"></a>
## Trading with a bot you own

A bot never answers a trade request on its own. The bot's owner drives the window through the TortoiseBotsForeverManager addon's trade action: select the bot (or name it) and the bot opens the window and accepts when you accept.

- **Who may trade:** the bot's owner (any GameMaster too). A stranger's trade request to a pool bot gets no answer and never completes; there is no setting that opens pool bots to strangers.
- **Your own party bots:** the same rule. Alts trade with their owner, and with nobody else.
- **During a dungeon run** the bot belongs to the run and the addon's trade action is refused for every caller, GameMasters included.
