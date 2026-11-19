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

## 3. Live test (build VM test server, Ragefire Chasm, 5 bots + commander)
Characters (account 1, level 18): `Mtank` (warrior, tank), `Mpriest`, `Mdpsa`..`Mdpsc` (warriors), commander `Mcmd` (priest, GM on, NOT in the group, console-driven). Setup per boot: `tb login` x6, `tb engine <bot> on` x5, `tb party Mtank Mpriest Mdpsa Mdpsb Mdpsc`, `tb master party Mtank Mcmd`, `tb role Mtank tank`, `tb gm Mcmd on`, then `tb tele Mcmd 1 1811.78 -4410.5 -18.47` and `tb tele party Mtank 1 ...` (the game_tele `RagefireChasm` spot, a few yards outside the trigger box; map 1 = Orgrimmar). Five boots (the binary changed between them, see section 4: boot 1 the plain merge, boot 2 with M4-1/M4-2, boot 3 with M4-3 (the idle test, the wipe variants), boot 4 with M4-4, boot 5 the final binary for the selftest and the id-recycling check); the server was never aborted: `grep -c 'ASSERT\|ABORT\|Segmentation'` on the log = 0 on every boot. Times are UTC of the VM.

| Gate | Result | Decisive lines |
|---|---|---|
| a entry | **PASS** | `tb enter party Mtank trigger 2230` at 12:24:34: `enter ... status=started inbox=no` x5; within 3 s `BotMgr: Mtank entered map 389 (instance 1) through area trigger 2230`, then Mdpsc, Mdpsb, Mdpsa, Mpriest, all `instance 1`, `entered=trigger`, `catchup=0`, at 0.80,-8.23 (the entrance), `ready=yes`. `got entrance world_safe_locs 1102230 (0.8, -8.2, -15.5)` logged once. Commander: `tb tele Mcmd to Mtank` -> `tele ... status=started map=389 inst=1`, `tb where Mcmd` = map 389 at 0.8 -8.2; `tb instance Mtank info` = `players=5 gms=1 inst=1 entrance=1102230`. No city guard reacted (no `incombat` in Orgrimmar in any of the 10 entries of the five boots; the spot is 6-18 yd from the box, the bots stood there 5-15 s). Direct entry was not needed. |
| b follow + ordered pull | **PASS** | commander `tb go Mcmd -20 -15`; 7 s later all five within 15 yd of it (`ready=yes`, `catchup=0`); `tb order Mtank pull spawn:20038042` at 12:25:31: tank in combat 12:25:33, the others 12:25:36-40 (after the pull landed: the pull hold), combat over 12:25:55 (24 s), 3 mobs dead (`mobs_alive 132 -> 129`), no death, lowest health 61 % (tank), 68 % (damage warrior). Later pulls: the same (2 mobs, no death). |
| c death -> resurrect | **PASS** | `tb kill Mdpsa` at 12:26:25 (out of combat): 12:26:26 `life=awaiting-res relhold=rezzer`; the priest's `Resurrection(2006)` CAST_START at 12:26:37.7 (castTime 10000 ms), `Mdpsa answers the resurrection offered by Mpriest: accepted`, `life=alive` at 12:26:48: **23 s from death to alive** (priest reaction 12 s + 10 s cast). `auto release` stayed `USELESS` while held. `life=dead` was never seen on this path: with a living priest the first state is already `awaiting-res`; `dead` shows only when nobody can resurrect (d, f). |
| d priest killed, others alive | **PASS (recorded)** | Out of combat: `tb kill Mpriest` 12:27:20: `life=dead relhold=wipe` (no living resurrecter among the party: the commander, also a priest, is not counted), it released and was `life=alive` at the entrance (0.80,-8.23, 52 % health) by 12:27:24, i.e. 4 s, then walked back (the entrance is 25 yd from the commander: `running-back` never showed, the rule starts at 30 yd). During a fight (second pull, 12:28:54): `life=awaiting-res relhold=combat` until combat ended at 12:29:03, then `relhold=wipe`, released, alive at the entrance 12:29:08. So: the body waits while a member fights, afterwards it comes back by itself; the driver's `.tb revive` is not needed. `.tb revive Mdpsb` (killed 12:28:05, `awaiting-res`): `revive ... status=revived practice=yes`, `PRACTICE: .tb revive raised Mdpsb in place` in the log, `practice=yes` until the next fight ended (`practice=no` after the second pull). |
| e release order | **PASS** | `tb kill Mdpsc` + `tb order Mdpsc release` (priest alive, so an order overrides the hold): alive at the entrance (-1.35,-15.14 after the first step, inst=1) in the same second, back within 20 yd of the commander at 12:29:40 (6 s), by walking (positions change 2-3 yd per sample). |
| f wipe | **PASS, two variants** | Boot 1, 12:30:28 `tb kill party Mtank`: `wipe=yes` at +1 s, all five alive at the entrance (`wipe=no whole=yes`) at 12:30:32 (4 s), `ready=yes` at 12:32:00 (92 s; the rest is resting: half health after the revive). Boot 3 (12:43:31): the priest came back 2 s before the others, so the four bodies waited for HER (`relhold=rezzer`): she raised them one by one as her mana allowed: Mtank 12:44:28, Mdpsc 12:45:09, Mdpsb 12:46:42, Mdpsa 12:47:34 (**4 min 3 s to all alive**), `ready=yes` 12:49:34 (6 min, priest mana). No outside help in either. A real wipe by a canyon fight (boot 2) was held by the stale-entrance bug (section 4, fixed). |
| g info, leave, reset, fresh instance | **PASS with a core fact** | `tb instance Mtank info`: `mobs_alive=121 mobs_total=132 mobs_loaded=130 boss=2732:alive;2733:alive;2734:alive;2735:alive entrance=1102230`. `tb instance Mtank leave` -> all six `status=started`, all on map 1 at the exit (1814.99,-4419.23) within 3 s; `tb instance Mtank reset` -> `reset map=389 inst=1 status=ok`; `tb enter party Mtank` -> a FRESH instance: `mobs_alive=132 mobs_total=132`, a new script (entrance 0 until the host sets it again). **Its id is 1 again, not a new number**: the core hands out the lowest free instance id once the reset instance is unloaded (`MapManager::GenerateInstanceId`, `_freeInstanceIds`), so "a new instance id" cannot be shown; the proof of freshness is the full creature count and the new script. |

