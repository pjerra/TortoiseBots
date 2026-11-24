---
id: server-settings-combat
title: Combat & Reaction Thresholds
category: server
summary: Configuration thresholds for health, mana, engagement distances, casting pacing, pull rules, and open-world PvP for bot AI.
tags: [config, combat, thresholds, health, mana, distance, pvp]
relates_to:
  - classes-overview
  - play-commands-party-actions
---

# Combat & Reaction Thresholds

These settings configure health and mana trigger percentages, engagement ranges, casting delays, tank pull mechanics, and the open-world PvP behaviour of the bot pool. Settings go in `aiplayerbot.conf`; see [How configuration works](../configuration.md).

The bot engine reads `aiplayerbot.conf` once, when it first starts a bot. A change to any setting on this page needs a server restart.

> **Which bots these settings apply to.** They shape the bot engine. Every bot the pool logs in, and every character a player adds with `.bot add`, runs the engine. A bot a GameMaster logs in with `.tb login <character> <master>` runs it only with `TortoiseBots.Slice.EngineByDefault = 1` in `tortoisebots.conf`, or after `.tb engine <character> on`.

## Summary

| Setting | Default | What it does |
| :--- | :---: | :--- |
| [`AiPlayerbot.AlmostFullHealth`](#aiplayerbotalmostfullhealth) | `90` | Health percentage above which bots stop routine heals to conserve mana. |
| [`AiPlayerbot.CriticalHealth`](#aiplayerbotcriticalhealth) | `20` | Health percentage treated as an emergency, triggering survival abilities. |
| [`AiPlayerbot.LowHealth`](#aiplayerbotlowhealth) | `50` | Health percentage that triggers the healer's heavy heals. |
| [`AiPlayerbot.MediumHealth`](#aiplayerbotmediumhealth) | `70` | Health percentage that triggers upkeep heals. |
| [`AiPlayerbot.LowMana`](#aiplayerbotlowmana) | `15` | Mana percentage where casters recover mana instead of spending it. |
| [`AiPlayerbot.MediumMana`](#aiplayerbotmediummana) | `40` | Mana percentage where bots get careful with expensive abilities. |
| [`AiPlayerbot.FollowDistance`](#aiplayerbotfollowdistance) | `1.5` | Following distance in yards, on foot, in the open world. |
| [`AiPlayerbot.RaidFollowDistance`](#aiplayerbotraidfollowdistance) | `4.0` | Following distance in yards inside dungeons and raids. |
| [`AiPlayerbot.SightDistance`](#aiplayerbotsightdistance) | `60.0` | How far a bot looks for enemies, and the leash of a `guard` bot. |
| [`AiPlayerbot.SpellDistance`](#aiplayerbotspelldistance) | `26.0` | Maximum range in yards for starting a ranged spell attack. |
| [`AiPlayerbot.ReactDistance`](#aiplayerbotreactdistance) | `150.0` | Distance in yards within which a bot answers the leader and combat calls. |
| [`AiPlayerbot.LootDistance`](#aiplayerbotlootdistance) | `25.0` | How far from the master a corpse may be before bots skip it. |
| [`AiPlayerbot.AoeRadius`](#aiplayerbotaoeradius) | `5.0` | Default yard radius for area damage and cluster checks. |
| [`AiPlayerbot.ShareTargets`](#aiplayerbotsharetargets) | `1` | Lets nearby friendly bots share their list of attackers. |
| [`AiPlayerbot.GlobalCooldown`](#aiplayerbotglobalcooldown) | `1500` | Milliseconds between rapid spell casts. |
| [`AiPlayerbot.InterruptIgnoresThreat`](#aiplayerbotinterruptignoresthreat) | `0` | Lets pure interrupts ignore the threat back-off. |
| [`AiPlayerbot.InterruptAllowStun`](#aiplayerbotinterruptallowstun) | `0` | Counts a stun as an interrupt. |
| [`AiPlayerbot.SpellFacingPrecheck`](#aiplayerbotspellfacingprecheck) | `0` | Refuses impossible-facing casts (Gouge, Backstab) before asking the core. |
| [`AiPlayerbot.RangedPull`](#aiplayerbotrangedpull) | `1` | Lets a tank open a commanded pull from range. |
| [`AiPlayerbot.RangedPullMeleeApproach`](#aiplayerbotrangedpullmeleeapproach) | `20.0` | Distance in yards beyond which a commanded pull opens from range. |
| [`AiPlayerbot.PullBackDpsJoinDelay`](#aiplayerbotpullbackdpsjoindelay) | `5` | Seconds party DPS wait during a pullback. |
| [`AiPlayerbot.PullBackMaxReturnTime`](#aiplayerbotpullbackmaxreturntime) | `15` | Seconds a tank has to get back to the group during a pullback. |
| [`AiPlayerbot.BotAcceptDuelMinimumLevel`](#aiplayerbotbotacceptduelminimumlevel) | `10` | Lowest bot level that accepts a duel. |
| [`AiPlayerbot.RandomBotForcePvp`](#aiplayerbotrandombotforcepvp) | `1` | Pool bots turn on the PvP flag at login. |
| [`AiPlayerbot.RandomBotForcePvpPercent`](#aiplayerbotrandombotforcepvppercent) | `50` | Share of pool bots that carry that flag. |
| [`AiPlayerbot.WorldPvpSeek`](#aiplayerbotworldpvpseek) | `2` | Free pool bots look for flagged enemies to attack; `0` off. |
| [`AiPlayerbot.WorldPvpSeekBotPercent`](#aiplayerbotworldpvpseekbotpercent) | `25` | Share of flagged free pool bots that get the seek role. |
| [`AiPlayerbot.WorldPvpSeekRerollMinutes`](#aiplayerbotworldpvpseekrerollminutes) | `30` | Minutes between seek-role re-rolls. |

## Health and mana thresholds

### `AiPlayerbot.AlmostFullHealth`
Health percentage at or above which a healer stops routine healing of that target.
**Default:** `90` · **Values:** `1` to `100` percent · **Restart:** needed

<details>
<summary>How it works in detail</summary>

While a party member is at or above this health, bots hold back routine heals so the mana is there when it matters. Heals whose spell row is only armed below this line are not cast at all.

</details>

### `AiPlayerbot.CriticalHealth`
Health percentage treated as an emergency.
**Default:** `20` · **Values:** `1` to `100` percent · **Restart:** needed

<details>
<summary>How it works in detail</summary>

At or below this health, bots use their survival tools:
- paladins cast *Divine Shield* or *Lay on Hands*;
- protection warriors use *Last Stand*;
- healers use their biggest direct heal.

</details>

### `AiPlayerbot.LowHealth`
Health percentage that triggers heavy healing.
**Default:** `50` · **Values:** `1` to `100` percent · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Below this line a healer reaches for its heavy direct heal rather than a cheap top-up, and a protection warrior uses *Shield Wall*. Raise it for dangerous dungeons or hard raid bosses; lower it to save mana on easy content.

</details>

### `AiPlayerbot.MediumHealth`
Health percentage that triggers upkeep healing.
**Default:** `70` · **Values:** `1` to `100` percent · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Between this line and `AlmostFullHealth`, healers use their cheap, fast heals to keep the group topped up.

</details>

### `AiPlayerbot.LowMana`
Mana percentage where casters stop spending and recover instead.
**Default:** `15` · **Values:** `1` to `100` percent · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Below this line the "low mana" trigger fires: mages use *Evocation* and warlocks use *Life Tap*.

</details>

### `AiPlayerbot.MediumMana`
Mana percentage where bots start pacing their expensive abilities.
**Default:** `40` · **Values:** `1` to `100` percent · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Below this line the "medium mana" trigger fires. A mana potion (or a warlock's *Dark Rune*) may be used here rather than at low mana, and a healer whose own mana is under this line stops using its inefficient heals and saves the mana for the ones that matter. A mana-restore trinket arms below this line too.

</details>

## Distances

### `AiPlayerbot.FollowDistance`
Following distance in yards between a follower bot and its leader, on foot.
**Default:** `1.5` · **Values:** positive float · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Sets how tightly a bot follows on foot in the open world. A mounted bot out of combat outside an instance adds 3 yards to this distance.

</details>

### `AiPlayerbot.RaidFollowDistance`
Following distance in yards inside dungeons and raids.
**Default:** `4.0` · **Values:** positive float · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Replaces `FollowDistance` inside instances so the party does not stack on the leader. The free-move (`follow`) behaviour also uses it as its range there.

</details>

### `AiPlayerbot.SightDistance`
How far a bot looks for enemies, and the radius a `guard` bot patrols.
**Default:** `60.0` · **Values:** positive float · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Sets the perception distance for hostile creatures, and the leash of a bot holding a `guard` position.

</details>

### `AiPlayerbot.SpellDistance`
Maximum range in yards at which a ranged bot starts a spell attack.
**Default:** `26.0` · **Values:** positive float · **Restart:** needed

<details>
<summary>How it works in detail</summary>

The default engagement range for ranged casters and several reach checks (dispels, totems, snare and healer targets). Bots walk closer until the target is inside this range.

</details>

### `AiPlayerbot.ReactDistance`
How far in yards a bot stays responsive to its leader and to combat calls.
**Default:** `150.0` · **Values:** positive float · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Several follow and movement checks use this radius to decide that a bot is far behind: past it, a follower can mount up to catch up (see [`AiPlayerbot.BoostFollow`](party.md#aiplayerbotboostfollow)), and a long walk's path is shortened where it safely can be.

</details>

### `AiPlayerbot.LootDistance`
How far from the master a corpse may lie for bots to loot it.
**Default:** `25.0` · **Values:** positive float · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Corpses farther from the master than this are dropped from the loot list. A grouped bot without an active player master uses the shorter `GroupMemberLootDistance` (15 yd).

</details>

### `AiPlayerbot.AoeRadius`
Default radius in yards for area damage and cluster checks.
**Default:** `5.0` · **Values:** distance in yards · **Restart:** needed

<details>
<summary>How it works in detail</summary>

The yardstick for ground-targeted spells (Blizzard, Rain of Fire), multi-target decisions and "enough enemies grouped?" checks. Individual rows scale it for their own scans.

</details>

## Targeting, casting and threat

### `AiPlayerbot.ShareTargets`
Lets nearby friendly bots share their list of attackers.
**Default:** `1` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

With this on, a bot also counts enemies that friendly bots within 10 yards are fighting, so the party defends itself as a group. Set it to `0` to make every bot find its own attackers.

</details>

### `AiPlayerbot.GlobalCooldown`
Milliseconds between two rapid spell casts.
**Default:** `1500` · **Values:** positive integer · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Paces consecutive casts and several item, potion and aura uses so a bot does not queue a burst of instant abilities in one server tick.

</details>

### `AiPlayerbot.InterruptIgnoresThreat`
Lets pure interrupts ignore the threat back-off, the way heals already do.
**Default:** `0` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

A bot with the `threat` strategy in a group normally stops single-target spell casts when its threat on the target reaches 80 % of the tank's. That rule also covered interrupt rows (including `on enemy healer`), so a damage dealer over the line let enemy casts finish. With this on, Kick, Pummel, Shield Bash, Bash, Counterspell and Silence are exempt while the bot's threat plus the interrupt's flat threat stays under the pull line (110 % of the tank's threat in melee, 130 % at range). A vetoed interrupt logs a `ThreatCapInterrupt` row. Off by default, so a shipped configuration plays as before. Recommended on.

</details>

### `AiPlayerbot.InterruptAllowStun`
Counts a stun as an interrupt.
**Default:** `0` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

With this on, stun spells carrying a `MOD_STUN` aura effect (Bash, Hammer of Justice, War Stomp, Repentance) and Intercept (`20252`, whose triggered effect stuns) interrupt a cast the target is not immune to. The core already stops a non-melee cast when a stun lands, so the victim's cast needs no special interrupt flag.

A stun is never picked for a stun-immune target, and a boss counts as unstunnable. Recommended on.

</details>

### `AiPlayerbot.SpellFacingPrecheck`
Refuses a cast the target's facing makes impossible, before asking the core.
**Default:** `0` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Two casts fail because of where the target looks: a from-behind-only spell such as Backstab while the target faces the bot, and Gouge while it does not. The old cast check found this out by asking the core on every attempt, which sent a spell failure to nearby clients each time, and it read the Gouge answer as castable. With this on the precheck runs before the core's other cast checks. It can only refuse a cast that the core always refuses on facing. Recommended on.

</details>

## Pulls

### `AiPlayerbot.RangedPull`
Lets a tank open a commanded pull with a ranged weapon or spell.
**Default:** `1` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

During a commanded pull (`.tb order ... pull`), the tank decides once, when the order arrives: with this on and the target in line of sight and inside the weapon's or pull spell's range, the tank shoots if the target is farther than `RangedPullMeleeApproach` or has no walking path. An order for a body pull always walks in, and a pullback always opens from range.

The bot needs the `pull` combat strategy, which a tank role gives it. With this off, only a pullback or an order that explicitly asks for a ranged opening shoots.

</details>

### `AiPlayerbot.RangedPullMeleeApproach`
Distance in yards beyond which a commanded pull opens from range.
**Default:** `20.0` · **Values:** positive float · **Restart:** needed

<details>
<summary>How it works in detail</summary>

A target farther away than this (and in sight and in shot range) is shot instead of walked up to, which avoids body-pulling packs on the way. Needs `RangedPull = 1`.

</details>

### `AiPlayerbot.PullBackDpsJoinDelay`
Seconds party DPS wait during a pullback before they engage.
**Default:** `5` · **Values:** seconds · **Restart:** needed

<details>
<summary>How it works in detail</summary>

During `.tb order ... pullback`, damage dealers hold their fire while the tank walks back to the group. The window starts when the tank is back at the anchor and takes the fight over. A per-order `[seconds]` (0–60) overrides the default.

</details>

### `AiPlayerbot.PullBackMaxReturnTime`
Seconds a tank has to get back to the group during a pullback.
**Default:** `15` · **Values:** seconds · **Restart:** needed

<details>
<summary>How it works in detail</summary>

The safety timeout of a commanded pullback: the hold covers this return leg plus the join delay. If the tank cannot get back within it, the maneuver ends and normal combat resumes.

</details>

## Duels

### `AiPlayerbot.BotAcceptDuelMinimumLevel`
Lowest bot level that accepts a duel.
**Default:** `10` · **Values:** `1` to `255` · **Restart:** needed

<details>
<summary>How it works in detail</summary>

A bot accepts a duel when it is alive, out of combat and at least this level. A challenger other than the bot's own real player master must also leave the bot at 90 % health or more; the master is accepted at any health.

The bot attacks its opponent with its normal kit. A duel never kills: the core leaves the loser at 1 health. The level comes from `aiplayerbot.conf`, the file the bot engine reads (not `worldserver.conf`).

</details>

## Open-world PvP

### `AiPlayerbot.RandomBotForcePvp`
Pool bots turn on the PvP flag at login, so players can attack them anywhere.
**Default:** `1` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

This only flags the bot; it does not make the bot hunt anyone. Owned bots are never affected. With `0`, no pool bot is flagged at login, so players cannot attack a bot that has not flagged itself.

</details>

### `AiPlayerbot.RandomBotForcePvpPercent`
Share of the pool that gets that flag.
**Default:** `50` · **Values:** `0` to `100` percent · **Restart:** needed

<details>
<summary>How it works in detail</summary>

The pick is fixed per character, so the same bots carry the flag after every login. `100` flags every pool bot. With `RandomBotForcePvp = 0` nothing is flagged at login, so this share has no effect.

</details>

### `AiPlayerbot.WorldPvpSeek`
Free pool bots look for PvP-flagged enemy players nearby and attack them.
**Default:** `2` · **Values:** `0` off, `1` real players only, `2` players and bots · **Restart:** needed

<details>
<summary>How it works in detail</summary>

A seeking bot attacks an enemy player only when all of these hold:
- it is not in a battleground;
- the target is an enemy-faction player with the PvP flag on;
- the target's zone is not in `AiPlayerbot.PvpProhibitedZoneIds`;
- the target is within 20 yards (a longer attack range when the bot has more health), in line of sight, and less than 30 yards higher or lower;
- neither stands within 80 yards of a graveyard;
- the level-gap odds allow it: never a target 5 or more levels above, and the odds fall as the gap widens (25 % at 4 or more levels apart, 100 % at 1 or none).

Unflagged players, challenge-mode characters and invisible GameMasters are never attacked. Owned bots, bots with a real player master, and bots grouped with a real player never seek. A seeker chases a fleeing target for up to 60 seconds, ignores a player it killed for 10 minutes, and joins a fight within 30 yards that a friendly player is in.

</details>

### `AiPlayerbot.WorldPvpSeekBotPercent`
Share of the flagged free pool bots that get the seek role.
**Default:** `25` · **Values:** `0` to `100` percent · **Restart:** needed

<details>
<summary>How it works in detail</summary>

The role is handed out when a bot logs in or rebuilds its strategies, and rolled again about every `WorldPvpSeekRerollMinutes`. Ignored while seeking is off; `0` gives it to nobody. The share is rolled over the eligible free bots; the PvP flag share above is a separate pick.

</details>

### `AiPlayerbot.WorldPvpSeekRerollMinutes`
Minutes between re-rolls of the seek role.
**Default:** `30` · **Values:** minutes · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Each re-roll carries a few minutes of jitter so the pool does not change roles in lockstep, and a bot in combat keeps its role until the fight ends.

</details>
