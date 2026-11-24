# Warrior - ticket #210 class review

Sources: talent-builds.csv, talents.csv, talents-detail.csv, forever-spell-ids.csv, datamap/warrior.csv (stale where marked), class-notes/warrior.md, rfc/talents-level*.csv, ai/playerbot/strategy/warrior/.
Client build for builds: 70888 (repack server/data/dbc 2026-11-11).

## arms

Port summary: `points=51 | unmapped points=0/51 | mapped talents=18 | position fallback=4 | rank violations=0 | unmapped=0`

### Build (20 steps)

| order | talent | rank | why |
|---|---|---|---|
| 1 | 641 | 5 | TB Arms 57 rank 5 -> Forever 641 (matched by spell) |
| 2 | 127 | 2 | TB Arms 58 rank 2 -> Forever 127 (matched by spell) |
| 3 | 126 | 2 | TB Arms 59 rank 2 -> Forever 126 (matched by spell) |
| 4 | 130 | 5 | TB Arms 60 rank 5 -> Forever 130 (matched by spell) |
| 5 | 128 | 3 | TB Arms 61 rank 3 -> Forever 128 (matched by spell) |
| 6 | 131 | 1 | TB Arms 62 rank 1 -> Forever 131 (matched by position-custom) |
| 7 |  | 0 | OVERLAP: TB Arms 63 shares Forever 131 (capped at 2, 1 pts freed) |
| 8 | 131 | 1 | TB Arms 63 rank 2 -> Forever 131 (matched by spell) |
| 9 | 121 | 3 | TB Arms 64 rank 3 -> Forever 121 (matched by spell) |
| 10 | 136 | 3 | TB Arms 65 rank 3 -> Forever 136 (matched by spell) |
| 11 | 662 | 2 | TB Arms 66 rank 2 -> Forever 662 (matched by spell) |
| 12 | 132 | 5 | TB Arms 67 rank 5 -> Forever 132 (matched by position-custom) |
| 13 | 133 | 1 | TB Arms 68 rank 1 -> Forever 133 (matched by spell) |
| 14 | 125 | 3 | TB Arms 69 rank 3 -> Forever 125 (matched by position-custom) |
| 15 | 134 | 2 | TB Arms 71 rank 2 -> Forever 134 (matched by position) |
| 16 | 135 | 1 | TB Arms 73 rank 1 -> Forever 135 (matched by spell) |
| 17 | 157 | 5 | TB Fury 94 rank 5 -> Forever 157 (matched by spell) |
| 18 | 159 | 5 | TB Fury 96 rank 5 -> Forever 159 (matched by spell) |
| 19 | 160 | 1 | TB Fury 98 rank 1 -> Forever 160 (matched by spell) |
| 20 | 127 | 1 | TOP-UP +1: freed Turtle-custom points -> Forever 127 (Arms, spell) |

### Non-spell-matched talent steps (verify on Forever)

- tree Arms TB talent 62 rank 1 -> Forever 131 (max 2) via `position-custom`; Forever spells 12290;12963
- tree Arms TB talent 67 rank 5 -> Forever 132 (max 5) via `position-custom`; Forever spells 12700;12781;12783;12784;12785
- tree Arms TB talent 69 rank 3 -> Forever 125 (max 5) via `position-custom`; Forever spells 12284;12701;12702;12703;12704
- tree Arms TB talent 71 rank 2 -> Forever 134 (max 5) via `position`; Forever spells 12165;12830;12831;12832;12833

### Strategy conditions

ArmsWarriorStrategy.cpp:79-85 mortal strike + master strike rows (master strike MISSING - never fires); :442-537 sweeping strikes 12292 + battle stance aura + CD gating for whirlwind/mortal/heroic/cleave AoE.

## fury

Port summary: `points=51 | unmapped points=0/51 | mapped talents=17 | position fallback=2 | rank violations=0 | unmapped=0`

### Build (17 steps)

| order | talent | rank | why |
|---|---|---|---|
| 1 | 641 | 5 | TB Arms 57 rank 5 -> Forever 641 (matched by spell) |
| 2 | 127 | 2 | TB Arms 58 rank 2 -> Forever 127 (matched by spell) |
| 3 | 130 | 3 | TB Arms 60 rank 3 -> Forever 130 (matched by spell) |
| 4 | 131 | 2 | TB Arms 63 rank 2 -> Forever 131 (matched by spell) |
| 5 | 121 | 3 | TB Arms 64 rank 3 -> Forever 121 (matched by spell) |
| 6 | 662 | 2 | TB Arms 66 rank 2 -> Forever 662 (matched by spell) |
| 7 | 157 | 5 | TB Fury 94 rank 5 -> Forever 157 (matched by spell) |
| 8 | 1581 | 5 | TB Fury 95 rank 5 -> Forever 1581 (matched by spell) |
| 9 | 159 | 5 | TB Fury 96 rank 5 -> Forever 159 (matched by spell) |
| 10 | 154 | 4 | TB Fury 97 rank 4 -> Forever 154 (matched by row-custom) |
| 11 | 160 | 1 | TB Fury 98 rank 1 -> Forever 160 (matched by spell) |
| 12 | 661 | 2 | TB Fury 99 rank 2 -> Forever 661 (matched by spell) |
| 13 | 168 | 3 | TB Fury 103 rank 3 -> Forever 168 (matched by position-custom) |
| 14 | 165 | 1 | TB Fury 104 rank 1 -> Forever 165 (matched by spell) |
| 15 | 1542 | 2 | TB Fury 105 rank 2 -> Forever 1542 (matched by spell) |
| 16 | 156 | 5 | TB Fury 107 rank 5 -> Forever 156 (matched by spell) |
| 17 | 167 | 1 | TB Fury 109 rank 1 -> Forever 167 (matched by spell) |