## 4. Bugs found by the live test and fixed here (all host code, `forever/src/TbBotMgr.cpp`, `forever/src/host/TbH4.cpp`)
| # | Found | Cause | Fix |
|---|---|---|---|
| M4-1 | boot 1: right after the entry all five bots ran in a straight line through the dungeon (10,-31 -> 152,-377 in 40 s, 300+ yd from the commander; `stuck=blocked`, `dm=322`) | the entry job's walk to the trigger box is a `MovePoint` toward the box centre in MAP 1 coordinates; the generator survived the teleport and kept walking toward (1818,-4427) inside map 389 (the heading ratio 1 : -2.43 is exactly the direction from the origin to the box) | `UpdateEntry` stops the motion (`Clear()`, `StopMoving()`) before the core's handler runs and again at landing. Live afterwards: the bots stand at the entrance (0.80,-8.23), `ready=yes`, and follow the commander normally |
| M4-2 | boot 1: `.tb instance Mtank info` printed `boss=` (empty) | the scan looked for `CREATURE_FLAG_EXTRA_DUNGEON_BOSS` in the creature template, but the core strips that bit from the database value (`CREATURE_FLAG_EXTRA_DB_ALLOWED`, CreatureData.h:382), so no database creature ever has it | `boss` now comes from the instance script's boss table: `<DungeonEncounter id>:<alive|dead>` (`GetBossDungeonEncounter`, `GetBossState == DONE`): `2732..2735` for Ragefire Chasm. TB-COMMANDS.md and the selftest example say so |
| M4-3 | boot 2: after leave + reset + a second entry the party wiped in a canyon fight and every body stayed `life=awaiting-res relhold=no-entrance` for minutes (nobody released: the safe fallback, never a graveyard revive in Durotar) | `EnsureEntrance` ran once per instance ID; the core reuses the lowest free id after a reset instance unloads, so the fresh instance came back as `inst=1` with entrance 0 and the cache said "done" | `EnsureEntrance` forgets the cached entrance when the script no longer holds it (a new instance under an old id) and gives it again; `_entryTriggerByInstance` takes the trigger of THIS entry when the script has no entrance yet. Live: `got entrance world_safe_locs 1102230` logged again on the recycled id, `info` says `entrance=1102230`, a wipe recovers |
The fallback of M4-3 did what the findings file asks (the bot did LESS: it waited; `relhold=no-entrance` said why), and it is the evidence that the guard works.

