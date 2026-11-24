---
id: classes-warrior
title: Warrior Bot Guide
category: classes
summary: Complete guide to Warrior bot AI, stance dancing, tanking rotations, melee DPS, shouts, and class abilities.
tags: [classes, warrior, tank, dps, melee]
relates_to:
  - classes-overview
---

# Warrior Bot Guide

Warriors serve as primary dungeon tanks or powerful melee DPS, managing dynamic stance switches, rage expenditure, interrupt timing, and defensive cooldowns.

## Specs & roles

| Spec | Role | Primary Focus |
| :--- | :--- | :--- |
| **Protection** | Tank | Defensive Stance holding, Sunder Armor stacking, Shield Slam, and peel taunts. |
| **Arms** | Melee DPS | Battle Stance two-handed strikes, Mortal Strike, Overpower procs, and cleaves. |
| **Fury** | Melee DPS | Berserker Stance dual-wielding, Bloodthirst, Whirlwind, and Execute bursts. |

---

## What the bot does in combat

### Protection (Tank)
- **Pull & Engagement:** With [`AiPlayerbot.WarriorGapCloser`](../server/settings/class-behaviour.md#aiplayerbotwarriorgapcloser) on (off by default), out of combat, with a target 8-25 yards away, swaps to Battle Stance and opens with *Charge*, then swaps back to Defensive Stance. A mob already hitting a party member (a pack a rogue or healer pulled) is charged first; a crowd-controlled mob never is. In a dungeon an unengaged pack is only charged on a melee pull or attack order. Ranged pulls using bows, guns, or thrown weapons fire when line of sight or distance requires it ([`AiPlayerbot.WarriorGapCloser`](../server/settings/class-behaviour.md#aiplayerbotwarriorgapcloser)).
- **Healer Rescue:** In combat, a mob hitting the healer or another non-tank member 8-25 yards away gets *Intercept* (a Berserker Stance swap and back) when the rage left after the swap pays for it; otherwise the tank runs in and taunts.
- **Threat Generation:**
  - Maintains *Shield Block* on cooldown, which opens up *Revenge*.
  - Stacks *Sunder Armor* to 5 stacks, refreshing before expiry. With [`AiPlayerbot.AnnounceSunderStacks`](../server/settings/class-behaviour.md#aiplayerbotannouncesunderstacks) on (off by default), says "5 Sunders on <target>" once per elite or boss target when a real player is in the group; damage-dealing warriors do the same.
  - Casts *Shield Slam* on cooldown (or when procced by Improved Shield Slam).
  - Uses *Thunder Clap* against 2 or more melee attackers.
  - Opens with *Shield Slam* when it is ready and treats *Sunder Armor* as the filler while the stack builds.
- **Mitigation & Taunts:**
  - Activates *Shield Wall* at low health (20–50%) and holds *Last Stand* for critical health emergencies (below 20%). Both lines are the `CriticalHealth` (20) and `LowHealth` (50) defaults.
  - Taunts with *Taunt* immediately if any attacker peels onto non-tank party members.
  - Intervenes to intercept attacks aimed at focused group members.
  - Falls back to *Mocking Blow* (from Battle Stance, back to Defensive after) when *Taunt* is on cooldown and the mob left the tank.
  - Saves *Challenging Shout* for real emergencies: 3 or more loose mobs on non-tanks, not for a pack it already holds.

### Arms & Fury (Melee DPS)
- **Opening (both specs):** With `AiPlayerbot.WarriorGapCloser` on (off by default), out of combat and with the target 8-25 yards away, opens with *Charge* (Fury swaps to Battle Stance for it and back to Berserker Stance after); a crowd-controlled mob is never charged, and in a dungeon only a mob already in the fight is.
- **Arms:**
  - Fights in Battle Stance, maintaining *Rend*, casting *Mortal Strike* on cooldown and *Overpower* on dodges. It swaps to Berserker Stance for *Pummel* and *Intercept*, then back to Battle Stance, but never dances for anything else — and never casts *Thunder Clap* in a group (a tank-only spell; solo Arms keeps it).
  - Cleaves packs with *Sweeping Strikes* (Battle Stance, 3 or more attackers in melee) and *Whirlwind* (only while already in Berserker Stance). *Sweeping Strikes* and its rage spenders agree on the pack size, so on two targets Arms keeps its single-target cycle instead of holding damage back for a buff that never fires. *Sweeping Strikes* is never started, and is dropped, beside a mob under the party's crowd control.
- **Fury:**
  - Fights in Berserker Stance, centering rage spend on *Bloodthirst* and *Whirlwind*. The build takes Dual-Wield Specialization.
  - Activates *Death Wish* and *Recklessness* in hard fights; uses *Retaliation* at high health against 2 or more melee attackers.
  - Rests in Berserker Stance between fights, so there is no Battle/Berserker swap per pull.
- **Snaring:** *Piercing Howl* only lands on a mob inside its own 10-yard radius (in a raid, only a runner beside the warrior); a refused Howl falls back to *Hamstring* in a dungeon, and both DPS specs skip it in a raid.
- **Execute Phase:** When targets drop below 20% health, *Execute* becomes top priority, consuming all available rage.
- **Finishing Runners:** Warriors in Berserker Stance with *Intercept* off cooldown charge after fleeing enemies within 25 yards per [shared runner rules](overview.md#shared-bot-behaviors).

---

## Custom abilities

- **Master Strike:** Not on WoW Forever yet.
- **Defensive Tactics:** Not on WoW Forever yet.

---

## Utility & interrupts

- **Interrupts:**
  - *Protection:* Relies strictly on *Shield Bash* to remain in Defensive Stance.
  - *Fury:* Uses *Pummel* (in Berserker Stance).
  - *Arms:* Uses *Pummel* first (stepping into Berserker Stance and back to Battle), falling back to *Shield Bash* without stance dancing.
  - Interrupts only fire on active cast bars or channels; delayed instants never trigger interrupts.
- **Shouts:** Maintains *Battle Shout* on party members (skipping if an equal or stronger *Blessing of Might* is active, or below 10 rage). Protection maintains *Demoralizing Shout*. *Intimidating Shout* is off in dungeons and raids (the `aoe fear` override is not registered on WoW Forever yet).
- **Rage gates:** *Disarm* is only cast on a target that carries a weapon and only with the 20 rage it costs; *Bloodrage* is held while its cooldown runs, below medium health (70%), or below 20 rage to spend. Unusable rows are skipped, so low-rage ticks go to *Sunder Armor* and *Revenge* instead.

---

## Talent builds

Bots spend one talent point per level from 10 to 60 along a preset build. The server checks each point against the class trait tree before it is spent.

| Spec Name | Role | 60 Points (Arms / Fury / Protection) | Key Talents |
| :--- | :--- | :--- | :--- |
| **Fury** | Melee DPS | `16 / 35 / 0` | Bloodthirst, Death Wish, Flurry, Dual Wield Specialization, Improved Whirlwind. |
| **Protection** | Tank | `10 / 0 / 41` | Shield Slam, Concussion Blow, Last Stand, Bastion, Improved Revenge. |
| **Arms** | Melee DPS | `33 / 18 / 0` | Mortal Strike, Sweeping Strikes, Weaponmaster, Impale, Deep Wounds. |

<details>
<summary>Leveling order</summary>

- **Fury:** Cruelty, Unbridled Wrath, Piercing Howl, Blood Craze, Furious Precision, Dual Wield Specialization, Improved Execute, Death Wish, Improved Berserker Rage, Flurry, Improved Whirlwind, Bloodthirst and Enrage; the last 16 points go to Arms (Improved Heroic Strike, Improved Rend, Improved Tactical Mastery, Improved Overpower, Anger Management, Deep Wounds, Deflection).
- **Protection:** Shield Specialization, Anticipation, Defiance, Master of Defense, Improved Sunder Armor, Vanguard, Improved Bloodrage, Concussion Blow, Focused Rage, Bastion, Shield Slam, Improved Revenge, Last Stand, Improved Thunder Clap, Improved Shield Bash and Improved Shield Wall; the last 10 points go to Arms (Improved Heroic Strike, Deflection, Improved Tactical Mastery, Improved Charge).
- **Arms:** Deflection, Improved Rend, Improved Charge, Improved Tactical Mastery, Improved Overpower, Anger Management, Deep Wounds, Two-Handed Weapon Specialization, Impale, Sweeping Strikes, Weaponmaster, Improved Slam and Mortal Strike; the last 18 points go to Fury (Cruelty, Unbridled Wrath, Piercing Howl, Blood Craze, Enrage).

</details>

---

## Class settings

- [`AiPlayerbot.WarriorGapCloser`](../server/settings/class-behaviour.md#aiplayerbotwarriorgapcloser): Lets warriors open with *Charge* from 8–25 yards and use *Intercept* to rescue a party member; off by default.
- [`AiPlayerbot.AnnounceSunderStacks`](../server/settings/class-behaviour.md#aiplayerbotannouncesunderstacks): Says "5 Sunders" once per elite or boss target when a real player is in the group; off by default.

General combat, tanking, and pull settings are configured in [Combat settings](../server/settings/combat.md).
