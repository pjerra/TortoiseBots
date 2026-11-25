---
id: classes-paladin
title: Paladin Bot Guide
category: classes
summary: Complete guide to Paladin bot AI, Holy healing, Protection tanking, Retribution DPS, blessing coordination, and Judgement upkeep.
tags: [classes, paladin, tank, healer, dps, hybrid]
relates_to:
  - classes-overview
---

# Paladin Bot Guide

Paladins provide resilient group utility, customizable auras, coordinated blessings, and capable Tank, Healer, or Melee DPS performance.

## Specs & roles

| Spec | Role | Primary Focus |
| :--- | :--- | :--- |
| **Holy** | Healer | Efficient single-target healing, emergency saves, and raid upkeep. |
| **Protection** | Tank | High AoE threat, Consecration holding, and Holy Shield mitigation. |
| **Retribution** | Melee DPS | Two-handed melee holy burst, Seal cycling, and Judgement maintenance. |

---

## What the bot does in combat

### Holy (Healer)
- Casts *Lay on Hands* on party members at critical health (20% default), claiming the target to prevent duplicate casts.
- Uses *Flash of Light* for efficient maintenance and *Holy Light* for heavy damage spikes.
- Downranks direct heals (*Holy Light*, *Flash of Light*) to match wounds per [shared downranking rules](overview.md#shared-bot-behaviors).
- Casts *Holy Shock* on allies below 50% health (`LowHealth` default).
- Weaves *Holy Strike* only when already in melee reach, without moving out of healing position.
- Deals off-spec damage (*Hammer of Wrath* on dying targets, *Holy Shock*, *Judgement*, *Exorcism* on Undead or Demon targets, *Consecration* into packs) per [healer off-DPS rules](overview.md#shared-bot-behaviors).
- Casts *Divine Shield* (Bubble) when personally critically wounded.

### Protection (Tank)
- Maintains *Righteous Fury* in and out of combat to boost holy threat generation.
- Has no taunt on ForeverCore: *Hand of Reckoning* exists only as a runecarving glove rune (the Libram of Justice grants the engraving), and *Righteous Defense* does not exist here, so the tank holds threat with damage and *Judgement* and cannot pull a mob off a party member.
- Drops *Consecration* against 3 or more attackers in melee reach while above medium mana (40% default), or against 2 at 70%+ mana.
- Keeps *Holy Shield* active on cooldown for block rating and reflective holy damage.
- Judges *Seal of Righteousness* on the primary target for sustained threat.

### Retribution (Melee DPS)
- Maintains *Seal of Command* (falling back to *Seal of Righteousness*) and unleashes *Judgement* on cooldown.
- Uses *Holy Strike* as its melee strike.
- Fires *Exorcism* instantly upon *Art of War* procs (procced Exorcism hits any enemy; unprocced Exorcism only hits Undead and Demon targets).
- Casts *Hammer of Wrath* as an execute when targets drop below 20% health.
- Off-heals wounded allies (*Lay on Hands* at critical health, *Holy Light* at low health, *Blessing of Protection* on an attacked ally) per [off-healing rules](overview.md#shared-bot-behaviors).

<details>
<summary>Judgement upkeep in groups</summary>

When enabled, paladin bots coordinate to maintain *Judgement of Wisdom* and *Judgement of Light* on bosses or elite targets:
- **Assignment:** Retribution paladins maintain *Judgement of Wisdom*; Holy paladins maintain *Judgement of Light*. A second non-tank paladin takes the unassigned debuff.
- **Protection Exemption:** Protection tanks never upkeep group judgements, keeping *Seal of Righteousness* for threat.
- **Timing:** Debuffs are refreshed when 4 seconds or fewer remain.
- **Mana Gates:** Everyone needs at least medium mana; a healing paladin needs 50% mana and judges only while nobody is below the medium-health line (see [Combat settings](../server/settings/combat.md#aiplayerbotmediumhealth)).
- **Player Paladins:** Active judgements placed by human players are respected and never overwritten.

</details>

---

## Custom abilities

- **Holy Strike:** Forever's paladin melee strike, used by Retribution and Protection.
- **The Art of War:** a proc that lets *Exorcism* hit any enemy instead of only Undead and Demons.
- **Bulwark of the Righteous:** Not on ForeverCore yet.
- **Crusader Aura:** Not on ForeverCore yet.

---

## Utility & interrupts

- **Blessings Coordination:** Coordinates blessings across party classes (*Kings*, *Might*, *Wisdom*, *Light*). Each paladin assigns one blessing per member, and multiple paladins stack distinct blessing families. Buff claims prevent duplicate casts. Holy healers never re-buff mid-fight; blessings are an out-of-combat job. *Blessing of Salvation* and *Blessing of Sanctuary* are Not on ForeverCore yet.
- **Manual Blessing Assignment:** [`.bot blessing`](../play/commands/bot-panel.md#bot-blessing) sets, per paladin, the blessing each class gets; classes left on `auto` keep the coordination above.
- **Hands & Blessings:** Casts *Blessing of Protection* on non-tank allies facing physical danger (checking *Forbearance* to prevent wasted casts) and *Blessing of Freedom* on rooted tanks (Holy) or rooted allies (other specs). *Hand of Freedom* is Not on ForeverCore yet.
- **Auras:** Holy runs *Concentration Aura*, Protection runs *Retribution Aura*, and Retribution runs *Sanctity Aura*. In raids, the first paladin bot in the fight auto-swaps to *Fire* or *Shadow Resistance Aura* on applicable bosses, then drops it out of combat. [`.bot choice`](../play/commands/bot-panel.md#bot-choice) orders one aura per paladin instead; the spec upkeep and the raid swap both stand down.
- **Cleansing:** Uses *Cleanse* and *Purify* to remove poisons, diseases, and magic debuffs from self and allies.
- **Crowd Control & Stuns:** Casts *Hammer of Justice* to stun dangerous casters or runners. *Repentance* interrupts with no humanoid restriction and *Turn Undead* covers assigned-CC undead.
- **Solo Survival:** Solo paladins with mana at or above the low-mana line (15 default) stand their ground and heal through combat rather than fleeing when outnumbered.

---

## Talent builds

Bots spend one talent point per level from 10 to 60 along a preset build. The server checks each point against the class trait tree before it is spent.

| Spec Name | Role | 60 Points (Holy / Protection / Retribution) | Key Talents |
| :--- | :--- | :--- | :--- |
| **Holy** | Healer | `35 / 16 / 0` | Holy Shock, Divine Favor, Illumination, Holy Power, Light's Vigil. |
| **Protection** | Tank | `0 / 41 / 10` | Holy Shield, Reckoning, Shieldsworn, Iron Creed, Swift Judgement. |
| **Retribution** | Melee DPS | `10 / 5 / 36` | Seal of Command, Repentance, Vengeance, Champion of the Light, Sanctified Judgement. |

<details>
<summary>Leveling order</summary>

- **Holy:** Spiritual Focus, Healing Light, Reverence, Divine Favor, Illumination, Holy Conduit, Infusion of Light, Oath of Light, Holy Shock, Boundless Light, Purifying Power, Holy Power, Light's Vigil, Divine Precision and Consecrated Ground; the last 16 points go to Protection (Divine Intellect, Guardian's Favor, Sacred Duty, Redoubt, Toughness, Improved Hammer of Justice).
- **Protection:** Redoubt, Precision, Guardian's Favor, Sacred Duty, Shield Specialization, Toughness, Templar's Bulwark, Improved Righteous Fury, Anticipation, Holy Shield, Stoicism, Iron Creed, Shieldsworn, Improved Hammer of Justice, Swift Judgement and Reckoning; the last 10 points go to Retribution (Benediction, Improved Judgement, Deflection).
- **Retribution:** Benediction, Improved Judgement, Conviction, Seal of Command, Vindication, Pursuit of Justice, Two-Handed Weapon Specialization, Sacred Arbiter, Eye for an Eye, Repentance, Vengeance, Champion of the Light, Unsealed Wrath, Sanctified Judgement and Deflection; the last 10 points go to Holy (Divine Strength, Improved Seals) and 5 to Protection (Divine Intellect).

</details>

---

## Class settings

- [`AiPlayerbot.HolyPaladinMeleeWeave`](../server/settings/class-behaviour.md#aiplayerbotholypaladinmeleeweave): Controls whether grouped Holy paladins walk into melee of non-raid targets to weave *Holy Strike* and *Seal of Wisdom* when no urgent heal is needed.
- [`AiPlayerbot.PaladinJudgementUpkeep`](../server/settings/class-behaviour.md#aiplayerbotpaladinjudgementupkeep): Controls whether paladins coordinate to maintain Judgement of Wisdom and Judgement of Light on bosses and elite targets.
- [`AiPlayerbot.ProtPaladinDungeonWisdom`](../server/settings/class-behaviour.md#aiplayerbotprotpaladindungeonwisdom): Controls whether Protection paladins keep Seal of Wisdom on 5-man dungeon trash and backfill Judgement of Wisdom when no non-tank paladin can own it.
