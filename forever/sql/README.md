# forever/sql: world-database rows the module ships

The core's auto-updater (`DBUpdater<WorldDatabase>::Update`, DBUpdater.cpp:304) reads the directories listed in the world database's `updates_include` table (UpdateFetcher.cpp:98-118: `$` = the configured `SourceDirectory`,
a directory that does not exist is skipped with a warning, :107-114), takes every `.sql` file in them, recursively (:61-73, the `.sql` test at :73), and applies the files in FILE NAME order (names must be unique across all included directories, :80-88; a file is recorded in `updates` by its name and hash and applied again when its hash changes, :326-332; the fork runs each file over its own connection and checks a marker, DBUpdater.cpp:421-). The module's files are therefore delivered by ONE data row, no core change:

1. the test setup copies `forever/sql/world/` to `<SourceDirectory>/sql/tortoisebots/world/` (the test server has `SourceDirectory = "/sqlsrc"`, so `~/forever-run/sqlsrc/sql/tortoisebots/world/`);
2. once, `INSERT INTO updates_include (path, state) VALUES ('$/sql/tortoisebots/world', 'RELEASED');` in the `world` database (the existing rows are `$/sql/custom/world` and `$/sql/updates/world` RELEASED and the ARCHIVED `$/sql/old/*`, read on forever-test1);
3. restart the worldserver: the updater applies the files that are new or changed. A directory outside `/custom/` is not in the fork's "re-apply every later custom file" chain (UpdateFetcher.cpp:232).

Every file here is idempotent (`CREATE TABLE IF NOT EXISTS`, `INSERT IGNORE`), has a unique name (`YYYY_MM_DD_NN_<what>.sql`, the date and number put it after the core's own files of that day), and ends with a statement and a newline (the fork appends its marker statement to the file text). Nothing in this folder is applied by the build, by `setup.sh` or by any package: whoever sets up a test server does it, and says so in the notes.
The trainer file of slice 4 (below) lives in the same folder, so with the `updates_include` row it is applied by the updater too; the hand command further down still works on a server that has no such row.

| File | What | Rows |
|---|---|---|
| `world/2026_11_19_00_warrior_trainer_spells.sql` | the warrior trainers (27: the ones that already teach Bloodrage and Shield Block) get Defensive Stance 71, Taunt 355, Sunder Armor 7386 and Intercept 20252, which the data lacked (a level 18 tank could not learn them); from the data collaborator (`forever-warrior-trainer-fix.sql`, 2026-11-19) | 4 `trainer_spell` rows per trainer, 108 in all (27 trainers) |
| `world/2026_11_20_00_tortoisebots_plans.sql` | the seven dungeon plan tables, final form: `ai_dungeon_plan`, `ai_dungeon_leg`, `ai_dungeon_step`, `ai_dungeon_leg_need`, `ai_dungeon_boss`, `ai_boss_tactic` (as the Turtle migrations leave them) and the new ForeverCore-only `ai_dungeon_step_unit` (package H5 of slice S5) | 0 (tables only) |
| `world/2026_11_20_01_tortoisebots_dungeon_bosses_rfc.sql` | the four Ragefire Chasm bosses (map 389), copied from the Turtle migration 20261021114931 | 4 `ai_dungeon_boss` rows |


## Applying one file BY HAND on the build VM's test server (a server without the `updates_include` row) (`~/forever-run`, containers `forever-db`)

```
docker exec -i forever-db mysql -utrinity -p"$(cat ~/forever-run/.db_pass)" world < forever/sql/world/2026_11_19_00_warrior_trainer_spells.sql
```

(Run on the build VM, from the module folder. The user `trinity` and the password in `~/forever-run/.db_pass` are the ones `~/forever-run/setup.sh` writes into `WorldDatabaseInfo` (`forever-db;3306;trinity;<password>;world`): use the file, never type or print the password. The command was not run: no server was touched in this package.)
Then restart the worldserver (the trainer table is read at start), and check one trainer: `.tb learn <warrior bot> 355` after `.tb train <bot>` at level 10 or more.
The file names the database in every statement (`world`.`trainer_spell`), so it can also be piped into any client already connected to the server's MySQL.

## Not here yet
The plan ROWS (`ai_dungeon_plan`, `_leg`, `_step`, `_step_unit` for `ragefire_chasm`) are the plan exporter's output (`turtle-agent plan-export`, package A5 adds `--forever` for the unit rows): the file `world/<date>_tortoisebots_plan_ragefire_chasm.sql` is produced by it and dropped in this folder by the merge package. No row is set by hand.
