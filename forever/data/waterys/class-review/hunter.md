# Hunter - ticket #210 class review

Sources: talent-builds.csv, talents.csv, talents-detail.csv, forever-spell-ids.csv, datamap/hunter.csv (stale where marked), class-notes/hunter.md, rfc/talents-level*.csv, ai/playerbot/strategy/hunter/.
Client build for builds: 70888 (repack server/data/dbc 2026-11-11).

## beastmastery

Port summary: `points=51 | unmapped points=3/51 | mapped talents=17 | position fallback=3 | rank violations=0 | unmapped=1 | Beast Mastery 5/1 rank 3: Turtle talent 385 has no Forever talent (custom)`

### Build (24 steps)

| order | talent | rank | why |
|---|---|---|---|
| 1 | 1382 | 5 | TB Beast Mastery 373 rank 5 -> Forever 1382 (matched by spell) |
| 2 | 1389 | 5 | TB Beast Mastery 374 rank 5 -> Forever 1389 (matched by spell) |
| 3 | 1395 | 3 | TB Beast Mastery 377 rank 3 -> Forever 1395 (matched by spell) |
| 4 | 1625 | 2 | TB Beast Mastery 378 rank 2 -> Forever 1625 (matched by spell) |
| 5 | 1391 | 1 | TB Beast Mastery 380 rank 1 -> Forever 1391 (matched by position-custom) |
| 6 | 1396 | 5 | TB Beast Mastery 381 rank 5 -> Forever 1396 (matched by spell) |
| 7 | 1390 | 2 | TB Beast Mastery 382 rank 2 -> Forever 1390 (matched by spell) |
| 8 | 1385 | 2 | TB Beast Mastery 383 rank 2 -> Forever 1385 (matched by spell) |
| 9 | 1393 | 5 | TB Beast Mastery 384 rank 5 -> Forever 1393 (matched by spell) |
| 10 |  | 0 | DROP Turtle talent 385 (Beast Mastery, 3 pts): no Forever counterpart (custom) |
| 11 | 1386 | 1 | TB Beast Mastery 386 rank 1 -> Forever 1386 (matched by spell) |
| 12 | 1387 | 1 | TB Beast Mastery 387 rank 1 -> Forever 1387 (matched by spell) |
| 13 | 1397 | 2 | TB Beast Mastery 388 rank 2 -> Forever 1397 (matched by row-custom) |
| 14 | 1388 | 2 | TB Beast Mastery 389 rank 2 -> Forever 1388 (matched by spell) |
| 15 |  | 0 | OVERLAP: TB Beast Mastery 390 shares Forever 1397 (capped at 5, 2 pts freed) |
| 16 | 1397 | 3 | TB Beast Mastery 390 rank 5 -> Forever 1397 (matched by spell) |
| 17 |  | 0 | OVERLAP: TB Beast Mastery 391 shares Forever 1386 (capped at 1, 1 pts freed) |
| 18 | 1342 | 5 | TB Marksmanship 413 rank 5 -> Forever 1342 (matched by spell) |
| 19 | 1348 | 1 | TB Marksmanship 414 rank 1 -> Forever 1348 (matched by spell) |
| 20 | 1348 | 1 | TOP-UP +1: freed Turtle-custom points -> Forever 1348 (Marksmanship, spell) |
| 21 | 1348 | 1 | TOP-UP +1: freed Turtle-custom points -> Forever 1348 (Marksmanship, spell) |
| 22 | 1348 | 1 | TOP-UP +1: freed Turtle-custom points -> Forever 1348 (Marksmanship, spell) |
| 23 | 1348 | 1 | TOP-UP +1: freed Turtle-custom points -> Forever 1348 (Marksmanship, spell) |
| 24 |  | 0 | SHORT 2 pts: no headroom left in mapped talents |

### Non-spell-matched talent steps (verify on Forever)

- tree Beast Mastery TB talent 380 rank 1 -> Forever 1391 (max 1) via `position-custom`; Forever spells 19596
- tree Beast Mastery TB talent 385 rank 3 -> Forever  (max ) via `custom`; Forever spells 
- tree Beast Mastery TB talent 388 rank 2 -> Forever 1397 (max 5) via `row-custom`; Forever spells 19621;19622;19623;19624;19625
- tree Beast Mastery TB talent 391 rank 1 -> Forever 1386 (max 1) via `position-custom`; Forever spells 19574

### Strategy conditions

BM adds kill command (NORMAL+4) and intimidation (NORMAL+3) combat triggers, bestial wrath boost HIGH+6 over base HunterStrategy.

## marksmanship

Port summary: `points=51 | unmapped points=2/51 | mapped talents=16 | position fallback=4 | rank violations=0 | unmapped=1 | Marksmanship 4/1 rank 2: Turtle talent 419 has no Forever talent (custom)`

