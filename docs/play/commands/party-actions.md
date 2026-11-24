---
id: play-commands-party-actions
title: Tactical Party Actions
category: play
summary: Party combat orders, toggles and movement on the party chat command, the addon and the GameMaster order commands.
tags: [commands, tactics, party-actions]
relates_to:
  - play-commands
  - play-commands-commander
  - play-commands-addon
  - server-settings-party
---

# Tactical Party Actions

You do not micromanage abilities: you give an order and the bots carry it out with their normal rotations. On WoW Forever there are three order surfaces:

| Surface | Who | What it covers |
| :--- | :--- | :--- |
| `.bot <verb> <bot>` chat | a player who owns, masters or commands the bot, or leads its party as a real player | `follow`, `stay`, `attack`, `flee` for one named bot |
| Addon buttons (`action <intent>` over the addon channel) | the same players | the combat and upkeep buttons of the TortoiseBotsForeverManager party and actions panels |
| `.tb cmd ...`, `.tb order ...`, `.tb cc ...`, `.tb strategy ...` | GameMaster (chat or console) | every party order, crowd control, strategies, object tasks |

A bot inside a running dungeon run refuses every `.bot` order, GameMasters included; use `.tb run pause` first.

## Single-Bot Chat Orders

```text
.bot follow <bot> [member]
.bot stay <bot>
.bot attack <bot>
.bot flee <bot>
```

- The bot must be online and a bot; ownership is by Battle.net account, and a bot's master, the commander of a binding, and the real player who leads the bot's party may also order it.
- `follow` with a member name makes the bot follow that party member instead of its master.
- `attack` reads your current selection and refuses with `no-selection` when nothing is selected.
- `flee` sends the bot to the flee behaviour.
- Answers are `TortoiseBots: <verb> <Bot>: ok order <seq>`; refusals name a code (`not-found`, `not-a-bot`, `not-yours`, `in-run`, `no-selection`, `no-engine`, `queue-full`).

## Addon Action Buttons

The party and actions panels send `action <intent>` to the server over the addon channel. The intent grammar is the Turtle one; these intents are carried out:

| Intent | What happens |
| :--- | :--- |
| `attack` | Sends the scoped bots at your target. |
| `interrupt` | The first scoped bot with a ready interrupt casts it at your target. |
| `stop` | Stops attacks; movement is kept. |
| `flee` | Breaks combat and runs the flee behaviour. |
| `guard` | Each bot guards the spot it stands on. |
| `stance <near\|tank\|turnback\|behind>` | Sets the combat stance. |
| `engage [seconds 0-60]` | Holds the bots until your first hit, then joins after the delay (default 4). |
| `rebuff` | Opens the force-rebuff window out of combat. |
| `pull [seconds] [ranged\|body]` | Orders a pull by the tank; a bot that cannot pull refuses. The party holds and then joins. |
| `pullback [seconds] [ranged\|body]` | Orders the tank to open from range and return to you. |
| `end pull` | Aborts the running pull. |
| `come` / `hold` / `comestay` | Walks the bots to your spot and sets them to stay. |
| `follow` / `stay` | Formation follow, or stay in place. |
| `focus skull` | Marks your target (or the existing Skull) and orders focused damage. |
| `cc clear [bot]` | Clears a bot's crowd-control mark. |
| `release` | Releases dead bots' spirits. |
| `repair` / `sell` | Repairs, or sells grey junk, at a nearby vendor. |
| `learn` | Trains at a nearby trainer. |
| `rest` / `drink` / `eat` | Sits, eats and drinks. |

The scope is your selected bot when it is a live bot you may order, otherwise the bots of your party you may order, or, when you are ungrouped, the bots you master. Refusals come back as `TBM:ACTION_ERR|<intent>|<code>|<message>`; success as `TBM:ACTION_ACK|<intent>|<scope>|<count>|<detail>`.

The server advertises one token per intent in its list (`action:<intent>`), plus the `pull-seconds`, `flee` and `engage` capabilities. An intent the build refuses answers `TBM:ACTION_ERR`.

## Crowd Control

```text
.tb cc <bot> <mark> <unit guid|target> [family]
.tb cc <bot> clear [<mark> <unit guid>]
.tb cc <bot|party [leader]> [class] allow|deny|list [kinds]
```

- Marks are `star`, `circle`, `diamond`, `triangle`, `moon`, `square` and `cross`. Skull is the party's kill mark, never a crowd-control mark.
- Giving a mark makes that bot the mark's owner, takes it from any other bot that held it, and moves damage bots that focused it back to Skull.
- `clear` resets the bot's own crowd-control mark; with a mark and a unit it also removes that icon while it still sits on that unit.
- The `family` word pins the spell family (`polymorph`, `seduce`, `fear` and so on); without it the bot keeps its usual choice.
- `allow`/`deny`/`list` store per-class permissions for the bot.

