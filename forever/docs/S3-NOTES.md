# S3: throws, falls and forced speed (FOREVER-SMALL-SLICES-SPEC section 3, S3; core d02645dd21)

Branch `slice/s3` from `tbf/main` 5f1ac1dae, worktree `~/wt-s3`. ZERO core changes. (gates below)

## 1. What changed

| Question | Turtle | ForeverCore before | ForeverCore now |
|---|---|---|---|
| a knockback | `SMSG_MOVE_KNOCK_BACK`, the bot answers with `CMSG_MOVE_KNOCK_BACK_ACK` and a jump (`PlayerbotAIPackets.cpp:459-540`) | the core sends a bot no packet (`Unit::KnockbackFrom`: `IsBot()` flies it like a creature, `MoveKnockbackFrom`); the engine's `StopMoving` (`PlayerbotAI.cpp:609`) could cut the flight | the host watcher sees the `EFFECT_MOTION` spline, sets the engine's jump time (no `StopMoving`; `InterruptSpell(false, "knockback")` once) |
| fall damage after it | `SetFallInformation(maxHeight)` + the jump landing block, then the 1.12 land packets | `SetFallInformationCompat` was an unported no-op | `FallLandCompat`: the core's own `SetFallInformation` + `HandleFall` from the highest z seen, only at the flight's own map and spot and on dry ground |
| forced speed | the six `SMSG_FORCE_*` packets; the headless ack row | `SetSpeedRate` applies at once; nothing to ack (D5) | `.tb speed` for the live check; the row is reworded "core applies, nothing to ack" |

### Files

