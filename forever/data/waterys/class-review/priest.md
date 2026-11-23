# Priest - ticket #210 class review

Sources: talent-builds.csv, talents.csv, talents-detail.csv, forever-spell-ids.csv, datamap/priest.csv (stale where marked), class-notes/priest.md, rfc/talents-level*.csv, ai/playerbot/strategy/priest/.
Client build for builds: 70888 (repack server/data/dbc 2026-11-11).

## discipline

Port summary: `points=51 | unmapped points=0/51 | mapped talents=19 | position fallback=5 | rank violations=0 | unmapped=0`

### Build (26 steps)

| order | talent | rank | why |
|---|---|---|---|
| 1 | 345 | 2 | TB Discipline 166 rank 2 -> Forever 345 (matched by spell) |
| 2 | 342 | 3 | TB Discipline 167 rank 3 -> Forever 342 (matched by position-custom) |
| 3 | 352 | 5 | TB Discipline 169 rank 5 -> Forever 352 (matched by spell) |
| 4 |  | 0 | OVERLAP: TB Discipline 170 shares Forever 342 (capped at 5, 3 pts freed) |
| 5 | 342 | 2 | TB Discipline 170 rank 5 -> Forever 342 (matched by spell) |
| 6 | 344 | 2 | TB Discipline 172 rank 2 -> Forever 344 (matched by spell) |
| 7 | 346 | 2 | TB Discipline 173 rank 2 -> Forever 346 (matched by spell) |
| 8 | 348 | 1 | TB Discipline 174 rank 1 -> Forever 348 (matched by spell) |
| 9 | 343 | 3 | TB Discipline 175 rank 3 -> Forever 343 (matched by spell) |
| 10 | 347 | 3 | TB Discipline 176 rank 3 -> Forever 347 (matched by spell) |
| 11 |  | 0 | OVERLAP: TB Discipline 177 shares Forever 346 (capped at 3, 2 pts freed) |
| 12 | 346 | 1 | TB Discipline 177 rank 3 -> Forever 346 (matched by position-custom) |
| 13 | 1201 | 3 | TB Discipline 179 rank 3 -> Forever 1201 (matched by spell) |
| 14 | 1201 | 1 | TB Discipline 180 rank 1 -> Forever 1201 (matched by position-custom) |
| 15 | 351 | 1 | TB Discipline 181 rank 1 -> Forever 351 (matched by position-custom) |
| 16 | 1202 | 5 | TB Discipline 182 rank 5 -> Forever 1202 (matched by spell) |
| 17 | 322 | 1 | TB Discipline 183 rank 1 -> Forever 322 (matched by position-custom) |
| 18 | 406 | 3 | TB Holy 184 rank 3 -> Forever 406 (matched by spell) |
| 19 | 410 | 2 | TB Holy 185 rank 2 -> Forever 410 (matched by spell) |
| 20 | 401 | 5 | TB Holy 186 rank 5 -> Forever 401 (matched by spell) |
| 21 | 1181 | 1 | TB Holy 187 rank 1 -> Forever 1181 (matched by spell) |
| 22 | 345 | 1 | TOP-UP +1: freed Turtle-custom points -> Forever 345 (Discipline, spell) |
| 23 | 1201 | 1 | TOP-UP +1: freed Turtle-custom points -> Forever 1201 (Discipline, spell) |
| 24 | 1181 | 1 | TOP-UP +1: freed Turtle-custom points -> Forever 1181 (Holy, spell) |
| 25 | 345 | 1 | TOP-UP +1: freed Turtle-custom points -> Forever 345 (Discipline, spell) |
| 26 | 1181 | 1 | TOP-UP +1: freed Turtle-custom points -> Forever 1181 (Holy, spell) |

### Non-spell-matched talent steps (verify on Forever)