### Gates h-j and the regression check (continued)
| Gate | Result | Decisive lines |
|---|---|---|
| h Ragefire Shaman flee at 15 % | **NOT CAPTURED (recorded, not a gate)** | Two canyon fights met Shamans (boot 2: the party walked into the trogg canyon behind the commander and wiped, `threat` showed `entry=11319 ... hppct=97 flee=no`; boot 3: an ordered pull of spawn 20048662 brought 7 mobs, 2 Shamans and 5 Troggs, by social aggro; the tank and priest died, the three damage warriors lived). The Shaman's hp never went below 85 % in a sample (the sampler had gaps of 10-20 s on a loaded VM), so `flee=yes` was not seen. What was seen: (1) the `threat` records carry `pos=` and `flee=no` as specified; (2) a revived tank + priest walked back from the entrance through the first room and pulled its pack on the way (U7: nobody leashes a revived bot, the engine's follow just walks the line to the commander); (3) the party cannot take a 7-mob canyon pull at level 18 with a priest at 7 % mana. A reviewer wanting h should pull a single Shaman (spawns 20038049, 20043580 in the cave west of the first room have 2-3 neighbours) with the priest rested. |
| i grammar | **PASS** | every new reply in this note and in TB-COMMANDS.md section 8 is real output; `.tb grammar selftest` output of the final binary is in section 7 (`grammar-end count=24 records=21 line_too_long=1`), the hand samples are gone. Keys seen live at the end of every state record: `life relhold resreq corpse corpsemap outside entered entry block blockmsg catchup practice` (`corpse`/`corpsemap` only on a dead or ghost bot); `state-end ... alive= dead= ghost= wipe= whole=`; threat `pos= flee=`. An open-world death was also seen by accident: a dead priest moved out by `leave` released at Razor Hill: `life=ghost corpse=1814.99,-4419.23,-18.82 corpsemap=1`, `pos=233.26,-4793.62,10.19` (section 4, M4-4). |
| j 20 minutes idle, five engines | **PASS** | boot 3, 12:49:34 to 13:09:35, 21 samples (`tb perf` + `state-end` + `docker ps` count): the server up at every sample, `ready=yes alive=5 whole=yes` at every sample, **module average 10 us on every sample** (worst single tick 1.0-6.0 ms), world update average 1.12-1.14 ms, worst 2-61 ms; raw lines in `~/forever-build/m4-idle20.txt` on the build VM. |
| regression: bot in one instance, commander in another; `.tb enter` on a character already inside | **PASS, server alive** | `tb enter Mcmd trigger 2230` (instance 2) then `tb enter party Mtank trigger 2230` (instance 1): `stuck=blocked:commander%20in%20another%20instance` and `dm=othermap` on the bots, no teleport, no abort. `tb enter Mtank 389`, `tb enter Mcmd 389`, `tb enter Mpriest 389`: `already on that map`. `tb tele Mcmd to Mtank` and `tb tele Mtank to Mcmd` with the two in different instances: `status=refused code=already-on-map`. Then `tb instance Mcmd leave` and `tb tele Mcmd to Mtank`: `inst=1`, `stuck=` empty, `dm=0.0`. `grep -c 'ASSERT\|ABORT\|Segmentation'` on the log: 0. |

### M4-4 (found at the end)
`.tb instance <leader> leave` moved a DEAD bot's body out of the dungeon; outside, the engine's `auto release` released it at the nearest graveyard (Razor Hill, Durotar, for an Alliance bot): a ghost far away until `.tb revive`. Now `leave` refuses a dead bot (`code=dead`, `status=refused`; the other bots still leave). Live (boot 4, the final binary): `tb kill Mdpsa`, `tb instance Mtank leave` -> `leave bot=Mdpsa map=1 status=refused code=dead ...`, `instance-end ... bots=6 started=5 refused=1`. A body left alone in the dungeon afterwards stays dead with `life=dead relhold=wipe`: its engine is DOWN because its commander left the map (the slice-3 R11 rule), so nothing releases it; `.tb revive` (PRACTICE) or the commander coming back is the way (also seen: `.tb revive Mdpsa`, then `leave` -> started). The driver must raise or release before it leaves.