There is no group-wide "assign a mark to the best bot" order on Forever; the commander path is `.tb cmd cc <mark> <guid> [bot|family] [family]`.

## Combat and Movement Orders for a Commander

A GameMaster with a standing binding gives the party-wide versions through `.tb cmd`:

| Order | Notes |
| :--- | :--- |
| `.tb cmd attack <guid> [bot]` | Healers keep healing and a tank stays on a mob it holds unless named. |
| `.tb cmd pull <guid> [seconds] [ranged\|body]` | Optional join delay 0–60; `ranged` needs a working shot, `body` always walks in. |
| `.tb cmd pullback <guid> [seconds] [ranged\|body]` | Join delay defaults to `AiPlayerbot.PullBackDpsJoinDelay` (5 s); the return is capped by `AiPlayerbot.PullBackMaxReturnTime` (15 s). |
| `.tb cmd focus <guid>` | Skull plus focused damage. |
| `.tb cmd interrupt <guid> [bot]` | One bot with a ready interrupt. |
| `.tb cmd stop [bot]` / `flee` / `guard` | Break off, run, or hold the current spot. |
| `.tb cmd stance <near\|tank\|turnback\|behind> [bot]` | Combat stance. |
| `.tb cmd engage [seconds]` | Player-pull hold. |
| `.tb cmd rebuff` | Force a buff pass out of combat. |
| `.tb cmd rest` / `revive party` | Sit and recover, or release all dead bots. |
| `.tb cmd corpserun [bot]` | Send a ghost back to its corpse. |
| `.tb cmd move <x> <y> <z> [direct]` | Walk the lead bot; followers follow. |
| `.tb cmd place <shape> ...`, `rally ...`, `fallback ...`, `resume` | Arrange the party, store a rally point, fall back to it, or end the fall back. |

## Toggles

```text
.tb cmd aoe [on|off|status] [bot]
.tb cmd autocc [on|off|status] [bot]
.tb cmd sap [on|off|status] [bot]
.tb order <bot|party> loot [on|off|status]
```

- `aoe`, `autocc` and `sap` are the combat toggles; the values live in memory for the server session.
- `loot` is the corpse-looting toggle through `.tb order`; `.tb cmd loot` is the object-loot task (below), so the toggle word does not work there.
- The same switches are available per bot in chat as `.bot behavior <bot|party> <aoe|autocc|loot|...> <on|off>`.

## Object Orders

```text
.tb cmd loot <my|guid|entry:<id>> [bot] [itemId]
.tb cmd open <my|guid|entry:<id>> [bot]
.tb cmd use <my|guid|entry:<id>> [bot]
.tb cmd gossip <my|guid|entry:<id>> [bot] [option]
.tb cmd lootmethod <ffa|group|rr|master|nbg> [threshold]
.tb cmd state [bot]
```

`loot`, `open` and `gossip` walk the bot to the object and act through the bot's own interaction rules. `use` is parsed but refused: the item-use path is not ported. `lootmethod` sets the party's method on the lead bot's group. `state` reads each bot's current task.

## Strategies

```text
.tb strategy <bot> co|nc <+name,-name,~name ...>
.tb strategy <bot> reset
.tb strategy role:<tank|healer|dps> co|nc <changes ...>
```

Changes the bot's combat (`co`) or non-combat (`nc`) strategy set; `reset` restores the kit. This is the GameMaster equivalent of the Turtle `.bot strategy` command.

## Other Order Verbs

`.tb order <bot|party [leader]> <verb>` gives the same engine actions to one bot or a bot party without a commander binding: `follow`, `stay`, `come`, `attack`, `pull`, `endpull`, `hold`, `assist`, `release`, `stop`, `flee`, `guard`, `stance`, `engage`, `rebuff`, `interrupt`, `aoe`, `autocc`, `sap`, `loot`, `corpserun`, `co`/`nc`, and upkeep orders such as `repair`, `sell`, `train` and `rest`. See [Commander Commands](commander.md#single-bot-orders).

## Not on WoW Forever Yet

These Turtle actions are absent here: `.bot action ...`; the other single-bot chat commands (`.bot guard`, `.bot free`, `.bot ready`, `.bot interrupt`, `.bot pullback`, `.bot formation`, `.bot strategy`, `.bot loot`, `.bot repair`, `.bot sell`, `.bot rest`, `.bot downtime`, `.bot use`); per-bot focus marks; the `free` movement order; ready checks; trade from an order; the `raid ...` intents; auto-CC and Sap from an addon action; and the `use-object` button. Some remain reachable in another form: crowd control through `.tb cc` / `.tb cmd cc`, toggles through `.tb cmd aoe|autocc|sap` and `.bot behavior`, and object use through `.tb cmd open`.

**Not on WoW Forever yet.**
