# Mage - ticket #210 class review

Sources: talent-builds.csv, talents.csv, talents-detail.csv, forever-spell-ids.csv, datamap/mage.csv (stale where marked), class-notes/mage.md, rfc/talents-level*.csv, ai/playerbot/strategy/mage/.
Client build for builds: 70888 (repack server/data/dbc 2026-11-11).

## arcane

Port summary: `points=51 | unmapped points=5/51 | mapped talents=17 | position fallback=4 | rank violations=0 | unmapped=1 | Arcane 6/3 rank 5: Turtle talent 54 has no Forever talent (custom)`

### Build (23 steps)

| order | talent | rank | why |
|---|---|---|---|
| 1 | 74 | 2 | TB Arcane 37 rank 2 -> Forever 74 (matched by spell) |
| 2 | 1650 | 3 | TB Arcane 38 rank 3 -> Forever 1650 (matched by spell) |
| 3 | 80 | 5 | TB Arcane 39 rank 5 -> Forever 80 (matched by spell) |
| 4 | 75 | 5 | TB Arcane 42 rank 5 -> Forever 75 (matched by spell) |
| 5 | 81 | 3 | TB Arcane 44 rank 3 -> Forever 81 (matched by spell) |
| 6 | 85 | 1 | TB Arcane 45 rank 1 -> Forever 85 (matched by position-custom) |
| 7 | 88 | 2 | TB Arcane 47 rank 2 -> Forever 88 (matched by spell) |
| 8 | 1142 | 3 | TB Arcane 48 rank 3 -> Forever 1142 (matched by row-custom) |
| 9 |  | 0 | OVERLAP: TB Arcane 49 shares Forever 1142 (capped at 3, 3 pts freed) |
| 10 | 421 | 3 | TB Arcane 50 rank 3 -> Forever 421 (matched by spell) |
| 11 | 86 | 1 | TB Arcane 51 rank 1 -> Forever 86 (matched by spell) |
| 12 | 77 | 1 | TB Arcane 52 rank 1 -> Forever 77 (matched by position-custom) |
| 13 | 77 | 2 | TB Arcane 53 rank 2 -> Forever 77 (matched by row-custom) |
| 14 |  | 0 | DROP Turtle talent 54 (Arcane, 5 pts): no Forever counterpart (custom) |
| 15 | 87 | 1 | TB Arcane 55 rank 1 -> Forever 87 (matched by spell) |
| 16 | 37 | 5 | TB Frost 19 rank 5 -> Forever 37 (matched by spell) |
| 17 | 1649 | 3 | TB Frost 20 rank 3 -> Forever 1649 (matched by spell) |
| 18 | 61 | 3 | TB Frost 21 rank 3 -> Forever 61 (matched by spell) |
| 19 | 1650 | 1 | TOP-UP +1: freed Turtle-custom points -> Forever 1650 (Arcane, spell) |
| 20 | 77 | 1 | TOP-UP +1: freed Turtle-custom points -> Forever 77 (Arcane, position-custom) |
| 21 | 77 | 1 | TOP-UP +1: freed Turtle-custom points -> Forever 77 (Arcane, row-custom) |
| 22 | 1650 | 1 | TOP-UP +1: freed Turtle-custom points -> Forever 1650 (Arcane, spell) |
| 23 |  | 0 | SHORT 4 pts: no headroom left in mapped talents |

### Non-spell-matched talent steps (verify on Forever)

- tree Arcane TB talent 45 rank 1 -> Forever 85 (max 1) via `position-custom`; Forever spells 28574
- tree Arcane TB talent 48 rank 3 -> Forever 1142 (max 3) via `row-custom`; Forever spells 18462;18463;18464
- tree Arcane TB talent 52 rank 1 -> Forever 77 (max 5) via `position-custom`; Forever spells 11232;12500;12501;12502;12503
- tree Arcane TB talent 53 rank 2 -> Forever 77 (max 5) via `row-custom`; Forever spells 11232;12500;12501;12502;12503
- tree Arcane TB talent 54 rank 5 -> Forever  (max ) via `custom`; Forever spells 

### Strategy conditions

Default arcane missiles; arcane rupture (MISSING - trigger gates HasSpell + buffs 52502/52588, inert); missiles-unaffordable>frostbolt/shoot. Boost: arcane power HIGH+2 (ArcanePowerTrigger: combat + live target + >=70% mana, 12042 no-cancel guard) + presence of mind.

## fire

Port summary: `points=51 | unmapped points=0/51 | mapped talents=18 | position fallback=1 | rank violations=0 | unmapped=0`

### Build (20 steps)

