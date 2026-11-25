---
id: play-resist-gear
title: Resist Gear for Raid Bosses
category: play
summary: What the resistance-gear swap does for a bot in a raid group, the three TortoiseBots.RaidResistGear settings, the read-out keys, and the characters-database store that survives a crash.
tags: [raid, resistance, gear, swap, settings, bank]
relates_to:
  - play-dungeons-and-raids
  - play-features
  - server-configuration
---

# Resist Gear for Raid Bosses

Many raid bosses lean on one spell school (Ragnaros and most of Molten Core on fire, the Shadow bosses on shadow, Sapphiron on frost). A tank or a damage dealer in the wrong resistance set takes far more damage than it needs to, so the module can put the resistance pieces a bot already carries into the slots they fit, then put the bot's own pieces back afterwards.

The swap is **in place**: `Player::SwapItem`, the piece that leaves a slot lands in the bag row the incoming piece came from. Nothing is bought, created, sold or destroyed, and the bot's kit is the same kit afterwards.

## Which pieces go on

A replacement must come from the bot's own bags (never the bank) and carry more of the boss's school than the worn piece:

| Slot group | Slots |
| :--- | :--- |
| Everyone | neck, back, waist, wrists, both fingers |
| Non-tanks only | head, shoulders, chest, legs, feet, hands |
| Never | shirt, trinkets, ranged/relic, weapons, tabard |

- In each slot the bag piece with the most resistance of that school wins, as long as it beats the worn piece by at least 5 and the server would let this bot wear it there.
- A worn piece the bot's keep/equip order pins stays where it is.
- One ring never fills both fingers.
- The boss table (`forever/data/raid/boss-resist.csv`) names the raid bosses whose school is known; the module checks every row against the spell's own school when it builds its index.

## The hold, and getting the set off again

The module remembers every piece it put on. While a set is held:

- The automatic upgrade pass leaves the resist piece alone instead of "improving" it back into the bags.
- A wipe does not take the set off.
- The set comes off when a release is asked for: a boss's death or the bot leaving the range (the automatic arming step), the bot logging out, or the setting being turned off. In this build only the logout and the switch can ask.
- A release that cannot finish keeps the pieces on and tries again: a dead bot, a bot in combat, a full bag, a piece the server refuses. The first passes are 5 seconds apart, then one a minute, for as long as the bot lives. Nothing is ever dropped while a piece is still named.
- The bot's own piece must be in the bags to be swapped back. If it is in the bank, the pass reports it and keeps the hold; put the piece back in the bags and a later pass restores the set.

## Surviving a crash or a restart

Each held piece is one row of the characters table `tortoise_bots_resist_hold`, written **before** any piece moves — if that record cannot be written, no swap happens, because a resist piece on a bot with nothing recording it is a piece nothing puts back. The row also carries the hold's state: whether the set is simply on, or a release was asked for and did not finish. A logout puts back what it can; the rest is restored at the next login, and a crash or a restart is restored the same way.

The table is applied from `forever/sql/characters/2026_11_24_11_tb_resist_hold.sql` and `2026_11_25_00_tb_resist_hold_state.sql`; the module also creates it itself when the database user may create tables. Without it the hold is kept in memory only and one log line says so.

## Settings

All three live in `worldserver.conf.d/tortoisebots.conf` (`TortoiseBots.*`), are read at load (`.reload config` re-reads them), and are off/inert until a server admin changes them.

| Key | Default | Meaning |
| :--- | :--- | :--- |
| `TortoiseBots.RaidResistGear.Enable` | `0` | The swap itself. At `0` nothing swaps; turning it off while a set is held releases the set. |
| `TortoiseBots.RaidResistGear.Range` | `60` | Arming distance in yards (5-200) for the automatic boss-range step. |
| `TortoiseBots.RaidResistGear.ReleaseSeconds` | `15` | Release delay in seconds (0-600) after the last listed boss left the range. |

## Reading it back

`.tb state <bot>` shows the bot's six worn resistances as `resist=<holy>,<fire>,<nature>,<frost>,<shadow>,<arcane>`, and, when the module is holding a set on that bot, `rgear=<school>:<pieces>`. `.tb gear <bot|party> show` prints the same resist tuple per equipped slot as `res=`. The line format is in [the command reference](../../forever/docs/TB-COMMANDS.md) (section 7 for the keys, the `.tb state` keys for the read-out).

## What this build does not do yet

The rule, the swap, the store and the read-outs are here; **nothing arms the swap before a pull automatically**, so a bot does not change its gear by itself on this build. The three settings and the `resist=`/`rgear=` read-outs are in place for the arming step that follows. With the shipped default (`Enable = 0`) nothing changes at all.
