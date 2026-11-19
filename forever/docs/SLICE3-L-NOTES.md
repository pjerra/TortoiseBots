# Slice 3, package L: fix what package M found, then the live test

Branch `slice3/integ` (worktree `~/wt-slice3-integ`), slot 2. Notes from the start; times are the build VM's UTC (the CET logs are +1 h). The test server was free all the time (the slice-2 worker had finished). Boots in this package: 7 (two crashes, section 7). Driver `~/forever-build/m3-sess.sh` as in package M (`NOLOG=1` swaps `AiPlayerbot.EnableActionLog` to 0 in `etc/aiplayerbot.conf` for that boot only and puts the file back on stop).

## 0. First: `port/forever` merged (slice 2)
`git merge --no-ff port/forever`: **two conflicts**, both in the slice sources.
* `forever/src/slice/SliceAiFactory.cpp`: slice 2 added `"heal interrupt"` to the priest's combat kit and made the priest branch `return`; slice 3's priest branch falls through to the `threat` block. Kept both: the priest gets `heal interrupt` AND the commanded `threat` back-off.
* `forever/src/slice/SliceAiObjectContext.cpp` (generated): took our side and re-ran `slice_gen_context.py` on the merged generator: `16 strategies, 44 actions, 73 triggers, 169 values` (slice 3 alone: 15/44/72/168); `--check` current, registry check `(a)=0 (b)=207`.
Auto-merged without conflict: `TbScripts.cpp`, `TbEngineBrain.cpp`, `TbSpellEntry.*`, `UnportedFeatures.h`, `CENSUS.md`, `UNPORTED.md`, the generator. Files outside `forever/` that the merge brought in (already in `port/forever`): `ai/cmangos-compat-r4v.h`, `ai/cmangos-compat-s2i.h`, `ai/playerbot/UnportedFeatures.h`, `GenericSpellActions.cpp`, `LootAction.cpp`, `UseItemAction.cpp`. SLICE1/2/3 lists need no change (`comm` shows nothing missing).
Build trap found on the way: after a merge the VM tree keeps the objects of the pre-merge build for files whose mtime is older (rsync keeps mtimes): the first merged binary still said `registered 15/44/72/168`. Touching the merge's changed files on the VM (`src-2/src/modules/TortoiseBots/<path>`) fixed it; `registered 16/44/73/169` and `heal_interrupt` in the priest's `co=` list are the proof.

## 1. The eight fixes
| # | Fix | Where |
|---|---|---|
| L1 | follow target of a COMMANDED bot = its master (the commander) when in the world on the bot's map, else the group master. Both-core helper `MasterTargetCompat` read by the value `master target` (which `follow target`, "distance to master" and the follow trigger read); Turtle = `GetGroupMaster()` | `ai/playerbot/strategy/values/MasterTargetValue.h:13`, `forever/src/compat/TbCompat_S3.h:74`, `ai/cmangos-compat-s3.h` |
| L2 | pull hold: the host's `pull` order calls the legacy party hold `ai::PausePartyPullHold(tank, 3, leadKeyed=true, 15)`: non-tanks wait until the pull LANDED, then until the whole pack is on the tank (3 s at the earliest, 10 s at the latest), at once when a mob hits a held bot or the tank is under 60 %, and 15 s after the order if it never lands. Two legacy gaps closed: a body pull never restamped the held bots' clock (`RestampPullParty` added after `OnPullActionCompleted` in the `reach pull` branch) and a released hold left a commanded bot with NO movement mode (`GetDefaultMovementStrategy` answered `wander`, which the slice does not register): a commanded bot's default is now `follow` | `forever/src/host/TbOrders.cpp:310`, `ai/playerbot/strategy/actions/PullActions.cpp:106,492`, `ai/playerbot/PlayerbotAI.cpp:5041` |
| L3 | taunt: measured, no behaviour invented (section 4) | - |
| L4 | why seven mobs came: section 5 | - |
| L5 | `food`: the item mirror's food category: `FindFoodVisitor` reads `Spells[0].SpellCategory` (11 for food); ForeverCore's ItemEffect row can carry no category. A missing (0) category now takes the spell's own (`SpellInfo::GetCategory`); a stated one is kept. `.tb item` prints the category | `forever/src/compat/TbItemPrototype.cpp:337`, `TbScripts.cpp` (`.tb item`) |
| L6 | perf with the action log off: section 6 | - |
| L7 | `MasterGraceSeconds`, `RestHealthPct`, `RestManaPct` were in `forever/conf/tortoisebots.conf.dist` already; the RUN FOLDER's copy lacked them (`.tb state` printed two "Missing name" errors per bot): appended to `~/forever-run/etc/worldserver.conf.d/tortoisebots.conf` (backup `.bak-L7`). No error since | conf |
| L8 | entry refusal text: `TRANSFER_ABORT_DIFFICULTY` with a failed MapDifficultyXCondition reads the condition's PlayerCondition: "level too low for this instance (needs N)" when its MinLevel is above the player's, else "refused by an entry condition (... ids)"; id 0 keeps "no map difficulty data" | `forever/src/TbBotMgr.cpp:69` (`EntryRefusalText`, used by `.tb enter` and the blocked-follower reason) |

