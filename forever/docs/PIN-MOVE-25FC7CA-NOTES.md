# Pin move 6517af05 -> 25fc7ca (0.1.38), 2026-11-19

Branch `pin/25fc7ca` off the port branch (slices 1-4). Commits: the pin (`forever/PINNED_CORE` only; no module code needed),
then the comment/doc refresh. Nothing outside `forever/` changed (`git diff --stat 55dff9543 HEAD -- . ':!forever'` is empty).
The impact analysis is `~/TortoiseBots-audit/FOREVER-PIN-MOVE-25fc7ca.md`; this file records what was proved and what the
merge still has to do. The core was read from a separate worktree (`git -C ~/ForeverCore worktree add ~/ForeverCore-25fc7ca
25fc7ca`, sparse like `~/ForeverCore`: no `sql/`, read it with `git show 25fc7ca:sql/...`). `~/ForeverCore` stays on 6517af0.

## 1. The row `(1807,389,15,1)`

It is not an access rule. It is the 343rd row of `sql/custom/world/2026_11_16_57_world_ahbot_market_prices_data.sql`, inserted into
the NEW table `auctionhousebot_market_price` (`ItemID, UnitPrice, Quantity, Samples`; created by `..._56_world_ahbot_market_price.sql`):
item 1807, 389 copper per unit, 15 seen on offer, 1 price sample. 389 is a price, not map 389. The table is read only when
`AuctionHouseBot.Vanilla.MarketPrices = 1` (default 0). The Ragefire Chasm entry rule is unchanged: the level 10 we measured comes from
DB2 (`block=level ... needs 10 (TransferAbortReason 8, MapDifficultyXCondition 4172, PlayerCondition 159263)`, `SLICE4-M-NOTES.md`),
and 25fc7ca touches neither `access_requirement`, `areatrigger_teleport`, `instance_template`, `map_difficulty*` nor PlayerCondition
159263 (the two hotfix files change `player_condition` rows 162926, 162929, 168945, 168967, 169022, 169029 only). No world SQL
row in the range names map 389 apart from that price row.

## 2. The call sites (read against the 25fc7ca source)

`PathGenerator::CalculatePath(src, dest)` keeps its signature and `bool` result; it is now a wrapper that clears the filter's excluded
polygons, calls the new `CalculateLiquidAwarePath` (the old body), and only when a `ClosedDoorCheck` is set (`PathGenerator.cpp:167-200`)
looks for closed doors. The 4-argument form (`PathGenerator.h:104`, `.cpp:317`) calls the 7-argument one. `SetClosedDoorCheck` and
`SetLiquidRouteCheck` are called by nothing in the core or in the module (grep), so both checks are null. `SetIgnoreLiquidRouteCheck` is
set only by the core's Charge code and `.mmap`.

| Site | Verdict |
|---|---|
| `TbCompat_Unit.h` `PathCalculateCompat(path, x, y, z, forceDest)` (`path.CalculatePath(x, y, z, forceDest)`, line 582) | same meaning: both checks null, so the call is `CalculatePathImpl`. One answer changes: for a swimmer (`CanSwim`) whose destination is under the water, `BuildPointPath` adds a dive leg or replaces a bent/incomplete route by a straight swim (`PathGenerator.cpp` ~914-1050), so the type can be NORMAL where it was INCOMPLETE. Land destinations: unchanged. Comment added at `PathIsCompleteCompat`. |
| `TbCompat_Unit.h` `PathCalculateCompat(path, start, dest, forceDest)` (line 588) | same as above (the 7-argument form) |
| `PathLengthCompat` / `PathIsCompleteCompat` / `PathTypeCompat` / `PathActualEndCompat` readers | same members, same meaning; `GetPathLength` moved to `.cpp:1702` (cite fixed) |
| `TbTestKit.cpp:250` (`forever/src/host/`, not `forever/src/`) `bot->SwapItem(...)` | signature untouched. `Player::EquipItem` now calls `resetAttackTimer(attType)` for a weapon put in a hand (`Player.cpp:12584-12592`): a weapon equipped by the test kit starts that hand's swing from zero. The kit is a setup tool, used out of combat; no gate depends on the first swing's time. Any equip in combat now delays the next swing by one weapon speed: that is the core's vanilla rule, not ours to compensate. |
| `Commander.cpp` and other `PathCalculateCompat` users (Turtle tree, not ForeverCore ports) | go through the same two wrappers |

The ~10 `IsValidAttackTarget` readers: signature unchanged; the core only made `SelectNearbyTarget` drop invalid targets.

## 3. PORT-PLAN rule 3 at 25fc7ca