## 5. Things the live test shows for the reviewers
* The release/resurrection race after a wipe (f, boot 3): whoever releases first (here the priest, 2 s early) becomes the rezzer for the others, who then WAIT (`relhold=rezzer`) and are raised one by one, limited by the priest's mana: 4 min to all alive, 6 min to `ready=yes`. Boot 1 (all five released together) took 4 s / 92 s. Both are the legacy crew rule working as written; if the driver wants the fast variant the rule would have to count "no one alive in the party at the moment the first body releases".
* `relhold` is the host's own computation of the crew rule (not the engine's `ReleaseHoldReason()`): in every sample it agreed with what the engine did (rezzer/combat held, wipe released, `no-entrance` held).
* The commander (a priest) is NOT counted as a rezzer by the crew rule: a priest death released at once although `Mcmd` stood on the map. Probably right (the commander has no engine), but it is the reason `d` released instead of waiting.
* A bot raised by the priest arrives at the priest's position (the core's resurrect teleport): seen as the three damage warriors standing at the commander after the canyon wipe.
* Entry took under 3 s for five bots from 6-18 yd outside the box; no guard attacked in any of the 10 entries (the Orgrimmar Grunts at 37/54 yd never aggroed). Level-9 refusal was verified in slice 3 (L8) and shows in the selftest.
* `slice: missing` lines at boot 3 (67 distinct): all the earlier slice-3 ones plus the slice-4 ones P4 listed as deliberately unregistered (`trigger dead`, `corpse near`, `falling far`, `move long stuck`, `resurrect request`, `val::gt32::{death count,15}` and `action self resurrect`), and `trigger release spirit`, `trigger revive from corpse` (WorldPacketHandlerStrategy packet triggers; the host calls `release` and the corpse run is unported: `incoming: reclaim corpse`). No error or warning line in the server log other than the console echo.
* The mob counts of `.tb instance info` (`mobs_total=132`) are the creature spawns of the map and difficulty; `mobs_loaded=130-132`.

## 6. Left / for the next package
* h (flee), o (open-world ghost, only seen by accident), the 30-minute idle of the spec's n (20 minutes done), a human commander (m).
* The party cannot yet clear canyon packs (priest mana); not slice-4 scope.
* The id recycling means the driver cannot tell a reset from "same instance" by `inst=`; `mobs_alive` equal to `mobs_total` and `entrance` re-set in the log are the signs.

