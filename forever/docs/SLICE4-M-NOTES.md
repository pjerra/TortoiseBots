# Slice 4, package M4: merge, link, live test

Branch `slice4/integ` (worktree `~/wt-slice4-integ`), build slot 1, off `port/forever` (3b9138052, slices 1-3 and the wide pass). Times are the build VM's UTC. Core read only (pin 6517af05), zero core changes. Test server: the build VM's own, `~/forever-run`, driver `~/forever-build/m4-sess.sh` (m3's, binary `~/forever-build/m4-on/worldserver`); `forever-test1` never touched.

## 1. The merge
`git merge --no-ff tbf/slice4/p` then `tbf/slice4/h` (both local branches equal the remote ones: 11d24e55a and 3b1e59c84). **No conflicts** (H4 predates the wide pass; the two packages touch disjoint files). Needs done in one commit (f78702bf8):
* `CMakeLists.txt` reads `forever/SLICE4.txt` (176 lines, 174 files); comment updated.
* Entrance check: the stricter one in both places. P4's `InstanceEntranceKnownCompat` already required a real `world_safe_locs` row; H4's `BotMgr::ReleaseHasEntrance` (TbBotMgr.cpp) now requires `GetWorldSafeLoc(id) != nullptr` as well as `id != 0`, so the host's `relhold=no-entrance`, the order refusal, the lease answer and the engine's hold agree.
* H4's `relhold` is NOT switched to P4's `AutoReleaseSpiritAction::ReleaseHoldReason()`: the host key stays computed from core facts (no engine header in host code); the live test compares it with the engine's behaviour.
* Generated: `slice_gen_context.py --check` current (`registered 17/48/75/170`), registry check `(a)=0 (b)=215`.

## 2. Server setup (build VM test server `~/forever-run`, container `forever-db`; nothing on `forever-test1`)
Gates before the first boot (slot 1, commit f78702bf8): option-ON `worldserver` `CMAKE_RC=0 MAKE_RC=0 TOTAL_SECONDS=379`, 0 `undefined` in `make.log`; option-OFF `tortoisebots` `CMAKE_RC=0 MAKE_RC=0`, `ar t libtortoisebots.a` = 8 objects (`TbBotMgr TbBotSession TbScripts TbItemPrototype TbSpellEntry TbEngineBrain TbH3Off TbH4`); census SLICE4 `files 174 compile clean 174 error places 0`, SLICE3/2/1 `170/160/133` clean, 0 error places.
SQL, exactly what ran (on the VM, from the module folder `~/forever-build/src-1/src/modules/TortoiseBots`; the password comes from `~/forever-run/.db_pass`, never printed):
```
docker exec -i forever-db mysql -utrinity -p"$(cat ~/forever-run/.db_pass)" world < forever/sql/world/2026_11_19_00_warrior_trainer_spells.sql
```
Before: `SELECT COUNT(*) FROM trainer_spell WHERE SpellId IN (71,355,7386,20252)` = 0; the 27 warrior trainers (`COUNT(DISTINCT TrainerId) ... SpellId IN (2687,2565)`) = 27. After: 108 rows; per spell `SELECT SpellId, COUNT(DISTINCT TrainerId) ... GROUP BY SpellId` = 27 / 27 / 27 / 27 for 71 / 355 / 7386 / 20252, and 27 trainers have all four. The trainer table is read at start, so the server was booted after the SQL.
Normal proof: a new warrior `Mfreshb` (human, level 18 by `.tb level`), spells BEFORE `.tb train`: none of 71 / 355 / 7386 / 20252; `.tb train Mfreshb` ("learned 22 spell(s) from 34 class trainer(s) and 42 from the class skill lines"); AFTER: `71 Defensive Stance`, `355 Taunt`, `7386 Sunder Armor` known, 20252 Intercept not (needs Berserker Stance, as the SQL comment says). No `.tb learn` used. (`Mfresh` was the first try; its `.tb learn` afterwards answered `known` for all three.)
