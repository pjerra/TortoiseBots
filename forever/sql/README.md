# forever/sql: world-database rows the module ships

The module has no world SQL that a server applies by itself: the core's auto-updater reads only its own `sql/updates` folders (`Updates.EnableDatabases`, `SourceDirectory`),
not a module's. A file here is applied BY HAND, once, to the `world` database of a test server, and is written so that applying it twice changes nothing
(`INSERT IGNORE`). Nothing in this folder is applied by the build, by `setup.sh` or by any package: whoever sets up a test server does it, and says so in the notes.

| File | What | Rows |
|---|---|---|
| `world/2026_11_19_00_warrior_trainer_spells.sql` | the warrior trainers (27: the ones that already teach Bloodrage and Shield Block) get Defensive Stance 71, Taunt 355, Sunder Armor 7386 and Intercept 20252, which the data lacked (a level 18 tank could not learn them); from the data collaborator (`forever-warrior-trainer-fix.sql`, 2026-11-19) | 4 `trainer_spell` rows per trainer, 106 in all |

## Applying it on the build VM's test server (`~/forever-run`, containers `forever-db`)

```
docker exec -i forever-db sh -c 'mysql -utrinity -p"$MYSQL_PASSWORD" world' < forever/sql/world/2026_11_19_00_warrior_trainer_spells.sql
```

(The database user and password are the ones `~/forever-run/setup.sh` wrote into `worldserver.conf` as `WorldDatabaseInfo`; the password is in `~/forever-run/.db_pass`: use the file, never type or print it.)
Then restart the worldserver (the trainer table is read at start), and check one trainer: `.tb learn <warrior bot> 355` after `.tb train <bot>` at level 10 or more.
The file names the database in every statement (`world`.`trainer_spell`), so it can also be piped into any client already connected to the server's MySQL.