## 7. `.tb grammar selftest #g5` on the final binary (boot 5; the fixed example records of every new kind and key)
```
TC>  
grammar-begin #g5
 
selftest case=specials value=a%20b%3Dc%3Ad%3Be%23f%25g_h%2Ci%2Bj%7Ck%C3%A9 empty= #g5
 
selftest case=list items=x%20y;a%3Bb;50%25 pairs=Mtank:343;a%3Ab:1 none= #g5
 
selftest case=coords pos=-601.26,-4250.10,38.94 map=389 flag=yes number=-17 fraction=12.35 #g5
 
selftest case=once key=second #g5
 
selftest bot=Mtank case=overlong list=strategy%20number%200;strategy%20number%201;strategy%20number%202;strategy%20number%203;strategy%20number%204;strategy%20number%205;strategy%20number%206;strategy%20number%207;strategy%20number%208;strategy%20number%209;strategy%20number%2010;strategy%20number%2011;strategy%20number%2012;strategy%20number%2013;strategy%20number%2014;strategy%20number%2015;strategy%20number%2016;strategy%20number%2017;strategy%20number%2018;strategy%20number%2019;strategy%20number%2020;strategy%20number%2021;strategy%20number%2022;strategy%20number%2023;strategy%20number%2024;strategy%20number%2025;strategy%20number%2026;strategy%20number%2027;strategy%20number%2028;strategy%20number%2029;strategy%20number%2030;strategy%20number%2031;strategy%20number%2032;strategy%20number%2033;strategy%20number%2034;strategy%20number%2035;strategy%20number%2036;strategy%20number%2037;strategy%20number%2038;strategy%20number%2039;strategy%20number%2040;strategy%20number%2041;strategy%20number%2042;strategy%20number%2043;strategy%20number%2044;strategy%20number%2045;strategy%20number%2046;strategy%20number%2047;strategy%20number%2048;strategy%20number%2049;strategy%20number%2050;strategy%20number%2051;strategy%20number%2052;strategy%20number%2053;strategy%20number%2054;strategy%20number%2055;strategy%20number%2056;strategy%20number%2057;strategy%20number%2058;strategy%20number%2059;strategy%20number%2060;strategy%20number%2061;strategy%20number%2062;strategy%20nu
 
selftest+ bot=Mtank list=strategy%20number%20127;strategy%20number%20128;strategy%20number%20129;strategy%20number%20130;strategy%20number%20131;strategy%20number%20132;strategy%20number%20133;strategy%20number%20134;strategy%20number%20135;strategy%20number%20136;strategy%20number%20137;strategy%20number%20138;strategy%20number%20139;strategy%20number%20140;strategy%20number%20141;strategy%20number%20142;strategy%20number%20143;strategy%20number%20144;strategy%20number%20145;strategy%20number%20146;strategy%20number%20147;strategy%20number%20148;strategy%20number%20149;strategy%20number%20150;strategy%20number%20151;strategy%20number%20152;strategy%20number%20153;strategy%20number%20154;strategy%20number%20155;strategy%20number%20156;strategy%20number%20157;strategy%20number%20158;strategy%20number%20159;strategy%20number%20160;strategy%20number%20161;strategy%20number%20162;strategy%20number%20163;strategy%20number%20164;strategy%20number%20165;strategy%20number%20166;strategy%20number%20167;strategy%20number%20168;strategy%20number%20169;strategy%20number%20170;strategy%20number%20171;strategy%20number%20172;strategy%20number%20173;strategy%20number%20174;strategy%20number%20175;strategy%20number%20176;strategy%20number%20177;strategy%20number%20178;strategy%20number%20179;strategy%20number%20180;strategy%20number%20181;strategy%20number%20182;strategy%20number%20183;strategy%20number%20184;strategy%20number%20185;strategy%20number%20186;strategy%20number%20187;s
 
selftest+ bot=Mtank list=strategy%20number%20250;strategy%20number%20251;strategy%20number%20252;strategy%20number%20253;strategy%20number%20254;strategy%20number%20255;strategy%20number%20256;strategy%20number%20257;strategy%20number%20258;strategy%20number%20259;strategy%20number%20260;strategy%20number%20261;strategy%20number%20262;strategy%20number%20263;strategy%20number%20264;strategy%20number%20265;strategy%20number%20266;strategy%20number%20267;strategy%20number%20268;strategy%20number%20269;strategy%20number%20270;strategy%20number%20271;strategy%20number%20272;strategy%20number%20273;strategy%20number%20274;strategy%20number%20275;strategy%20number%20276;strategy%20number%20277;strategy%20number%20278;strategy%20number%20279;strategy%20number%20280;strategy%20number%20281;strategy%20number%20282;strategy%20number%20283;strategy%20number%20284;strategy%20number%20285;strategy%20number%20286;strategy%20number%20287;strategy%20number%20288;strategy%20number%20289;strategy%20number%20290;strategy%20number%20291;strategy%20number%20292;strategy%20number%20293;strategy%20number%20294;strategy%20number%20295;strategy%20number%20296;strategy%20number%20297;strategy%20number%20298;strategy%20number%20299;strategy%20number%20300;strategy%20number%20301;strategy%20number%20302;strategy%20number%20303;strategy%20number%20304;strategy%20number%20305;strategy%20number%20306;strategy%20number%20307;strategy%20number%20308;strategy%20number%20309;strategy%20number%20310;s
 
selftest bot=Mtank case=freetext msg=xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx trunc=yes #g5
 
selftest bot=Mtank case=unsplittable #g5
 
error code=line-too-long cmd=grammar msg=a%20value%20of%20the%20record%20cannot%20be%20split%20below%203000%20bytes #g5
 
selftest case=state-h4 bot=Mdpsa life=awaiting-res relhold=rezzer resreq=Mpriest corpse=-3.85,-11.03,-15.83 corpsemap=389 outside=no entered=trigger entry= block= catchup=0 practice=no #g5
 
selftest case=state-h4-refused bot=Mlow life=alive relhold= resreq= outside=yes entered= entry= block=level blockmsg=level%20too%20low%20for%20this%20instance%20%28needs%2010%29%20%28TransferAbortReason%208%2C%20MapDifficultyXCondition%204172%2C%20PlayerCondition%20159263%29 catchup=0 practice=no #g5
 
selftest case=state-end-h4 alive=3 dead=1 ghost=1 wipe=no whole=no #g5
 
selftest case=threat-h4 mob=000000000000000000000000000000AB pos=12.50,-40.25,-19.10 flee=yes #g5
 
trigger id=2230 map=1 pos=1811.00,-4410.50,-18.50 shape=box length=6.00 width=6.00 height=5.00 yaw=0.00 target=1102230 targetmap=389 targetpos=0.80,-8.23,-15.53 #g5
 
enter bot=Mtank trigger=2230 map=389 status=started inbox=yes direct=no #g5
 
enter bot=Mlow trigger=2230 map=389 leader=Mtank status=refused code=level msg=level%20too%20low%20for%20this%20instance%20%28needs%2010%29 #g5
 
instance leader=Mtank map=389 name=Ragefire%20Chasm inst=1 diff=1 players=5 gms=1 mobs_alive=88 mobs_total=91 mobs_loaded=91 boss=2732:alive;2734:dead entrance=1102230 recent=1 owner=Mtank #g5
 
leave bot=Mtank map=1 status=started #g5
 
reset map=389 inst=1 status=ok #g5
 
tele bot=Mcmd to=Mtank status=started map=389 inst=1 tag=SETUP #g5
 
revive bot=Mdpsa tag=PRACTICE status=revived practice=yes #g5
 
kill bot=Mdpsb tag=SETUP status=killed #g5
 
gm bot=Mcmd tag=SETUP status=ok gm=yes #g5
 
grammar-end count=24 records=21 line_too_long=1 note=fixed%20records ready=no why=resting:Mpriest;far:Mdpsb #g5
 
error code=selftest cmd=grammar msg=a%20b%3Dc%3Bd #g5
```
(The two `selftest+` continuation lines of the over-long record are left out here; they are in TB-COMMANDS.md section 3.) Boot 5 also re-checked the id-recycling fix on the final binary: leave, reset, enter -> `got entrance world_safe_locs 1102230` logged a second time for `instance 1`, `info` = `mobs_alive=132 mobs_total=132 entrance=1102230`; and `tb kill party Mtank` on that fresh instance: two bots alive at the entrance after 1 s, the other three waited for the priest (the race of section 5).

