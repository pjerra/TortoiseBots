# Ragefire Chasm — Forever data sheet (level-18 test party)

Client build **1.60.1.70938** (2026-11-17); world/hotfixes **TDB 2026-10-14**. Map **389**, four
bosses, entrance inside Orgrimmar (Cleft of Shadow). Core data wins; where a guide disagrees with the
TDB/SmartAI rows, the local rows are marked.

Test party: Alliance humans, level 18 — 1 prot warrior, 1 priest, 3 arms warriors. No druid, shaman or
paladin, so **no poison cleanse and no curse dispel** in the group; plan around that (below).

Sources: local TDB `creature_template`, `creature_classic_level`, `creature`, `smart_scripts`,
`creature_template_spell`; client `SpellName`/`SpellEffect` DB2 70938; [Wowhead Classic RFC guide](https://www.wowhead.com/classic/guide/ragefire-chasm-dungeon-strategy-wow-classic);
[Warcraft Wiki RFC](https://warcraft.wiki.gg/wiki/Ragefire_Chasm); [Hearthproof Forever RFC guide](https://hearthproof.com/dungeons/ragefire-chasm/)
(fan guide; used for Forever-specific loot/route claims and marked where local data differs).

## Mobs

| Entry | Name | Role | Level | Spawns | Behavior / ability (spell id) |
|---:|---|---|---|---:|---|
| 11318 | Ragefire Trogg | trash | 13–15 | 29 | Strike **11976**; no heal/flee |
| 11319 | Ragefire Shaman | trash | 13–15 | 14 | **Heals** (Healing Wave **11986**, ~30–35 s); Lightning Bolt **9532**; **flees at ≤15%** |
| 11320 | Earthborer | trash | 13–14 | 11 | Earthborer Acid **18070**, stacking armor debuff |
| 11321 | Molten Elemental | trash | 13–15 | 13 | Fire Shield **18968** (retaliation trigger 11968) |
| 11322 | Searing Blade Cultist | trash | 13–15 | 29 | Curse of Agony **18266** |
| 11323 | Searing Blade Enforcer | trash | 13–15 | 21 | Shield Slam **8242** (short stun) |
| 11324 | Searing Blade Warlock | trash | 13–15 | 10 | Shadow Bolt **20791**; summons Voidwalker **12746**; **flees at ≤15%** |
| 11834 | Maur Grimtotem | quest object | 14 | 1 | non-hostile corpse/satchel (npcflag 2, faction 35), not a combat mob |
| 11517 | Oggleflint | boss | 16 | 1 | Cleave **5532**; 2 trogg adds at his cave |
| 11518 | Jergosh the Invoker | boss | 16 | 1 | Immolate **20800**; Curse of Weakness **18267**; Cultist adds nearby |
| 11519 | Bazzalan | boss | 16 | 1 | Sinister Strike **14873**; Poison **744**; Cultist adds nearby |
| 11520 | Taragaman the Hungerer | boss | 16 | 1 | Uppercut **18072** (knockback); Fire Nova **11970** |

**Calls for help:** no RFC SmartAI row uses CALL_FOR_HELP, and none of these templates has a script; only
social aggro. Three behaviours matter for a bot group: the Shaman heals, and the Shaman/Warlock flee at
15% (SmartAI event 2 → action 25). No RFC mob is a healer besides the Shaman, and no boss flees.

**Boss adds:** TDB has static spawns near the bosses, not scripted summons: Oggleflint has one Ragefire
Shaman and one Ragefire Trogg close (plus Maur's corpse); Jergosh has three Searing Blade Cultists near
his platform; Bazzalan two Cultists; Taragaman one Enforcer nearby. These counts include ambient packs —
clear the room first so a patrol does not join. Hearthproof describes two adds for Oggleflint/Jergosh/
Bazzalan and a patrol for Taragaman; treat the static count as the minimum.

## Route and pulls

1. Entry corridor → first room: two Earthborers + a Molten Elemental. Pull worms one at a time; acid stacks.
2. Neck with lava on both sides; fight on the near side.
3. Trogg canyon: Troggs + Shamans. **Shaman first** in every pull; interrupt Healing Wave. A Shaman patrols
   the ramp — wait for it.
4. West cave (first right at the canyon head): Maur's corpse and Oggleflint's south lobe. Oggleflint is
   optional; the corpse is on the same lobe, so expect to fight him to reach it.
5. Lava cavern: Cultists/Enforcers/Warlocks on the paths; Taragaman in the middle on rock. Clear around him
   and wait out patrols before pulling.
6. Last chamber: Jergosh on the floor; Bazzalan on the ledge/shelf. Either order; both are required for
   Hidden Enemies (Horde quest).

## Boss tactics (level-18 five-man)

**Oggleflint (optional).** Face him away from the group (Cleave). With no mage in the party, there is no
Polymorph: tank picks up the two adds, DPS kills one add then the other, then the boss. The satchel corpse
is in the same lobe; do not pull the boss with the corpse interaction if the group is not ready.

**Taragaman the Hungerer.** Tank him in the middle of the rock, **back to rock, not lava**; Uppercut knocks
the tank back. Fire Nova hits everyone near him: healer and ranged at max range, away from the rock edge.
Priest heals through; no nature/fire dispel needed. Kill the nearby Enforcer first so its stun does not
overlap the knockback.

**Jergosh the Invoker.** Two (TDB: three nearby) Cultist adds. Tank controls them while DPS kills one; then
boss. **Interrupt Immolate** (Shield Bash; only the prot warrior reliably has it). Curse of Weakness slows
the tank's threat; the party has no curse dispel (mage 18/druid 24 only), so wait a few seconds after it
lands before bursting. Priest can dispel the Immolate burn at 18 (magic), but not the curse.

**Bazzalan.** Pull the lone Cultist if it separates; otherwise control one add, kill the other, then boss.
**Poison** is a Nature DoT on his target; this party has **no Cure Poison** (druid/paladin/shaman only), so
heal through it and kill him fast. Sinister Strike gives healer spikes; keep the tank topped. He is on the
ledge/shelf; do not fight with the group strung out on the climb.

## Differences from classic 1.12 / Forever notes

- **Loot:** Forever has twelve Rare boss items, three per boss, all required level 13 / item level 18.
  Six are Forever-new: all three Oggleflint items (Barbaric Crossbow, Trogg Scepter, Bone Knuckles) and all
  three Bazzalan items (Searing Dagger, Satyrskin Cloak, Chasm Walkers); the other six are Classic. New
  quest reward option **Garrison Cuffs (270003)** is added to the satchel bracers line. This is the main
  player-visible difference; the fight scripts are not custom.
- **Abilities:** local spell ids and names match the Classic encounter (Cleave, Uppercut, Fire Nova,
  Immolate, Curse of Weakness, Sinister Strike, Poison). There is **no boss C++ script** here; all four use
  SmartAI/creature spell rows, so timers are the VMaNGOS spell-list timers above, not a custom phase script.
- **Flee:** Ragefire Shaman and Searing Blade Warlock have a 15% flee action. Classic guides describe the
  Shaman heal and the Warlock pet but usually omit the flee; for bots, expect a runner and finish it before
  it reaches another pack.
- **Quests:** RFC quests are Horde-only; an Alliance human party has no quests here (the portal is in
  Orgrimmar). The Forever group finder lists the dungeon from level 13; the meeting stone does not summon.
- **Levels:** trash 13–15, bosses 16 (Hearthproof's Forever run confirms the same), so a level-18 party is
  above the instance's intended 14–16 band but still needs the mechanics.
- **No call-for-help:** do not assume a runner will bring the room; only the two flee behaviours above
  create unexpected pulls.

## Party-specific notes (1 prot / 1 priest / 3 arms)

- Only the prot warrior has a reliable interrupt (Shield Bash) for Healing Wave, Shadow Bolt and Immolate;
  assign it and keep it off cooldown for those casts. At level 18 the other warriors have no interrupt
  (Pummel is a level-38 trainer skill).
- No poison cleanse: Bazzalan's Poison and Earthborer Acid must be healed/raced. No curse dispel: Curse of
  Agony/Weakness run their duration.
- No crowd control beyond taunt/stun: Oggleflint/Jergosh/Bazzalan adds must be tanked and killed, not
  polymorphed. Kill the Shaman first in every trogg pull.
- Priest: Dispel Magic (18) removes Immolate's magic burn; Fear Ward is not needed here. Keep the tank
  healed through Uppercut/Fire Nova and the Enforcer stun.
- Gear/supplies: see `gear-level18.csv`, `food-water-5-25.csv`, `bandages-by-skill.csv` in this directory.

## Evidence

- `rfc-mobs.csv`: one row per template with entries, levels, spawn counts, SmartAI/spell rows.
- Local data: map 389, 12 templates (8 trash/quest + 4 bosses); `creature_classic_level` gives 13–16.
- Spell names/effects: client DB2 70938 (`SpellName`, `SpellEffect`); intervals from `smart_scripts`.
- Forever-specific loot/quest/route claims: Hearthproof RFC guide; local item rows cross-check the ids
  where it names them. The Hearthproof guide is a fan source, not core data.
