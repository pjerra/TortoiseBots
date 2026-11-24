---
id: server-settings-party
title: Party & Quality-of-Life Settings
category: server
summary: Configuration settings for owned bots, alt level sync, follow behaviour and formations, maintenance, autogear, party loot and quest sync.
tags: [config, party, alts, qol]
relates_to:
  - play-commands-party-actions
  - server-settings-hiring
---

# Party & Quality-of-Life Settings

These settings configure the bots a player logs in as their own characters (`.bot add`), how they follow, the party maintenance and autogear commands, and a few party conveniences. Settings go in `aiplayerbot.conf`; see [How configuration works](../configuration.md).

The bot engine reads `aiplayerbot.conf` once, when it first starts a bot, so a change needs a server restart. One key on this page is the exception: `AiPlayerbot.OwnedBotSummonCooldown` is read from the module config (`worldserver.conf.d/tortoisebots.conf`), and `.reload config` applies it.

> **Which bots these settings apply to.** They shape the bot engine. Every bot the pool logs in, and every character a player adds with `.bot add`, runs the engine. A bot a GameMaster logs in with `.tb login <character> <master>` runs it only with `TortoiseBots.Slice.EngineByDefault = 1` in `tortoisebots.conf`, or after `.tb engine <character> on`.

## Summary

