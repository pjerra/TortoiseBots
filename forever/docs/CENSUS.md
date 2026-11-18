# Port census

`forever/tools/port_census.py` compiles every `.cpp` of the bot brain syntax-only against ForeverCore, with
`forever/src/compat/botpch_forever.h` in front (as `ai/botpch.h` is on the Turtle core), and counts distinct error
places. A place is one file, line and message; a mistake in a header counts once however many files include it.
The compiler stops looking in a function after some errors, so the number is a floor, not the whole distance.

Run: `forever/tools/vm-census.sh "" 14` (all, about 45 minutes) or with a path part for a subset.

| Date | Core pin | State | Files | Compile clean | Error places |
|---|---|---|---|---|---|
| 2026-11-17 | 6517af05 | no pre-include, include paths only | 648 | 8 | 30,167 |
| 2026-11-17 | 6517af05 | pre-include + first shim, `ai/playerbot/strategy/values` only | 104 | 0 | 2,717 |
| 2026-11-17 | 6517af05 | pre-include + first shim (7808b02ae): the real baseline | 648 | 0 | 17,530 |
| 2026-11-17 | 6517af05 | shim round 1 (a7a640591), no bot code changed | 648 | 11 | 13,496 |
| 2026-11-17 | 6517af05 | pre-include + first shim, all files (BEFORE shim round 1) | 648 | 0 | 17,530 |
| 2026-11-17 | 6517af05 | shim round 1 (AFTER) | 648 | 11 | 13,496 |
| 2026-11-17 | 6517af05 | shim round 2 (BEFORE, = round 1 AFTER at 233e35a0c) | 648 | 11 | 13,496 |
| 2026-11-17 | 6517af05 | shim round 2 (AFTER, item prototype mirror, 570d22dd4) | 648 | 11 | 12,182 |
| 2026-11-18 | 6517af05 | shim round 2 fix round 1 (review: block/holy/bag family/one fill; entry + IsMoving helpers, b7d20355a) | 648 | 11 | 11,938 |
| 2026-11-18 | 6517af05 | shim round 3 (BEFORE, = round 2 fix 2) | 648 | 11 | 11,938 |
| 2026-11-18 | 6517af05 | shim round 3 (AFTER, spell mirror + fetch helpers, 6cbc923cd) | 648 | 11 | 11,065 |

The first row is not comparable with later ones: without the pre-include most files stopped early.

What the first run named most (distinct places): the cmangos singleton spelling (`MaNGOS::Singleton<>::Instance()`,
`sObjectMgr.`), `Player` / `Item` / `Unit` members with other names (`GetObjectGuid`, `GetGUIDLow`, `GetProto`,
`getPositionX`), `ItemPrototype` fields (`Class`, `SubClass`, `Quality`, `ItemId`), `SpellEntry` fields (`Effect[i]`),
`WorldPosition`'s `x` / `y` / `z` / `mapId` (it derives from the core's `WorldLocation`, whose fields are named
differently), update fields (`GetUInt32Value`), `WorldTimer`, `sLog.outString`.

Shim round 1 notes, the table of mapped names and the "cannot shim" groups: `SHIM-R1-NOTES.md`. Largest groups left:
item template fields (1,168 places), then spell data, position members, database calls, update fields, `GetProto`.

Shim round 3 (the spell mirror) notes: `SHIM-R3-NOTES.md`. `SpellEntry` is our own struct on ForeverCore, filled per spell
from `SpellInfo` on first use; every spell fetch reads `GetSpellEntryCompat` (32 `sSpellMgr.GetSpellEntry` and 54
`sSpellTemplate.LookupEntry` sites, 125 `X->m_spellInfo` and 32 `X->GetSpellProto()` wraps, 4 by hand). The spell-data
families are gone from the name table: `Effect` 173 -> 0, `EffectApplyAuraName` 102 -> 0, `MAX_EFFECT_INDEX` 64 -> 0,
`EffectTriggerSpell` 58 -> 0, `EffectMiscValue` 42 -> 0, `EffectItemType` 37 -> 0, `EffectBasePoints` 35 -> 0,
`SpellEffectIndex` 28 -> 0, `IsPassiveSpell` 21 -> 0, `GetSpellCastTime` 18 -> 0, `manaCost` 11 -> 0, `rangeIndex` 9 -> 0;
what stays is `SpellIconID` (3) and `Custom` (2), both left out for want of a counterpart. Census 11,938 -> 11,065.

Shim round 2 (the item prototype mirror) notes: `SHIM-R2-NOTES.md`. The lead's round-1 finding on
`IsMoving -> isMoving` is resolved there: the rename is removed (D6), and the bot's 59 `IsMoving()` calls now go through
`IsMovingCompat` (fix round B2). The review fix round 1 also corrected `Block` (the classic column, not the item stat),
kept holy out of "+all resistances", converted `BagFamily` from the core mask to the old number, filled the mirror once at
`OnWorldInitialized` (lock-free lookups afterwards) and moved the 180 entry lookups to `GetItemProtoByIdCompat`: census
12,182 -> 11,938 places, with `GetItemPrototype` and `IsMoving` gone from the name table.
