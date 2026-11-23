# Rogue - ticket #210 class review

Sources: talent-builds.csv, talents.csv, talents-detail.csv, forever-spell-ids.csv, datamap/rogue.csv (stale where marked), class-notes/rogue.md, rfc/talents-level*.csv, ai/playerbot/strategy/rogue/.
Client build for builds: 70888 (repack server/data/dbc 2026-11-11).

## assassination

Port summary: `points=51 | unmapped points=0/51 | mapped talents=18 | position fallback=4 | rank violations=1 | unmapped=0 | Assassination 6/1 rank 2 > Forever max 1 (talent 382)`

### Build (24 steps)

| order | talent | rank | why |
|---|---|---|---|
| 1 | 270 | 5 | TB Assassination 130 rank 5 -> Forever 270 (matched by spell) |
| 2 | 273 | 3 | TB Assassination 131 rank 3 -> Forever 273 (matched by spell) |
| 3 | 274 | 2 | TB Assassination 132 rank 2 -> Forever 274 (matched by spell) |
| 4 | 277 | 3 | TB Assassination 133 rank 3 -> Forever 277 (matched by spell) |
| 5 | 281 | 1 | TB Assassination 134 rank 1 -> Forever 281 (matched by spell) |
| 6 | 269 | 5 | TB Assassination 136 rank 5 -> Forever 269 (matched by spell) |
| 7 | 279 | 2 | TB Assassination 137 rank 2 -> Forever 279 (matched by spell) |
| 8 | 682 | 3 | TB Assassination 138 rank 3 -> Forever 682 (matched by spell) |
| 9 | 268 | 3 | TB Assassination 139 rank 3 -> Forever 268 (matched by spell) |
| 10 |  | 0 | OVERLAP: TB Assassination 140 shares Forever 279 (capped at 3, 2 pts freed) |
| 11 | 279 | 1 | TB Assassination 140 rank 3 -> Forever 279 (matched by row-custom) |
| 12 | 280 | 1 | TB Assassination 141 rank 1 -> Forever 280 (matched by position-custom) |
| 13 |  | 0 | OVERLAP: TB Assassination 142 shares Forever 280 (capped at 1, 1 pts freed) |
| 14 | 382 | 1 | TB Assassination 143 rank 2 -> Forever 382 (matched by spell) |
| 15 | 283 | 5 | TB Assassination 144 rank 5 -> Forever 283 (matched by spell) |
| 16 |  | 0 | OVERLAP: TB Assassination 145 shares Forever 382 (capped at 1, 1 pts freed) |
| 17 | 186 | 5 | TB Combat 110 rank 5 -> Forever 186 (matched by row) |
| 18 | 202 | 3 | TB Combat 113 rank 3 -> Forever 202 (matched by spell) |
| 19 | 181 | 3 | TB Combat 114 rank 3 -> Forever 181 (matched by spell) |
| 20 | 682 | 1 | TOP-UP +1: freed Turtle-custom points -> Forever 682 (Assassination, spell) |
| 21 | 268 | 1 | TOP-UP +1: freed Turtle-custom points -> Forever 268 (Assassination, spell) |
| 22 | 181 | 1 | TOP-UP +1: freed Turtle-custom points -> Forever 181 (Combat, spell) |
| 23 | 682 | 1 | TOP-UP +1: freed Turtle-custom points -> Forever 682 (Assassination, spell) |
| 24 | 268 | 1 | TOP-UP +1: freed Turtle-custom points -> Forever 268 (Assassination, spell) |

### Non-spell-matched talent steps (verify on Forever)

- tree Assassination TB talent 140 rank 3 -> Forever 279 (max 3) via `row-custom`; Forever spells 14174;14175;14176
- tree Assassination TB talent 141 rank 1 -> Forever 280 (max 1) via `position-custom`; Forever spells 14177
- tree Assassination TB talent 145 rank 1 -> Forever 382 (max 1) via `position-custom`; Forever spells 14983
- tree Combat TB talent 110 rank 5 -> Forever 186 (max 5) via `row`; Forever spells 13712;13788;13789;13790;13791

### Strategy conditions

eviscerate HIGH+2 (+almost-dead), SnD HIGH+1, envenom HIGH+1 (MISSING - dead node), rupture @3, noxious assault NORMAL+1, backstab (fallback sinister). Boost: cold blood @4 combo HIGH+5 (spell id unverified - not in spell audit).

## combat

Port summary: `points=51 | unmapped points=0/51 | mapped talents=18 | position fallback=3 | rank violations=0 | unmapped=0`

### Build (20 steps)

