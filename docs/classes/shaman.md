---
id: classes-shaman
title: Shaman Bot Guide
category: classes
summary: Complete guide to Shaman bot AI, totem sets, weapon imbues, chain heals, shocks, and class abilities.
tags: [classes, shaman, healer, dps, totems, melee]
relates_to:
  - classes-overview
---

# Shaman Bot Guide

Shamans provide unmatched group utility through versatile totem sets, elemental shocks, weapon imbues, and potent chain spells across Melee DPS, Caster DPS, and Healing roles.

## Specs & roles

| Spec | Role | Primary Focus |
| :--- | :--- | :--- |
| **Restoration** | Healer | Chain Heal throughput, single-target wave downranking, and totem support. |
| **Enhancement** | Melee DPS | Front-line melee damage, Windfury burst, Stormstrike, and totem utility. |
| **Elemental** | Ranged DPS | High-damage Lightning Bolt and Chain Lightning spells with instant crits. |

The shaman tank spec is Not on WoW Forever yet.

---

## What the bot does in combat

### Restoration (Healer)
- Casts *Chain Heal* when several nearby allies are injured.
- Deals off-spec damage (*Flame Shock*, *Lightning Bolt*, *Chain Lightning* into packs) per [healer off-DPS rules](overview.md#shared-bot-behaviors).

### Enhancement (Melee DPS)
- Fights in melee, keeping weapon imbues active.
- Casts *Stormstrike* on cooldown to amplify nature burst.
- Cycles *Flame Shock* for periodic damage and *Earth Shock* for burst damage.
- Off-heals allies at 20–50% health with *Healing Wave* per [off-healing rules](overview.md#shared-bot-behaviors).

### Elemental (Ranged DPS)
- Attacks from range with *Lightning Bolt* and casts *Chain Lightning* against enemy packs.
- Keeps *Water Shield* active for mana regeneration (falling back to *Lightning Shield* before training it).
- Saves *Earth Shock* as a finishing execute below 25% health and below 1500 absolute HP while reserving interrupts for casting targets.
- Respects [party threat back-off](overview.md#shared-bot-behaviors) on *Chain Lightning* when holding 50% or more of tank threat.
- Off-heals allies at 20–50% health with *Healing Wave* per [off-healing rules](overview.md#shared-bot-behaviors).

<details>
<summary>Totem orchestration details</summary>

Shamans maintain 4-element totem sets tailored to group composition:
- **Earth Totem:** Drops *Strength of Earth Totem* if melee allies are present and the spell is known; otherwise the slot stays empty.
- Reacts to fears with *Tremor Totem* and to runners with *Earthbind Totem*.
- **Fire Totem:** Drops *Searing Totem* on single targets and *Magma Totem* on packs.
- **Water Totem:** Drops *Mana Spring Totem* if mana users are present; *Healing Stream Totem* otherwise. Reacts to poison and disease with cleansing totems.
- **Air Totem:** Drops *Windfury Totem* for melee groups; drops *Grace of Air Totem* for casters. Reacts to incoming enemy spells with *Grounding Totem*.
- **Out of Combat:** Totems drop in combat and in the pre-pull window (ready check / rebuff) only — never otherwise out of combat, so a following bot leaves no totems behind in town.
- After the fight *Totemic Recall* refunds the standing set, and nothing re-drops it out of combat, so the refund sticks until the next fight.
- `.bot totem` `mode manual` quiets the automatic rows.
- The reactive rows (*Tremor*, cleansing, *Grounding*, *Earthbind*) still fire on need.
- A slot with a `.bot totem` order takes only the ordered totem, so the reactive rows skip it. *Tremor Totem* alone still takes an ordered earth slot when the group is feared or charmed, and hands it back when it expires.

</details>

---

## Custom abilities

- **Water Shield:** a shaman shield that restores mana; the elemental kit keeps it up.
- **Totemic Recall:** refunds the standing totem set after a fight.
- **Earthquake:** Not on WoW Forever yet.
- **Lightning Strike:** Not on WoW Forever yet.
- **Spirit Link:** Not on WoW Forever yet.
- **Ancestral Swiftness:** Not on WoW Forever yet.
- **Bloodlust:** Not on WoW Forever yet.
- **Earthshaker Slam:** Not on WoW Forever yet.

---

## Utility & interrupts

- **Interrupts:** Casts *Earth Shock* to interrupt dangerous enemy spellcasters and healing mobs.
- **Weapon Imbues:** Enhancement prioritizes *Windfury Weapon*, others *Flametongue Weapon*; first-known fallback is *Frostbrand Weapon*, then *Rockbiter Weapon*.
- **Dispels:** Strips enemy buffs with *Purge* and cleanses allies using *Cure Poison* and *Cure Disease*.
- **Resurrection:** Revives dead allies out of combat with *Ancestral Spirit*.
- **Ghost Wolf:** Shifts to *Ghost Wolf* when carrying a flag in PvP.
- **Player Totem Controls:** Players can customize totem sets via [`.bot totem`](../play/commands/bot-panel.md#bot-totem) or manage them with `drop` and `status`.

---

## Talent builds

Bots spend one talent point per level from 10 to 60 along a preset build. The server checks each point against the class trait tree before it is spent.

| Spec Name | Role | 60 Points (Elemental / Enhancement / Restoration) | Key Talents |
| :--- | :--- | :--- | :--- |
| **Restoration** | Healer | `0 / 10 / 41` | Nature's Swiftness, Riptide, Mana Tide Totem, Healing Way, Water Shield. |
| **Enhancement** | Melee DPS | `11 / 40 / 0` | Stormstrike, Flurry, Maelstrom Weapon, Elemental Devastation, Rage of the Farseer. |
| **Elemental** | Ranged DPS | `41 / 0 / 10` | Elemental Fury, Lightning Mastery, Lightning Overload, Call of Thunder, Elemental Focus. |

<details>
<summary>Leveling order</summary>

- **Restoration:** Improved Healing Wave, Tidal Focus, Mindfulness, Ancestral Healing, Healing Focus, Water Shield, Tidal Mastery, Restorative Totems, Mana Tide Totem, Healing Way, Nature's Swiftness, Purification and Riptide; the last 10 points go to Enhancement (Ancestral Knowledge, Guardian Totems, Mental Dexterity).
- **Enhancement:** Thundering Strikes, Ancestral Knowledge, Mental Dexterity, Improved Lightning Shield, Elemental Weapons, Shamanistic Focus, Anticipation, Flurry, Stormstrike, Spirit Weapons, Mental Quickness, Improved Stormstrike, Maelstrom Weapon and Rage of the Farseer; the last 11 points go to Elemental (Concussion, Elemental Devastation, Call of Flame).
- **Elemental:** Convection, Concussion, Elemental Alacrity, Call of Flame, Reverberation, Elemental Focus, Eye of the Storm, Call of Thunder, Lightning Overload, Elemental Reach, Elemental Fury, Lightning Mastery, Trial by Fire and Elemental Warding; the last 10 points go to Restoration (Improved Healing Wave, Tidal Focus).

</details>

---

## Class settings

The shaman tank spec is Not on WoW Forever yet; there is no tank talent build on this server. A shaman can still be put in the tank role, where it uses its melee kit.
