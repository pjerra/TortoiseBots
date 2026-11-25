---
id: server-bot-pool
title: Managing & Resetting the Bot Pool
category: server
summary: How the ForeverCore pool creates its own accounts and characters, keeps a target number online, prunes failed rows, and resets at world startup.
tags: [server, pool, reset, admin, database, registry]
relates_to:
  - server-settings-world-population
  - server-admin-commands
  - server-install
---

# Managing & Resetting the Bot Pool

The pool is a set of bot characters the module **creates itself** and logs in as free bots on a schedule. Player characters and your own alts are never part of the pool. This page covers the registry, the scheduler, pruning and the opt-in reset.

The features the bots use while online are in [What bots can do](../play/features.md#the-living-world).

## The pool registry

Three character-database tables hold the pool:

| Table | What it holds |
| :--- | :--- |
| `tb_pool_account` | The accounts the module made (`TBPOOL0001` ...). `username`, a random creation stamp (written as the account's e-mail, the one field the core's account lookup reads back) and a state. |
| `tb_pool_character` | The characters on those accounts: name, guid (empty until the character exists), race, class, gender, state, last login and last logout. |
| `tb_pool_state` | One row: the last completed reset generation and when it finished. |

- **Ownership is the row, not the name.** Only a registered row is used. A character on a pool account without a ready row is kept and counted, never deleted. A real player's account has no row, so it can never enter the pool.
- **States.** `creating` is written before the account or character exists; only `ready` rows are used. A row whose account id, name or stamp no longer agree (a restored auth database, a reused id, a manual repair) becomes `stale` and is never acted on. A creation that cannot be finished becomes `failed`; only `.tb pool prune` removes it.
- **The module creates the tables itself** when `TortoiseBots.Pool.Enable = 1` and the database user may create tables. If it may not, apply the files in `forever/sql/characters/` by hand (see [Install](install.md#database)). With a table missing, the pool is off and logs one error line; the server stays up.
- **One character per account.** One session per account is the core's rule, so a second character on a pool account could never be online.

## Enabling and creating

```ini
# worldserver.conf.d/tortoisebots.conf
TortoiseBots.Enable = 1
TortoiseBots.Pool.Enable = 1
TortoiseBots.Pool.CreatePerMinute = 30
TortoiseBots.Pool.MaxCharacters = 50
```

1. Set the keys.
2. Create characters with [`.tb pool create <n>`](admin-commands.md#the-bot-pool). Creation is paced: one character per world tick at most, at most `CreatePerMinute` per minute, never past `MaxCharacters`.
3. Start the schedule with [`.tb pool start`](admin-commands.md#the-bot-pool).

`CreatePerMinute = 0` means nothing may be created; `MaxCharacters = 0` means the pool cannot grow. `.tb pool create` creates only what still fits under the cap and refuses a full pool.

### What a new character looks like

By default (`TortoiseBots.Pool.CensusComposition = 1`) the pool rolls each new character the way the live game looks (aotc.gg, read 2026-11-24): class by its share of the faction, race within the faction among the pairs ForeverCore can create, gender by the class's female share. The faction alternates, so the pool stays half Alliance and half Horde, and both Skyborne races are part of the mix. `0` keeps the old balanced sequence: the vanilla races in turn and a random class of the race.

A pool bot's spec tree follows the live spec shares of its class (`TortoiseBots.Pool.CensusSpec = 1`); `0` keeps the pool's own roll. Hired and commanded bots always get the spec their role needs.

## The scheduler

Once a second, the scheduler keeps `TortoiseBots.Pool.TargetOnline` pool bots of both factions online:

| Key | Default | Meaning |
| :--- | :---: | :--- |
| `TortoiseBots.Pool.TargetOnline` | 0 | Bots to keep online. 0 runs the schedule but logs nobody in. Up to 5000. |
| `TortoiseBots.Pool.LoginsPerMinute` | 20 | At most this many logins in any 60 seconds, and at most one per second. 0 = no logins. |
| `TortoiseBots.Pool.SessionMinutes.Min` / `.Max` | 30 / 90 | A session's rolled length in minutes. A living bot in combat keeps its session until the fight ends. |
| `TortoiseBots.Pool.OfflineMinutes.Min` / `.Max` | 5 / 20 | How long a bot stays away after its session. |
| `TortoiseBots.Pool.PopulationLow` / `.PopulationHigh` | 10 / 50 | The presets behind `.tb pool population low\|high`. |

- Logins alternate factions and take the bot that has been offline longest first, and only on an account without a session.
- A bot that gets a master is **bound**: it leaves the online count and the pool never logs it out. Its account is out of reach until the binding ends, so the pool may need more characters than its target to refill that slot. `.tb pool status` says `capped by accounts: N` when the target is above the accounts left.
- A lowered target logs out the newest sessions first, never a dead or fighting bot.
- `.tb pool stop` drains every pool bot at five logouts per second. A bot held in a running battleground is released when the match ends or after `TortoiseBots.Bg.MaxMinutes`.
- No login is planned while the core's shutdown countdown runs.

## Pruning failed rows

A creation the reconciliation cannot finish is marked `failed` and kept. It is never used and never deleted automatically:

```text
.tb pool prune            # dry run: what would be removed
.tb pool prune confirm    # remove it (needs the account-delete permission)
```

`confirm` re-verifies the database first. `.tb pool prune` alone changes nothing.

## Resetting the pool

A reset deletes every **ready** pool character, one per world tick, then refills to `TortoiseBots.Pool.MaxCharacters`. It is opt-in and runs **only during world startup**. `.reload config` never starts one, and there is no live reset command.

```ini
TortoiseBots.Pool.Reset = "off"              # default: never
TortoiseBots.Pool.Reset = "once:fresh-01"   # once, while the token differs from the last completed one
TortoiseBots.Pool.Reset = "always"           # every startup: development only
```

- A token is 1-128 printable, non-space characters. Leaving the same token in place makes every later start a no-op. `always` never overwrites a stored one-shot token.
- Needs `TortoiseBots.Pool.Enable = 1` and `TortoiseBots.Pool.CreatePerMinute > 0`, otherwise the reset refuses.
- The reset moves through planning, deleting, verifying and rebuilding. It records the generation in `tb_pool_state` only after the characters table proves every target gone; a crash mid-reset leaves it unrecorded, so the next start plans again from the remaining rows.
- A reset can shrink a pool that is larger than `MaxCharacters`.

### What it refuses before deleting anything

- A guild led by a pool character has any member outside the pool (including a member with no character row).
- A target owns an auction listing (the core's character deletion does not remove listings).
- A target account carries a player session.
- A database read does not answer.

Unlike the Turtle build, there is no auction settlement phase: the reset refuses a target with a live listing instead of refunding bidders.

### What a reset deletes

- The pool characters and their progress: levels, equipment, bags, bank, quests, professions. Deletion goes through the core's own character deletion.
- The `tb_pool_character` rows for those targets, then the row's state changes to the new generation.
- Lineage rows if that old table happens to exist; the Forever pool keeps no lineages.

Login accounts are **not** deleted by a reset; the refill creates new characters on the accounts the registry already holds (or new pool accounts when it needs more). Player accounts and characters are never touched.

### Safe workflow

1. Back up the character database.
2. Set `TortoiseBots.Pool.Reset = "off"` and restart once.
3. Check the pool with [`.tb pool status`](admin-commands.md#the-bot-pool), and prune failed rows if any.
4. Stop the world cleanly.
5. Set `TortoiseBots.Pool.Reset = "once:<token>"` (and keep `Pool.Enable = 1`, `CreatePerMinute > 0`).
6. Start the world and watch the server log and `.tb pool status`. The status line ends with `reset=<mode> phase=<phase> targets=<n> applied=<generation>`.
7. Leave the token in place; a later start with the same token does nothing.