| Setting | Default | What it does |
| :--- | :---: | :--- |
| [`AiPlayerbot.SyncAltLevelToMaster`](#aiplayerbotsyncaltleveltomaster) | `0` | Levels a bot in your group up to your level. |
| [`AiPlayerbot.BoostFollow`](#aiplayerbotboostfollow) | `1` | Lets a bot far behind mount up to catch up. |
| [`AiPlayerbot.DungeonFollowHoldBeforePacks`](#aiplayerbotdungeonfollowholdbeforepacks) | `0` | Stops a follower short of an unpulled pack in a dungeon. |
| [`AiPlayerbot.DefaultFormation`](#aiplayerbotdefaultformation) | `chaos` | The idle formation bots take around the leader. |
| [`AiPlayerbot.RaidFollowWedge`](#aiplayerbotraidfollowwedge) | `0` | Spreads instance followers behind the leader and has stragglers walk its path. |
| [`AiPlayerbot.TeleportLandingFall`](#aiplayerbotteleportlandingfall) | `0` | Drops a bot to the floor after a teleport that ends in mid-air. |
| [`AiPlayerbot.OwnedBotMaintenanceEnabled`](#aiplayerbotownedbotmaintenanceenabled) | `0` | Enables `.bot maintenance`. |
| [`AiPlayerbot.OwnedBotAutogearEnabled`](#aiplayerbotownedbotautogearenabled) | `0` | Enables `.bot autogear`. |
| [`AiPlayerbot.OwnedBotAutogearQualityCap`](#aiplayerbotownedbotautogearqualitycap) | `2` | Highest item quality `.bot autogear` may give. |
| [`AiPlayerbot.OwnedBotAutogearIlvlCap`](#aiplayerbotownedbotautogearilvlcap) | `0` | Highest item level `.bot autogear` may give. |
| [`AiPlayerbot.PlayerAutogearForAll`](#aiplayerbotplayerautogearforall) | `0` | Lets every player run `.autogear` on themselves instead of GMs only. |
| [`AiPlayerbot.OwnedBotSummonCooldown`](#aiplayerbotownedbotsummoncooldown) | `300` | Seconds between two `.bot summon` uses for one bot. |
| [`AiPlayerbot.RollBadItemsWithPlayer`](#aiplayerbotrollbaditemswithplayer) | `0` | Lets bots need-roll filler equipment instead of greeding it. |
| [`AiPlayerbot.SyncQuestForPlayer`](#aiplayerbotsyncquestforplayer) | `0` | Completes your copy of a quest when your bot turns it in. |
| [`AiPlayerbot.SyncQuestWithPlayer`](#aiplayerbotsyncquestwithplayer) | `0` | Completes a bot's quest when you turn it in. |

## Owned bots

A player logs one of their own characters in as a bot with `.bot add <name>`. It joins their party and follows them; `.bot logout <name>` sends it away. Both characters must be on the same Battle.net account, the player must lead the group (and the party must have room), and one player may run only a few at once (`TortoiseBots.Player.MaxBots`, default 4). See [Roster and character commands](../../play/commands/roster-and-hiring.md).

### `AiPlayerbot.SyncAltLevelToMaster`
Levels a bot in your group up to your level.
**Default:** `0` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

While a bot is in your group, you are the group leader, and you are alive, the bot gains one character level per AI update until it matches your level (never past the server's maximum level). Each gain writes an `AltLevelSync` row to `bot_events.csv` when that log is enabled.

The bot must be in your group to benefit; a bot that is merely following you is not levelled.

</details>

## Follow and formations

### `AiPlayerbot.BoostFollow`
Lets a bot that has fallen far behind mount up and catch up.
**Default:** `1` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

When a follower is farther than `ReactDistance` (150 yd by default) behind its leader and is not mounted and not in combat, it asks the mount logic to mount up. It then rides until it is back at normal follow distance. This uses normal mounts and normal speed; it is not a speed cheat.

</details>

### `AiPlayerbot.DungeonFollowHoldBeforePacks`
Stops a follower before an unpulled pack in a dungeon.
**Default:** `0` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Inside a dungeon or raid, with the bot out of combat and its leader more than 30 yards ahead, the bot walks the route to the leader itself instead of using the core follow. If an unpulled hostile creature stands on that route, the bot stops 3 yards outside its aggro radius and waits. It stays on until the leader is closer than 25 yards, the pack is gone, or the route changes.

While waiting it whispers its master once per pack, at most every 30 seconds: `Pack in the way: <name>. Waiting here - pull it or come back for me.` The check runs once per second and is suspended on a transport.

</details>

### `AiPlayerbot.DefaultFormation`
The idle formation bots take around the party leader.
**Default:** `chaos` · **Values:** `near`, `melee`, `queue`, `chaos`, `circle`, `line`, `shield`, `arrow`, `far` (also `default` = `near`, and `custom`) · **Restart:** needed

<details>
<summary>How it works in detail</summary>

- `chaos`: a loose cluster with small random offsets, so bots do not stack.
- `near`: a tight circle inside melee reach.
- `melee`: follow positions keyed to the leader's target.
- `far`: holds the follow angle at a longer range.
- `circle`: an evenly spaced ring around the leader.
- `line`: a line abreast of the leader.
- `shield`: role-aware lines, tanks forward and the rest behind.
- `arrow`: an arrowhead behind the leader's facing.
- `queue`: a single-file column.

Each bot keeps this formation while it follows; a dungeon or raid can override it with `RaidFollowWedge`.

</details>

### `AiPlayerbot.RaidFollowWedge`
Spreads instance followers behind the leader and makes stragglers walk the leader's path.
**Default:** `0` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Inside a dungeon or raid, the chosen formation gives way to a `near` formation spread over a 150-degree arc directly behind the leader, so no slot is ahead of or beside the tank. The follow distance is `AiPlayerbot.RaidFollowDistance` (4 yd).

With more than ten followers the non-tank, non-healer bots use a second ring 3 yards further back; tanks and healers stay on the inner ring.

A follower more than 20 yards behind the leader, with neither in combat, walks the leader's recorded recent positions instead of the direct line to its slot, so stairs and corners are taken the way the leader took them. The open world keeps the configured formation.

</details>

### `AiPlayerbot.TeleportLandingFall`
Drops a bot to the floor after a teleport that ends in mid-air.
**Default:** `0` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

A player's client makes the character fall to the floor after the server moves it. A bot has no client, so a teleport that ends above the floor can leave it hovering. A known case is Jin'do's Banish in Zul'Gurub, which drops a character over the pit floor; melee bots then hovered there and never walked back into the fight.

With this on, every near teleport looks for the floor under its destination (navigation mesh and map floor); a floor taken from the map alone counts only within `AiPlayerbot.JumpHeightLimit`. The bot then plays the landing fall: it hovers for about the fall time plus a second, lands and carries on. A destination less than a yard above its floor does not fall, so an ordinary summon or revive is unaffected. Because it changes every near teleport, it is off by default.

</details>

## Maintenance and gear

### `AiPlayerbot.OwnedBotMaintenanceEnabled`
Enables the `.bot maintenance` command.
**Default:** `0` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

`.bot maintenance <bot>` restocks the bot's class reagents and the cloth bandages for its First Aid skill, tops up its ammunition, repairs all equipped gear and saves the character. Potions, food, thrown weapons and other consumables are not restocked.

With this off the command answers that maintenance is disabled. An owner or a GameMaster may run it on an owned bot; random pool bots are refused.

</details>

### `AiPlayerbot.OwnedBotAutogearEnabled`
Enables the `.bot autogear` command.
**Default:** `0` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

`.bot autogear <bot> [quality|ilvl]` equips a bot with better items from the same gear pool a new bot is geared from, choosing by its class and spec. It only upgrades: the replaced item goes to the bot's bags, or to its mailbox when the bags are full. An owner gears their own bots; a GameMaster may gear any bot. Pool bots, dead bots and bots in an active dungeon run are refused.

With this off the command answers that owned-bot autogear is disabled. Random pool bots are not geared this way.

</details>

### `AiPlayerbot.OwnedBotAutogearQualityCap`
Highest item quality `.bot autogear` may give.
**Default:** `2` · **Values:** `0` poor, `1` common, `2` uncommon, `3` rare, `4` epic, `5` legendary · **Restart:** needed

<details>
<summary>How it works in detail</summary>

A quality named in the command is clamped to this ceiling. The default `2` keeps autogear to uncommon items.

</details>

### `AiPlayerbot.OwnedBotAutogearIlvlCap`
Highest item level `.bot autogear` may give.
**Default:** `0` (no cap) · **Values:** item level · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Limits candidate items to this item level or lower. `0` leaves the character level and the quality cap as the only limits; an item level named in the command is clamped to this ceiling.

</details>

### `AiPlayerbot.PlayerAutogearForAll`
Lets every player gear themselves with `.autogear`.
**Default:** `0` · **Values:** `0` GMs only, `1` every player on themselves · **Restart:** needed

<details>
<summary>How it works in detail</summary>

`.autogear [player name] <spec> [ilvl]` equips a real player from the same gear pool a spawned bot of that level gets, with the same scoring and limits. It only upgrades; replaced items go to the bags or the mailbox.

- `0`: only a GameMaster can run it, on any online player including themselves.
- `1`: every player can run it, on themselves only.

Characters on a challenge mode stay GM-only whatever this says. Bots are never geared by this command; owned bots use `.bot autogear`.

</details>

### `AiPlayerbot.OwnedBotSummonCooldown`
Seconds between two `.bot summon` uses for the same bot.
**Default:** `300` · **Values:** seconds (`0` to `3600`; `0` no wait) · **Restart:** not needed (`.reload config` applies it)

On WoW Forever this key is read from `worldserver.conf` or `worldserver.conf.d/tortoisebots.conf`, not from `aiplayerbot.conf`.

<details>
<summary>How it works in detail</summary>

`.bot summon <bot>` brings a bot to its master. The caller must be that bot's master (a GameMaster may summon any bot), and after a use the same bot cannot be summoned again for this many seconds, so it cannot be used in a loop.

</details>

## Party loot and quests

### `AiPlayerbot.RollBadItemsWithPlayer`
Lets a bot need-roll equipment that is only filler for its slot.
**Default:** `0` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Item scoring labels some drops as filler for the bot's spec. With this on, the bot rolls Need on them; with it off, a bot that has a real player master greeds them instead (a bot without a master still needs them). A real upgrade is always a Need roll regardless of this setting.

</details>

### `AiPlayerbot.SyncQuestForPlayer`
Completes your copy of a quest when your bot turns it in.
**Default:** `0` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

When a bot turns in a quest and its master is on the same quest (incomplete or failed), the master's copy is completed too.

</details>

### `AiPlayerbot.SyncQuestWithPlayer`
Completes a bot's quest when you turn it in.
**Default:** `0` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

When the master has a quest complete and the bot's copy is still incomplete or failed, the bot's copy is completed at its next turn-in. Bots then do not pick up quest items they no longer need.

</details>

## Not in this build

These settings from the Turtle WoW version have no effect in the WoW Forever build:

- **Autonomous alts** (`AltBots`, `AltBotsLogoutWithOwner`): the autonomous-alt service is not part of this build, so these two keys change nothing.
- **Automatic summon on group invite** (`OwnedBotSummonWhenGroup`, `OwnedBotSummonAllowInCombat`, `OwnedBotSummonAllowMasterDead`, `OwnedBotSummonAllowBotDead`, `OwnedBotSummonRevive`, `OwnedBotSummonRepair`): use `.bot summon <bot>` instead.
- **Innkeeper summoning** (`SummonAtInnkeepersEnabled`) and the free non-GM instant summon (`NonGmFreeSummon`): `.bot summon` has its own ownership checks.
- **Cheat flags** (`BotCheats`, `RndBotCheats`): bots play by normal game rules here; none of the cheat keywords is applied.
