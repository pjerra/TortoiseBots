---
id: play-dungeon-run-leader
title: Dungeon Run Leader
category: play
summary: The run leader (.tb run, AiPlayerbot.DungeonSelfClear, on by default, work in progress) that lets a bound bot party clear a stored dungeon plan by itself, its safety rules, supported steps and the keys its doors need, and the plan tables the exporter fills.
tags: [guide, dungeon, plan, commander, self-clear, uldaman, events]
relates_to:
  - play-boss-tactics
  - play-dungeons-and-raids
  - play-commands
  - server-configuration
  - developers-architecture
---

# Dungeon Run Leader

> Work in progress: this page describes an unfinished feature that is still changing.

`.tb run` lets a bound party of bots clear a stored dungeon plan by itself. It needs `AiPlayerbot.DungeonSelfClear = 1` (default `1`, on) and a party of host bots that one bot leads; every member must already be bound with `.tb master party <leader> <commander>` (or `.tb master <bot> <commander>`), because the run never binds or rebuilds a bot's kit. The run sends at most one order per second, through the same order judge as a commander's own orders.

The command is a GameMaster command: in game, on the console, over remote access or over SOAP. Its shape:

```text
.tb run start <leader> <plan> [from step <N> | from leg <leg>] [lead=player]
.tb run stop <leader> [reason]
.tb run pause <leader>
.tb run resume <leader>
.tb run status <leader|all>
.tb run events <leader> [since=<seq>]
```

`start` refuses a leader that is not a host bot leading a party (a raid group is refused), a plan the server has not loaded, a party member that is not a host bot, offline or in another instance, a member with too few free bag slots, a member with no commander, and a party whose loot method is neither group loot nor need-before-greed. `status all` also prints the run's tick cost.

### Reloading the plans (`.tb plan reload`)

Only a GameMaster can reload the plans, and it works with no run going. It re-reads `ai_dungeon_plan`, `ai_dungeon_leg`, `ai_dungeon_step`, `ai_dungeon_leg_need`, `ai_dungeon_boss` and `ai_boss_tactic` and swaps the new set in only when every row passes. A refused row, a missing plan, a failed query, or a table that changed while it was read keeps the old set. A run in progress keeps the plan it started with.

`.tb plan list` shows the loaded plans, `.tb plan <plan> info` shows one plan's legs and steps and how many database rows the loader skipped, and `.tb plan <plan> check <character>` checks a bot's path through it.

## Tables

A plan is the route of one dungeon written as data: where to walk, which packs to pull, which bosses to kill, which objects to open, and what to wait for. Plans are read at startup into a cache, one pass per table, and nothing reads them per tick.

### `ai_dungeon_plan`

| Column | Meaning |
| :--- | :--- |
| `plan` | Lower-case plan name, primary key. |
| `map` | Instance map id. |
| `cc_priority`, `cc_always`, `kill_first`, `cc_never` | Comma entry lists, `''` for none. `cc_never` lists entries the automatic crowd control under `AiPlayerbot.DungeonRunAutoCc` never marks (trivial non-eligible units are excluded by rank anyway, unless the plan lists them in `cc_priority`/`cc_always`). |
| `source` | Where the rows came from, for the reader. |
| `comment` | Free text. |

Each of the four lists is read on its own: a list with more than 16 entries keeps its first 16, any other bad list is dropped by itself, and the rest are kept. The server log names every cut or dropped list.

### `ai_dungeon_leg`

| Column | Meaning |
| :--- | :--- |
| `plan`, `leg` | Plan and leg number, primary key. |
| `name` | Leg name shown in the status line. |
| `comment` | Free text. |

### `ai_dungeon_step`

One row is one thing to do, run in `(leg, seq)` order with primary key `(plan, leg, seq)`, and `seq` need not be dense.