## 8. Gates on the final tree (code commit 295c72b81; later commits are notes only)
| Gate | Result |
|---|---|
| option-ON `worldserver` (slot 1, `FOREVER_CMAKE_EXTRA=-DTORTOISEBOTS_SLICE1=1`) | `CMAKE_RC=0 MAKE_RC=0`, links, `grep -c undefined make.log` = 0 (every boot ran a binary built from the commit it names) |
| option-OFF module build (`-DTORTOISEBOTS_SLICE1=OFF`, target `tortoisebots`) | `CMAKE_RC=0 MAKE_RC=0`; `ar t libtortoisebots.a` = 8 objects (`TbBotMgr TbBotSession TbScripts TbItemPrototype TbSpellEntry TbEngineBrain TbH3Off TbH4`) |
| census SLICE4 / SLICE3 / SLICE2 / SLICE1 (slot 1) | `files 174 / 170 / 160 / 133  compile clean = files  with errors 0  error places 0  includes of a header ForeverCore does not have 0` (the legacy files did not change after the merge; only `forever/src` and docs did) |
| generator `--check`, registry check | `SliceAiObjectContext.cpp is up to date` (`slice: registered 17/48/75/170` in the boot log); `(a)=0  (b)=215` |
| `policy-tests.sh` (tree synced to `forever-build/tb`) | `472 PASS, 4 COMPILE_FAIL, 2 RUN_FAIL` = baseline (the same six tests as slice 3) |
| Turtle `vm-build ~/wt-slice4-integ --wait` (sha 18a5494c4, no live server running) | `COMPILE_SECONDS=19 CMAKE_RC=0 MAKE_RC=0` (ccache: the slice-4 files were compiled by P4's gate before) |
| Turtle `vm-test tb ~/wt-slice4-integ` (sha 3201a9d9c) | `RESULT: PASS ... rc=0 vm_duration=882s` |
| files outside `forever/` changed by this package | none (`CMakeLists.txt` one line: the list; the Turtle side is exactly P4's) |
| the test server | stopped after every boot (`docker ps`: `forever-bnet`, `forever-db`); `bin/worldserver` restored by the driver; the trainer SQL stays applied (INSERT IGNORE, 108 rows); characters `Mfresh`, `Mfreshb` added on account 1; `Mtank Mpriest Mdpsa Mdpsb Mdpsc Mcmd` left alive in Orgrimmar at the exit (1814.99,-4419.23); no `game_tele` rows added; `forever-test1` never touched |