| order | talent | rank | why |
|---|---|---|---|
| 1 | 270 | 5 | TB Assassination 130 rank 5 -> Forever 270 (matched by spell) |
| 2 | 273 | 3 | TB Assassination 131 rank 3 -> Forever 273 (matched by spell) |
| 3 | 274 | 2 | TB Assassination 132 rank 2 -> Forever 274 (matched by spell) |
| 4 | 277 | 3 | TB Assassination 133 rank 3 -> Forever 277 (matched by spell) |
| 5 | 281 | 1 | TB Assassination 134 rank 1 -> Forever 281 (matched by spell) |
| 6 | 269 | 5 | TB Assassination 136 rank 5 -> Forever 269 (matched by spell) |
| 7 | 279 | 1 | TB Assassination 137 rank 1 -> Forever 279 (matched by spell) |
| 8 | 186 | 5 | TB Combat 110 rank 5 -> Forever 186 (matched by row) |
| 9 | 202 | 3 | TB Combat 113 rank 3 -> Forever 202 (matched by spell) |
| 10 | 181 | 5 | TB Combat 114 rank 5 -> Forever 181 (matched by spell) |
| 11 | 222 | 2 | TB Combat 116 rank 2 -> Forever 222 (matched by spell) |
| 12 | 221 | 5 | TB Combat 120 rank 5 -> Forever 221 (matched by spell) |
| 13 | 223 | 1 | TB Combat 122 rank 1 -> Forever 223 (matched by position-custom) |
| 14 | 242 | 2 | TB Combat 123 rank 2 -> Forever 242 (matched by spell) |
| 15 | 1703 | 2 | TB Combat 124 rank 2 -> Forever 1703 (matched by spell) |
| 16 |  | 0 | OVERLAP: TB Combat 125 shares Forever 1703 (capped at 2, 2 pts freed) |
| 17 | 1122 | 3 | TB Combat 126 rank 3 -> Forever 1122 (matched by spell) |
| 18 | 205 | 1 | TB Combat 127 rank 1 -> Forever 205 (matched by spell) |
| 19 | 279 | 1 | TOP-UP +1: freed Turtle-custom points -> Forever 279 (Assassination, spell) |
| 20 | 242 | 1 | TOP-UP +1: freed Turtle-custom points -> Forever 242 (Combat, spell) |

### Non-spell-matched talent steps (verify on Forever)

- tree Combat TB talent 110 rank 5 -> Forever 186 (max 5) via `row`; Forever spells 13712;13788;13789;13790;13791
- tree Combat TB talent 122 rank 1 -> Forever 223 (max 1) via `position-custom`; Forever spells 13877
- tree Combat TB talent 125 rank 2 -> Forever 1703 (max 2) via `position-custom`; Forever spells 30919;30920

### Strategy conditions

riposte + surprise attack HIGH+3 (surprise needs reactive-dodge proc; 52511 unverified), expose armor HIGH, almost-dead evis, SnD HIGH+2, rupture, sinister. Boost: adrenaline rush HIGH+5, blade flurry HIGH+4. AoE: blade flurry + oil of immolation.

## subtlety

Port summary: `points=51 | unmapped points=8/51 | mapped talents=19 | position fallback=7 | rank violations=0 | unmapped=3 | Subtlety 1/4 rank 3: Turtle talent 148 has no Forever talent (none) | Subtlety 3/2 rank 3: Turtle talent 153 has no Forever talent (custom) | Subtlety 5/4 rank 2: Turtle talent 162 has no Forever talent (custom)`

### Build (29 steps)

