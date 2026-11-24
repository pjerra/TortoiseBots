---
id: classes-rogue
title: Rogue Bot Guide
category: classes
summary: Complete guide to Rogue bot AI, stealth openers, combo point spenders, poison application, interrupts, and dungeon threat drops.
tags: [classes, rogue, dps, melee, stealth]
relates_to:
  - classes-overview
---

# Rogue Bot Guide

Rogues deliver exceptional single-target physical melee DPS, pre-combat crowd control (*Sap*), rapid interrupts (*Kick*), and defensive threat dumps (*Feint*, *Vanish*).

## Specs & roles

| Spec | Role | Primary Focus |
| :--- | :--- | :--- |
| **Combat** | Melee DPS | Sustained sword/mace DPS, Slice and Dice uptime, and Blade Flurry cleave. |
| **Assassination** | Melee DPS | Dagger strikes, poison synergy, Cold Blood burst, and high critical damage. |
| **Subtlety** | Melee DPS | High mobility, bleed scaling, Preparation resets, and opening burst. |

---

## What the bot does in combat

### Stealth & Openers
- Automatically enters *Stealth* out of combat when a nearby target is present and Stealth is ready.
- Manoeuvres behind targets to open with *Ambush* (with main-hand daggers) or *Garrote* (bleed, no weapon requirement).
- Uses *Cheap Shot* as a fallback opener or when lockdown stuns are required.
- **Pre-pull Sap:** When assigned a crowd control mark on an unpulled humanoid, a rogue with a live-client master stealths forward and casts *Sap* before combat begins. With the setting on (off by default), the approach keeps clear of the other mobs in the pack and is skipped when no safe route exists ([`AiPlayerbot.SapApproachAvoidPack`](../server/settings/class-behaviour.md#aiplayerbotsapapproachavoidpack)).
- A stealthed rogue still ends a pull's join window on time.

### Combo Points & Finisher Priority
- **Builders:** Uses *Sinister Strike* (Combat), *Backstab* (Assassination), or *Hemorrhage* (Subtlety).
- **Slice and Dice:** Combat prioritizes *Slice and Dice* above *Eviscerate*; Assassination and Subtlety prioritize *Eviscerate*.
- **Spenders:** At 4 or more combo points, casts *Eviscerate*; *Rupture* fires at 4 combo points for Combat and at 3 or more for Assassination and Subtlety. Dumps banked combo points (1+) into *Eviscerate* if the target falls below 25% health.
- *Expose Armor* is withheld if the target already carries *Sunder Armor*.

### Cooldowns & Defensive Play
- **Combat:** Activates *Adrenaline Rush* and *Blade Flurry* whenever ready in combat. *Blade Flurry* is automatically cancelled if a crowd-controlled mob comes within 8 yards.
- **Assassination:** Holds *Cold Blood* until 4 or more combo points are banked to guarantee a critical finisher strike.
- **Subtlety:** Activates *Preparation* when a resettable cooldown (Evasion, Sprint, Vanish line) is spent.
- **Defensives:** Triggers *Evasion* at low health, *Blind* to peel attackers, and *Vanish* at critical health (below 20%) to drop threat. *Blind* fires ahead of *Vanish*. *Feint* is cast only when a living party tank or pet is present to hold aggro.

<details>
<summary>Dungeon threat drops in detail</summary>

When enabled via configuration, rogues in 5-man dungeons use the same threat-drop discipline as in raids (5-man means a dungeon map that is not a raid):
- Casts *Feint* upon reaching medium threat (relative threat at or above 60%).
- Casts *Vanish* upon reaching high threat (relative threat climbing past 80%), provided the tank is holding the target.
- Requires an active, living tank to prevent accidental resets in solo play.

</details>

---

## Custom abilities

- **Death Mark:** learned by the Subtlety preset; the bot does not cast it yet.
- **Surprise Attack, Noxious Assault, Envenom, Shadow of Death and Mark for Death:** Not on WoW Forever yet.

---

## Utility & interrupts

- **Poisons:** Automatically applies poisons based on context: raid and generic combat use Instant Poison (main hand) and Deadly Poison (off hand); general PvE uses Instant Poison on both hands; PvP uses Crippling Poison (off hand). Gated by poison item required level and stock. [`.bot choice <bot> poison_mh|poison_oh <instant|deadly|crippling|wound|auto>`](../play/commands/bot-panel.md#bot-choice) orders the poison per hand; every automatic poison row for that hand then stands down, so the two never overwrite each other. Without the poison item (and without the item cheat) the rogue keeps what the weapon carries. *Mind Poison* is Not on WoW Forever yet.
- **Interrupts:** Casts *Kick* immediately to lock out enemy spell casts. Uses *Kidney Shot* as a stun fallback. With `AiPlayerbot.InterruptIgnoresThreat = 1` (recommended) *Kick* also goes out while the rogue is over the threat back-off line, which otherwise holds it, as long as the Kick would not pull the caster off the tank (under 110 % of the tank's threat on it).
- **Self-Defense CC:** Uses *Gouge* at medium health (50–70%) and *Blind* at critical health (below 20%) to disorient attackers. *Gouge* only works while the target faces the rogue; with `AiPlayerbot.SpellFacingPrecheck = 1` (recommended) a rogue behind its target skips it instead of trying it over and over.

---

## Talent builds

Bots spend one talent point per level from 10 to 60 along a preset build. The server checks each point against the class trait tree before it is spent.

| Spec Name | Role | 60 Points (Assassination / Combat / Subtlety) | Key Talents |
| :--- | :--- | :--- | :--- |
| **Combat** | Melee DPS | `15 / 36 / 0` | Adrenaline Rush, Blade Flurry, Hack and Slash, Flash of Steel, Riposte. |
| **Assassination** | Melee DPS | `41 / 10 / 0` | Cold Blood, Seal Fate, Mutilate, Vile Poisons, Improved Kidney Shot. |
| **Subtlety** | Melee DPS | `15 / 0 / 36` | Hemorrhage, Preparation, Death Mark, Boot Knife, Premeditation. |

<details>
<summary>Leveling order</summary>

- **Combat:** Lightning Reflexes, Precision, Deflection, Riposte, Endurance, Improved Sprint, Weapon Expertise, Aggression, Flawless Execution, Dual Wield Specialization, Blade Flurry, Hack and Slash, Flash of Steel, Adrenaline Rush and Coup de Grace; the last 15 points go to Assassination (Malice, Ruthlessness, Improved Slice and Dice, Lethality, Vigor).
- **Assassination:** Malice, Ruthlessness, Improved Slice and Dice, Lethality, Vigor, Vile Poisons, Cold Blood, Improved Poisons, Venom, Relentless Strikes, Improved Kidney Shot, Seal Fate, Mutilate, Remorseless Attacks and Improved Gouge; the last 10 points go to Combat (Efficient Strikes, Coup de Grace, Precision, Deflection).
- **Subtlety:** Opportunity, Improved Ambush, Dirty Deeds, Premeditation, Serrated Blades, Initiative, Elusiveness, Ghostly Strike, Setup, Preparation, Hemorrhage, Heightened Senses, Camouflage, Boot Knife, Death Mark, Master of Deception and Dirty Tricks; the last 15 points go to Assassination (Malice, Ruthlessness, Improved Slice and Dice, Lethality, Vigor).

</details>

---

## Class settings

- [`AiPlayerbot.RogueDungeonThreatDrops`](../server/settings/class-behaviour.md#aiplayerbotroguedungeonthreatdrops): Enables Feint on medium threat and Vanish on high threat in 5-man dungeons.
- [`AiPlayerbot.SapApproachAvoidPack`](../server/settings/class-behaviour.md#aiplayerbotsapapproachavoidpack): The pre-pull Sap approach goes round the pack, or the Sap is skipped when no safe route exists. Off by default.
