# C. W6a: item usage and equip upgrades

**Can start now from `main`** (item loot L9 is merged). Seam with our W1 (`world/w1`): the "slice 9" block of
`forever/src/slice/SliceLinkStubs.cpp` and the keys in `forever/tools/slice_gen_context.py`. W1 owns them first; rebase
when W1 lands.

## Done looks like

`ItemUsageValue` scores items with real stat weights on ForeverCore's classic item stats, and a free bot puts on the better item.

**Live acceptance** (W6a plan, 2.6): four level-10 then 15 bots (warrior, priest, mage, rogue, roles by `.tb role`):
- a: boot prints "Loaded N weightscale class specs"; `.tb statcensus` output filed;
- b: `.tb itemscan` of a 50-item hand table (20 quest rewards, 30 drops, levels 5-20, ids read from the world DB, never from
  memory) matches the table, zero `EQUIP` where `canequip=no`;
- c: `.tb additem <warrior> <better item>` is equipped within one tick (push poll), old item in the bags; without a push
  within 300 s (audit);
- d: a spell-damage item scores for the mage, not the warrior; a healing item scores for a holy priest above a same-level
  int item;
- e: a protection warrior keeps its shield; nothing equipped and removed more than once in 30 min;
- f: with W1, a free bot equips a looted upgrade unasked; a commanded run party never auto-equips.

## Files (W6a plan, 2.2)

| Pkg | Files |
|---|---|
| S6 split (Turtle side, **pure move**) | `ai/playerbot/RandomItemMgr.cpp` -> new `RandomItemWeights.cpp`: weight-scale load (from `BuildItemInfoCache` :848-897 as `LoadWeightScales()`), equip rules :329-829, `CalculateStatWeight` and enchant/property weights :1452-2200, spec ids :3048-3213 (~1,480 lines, ~25 census places) |
| I6 weights | `RandomItemWeights.cpp`; new pure `policy/item/ClassicItemStatPolicy.h` + its test `test_classic_item_stat_policy.cpp` beside the other policy tests |
| E6 equip | `strategy/actions/EquipAction.cpp` (925 lines, 12 places); `strategy/generic/MaintenanceStrategy.cpp` registered for its `equipment audit` row only |
| compat | `forever/src/compat/TbCompat_W6.h` + `ai/cmangos-compat-w6.h`: `AutoEquipCompat` (typed `AutoEquipItem` into `HandleAutoEquipItemOpcode`), `ItemDurabilityCompat`, `WeightScaleQueryCompat` |
| D6 data | `forever/sql/world/<date>_tb_weightscales.sql` |
| H6 host | `.tb itemscan` gains `usage= w= worn= wornw=`; `.tb state` gains `spec=` and `gear=<slot>:<item>,...`; new `.tb statcensus` |

Delete the SliceLinkStubs "slice 9" stand-ins for `ItemStatWeight`, `ShouldEquip*`, `GetFallbackSpecId`, `GetSpecName`,
`GetSpecStatFamily`, `GetPlayerSpecId`. `Init` stays a stand-in calling only `LoadWeightScales()`; `GetStatWeight` and
`GetLiveStatWeight` stay 0 (less: `IsFutureUpgrade` finds nothing). Paths are under `ai/playerbot/` unless they start with `forever/`.

**Keys:** register `equip upgrades` (action), `equipment audit`, `item push result` (triggers), `maintenance` (strategy; its
other rows name unregistered keys, skipped and logged once). Leave out `loot roll won`, `pool gear top up`,
`query item usage`, the `equip` chat command.

## Classic stats on ForeverCore (W6a plan, 2.3)

