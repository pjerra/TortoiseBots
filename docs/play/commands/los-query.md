---
id: play-commands-los-query
title: Line of Sight Query
category: play
summary: The GameMaster command that asks the server whether two units can see each other, with the distance, for planning pull stands and diagnosing sight problems.
tags: [commands, diagnostics, line-of-sight]
relates_to:
  - play-commands
  - play-dungeon-run-leader
  - play-commands-roster-and-hiring
---

# Line of Sight Query

`.tb los` asks the SERVER whether one thing can see another. The bots' own planning tools estimate sight with a flat
navmesh ray, which can disagree with the server in both directions — a mob slightly above a tunnel floor is the classic
case — so this command answers with the server's own line-of-sight test: the same test a bot makes before it accepts an
attack target. It changes nothing: no movement, no target change, no combat state.

It is a GameMaster and console command. A player who is not a GameMaster may use it only on a unit of their own: their
own character, or a bot in a run or party they lead or observe. It never answers about another player's character, so it
cannot be used to look at players — and it does not even say whether such a character is online: for a caller below
GameMaster, another player's character and a name that does not exist get the same `no-access` answer, which shows only the
words that were typed and no map or world state.

## Usage

```
.tb los <a> <b> [map=<id>]
.tb los <a> <b> #token
```

Each side is one word:

- a character or bot name, for example `Mtank`;
- `target` — the caller's own current selection (the console has none);
- a guid, as the module prints it (32 hex digits, or with the core's `0x` in front);
- a position `x,y,z`, for example `-601.25,-4250.5,38.9`, on the other side's map.

Two positions have no unit to borrow a map from, so a position-to-position query needs `map=<id>`. With a unit side the
unit's own map is used, and `map=` is refused. A position-to-position query is answered on the copy of that map the
server has loaded first (the continent, or a dungeon's first loaded instance); it cannot pick another copy of a dungeon.

## The answer

One line, always:

```
los a=<name|guid|x,y,z> b=<...> map=<id> los=<0|1> dist=<yards, one decimal> same_map=<0|1> in_world=<0|1> [reason=<code>]
```

- `los=1` means the server's line of sight is clear.
- `dist=` is the horizontal distance in yards with one decimal, and height is never added: between two units the game
  subtracts both bodies' reach, between a unit and a position it subtracts only the unit's reach, and between two positions
  it is the plain centre-to-centre distance.
- `same_map=` is `1` only when both sides are on the same map **and in the same instance of it**: two bots in two copies
  of one dungeon answer `same_map=0` and are refused with `different-maps`. `in_world=` says whether both sides are in
  the world at all.
- `reason=` appears only when the query was refused: `bad-argument`, `unknown-unit`, `not-in-world`, `different-maps`
  (a different map, or a different instance of the same map), `no-access` or `rate`. A refused query still prints the same
  line, so tools read one shape either way.

This line is a **stable interface**: the format, the key names and their order do not change without a note here. The
agent's plan scout records true sight with it; other tools may parse it too.

```
.tb los Mtank target
los a=Mtank b=Kobold map=389 los=1 dist=12.3 same_map=1 in_world=1
```

The line of sight is the server's own test for an attack target: the ray leaves and reaches at the height the game uses
(a player's eye height above the feet, a creature's body point), so it agrees with what the bots see. The command does not
apply the attack rules about distance, friendliness or immunities — it answers geometry, and the distance is printed so you
can judge reach yourself.

At most five queries a second per caller are answered; the sixth answers `reason=rate`. A caller is a character, and the
console and SOAP have no character, so every tool call is one caller: console and SOAP queries together get five a second.
That bucket is per caller, so it never affects anyone else.

The full command table, with every key and refusal, is `forever/docs/TB-COMMANDS.md` (section 24).

## Why it exists

Pull stands and patrol routes are planned before anyone pulls. A stand that "looks" clear on the map may have no true sight
of the mob, or the other way round: a stand that looks blocked may be in plain sight because the mob's body point clears the
ledge. Asking the server removes the guess, and each answer can be recorded by the planner so the next run starts from
known-good stands.