| order | talent | rank | why |
|---|---|---|---|
| 1 | 270 | 5 | TB Assassination 130 rank 5 -> Forever 270 (matched by spell) |
| 2 | 273 | 3 | TB Assassination 131 rank 3 -> Forever 273 (matched by spell) |
| 3 | 274 | 2 | TB Assassination 132 rank 2 -> Forever 274 (matched by spell) |
| 4 | 244 | 5 | TB Subtlety 146 rank 5 -> Forever 244 (matched by spell) |
| 5 | 261 | 2 | TB Subtlety 147 rank 2 -> Forever 261 (matched by position) |
| 6 |  | 0 | DROP Turtle talent 148 (Subtlety, 3 pts): no Forever counterpart (none) |
| 7 | 263 | 3 | TB Subtlety 149 rank 3 -> Forever 263 (matched by spell) |
| 8 | 247 | 2 | TB Subtlety 150 rank 2 -> Forever 247 (matched by spell) |
| 9 | 1123 | 3 | TB Subtlety 151 rank 3 -> Forever 1123 (matched by spell) |
| 10 | 245 | 3 | TB Subtlety 152 rank 3 -> Forever 245 (matched by spell) |
| 11 |  | 0 | DROP Turtle talent 153 (Subtlety, 3 pts): no Forever counterpart (custom) |
| 12 |  | 0 | OVERLAP: TB Subtlety 154 shares Forever 263 (capped at 3, 1 pts freed) |
| 13 | 681 | 1 | TB Subtlety 155 rank 1 -> Forever 681 (matched by spell) |
| 14 | 246 | 2 | TB Subtlety 156 rank 2 -> Forever 246 (matched by position-custom) |
| 15 | 262 | 2 | TB Subtlety 157 rank 2 -> Forever 262 (matched by spell) |
| 16 | 265 | 2 | TB Subtlety 159 rank 2 -> Forever 265 (matched by spell) |
| 17 | 284 | 1 | TB Subtlety 160 rank 1 -> Forever 284 (matched by spell) |
| 18 |  | 0 | OVERLAP: TB Subtlety 161 shares Forever 265 (capped at 2, 1 pts freed) |
| 19 |  | 0 | DROP Turtle talent 162 (Subtlety, 2 pts): no Forever counterpart (custom) |
| 20 | 1702 | 2 | TB Subtlety 163 rank 2 -> Forever 1702 (matched by row-custom) |
| 21 | 1702 | 2 | TB Subtlety 164 rank 2 -> Forever 1702 (matched by position-custom) |
| 22 | 381 | 1 | TB Subtlety 165 rank 1 -> Forever 381 (matched by position-custom) |
| 23 | 262 | 1 | TOP-UP +1: freed Turtle-custom points -> Forever 262 (Subtlety, spell) |
| 24 | 261 | 1 | TOP-UP +1: freed Turtle-custom points -> Forever 261 (Subtlety, position) |
| 25 | 246 | 1 | TOP-UP +1: freed Turtle-custom points -> Forever 246 (Subtlety, position-custom) |
| 26 | 1702 | 1 | TOP-UP +1: freed Turtle-custom points -> Forever 1702 (Subtlety, row-custom) |
| 27 | 261 | 1 | TOP-UP +1: freed Turtle-custom points -> Forever 261 (Subtlety, position) |
| 28 | 261 | 1 | TOP-UP +1: freed Turtle-custom points -> Forever 261 (Subtlety, position) |
| 29 |  | 0 | SHORT 4 pts: no headroom left in mapped talents |

### Non-spell-matched talent steps (verify on Forever)

- tree Subtlety TB talent 147 rank 2 -> Forever 261 (max 5) via `position`; Forever spells 14057;14072;14073;14074;14075
- tree Subtlety TB talent 148 rank 3 -> Forever  (max ) via `none`; Forever spells 
- tree Subtlety TB talent 153 rank 3 -> Forever  (max ) via `custom`; Forever spells 
- tree Subtlety TB talent 154 rank 1 -> Forever 263 (max 3) via `position-custom`; Forever spells 14079;14080;14081
- tree Subtlety TB talent 156 rank 2 -> Forever 246 (max 3) via `position-custom`; Forever spells 13983;14070;14071
- tree Subtlety TB talent 161 rank 1 -> Forever 265 (max 2) via `position-custom`; Forever spells 14082;14083
- tree Subtlety TB talent 162 rank 2 -> Forever  (max ) via `custom`; Forever spells 
- tree Subtlety TB talent 163 rank 2 -> Forever 1702 (max 5) via `row-custom`; Forever spells 30902;30903;30904;30905;30906
- tree Subtlety TB talent 164 rank 2 -> Forever 1702 (max 5) via `position-custom`; Forever spells 30902;30903;30904;30905;30906
- tree Subtlety TB talent 165 rank 1 -> Forever 381 (max 1) via `position-custom`; Forever spells 14183

### Strategy conditions

eviscerate (+almost-dead), SnD, shadow of death / mark for death / smoke bomb (all MISSING - queued but inert), rupture @3, ghostly strike front vs hemorrhage back/front builders. Dungeon feint/vanish need HasSpell + 5-man + tank-aggro.

## Shared strategy notes

blade flurry near cc>remove; thistle tea no-energy; blind/vanish critical; evasion/vanish low-HP; sprint; poisons (instant MH / deadly+instant OH); sap-cc-mark>stealth+sap; garrote/sinister high energy; expose armor skips if ANY sunder rank on target (id-based).

## Spells not OK on Forever

| spell | rank1 id | status | note |
|---|---|---|---|
| envenom |  | MISSING | TBC spell; no vanilla/Forever equivalent |
| mark for death |  | MISSING | TB rogue action; verify Turtle/Forever id |
| shadow of death |  | MISSING | TB rogue action; no vanilla/Forever spell by this name |
| smoke bomb |  | MISSING | TB rogue action; verify Turtle/Forever id |
