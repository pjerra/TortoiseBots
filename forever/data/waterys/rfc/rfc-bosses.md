# Ragefire Chasm bosses — Forever 1.60.1.70959

Core data first. Boss rows come from TDB 2026-10-14 `creature_template`, `creature_classic_level`,
`creature_classic_stats`, `creature_classic_classlevelstats` and `smart_scripts`; spell effects from the
70959 client `SpellEffect.db2`/`SpellName.db2`.

## Health

All four are level 16, `Classification=1` (elite), and have no high-level health modifier at 16
(`Classic.CreatureHealth.RaiseFromLevel=20`; `Classic.ForeverCreatureHealth=0` default). The base health is
`creature_classic_classlevelstats.health` for the creature's `unit_class` at level 16, times the creature's
`creature_classic_stats.health_multiplier`, times the elite rate (`Rate.Creature.HP.Elite=1`):

| Boss | Unit class | Base HP | Multiplier | Health |
|---|---:|---:|---:|---:|
| Oggleflint | 1 | 356 | 4.00 | **1,424** |
| Jergosh the Invoker | 8 | 307 | 4.50 | **1,382** (1381.5 rounded) |
| Bazzalan | 1 | 356 | 4.25 | **1,513** |
| Taragaman the Hungerer | 1 | 356 | 5.25 | **1,869** |

`health` in the CSV is the computed value; the formula and every input are in `health_formula`. If a live
config changes `Classic.ForeverCreatureHealth`, `Rate.Creature.HP.Elite`, or the level-20 health raise, these
numbers change.

## Abilities and reactions

| Boss | Ability | Effect (SpellEffect) | Timer (SmartAI) | Reaction |
|---|---|---|---|---|
| Oggleflint | **Cleave 5532** | Weapon damage (Effect 58) to target + one nearby (chain 2) | 8–15 s initial, 5–12 s repeat | Tank faces away; melee behind; kill the two trogg adds first |
| Taragaman | **Uppercut 18072** | Weapon damage (58) + **knockback** (98, base 125) | 7–12 s / 6–16 s | Tank on rock, back to rock, not lava; healer/ranged off the edge |
| Taragaman | **Fire Nova 11970** | Fire damage (2), base ~620, 10 yd radius | 9–14 s / 10–18 s | Ranged/healer at max range; heal through; not interruptible at 18 |
| Jergosh | **Immolate 20800** | Direct Fire (2, ~327) + Fire DoT (aura 3, ~94) | 14 s / 6–23 s | Interrupt with Shield Bash; Priest Dispel Magic at 18 |
| Jergosh | **Curse of Weakness 18267** | Curse aura 13, reduces physical damage | 5 s / 7–16 s | No curse dispel at 18 (mage/druid only); wait before bursting |
| Bazzalan | **Sinister Strike 14873** | Weapon damage no school (17) + bonus | Range event, 4.9–15.7 s | Healer expects spikes; tank cooldowns |
| Bazzalan | **Poison 744** | Nature DoT (aura 3, ~70) | Range event, 10.2–15 s | No Cure Poison in a 1/1/3 warrior/priest party; heal through and kill fast |

No boss has a C++ `ScriptName`; all four use SmartAI rows. Bosses do not heal or flee. Adds are static
spawns (Oggleflint: 1 Shaman + 1 Trogg near; Jergosh: 3 Cultists; Bazzalan: 2 Cultists; Taragaman: 1
Enforcer) and should be cleared/assigned before the boss.

## Party notes (1 prot / 1 priest / 3 arms, level 18)

Only the prot warrior has Shield Bash for Immolate; Taunt/Defensive Stance are a separate trainer-data fix
(`forever-warrior-trainer-fix.sql`). No poison cleanse and no curse dispel in this party. With the trainer
fix, the prot warrior can hold the bosses in Defensive Stance and use Taunt/Sunder.

CSV: `rfc-bosses.csv` (7 boss-ability rows, all values sourced).