| Column | Meaning |
| :--- | :--- |
| `kind` | What to do (see Step kinds). |
| `entry` | Creature, game object, or item entry, by kind. |
| `x`, `y`, `z` | Point on the plan's map, by kind. |
| `radius` | Yards, by kind, at most 100. |
| `item` | Item entry, by kind, `0` for none. |
| `param` | By kind: `wait_object` state to wait for, `ritual` bot count (`0` = 3), `gossip` menu option (1-15), `hold` done value. |
| `flags` | Bitmask; see [Step flags](#step-flags). |
| `timeout_s` | `0` means 60, at most 600 s, and hold steps up to 1800 s. |
| `entries` | Comma creature entry list, at most 16, by kind. |
| `comment` | Free text. |

Defaults by kind: pull radius `0` means 15, boss radius `0` means 30, hold radius `0` means 30, and `ritual` `param` `0` means 3 bots.
`wait_object` states are `0` ready/closed, `1` active/open, and `2` active_alt.
`move_hop_exact` is its own kind for a direct hop after an exact move.
`open_proof` needs a proof radius over 0 and at most 20.
`use_on` reads no flag or param.

The loader skips rows it cannot use and names them in the server log: an unknown kind, a `pull` without entries, a `boss`, `loot`, `open`, `use_item`, `wait_object`, `ritual`, `gossip` or `use_on` without `entry`, a `gossip` with `param` outside 1-15 or any flag but 512, a `use_on` with any flag or param, a `use_on` without `item` or without a point, a negative or over-100 radius, a `wait_clear` without a radius, a timeout over the cap, a `wait_object` state above 2, a stand pull with `param` over 60, a pull with `item` over 80, an `open_proof` with proof equal to entry, with `param` or timeout set, or with 2048 misused, or the exact flag on a non-`move` step, or a wade flag on a kind other than `move`, `pull` or `boss`.

### Step flags

| Flag | Steps | Meaning |
| :---: | :--- | :--- |
| `1` | `pull` | A stealthed pack. |
| `2` | any | Alliance team. |
| `4` | any | Horde team. |
| `8` | `hold` | Done when the instance data reads `param`. |
| `16` | `hold` | Done when the object within 30 yd reads `param`. |
| `32` | `hold` | Reads encounter states 0/1/2/3; fails on 2 or reset. |
| `64` | `move` | Exact waypoint: the lead bot must stand on the point (1.5 yd, still 2 s). |
| `256` | `loot`, `open`, `wait_object` | Gone from the world counts as done. |
| `512` | `gossip` | Dead is done. |
| `1024` | `pull` | Never defers an unreachable unit; pauses instead. |
| `2048` | `open_proof` | The proof is a living creature. |
| `4096` | `boss` | Out of reach ends the step skipped, not paused. |
| `8192` | `pull` | Pulls from the prior move stand. |
| `16384` | `hold` | Reset to gossip. |
| `32768` | `hold` | Dead gossip is OK. |
| `131072` | `move`, `pull`, `boss` | Plan data: the map probe found this hop wet. The run's own moves may still wade shallow water; the first wet sub-hop logs its depth and liquid status. |

### Step kinds

| Kind | Columns used | What it means |
| :--- | :--- | :--- |
| `move` | Point, exact flag 64, entry | Walks the navmesh to the point, `entry` fires that area trigger once, and with flag 64 the lead bot must stand on the point (1.5 yd, still 2 s) before the next step. |
| `move_direct` | Point, entry | Walks a straight line over a navmesh gap, `entry` fires that area trigger once. |
| `move_hop_exact` | Point | Direct hop launched from the exact point before it, only on proof. |
| `pull` | Centre, radius, entries, param, item | Every living unit of `entries` within `radius` of the centre, with flag 1 for a stealthed pack, flag 8192 pulls from the prior move stand with `param` as patrol-clear yards and `item` as body-count ceiling. |
| `boss` | Entries first, then boss entry | Kills `entries` in order, then the boss entry, with flag 4096 ending skipped not paused when out of reach. |
| `loot` | Entry, near point, item | Loots and the item must be in the bags after, with flag 256 counting gone as done. |
| `open` | Entry, near point, item, param | Uses the object with the item its lock needs, `param` is a second item in the same bags, with flag 256 counting gone as done. |
| `open_proof` | Object plus separate proof entry and radius | Uses the object and finishes when the proof stands near the point, with flag 2048 for a living-creature proof. |
| `use_item` | Entry, item, param | Uses the item from the bags and the made item must appear, `param` is a second item in the same bags. |
| `use_on` | Object, near point (required), item | The carrier's item is used ON the object. |
| `wait_object` | Entry, near point, state, timeout | Waits until the object reads the wanted state, with flag 256 counting gone as done. |
| `wait_clear` | Point, radius, timeout | Waits until no living hostile stands in the radius, and past the timeout bystanders on another floor finish it with a note. |
| `rest` | None | Rests even when nobody is low. |
| `ritual` | Entry, near point, bot count | Several bots use the object together, and its answer starts the event or pauses. |
| `hold` | Spot, radius, units, done condition, timeout | Stands on the spot and fights what comes until the done condition reads true (see Events). |
| `gossip` | NPC entry, near point, option 1-15 | Talks to the NPC and picks one menu option, searched within 40 yd after the lead bot walks within 30 yd first. |

A walk refuses a route that crosses or ends in liquid: any liquid at the walked height or up to 0.6 yd below it, and swimming water.
A move the run makes on its own (a plan row's walk, a regroup, an approach stand, a return to the plan point, a follow-up, a bot walk) may wade shallow water on any leg of the run, when every sample is at most the game's swim depth (2.0 yd of water above the ground, the core's own under-water threshold) and none is swimming; deep water, lava and a walk whose every sample cannot be proven shallow keep the refusal.
Flag `131072` records the rows whose own hop the map probe found wet; it is plan data now and no longer the gate. Wailing Caverns' river hops carry it; no other shipped plan does.

## Joining a dungeon fill

A player can ask the server to fill his own group with bots:

```text
.bot lfg join <dungeon id> <tank|healer|dps>
.bot lfg status
.bot lfg leave
```

The caller becomes the party's leader and the module adds bots one at a time, on its own timer (`AiPlayerbot.RandomBotLftUpdateInterval`, default `15000` ms, clamped 5000–60000 ms) at most `AiPlayerbot.RandomBotLftMaxFillsPerInterval` per pass (default 1), while `AiPlayerbot.RandomBotLftEnabled` is on (default 1). `status` names the dungeon, the role, the slots still open and the bots already added; `leave` releases them. This is not the run leader: a bot LFG group with its own leader is not on WoW Forever yet.

## Run leader

The run leads the bound party and nobody else's.

- `from leg <leg>` or `from step <N>` starts the run partway instead of at the beginning.
- `AiPlayerbot.DungeonRunMaxMinutes` (default 180, `0` for none) ends a run that has lasted that long with a note; paused time counts.
- `.tb run stop <leader> [reason]` keeps the reason in the end note.
- `.tb run events <leader> [since=<seq>]` prints the run's event ring.

The commands below are GameMaster commands; a normal player does not start, pause or stop a run.

### How it plays

Once a second the run reads the party and the current step's surroundings and answers with at most one order, sent through the same order judge the commander uses.

| Step | What the run does |
| :--- | :--- |
| Walks | Approach stands ring 15-25 yd, defer-off-floor packs, split too-far in 120 yd legs, walk back stops at the last cleared fight; island hops off twice per step, stranded rescue teleports once per step, 3-miss rewind; a direct step needs its own ground-and-height gather test; movement counts as walk progress up to 120 s. |
| `move`, `move_direct` | Done within 3 yd, or within 6 yd after 3 s stalled (12 s for `move_direct`), where 6 yd is measured on the ground plane when within 4.5 yd of height and in 3D beyond that. |
| `pull` | Walks in hops toward the centre until within 60 yd, gathers the party within 20 yd (30 s), then sends pullback, where a pull with no fight in 25 s is retried once and then that unit is passed, and a unit surviving 3 pulled fights is left as out of reach. |
| `boss` | Runs like a pull in kill order, pulls a neutral boss and leaves only a friendly one alone, needs the boss's death in sight (a dead unit in the step radius plus 60 yd, or within 60 yd of the lead bot once seen alive), and waits `timeout_s` for an absent boss before pausing. |
| Team flags | Skips an other-team pull unless the party already stands within 60 yd and a unit of it is not friendly, and always walks to an other-team boss and skips it only when every unit there is friendly. |
| `loot` | Counts the step done when any party bot already carries the item, else sends the loot order and pauses when the item is still missing after; a loot-rule refusal pauses. The free-for-all detour (`DungeonRunLootFfa`) is **Not on WoW Forever yet**: a run starts and goes on only on group loot or need-before-greed. |
| `open` | Pauses when the carrier lacks the key item, else sends the open order and waits for its task. |
| `use_item`, `use_on` | Sends the matching order once a carrier with the items is found, where a `use_on` ACK alone does not end the step because the object must change state or the boss must show within 30 s. |
| `wait_object`, `wait_clear` | Waits as the Step kinds table says, bounded by `timeout_s`. |
| `rest`, `ritual` | Rests through the rest gate, and a ritual answer starts the event while an error or no answer pauses. |
| `hold` | See Events. |

### Exact waypoints

Plain walk arrival only starts the tighten, then the lead bot must be within 1.5 yd of the point and still for 2 s.
The tighten uses at most 3 moves onto the point, pauses when no proof comes within 36 s, sends the hop only on proof, and a fight or a pause clears the proof.

### Safety rules

The run protects itself with these rules:

- It gathers the party before a pull and pulls only what is near.
- It guards from the stand, attacking mobs that hit the party.
- Respawn-nest handling is **Not on WoW Forever yet** (`AiPlayerbot.DungeonRunRespawnNest` is not read): a unit that comes back in its nest is an ordinary unit for the run.
- The nest-aware gather spot is off with the respawn-nest handling.
- It never pulls what a later step owns.
- It ends a pull or boss step past the 180 s fight ceiling once the game confirms that step's own units are dead or gone from the map (a unit that merely dropped out of the run's reads is not dead) and lets the walk go on, even while unrelated mobs (respawning trash, a patrol that walked in) still fight: those are fought by the bots' ordinary combat and do not hold the step. A boss step ends as soon as the boss's corpse is confirmed, whatever keeps the party in the combat afterwards. Younger fights, a combat flag with nothing engaged, and a row with its own units left keep the rules as before.
- It judges no unseen pack or boss, only what the lead bot sees within 60 yd.
- A healer on stay is held 9 yd behind the tank and heals only.
- A reaching pull ends once any creature has a party victim.
- A big pack (8+ joiners) stand walk is watched every tick.
- It regroups by itself after 3 dead or a wipe, within the fight ceiling of 180 s (420 s on a boss), by reviving the party, gathering it, and walking back.
- It waits for every fallen member until that bot is back alive within 20 yd of the lead bot.
- It recovers a lead bot that is back at the entrance alone by regrouping on it and walking back over the plan's own points.
- It always holds for a party resurrection when a living resurrector is available (`AiPlayerbot.DungeonRunRezHold` is hard-coded on in WoW Forever; the setting has no effect).
- A revived bot over 40 yd away or 8 yd of height walks back; the run leader does not summon bots on WoW Forever (summon orders are refused as `unsupported`).
- It regroups on resume by reviving, gathering over 60-180 s, and walking back.
- It gathers back on the run's anchor when a fight left the lead bot off it.
- It passes hop pulls that draw no fight for the rest of the run.
- It walks back over the plan's own points across a navmesh hole.
- It sends no step while a member is dead and regroups first.
- It retries a refused gather once and then skips it.
- It stops waiting for a straggler at the gather ceiling (30 s, 60 s on regroup); the straggler walks back (no summon on WoW Forever).
- It first walks a straggler by path; there is no summon to fall back on (summon orders are refused as `unsupported`).
- It never summons a member onto a lead bot that stands off the floor: more than 3 yards of air under it, read the way the teleport landing reads the floor, so a lead bot left hovering in mid-air is no summon point and its own landing or rescue comes first. A wedged member's teleport return is no summon and still runs. A lead bot on a game-object platform or an elevator counts as on the floor. A lead bot left hanging that way for 30 seconds pauses the run with both heights.
- It runs a member with attackers back to the party as a passive follower within 120 yd.
- It pauses once on a missing item and then skips the dependent steps.
- It still treats a boss that changed entry mid-fight as the boss.
- It rejoins a bot stuck in another copy of the dungeon instance, at most 3 times per step and 15 s apart.
- With `AiPlayerbot.DungeonRunLinkedDeath = 1` (default `0`), linked deaths run the die-together tactic with a `fighting: linked` line.
- It bounds every wait (gather 30 s, summon 10 s, walk back 90 s, pull 25 s, fight 180 s and boss 420 s, ritual answer 5 s, rest 60 s, task 90 s, step 300 s out of combat, and at most 20 pulls per step).
- It pauses at ceilings and nothing loops forever.
- It applies the player-led rules below.
- Orders go to bots only, and summon and revive never pause the run.
- A rest walk is resent once when the lead gains under 2 yd in 3 s.
- A patroller within 30 yd past the radius is waited up to 60 s.
- A pull the tank cannot start from range is retried once as a walk-in pull before the standing search, the deferral and the pause apply.
- A refused pull whose stand search found none while the lead bot stands on a navmesh island hops it off first instead of pausing.
- Risky fear families (`AiPlayerbot.DungeonRunAutoCcRisky`) are **Not on WoW Forever yet**.
- `AiPlayerbot.DungeonRunNearWipeStopParty` gives parties the near-wipe stop too (raid default).
- `AiPlayerbot.DungeonRunPackClearMarginYd` (default 10) is the pack-clear band, and `AiPlayerbot.DungeonRunPackClearRepulls` (default 3) the repulls before pausing.
- The raid healer-guard (`AiPlayerbot.DungeonRunRaidHealerGuard`) is **Not on WoW Forever yet** (a run never leads a raid).

Retreat at 2 dead is opt-in with `AiPlayerbot.DungeonRunRetreatAt2Dead = 1` (default `0`), see the [dungeon and raid settings](../server/settings/dungeons-raids.md#aiplayerbotdungeonrunretreatat2dead).
Walk back to the stand after every guarded fight is opt-in with `AiPlayerbot.DungeonRunWalkBackAfterWonFight = 1` (default `0`). `DungeonRunWalkBackRescue` and `DungeonRunDryWalk` are hard-coded off on WoW Forever (the settings have no effect).

### Combat reads

- A stale flag gets 20 s grace, then the ceiling runs.
- An unattackable-only fight is let go, with no ceiling.
- A pause for the fight ceiling goes on by itself only when every engaged unit cannot be attacked; respawn-nest units are real fights on WoW Forever.
- Charmed and fake-death units read as cannot-attack.
- A game-event-gated boss is skipped, not paused.
- Off-plan-map bots never hold fights, and bots never leave the plan map by follow, flee, or trigger.
- An alive-by-spot-but-unpulled boss pauses at once with distance and height.
- Health progress holds the ceiling; a unit held at 1 hp or 1% is watched.

### Focus (the skull)

Every fight's skull comes from the run: the plan's `kill_first` unit of that fight is focused (skull on it, every bot attacks its skull target) and the focused unit is kept until it dies or is crowd-controlled; a pause or the fight's end takes the skull off.
A `kill_first` target that is only passive so far — it is in no combat yet, or has stopped fighting while the adds still do — keeps the run's skull while it lives and can be attacked: the pack's fallback never replaces it, and only its death frees the skull for the pack's own best unit.
A pull or move step whose fight has no `kill_first` unit of the pack focuses the pack's own best unit instead, so every pulled group gets a skull: a mana user (a healer or a caster) before the rest, and the nearest of that class to the lead bot within 15 yd of it. A unit the party cannot attack, one the run's crowd control holds, one evading and one whose focus order was refused a moment ago are passed over; the skull moves to the next unit when the focused one dies.
A boss step's fight does the same for its adds: never the boss itself, never an add the boss-tactics `kill_first` or `linked_death` rows name, and no fallback skull once only the boss is left. An event and a rest fight keep their own rule.
No boss-tactic rows are shipped on WoW Forever, so the fallback skull never has a tactic unit to spare (`DungeonRunPackFocusSparesTacticUnits` is not read).
A skull a player moves onto a living hostile of the fight becomes the focus ahead of the run's own pick, `kill_first` included: every bot is sent to that unit by GUID and the player's mark stays where they put it; while the target lives, the run picks nothing else. The run knows its own skull only while the marked mob is still alive in that fight and the mark is still the one it set.

### Rest gate

The rest gate runs after every fight and before every step.
It rests a living bot under 80% health, a healer under 50% mana, or other mana under 35% mana until 90% health, 80% healer mana, or 60% other mana, or for 60 s, and checks again every 20 s.
With `AiPlayerbot.DungeonRunLightRest = 1` (default `1`), before a plain pull or with no fight ahead it starts only under 60% health, 35% healer mana, or 25% other mana, and ends at 75% health, 60% healer mana, or 45% other mana.
With `AiPlayerbot.DungeonRunFightForward = 1` (default `1`), the next pack within 30 yd is pulled from where the tank stands.
- Walking to and pulling a waited-for unit that never comes (`AiPlayerbot.DungeonRunStragglerPull`) is **Not on WoW Forever yet**.
- A fight held open only by the run's own crowd control is broken one held unit at a time, without re-applying the hold.
- A charmed group member is never a fight's unit: orders at it are refused and the reads leave it out.

### Aggro estimate

**Not on WoW Forever yet.** The run does not estimate what a pull would wake; `AiPlayerbot.DungeonRunAggroEstimate`, `DungeonRunAggroCeiling`, `DungeonRunAggroWalkRing` and `DungeonRunAggroRingYd` are not read by this build.

### Pull stand

**Not on WoW Forever yet.** The run does not place a firing stand before a pull: `AiPlayerbot.DungeonRunPullStand`, `DungeonRunStandClearance`, `DungeonRunStandPullPreCc` and `DungeonRunStandDiscipline` are not read by this build, and the run's own automatic crowd control (`DungeonRunAutoCc`, `DungeonRunAutoCcAbove`) is forced off. The bots still use their class crowd control.

### Totem focus

**Not on WoW Forever yet.** The run does not focus a totem first (`AiPlayerbot.DungeonRunTotemFocus` is not read).

While the lead bot (the main tank) is charmed, a Brain Wash Totem is judged friendly against him. The first living, uncharmed commanded bot judges the kill-first read, the engaged units, the raid guard's anchor and the focus order's target instead (`AiPlayerbot.DungeonRunFactJudgeCharmedLead` is hard-coded on in WoW Forever; the setting has no effect). See [Boss Tactics](boss-tactics.md).

### Events

A scripted event is a start (`ritual`, `open`, `use_item`, `gossip`, or `use_on`) plus a `hold`.
The hold walks the party to the spot, stands there, sends no pulls of its own, and fights what comes, with ranged and healer stands kept and the party walked back when the lead bot drifts over 8 yd off the spot.
It finishes by flag `8` when the instance data reads `param` (with flag `32` reading encounter states 0/1/2/3, failing on 2 or reset, and on 30 s with no start), by flag `16` when the object within 30 yd reads `param`, and otherwise when all its units are dead after one was seen awake (ending 10 s after an all-dead hold whose units died before it).
`timeout_s` bounds every hold, and a failed event goes back to its start step and pauses.
- A hold places the party, returns on fall, regroups on pause, self-resumes, and fails when it cannot start.
- `reset_to_gossip` re-arms once then pauses at gossip, and `dead_gossip_ok` ends when none is hostile if the start NPC was dead.

| Event | Start | End |
| :--- | :--- | :--- |
| Keepers | Altar ritual | 4 keepers dead, then the door opens. |
| Archaedas | Altar ritual | Waves fought, then the vault opens. |
| Keystone | Open with the Staff | The door opens. |
| Seal | Wait step | The seal reads open. |
| Temple Doors | Open | The doors read open. |

### Keys and doors

The plan can carry locked-door rows (`ai_dungeon_leg_need`), but the run does not read them on WoW Forever, so it never waits on a key or Lockpicking and never skips a leg for a missing key. `AiPlayerbot.DungeonRunGiveItems` is not read and `AiPlayerbot.DungeonRunBotKeysForPlayers` is hard-coded off on this build. An `open` step in the plan still uses its key item as the Step kinds table says.

### Player-led runs

Player-led runs (`AiPlayerbot.DungeonRunPlayerLed = 1`, the default) sit on top of `AiPlayerbot.DungeonSelfClear = 1`.
The leading player starts the run with `.tb run start <tank> <plan> lead=player`; this is a GameMaster command typed in game by the player who leads. The start requires a party of at most five, never a raid, led by the starter, with at least one bot, every bot commanded by the starter, and the named tank a bot of that party.
The player walks and the run leader pulls near him (within `AiPlayerbot.DungeonRunPlayerLedPullYd`, default 40).
Player orders never pause the run (only pause and stop do).
Staying away from the dungeon instance for over `AiPlayerbot.DungeonRunPlayerLedAwayMinutes` (default 10) stops the run.
- Orders are move, fight, or control; the step is the adopted fight else the nearest pull within 40 yd on the same floor and team; object steps gate on their leg order.

### Raids

A run is a party feature. A raid group is refused at start (`group-raid`), so the run never leads a raid. Raid fights and their tactics belong to the bots' own combat AI and to the raid material outside the run.

### Plans on the server

The plan rows' own `comment` column records their state: `shadowfang_keep` is marked `live-checked`; `ragefire_chasm` and `wailing_caverns` are `mapped (not live-checked)`; the rest are `draft (not mapped, not live-checked)`, including `deadmines` and the Scarlet plans. `.tb plan list` shows what the server loaded.

### Cost

A running run ticks once per second with at most one local grid search near the lead bot within 60 yd.
A hold done by data reads one value a tick, attacker sets come from the core, and nothing touches the database after startup.
Not supported:

- Avoid boxes are unknown.
- `gm_cross` steps are unsupported.
- Open-water moves are unsupported.
- Plan `skip` is unsupported.
- Per-leg `rest` is unsupported.
- `companions_loot` is unsupported.
- `area_trigger` is exported with a warning but never runs.

With `AiPlayerbot.DecisionCapture.Enabled = 1` (off by default, test servers only) every tick's facts, run and order are also written to a rotating capture: see [Decision capture](../server/decision-capture.md).
