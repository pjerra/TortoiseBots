# Class review summary (The Sub: warrior/priest/mage/rogue/hunter) - ticket #210

Per-class files: warrior.md, priest.md, mage.md, rogue.md, hunter.md (this branch, DeepSeek adds warlock/paladin/shaman/druid).

## Build health (from talents.csv)

| class | spec | points | unmapped pts | fallbacks | violations | unmapped talents |
|---|---|---|---|---|---|---|
| warrior | fury | 51 | 0/51 | 2 | 0 | 0 unmapped=0 |
| warrior | protection | 51 | 2/51 | 2 | 0 | 1 Protection 5/4 rank 2: Turtle talent 89 has no Forever talent (custom) |
| warrior | arms | 51 | 0/51 | 4 | 0 | 0 unmapped=0 |
| hunter | beastmastery | 51 | 3/51 | 3 | 0 | 1 Beast Mastery 5/1 rank 3: Turtle talent 385 has no Forever talent (custom) |
| hunter | marksmanship | 51 | 2/51 | 4 | 0 | 1 Marksmanship 4/1 rank 2: Turtle talent 419 has no Forever talent (custom) |
| hunter | survival | 51 | 0/51 | 7 | 1 | 0 Survival 5/2 rank 3 > Forever max 2 (talent 1322) |
| rogue | combat | 51 | 0/51 | 3 | 0 | 0 unmapped=0 |
| rogue | assassination | 51 | 0/51 | 4 | 1 | 0 Assassination 6/1 rank 2 > Forever max 1 (talent 382) |
| rogue | subtlety | 51 | 8/51 | 7 | 0 | 3 Subtlety 5/4 rank 2: Turtle talent 162 has no Forever talent (custom) |
| priest | holy | 51 | 0/51 | 4 | 0 | 0 unmapped=0 |
| priest | shadow | 51 | 0/51 | 1 | 0 | 0 unmapped=0 |
| priest | discipline | 51 | 0/51 | 5 | 0 | 0 unmapped=0 |
| mage | arcane | 51 | 5/51 | 4 | 0 | 1 Arcane 6/3 rank 5: Turtle talent 54 has no Forever talent (custom) |
| mage | fire | 51 | 0/51 | 1 | 0 | 0 unmapped=0 |
| mage | frost | 51 | 0/51 | 2 | 0 | 0 unmapped=0 |

## Dead conditions / missing spells (strategy will never fire)

| class/spec | dead item | cause |
|---|---|---|
| warrior arms | master strike row | spell MISSING (no vanilla/Forever name) |
| priest shadow | none found | all rotation spells OK; gates are aura/id-based and live |
| mage arcane | arcane rupture action | spell MISSING (Turtle 51949, no Forever equivalent) |
| mage frost/any | icicles + channel-check | spell MISSING (hardcoded channel ids 52516/51991/51995/51997) |
| rogue assassination | envenom | TBC spell, no Forever equivalent |
| rogue subtlety | mark for death / shadow of death / smoke bomb | all MISSING on Forever |
| hunter any | aspect of the viper logic | spell has no Forever equivalent (classic); trigger/action dead |
| hunter any | aspect of the beast trigger | TBC spell, MISSING |
| rogue any | cold blood / hemorrhage / ghostly strike / riposte / surprise attack / prep / gouge / blind / thistle tea | NOT in forever-spell-ids.csv audit - verify before sign-off |
| mage low ranks | rank 2+ of Imp Frostbolt/Precision/Ice Shards etc | SpellName has rank 1 only - verify live GetSpellInfo |
| rogue low ranks | rank 2+ of Imp Eviscerate/Precision/Dual Wield etc | SpellName has rank 1 only - verify live GetSpellInfo |
| hunter | Revive Pet 982 / Beast Training 5149 trainer rows | zero rows in dump AND live ibd; bots force-learn via spell_learn_spell/AutoLearnSpellAction - trainer data gap remains |

## Custom / position-matched talents to double-check on Forever (not spell-verified)

| spec | TB talent -> Forever | method |
|---|---|---|
| warrior arms | 62->131, 67->132, 69->125, 71->134 | position-custom x3 + position |
| warrior fury | 97->154, 103->168 | row-custom + position-custom |
| warrior prot | 88->149, 91->702 | position-custom x2 (89 DROPPED custom) |
| priest disc | 167->342, 177->346, 180->1201, 181->351, 183->322 | position-custom x5 |
| priest holy | 193->1635, 196->413, 198->402, 200->1637 | position-custom x4 |
| priest shadow | 215->1638 | position-custom |
| mage arcane | 45->85, 52->77, 53->77, 48->1142 | position/row-custom x4 (54 DROPPED custom) |
| mage fire | TB-Fire15->33 | row-custom |
| mage frost | TB-Frost32->64, TB-Frost35->68 | position-custom x2 |
| rogue assa | TB140->279, TB141->280, TB145->382, TB110->186 | row/position-custom x4 (+382 rank violation) |
| rogue combat | TB137->279, TB122->223, TB125->1703 | mixed x3 |
| rogue subtlety | 7 fallbacks (244/261/246/262/265/284/381 area + 1702 row) | 3 DROPPED (148/153/162, 8 pts, SHORT 4) |
| hunter BM | 380->1391, 388->1397, 390->1397 | position + row-custom (385 DROPPED, SHORT 2) |
| hunter MM | 422->1347, 423->1351, 425/426->1362, 427->1361 | position/row-custom x4 (419 DROPPED) |
| hunter survival | 392->1623, 401->1308, 403->1322, 408->1312, 411->1325 + 1311/1308 row | x7 (1322 rank violation 3>2) |