| File | Change |
|---|---|
| `forever/src/host/TbThrownPure.h` | pure `Observe`/`State`/`Step`: Start, Flying (apex, jump deadline only raised), Land once; `RaisesDeadline` (0 and the uint32 wrap); `chargesFall` (same map/instance, <= 5 yd from the last airborne sample, dry) |
| `forever/src/host/TbThrown.{h,cpp}` | the per-bot watcher, `Forget`, `AppendThrownState`, `.tb knock`, `.tb speed`; the liquid read at the landing point |
| `forever/src/compat/TbCompat_KB.h` | `FallLandCompat`: movement info position, apex, `HandleFall`, reset |
| `forever/src/compat/TbCompat_Unported.h` | `SetFallInformationCompat` is the core's `SetFallInformation(0, z)` |
| `forever/src/host/TbEngineBrain.{h,cpp}` | `NoteThrown` (start: interrupt, reset jump destination; raise the jump deadline through `kb::RaisesDeadline`) and the OFF stub; `InFlight` (the host's stops hold for a flight) |
| `forever/src/host/run/TbRunTasks.cpp` | `HoldForAct` does not stop a bot in flight (`ai->IsJumping()`) |
| `ai/playerbot/PlayerbotAI.h` | `GetJumpTime()` (the deadline is only raised) |
| `forever/src/TbBotMgr.cpp` | `kb::Update` for every bot in `Update`, before the master-away gate; `engine::InFlight` on all four host stop pairs; `kb::Forget` in `OnPlayerLogout` |
| `forever/src/TbScripts.cpp` | `.tb knock`, `.tb speed` rows at the end of `tbTable` |
| `forever/src/host/TbPartyState.cpp` | `thrown=`, `falldmg=`, `runrate=` after the P11 keys |
| `ai/playerbot/UnportedFeatures.h`, `forever/docs/UNPORTED.md` | the three rows reworded; no enum entry deleted (the legacy packet path still names all three) |
| `forever/docs/TB-COMMANDS.md` | section 19 |
| `forever/SLICE-KB.txt`, `forever/tools/test_thrown.cpp`, `test_kb_wiring.py` | census list and tests |

The spec's "called from `BotMgr::UpdateEntry`" does not fit this tree: `UpdateEntry` runs only while a dungeon entry is active
(`if (bot.entry.active)`); the one place every bot passes each world tick is `BotMgr::Update`. The watcher is one line there.

## 2. Why it is safe

* Nothing is started on Turtle (`forever/` only; two headers).
* `Observe` is pure and one-directional: a flight needs a Start; a Land is reported once; the jump deadline only grows.
* `NoteThrown` acts only on a usable brain; without one the bot still lands with the core's fall damage.
* The landing is charged only where the flight was thrown (same map and instance, within 5 yd of the last airborne sample) and on dry
  ground: a teleport mid-flight or a landing in water is reset, not charged.
* The watcher runs for every live bot before the master-away gate, and every host stop (`Unit::StopMoving`, `MotionMaster::Clear`) holds
  while the engine's jump time is up: nothing in the host cuts a flight.
* `HandleFall` reads exactly what the core's own `CMSG_MOVE_FALL_LAND` path leaves (position, last fall z), so thresholds, safe-fall,
  rate and caps are the core's.
* OFF build: `AppendThrownState` has a no-op body (the only user is the engine's `.tb state`); commands and the watcher compile without the engine.

## 3. Tests

* `forever/tools/test_thrown.cpp` (VM, `g++ -std=c++17 -O1 -Wall -Wextra -I.`): the cases below, `57 checks, 0 failed`. Red first, four
  mutations of `TbThrownPure.h` (each reverted after the run): the `currentMs == 0 ||` clause removed -> 1 failed; the map/instance clause
  -> `true` -> 2 failed; the landing-distance term -> `true` -> 1 failed; `!inWater()` -> `true` -> 1 failed.
* `forever/tools/test_kb_wiring.py`: watcher, engine, wiring (watcher order, every host stop, the act hold), rows and docs pins;
  `--mutate` catches all 8 mutations (rc 1). Run: `Ran 7 tests ... OK`.

## 4. Review fix round (claude M1/M2/L3/L4/L5, codex high #1)

| Finding | Fix |
|---|---|
| M1 `TbEngineBrain.cpp:333` was not wrap-safe and missed jump time 0 (after 24.8 days uptime the engine never saw a flight) | one pure predicate `kb::RaisesDeadline(current, wanted)` (`current == 0`, else a wrap-safe `int32_t` compare) used by both `NoteThrown` and `Observe`; pure test at 0 with `now = 0x90000000` and across the wrap |
| M2 `TbThrown.cpp:83-84`: a flight cut by a teleport was "landed" at the arrival spot with the old apex (fall damage, possibly on another map) | `State`/`Sample` carry map, instance and the last airborne x/y; `Step.chargesFall` is false when the landing is on another map/instance or more than `kLandingToleranceYd` (5 yd) from that sample; the watcher then skips `FallLandCompat` entirely |
| L3 a landing in water still took fall damage | `InWaterAt` (the core's `Map::GetLiquidStatus` at the landing point, `LIQUID_MAP_IN_WATER | LIQUID_MAP_UNDER_WATER`) is asked through the `Observe` callback, only when the flight ends; `chargesFall` false there |
| L4 `HoldForAct` and the other host `StopMoving` callers cut a flight | `HoldForAct` stops only `if (moving && !ai->IsJumping())`; `BindMaster` likewise; `engine::InFlight` (the brain's jump time) guards all four `MotionMaster::Clear()` + `StopMoving()` pairs in `TbBotMgr.cpp` (entry fail, entry landing, entry handler, engine drop) |
| L5 the watcher was skipped for a master-away bot | `kb::Update` runs in `BotMgr::Update` before the master-away gate, for every live, non-teleporting bot (the engine hold is skipped without a brain; the landing is not) |
| codex high #1 | same fix as L5 |

Not actioned this round (the brief's decisions): codex's "every EFFECT_MOTION is a knockback" (claude rates it INFO: the same flights get
the same hold and fall damage a client would) and codex's apex sampled per tick (the server's own rule, `Player::UpdateFallInformationIfNeed`;
a spline-parameter apex needs a discriminator the core does not expose to the host).

Gate lines (build VM, slots 6 and 7, core d02645dd21). First round (head `76836587b`):

* ON module (slot 6, `-DTORTOISEBOTS_SLICE1=ON`): first attempt `MAKE_RC=1` at `TbThrown.cpp:66:64: error: member access into incomplete type 'Movement::MoveSpline'` (MotionMaster.h forward-declares it); adding `#include "MoveSpline.h"` and rebuilding: `CMAKE_RC=0` / `MAKE_RC=0` (`TOTAL_SECONDS=1366`)
* OFF module (slot 7, no extra options): `combined build: slot 7 key core-9b88bd2d6584-opt-78bf025f` / `CMAKE_RC=0` / `MAKE_RC=0` (`TOTAL_SECONDS=66`)
* census (`FOREVER_SLOT=91 FOREVER_CENSUS_LIST=forever/SLICE-KB.txt forever/tools/vm-census.sh "" 14`, run folder `wt-s3-s91-SLICE-KB-20261121T031337Z-3679851`): `files 286  compile clean 286  with errors 0  error places 0 (in 0 files)  includes of a header ForeverCore does not have 0`
* generator: `python3 forever/tools/slice_gen_context.py --check` -> `SliceAiObjectContext.cpp is up to date` (rc 0); registry `(a)=0` (unchanged)
* Turtle build (`vm-build ~/wt-s3 --wait`, job `1-1795228916214341159-3492533`, tree `051a8cafc-dirty`): **RED, a pre-existing break**:
  `COMPILE_SECONDS=4010 CMAKE_RC=0 MAKE_RC=2`, first error `ai/cmangos-compat-unported.h:126:44: error: 'char* ChatHandler::ExtractKeyFromLink(char**, const char*, char**)' is protected within this context`
  (`'ChatHandler' is not an accessible base of 'PlayerbotChatHandler'`, compiling `ai/playerbot/PlayerbotAI.cpp`). That file is byte-identical to `tbf/main`
  (`0d27298a9`); the separately committed `fix/turtle-build` (20068fc43) repairs it, not this round. S3 changes no Turtle code: the new
  `GetJumpTime`/`UnportedFeatures.h` text only forces `PlayerbotAI.cpp` to recompile, which is what meets the existing break (S2 met the same line).
* `vm-test tb ~/wt-s3`: `RESULT: PASS  kind=tb wt=wt-s3 sha=051a8cafc dirty=1 files=4076` / `rc=0  vm_duration=2147s` (log `wt-s3-20261121-033911-3061550-82d7.log`)
* pure unit (`test_thrown.cpp` in `forever-builder`, VM): `23 checks, 0 failed`
* wiring: `Ran 7 tests ... OK`; `--mutate` rc 1.

Fix round (this head):

* ON module (`tar -C ~/wt-s3 --exclude=.git -cf - . | ssh yulon-ubuntu2 "~/forever-build/slot-sync-build.sh 6 tortoisebots -DTORTOISEBOTS_SLICE1=ON"`):
  `combined build: slot 6 key core-9b88bd2d6584-opt-85c8cb4e` / `build token 1 of 3 (slot 6)` / `module sync: 324 changed/new/deleted files` / `CMAKE_RC=0` / `MAKE_RC=0` / `TOTAL_SECONDS=131`
* OFF module (`slot-sync-build.sh 7 tortoisebots`): `combined build: slot 7 key core-9b88bd2d6584-opt-78bf025f` / `build token 3 of 3 (slot 7)` / `module sync: 284 changed/new/deleted files` / `CMAKE_RC=0` / `MAKE_RC=0` / `TOTAL_SECONDS=34`
* generator: `python3 forever/tools/slice_gen_context.py --check` -> `SliceAiObjectContext.cpp is up to date` (rc 0)
* pure unit (`g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/test_thrown.cpp && /tmp/test_thrown`): `57 checks, 0 failed`; four mutations of `TbThrownPure.h` (reverted, compiled from `/tmp/kb-mut`): `currentMs == 0 ||` removed -> `57 checks, 1 failed`; map/instance clause -> `2 failed`; landing distance -> `1 failed`; `!inWater()` -> `true` -> `1 failed`
* wiring (`python3 -m unittest forever/tools/test_kb_wiring.py`): `Ran 7 tests in 0.008s` / `OK`; `python3 forever/tools/test_kb_wiring.py --mutate` -> `all 8 mutations caught` (rc 1)
* Turtle build: NOT run. PR #99 is not on `tbf/main` (`5f1ac1dae` is the tip; the breaking `0d27298a9` is still the file's last change), so the build would still be red at the known main break. This round changes no Turtle code.
* No live run (none was scheduled for this round).
* Docs-side (`bash tools/verify_all.sh`, the repo rule for doc changes): OKF, command docs, migration guard, module surface, Eluna guards
  and action/trigger wiring (`queued=1890 creators=2926 live-missing=0`) passed; the run was cut by the wrapper's 900 s timeout (rc 124)
  inside the repo unit-test discovery, which does not read `forever/tools`.


## 5. Live checks (not run)

a) flat ground `.tb knock Mwar 10 7`: lands, `thrown=1`, `falldmg=0`, follows within 3 s.
b) 25 yd ledge edge (record coordinates), `.tb knock Mwar 20 7`: `falldmg` within 10% of `0.018*(drop)-0.2426` of max health.
c) knock mid cast: cast interrupted, recast after landing.
d) `.tb speed Mwar run 0.5`: state `runrate=0.50`, follow keeps up, back at 1.0.
e) a Hamstring/Frostbolt slow (or a second bot's cast) shows in `runrate`.
f) a live knockback mob/boss on the route: the same watcher line and one landing.
