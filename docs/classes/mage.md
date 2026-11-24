---
id: classes-mage
title: Mage Bot Guide
category: classes
summary: Complete guide to Mage bot AI, elemental rotations, Polymorph CC, Counterspell interrupts, conjured refreshments, and Arcane Power safety.
tags: [classes, mage, dps, ranged, cc]
relates_to:
  - classes-overview
---

# Mage Bot Guide

Mages provide high-burst ranged spell DPS, dependable crowd control (*Polymorph*), school-locking interrupts (*Counterspell*), and party refreshments.

## Specs & roles

| Spec | Role | Primary Focus |
| :--- | :--- | :--- |
| **Frost** | Ranged DPS | Crowd control, survivability, and shattered critical strikes. |
| **Fire** | Ranged DPS | Massive spell burst, periodic Ignite burns, and instant Pyroblasts. |
| **Arcane** | Ranged DPS | High single-target burst, mana efficiency, and instant Presence of Mind casts. |

---

## What the bot does in combat

### Frost Mage
- Defaults to *Frostbolt* in combat; pulls open with Shoot (wand).
- If enemies close within 8 yards and attack the mage, turns away and casts *Blink* to reset distance. With `.bot behavior <bot> noblink on` it never casts *Blink* inside a dungeon or raid and keeps fighting instead.
- Roots attackers with *Frost Nova* — only when a mob within reach is attacking the mage itself, skipping targets already frozen or immune.
- Casts *Arcane Explosion* when 3 or more enemies cluster inside 10 yards and none of them is loose on the party.
- Casts *Flamestrike* on 3 or more grouped enemies, layering channeled *Blizzard* over the burning zone (6-second window); a running *Blizzard* is cancelled when a mob under the party's crowd control stands in it.
- A Frost mage withholds *Flamestrike* for the first 10 seconds of a fight, the same opener gate *Blizzard* has, so it does not open a fresh pack before the tank holds it.
- Weaves *Cone of Cold* against 2 or more enemies at ranged scan, cast only inside 10 yards while facing the target.
- Activates *Cold Snap* when any Frost cooldown (*Frost Nova*, *Cone of Cold*, *Ice Barrier*, *Ice Block*, *Frost Ward*) is spent.
- Area spells (*Blizzard*, *Flamestrike*, *Cone of Cold*) pause during [party threat back-off](overview.md#shared-bot-behaviors) if holding 50% or more tank threat.

### Fire Mage
- Casts *Fireball* as primary filler and weaves *Scorch* to maintain Fire Vulnerability debuffs (refreshing before the 7-second expiry).
- Consumes *Presence of Mind* with instant *Pyroblast*.
- Uses *Fire Blast* as an instant finishing blow below 20% target health.
- Casts *Arcane Explosion* when 3 or more enemies cluster inside 10 yards and none of them is loose on the party.
- Casts *Blast Wave* as point-blank area damage at interrupt priority on any enemy within 10 yards (no school-lock interrupt or daze logic).
- Automatically falls back to *Frostbolt* when facing fire-immune enemies (via the generic school-immunity veto).

### Arcane Mage
- Channels *Arcane Missiles* while the empowerment buff holds.
- Uses *Presence of Mind* for instant burst spells.
- Casts *Arcane Explosion* at point-blank range when enemies close in (no density or crowd-control gate, unlike Frost and Fire).

<details>
<summary>Wand and mana recovery details</summary>

- When the primary filler is out of mana reach, the bot fires cheaper *Frostbolt* before falling back to the wand.
- While out of mana (below 5%), the bot relies on *Shoot* to keep damage going.
- Wand swings stand down as soon as filler spells are affordable again, or when a *Polymorph* target is queued for crowd control.

</details>

---

## Custom abilities

- **Arcane Power:** The bot enforces a 70% mana floor and requires a live hostile target before activating, and never cancels the buff once active.
- **Evocation:** Arms below 40% mana, channels *Evocation* when out of spell reach, and cancels the channel at 95% mana to resume combat.
- **Arcane Rupture:** Not on WoW Forever yet.
- **Icicles:** Not on WoW Forever yet.

---

## Utility & interrupts

- **Polymorph CC:** Casts *Polymorph* on targets assigned via `.tb cc <bot> <mark> <unit>` (GameMaster); the addon's party CC pick is not on WoW Forever yet. Stands still during casting and prevents AoE spells near sheeped mobs.
- **Interrupts:** Casts *Counterspell* immediately upon detecting dangerous enemy spell casts, locking the school.
- **Food & Drink Conjuration:** Conjures food and water out of combat for itself when it has none. You can also order conjuration with `.bot service <Name> conjure <food|water>` and trade it through the addon.
- **Portals:** `.bot service <Name>` lists the portals the mage knows, and `.bot service <Name> portal <spellId>` casts one (the addon's service action does the same). Out of combat only, rune is free. Only the mage's group can use the portal, so invite the bot first.
- **Portal Follow:** Not on WoW Forever yet.
- **Mana Gems & Runes:** Consumes the strongest mana gem held (Ruby down to Agate) below 40% mana in combat, re-conjuring gems between pulls. Uses *Dark Rune* below 40% mana when no ready gem is held (requires level 50).
- **Party Buffs:** Maintains *Arcane Intellect* (and *Arcane Brilliance*), refreshing expiring buffs (under 15 seconds) and coordinating buff claims with other mages.
- **Defensive Wards:** Casts *Mana Shield* at low health (also requires mana above medium) and *Ice Block* at critical health.
- **Decurse:** Casts *Remove Lesser Curse* on party members afflicted by debilitating curses.

---

## Talent builds

Bots spend one talent point per level from 10 to 60 along a preset build. The server checks each point against the class trait tree before it is spent.

| Spec Name | Role | 60 Points (Arcane / Fire / Frost) | Key Talents |
| :--- | :--- | :--- | :--- |
| **Arcane** | Ranged DPS | `40 / 0 / 11` | Arcane Power, Presence of Mind, Arcane Blast, Missile Barrage, Arcane Instability. |
| **Fire** | Ranged DPS | `13 / 38 / 0` | Combustion, Pyroblast, Blast Wave, Ignite, Fire Power. |
| **Frost** | Ranged DPS | `13 / 0 / 38` | Ice Barrier, Ice Block, Shatter, Cold Snap, Winter's Chill. |

<details>
<summary>Leveling order</summary>

- **Arcane:** Wand Specialization, Arcane Resilience, Arcane Subtlety, Arcane Concentration, Missile Barrage, Arcane Impact, Arcane Blast, Arcane Meditation, Arcane Wrath, Presence of Mind, Arcane Mind, Arcane Instability, Arcane Power, Improved Channeling, Magic Absorption and Arcane Geometry; the last 11 points go to Frost (Precision, Improved Frostbolt, Ice Shards, Frostbite).
- **Fire:** Incineration, Ignite, Flame Throwing, Pyroblast, Improved Fire Blast, Burning Soul, Improved Scorch, Master of Elements, Heating Up, Critical Mass, Blast Wave, Fire Power, Combustion, Accelerant and Impact; the last 13 points go to Arcane (Wand Specialization, Arcane Resilience, Arcane Concentration, Arcane Subtlety, Magic Absorption).
- **Frost:** Improved Frostbolt, Ice Shards, Improved Frost Nova, Frost Channeling, Ice Lance, Arctic Reach, Ice Block, Shatter, Improved Cone of Cold, Cold Snap, Fingers of Frost, Winter's Chill, Ice Barrier, Precision and Frostbite; the last 13 points go to Arcane (Wand Specialization, Arcane Resilience, Arcane Concentration, Arcane Subtlety, Magic Absorption).

</details>

---

## Class settings

There are no class-specific configuration flags for Mages. General combat and mana thresholds are configured in [Combat settings](../server/settings/combat.md).
