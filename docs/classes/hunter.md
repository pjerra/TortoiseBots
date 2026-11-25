---
id: classes-hunter
title: Hunter Bot Guide
category: classes
summary: Complete guide to Hunter bot AI, pet management, ranged rotations, melee Survival, and Aspect of the Viper logic.
tags: [classes, hunter, dps, ranged, pet, melee]
relates_to:
  - classes-overview
---

# Hunter Bot Guide

Hunters deliver consistent ranged physical damage, pet management, crowd control via traps, and melee-weaving Survival combat.

## Specs & roles

| Spec | Role | Primary Focus |
| :--- | :--- | :--- |
| **Beast Mastery** | Ranged DPS | Pet empowerment, high physical damage, and enrage burst. |
| **Marksmanship** | Ranged DPS | Heavy ranged shot rotations, party attack power, and *Trueshot Aura*. |
| **Survival** | Ranged DPS | Front-line melee strikes and trap talents (keeps the full ranged shot kit and weaves melee). |

---

## What the bot does in combat

### Beast Mastery & Marksmanship (Ranged DPS)
- Opens with *Hunter's Mark* and *Auto Shot*. With a living tank in the group the mark goes only on the mob the tank is fighting.
- *Auto Shot* needs a bow, gun or crossbow in the ranged slot and matching arrows or bullets in the bags. Bots keep a level-fitting ammo stack topped up on login, on level-up and while playing, and a hunter below level 5 whose ranged slot is empty gets the starter bow back. A hunter with no ranged weapon logs one line on login and fights in melee until it gets one.
- Fires *Aimed Shot* (Marksmanship, once trained, within range) or *Arcane Shot* on cooldown.
- Uses *Steady Shot* as a filler when it is trained.
- Uses *Multi-Shot* only when the splash around the target catches a second fighting enemy, and never into a crowd-controlled mob standing beside it.
- Channels *Volley* against grouped mobs, cancelling the channel if fewer than 2 mobs remain in the area or a mob under the party's crowd control stands in it.
- Multi-Shot and Volley respect [party threat back-off](overview.md#shared-bot-behaviors) when holding 50% or more of the tank's threat.
- Keeps *Serpent Sting* ticking on high-health targets (skipping targets near death).
- Replaces Serpent Sting with *Viper Sting* when the hunter's mana is below 50% and the target has mana (10% or more); a `.bot choice <bot> sting ...` order overrides this gate.
- [`.bot choice <bot> sting <serpent|scorpid|viper|auto>`](../play/commands/bot-panel.md#bot-choice) orders one sting per hunter: it goes on any target without a sting, and the automatic Serpent and Viper rows stand down while it is set.
- Beast Mastery activates *Bestial Wrath* on cooldown. *Rapid Fire* fires for all specs on cooldown.
- Switches between *Aspect of the Hawk* for damage and *Aspect of the Viper* when low on mana. [`.bot choice`](../play/commands/bot-panel.md#bot-choice) orders one aspect per hunter instead; the Hawk upkeep and the low-mana Viper switch both stand down.
- Threat handling (Feign Death, Disengage, multi-mob kiting): see [Threat drops](#threat-drops).

### Survival (Melee / Ranged Hybrid)
- Fights at range with the full shot kit and weaves melee strikes when enemies close in.
- Uses *Raptor Strike* and *Mongoose Bite* on cooldown without requiring a dodge proc.

### Dead-Zone & Melee Weaving
- If enemies close inside Auto Shot minimum range, ranged hunters trade into melee using *Wing Clip*, *Raptor Strike*, and *Mongoose Bite*, then step back out.
- When stepping out to shooting range, the hunter picks a spot that steers clear of unpulled hostile packs and enemies approaching from the sides or behind, fanning out away from threats.
- In a party, a hunter walking into shooting range holds still while its path would pass within aggro distance of an idle hostile pack, and walks on once the tank has pulled it.
- The bot switches back to ranged attacks once the mob is pulled off the hunter by the tank or pet, or is rooted — not on distance alone.

### Threat drops
- When the mob the hunter is fighting is on the hunter itself (or about to be) and stands in melee reach, and a living tank is in the group, the hunter casts *Disengage* to drop off the mob's threat list so the tank can take it back. *Disengage* does not move the hunter and is not used as an escape.
- Drops threat more heavily with *Feign Death* upon reaching medium threat. The bot holds back Feign Death if the attacking mob has only the hunter on its threat list to avoid causing scripted encounters to evade.
- When two or more mobs are on the hunter itself, it stops trading: *Wing Clip* on the nearest attacker and a step back out to shooting range, then shoots the mobs chasing it (repeating until the tank takes them back or they die).

<details>
<summary>Pet management and lifecycle details</summary>

- **Lifecycle:** Hunters obtain their pet at level 10; pool hunters logging in below level 10 give up any previously seeded pet, while player-owned or adopted hunters keep theirs.
- Automatically summons, feeds, and revives pets out of combat.
- Sends pets to attack the primary target, holding them back for about two seconds after the group enters combat so a grouped tank can build a lead first (solo hunters and tankless groups send the pet in at once).
- Casts *Mend Pet* below 40% in combat (channeled, below rotation shots in priority) and below 70% out of combat.
- Controls pets in Defensive stance. Solo hunters keep *Growl* on autocast; when grouped with a tank, *Growl* is automatically disabled and the pet uses *Cower* on threat spikes — as soon as the pet's threat on the target passes the tank's, before the mob turns on the pet.
- When an enemy is crowd-controlled, pets are strictly prevented from attacking the controlled target (except a skull-marked focus target).

</details>

---

## Custom abilities

- **Aspect of the Beast:** manual only (`.bot choice aspect beast`); on ForeverCore the class-line autotrain teaches it (13161).
- **Aspect of the Viper:** a hunter aspect that trades damage for mana regeneration; the bot switches to it when mana is low and back to *Aspect of the Hawk* once mana recovers. On ForeverCore the client's Viper (415423) is on the Beast Mastery line but no trainer row, preset or class-line pass grants it, so the switch stays idle there.
- **Aspect of the Wolf:** Not on ForeverCore yet.
- **Lacerate:** the client's Lacerate on ForeverCore is a pet ability (Survival-line ranks); the hunter's own `lacerate` action has no trigger, so the bot never casts it itself.

---

## Utility & interrupts

- **Enrage Dispel:** Dispels enrage with *Tranquilizing Shot* via the dispel-enrage trigger.
- **Crowd Control:**
  - Deploys *Freezing Trap* on assigned crowd control marks; without a mark it traps a loose extra attacker on its own — a mob nobody in the group is fighting yet, not dotted and not already held by another crowd control — and walks into reach to lay the trap.
  - Casts *Scare Beast* on hostile beasts.
- **Combat Traps:**
  - Lays *Immolation Trap*, *Frost Trap*, or *Explosive Trap* via *Feign Death*, avoiding melee suicide walks.
- **Snares:** Uses *Concussive Shot* and *Wing Clip* to slow fleeing or pursuing enemies.

---

## Talent builds

Bots spend one talent point per level from 10 to 60 along a preset build. The server checks each point against the class trait tree before it is spent.

The presets follow the level-60 picks of Forever hunters on aotc.gg (read 2026-11-24): a talent that about 60% of that spec's level-60 players take is spent at their usual rank, while talents players take while levelling and drop by 60 are left out, because a bot cannot respec. Points go in the preset's own order, so a levelling bot holds the final build's talents as its level allows.

| Spec Name | Role | 60 Points (Beast Mastery / Marksmanship / Survival) | Key Talents |
| :--- | :--- | :--- | :--- |
| **Beast Mastery** | Ranged DPS | `31 / 20 / 0` | Bestial Wrath, Frenzy, Intimidation, Unleashed Fury, Careful Aim. |
| **Marksmanship** | Ranged DPS | `5 / 41 / 5` | Trueshot Aura, Mortal Shots, Barrage, Sniper Shot, Rapid Recuperation. |
| **Survival** | Ranged DPS | `5 / 15 / 31` | Predator's Edge, Improved Tracking, Lethal Attacks, Expose Prey, Survivalist's Discipline. |

---

## Class settings

- [`AiPlayerbot.HunterCarveInReachOnly`](../server/settings/class-behaviour.md#aiplayerbothuntercarveinreachonly): When the hunter has Carve, only uses it on a target already in melee reach.
- [`AiPlayerbot.HunterViperOnLowMana`](../server/settings/class-behaviour.md#aiplayerbothunterviperonlowmana): Controls whether hunters swap to Aspect of the Viper when low on mana.
