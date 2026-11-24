---
id: classes-priest
title: Priest Bot Guide
category: classes
summary: Complete guide to Priest bot AI, healing ladders, Power Word Shield rules, threat drops, Shadow DPS, and class abilities.
tags: [classes, priest, healer, dps, ranged]
relates_to:
  - classes-overview
---

# Priest Bot Guide

Priests provide comprehensive healing coverage through layered spell ladders, damage absorption shields, party dispels, and viable ranged Shadow DPS.

## Specs & roles

| Spec | Role | Primary Focus |
| :--- | :--- | :--- |
| **Holy** | Healer | Maximum throughput healing, group AoE heals, and spirit scaling. |
| **Discipline** | Healer / Support | Damage absorption shielding, mana efficiency, and support damage. |
| **Shadow** | Ranged DPS | Periodic Shadow damage, Mind Flay channels, and Vampiric Embrace party healing. |

---

## What the bot does in combat

### The Priest Healing Ladder
Priests select healing spells based on health bands (bands follow [Combat settings](../server/settings/combat.md); all lines below use the defaults Critical 20 / Low 50 / Medium 70 / Almost-Full 90):
- **Critical (below 20%):** *Power Word: Shield* + *Flash Heal* (missing *Flash Heal* rows fall through to *Greater Heal* / *Heal* / *Lesser Heal*).
- **Low (20–50%):** *Power Word: Shield* + *Greater Heal* (+ *Heal* / *Lesser Heal* fallbacks at low level). The same heavy-damage and low-mana pace rule as the medium band applies here, so the pick never downgrades from fast *Flash Heal* to a slow cast as the bar crosses the band line; loose mobs on non-tanks keep *Flash Heal* ahead.
- **Medium (50–70%):** *Power Word: Shield* + *Greater Heal* / *Heal* / *Lesser Heal* by deficit (Holy also uses *Renew* for light wounds). Heavy incoming damage (the bar would leave the band before a slow cast lands) switches the pick to fast *Flash Heal* while the healer's mana is comfortable; a low-mana healer keeps the efficient heals unless the member is about to fall into the critical band.
- **Almost-Full (70–90%):** *Renew* (Discipline shields first).
- **Multiple Injured:** *Prayer of Healing* when 3 or more members of the priest's own party within 30 yards are hurt at or below 70%. The spell only reaches the priest's party, so hurt raid members in other parties do not trigger it.

Direct heals are downranked to match target wounds per [shared downranking rules](overview.md#shared-bot-behaviors), using the lowest rank covering missing health. Full ranks are used at or below critical health (20% default) — or when heavy incoming damage would push the target below it before the cast lands. *Desperate Prayer* serves as a self-only emergency heal.

### Power Word: Shield & Weakened Soul Refusal
The bot verifies that the target does not have the *Weakened Soul* debuff before casting *Power Word: Shield*. If present, the shield is skipped in favor of direct heals. Above 20% health the shield is also held off a warrior or bear tank, in or out of combat, since absorbed hits give no rage and starve the tank's threat (a shield cast between pulls still eats the next pull's opening hits); the shield still lands as the save at or below 20%. A warrior in Defensive Stance or a druid in Bear Form counts as a tank for this rule even without Protection or Feral talents, but an explicitly assigned non-tank role wins over the stance. A damage-dealing warrior who switches to Defensive Stance counts as a tank while he holds it, unless he has an assigned non-tank role.

### Threat Drops & Loose Adds
- **Fade:** Casts *Fade* when climbing toward the tank's threat (60% or more) on a mob the tank holds, or immediately when attacked by a mob in a group (the attacked row runs at the highest priority; the climbing-threat row sits below heal rows).
- **Loose Add Priority:** If loose mobs attack non-tanks, Holy and Discipline priests prioritize fast *Flash Heal* casts over slow *Greater Heal* casts to avoid drawing further aggro.