## 2. L9: reply grammar version 2
Done (TB-STATE-GRAMMAR-V2.md). One encoder (`Enc`, `Rec`, `Reply` in `forever/src/host/TbH3.cpp`/`.h`) used by every writer (`TbOrders.cpp`, `TbPartyState.cpp`, `TbTestKit.cpp`, errors in `TbH3.cpp`, the default build's stub `TbH3Off.cpp` encodes its one error the same way). `.tb grammar selftest` prints fixed records (space `=` `:` `;` `#` `%` `_`, an empty value, a repeated key, an over-long line with its `selftest+` continuation). `state-end` carries `ready=yes|no` and `why=<reason>:<bot>;...` (reasons `dead combat resting far blocked`). `forever/docs/TB-COMMANDS.md` rewritten from real output. One real state reply (the live test below read v2 throughout):
```
state-begin #t1
state bot=Mtank role=tank rolesrc=forced class=warrior level=18 hp=708 hpmax=708 hppct=100 power=rage pw=0 pwmax=100 st=non-combat incombat=no tgt= tgtentry= tgthp= victim= hold=no mv=follow stuck= engine=yes pos=-9100.0,-2005.0,131.0 map=0 inst=0 dl=self dm=0.0 ord=0: lastorder= co=close;protection;pull;tank%20assist;tank%20face nc=default;follow;food;protection;tank%20assist cast_queued=0 cast_delivered=0 cast_dropped=0 cast_pending=0 eating=no drinking=no resting=no rest_why= #t1
state-end count=1 bots=1 resting=0 ready=yes marks=0 target=Mtank #t1
```
Not-ready sample after a pull: `state-end count=5 bots=5 resting=3 ready=no why=resting:Mpriest;resting:Mdpsb;resting:Mdpsc marks=0 target=party:Mtank #rd`; a bot that did not follow into an instance: `ready=no why=far:Mpriest`.

## 3. Live observations (open world, Redridge, console commander; VM load 2-12 unless said)
| Obs | Result |
|---|---|
| L1 follow over 180 yd with a corner (90 south, 90 east, bots started in a different direction) | PASS: all five within 3 yd of the commander at the end; the transient worst was 11.9 yd (a damage warrior; limit 8) in the first 5 s. The tank follows the commander like everyone else (`dm=0.0`, not `follow target: itself` as in M). In the first try (200+ yd, Lakeshire) two bots did not arrive: `A:follow - FAILED why=dispatch-short path=0x8` (no path from where they stood) and `no-move-try` on a hill: the navmesh, not the follow rule |
| `stay` / `come` | PASS (M); `too-far` for `come` is NOT reachable: the host's own catch-up puts a commanded bot next to its commander within seconds (also after the commander `tele`s 290 yd away), so no bot is ever more than the react distance (150 yd) from him |
| marks restored after `attack` | PASS: `order ... attack spawn:20010110` ran (`ord=17:ok lastorder=attack`) and `state-end marks=0` |
| GM chat sender | NOT RUN: it needs a network client on an account at security level 1 or more; none was available in this package. The reply writers are the same handler code for the console and a session (`ChatHandler::SendSysMessage`), see TB-COMMANDS.md section 1 |
| three ordered single-pack pulls, no death, d and f | PASS for no death in all five pulls (pack A twice counting the M-style first attempt, pack B three times, a single mob in RFC). d: in every pull the damage warriors' first combat/cast was after the tank's first threat action (pack A, 09:33.1 tank `Revenge`, dps `Execute` 09:37.7; pack B: tank 02:36:00.2, priest 04.0, dps 07.4: the hold released 3-4 s after the landing). f: see section 4 |
| rest between pulls via `state-end` | PASS: `ready=no why=resting:...` until every line says resting=no; the damage warriors recover in under 20 s from the priest's heals; the priest's mana takes minutes (`drink` works, see section 8) |
| `hold` / `free` | PASS (`hold=yes` then `ord=..:ok` for `free` clears it), as in M |

## 4. L3: where the taunt time went
Pack A, pull 2 (cast log of the tank + 100 ms state samples): the mobs turned to the priest at 09:34.4 (the priest's first heal gave her threat before the tank had hit anything: tank threat 0 on all four mobs); the tank's `Taunt` was `CAST_REFUSED reason=out-of-reach` 30 times between 35.0 and 35.6 (Taunt needs the mob in range, the mobs stood on the priest), then `Thunder Clap` at 36.27 and `Taunt` at 37.83; the victim was the tank again at 09:36.04 (1.7 s after the turn; threat from `Revenge` 33.1, `Thunder Clap`). So the first-turn recovery is 1.7 s, not 4.7 s. The 4.7 s of M was the **pull multiplier**: while a pull runs the legacy `PullMultiplier` scales `taunt`, `taunt on attacker` and `reach melee` by x0.01 / x0.00 until `pull end` (the landing, 4.4-4.8 s after the order here: `A:taunt - MULT pull x0.010 (41->0.41)`), and the mobs were already on the party in that window (they aggro by proximity while the tank walks).
Returns measured in pack B (3 pulls): 0.9 s, 1.7 s, 1.9 s, about 3.0 s (mob `8B`, victim a damage warrior 11:18.06-11:21.05) and, **8 s** in pack A (mob 23, victim Mdpsc 09:42.05-09:50.1) when a second mob turned inside Taunt's 10 s cooldown: the legacy `mocking blow taunt` (learned at 16) did not fire. No code changed: the multiplier is the legacy pull rule and the dominant part (the approach) cannot be shortened without changing the pull; the 8 s case is the single-target Taunt cooldown. Left for the lead: whether `mocking blow taunt` should cover the second mob; whether the tank should wait for the party to be out of aggro range.

## 5. L4: why seven mobs came
The world database (`world.creature`, map 0): around pack A's three brutes (20010110/11/12) stand a Mystic on a patrol (20010108, 12 yd), a patrolling brute (20010109, 32 yd west: exactly where M's party stood, 40 yd west of the pack) and further patrols on every side (MovementType 1/2 within 30-70 yd: 20010130, 25065400, 20016350, 20011669, 25046676...). Pack B's three brutes have a wandering mystic at 16 yd and a wandering brute at 17.5 yd. So "a pack of 3" does not exist in Redridge: the pull's social assist (about 10 yd) and the patrols that walk into the staging place add 1-4 mobs. M's pack A seven = 3 brutes + the Mystic + a patrolling brute + two Mystics that reached the party. Used here: staging points chosen away from patrols (north of A: (-9100,-2005); north of B: (-9034,-2385)), z 131 (the bots fall to the ground when they move). Result: pack A pull = 4 mobs, pack B = 3-5 mobs (patrols still join), no death in any. The healer taking aggro with tank threat 0 happens in every pull during the first 2-3 s (the priest's first heal is the first threat on the pack); with 3-5 mobs she lost at most 22 % (78 % left in pack A, untouched in pack B).

