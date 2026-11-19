# Slice 9, K9: level-22 party data (prot warrior, arms warrior, priest, mage, rogue)

Read-only SELECTs on forever-test1 (world DB, `trainer`/`trainer_spell`/`creature_trainer`/`creature_template`,
hotfixes `spell_name`). No SQL was applied. Files in `forever/data/slice9/`:

- `kits-level22.csv`: the level-18 gear kit, copied unchanged (spec 5a: gear stays the level-18 kit).
- `spells-19-22.csv`: every spell the class trainers teach at ReqLevel 19-22 (51 rows: warrior 9 (6 at 20, 3 at 22), priest 16, mage 18, rogue 8;
  trainers: Type-0 `trainer` rows of the NPCs whose subname is `Warrior|Priest|Mage|Rogue
  Trainer` (joined through `creature_trainer`). `req_spell` = `ReqAbility1`; `rank` is `1` only where no prerequisite exists.
  Costs in copper. Names: only 8 of 51 resolve from hotfixes `spell_name`; the rest say `not resolved` (SpellName.db2
  WDC5 was not parsed in this slice). IDs and costs are the server's.
- `talents-level22.csv`: 13 points per class (level 22). Points 1-9 = `talents-level18.csv` with the true cumulative rank
  (the level-18 file repeats rank 1 on every point; the spell per rank comes from `talents-detail.csv` `forever_spells`).
  Points 10-13 = four level 19-22 points.

## Decisions (not in the spec)

- Talent picks for points 10-13: rank-ups of talents the level-18 pick already holds (no new tier access, so the tier
  rule cannot be broken): prot warrior 1601 to 5 and 138 to 4; arms warrior 130 to 5 and 121 to 3; rogue 181 to 5 and 221
  to 3; priest 1181 to 5 (3 points); mage 61 to 3 (2 points). Every other talent is full at its max.
- **Open: priest 1 point, mage 2 points.** The tree tiers are not in the repo (`hotfixes.talent` is empty; `talents-detail.csv`
  has no tier column), so no new talent was picked. Rows say `OPEN`; the lead chooses.

## Ranks missing on the server (`trainer_spell`)

- Warrior 71 Defensive Stance, 355 Taunt, 7386 Sunder Armor: no rows, no `spell_learn_spell`, no creation spell, no quest
  reward. **Already covered** by `forever/sql/world/2026_11_19_00_warrior_trainer_spells.sql` (slice 4, not applied): no K9
  trainer SQL for these. 7405 Sunder Armor rank 2 (level 22, needs 7386) becomes learnable only after that file is applied.
- Priest 2652 (prerequisite of 19261, level 20) and 9035 (prerequisite of 19281, level 20): no trainer rows, no other learn
  path. The level and trainer are not in the repo, so **no SQL was written**. Lead decides whether the priest needs them.
- Every other `req_spell` of the 51 rows has trainer rows.