### Build (24 steps)

| order | talent | rank | why |
|---|---|---|---|
| 1 | 1382 | 5 | TB Beast Mastery 373 rank 5 -> Forever 1382 (matched by spell) |
| 2 | 1624 | 2 | TB Beast Mastery 375 rank 2 -> Forever 1624 (matched by spell) |
| 3 | 1381 | 2 | TB Beast Mastery 376 rank 2 -> Forever 1381 (matched by spell) |
| 4 | 1342 | 5 | TB Marksmanship 413 rank 5 -> Forever 1342 (matched by spell) |
| 5 | 1348 | 5 | TB Marksmanship 414 rank 5 -> Forever 1348 (matched by spell) |
| 6 | 1344 | 5 | TB Marksmanship 415 rank 5 -> Forever 1344 (matched by spell) |
| 7 | 1352 | 2 | TB Marksmanship 416 rank 2 -> Forever 1352 (matched by spell) |
| 8 | 1345 | 1 | TB Marksmanship 417 rank 1 -> Forever 1345 (matched by spell) |
| 9 | 1346 | 3 | TB Marksmanship 418 rank 3 -> Forever 1346 (matched by spell) |
| 10 |  | 0 | DROP Turtle talent 419 (Marksmanship, 2 pts): no Forever counterpart (custom) |
| 11 | 1349 | 5 | TB Marksmanship 420 rank 5 -> Forever 1349 (matched by spell) |
| 12 | 1347 | 1 | TB Marksmanship 422 rank 1 -> Forever 1347 (matched by position-custom) |
| 13 | 1351 | 2 | TB Marksmanship 423 rank 2 -> Forever 1351 (matched by position-custom) |
| 14 |  | 0 | OVERLAP: TB Marksmanship 424 shares Forever 1347 (capped at 3, 1 pts freed) |
| 15 | 1347 | 2 | TB Marksmanship 424 rank 3 -> Forever 1347 (matched by spell) |
| 16 | 1362 | 2 | TB Marksmanship 425 rank 2 -> Forever 1362 (matched by row-custom) |
| 17 |  | 0 | OVERLAP: TB Marksmanship 426 shares Forever 1362 (capped at 5, 2 pts freed) |
| 18 | 1362 | 3 | TB Marksmanship 426 rank 5 -> Forever 1362 (matched by spell) |
| 19 | 1361 | 1 | TB Marksmanship 427 rank 1 -> Forever 1361 (matched by position-custom) |
| 20 | 1381 | 1 | TOP-UP +1: freed Turtle-custom points -> Forever 1381 (Beast Mastery, spell) |
| 21 | 1352 | 1 | TOP-UP +1: freed Turtle-custom points -> Forever 1352 (Marksmanship, spell) |
| 22 | 1346 | 1 | TOP-UP +1: freed Turtle-custom points -> Forever 1346 (Marksmanship, spell) |
| 23 | 1351 | 1 | TOP-UP +1: freed Turtle-custom points -> Forever 1351 (Marksmanship, position-custom) |
| 24 | 1381 | 1 | TOP-UP +1: freed Turtle-custom points -> Forever 1381 (Beast Mastery, spell) |

### Non-spell-matched talent steps (verify on Forever)

- tree Marksmanship TB talent 419 rank 2 -> Forever  (max ) via `custom`; Forever spells 
- tree Marksmanship TB talent 422 rank 1 -> Forever 1347 (max 3) via `position-custom`; Forever spells 19461;19462;24691
- tree Marksmanship TB talent 423 rank 2 -> Forever 1351 (max 3) via `position-custom`; Forever spells 19491;19493;19494
- tree Marksmanship TB talent 425 rank 2 -> Forever 1362 (max 5) via `row-custom`; Forever spells 19507;19508;19509;19510;19511
- tree Marksmanship TB talent 427 rank 1 -> Forever 1361 (max 1) via `position-custom`; Forever spells 19506

### Strategy conditions

MM adds trueshot aura buff non-combat NORMAL+1 over base.

## survival

Port summary: `points=51 | unmapped points=0/51 | mapped talents=18 | position fallback=7 | rank violations=1 | unmapped=0 | Survival 5/2 rank 3 > Forever max 2 (talent 1322)`

### Build (22 steps)

