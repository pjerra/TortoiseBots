# W4-A2: travel destinations on ForeverCore

Port of the travel destination layer (W4-A2 of `FOREVER-W0-W4-SPEC.md`, section 2.3) onto the core pin `25fc7ca`: what a free bot
may travel to, how a destination's level is rated, and which data on this core answers the questions the Turtle loader answered
from its own stores. Round A1 (the node graph) and B (generation) are other rounds; this one keeps the boot path, the destination
build and the two pure rules.

## 1. What runs

| Piece | Where |
|---|---|
| Both-core pair (field names, the three travel data sets, the reduced value answers, the purpose gate) | `forever/src/compat/TbCompat_WL4.h`, `ai/cmangos-compat-wl4.h`, wrapper `ai/playerbot/TbCompatWL4.h` |
| Out-of-line ForeverCore side: quest relations, spawn index, purpose map, registry gate, the reduced values | `forever/src/compat/TbCompat_WL4.cpp` (slice-only, filtered like `TbCompat_C.cpp`) |
| Destination manager (area levels, quest table, map transfers, pick rules) | `ai/playerbot/TravelMgr.cpp` (in the slice now) |
| Its values | `ai/playerbot/strategy/values/TravelValues.cpp` (bodies moved to the pair, see 3) |
| Zone level rule | `ai/playerbot/policy/travel/ZoneLevelPolicy.h` + `forever/tools/test_zone_level_policy.cpp` |
| Purpose gate (D5) | `ai/playerbot/policy/travel/TravelPurposeGatePolicy.h` + `forever/tools/test_travel_purpose_gate_policy.cpp` |
| Pins | `forever/tools/test_wl4_wiring.py` (round A1 appends its own classes to the same file) |

## 2. ForeverCore data, instead of the Turtle stores

| Need | Turtle | ForeverCore |
|---|---|---|
| Quest givers/enders and objectives | `ObjectMgr` quest relation stores (`QuestObjectMgr`) and the flat `ReqCreatureOrGOId` | `creature_queststarter/_ender`, `gameobject_queststarter/_ender` and `quest_objectives` rows, through `WorldQueryCompat`; classic quests only (`Expansion = 0`, the retail leftovers 81030/62609/80388 skipped), one line when a table is missing |
| Spawn points per entry | world scan (`WorldPosition().GetCreaturesNear()`/`GetGameObjectsNear()`) | `sObjectMgr` spawn stores, `FetchArea()` once per spawn at boot (the area flag is cached; no rule loads terrain later, #684) |
| Purposes a template serves | creature/game-object templates with every purpose | the reduced set the plan keeps on this core: **grind, town (GenericRpg), vendor, repair**. A vendor needs a non-empty stock list; invisible marker NPCs (`CREATURE_FLAG_EXTRA_TRIGGER`) are skipped; no trainer, AH, mail, boss or gather destination until their slices exist |
| Area level | `ai_playerbot_zone_level`, then the DBC `AreaLevel`, then a sub-area average, then the parent zone | the same table row, then **the parent zone**, then `AreaTableEntry::ExplorationLevel`, else `-1`. No sub-area average, no terrain. `ZoneLevelPolicy` is the rule the generation boot (W4-B) feeds: median `creature_classic_level` of hostile, non-critter, non-elite spawns, five samples minimum, lower median |
| Creature level/rank/gold/xp, quest level/type/items, area id/team/explore bit, skill maxima, map id | the cmangos fields | helpers in the pair (`CreatureLevelMaxWL4` = `creature_classic_level`, `CreatureRankWL4` = the classification enum with the same numbers, `QuestMinLevelWL4` = `GetClassicMinLevel`, `QuestReqItemIdWL4` = the objective rows, `AreaExploredWL4` = `Player::HasExploredZone`, ...) |
| Taxi zones | node store + terrain zones | none: flights are W5, the cache is empty |
| Fish locations | `ai_playerbot_named_location` rows | none: fishing is W7; the loader and saver return before their bodies |
| Travel graph | node store, generated links and paths | none yet: `TravelGraphLoadCompat()` says so once; **W4-A1 wires its node store in there** (the call site is `TravelMgr::LoadQuestTravelTable`) |

## 3. The cut (each one makes the bot do LESS)

| Legacy (Turtle) | ForeverCore |
|---|---|
| Quest needs by name, trainer needs by type, city/pvp/guild meeting/petition/mount purposes | `ShouldTravelNamedValue` answers false: those slices (W2/W3/W8/W9) are not merged |
| Vendor/repair/quest purposes | refused by `TravelPurposeGatePolicy` until W2's `sell`/`repair` actions plus the `maintenance` strategy, or W3's `rpg quest` strategy, are in the generated registry |
| Grind/town purposes | allowed; the legacy phase clock (`rpgPhase`) and the travel-refresh guard are kept |
| Outgrown-zone trigger with quest hand-in yields, targetless latch, grind dry-streak | zone level against the bot's level only (level 10+, pool bots); the yields come with W3/W0 |
| Run logs, fish, taxi zones, terrain trim, the travel graph | not opened, not built, not loaded on this core |

The four value bodies (`entry guidps`, `entry travel purpose`, `need travel purpose`, `should travel named`, `should leave outgrown
zone`, `quest stage active`) moved verbatim into `ai/cmangos-compat-wl4.h`, because their Turtle answers read services this core
does not build (guild orders, alt/hire lifecycle, trainer values) and the shared file must never compile those references into the
ForeverCore build. `TravelValues.cpp` keeps one call per value; the ForeverCore answers are the reduced ones above.

## 4. Shared lines touched (other rounds work on the same tree)

| File | Change | Owner note |
|---|---|---|
| `forever/SLICE-W1.txt` | three appended lines (marker comment + the two files) | the build list; round A1 appends its node graph the same way |
| `forever/SLICE-W4.txt` | created: SLICE-W1 plus the two files | round A1 appends `ai/playerbot/TravelNode.cpp` |
| `CMakeLists.txt` | the OFF-build filter names `TbCompat_WL4` | the out-of-line pair includes the bot brain |
| `forever/src/slice/SliceLinkStubs.cpp` | the Travel block keeps only the **node graph** stand-ins; the `TravelMgr`/`TravelTarget`/destination stand-ins are gone | round A1 deletes the node-graph lines when `TravelNode.cpp` lands |
| `forever/tools/slice_gen_context.py` | untouched (`WL4_KEYS` belongs to W4-C/D) | the pair reads registry keys without registering any |
| `ai/playerbot/TbCompatWL4.h` | new wrapper | A1's pair is `TbCompat_WL4n.h` per the plan |
| `forever/tools/test_wl4_wiring.py` | created with this round's classes | round A1 appends its classes |

## 5. Gates

Filled in below as they run.
