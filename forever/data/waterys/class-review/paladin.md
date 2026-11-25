# Paladin — review against WoW Forever 70959

Builds reviewed: `forever/data/waterys/talent-builds.csv` against 70959 `Talent.db2`/`SpellName.db2`,
plus the paladin strategy files. Rank spells beyond rank 1 are absent from the client DB in every build
(known limitation); rank-1 gaps are listed separately.

## Builds

| Spec | Points | Status | Build issues |
|---|---:|---|---|
| Holy | **40/51** | **11 points short; one 5-point-tier violation** | `1502` is tier 6 (needs 30 Holy points) but the build has only 28; Turtle 446/456 custom points dropped, 3 overlaps |
| Protection | 51/51 | all talent IDs exist; no rank > max | 4 position-custom, 1 row-custom, 2 overlaps; no tier violation |
| Retribution | **45/51** | **6 points short** | 2 overlaps, 2 Turtle points dropped; no tier violation |

No paladin build talent ID is missing from 70959 and no cumulative rank exceeds its max rank. The Holy
build's final distribution breaks the 5-points-per-tier rule for talent 1502; it also cannot be a legal
51-point build as currently listed. Reallocate the 11 missing Holy points (and the 2 points needed
before 1502) into lower-tier Holy talents; the CSV's `TOP-UP` rows are not enough.

### Client SpellName gaps

- Holy talent **1443** rank 1 spell **20234** and Protection talent **1431** rank 1 spell **20911** are
  absent from the 70959 client spell DB; verify with live `GetSpellInfo`.

## Rotation / spells

| Spell / action | 70959 | Effect |
|---|---|---|
| Holy Strike 678 | **exists** | Forever Retribution strike; use it (class note) |
| Crusader Strike | **near-name spells exist** (2537/8823/8829/409914 etc.) | TB exact action name may resolve; verify in game |
| Crusader Aura | **absent by name** | action/trigger `CrusaderAuraTrigger` is dead |
| Bulwark of the Righteous (51346/51565) | **absent** | `CastBulwarkAction`/trigger dead |
| Hand of Freedom | **absent by name** | buff action dead |
| Hand of Reckoning | **runecarving rune only**: enchant 6844 "Hand of Reckoning" applies 407774/409911, from Engrave Gloves 410001, itself from the Libram of Justice (item 208851); the taunt ability is 1219206 | no SkillLineAbility, trainer row, quest or preset grants it; bots do not engrave, so the taunt is dead and its rows are compiled out on Forever |
| Righteous Defense | **absent by name** | taunt action dead, row compiled out on Forever |
| Hand of Protection/Sacrifice | present under Classic names | verify exact names live |

## Dead conditions and custom differences

- Dead or name-sensitive: `crusader aura`, `bulwark of the righteous`, `hand of freedom`; the strategies still schedule them
  (`PaladinActions.h`/`PaladinAiObjectContext.cpp`).
- No paladin taunt on Forever: `righteous defense` is absent and `hand of reckoning` is only the runecarving glove rune
  above. Both protection/tank rows are behind the Forever guard in `ProtectionPaladinStrategy.cpp` and
  `TankPaladinStrategy.cpp`; on Forever the tank keeps a Judgement-only `lose aggro` row.
- Custom Forever: **Holy Strike 678** replaces the old Retribution strike; keep the changed behavior.
- Turtle talents with no exact counterpart: Holy 446, 456; Retribution/Protection custom points were
  dropped/top-upped. Position/row-custom mappings (1421, 1425, 1426, 1429, 1430, 1432, 1446, 1502, 1521,
  1627, 1629, 1631) are not exact spell matches and need the live check.

## Rotation spell check (70959 `SpellName`)

| Spec | Spell | Known |
|---|---|---|
| Holy/Protection | Holy Light, Flash of Light, Holy Shock, Consecration, Seal of Righteousness/Wisdom | yes |
| Retribution | Holy Strike 678 | yes — Forever strike |
| Retribution | Crusader Strike | yes under near-name IDs (2537/8823/409914); exact action name needs live check |
| All | Crusader Aura, Bulwark of the Righteous, Hand of Freedom, Righteous Defense | no exact name (dead action candidates) |
| Protection | Hand of Reckoning | only as a runecarving glove rune; no bot learn path (row compiled out) |