| order | talent | rank | why |
|---|---|---|---|
| 1 | 1389 | 5 | TB Beast Mastery 374 rank 5 -> Forever 1389 (matched by spell) |
| 2 | 1395 | 3 | TB Beast Mastery 377 rank 3 -> Forever 1395 (matched by spell) |
| 3 | 1623 | 3 | TB Survival 392 rank 3 -> Forever 1623 (matched by position-custom) |
| 4 | 1311 | 5 | TB Survival 393 rank 5 -> Forever 1311 (matched by row-custom) |
| 5 | 1304 | 3 | TB Survival 395 rank 3 -> Forever 1304 (matched by spell) |
| 6 | 1621 | 2 | TB Survival 396 rank 2 -> Forever 1621 (matched by spell) |
| 7 | 1305 | 3 | TB Survival 397 rank 3 -> Forever 1305 (matched by spell) |
| 8 | 1306 | 2 | TB Survival 399 rank 2 -> Forever 1306 (matched by position-custom) |
| 9 | 1622 | 5 | TB Survival 400 rank 5 -> Forever 1622 (matched by spell) |
| 10 | 1308 | 1 | TB Survival 401 rank 1 -> Forever 1308 (matched by position-custom) |
| 11 |  | 0 | OVERLAP: TB Survival 402 shares Forever 1308 (capped at 1, 1 pts freed) |
| 12 | 1322 | 2 | TB Survival 403 rank 2 -> Forever 1322 (matched by position-custom) |
| 13 | 1310 | 3 | TB Survival 404 rank 3 -> Forever 1310 (matched by spell) |
| 14 | 1321 | 3 | TB Survival 406 rank 3 -> Forever 1321 (matched by spell) |
| 15 |  | 0 | OVERLAP: TB Survival 407 shares Forever 1322 (capped at 2, 2 pts freed) |
| 16 | 1312 | 1 | TB Survival 408 rank 1 -> Forever 1312 (matched by position-custom) |
| 17 | 1303 | 5 | TB Survival 410 rank 5 -> Forever 1303 (matched by spell) |
| 18 | 1325 | 1 | TB Survival 411 rank 1 -> Forever 1325 (matched by position-custom) |
| 19 | 1304 | 1 | TOP-UP +1: freed Turtle-custom points -> Forever 1304 (Survival, spell) |
| 20 | 1305 | 1 | TOP-UP +1: freed Turtle-custom points -> Forever 1305 (Survival, spell) |
| 21 | 1304 | 1 | TOP-UP +1: freed Turtle-custom points -> Forever 1304 (Survival, spell) |
| 22 | 1305 | 1 | TOP-UP +1: freed Turtle-custom points -> Forever 1305 (Survival, spell) |

### Non-spell-matched talent steps (verify on Forever)

- tree Survival TB talent 392 rank 3 -> Forever 1623 (max 3) via `position-custom`; Forever spells 24293;24294;24295
- tree Survival TB talent 393 rank 5 -> Forever 1311 (max 5) via `row-custom`; Forever spells 19295;19297;19298;19301;19300
- tree Survival TB talent 399 rank 2 -> Forever 1306 (max 2) via `position-custom`; Forever spells 19239;19245
- tree Survival TB talent 401 rank 1 -> Forever 1308 (max 1) via `position-custom`; Forever spells 19263
- tree Survival TB talent 403 rank 2 -> Forever 1322 (max 2) via `position-custom`; Forever spells 19376;19377
- tree Survival TB talent 408 rank 1 -> Forever 1312 (max 1) via `position-custom`; Forever spells 19306
- tree Survival TB talent 411 rank 1 -> Forever 1325 (max 1) via `position-custom`; Forever spells 19386

### Strategy conditions

Survival adds carve in AoE HIGH-1 (below multi-shot, shared 10s CD) over base.

## Shared strategy notes

auto shot idle; aimed/arcane/steady/multi/volley rows; hunter's mark (tank-target); traps (immolation PvE, explosive AoE, frost/freezing CC); scatter interrupt; wyvern sting fallback; concussive/intimidation/counterattack/mongoose melee chain; dead-zone/switch/kite; feign death + disengage threat drops; tranquilizing vs enrage. Pet: no-pet>call pet (combat +1, noncombat +2), tame beast last-resort fallback; safe-to-revive>revive; pet-hp>mend; pet-unhappy>feed. aspect of the viper low-mana + viper-sting logic exist in code but Viper 415423 is not learnable by bots (no trainer row, no preset node, retail-chainless skip) - dead conditions. aspect of the beast is taught by the class-line pass (13161), but only the manual aspect order schedules it.

## Spells not OK on Forever

| spell | rank1 id | status | note |
|---|---|---|---|
| aspect of the viper | 415423 | CHANGED | exists on the Beast Mastery line, but no trainer row/preset buys it and the class-line pass skips retail-era chainless ids; the old 34074 has no learn path either |
| aspect of the wolf |  | MISSING | Turtle custom; no vanilla/Forever equivalent |
| choice aspect |  | PSEUDO | TB pseudo-action, not a spell |
| choice sting |  | PSEUDO | TB pseudo-action, not a spell |