### Shadow DPS Rotation
1. Maintains *Shadowform*.
2. Keeps *Vampiric Embrace* active on primary targets (party healing comes from the server spell effect).
3. Applies and maintains *Shadow Word: Pain* (plus *Devouring Plague* wherever known — an undead racial, gated by the spellbook).
4. Casts *Mind Blast* on cooldown.
5. Channels *Mind Flay* as primary filler, stacking *Shadow Weaving* where talented.
6. Casts *Silence* to interrupt dangerous enemy spellcasters and healers.
7. Off-heals wounded allies (*Power Word: Shield*, *Greater Heal*, *Renew* by wound band) per [off-healing rules](overview.md#shared-bot-behaviors). In *Shadowform* the priest leaves a *Renew* to another healer who already covers that ally, and drops the form only when its heal can go out at once (not during a global cooldown or a wand swing).

<details>
<summary>Healer off-spec damage and wand stops</summary>

- **Off-spec DPS:** Enabled by default via the `offdps` strategy per [healer off-DPS rules](overview.md#shared-bot-behaviors), casting *Shadow Word: Pain*, *Holy Fire*, *Smite*, *Starshards* (only above 50% mana and beyond 15 yards), or *Mind Blast* (*Holy Nova* into packed melee) against engaged mobs.
- **Wand Stops:** The bot stops wanding as soon as a heal or a nuke needs the cast window. A separate `wand blocks cc` trigger stops wanding while a *Shackle Undead* cast is queued. A Holy or Discipline priest in a group checks every moment while it wands (not once per shot), so it stops the wand and heals within one wand swing of a hit.

</details>

---

## Custom abilities

- **Chastise:** a ranged disorient at interrupt priority, used to stop enemy casters.
- **Ascendance:** Not on ForeverCore yet.

---

## Utility & interrupts

- **Party Buffs:** Maintains *Power Word: Fortitude*, *Divine Spirit*, and *Shadow Protection*. Replaces single-target casts with group prayers (*Prayer of Fortitude*, etc.) when trained and reagents are stocked.
- **Fear Ward:** Casts *Fear Ward* on the party tank first to prevent fear-based wipes, re-applying once the cooldown clears.
- **Dispels:** Uses *Dispel Magic* on allies, and *Abolish Disease* (falling back to *Cure Disease*) on infected party members.
- **Crowd Control:** Casts *Shackle Undead* on assigned undead CC marks (in dungeons only on the assigned mark unless `auto cc` is on; open world and raids also free-pick). *Psychic Scream* is off in dungeons, raids, and real-player groups (the `aoe fear` override is not registered on ForeverCore yet).
- **Resurrection:** Revives fallen allies out of combat with *Resurrection* (dropping *Shadowform* first).

---

## Talent builds

Bots spend one talent point per level from 10 to 60 along a preset build. The server checks each point against the class trait tree before it is spent.

| Spec Name | Role | 60 Points (Discipline / Holy / Shadow) | Key Talents |
| :--- | :--- | :--- | :--- |
| **Holy** | Healer | `13 / 38 / 0` | Spirit of Redemption, Spiritual Guidance, Spiritual Healing, Prayer of Mending, Holy Nova. |
| **Shadow** | Ranged DPS | `15 / 0 / 36` | Mind Flay, Shadowform, Vampiric Embrace, Vampiric Touch, Shadow Weaving. |
| **Discipline** | Healer / Support | `40 / 11 / 0` | Power Infusion, Penance, Divine Aegis, Renewed Hope, Soul Warding. |

<details>
<summary>Leveling order</summary>

- **Holy:** Improved Renew, Holy Specialization, Divine Fury, Inspiration, Holy Nova, Blessed Recovery, Holy Reach, Improved Healing, Spirit of Redemption, Spiritual Guidance, Litany of Light, Spiritual Healing, Prayer of Mending, Twilight Focus and Holy Specialization; the last 13 points go to Discipline (Wand Specialization, Twin Disciplines, Martyrdom, Mental Strength, Mental Agility, Inner Focus).
- **Shadow:** Shadow Focus, Improved Mind Blast, Mind Flay, Improved Mind Flay, Improved Shadow Word: Pain, Vampiric Embrace, Shadow Weaving, Dark Inspiration, Silence, Devouring Contagion, Darkness, Early Demise, Shadowform, Shadow Reach and Spirit Tap; the last 15 points go to Discipline (Twin Disciplines, Mental Strength, Mental Agility, Inner Focus, Meditation).
- **Discipline:** Twin Disciplines, Silent Resolve, Mental Strength, Inner Focus, Improved Power Word: Shield, Meditation, Mental Agility, Improved Inner Fire, Soul Warding, Penance, Renewed Hope, Divine Aegis, Power Infusion and Power in Light; the last 11 points go to Holy (Improved Renew, Twilight Focus, Divine Fury, Holy Specialization).

</details>

The Shadow premade skips *Vampiric Touch* in play: the talent is taken in the build but the bot never casts it.

---

## Class settings

There are no class-specific configuration flags for Priests. General healing thresholds, mana reserves, and reaction times are configured in [Combat settings](../server/settings/combat.md).