- tree Discipline TB talent 167 rank 3 -> Forever 342 (max 5) via `position-custom`; Forever spells 14522;14788;14789;14790;14791
- tree Discipline TB talent 177 rank 3 -> Forever 346 (max 3) via `position-custom`; Forever spells 14747;14770;14771
- tree Discipline TB talent 180 rank 1 -> Forever 1201 (max 5) via `position-custom`; Forever spells 18551;18552;18553;18554;18555
- tree Discipline TB talent 181 rank 1 -> Forever 351 (max 1) via `position-custom`; Forever spells 14752
- tree Discipline TB talent 183 rank 1 -> Forever 322 (max 1) via `position-custom`; Forever spells 10060

### Strategy conditions

power infusion boost trigger+action (GenericPriestStrategy.cpp:62, PriestStrategy.cpp:579-581, Triggers.h:238, Actions.h:304-306); inner focus action + InnerFocusForHealTrigger (!HasSpell gate, Triggers.cpp:139-142); fear ward row (PriestStrategy.cpp:522-523).

## holy

Port summary: `points=51 | unmapped points=0/51 | mapped talents=19 | position fallback=4 | rank violations=0 | unmapped=0`

### Build (24 steps)

| order | talent | rank | why |
|---|---|---|---|
| 1 | 345 | 2 | TB Discipline 166 rank 2 -> Forever 345 (matched by spell) |
| 2 | 341 | 5 | TB Discipline 168 rank 5 -> Forever 341 (matched by spell) |
| 3 | 352 | 2 | TB Discipline 169 rank 2 -> Forever 352 (matched by spell) |
| 4 | 321 | 2 | TB Discipline 171 rank 2 -> Forever 321 (matched by spell) |
| 5 | 344 | 2 | TB Discipline 172 rank 2 -> Forever 344 (matched by spell) |
| 6 | 348 | 1 | TB Discipline 174 rank 1 -> Forever 348 (matched by spell) |
| 7 | 406 | 3 | TB Holy 184 rank 3 -> Forever 406 (matched by spell) |
| 8 | 410 | 2 | TB Holy 185 rank 2 -> Forever 410 (matched by spell) |
| 9 | 1181 | 5 | TB Holy 187 rank 5 -> Forever 1181 (matched by spell) |
| 10 | 1635 | 2 | TB Holy 189 rank 2 -> Forever 1635 (matched by spell) |
| 11 | 361 | 3 | TB Holy 191 rank 3 -> Forever 361 (matched by spell) |
| 12 |  | 0 | OVERLAP: TB Holy 193 shares Forever 1635 (capped at 2, 2 pts freed) |
| 13 | 408 | 3 | TB Holy 194 rank 3 -> Forever 408 (matched by spell) |
| 14 | 402 | 5 | TB Holy 195 rank 5 -> Forever 402 (matched by spell) |
| 15 | 413 | 2 | TB Holy 196 rank 2 -> Forever 413 (matched by position-custom) |
| 16 | 1561 | 1 | TB Holy 197 rank 1 -> Forever 1561 (matched by spell) |
| 17 |  | 0 | OVERLAP: TB Holy 198 shares Forever 402 (capped at 5, 3 pts freed) |
| 18 | 404 | 5 | TB Holy 199 rank 5 -> Forever 404 (matched by spell) |
| 19 | 1637 | 1 | TB Holy 200 rank 1 -> Forever 1637 (matched by position-custom) |
| 20 | 345 | 1 | TOP-UP +1: freed Turtle-custom points -> Forever 345 (Discipline, spell) |
| 21 | 352 | 1 | TOP-UP +1: freed Turtle-custom points -> Forever 352 (Discipline, spell) |
| 22 | 345 | 1 | TOP-UP +1: freed Turtle-custom points -> Forever 345 (Discipline, spell) |
| 23 | 352 | 1 | TOP-UP +1: freed Turtle-custom points -> Forever 352 (Discipline, spell) |
| 24 | 345 | 1 | TOP-UP +1: freed Turtle-custom points -> Forever 345 (Discipline, spell) |

### Non-spell-matched talent steps (verify on Forever)

