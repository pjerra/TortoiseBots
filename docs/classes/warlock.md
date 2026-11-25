---
id: classes-warlock
title: Warlock Bot Guide
category: classes
summary: Complete guide to Warlock bot AI, DoT spreading, demon summoning, Soul Shard economy, Life Tap, and class abilities.
tags: [classes, warlock, dps, ranged, pet]
relates_to:
  - classes-overview
---

# Warlock Bot Guide

Warlocks provide sustained Shadow and Fire DPS through curses, damage-over-time (DoT) spells, demon summons, Healthstones, and crowd control (*Fear*, *Banish*).

## Specs & roles

| Spec | Role | Primary Focus |
| :--- | :--- | :--- |
| **Affliction** | Ranged DPS | Multi-target DoT spreading and Drain Life sustain. |
| **Demonology** | Ranged DPS / Tanky | Heavy demon empowerment, Soul Link damage sharing, Demonic Sacrifice, and high durability. |
| **Destruction** | Ranged DPS | Fire burst damage, Incinerate nukes, and instant Conflagrate explosions. |

---

## What the bot does in combat

### Affliction
- Applies and maintains *Curse of Agony*, *Siphon Life*, and *Immolate* on primary targets. *Corruption* upkeep runs through the shared warlock rotation.
- Spreads *Corruption*, *Siphon Life*, and *Immolate* across secondary attackers under the AoE strategy.
- Channels *Wrack* between DoT refreshes; while it channels, *Corruption*, *Siphon Life* and *Bane of Agony* tick faster.
- Uses *Life Tap* (own health) to replenish mana.
- Channels *Drain Life* for self-sustain and *Drain Soul* on dying enemies (at or below 20% health) to harvest shards.

### Demonology
- Summons resilient demons, links damage via *Soul Link*, and utilizes *Demonic Sacrifice* in raids.
- Maintains *Immolate* while casting *Shadow Bolt* fillers. *Corruption* upkeep runs through the shared warlock rotation.
- Channels *Health Funnel* when the active demon falls below 50% health in combat, provided personal health exceeds 60%.
- Burns the *Fel Domination* cooldown to summon a replacement demon during combat if the active pet dies (Demonology kit only).

### Destruction
- Defaults to *Curse of the Elements* once trained, *Curse of Agony* before that (gated by the spellbook, no level check in the bot). The curse outranks *Immolate*, so the curse — not Immolate — lands first.
- Delivers burst damage using *Conflagrate* (only while Immolate ticks with 7 seconds or less left) and *Shadowburn*.
- Casts *Incinerate* as the primary nuke (extra damage while Immolate ticks) and *Shadow Bolt* before Incinerate is learned and against fire-immune enemies. Channels *Rain of Fire* on packs of 3 or more clustered enemies. *Shadowburn* fires through the shared warlock rotation on dying targets.
- Cancels *Rain of Fire* channels if fewer than 2 enemies remain in the target area, or a mob under the party's crowd control stands in it.

<details>
<summary>Soul Shard harvest and Life Tap safety</summary>

- **Shard Harvest:** Warlocks harvest Soul Shards organically with *Drain Soul* when targets drop to at or below 20% health. Bots keep up to 5 shards, destroying excess shards out of combat.
- **Life Tap Safety:** *Life Tap* requires personal health above the low-health line (50% default). The urgent tap fires at or below medium mana (40% default) in combat, while top-up tapping occurs only out of combat between pulls to avoid pausing spell damage. Top-up tapping stops at 85% mana.
- **Wand Stops:** Dedicated triggers stop active wanding when spells become affordable (only when Shadow Bolt is blocked, not for intentional raid or no-mana wands) or crowd control is needed.

</details>

---

## Custom abilities

