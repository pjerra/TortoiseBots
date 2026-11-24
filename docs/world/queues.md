---
id: world-queues
title: Dungeon & battleground queues
category: world
summary: How pool bots fill a player's dungeon party through the module's own queue and play Warsong Gulch and Arathi Basin.
tags: [world, queues, dungeons, battlegrounds, pvp]
relates_to:
  - world-living-world
  - play-dungeons-and-raids
  - server-settings-queues
---

# Dungeon & battleground queues

This page explains how free pool bots fill a player's dungeon party and how they take part in battlegrounds. Their settings live in `aiplayerbot.conf` and `tortoisebots.conf`; see [Queue settings](../server/settings/queues.md).

<a id="looking-for-turtles-lft-dungeon-autofill"></a>
## Dungeon fill: `.bot lfg`

The 1.60 client has no dungeon-finder window. The module runs its own queue, and you fill it from chat:

| Command | What it does |
| :--- | :--- |
| `.bot lfg join <dungeon id> <tank\|healer\|dps>` | Puts **you** in the fill queue for that dungeon. You must lead your party (or be alone); a raid and a full party of five are refused. |
| `.bot lfg status` | Shows your entry: dungeon, role, the slots still missing and the bots already with you. |
| `.bot lfg leave` | Leaves the queue and sends every bot it grouped with you back to its free life. |

GameMasters have the same three verbs as `.tb lfg join|status|leave`; `.tb lfg status` shows every waiting player.

Filling is demand-driven: with nobody waiting in the queue, no bot is grouped. The service is on by default ([`AiPlayerbot.RandomBotLftEnabled`](../server/settings/queues.md#aiplayerbotrandombotlftenabled)) and evaluates the queue every 15 s by default, taking at most one bot per pass.

### What the fill builds
- A party is one tank, one healer and three damage dealers. Your own chosen role takes its slot, so a solo damage dealer gets a tank, a healer and two other damage bots.
- Bots come from the free pool of **your faction**: masterless, ungrouped, alive, level 10 or more, standing in the world, not in a battleground or its queue, not teleporting, not on a flight and not in combat.
- A bot fills a role its own spec covers: a tanking spec for the tank slot, a healing spec for the healer slot, anything for damage. With [`AiPlayerbot.RandomBotLftAllowRoleBorrow`](../server/settings/queues.md#aiplayerbotrandombotlftallowroleborrow) on, any class that has the kit may be pulled into a missing role instead.
- The bot joins through the core's own invite and accept, wears the group's role bit, and is rebuilt into the role's kit (the same rebuild `.tb role` runs). When the party breaks up or you leave the queue, its role and group are cleared again.

The fill never teleports anyone: the party walks to the instance portal. To bring a straggler, use [`.bot summon`](../play/commands/roster-and-hiring.md#bot-summon).

<a id="battleground-auto-queue-human-demand"></a>
## Battlegrounds: Warsong Gulch and Arathi Basin

Bot battlegrounds are a separate round, **off by default** ([`TortoiseBots.Bg.Enable`](../server/settings/queues.md#tortoisebotsbgenable)). With it on:

- **Two maps only:** Warsong Gulch and Arathi Basin ([`TortoiseBots.Bg.Maps`](../server/settings/queues.md#tortoisebotsbgmaps)). Alterac Valley, the arenas and the random queue are not filled.
- **Human demand first:** while a real player waits in a WSG or AB queue, pool bots of that bracket and faction queue on **both** sides until each side holds the module minimum plus one (five plus one by default). At most five bots per side enter the queue per five-second check.
- **No half match:** the core is not allowed to start an instance before both sides reach that target, so a fill does not open a 1v1 and leave the rest waiting. A side that stays short for three minutes gets one system line to the waiting players, naming the side and its count.
- **Invitations:** a queued bot accepts after a short delay. A bot whose human leaves the queue or the world leaves with them, and a queue entry nobody wants is dropped.
- **Caps:** at most two instances hold bots, and at most fifty bots are held for battlegrounds server-wide, counting queued, invited and inside.
- **Leaving:** bots leave a finished match instead of waiting for the instance to shut down, and a bot still inside after 45 minutes leaves without the Deserter debuff.
- **After a death:** the bot releases at once, walks to the area spirit healer and waits there to be revived in the core's resurrection waves.

Bot objective play is not there yet: a bot in a match fights the players who attack it, but it does not run flags, capture nodes, defend objectives or follow a leader.

<a id="battlegrounds-with-your-own-party-bots"></a>
## Joining a battleground with your own bots

Your own bots (alts) queue the normal way through the battlemaster's **Join as Group**: the core queues every member of your party. In a match they follow you and fight alongside you, and they leave with you. The fill never queues these bots itself, but a party bot inside a match counts toward `TortoiseBots.Bg.MaxBots`.

<a id="autonomous-bot-only-matches"></a>
## Not on WoW Forever yet

- The Looking for Turtles dungeon tool; the module's `.bot lfg` queue replaces it.
- Bot-only battleground matches with no real player in the queue.
- Warsong Gulch flag roles and Arathi Basin node strategies.
- Alterac Valley, Sunnyglade Valley and the Blood Ring arena fills.
