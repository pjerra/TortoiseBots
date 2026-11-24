---
id: server-settings-dungeons-raids
title: Dungeon & Raid Settings
category: server
summary: "Settings for the run leader and for bot behaviour in dungeons: pulls and rests, retreats, charmed allies, threat lines, creature auras, the combat monitor and reach."
tags: [config, dungeons, raids, runs, hazards]
relates_to:
  - play-dungeons-and-raids
  - play-dungeon-reference
  - play-dungeon-run-leader
---

# Dungeon & Raid Settings

> Work in progress. These features are not officially released and not supported yet. Enable them at your own risk. Until the official release, bug reports and feedback about them are not handled through the issue tracker; contact Hypeer directly.

These settings shape how bots behave in dungeons and how the run leader paces a clear. They go in `aiplayerbot.conf`; see [How configuration works](../configuration.md). Commands are typed in game or in the server console with the `.tb` prefix, and the run leader's commands need Game Master rights.

Every value is read when the bot engine starts, so a change needs a server restart. A setting that this page does not list is not read by the ForeverCore build: writing it into the file changes nothing.

## Summary

| Setting | Default | What it does |
| :--- | :---: | :--- |
| [`AiPlayerbot.AvoidCreatureAuras`](#aiplayerbotavoidcreatureauras) | `1` | Bots keep out of permanent damaging creature auras and hazardous monsters. |
| [`AiPlayerbot.BombRunoutDistance`](#aiplayerbotbombrunoutdistance) | `30` | Yards a bomb carrier must be clear of the group before its debuff may be cured. |
| [`AiPlayerbot.BotGossipNpcs`](#aiplayerbotbotgossipnpcs) | *(empty)* | Allowlist of NPCs a commanded bot may talk to for its gossip order. |
| [`AiPlayerbot.CombatMonitor`](#aiplayerbotcombatmonitor) | `0` | Per-fight combat numbers of every bound group for the dashboard. |
| [`AiPlayerbot.CombatTimeline`](#aiplayerbotcombattimeline) | `0` | The boss timeline in each fight record: health, target changes and watched auras. |
| [`AiPlayerbot.DungeonRunAuraGatedTargets`](#aiplayerbotdungeonrunauragatedtargets) | `0` | Bots without a row's aura leave the unit that needs it alone. |
| [`AiPlayerbot.DungeonRunAutoCc`](#aiplayerbotdungeonrunautocc) | `0` | A crowd-control mark is not cast on a unit the party has dotted. |
| [`AiPlayerbot.DungeonRunCarrierHold`](#aiplayerbotdungeonruncarrierhold) | `0` | A cursed bot keeps its own Shade instead of switching to the raid's focus. |
| [`AiPlayerbot.DungeonRunCharmYield`](#aiplayerbotdungeonruncharmyield) | `0` | A charmed bot stops playing, and the party leaves charmed members alone. |
| [`AiPlayerbot.DungeonRunCureIgnoresThreat`](#aiplayerbotdungeonruncureignoresthreat) | `0` | Party cures and dispels are not held back by the damage threat limit. |
| [`AiPlayerbot.DungeonRunFightForward`](#aiplayerbotdungeonrunfightforward) | `1` | After a won fight the next pack close by is pulled from where the tank stands. |
| [`AiPlayerbot.DungeonRunLightRest`](#aiplayerbotdungeonrunlightrest) | `1` | Before a small pull the party rests to light lines instead of the full ones. |
| [`AiPlayerbot.DungeonRunLinkedDeath`](#aiplayerbotdungeonrunlinkeddeath) | `0` | The run leader treats a "die together" group of boss units as one fight. |
| [`AiPlayerbot.DungeonRunMaxMinutes`](#aiplayerbotdungeonrunmaxminutes) | `180` | Time limit of a run (`0` = none). |
| [`AiPlayerbot.DungeonRunNearWipeStop`](#aiplayerbotdungeonrunnearwipestop) | `2` | Near wipes within the window that pause a run once the fight is over. |
| [`AiPlayerbot.DungeonRunNearWipeStopMinutes`](#aiplayerbotdungeonrunnearwipestopminutes) | `10` | The near-wipe stop's window, in minutes of running time. |
| [`AiPlayerbot.DungeonRunNearWipeStopParty`](#aiplayerbotdungeonrunnearwipestopparty) | `0` | `1`: a party run gets the near-wipe stop too; `0`: it never happens. |
| [`AiPlayerbot.DungeonRunPackClearMarginYd`](#aiplayerbotdungeonrunpackclearmarginyd) | `10` | The band around a pull step's radius in which a living unit still counts. |
| [`AiPlayerbot.DungeonRunPackClearRepulls`](#aiplayerbotdungeonrunpackclearrepulls) | `3` | Residue pulls a pull step may send before it pauses for a human. |
| [`AiPlayerbot.DungeonRunPlaceUnstick`](#aiplayerbotdungeonrunplaceunstick) | `0` | A bot whose path is cut off is put beside its master instead of staying stuck. |
| [`AiPlayerbot.DungeonRunPlayerLed`](#aiplayerbotdungeonrunplayerled) | `1` | Lets a player who leads his own party start a player-led run. |
| [`AiPlayerbot.DungeonRunPlayerLedAwayMinutes`](#aiplayerbotdungeonrunplayerledawayminutes) | `10` | How long the leading player may be away before the run stops. |
| [`AiPlayerbot.DungeonRunPlayerLedPullYd`](#aiplayerbotdungeonrunplayerledpullyd) | `40` | How close he stands to a pack or boss for the run to pull it. |
| [`AiPlayerbot.DungeonRunRetreatAt2Dead`](#aiplayerbotdungeonrunretreatat2dead) | `0` | `1`: with two dead in a losing fight the living pull back once. |
| [`AiPlayerbot.DungeonRunTauntGate`](#aiplayerbotdungeonruntauntgate) | `0` | A tank does not taunt a unit that needs an aura it does not carry. |
| [`AiPlayerbot.DungeonRunThreatSuccessorKeepsAway`](#aiplayerbotdungeonrunthreatsuccessorkeepsaway) | `0` | A cursed tank still counts as on the boss for the threat line. |
| [`AiPlayerbot.DungeonRunThreatSuccessorNotHolder`](#aiplayerbotdungeonrunthreatsuccessornotholder) | `0` | The threat line is drawn against the tank the boss is not hitting. |
| [`AiPlayerbot.DungeonRunThreatSuccessorStrict`](#aiplayerbotdungeonrunthreatsuccessorstrict) | `0` | The white-damage stop on the threat line holds until the bot drops well below it. |
| [`AiPlayerbot.DungeonRunWalkBackAfterWonFight`](#aiplayerbotdungeonrunwalkbackafterwonfight) | `0` | `1`: after a won fight the party walks back to the stand (the old way). |
| [`AiPlayerbot.RaidReactions`](#aiplayerbotraidreactions) | `0` | Every bot reacts to raid hazards: traps, bombs, dragon breath, spread and Four Horsemen marks. |
| [`AiPlayerbot.RaidTankHealerAssist`](#aiplayerbotraidtankhealerassist) | `0` | A second healer on a tank helps injured raid members while the tank is safe. |
| [`AiPlayerbot.RangedReachHoverTarget`](#aiplayerbotrangedreachhovertarget) | `0` | A ranged damage dealer closes under a hovering boss inside a dungeon. |

## Settings

### `AiPlayerbot.AvoidCreatureAuras`
Keeps bots out of the permanent damage aura of a few creatures.
**Default:** `1` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Two creatures are listed:

| Creature | Where | Keep-out | Tanks |
| :--- | :--- | :---: | :--- |
| Creeping Sludge | Maraudon | 8 yards | may stand inside while fighting |
| Living Poison | Naxxramas | 10 yards | no exception (one hit is lethal) |

- Bots step out of the ring, and a path or a landing point keeps 1 yard more, so a bot does not walk back to the rim.
- Gap-closer abilities (Charge, Intercept, Feral Charge and the like) refuse a landing point inside the ring.
- A bot held outside the creature for 3 seconds sets it aside for 30 seconds and fights something else.
- An explicit attack order on that creature is never refused and never set aside.

Off: bots treat the creatures like any other mob.

</details>

### `AiPlayerbot.BombRunoutDistance`
How far a bomb carrier must be from the group before its bomb debuff may be cured.
**Default:** `30` · **Values:** yards · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Some debuffs drop a damaging cloud where they end (Mutating Injection in Naxxramas). Before a cure removes one, the party waits until its carrier stands at least this many yards from the nearest group member, so the cloud lands away from the group. A boss-tactic `clear` row that names its own distance overrides this value for that fight.

</details>

### `AiPlayerbot.BotGossipNpcs`
Allowlist of creature entries a commanded bot may talk to.
**Default:** *(empty)* · **Values:** comma-separated creature entries; empty refuses every gossip order except a run's own current gossip step · **Restart:** needed

<details>
<summary>How it works in detail</summary>

A gossip option runs the NPC's script, which can spend money, hand over items or teleport the party. List only NPCs whose script you have read.

- Audited: `7604` Sergeant Bly and `7607` Weegli Blastfuse, so `7607,7604` allows both.
- A run's own gossip step is always allowed, by its own entry.
- Any other order is refused with `not-allowed`.

See [the gossip order](../../play/commands/commander.md#bot-cmd-gossip).

</details>

### `AiPlayerbot.CombatMonitor`
Collects per-fight combat numbers for the dashboard.

**Default:** `0` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Numbers of every bound group:

- casts, damage, healing and overheal, damage taken, deaths with the killing hit, debuff uptime
- casts made in the 3 seconds after a fight ends are listed apart
- a fight shorter than 5 seconds is not recorded
- each finished fight is sent twice (a lost datagram is deduplicated by the dashboard)
- up to 500 different creatures are followed per fight; past that the rest counts in the record's `dropped` number (which stops at 4096)
- a rolling 10-second window rides the dashboard's commanders update

It needs the dashboard feed: `TortoiseBots.Dashboard.Enable = 1` (the default) and an open dashboard socket.

</details>

### `AiPlayerbot.CombatTimeline`
Adds the boss timeline to each finished fight record, so a fight can be replayed against the boss's health and its targets.

**Default:** `0` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Needs [`AiPlayerbot.CombatMonitor`](#aiplayerbotcombatmonitor). The fight record gains three lists, times in ms since the fight's start:

- `boss_hp`: the boss's health each 2 seconds (one decimal); at most 600 samples.
- `victims`: one row per change of the boss's target, with the unit and whether it was a tank, healer, damage dealer, a player, a pet or nobody; at most 400 rows.
- `auras`: an apply or a remove of a watched aura on a bot of the group; at most 1000 rows.

Past a cap the oldest row is dropped. The watched spells are the rows of the world table `ai_combat_aura_watch` (read once at startup), never spell ids in code. The Forever build ships no rows for that table, so on a stock database the timeline records no aura rows and the module logs one warning at startup; add rows to watch auras.

</details>

### `AiPlayerbot.DungeonRunAuraGatedTargets`
Leaves a unit that a boss-table row gates behind an aura to the bots that carry that aura.

**Default:** `0` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Some creatures take damage only from a player who carries a certain aura (Jin'do's Shade of Jin'do needs Delusions of Jin'do). With this on:

- a bot without the aura does not pick such a unit as a target, and sends no pet at it
- its direct damage spells and damage-over-time spells skip it
- an attack order on it is answered `immune to my damage without its aura`
- a healer weighs a group member that such a unit is hitting higher

Off: bots target it like any other creature and their damage is wasted.

</details>

### `AiPlayerbot.DungeonRunAutoCc`
A crowd-control mark is not cast on a unit the party has already dotted.
**Default:** `0` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

With this on, a bot assigned to crowd-control a unit skips the cast while the group's damage-over-time effects are on it: their ticks would break the hold the moment it lands. The mark stays assigned; only the cast waits. Fear and banish are exempt, because damage does not break them.

Off: a marked unit is controlled even when the party has dotted it.

</details>

### `AiPlayerbot.DungeonRunCarrierHold`
A bot cursed by Delusions of Jin'do keeps fighting its own Shade of Jin'do instead of following the raid's focus target.

**Default:** `0` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

While the bot carries the aura and the Shade it is on needs that aura, the "dps assist" target does not pull the bot to the skull. A hostile player always wins over the hold, and a bot without the aura is unaffected.

</details>

### `AiPlayerbot.DungeonRunCharmYield`
A bot a creature charms stops playing, and the party leaves a charmed member alone.

**Default:** `0` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

- A bot charmed by a creature (Hakkar's Cause Insanity) skips its whole update: the charmer drives it.
- No attack, pet attack, interrupt or spell lands on a charmed group member, and an attack order on one is refused.
- Pets are set passive while a charmed member is up, and their stance is restored when the charm ends.
- A charm cast by a player keeps the old behaviour.

Off: bots play as before.

</details>

### `AiPlayerbot.DungeonRunCureIgnoresThreat`
Party cures and dispels are not held back by the damage threat limit.
**Default:** `0` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Heals are never held by the damage threat limit. With this on, a cure or a dispel is exempt too, so a decurse on the tank goes out at the threat level where a damage spell would be refused. Off: a cure can be held back.

</details>

### `AiPlayerbot.DungeonRunFightForward`
After a won fight the run pulls the next pack close by from where the tank stands.
**Default:** `1` · **Values:** `0` the plan's moves as written, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

The next pull may come from the tank's position when:

- the pack's point is within 30 yards of the tank,
- the steps between are plain moves (no exact point, no launch step, no direct move), at most 4 of them,
- the pack is not an ambush and was not left uncleared,
- and the straight line to it passes no other living pack.

Off: each pull starts from its own stand in the plan.

</details>

### `AiPlayerbot.DungeonRunLightRest`
Before a small pull the party rests to the light lines only.
**Default:** `1` · **Values:** `0` the full lines everywhere, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

The light lines are 60% health, 35% healer mana and 25% mana, up to 75/60/45. Before a boss, a scripted event or a big pull the party always rests to the full lines.

</details>

### `AiPlayerbot.DungeonRunLinkedDeath`
The run leader treats a "die together" group of boss units as one fight.
**Default:** `0` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

With this on, the run leader reads the linked-death groups from the world database's boss-tactic rows. It reports the group's units together by health, records which of them fall and when, and never sends a straggler pull at one member alone: the group is fought together.

The Forever SQL creates the boss-tactic table but ships no rows for it, so on a stock database there is nothing to group. Other boss-tactic rows still do nothing here; see [Not on ForeverCore yet](#not-on-forevercore-yet).

</details>

### `AiPlayerbot.DungeonRunMaxMinutes`
How long a run may last before it ends by itself.
**Default:** `180` · **Values:** minutes; `0` = no limit · **Restart:** needed

<details>
<summary>How it works in detail</summary>

The limit ends the run like a stop, so a fight in progress is not cut off. The end note names the limit.

</details>

### `AiPlayerbot.DungeonRunNearWipeStop`
How many near wipes within the window pause a run once the fight is over.
**Default:** `2` · **Values:** `0` off, `1`-`10` events · **Restart:** needed

<details>
<summary>How it works in detail</summary>

A pause keeps the binding and the plan; `.tb run resume <leader>` goes on. This only happens in a party run with [`AiPlayerbot.DungeonRunNearWipeStopParty`](#aiplayerbotdungeonrunnearwipestopparty) on: a run cannot be a raid on the ForeverCore build (`.tb run start` refuses a raid group with `group-raid`).

</details>

### `AiPlayerbot.DungeonRunNearWipeStopMinutes`
The near-wipe stop's window, in minutes of running time.
**Default:** `10` · **Values:** `1`-`60` minutes · **Restart:** needed

Only read while [`AiPlayerbot.DungeonRunNearWipeStop`](#aiplayerbotdungeonrunnearwipestop) is not `0`.

### `AiPlayerbot.DungeonRunNearWipeStopParty`
Whether a party run gets the near-wipe stop.
**Default:** `0` · **Values:** `0` off, `1` on · **Restart:** needed

A run cannot be a raid on the ForeverCore build, so `0` means the near-wipe stop never happens.

### `AiPlayerbot.DungeonRunPackClearMarginYd`
The band around a pull step's radius in which one of its living units still counts as left behind.
**Default:** `10` · **Values:** `0` off, any number of yards up to `30` · **Restart:** needed

<details>
<summary>How it works in detail</summary>

A pull step is done only when none of its creatures stands within the step's radius plus this band (a unit hovering above the centre counts at any height). A leftover is pulled again up to [`AiPlayerbot.DungeonRunPackClearRepulls`](#aiplayerbotdungeonrunpackclearrepulls), otherwise it is named in the run's note so a human sees what was skipped.

</details>

### `AiPlayerbot.DungeonRunPackClearRepulls`
How many residue pulls a pull step may send before it pauses for a human.
**Default:** `3` · **Values:** `1`-`10` pulls · **Restart:** needed

Only read while [`AiPlayerbot.DungeonRunPackClearMarginYd`](#aiplayerbotdungeonrunpackclearmarginyd) is above `0`.

### `AiPlayerbot.DungeonRunPlaceUnstick`
Puts a bot whose path is cut off beside its master instead of leaving it stuck.
**Default:** `0` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

In a dungeon or raid, a bot whose moves have failed to start for 10 seconds is teleported beside its master, keeping its combat state and its pet. Off: the bot keeps failing those moves.

</details>

### `AiPlayerbot.DungeonRunPlayerLed`
Lets a player who leads his own party start a player-led run.
**Default:** `1` · **Values:** `0` refused, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

The start is `.tb run start <tank> <plan> lead=player`, typed by the player who leads the party. He walks wherever he likes; the run reads where he stands and its tank pulls the plan's open pack or boss near him. The run never orders a walk.

`0` refuses the word with `disabled` and ends a player-led run already going on the next tick. Without `lead=player` a run is bot-led, as always.

</details>

### `AiPlayerbot.DungeonRunPlayerLedAwayMinutes`
How long the leading player of a player-led run may be off the plan's dungeon, or dead, before the run stops.
**Default:** `10` · **Values:** `1`-`60` minutes · **Restart:** needed

Only read while [`AiPlayerbot.DungeonRunPlayerLed`](#aiplayerbotdungeonrunplayerled) is `1`.

### `AiPlayerbot.DungeonRunPlayerLedPullYd`
How close the leading player of a player-led run stands to a plan pack or boss for the run to pull it.
**Default:** `40` · **Values:** `5`-`100` yards · **Restart:** needed

<details>
<summary>How it works in detail</summary>

The pick is the nearest open pack or boss step within this distance, on the same floor and team, with a walk no longer than 1.5 times the straight line. It pulls when he stands still near his bots. Read only while [`AiPlayerbot.DungeonRunPlayerLed`](#aiplayerbotdungeonrunplayerled) is `1`.

</details>

### `AiPlayerbot.DungeonRunRetreatAt2Dead`
With two dead in a losing fight, the living pull back to the run's last stand once.
**Default:** `0` · **Values:** `0` the party fights on, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

The retreat happens in a five-player party with exactly two members dead, the lead bot alive and the fight behind its progress watch. The living walk back to the run's last stand (at most 40 yards away) for at most 20 seconds, once per fight. It never starts inside a scripted event, on the steps leading into one, on a hold step, or on a boss step that is an event.

</details>

### `AiPlayerbot.DungeonRunTauntGate`
A tank's taunt skips a unit it must not hold.
**Default:** `0` · **Values:** `0` off (every taunt goes out as before), `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

A unit that a boss-table row says needs an aura (Jin'do's Shade of Jin'do needs Delusions of Jin'do) is no pick for a tank's taunt, or for a warrior's Charge, unless the bot carries that aura. Off: every unit is a pick.

</details>

### `AiPlayerbot.DungeonRunThreatSuccessorKeepsAway`
A tank cursed by Delusions of Jin'do still counts as fighting the boss for the threat line.
**Default:** `0` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

On a boss whose boss-table row asks for a threat successor (Hakkar, Jin'do), the successor pick skips a tank that is not on the boss. With this on, a cursed tank that keeps a real share of the boss's threat counts as on it, so its threat stays the reference.

</details>

### `AiPlayerbot.DungeonRunThreatSuccessorNotHolder`
On a boss with a threat-successor row, the damage dealers' threat line is drawn against the tank the boss is not hitting.
**Default:** `0` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

The reference is the highest-threat tank among those that do not hold the boss and are hitting it. A tank on an add, held on a Shade, away, dead or charmed is no successor. A zero reference (the boss has no victim, or a damage dealer holds it) holds the damage dealers.

Off: the reference is the highest-threat tank that is on the boss, with the off-tank kept as the reference while it is there.

</details>

### `AiPlayerbot.DungeonRunThreatSuccessorStrict`
On a boss whose threat-successor row carries the word `strict`, the white-damage stop holds.
**Default:** `0` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

A bot whose auto-attack, Shoot or Auto Shot was stopped on the threat line does not start it again until it drops to 85% of the reference. It stops at 92%. A plain successor row keeps the old rule; a target without the row clears the stop.

</details>

### `AiPlayerbot.DungeonRunWalkBackAfterWonFight`
Whether the run walks the party back to the guard's stand point after every guarded fight.
**Default:** `0` · **Values:** `0` no walk back once the step's pack is cleared, `1` the old walk back · **Restart:** needed

### `AiPlayerbot.RaidReactions`
Adds the raid survival reactions to every bot.
**Default:** `0` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

With this on, every bot's reaction engine gets the raid rows at emergency priority:

- step out of an environmental hazard (a brazier and the like), in or out of combat;
- run a bomb debuff clear of the raid;
- flank a dragon's breath attack;
- spread out when the raid must not stack;
- rotate away from a Four Horsemen mark at three stacks.

Off: the engine has no reaction rows, as before.

</details>

### `AiPlayerbot.RaidTankHealerAssist`
Lets a second healer on a tank help the raid while that tank is safe.
**Default:** `0` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

A bound raid's layout can put two or more healers on one tank. Only the tank's first healer stays on it; with this on, a second seat may heal a raid member it can see and reach who is below 70% health, while the tank is alive, off the boss and at 90% health or more after the damage it is taking. A player who edits the tank's focus list keeps the tank healer but loses this assist.

</details>

### `AiPlayerbot.RangedReachHoverTarget`
A ranged damage dealer closing on a hovering boss inside a dungeon walks under it instead of standing still out of range.
**Default:** `0` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

A hostile that stands at least 8 yards above the floor under it counts as hovering (Onyxia in the air). A ranged damage dealer walks the path to the floor under it and stops where its spell reaches the target, counted from the floor point. Tanks, melee and healers keep the normal approach.

One `BossReachHover` row per bot per 5 seconds goes to `bot_events.csv` when that log is on.

</details>

## Not on ForeverCore yet

These settings exist in the configuration file but do nothing on the ForeverCore build:

- **Boss tactics (general):** the per-boss rows of the `ai_boss_tactic` table (interrupt, kill first, stay behind, spread, tank post, burn mana, ranged only, use object, tank swap, add crew, gate hold, heal reserve, avoid cast, pull gate) do nothing for bots, and so do their keys: `DungeonTactics`, `EnableCustomRaidTactics`, `DungeonRunBossSpread`, `DungeonRunBossSpreadSlots`, `DungeonRunBossProtectionPotion`, `DungeonRunBurnMana`, `DungeonRunRangedOnly`, `DungeonRunAvoidCastAll`, `DungeonRunHealReserve`, `DungeonRunAddCrew`, `DungeonRunGateHold`, `DungeonRunKillFirstGrid`, `DungeonRunUseObject`, `DungeonRunTankSwap`, `DungeonRunTankPost` (with its `HoldSpot`, `Leash`, `OwnsSubject`, `OwnsUnit`, `Main`, `KeepsUnit`, `Taunts`, `Walks`, `Waits` and `Healers` keys), `DungeonRunPullGate`, `DungeonRunTauntAfterSkipsMain`, `DungeonRunAuraTactics`. **Not on ForeverCore yet.**
- **Boss tactics (seek and Hakkar):** `DungeonRunSeekAura`, `DungeonRunSeekPullCycle`, `DungeonRunSeekSonMeet` (with `MeetTakeReach`, `MeetPrePlace`, `MeetPrePlaceLate`, `MeetPrePlaceHold`, `ShotWaitsReceiver`, `MeetNoHeal` and `TakenSonThreatCap`), `DungeonRunSeekTakeEdge`, `DungeonRunSeekNearestTake`, `DungeonRunSeekNearestTakeReach`, `DungeonRunSeekVictimRelief`, `DungeonRunSeekWalkoutFocus`, `DungeonRunSeekWalkoutHoldCombat`, `DungeonRunSeekWalkoutFaceCast`, `DungeonRunSeekWalkoutStandLog`, `DungeonRunSeekWalkoutStandSight`, `DungeonRunSeekSiphonClock`, `DungeonRunSeekCycleRoll`. **Not on ForeverCore yet.**
- **Boss tactics (threat, carrier and encounter extras):** `DungeonRunThreatResetWindow`, `DungeonRunCarrierStrike`, `DungeonRunCarrierScan`, `DungeonRunShadeHealerCover`, `DungeonRunImmuneBossVictimEscape`, `FankrissSwapStacks`, `PatchwerkOpeningDelay`, `OnyxiaWhelpsAndEggs`, `BossTacticsKeepsEngagedBoss`, `DungeonRunFactJudgeCharmedLead`. **Not on ForeverCore yet.**
- **Boss stand seats** from the `ai_boss_stand` table: `BossStandOutranksBreathDodge`, `BossStandEvents`, `BossStandWholeRaid`. **Not on ForeverCore yet.**
- **Raid runs:** `DungeonRunRaid`, `DungeonRunRaidBossRules`, `DungeonRunRaidHealerGuard`, `RaidHealerSeatsByClass`. **Not on ForeverCore yet.**
- **Bot LFG groups:** `BotLfgGroups`, `BotLfgChat`, `BotLfgFillAfterMinutes`, `BotLfgGroupsPer100Bots`, `BotLfgMaxGroups`, `BotLfgOnlyWithPlayersOnline`. **Not on ForeverCore yet.**
- **Run leader: aggro and pulls:** `DungeonSelfClear`, `DungeonRunAggroEstimate`, `DungeonRunAggroCeiling`, `DungeonRunAggroRingYd`, `DungeonRunAggroWalkRing`, `DungeonRunAutoCcAbove`, `DungeonRunAutoCcRisky`, `DungeonRunStragglerPull`, `DungeonRunRespawnNest`, `DungeonRunTotemFocus`, `DungeonRunPackFocusSparesTacticUnits`, `DungeonRunPullStand`, `DungeonRunStandClearance`, `DungeonRunStandDiscipline`, `DungeonRunStandPullPreCc`. **Not on ForeverCore yet.**
- **Run leader: loot, death and recovery:** `DungeonRunRezHold`, `DungeonRunLootFfa`, `DungeonRunGiveItems`, `DungeonRunBotKeysForPlayers`, `DungeonRunWalkBackRescue`, `DungeonRunDryWalk`, `RunTeleportReturnYd`, `DungeonReviveHoldYd`. **Not on ForeverCore yet.**
- **Other combat extras:** `HazardEvasionDistance`, `ReactionHealCastGuardPct`, `AirborneWandYieldsToNukes`, `HoverCastHoldsSpread`, `SpreadRangedMinRange`, `RaidEntryByCommander`. **Not on ForeverCore yet.**
- **Scripted area triggers for bots** (`BotAreaTriggers`), including a run's own trigger steps: the script never runs. **Not on ForeverCore yet.**