`Main.cpp`, `World.cpp`, `ObjectMgr.cpp`, `SpellMgr.cpp`, `SpellInfo.*` and `cs_reload.cpp` are not in the 6517af0..25fc7ca diff, so every
line number in rule 3 is unchanged (`World.cpp:1471-1500` spell passes, `:1608` `LoadItemTemplates`, `Main.cpp:369/372` the world settings
then `OnWorldInitialized`). The new text in `PORT-PLAN.md` lists them with the full set of `LoadClassic*` passes, which also run once.
A grep of `src/server` for each loader finds no second caller. No reload path for either store.

## 4. Cites

`forever/tools/check_core_cites.py --old 6517af0 --new 25fc7ca` (reads the core through `git show`) listed 194 cites into the 50 changed core
files: 117 unchanged, about 75 moved (all refreshed; Turtle `T ...` cites on the same lines are skipped by their marker), 0 whose line was edited,
1 ambiguous (`PathGenerator.cpp`: there is also `src/tools/mmaps_generator/PathGenerator.cpp`; fixed by hand to `:1702`). Two bare follow-on
cites (`:20284`) were fixed by hand. Do not run `--apply` twice: it moves a line by the diff each time. To check a tree after a refresh,
run it without `--apply` with the PREVIOUS pin as `--old` only against an unrefreshed copy.

## 5. Gates (slot 3, core 25fc7ca)

Core: `~/ForeverCore-25fc7ca` (a git worktree of `~/ForeverCore` at 25fc7ca3a68b, sparse, no `sql/`), synced to the VM's `src-3` only, with the same
rsync `vm-census.sh` makes (`FOREVER_CORE=~/ForeverCore-25fc7ca FOREVER_SLOT=3`; `build.sh` reads `src-3`). Slots 0/1/2 and `~/ForeverCore`
(still 6517af05) were not touched. Code tree: the second commit's sources (this notes file is the only later change).

| Gate | Result |
|---|---|
| zero core change | `git -C ~/ForeverCore-25fc7ca status --short` and `diff --stat` empty; `rsync -anci --delete` of the core to `src-3` lists 0 lines |
| module build, option OFF (`FOREVER_SLOT=3 build.sh 12 tortoisebots`) | `CMAKE_RC=0 MAKE_RC=0 TOTAL_SECONDS=53`, `ar t libtortoisebots.a` = 8 objects, 0 warnings |
| worldserver, option ON (`FOREVER_CMAKE_EXTRA=-DTORTOISEBOTS_SLICE1=ON build.sh 12 worldserver`, full rebuild, 1647 steps) | `CMAKE_RC=0 MAKE_RC=0 TOTAL_SECONDS=1381`, `bin/Release/bin/worldserver` linked (62 MB), `grep -c undefined make.log` = 0, 0 `error:` |
| census SLICE4 / SLICE3 / SLICE2 / SLICE1 | `files 174 / 170 / 160 / 133  compile clean = files  with errors 0  error places 0  includes of a header ForeverCore does not have 0` |
| `check_worldlocation_scope.py` (SLICE4 list) | `checked 174 files, 0 could not be preprocessed, 0 core headers under the macro` |
| `slice_gen_context.py --check`, `slice_registry_check.py`, `test_slice4_d4_wiring.py` | `up to date`; `(a)=0  (b)=215`; `Ran 10 tests ... OK` |
| `gen_compat_headers.py <new core>` | writes the same 67 forwarding headers as with the old core and the same `UNKNOWN`/`SAME FILE NAME` notes: no diff in `forever/compat/` from the pin. (It also writes an untracked `Chat/Channel.h`, with the old core too: not a pin effect, not committed.) |
| policy tests (`policy-tests.sh` copy on `tb-3`/`pt-3`) | `472 PASS, 4 COMPILE_FAIL, 2 RUN_FAIL` = baseline |
| Turtle | no file outside `forever/` changed (`git diff --stat 55dff9543 HEAD -- . ':!forever'` empty): no Turtle build or test needed |

## 6. Live-boot checklist for the merge (not run in this package)