## 6. L6: perf (`.tb perf`, a rolling window of the last ticks; five engines; load is the VM's 1-minute average)
| State | Action log | Module average | Load |
|---|---|---|---|
| idle | on (scope `commanded`) | 10-14 us | 1.4 |
| idle | off | 11-12 us | 9.7 |
| fighting (10 s window, pack B) | on | 48 us (16 us in the quiet window before) | 1.9 |
| fighting | off | 44 us (17 us before) | 11.5 |
Package M's 71-96 us idle was measured at load 14-30: the VM's load, not the module. The action log costs about 8 % while fighting; it is not the top cost. Not over 50 us at low load, but the fight average sits at 44-48 us: the largest visible cost in the bot logs is the damage warrior's `Execute` loop (a `CAST_START`/`CAST_OK` pair every 0.1-0.15 s on a target under 20 % health, 100 casts in 4 s: the action reports OK and is repeated every tick, probably because the cast is refused for rage after the cast result: `[CAST_OK] phase=PREPARE`). Not profiled with a profiler; no blind optimisation.

## 7. Crashes (both the core's, both with a `tele` of a bot the host was already sending to the same instance)
`InstanceMap::CannotEnter - player <bot> already in map 389, 2, 1!` then `Map.cpp:2970 in CannotEnter ABORTED`:
1. `tele name <all six> RFC` for every character in one console burst while the bots are commanded: the host's catch-up (`BotMgr::Think`) and my `tele` both sent one bot (Mdpsc) into the instance.
2. a second time after the commander alone entered Ragefire Chasm: FOUR of five bots followed in 12 s, the fifth (Mdpsb, and in the next boot Mpriest) stayed at the human start (-8950,-132.5, map 0) with `stuck=` empty and `ready=no why=far:<bot>`; a `tele name <that bot> RFC` then hit the same assert. The core thinks that bot is already in map 389 while its position says map 0: it is half-transferred (a failed or raced teleport into the instance). Not fixed: it needs a look at `BotMgr::Think`'s instance entry (a bot already `IsBeingTeleported` must not be sent again) and at what the core does with a refused transfer (the "homebind" position). The 20-minute idle and the RFC pull below were run with the stranded priest left where she was.
Also the world-thread hang of package M did not recur (all boots at load 2-20).

## 8. Ragefire Chasm (recorded, not a gate)
Commander `tele`d in, four bots followed (above), pull `spawn:20038043` (a mob of entry 11320, 41 yd from the entrance): the tank landed it, the three damage warriors stayed out until 03 s after the landing, the mob died in 19 s, tank low point 65 %, no death (the priest was the one that had not followed). `ready` after: all `resting=no`.

## 9. 20 minutes idle, five engines
(filled in below)

## 10. Gates
(filled in at the end)
