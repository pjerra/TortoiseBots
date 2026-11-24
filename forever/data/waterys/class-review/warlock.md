# Warlock — review against WoW Forever 70959

Builds reviewed: `forever/data/waterys/talent-builds.csv` against 70959 `Talent.db2`/`SpellName.db2`,
plus the warlock strategy files. Rank 2+ talent spells are absent from the client DB in every build
(known limitation); rank-1 gaps are listed separately.

## Builds

| Spec | Points | Status | Build issues |
|---|---:|---|---|
| Affliction | 51/51 | all talent IDs exist; no rank > max | 2 Turtle talents dropped (340/348), 2 overlaps, 5 position/2 row-custom |
| Demonology | **50/51** | **1 point short** | Turtle 364 dropped, 2 overlaps, 5 position/3 row-custom; no tier violation |
| Destruction | 51/51 | all talent IDs exist; no rank > max | Turtle 323 dropped, 1 overlap; no tier violation |

No missing talent IDs, no cumulative rank > max, and no 5-points-per-tier violation.

### Client SpellName gaps

- Affliction talent **1022** rank-1 spell **18220** and Demonology talent **1241** rank-1 spell **18748**
  are absent from the 70959 client spell DB; verify live.

## Rotation / spells

- Pets/demons: **Summon Imp 688**, **Voidwalker 697**, **Succubus 712**, **Felhunter 691** all exist;
  the four-demon mapping is intact.
- Curses/Corruption/Immolate/Shadow Bolt/Drain/Soul Shard spells are present in the class notes' mapping.
- **Dark Harvest** has no matching 70959 spell by name/ID; `CastDarkHarvestAction` and its trigger are
  dead (`WarlockActions.h:91`, `WarlockTriggers.h:564`).
- **Power Overwhelming** likewise has no matching spell (only a `[DNT]` name exists); the action/trigger
  are dead (`WarlockActions.h:97`, `WarlockTriggers.h:578`).

## Dead conditions and custom differences

- Dead actions: `dark harvest`, `dark harvest channel check`, `power overwhelming`. Their trigger rows
  remain in the Affliction/Demonology strategies, so those rotations lose a button on Forever.
- Turtle custom talents with no exact counterpart: Affliction 340/348, Demonology 364, Destruction 323.
  The build drops/tops them up; the position/row-custom mappings (1004, 1082, 1222, 1225, 1262, 1281,
  1283, 1244, 986) are not exact spell mappings and need a live check.

## Rotation spell check (70959 `SpellName`)

| Spec | Spell | Known |
|---|---|---|
| All | Shadow Bolt, Corruption, Curse of Agony, Immolate, Drain Life, Searing Pain | yes |
| All | Summon Imp 688, Voidwalker 697, Succubus 712, Felhunter 691 | yes |
| Affliction | Dark Harvest / Dark Harvest channel check | no exact spell (action/trigger dead) |
| Demonology | Power Overwhelming | no exact spell (action/trigger dead) |
