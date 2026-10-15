---
id: guide-dungeon-run-leader
title: Dungeon Run Leader
category: guides
summary: The opt-in run leader (.bot run, AiPlayerbot.DungeonSelfClear) that lets a bound party clear a dungeon plan by itself, its safety rules and supported steps, and the plan tables (ai_dungeon_plan, ai_dungeon_leg, ai_dungeon_step) that turtle-agent's exporter fills.
tags: [guide, dungeon, plan, commander, self-clear, uldaman]
relates_to:
  - guide-boss-tactics
  - guide-dungeon-tactics
  - guide-player-controls
  - guide-configuration-tuning
  - concept-architecture-invariants
---

# Dungeon Run Leader

A **dungeon plan** is the route of one dungeon written as data: where to walk, which packs to pull, which bosses to kill, which objects to open and what to wait for. Plans are made and checked live by [turtle-agent](https://github.com/pjerra/turtle-agent) (its `dungeons/<name>.json`, plan format v1); its exporter turns them into world migrations for the tables below. The turtle-agent file is the single source: edit the plan there and export again, never the rows by hand.

The **run leader** (`.bot run`) lets a bound party clear such a plan by itself. It is **off by default** (`AiPlayerbot.DungeonSelfClear = 0`); see [Run leader](#run-leader) below.

The tables are read once at startup into a cache (`ai/playerbot/DungeonPlanMgr.cpp`, one pass per table). Nothing reads them per tick; a change needs a restart.

## Tables

The schema comes from `data/sql/world/20261014150000_world.sql` (schema only; the rows come in the exporter's own migrations).

### `ai_dungeon_plan`

| Column | Meaning |
| :--- | :--- |
| `plan` | The plan's name, lower case (`uldaman`). Primary key. |
| `map` | The instance map id (Uldaman 70). |
| `source` | Where the rows came from (the turtle-agent commit and file), for the reader. |
| `comment` | Free text. |

### `ai_dungeon_leg`

| Column | Meaning |
| :--- | :--- |
| `plan`, `leg` | The plan and the leg number (0, 1, ...). Primary key. |
| `name` | The leg's name in the plan (`baelog`, `keystone`), shown in the status line. |
| `comment` | Free text. |

### `ai_dungeon_step`

One row is one thing to do. Steps run in `(leg, seq)` order; the primary key is `(plan, leg, seq)`.

| Column | Meaning |
| :--- | :--- |
| `plan`, `leg`, `seq` | Where the step sits. `seq` need not be dense. |
| `kind` | What to do (table below). |
| `entry` | A creature, game object or item entry, by kind. |
| `x`, `y`, `z` | A point on the plan's map, by kind. |
| `radius` | Yards, by kind; at most 100. |
| `item` | An item entry, by kind; `0` for none. |
| `param` | `wait_object`: the state to wait for (`0` ready / closed, `1` active / open, `2` active_alt). |
| `flags` | `1` ambush (a stealthed pack), `2` Alliance only, `4` Horde only. |
| `timeout_s` | `wait_object`, `wait_clear`: seconds before the wait fails; `0` = 60, at most 600. |
| `entries` | Comma-separated creature entries (`4851,4852`), by kind; at most 16. |
| `comment` | Free text. |

### Step kinds

| Kind | Columns used | What it means |
| :--- | :--- | :--- |
| `move` | `x y z` | Walk on the navmesh to the point. A plan leg's `to` and every via point of the route are moves. |
| `move_direct` | `x y z` | Walk a straight line to the point (a short gap the navmesh does not bridge). |
| `pull` | `x y z` (centre), `radius` (0: 15), `entries`, `flags` | The pack: every living unit of `entries` within `radius` of the centre. `flags 1`: the pack is stealthed; the party walks through it and fights what shows. |
| `boss` | `entry`, `x y z`, `entries`, `radius` (0: 30), `flags` | Kill `entries` in that order, then the boss `entry`. |
| `loot` | `entry`, `x y z` (near), `item` | Loot a chest or a corpse of that entry near the point; `item` must be in the bags afterwards. |
| `open` | `entry`, `x y z` (near), `item` | Use a game object (a door, the Keystone); `item` is what its lock needs. |
| `use_item` | `entry` (the item), `item` | Use an item from the bags; `item` is what the use must make. |
| `wait_object` | `entry`, `x y z` (near), `param`, `timeout_s` | Wait until the object is in that state (the Seal of Khaz'Mul opens). |
| `wait_clear` | `x y z`, `radius`, `timeout_s` | Wait until no living hostile stands within `radius` of the point. |
| `rest` | — | A rest gate even when nobody is low. |
| `ritual` | `entry`, `x y z` (near) | An object several players must use together. Loaded; not run yet. |

The loader skips a row it cannot use and names it in the server log (`ai_dungeon_step: plan ... skipped`): an unknown kind, a `pull` without entries, a `boss`/`loot`/`open`/`use_item`/`wait_object`/`ritual` without `entry`, a negative or over-100 radius, a `wait_clear` without one, a timeout over 600 s, a `wait_object` state above 2, a duplicate key, or a step of a plan with no `ai_dungeon_plan` row, or an `entries` text that is not a comma list of non-zero entries (`BadEntries`). It also logs a plan whose consecutive moves are more than 60 yd apart (the exporter should cut such a leg into via points). The rules are `NormalizePlanStep` in `ai/playerbot/DungeonPlanPolicy.h`; `tools/test_dungeon_plan_policy.cpp` and `tools/test_dungeon_plan_sql.py` test them.

### From plan v1 to rows

| Plan v1 | Rows |
| :--- | :--- |
| `legs[i].name` | `ai_dungeon_leg` (`leg` = i) |
| `legs[i].to` (and the route's via points before it) | `move` |
| `legs[i].pull_groups[j]` | `pull`: centre = the mean of `positions`, `radius` = the group's `radius` or the farthest position from the centre plus 10 yd, `entries`, `flags` from `ambush` and `team` |
| `legs[i].steps[]` / `after_boss[]` `use_object` | a `move` to `stand` when given, then `open` (`item` = `requires_item`) |
| `wait_for.gameobject` | `wait_object` (`param` from `state`) |
| `wait_for.hostiles_gone` | `wait_clear` |
| `loot_object` | `loot` (`item` = `expect_item`) |
| `use_item` | `use_item` (`item` = `expect_item`) |
| `legs[i].boss` | `boss` (`entries` = `first`, `flags` from `team`); its `loot_item` becomes a `loot` of the boss entry right after it |
| `legs[i].after_boss` | after the `boss` row, same kinds as `steps` |
| `cc_priority`, `cc_always`, `kill_first` | `ai_dungeon_plan` columns of the same names, comma lists of creature entries as `entries` (`''` none). The exporter at turtle-agent 6cd0b13 does not write them yet (and its plan has no `kill_first`); the Zul'Farrak row carries them set by hand, pending pjerra/turtle-agent#190 (`20261015200100_world.sql` as applied, then the owner's values of 2026-10-15 in `20261015200200_world.sql`, whose header records that they are hand-set: polymorph the Shadowhunter 7246, whose Hex takes the tank out, and kill the ward 8179, then the Witch Doctor 5650, first: `cc_priority` 7246,7247,5648, `cc_always` 7246, `kill_first` 8179,5650,7247); `tools/test_dungeon_plan_sql.py` fails if the newest Zul'Farrak row loses `cc_always` or `kill_first` (a re-export from an exporter without them). |
| `skip`, `rest`, `companions_loot`, `watchdog` | not carried over yet |

## Run leader

Owner decision (2026-10-14): bots may clear a dungeon by themselves as an **opt-in** mode, off by default. It needs `AiPlayerbot.DungeonSelfClear = 1`, `AiPlayerbot.RemoteCommander = 1` and a remote commander binding (`.bot commander bind <LeadBot>`): the run leads the bound party, nobody else's. The commands and the `TBM:RUN` status line are in [Player Controls §1b](player-controls.md#1b-run-leader-bot-run-dungeon-self-clear).

It is the **observe** mode of the dungeon modes: the bots clear, the commander (a player, or turtle-agent with `--mode observe`) only watches the status line and the `TBM:VIEW` feed, and may pause, skip, resume or stop.

### How it plays

Once a second the run reads the party (alive, dead, in combat, health, mana, spread) and the current step's surroundings, and the step machine (`runtime/DungeonRunPolicy.h`) answers with at most one order. The order is sent exactly as the commander's own `.bot cmd <order>` (`move`, `move ... direct`, `pullback`, `rest`, `loot`, `open`, `use`): same checks, same ACK/ERR lines. The fights are the bots' own combat AI, with the [boss tactics](boss-tactics.md) of a bound party.

| Step | What the run does |
| :--- | :--- |
| `move`, `move_direct` | Before the hop: pulls a hostile within 15 yd of the hop's point that no later `pull` or `boss` step owns. Then `cmd move` to the point; done within 3 yd, or within 6 yd once the lead bot has stopped gaining ground for 12 s (a point just off the mesh or on a ledge). A point more than 60 yd away is walked to in navmesh hops first. A `move_direct` first gathers the party on the lead bot (`cmd move ... direct` refuses while a bot has no way to the lead's spot); its line-of-sight check stays (a direct step never cuts through a wall), and with the steps run in order from a gathered party the Sentinel ramp's two direct steps pass. A `cmd move` refused as `unreachable` or `off-mesh` is not skipped: the run looks once for the nearest point within 15 yd of the plan's that the lead bot can walk to (the point on the ground, then rings of 3, 6, 10 and 15 yd) and walks there by the navmesh path around what is in the way; with none, it pauses and says so. No progress for 12 s: the move again, at most 3 times, then pause. |
| `pull` | Not within 60 yd of the centre yet: walks toward it in hops of the lead bot's own navmesh path (at most 40 yd each, stopping 30 yd short; at most 8 hops; no path at all pauses the run). Then, nearest first: gathers the party (every bot within 20 yd of the lead bot, `cmd move` to the lead bot's own spot; 30 s at most) and sends `cmd pullback <guid>`. A pull that starts no fight in 25 s is tried once more, then that unit is passed. A unit that survives 3 pulls that did start fights (it evades, leashes home or stands out of reach) is left as out of reach, and so is the rest of a pack after 3 pulls in a row that killed nothing (a pit, a ledge): the step ends *skipped, not cleared: N units out of reach*, and those units are never pulled again in the run (hops included). Done when no living unit of the pack that is not friendly (hostile or neutral) is left within its radius. A stealthed (ambush) pack: the party walks through its centre and fights what shows. **Crowd control and kill-first** (the plan's `cc_priority`, `cc_always`, `kill_first`): the pull target is a `kill_first` unit of the pack when one is there (best rank, then nearest), else the nearest. Before that pull, when the party has a living mage that knows Polymorph and another unit of the pack is in the plan's cc lists, the run sends `cmd cc moon <guid> <mage>` on the best-ranked one, never on the pull target. The ranking is `cc_always` first, in its order (such a unit gets the Polymorph whenever it is in the pack, whatever `cc_priority` unit stands beside it), then the `cc_priority` entries `cc_always` does not list. That is one Polymorph at a time: no second one while the first unit lives, and a unit is crowd-controlled once per step. A crowd-controlled unit is pulled last. The mage casts with the fight (its `cc` strategy is a combat one), so the run checks there that the Polymorph aura shows on that unit within 8 s of the mage's own fight time (it waits for the tank's threat and walks into range first); if not, it sends the mark once more, and if it still does not show, the note says *cc did not land on <entry>* and the fight goes on. In a fight, the plan's `kill_first` units of this fight get `cmd focus <guid>` on the best-ranked one, sent again only when that changes (a ward comes up, the focused one dies). "Of this fight" means in combat with a party bot (its victim, or attacked by one), or summoned by a unit that is (a healing ward), so an unrelated ward nearby is left alone. They are read from the bots' own cached attacker lists (which carry the attackers' totems), with no grid scan. A spell is not cast at a hostile totem (a healing ward is one) when its effects on that target can do nothing there; an area spell around the caster or at a place (Demoralizing Shout, Challenging Shout, Psychic Scream, Rain of Fire) is not judged by the current target. The rule mirrors the core's `Totem::IsImmuneToSpellEffect` (Totem.cpp): a totem takes nothing from a taunt, a heal or an energize, nothing from any aura of a spell that is not positive (DoTs, curses, stings, Hunter's Mark, Faerie Fire, stuns, slows, crowd control), and nothing from a positive spell's periodic heal or energize. So the bots use strikes, auto shot, the wand and direct-damage spells on it (an area spell like Rain of Fire is not in the core's list and still goes out). A crowd-controlled unit is not focused. A ward is read off its owner the moment it is up (the owner's totems and guardians), not when a bot's cached list next takes it in. While a run is going, the party's crowd control takes only the run's ordered mark (no free picks), and no bot ever crowd-controls a totem. A unit an NPC summoned or created (a mob's pet, a skeleton) stays a crowd-control target, except while the party has a run going and the run's plan lists it `kill_first` (a healing ward is killed, not polymorphed). A refused `cc` or `focus` only notes it. |
| `boss` | As `pull`, in kill order: the `entries` first, the boss last. A neutral boss is pulled; only a friendly one is left alone. **Boss down** needs the boss's death in sight: a dead unit (his corpse) of the boss entry within the step's radius plus 60 yd of his spot (a pullback drags him toward the party), or, once he was seen alive in this step, within 60 yd of the lead bot wherever he fell. A boss neither alive nor dead there is away (a patrol, Theka in Zul'Farrak), not killed: the party waits at his spot for him (the rows carry no patrol path) and pauses after 60 s with *boss <entry> not found* (`skip` if he is dead already). |
| team flags (`2` Alliance, `4` Horde) on `pull`/`boss` | A `pull` for the other team is skipped without a walk to it, unless the party already stands within 60 yd and a unit of it is not friendly. A `boss` for the other team is always walked to and looked at, and skipped only when every unit of its encounter there is friendly to the party; a hostile or neutral one is fought (the Lost Dwarves carry team horde in the plan, yet were hostile to an Alliance party in a live run). A later step for the other team owns nothing (its units near a hop may be pulled). |
| `loot` | Already carried: done. Else `cmd loot entry:<id>@x,y,z <lead bot> [item]`, waits for the task, and pauses when the item is not in the bags afterwards. |
| `open` | Pauses when the lead bot lacks the item the lock needs; else `cmd open entry:<id>@x,y,z <lead bot>` and waits for the task. |
| `use_item` | Already made: done. Else `cmd use <item> <lead bot>`; pauses when the made item is missing. |
| `wait_object` | Reads the object's state near the point (like `cmd state`) until it matches; `timeout_s` then pause. |
| `wait_clear` | Walks to the spot if needed; waits until no living hostile is within the radius; `timeout_s` then pause. |
| `rest` | `cmd rest` and waits as the rest gate does. |
| `ritual` | **Not supported yet**: the run pauses and names it (do it by hand with `.bot cmd ritual`, then `skip`, `resume`). |

The lead bot is the carrier of every object and item step.

### Safety rules

From turtle-agent's remote driver (the same rules, now on the server):

- **Gather before a pull**, and pull only what is near: the nearest unit of the step, or of the next hop.
- **Never pull what a later step owns**: a unit near the next hop that a later `pull` or `boss` step lists (by entry, within its radius) is left for that step.
- **No unseen pack counted cleared, no live boss counted dead**: a pack or boss is judged only while the lead bot stands within 60 yd of it (its grid is loaded there), and only "no living unit left that is not friendly" ends it.
- **3 dead (or a wipe) regroup by itself**: once the fight is over. With 3 or more dead and someone still fighting, the run does not pause: the living fight on until the fight ends, and a fight that goes past the fight ceiling (180 s, a boss 420 s) with 3 or more dead is taken for the wipe it is and starts the regroup too (within the step's budget). A lead bot that dies first does not pause the run while the others fight; if they fall too, it is a wipe. The run revives the party (`cmd revive party`, as below), gathers it, and then **walks back**: first straight to the last `move` point before the step, in navmesh hops (at most 40), with the pull-before-move check at each hop, but only while the navmesh path read for each hop, its last leg included, keeps more than the group's radius plus 10 yd from every pull or boss group not cleared yet: the current and later ones, and earlier ones left uncleared (skipped, out of reach). The shortest way ignores packs and would body-pull them; only when that fails (no path, or the hops run out) does it replay the plan's own `move` and `move_direct` points in order, from the one nearest the lead bot (the instance entrance after a corpse run) up to the current step. Then it starts the step over. It is the way the run came. Each replayed point is walked as its `move` step is: a point more than 60 yd away (the party back at the entrance, a point beyond ReactDistance) is approached in navmesh hops as a resume does, and one with no path toward it at all is passed for the next; a hostile within 15 yd of it is pulled first (at most 8 such pulls in a walk back, counted apart from the step's 20, then pause) (a pack of a step already done that came back; the current step's own pack and later ones are left to their steps); a point refused now because it cannot be walked to (`unreachable`, `off-mesh`, `no-ground`, `no-los`) is passed for the next one, while a refusal for the lead bot's state of the moment (`lead-unavailable`, `busy` and the like) sends the same point again, at most 3 times, then pauses; and a party that came back near the step walks nothing. At most **2** regroups by itself per step; the third pauses the run as before (*3 dead: regroup (cmd revive party), then .bot run resume*). A dead lead bot alone (fewer than 3 dead) still pauses. Each wipe (the whole party dead) and each near wipe (3 or more dead, someone still alive) is counted once (a near wipe that ends in a wipe counts as both); the counts are in the regroup's note (*5 dead (2 wipes, 1 near wipe this run): regrouping by itself (1 of 2 at this step)*) and in the end note (*plan done (2 wipes, 1 near wipe)*).
- **`.bot run resume`** (or `skip` from a pause) first **regroups**: `cmd revive party` while anyone is dead (again every 60 s; 240 s at most; a released lead bot whose ghost is outside the instance meanwhile does not pause it), then the party gathers on the lead bot (60 s at most); a lead bot far from the step (back from the entrance) walks back as after a regroup by itself (straight to the last plan point in hops, else the replay). A resume gives the step its 2 regroups by itself back.
- **A gather goes back to the run's anchor** (the last point the run itself walked the party to: a move, an approach hop, a stand point; after a stalled arrival short of a point, where the lead bot stopped) when a fight left the lead bot off it: more than 8 yd above or below it (a tank that chased over a ridge) or with no navmesh path back. The lead bot then walks back to the anchor and the party gathers there; no pull goes out while the lead bot is still off it. On the anchor's floor with a path back, the gather stays on the lead bot's own spot. After a regroup there is no anchor until the run walks again.
- **A refused gather does not pause**: a gather (`cmd move` to the lead bot's own spot, before a pull, a direct step or in a regroup) that is refused is sent once more; refused again, the run goes on without it (that pull or step only; the next one gathers again). The run leader's own gather (`cmd move` to the lead bot's spot, within 3 yd of it) is always accepted: the path finder could answer `unreachable` on the very spot the lead bot stands on; a player's `cmd move` to a point that close is checked as any other. A `pullback` refused as `invalid-target` (not an enemy, or the group cannot attack it: Zul'Farrak's Sul'lithuz Hatchlings 8130 by Antu'sul) leaves that unit out for the run and the step goes on. Other refused orders still pause the run.
- **A straggler is summoned before a gather pauses**: at a gather's ceiling (30 s; 60 s in a regroup) the farthest bot more than 40 yd from the lead bot, or more than 8 yd above or below it (another floor), that came less than 5 yd closer since the last look (when the gather went out, then at each ceiling: stuck behind, not on its way) is summoned to the lead bot (`cmd summon <bot>`), once per bot and step, and the gather waits again; still not gathered, the run pauses as before. A refused summon only notes it.
- **A missing item pauses once**: a `loot` or `use_item` that does not get its item pauses, naming it; after `.bot run skip` the item is noted missing, and the later steps that need it (a `use_item` of it or one that fails because of it, an `open` whose lock needs it, the `wait_object` right after such an `open`) are left out with a note instead of failing one after the other.
- **Switched off mid-run** (a config reload sets `AiPlayerbot.DungeonSelfClear = 0`): every run ends with a last status line.
- **Every wait is bounded**: gather 30 s, a pull's fight start 25 s, a fight 180 s (a boss 420 s), a rest 60 s (it then goes on), a task 90 s, a step 300 s out of combat (a wait step: its timeout plus 60 s), at most 20 pulls per step. A ceiling pauses the run with the reason; nothing loops.
- **Any player order pauses it**: `.bot cmd` (but `state`), `.bot action`, and any chat command a real player gives a bot of the party (a whisper such as `stay`, `follow` or `attack`, `.bot command`, party chat) that passes the bot's command check.
- **Rest gate** after every fight and before every step: when a living bot is under 80 % health, a healer under 50 % mana or another mana user under 35 %, `cmd rest` (again every 20 s) until everyone is at 90 % health, healers 80 % and others 60 % mana, or 60 s.

### Not supported yet

- `ritual` steps (Uldaman's Altar of the Keepers, Archaedas) pause the run.
- A party with a real player in it (a player leading their own party): the run leads bound parties only.
- Plan v1's `skip`, per-leg `rest`, `companions_loot` (Revelosh's Shaft: the run's `loot` takes the corpse with the lead bot, but other bots may loot it first), `area_trigger`, gossip and waves.

### Cost

Nothing while no run exists. A running run ticks once a second (never per bot tick) and does at most one local grid search around its lead bot, only when the step's point is within 60 yd. No database access after startup.