Run it on a build/test box that has the merged tree and the new core (the build VM's `~/forever-run` once its other worker is done).

1. Build: core at 25fc7ca (a separate checkout, never the pinned one other workers read) in `src`, module linked in at
   `src/src/modules/TortoiseBots`; `FOREVER_CMAKE_EXTRA=-DTORTOISEBOTS_SLICE1=ON ./build.sh 12 "worldserver bnetserver"`. Full rebuild (class layouts grew).
2. Before the restart, back up the four databases (the world gains ~7.5k rows of inserts and one new table; characters: nothing).
3. SQL to be applied by the core's DB updater at start (all `sql/custom`, nothing by hand): auth `2026_11_17_01_auth_command_bug`,
   `2026_11_17_70_auth_classic_70959` (the `build_info` row for 70959); hotfixes `2026_11_17_75_..._70959`, `2026_11_17_76_..._70959_b`;
   world about 38 files (`git -C <core> diff --name-only 6517af0..25fc7ca -- sql/custom/world`: the ahbot table `2026_11_16_56` and its data `_57`, the
   `2026_11_17_*` batch incl. Scourge Invasion `_40`/`_41`/`_42`, `2026_11_18_00`/`_17`/`_96`/`_97`, the sniff/Questie/Everlake data files). Check after the boot:
   `SELECT name FROM auth.updates WHERE name LIKE '2026_11_17%'` lists both auth files, same for `hotfixes.updates` and `world.updates`;
   `SELECT build FROM auth.build_info WHERE build = 70959` has one row; no `updates` hash error in `logs/update.out`; no `ASSERTION FAILED`.
4. Restart order: stop worldserver and bnetserver; run `world.sh update`; start bnetserver then worldserver. Both read `build_info` only at start,
   so both must be restarted once after the auth update. `Realm.AlsoAcceptBuilds` keeps 70959 and `Network.SkipBuildAuthKeyCheck = 1`
   stays; the realm stays on gamebuild 70707. Conf files: a drop-in without a `[section]` header now takes effect (Config.cpp fix): check
   our `conf.d` drop-ins have the headers we mean (`grep -L '^\[' <run>/etc/*.conf <run>/etc/worldserver.conf.d/*`, nothing should print) and that nothing new switches on
   (`Classic.ScourgeInvasion` and `AuctionHouseBot.Vanilla.MarketPrices` default 0).
5. Boot gate: log shows `ready...`, the item and spell mirror lines, `tb unported` unchanged against the last boot; `world.sh cmd "tb item <entry>"` answers.
6. Slice-4 regression set (RFC, characters of `SLICE4-M-NOTES.md` section 3: Mtank, Mpriest, Mdpsa-c, commander Mcmd; setup `tb login` x6, `tb engine <bot> on`
   x5, `tb party ...`, `tb master party Mtank Mcmd`, `tb role Mtank tank`, `tb gm Mcmd on`, `tb tele` to `1 1811.78 -4410.5 -18.47`), at level 18:
   a. entry through the portal: `tb enter party Mtank trigger 2230`: all five on map 389, one instance id, at least four `entered=trigger`;
   b. a pull: `tb order Mtank pull spawn:20038042`: combat over, no death (pathing, melee reach and `SelectNearbyTarget` changed in the core);
   c. a resurrection: `tb kill Mdpsa` out of combat: `awaiting-res`, the priest's Resurrection, alive within 45 s;
   d. a wipe: `tb kill party Mtank`: all five alive at the entrance, `whole=yes`, within 5 minutes (compare 8 s last time);
   e. the level-entry rule: a level-9 bot `tb enter` stays outside with `block=level ... needs 10`. Row `(1807,389,15,1)` is an auction price and
      does not change it, so the expected answer is the old one (10); a different number means something else changed: stop and report;
   f. `.mmap chargepath` smoke (new command) and one underwater destination path (the dive change) are optional.
   30 minutes with five engines: no crash, `grep -c 'ASSERT\|ABORT\|Segmentation'` = 0.
7. Turtle side: untouched by this branch; no Turtle gate needed.

## 7. Updater manifest entry for the new core (describe only; the data repository is not edited here)

In `~/forever-server-data-repo/manifest.json` add an entry to `entries`, not yet `current`, copied from the current `2026-11-19-70959` one:

```
id        "2026-11-19-70959-25fc7ca"            (any unused id)
date      the merge day
core      { repo "Karrz1/TrinityCore", branch "forever", commit "25fc7ca3a68b2f51991c2ce3f184d3c0f819277e" }
module    { repo "TortoiseBots/TortoiseBotsForever", branch "port/forever", commit <the 40-char sha of the merge of pin/25fc7ca> }
accepted_builds [70707, 70938, 70959], realm_gamebuild 70707, also_accept_builds [70959]     (unchanged)
data      unchanged (data-1.60.1.70959, forever-server-data-1.60.1.70959.tar.zst, sha256 b0d6cefb...05b6, client_build 70959): no extractor,
          DB2, map, vmap or mmap version constant changed in the range; the existing mmaps stopgap stays
server    { tag "server-25fc7ca3-<module8>", file "server-25fc7ca3-<module8>.tar.zst", sha256, runner_image_id }  made by
          `tools/forever-package pack --core 25fc7ca3a68b2f51991c2ce3f184d3c0f819277e --module <40-char> --sql-dir <core>/sql`, then `publish`
          (it fills core, module and server); `pack` needs the full-core SQL folder: use a full checkout of 25fc7ca (the sparse one has no `sql/`)
notes     "Core 25fc7ca (0.1.38: spells, talents, Scourge Invasion off by default, AH bot prices, pathing, client 70959 hotfixes) with the module at <module8>"
```
then `tools/manifest-check`, commit on a branch, and `forever-update check` on a test box before `apply --yes`. The data release is not rebuilt.
Rollback is the updater's own (`.prev` folders and the database dump it takes).
