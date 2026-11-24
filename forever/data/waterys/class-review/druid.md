# Druid — review against WoW Forever 70959

Builds reviewed: `forever/data/waterys/talent-builds.csv` (51-point TB builds) against 70959
`Talent.db2`/`SpellName.db2`, plus the class strategy files and the earlier class notes.
Client-build caveat: the mapping file was produced against 70888; all IDs/ranks below were re-read from
70959. Talent rank spells beyond rank 1 are absent from the client `Spell`/`SpellName` DB in every build
(the same caveat already recorded in `talents-level18/22.csv`); they need a live `GetSpellInfo` check.

## Builds

| Spec | Points | Status | Build issues |
|---|---:|---|---|
| Balance | 51/51 | all talent IDs exist; no rank > max | 4 overlap rows, 3 position-custom mappings; Turtle custom talents 314/318/319/320 are folded/dropped |
| Feral | 51/51 | all talent IDs exist; no rank > max | 1 Turtle talent dropped (276), 1 overlap, 5 position-custom; rank-1 client gaps below |
| Restoration | 50/51 | **1 point short** | Turtle 304/295 custom dropped, 2 overlaps, 4 position-custom; no 5-point-tier violation |

Talent-ID/rank verification: no missing talent and no `rank > max` in any druid build. No tier rule
violation in the final distribution.

### Client SpellName gaps (not proof the spell is absent server-side)

- Feral talent **800** rank 1 spell **16952** and talent **1162** rank 1 spell **16857** are absent from
  the 70959 client `Spell`/`SpellName` DB. Rank 2+ spells are absent for many talents as noted above.

## Rotation / spells

- Balance: the notes mark the core Balance rotation intact; no dead Balance spell IDs found in the
  build list.
- Feral/tank: **Savage Bite 45736** is Turbo/Turtle-only and absent from Forever; the action/trigger still
  reference it (`DruidTriggers.h:462`, `DruidActions.h:646`), so that condition can never fire.
- **Mangle (bear)** action (`DruidActions.h:667`) resolves by name; 70959 has `Mangle` 19820/409828 but
  not the exact `mangle (bear)` name. Verify live before treating it as dead.
- **Berserk 50334** (TB Feral Berserk) is absent; 70959 has other `Berserk` spells (e.g. 23397) but the
  action name may not resolve to them. Live check.
- Restoration: **Tree of Life form 45705** (Turtle) is absent; 70959 has `Tree of Life` 439733/439745
  under different IDs/names. The current action/aura name check may be dead; verify live.

## Dead conditions and custom differences

- `savage bite` trigger/action is dead on the current IDs.
- Turtle-only talents in `talents-detail.csv`: 314/318/319/320 (Balance), 276 (Feral), 304/295/297/300
  (Restoration) have no exact Forever counterpart; the build dropped/top-upped them. Re-check the
  position-custom mappings (307, 278, 280, 281, 297, 300) rather than treating them as exact spell maps.
- No build talent ID is missing from 70959; the failures are action-name/rank-data checks, not dead
  talent nodes.

## Rotation spell check (70959 `SpellName`)

| Spec | Spell | Known |
|---|---|---|
| Balance | Wrath, Starfire, Moonfire, Insect Swarm, Hurricane, Force of Nature | yes (core IDs/ranks exist) |
| Feral | Maul, Swipe, Claw, Mangle | yes (Mangle 19820/409828; exact `mangle (bear)` action name needs live check) |
| Feral | Berserk (TB 50334), Savage Bite (45736), Mangle (bear action) | exact TB IDs/names absent; near-name `Berserk` spells exist — verify live |
| Restoration | Rejuvenation, Regrowth, Healing Touch, Lifebloom | yes (Lifebloom 408124 etc.) |
| Restoration | Tree of Life form (45705) | exact action ID absent; 70959 has Tree of Life 439733/439745 under different names — verify live |
