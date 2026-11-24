---
id: play-commands-commander
title: Commander Commands
category: play
summary: GameMaster commands that bind bots to a commander, issue party orders, and report a binding.
tags: [commands, commander, party-orders]
relates_to:
  - play-commands
  - play-commands-party-actions
  - play-commands-roster-and-hiring
  - play-dungeon-run-leader
---

# Commander Commands

On ForeverCore, the commander machinery is a GameMaster command surface: `.tb` typed in chat by a GameMaster, or `tb` on the worldserver console. A normal player's bots take single-bot chat orders (`.bot follow`, `.bot stay`, `.bot attack`, `.bot flee`) and the addon's party action buttons; `.tb master` and `.tb cmd` are not open to players.

The commander commands bind a group of bots to one commander. The commander usually stands outside the bot party and can give orders from anywhere on their map. The bots' own combat AI still fights; the orders say what to fight, where to stand, and where to walk.

## Summary

| Command | What it does |
| :--- | :--- |
| [`.tb master`](#binding-a-party) | Binds bots (or a bot party) to a commander, or releases them. |
| [`.tb commander status`](#reading-a-binding) | Reports every standing binding and its members. |
| [`.tb cmd`](#party-orders) | Gives a party order to your bound group. |
| [`.tb order`](#single-bot-orders) | Gives one order to one bot, or to a bot party. |
| [`.tb run`](#bot-led-dungeon-runs) | Starts, pauses, resumes, stops and reports a dungeon run of a bot party. |

## Binding a Party

```text
.tb master <bot|party [leader]> <commander|none>
```

- The commander must be a logged-in bot or your own character. A bot cannot command itself.
- Addresses: one bot by name; `party <leader>` for every host bot in that character's party, with that character as the binding's lead; `party` alone for the bots already bound to you, else the one commander's bots, else the one bot-led party.
- `none` releases the bots; they go back to following their own masters.
- Binding does not move anyone and does not change the group.
- A binding ends by itself when the party it was made on stops being that party: the lead leaves the world or the party, the group leadership changes, another player joins a binding whose commander is outside the party, the party becomes a raid or grows past five, the lead lies dead past the dead grace, the commander leaves an in-party binding, or the commander is away past the master grace.
- Bindings are kept in memory only unless `TortoiseBots.Commander.KeepBindings = 1` (default `0`), so a restart drops them.

## Reading a Binding

```text
.tb commander status [<Commander>]
```

Reports every binding, or the one commander's. There is no separate bind or unbind verb: `.tb master` does that, and this command only reads. For each binding you get the commander, its lead bot, the member list with class, role, level, health, power, position and raid subgroup, the last orders, and the run record when the group runs a plan.

## Party Orders

```text
.tb cmd <verb> [arguments]
```

The orders go to the party your binding commands; `.tb cmd` needs a player character as the sender and a standing binding (`not-bound` otherwise). Combat, movement and placement orders are logged on the binding card; object tasks are not.

| Verb | Use |
| :--- | :--- |
| `move <x> <y> <z> [direct]` | The lead bot walks to the point; followers follow. `direct` walks a straight line instead of a navmesh path. |
| `walk <bot> <x> <y> <z>` | One named bot walks to the point. |
| `attack <guid> [bot]` | The party attacks the unit; naming a bot sends only that bot. Healers keep healing, and a tank on a mob it holds stays on it. |
| `pull <guid> [seconds] [ranged\|body]` | The lead tank pulls. The rest hold at the lead until the pull lands, then join after the delay. |
| `pullback <guid> [seconds] [ranged\|body]` | The tank opens from range and returns to the lead anchor; the rest join after it is back. |
| `focus <guid>` | Sets Skull on the target and orders focused damage. |
| `cc <mark> <guid> [bot\|family] [family]` | Gives a crowd-control mark to a capable bot. |
| `cc clear [bot] [mark guid]` | Clears a bot's crowd-control mark; with a mark and a unit, clears that icon too. |
| `stay` / `follow` / `hold` / `rest` | Party-wide movement or rest. `rest` sits, eats and drinks. |
| `revive party` | Releases every dead bound bot's spirit. A single bot's revive is not carried out. |
| `stop [bot]` | Stops attacks and clears the movement generator. The bot still defends itself when hit. |
| `flee` | Breaks combat and runs the flee behaviour. |
| `guard` | Each bot guards the spot it stands on. |
| `stance <near\|tank\|turnback\|turn\|behind> [bot]` | Sets the combat stance; `turn` reads back as `turnback`. |
| `engage [seconds 0-60]` | Holds the party until your first hit, then joins after the delay (default 4). |
| `rebuff` | Opens the force-rebuff window (out of combat only). |
| `interrupt <guid> [bot]` | Interrupts the target's cast with a bot that has a ready interrupt. |
| `aoe [on\|off\|status] [bot]` | Area-of-effect switch. |
| `autocc [on\|off\|status] [bot]` | Automatic crowd control on loose adds. |
| `sap [on\|off\|status] [bot]` | Rogue pre-pull Sap switch. |
| `corpserun [bot]` | Sends a dead bot's ghost back to its corpse. |
| `loot <my\|guid\|entry:<id>> [bot] [itemId]` | A bot loots a corpse or container. |
| `open <my\|guid\|entry:<id>> [bot]` | A bot walks to and opens a door, lever, chest or goober. |
| `use <my\|guid\|entry:<id>> [bot]` | A bot walks to and uses a game object (same path as `open`). Passing an item id (`<itemId>` or `item:<id>`) is refused as `unsupported`. |
| `gossip <my\|guid\|entry:<id>> [bot] [option]` | A bot picks one gossip option of an allowlisted NPC. |
| `lootmethod <ffa\|group\|rr\|master\|nbg> [threshold]` | Sets the party's loot method. |
| `state [bot]` | Reads each bound bot's current task. |
| `place ...`, `rally ...`, `fallback ...`, `resume` | Positions the party in a shape, stores a fall-back point, runs the fall back, or ends it. |

Targets are a 32-hex GUID, `my` for your selection, or an object entry as `entry:<id>[@x,y,z]`. Accepted orders carry `status=accepted`; refusals answer one `error` line with a stable code (`not-bound`, `invalid`, `no-target`, `dead-target`, `no-bot`, `unsupported`, and the order's own codes).

## Bot-Led Dungeon Runs

```text
.tb run start <leader> <plan> [from step <N> | from leg <number|name>] [lead=player]
.tb run stop <leader> [reason]
.tb run pause <leader>
.tb run resume <leader>
.tb run status <leader|all>
.tb run events <leader> [since=<seq>]
```

The leader is a tank bot that leads its party: a warrior tank or a bear druid with the `pull` strategy. `start` binds the other members to the tank and the run clears the plan by itself. `lead=player` is the opt-in player-led mode: you lead and walk, and the tank bot pulls for you. Plans, steps and safety rules: [Dungeon Run Leader](../dungeon-run-leader.md).

## Single-Bot Orders

`.tb order <bot|party [leader]> <verb> [argument]` queues one order for one bot or a bot party. It uses the same engine actions as the commander orders: `follow`, `stay`, `come`, `attack`, `pull`, `pullback`, `focus`, `endpull` (`end`), `hold`, `assist`, `release`, `stop`, `flee`, `guard`, `stance`, `engage`, `rebuff`, `interrupt`, `aoe`, `autocc`, `sap`, `loot`, `corpserun`, `co`/`nc` for strategy changes, and upkeep orders such as `repair`, `sell`, `train` and `rest`. It has no commander binding requirement.

One difference from `.tb cmd`: `.tb order party loot on|off|status` is the loot toggle, because `.tb cmd loot` is the object-loot task.

A player with no GameMaster account has the four single-bot orders in chat: `.bot follow <bot> [member]`, `.bot stay <bot>`, `.bot attack <bot>`, `.bot flee <bot>`. See [Tactical Party Actions](party-actions.md).

## Not on ForeverCore Yet

The Turtle player commands `.bot commander bind`, `.bot commander unbind`, `.bot commander view`, `.bot cmd` and `.bot run` do not exist here. `AiPlayerbot.RemoteCommander` is not read by any order path, so it gates nothing. The live perception feed (`TBM:VIEW`) and the GameMaster `.bot scout` path tool are not implemented.