| order | talent | rank | why |
|---|---|---|---|
| 1 | 74 | 2 | TB Arcane 37 rank 2 -> Forever 74 (matched by spell) |
| 2 | 1650 | 3 | TB Arcane 38 rank 3 -> Forever 1650 (matched by spell) |
| 3 | 75 | 5 | TB Arcane 42 rank 5 -> Forever 75 (matched by spell) |
| 4 | 26 | 5 | TB Fire 1 rank 5 -> Forever 26 (matched by spell) |
| 5 | 34 | 5 | TB Fire 3 rank 5 -> Forever 34 (matched by spell) |
| 6 | 28 | 2 | TB Fire 4 rank 2 -> Forever 28 (matched by spell) |
| 7 | 27 | 3 | TB Fire 5 rank 3 -> Forever 27 (matched by spell) |
| 8 | 1141 | 2 | TB Fire 6 rank 2 -> Forever 1141 (matched by spell) |
| 9 | 31 | 3 | TB Fire 7 rank 3 -> Forever 31 (matched by spell) |
| 10 | 29 | 1 | TB Fire 8 rank 1 -> Forever 29 (matched by spell) |
| 11 | 23 | 2 | TB Fire 9 rank 2 -> Forever 23 (matched by spell) |
| 12 | 25 | 3 | TB Fire 10 rank 3 -> Forever 25 (matched by spell) |
| 13 | 1639 | 3 | TB Fire 12 rank 3 -> Forever 1639 (matched by spell) |
| 14 | 32 | 1 | TB Fire 13 rank 1 -> Forever 32 (matched by spell) |
| 15 | 33 | 3 | TB Fire 14 rank 3 -> Forever 33 (matched by spell) |
| 16 |  | 0 | OVERLAP: TB Fire 15 shares Forever 33 (capped at 3, 2 pts freed) |
| 17 | 35 | 5 | TB Fire 16 rank 5 -> Forever 35 (matched by spell) |
| 18 | 36 | 1 | TB Fire 17 rank 1 -> Forever 36 (matched by spell) |
| 19 | 1650 | 1 | TOP-UP +1: freed Turtle-custom points -> Forever 1650 (Arcane, spell) |
| 20 | 1650 | 1 | TOP-UP +1: freed Turtle-custom points -> Forever 1650 (Arcane, spell) |

### Non-spell-matched talent steps (verify on Forever)

- tree Fire TB talent 15 rank 2 -> Forever 33 (max 3) via `row-custom`; Forever spells 11115;11367;11368

### Strategy conditions

fireball filler; fire blast (fallback scorch); no-improved-scorch>scorch HIGH+1; no-fire-vulnerability>scorch; hot streak>pyroblast HIGH+2. Boost: combustion HIGH+1. Id-based: HotStreak needs talent ranks 11095/12872-12875, debuff 22959; procs only 51930/51931 stacks.

## frost

Port summary: `points=51 | unmapped points=0/51 | mapped talents=18 | position fallback=2 | rank violations=0 | unmapped=0`

### Build (20 steps)

| order | talent | rank | why |
|---|---|---|---|
| 1 | 74 | 2 | TB Arcane 37 rank 2 -> Forever 74 (matched by spell) |
| 2 | 1650 | 3 | TB Arcane 38 rank 3 -> Forever 1650 (matched by spell) |
| 3 | 75 | 5 | TB Arcane 42 rank 5 -> Forever 75 (matched by spell) |
| 4 | 37 | 5 | TB Frost 19 rank 5 -> Forever 37 (matched by spell) |
| 5 | 1649 | 3 | TB Frost 20 rank 3 -> Forever 1649 (matched by spell) |
| 6 | 61 | 3 | TB Frost 21 rank 3 -> Forever 61 (matched by spell) |
| 7 | 62 | 2 | TB Frost 23 rank 2 -> Forever 62 (matched by spell) |
| 8 | 73 | 5 | TB Frost 25 rank 5 -> Forever 73 (matched by spell) |
| 9 | 69 | 1 | TB Frost 26 rank 1 -> Forever 69 (matched by spell) |
| 10 | 741 | 2 | TB Frost 28 rank 2 -> Forever 741 (matched by spell) |
| 11 | 66 | 3 | TB Frost 29 rank 3 -> Forever 66 (matched by spell) |
| 12 | 67 | 5 | TB Frost 30 rank 5 -> Forever 67 (matched by spell) |
| 13 | 72 | 1 | TB Frost 31 rank 1 -> Forever 72 (matched by spell) |
| 14 | 64 | 1 | TB Frost 32 rank 1 -> Forever 64 (matched by position-custom) |
| 15 | 64 | 2 | TB Frost 33 rank 2 -> Forever 64 (matched by spell) |
| 16 | 68 | 5 | TB Frost 34 rank 5 -> Forever 68 (matched by spell) |
| 17 |  | 0 | OVERLAP: TB Frost 35 shares Forever 68 (capped at 5, 2 pts freed) |
| 18 | 71 | 1 | TB Frost 36 rank 1 -> Forever 71 (matched by spell) |
| 19 | 1650 | 1 | TOP-UP +1: freed Turtle-custom points -> Forever 1650 (Arcane, spell) |
| 20 | 1650 | 1 | TOP-UP +1: freed Turtle-custom points -> Forever 1650 (Arcane, spell) |

### Non-spell-matched talent steps (verify on Forever)

- tree Frost TB talent 32 rank 1 -> Forever 64 (max 3) via `position-custom`; Forever spells 11190;12489;12490
- tree Frost TB talent 35 rank 2 -> Forever 68 (max 5) via `position-custom`; Forever spells 11180;28592;28593;28594;28595

### Strategy conditions

frostbolt filler; icicles HIGH + icicles-channel-check cancel HIGH+3 (MISSING spell - never fires usefully); frostbolt-unaffordable>shoot. Boost: cold snap HIGH+3.

## Shared strategy notes

MageBoostStrategy branches by spec tab: ARCANE>arcane power, FIRE>combustion, FROST>cold snap (GenericMageStrategy.cpp:119-132). Cure: remove curse on party. Shared: ice block on critical, mana shield (HasSpell-gated), evocation on low mana, wards, frost nova, blink-back 1953.

## Spells not OK on Forever

| spell | rank1 id | status | note |
|---|---|---|---|
| arcane rupture |  | MISSING | Turtle mage spell (51949); no Forever equivalent |
| icicles |  | MISSING | TB mage action; Turtle-side spell, verify id |