- tree Holy TB talent 193 rank 2 -> Forever 1635 (max 2) via `position-custom`; Forever spells 27789;27790
- tree Holy TB talent 196 rank 2 -> Forever 413 (max 2) via `position-custom`; Forever spells 14911;15018
- tree Holy TB talent 198 rank 3 -> Forever 402 (max 5) via `position-custom`; Forever spells 14901;15028;15029;15030;15031
- tree Holy TB talent 200 rank 1 -> Forever 1637 (max 1) via `position-custom`; Forever spells 724

### Strategy conditions

lightwell action/factory (Actions.h:209, HolyPriestStrategy.cpp:12-16); ascendance 52962 capstone trigger+action (HolyPriestStrategy.cpp:321-325, Actions.h:99, AiObjectContext.cpp:241).

## shadow

Port summary: `points=51 | unmapped points=0/51 | mapped talents=18 | position fallback=1 | rank violations=0 | unmapped=0`

### Build (18 steps)

| order | talent | rank | why |
|---|---|---|---|
| 1 | 345 | 2 | TB Discipline 166 rank 2 -> Forever 345 (matched by spell) |
| 2 | 341 | 5 | TB Discipline 168 rank 5 -> Forever 341 (matched by spell) |
| 3 | 342 | 4 | TB Discipline 170 rank 4 -> Forever 342 (matched by spell) |
| 4 | 344 | 2 | TB Discipline 172 rank 2 -> Forever 344 (matched by spell) |
| 5 | 348 | 1 | TB Discipline 174 rank 1 -> Forever 348 (matched by spell) |
| 6 | 343 | 3 | TB Discipline 175 rank 3 -> Forever 343 (matched by spell) |
| 7 | 347 | 3 | TB Discipline 176 rank 3 -> Forever 347 (matched by spell) |
| 8 | 481 | 5 | TB Shadow 202 rank 5 -> Forever 481 (matched by spell) |
| 9 | 466 | 2 | TB Shadow 203 rank 2 -> Forever 466 (matched by spell) |
| 10 | 482 | 2 | TB Shadow 204 rank 2 -> Forever 482 (matched by spell) |
| 11 | 463 | 5 | TB Shadow 205 rank 5 -> Forever 463 (matched by spell) |
| 12 | 501 | 1 | TB Shadow 209 rank 1 -> Forever 501 (matched by spell) |
| 13 | 881 | 2 | TB Shadow 211 rank 2 -> Forever 881 (matched by spell) |
| 14 | 461 | 5 | TB Shadow 212 rank 5 -> Forever 461 (matched by spell) |
| 15 | 484 | 1 | TB Shadow 214 rank 1 -> Forever 484 (matched by spell) |
| 16 | 1638 | 2 | TB Shadow 215 rank 2 -> Forever 1638 (matched by position-custom) |
| 17 | 462 | 5 | TB Shadow 216 rank 5 -> Forever 462 (matched by spell) |
| 18 | 521 | 1 | TB Shadow 217 rank 1 -> Forever 521 (matched by spell) |

### Non-spell-matched talent steps (verify on Forever)

- tree Shadow TB talent 215 rank 2 -> Forever 1638 (max 2) via `position-custom`; Forever spells 27839;27840

### Strategy conditions

mind flay / vampiric embrace / silence actions; VE + shadow-weaving-missing>mind-flay + shadowform + feedback rows (ShadowPriestStrategy.cpp:27-29,74-75,235-240). Spirit Tap proc by ID HasAura(15271), Shadow Weaving gate by talent-rank spells 15257/15331-15334 (Triggers.h:262-315).

## Shared strategy notes

Shadowform gating: every heal has remove-shadowform prereq; wand-blocked-in-form probes + mind-flay-vs-smite by HasAura(shadowform) (Triggers.cpp:202-203,231-234); ShadowformTrigger = HasSpell && !HasAura (:95). VE upkeep + remove-shadowform-before-resurrect (NonCombat :25,27).

## Spells not OK on Forever

All audited spells OK.