- **Rain of Fire:** channels pause during [party threat back-off](overview.md#shared-bot-behaviors) if the warlock holds 50% or more tank threat.
- **Dark Pact:** Not on ForeverCore yet.
- **Dark Harvest:** Not on ForeverCore yet.
- **Power Overwhelming:** Not on ForeverCore yet.

---

## Utility & interrupts

- **Demon Summons:**
  - *Voidwalker:* Tanking demon that taunts with *Torment* in solo or tankless play and shields the warlock with *Sacrifice* in emergencies; grouped with a tank it never taunts.
  - *Imp:* Raid pet providing *Blood Pact* stamina buffs and ranged fire support.
  - *Felhunter:* Utility pet casting *Spell Lock* for ranged interrupts and *Devour Magic* for offensive and defensive dispels.
  - *Succubus:* CC demon casting *Seduction* on humanoids (never on dotted marks).
  - **Default Demon:** without an order the warlock summons the strongest demon it has learned — *Felhunter*, then *Succubus*, then *Voidwalker*, then *Imp* — so one below level 10 still brings out the *Imp* it learns at level 1. Every demon but the Imp needs a Soul Shard: without one a petless warlock falls back to the Imp and a live demon is never swapped. A manual `pet <demon>` strategy or a [`.bot choice`](../play/commands/bot-panel.md#bot-choice) order wins, and in PvE the [`AiPlayerbot.WarlockPetBySpec`](../server/settings/class-behaviour.md#aiplayerbotwarlockpetbyspec) pick owns the demon ahead of it.
  - **Demon Order:** [`.bot choice <bot> pet <imp|voidwalker|succubus|felhunter|auto>`](../play/commands/bot-panel.md#bot-choice) picks the demon per warlock. Out of combat the warlock summons it over the current one; the spec pick, the solo Voidwalker upgrade and the chat `pet <demon>` rows stand down. Every demon but the Imp needs a Soul Shard unless the bot has the item cheat; without one it keeps its demon and says so once. The dungeon run's Seduce needs a live Succubus.
- **Pet Entry:** In a party the demon is held about 2 seconds for the tank to build a lead; a mob already on the warlock, an enemy player, a tankless party, and an explicit `pet attack` order send it in at once. Crowd-controlled and immune targets are never attacked.
- **Healthstones & Soulstones:** Creates Healthstones out of combat and uses them in combat. Stores one Soulstone out of combat, prioritizing the party healer, then the tank, then the leader. Each warlock keeps one stone up at a time; a second warlock stones the next target. You can order a Healthstone with `.bot service <Name> conjure healthstone` and trade it through the addon.
- **Weapon Stones & Buffs:** Conjures and carries *Spellstones* (Affliction/Demonology) or *Firestones* (Destruction) in bags. Maintains *Demon Armor* (*Demon Skin* fallback) and *Unending Breath* on self and party while swimming.
- **Curse Overrides:** Defaults to spec-appropriate curses (*Curse of Agony* or *Curse of the Elements*; second warlocks in raids cover *Recklessness* over Shadow). Players can order one curse per warlock with [`.bot choice <bot> curse <shadow|weakness|elements|agony|doom|tongues|exhaustion|recklessness|auto>`](../play/commands/bot-panel.md#bot-choice) (GameMasters can also use `.tb strategy`). Exhaustion snares runners. Curse claims prevent duplicate picks on the main curse rows.
- **Crowd Control:** Casts *Fear* on assigned CC marks (skipping undead, mechanical, or already controlled targets; off in dungeons, raids, and real-player groups, and the `aoe fear` override is not registered on ForeverCore yet) and *Banish* on Demons and Elementals. Area spells avoid targets near CC marks.

---

## Talent builds

Bots spend one talent point per level from 10 to 60 along a preset build. The server checks each point against the class trait tree before it is spent.

The presets follow the level-60 picks of Forever warlocks on aotc.gg (read 2026-11-24): a talent that about 60% of that spec's level-60 players take is spent at their usual rank, while talents players take while levelling and drop by 60 are left out, because a bot cannot respec. Points go in the preset's own order, so a levelling bot holds the final build's talents as its level allows.

| Spec Name | Role | 60 Points (Affliction / Demonology / Destruction) | Key Talents |
| :--- | :--- | :--- | :--- |
| **Affliction** | Ranged DPS | `35 / 11 / 5` | Siphon Life, Shadow Mastery, Nightfall, Wrack, Pandemic. |
| **Demonology** | Ranged DPS / Tanky | `10 / 36 / 5` | Soul Link, Master Demonologist, Demonic Sacrifice, Demonic Knowledge, Demonic Pact. |
| **Destruction** | Ranged DPS | `7 / 11 / 33` | Conflagrate, Ruin, Shadowburn, Incinerate, Shadow and Flame. |

---

## Class settings

- [`AiPlayerbot.WarlockPetBySpec`](../server/settings/class-behaviour.md#aiplayerbotwarlockpetbyspec): Configures warlocks to summon the demon tailored to their active talent spec in PvE content (Affliction/Demonology toward Felhunter, Destruction toward Imp, tankless parties toward Voidwalker).
- [`AiPlayerbot.WarlockRaidRotation`](../server/settings/class-behaviour.md#aiplayerbotwarlockraidrotation): Opt-in raid rotation that keeps DoTs, curses and *Drain Soul* off targets that are about to die (bosses and the raid-utility curses are exempt), caps each raid DoT spread row at two extra targets, and taps for mana only when two *Shadow Bolts* cannot be paid for.