### Non-spell-matched talent steps (verify on Forever)

- tree Fury TB talent 97 rank 4 -> Forever 154 (max 5) via `row-custom`; Forever spells 12318;12857;12858;12860;12861
- tree Fury TB talent 103 rank 3 -> Forever 168 (max 5) via `position-custom`; Forever spells 12862;12330;20497;20498;20499

### Strategy conditions

FuryWarriorStrategy.cpp:62-64,304-306 bloodthirst + death wish triggers; ACTION_NODE_A(berserker_rage_fear).

## protection

Port summary: `points=51 | unmapped points=2/51 | mapped talents=17 | position fallback=2 | rank violations=0 | unmapped=1 | Protection 5/4 rank 2: Turtle talent 89 has no Forever talent (custom)`

### Build (22 steps)

| order | talent | rank | why |
|---|---|---|---|
| 1 | 124 | 3 | TB Arms 56 rank 3 -> Forever 124 (matched by spell) |
| 2 | 641 | 4 | TB Arms 57 rank 4 -> Forever 641 (matched by spell) |
| 3 | 142 | 2 | TB Protection 74 rank 2 -> Forever 142 (matched by spell) |
| 4 | 1601 | 5 | TB Protection 75 rank 5 -> Forever 1601 (matched by spell) |
| 5 | 138 | 3 | TB Protection 76 rank 3 -> Forever 138 (matched by spell) |
| 6 | 140 | 5 | TB Protection 78 rank 5 -> Forever 140 (matched by spell) |
| 7 | 153 | 1 | TB Protection 79 rank 1 -> Forever 153 (matched by spell) |
| 8 | 143 | 2 | TB Protection 81 rank 2 -> Forever 143 (matched by spell) |
| 9 | 147 | 3 | TB Protection 82 rank 3 -> Forever 147 (matched by spell) |
| 10 | 149 | 2 | TB Protection 83 rank 2 -> Forever 149 (matched by spell) |
| 11 | 144 | 5 | TB Protection 85 rank 5 -> Forever 144 (matched by spell) |
| 12 | 702 | 5 | TB Protection 86 rank 5 -> Forever 702 (matched by spell) |
| 13 | 148 | 1 | TB Protection 87 rank 1 -> Forever 148 (matched by spell) |
| 14 |  | 0 | OVERLAP: TB Protection 88 shares Forever 149 (capped at 2, 2 pts freed) |
| 15 |  | 0 | DROP Turtle talent 89 (Protection, 2 pts): no Forever counterpart (custom) |
| 16 | 150 | 2 | TB Protection 90 rank 2 -> Forever 150 (matched by spell) |
| 17 |  | 0 | OVERLAP: TB Protection 91 shares Forever 702 (capped at 5, 3 pts freed) |
| 18 | 152 | 1 | TB Protection 92 rank 1 -> Forever 152 (matched by spell) |
| 19 | 641 | 1 | TOP-UP +1: freed Turtle-custom points -> Forever 641 (Arms, spell) |
| 20 | 138 | 1 | TOP-UP +1: freed Turtle-custom points -> Forever 138 (Protection, spell) |
| 21 | 138 | 1 | TOP-UP +1: freed Turtle-custom points -> Forever 138 (Protection, spell) |
| 22 |  | 0 | SHORT 4 pts: no headroom left in mapped talents |

### Non-spell-matched talent steps (verify on Forever)

- tree Protection TB talent 88 rank 2 -> Forever 149 (max 2) via `position-custom`; Forever spells 12311;12958
- tree Protection TB talent 89 rank 2 -> Forever  (max ) via `custom`; Forever spells 
- tree Protection TB talent 91 rank 3 -> Forever 702 (max 5) via `position-custom`; Forever spells 16538;16539;16540;16541;16542

### Strategy conditions

ProtectionWarriorStrategy.cpp:80-85,173-221,502-507 shield wall (low HP), last stand (critical), shield slam via ImprovedShieldSlam proc aura 51596/51597 + bloodthirst/mortal/concussion/shield bash rows.

## Shared strategy notes

Tactical Mastery ranks 12295,12676-12679 gate rage kept 5/rank (WarriorRageGatePolicy.h:25-29; WarriorActions.h:50-52,76-78; GapCloser.cpp:86-88; Triggers.h:114-116). MockingBlowTrigger (rage-after-swap, taunt+mocking readiness); SunderArmor 5-stack; Bloodrage; Revenge.

## Spells not OK on Forever

| spell | rank1 id | status | note |
|---|---|---|---|
| master strike |  | MISSING | TB warrior action; no vanilla/Forever spell by this name |