ForeverCore turned vanilla "Equip:" spells into item stats: spell damage by school 83-89, skills 90-118, penetration
119-123, all resistances 124, AP and spell damage vs creature types 125-139 (`ItemTemplate.h:109-121`); healing and spell
damage are `ITEM_MOD_SPELL_HEALING_DONE`/`_DAMAGE_DONE` 41/42 (`Player.cpp:8874-8876`); hit, crit, defense, AP, mp5 use the
retail ids (`Player.cpp:8626-8791`). The mirror passes the stat ids through (`forever/src/compat/TbItemPrototype.cpp:259-260`) and the
effects as `Spells[]` (`:318-336`), but `CalculateStatWeight` maps only five base stats by id (`RandomItemMgr.cpp:98-102`)
and reads the rest from on-equip auras (`:1610-1820`). `ClassicItemStatPolicy.h` maps each ForeverCore id to the legacy
weight name (`splpwr`, `<school>splpwr`, `splheal`, `atkpwr`, `hitrtng`, `critstrkrtng`, `defrtng`, `dodgertng`,
`parryrtng`, `blockrtng`, `block`, `manargn`, `spellpenrtng`, ...); an unknown id scores 0; items that still carry an
on-equip spell keep the aura path.

## ForeverCore APIs (W6a plan, 2.4)

`ItemTemplate::GetStatModifierBonusStat` `ItemTemplate.h:893`; `Item::GetItemStatValue` `Item.cpp:2426`; classic stat
application `Player.cpp:8620-8930`; `Player::CanEquipItem` `:11757`; `SwapItem` `:13640`; `HandleAutoEquipItemOpcode`
`ItemHandler.cpp:194-213` (INPLACE opcode); `SendNewItem` `Player.cpp:14954` (what the push poll stands in for);
`Item::m_itemData` durability for `BROKEN_EQUIP`. The push poll uses L9's `PollPacketEventCompat`
(`forever/src/compat/TbCompat_S9.h:119`): a per-bot bag generation; no packet parsing.

## UNPORTED rows

The `item usage` row was removed by L9 (`ItemUsageValue.cpp` is in the slice). This item removes the SliceLinkStubs
stand-ins above. Candidate: `equip slot of an item prototype` (`ai/playerbot/strategy/ItemVisitors.h`, `FindEquipSlot` on a prototype) if E6
needs it. Stays: `item random properties` (suffix items score base stats only), `incoming: loot vote and roll won` (no
auto-equip from rolls).

## Decisions already made (lead, 2026-11-20)

- D1 split `RandomItemMgr.cpp` on the Turtle side by a pure move (Turtle build + Turtle unit tests prove no change); W6a
  compiles only `RandomItemWeights.cpp`; the caches stay stand-ins for W6b (gear generation).
- D2 the stat map is one pure tested header; unknown ids score 0.
- D3 weights = our own `ai_playerbot_weightscales` + `_data`, the final state of the ten Turtle world updates
  (`forever/data/waterys/sql-inventory.csv`), one module SQL file; **not** the ForeverCore team's retail-era scorer weights.
- D4 auto-equip ON for `free` bots only (audit 300 s + push poll); commanded run parties keep "no auto-equip"; rolls stay greed-only.
- D5 only the equip classifications go live; QUEST/SKILL/VENDOR/AH/DISENCHANT usages stay stand-ins until their slice.

## Rules that bite here

- `WeightScaleQueryCompat` is a guarded reader (schema first): a missing table reads empty and every weight stays 0. A
  plain `WorldDatabase.Query` on a missing table aborts the server.
- S6 touches a file outside `forever/`: Turtle build and unit tests required, and no behaviour change on Turtle.

## Tests (failing case first)

- **Failing case:** a classic spell-damage item (stat 83-89 or 42) scores 0 for a mage today; must score > 0, and 0 for a warrior.
- `test_classic_item_stat_policy.cpp`: every mapped id, unknown id -> 0, healing vs int ordering for a holy priest.
- Mutation: drop the classic-id map -> the check fails.
- Wiring: auto-equip refused for a bot bound in a run party (D4).

## Unknowns

Run `.tb statcensus` once (which ids carry what), compare `.tb item` with tooltips for ten items, and check `.tb state spec=` per role at level 10-15 (spec tab may read 0 until slice 10's role tab feeds it).
