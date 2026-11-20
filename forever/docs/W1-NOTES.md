# W1: grind and live (world-life slice 1)

Port of the open-world life onto ForeverCore (core pin `6517af05`): a **masterless** host bot picks level-fit mobs, fights, loots,
rests and eats, and on an open-world death releases, runs back as a ghost and reclaims its corpse (the spirit healer is the
fallback). One teleport is allowed: the host's rescue of a ghost that made no headway for five minutes. Zero core changes.
Spec: `FOREVER-W1-W6A-SPEC.md` Part 1 (W1 only), plan row W1 of `FOREVER-WORLD-LIFE-PLAN.md`.

## 1. What runs

| Piece | Where |
|---|---|
| Free flag on the host entry, engine without a master, the five-minute rescue | `forever/src/TbBotMgr.*`, `forever/src/host/TbEngineBrain.*` |
| `.tb login <bot> free`, `.tb free <bot> on\|off` | `forever/src/TbScripts.cpp` |
| `IsFreeBot` = a free host bot (not random) | `forever/src/host/TbHostBotManager.cpp` |
| Free kit: class spec + `grind, food, loot, roll, default, buff` | `ai/playerbot/policy/world/FreeKitPolicy.h` (pure, tested), `forever/src/slice/SliceAiFactory.cpp` |
| Grind target | `ai/playerbot/strategy/values/GrindTargetValue.cpp`, `strategy/generic/GrindingStrategy.cpp` |
| Dead kit: release (slice 4), find corpse, revive from corpse, spirit healer | `ai/playerbot/strategy/actions/ReviveFromCorpseAction.cpp`, `strategy/values/DeadValues.cpp` |
| Safe revive point | `ai/playerbot/FleeManager.cpp` (only the `const TerrainInfo` fix) |
| The both-core pair | `forever/src/compat/TbCompat_W1.h` + `ai/cmangos-compat-w1.h`, wrapper `ai/playerbot/TbCompatW1.h` |

New host helpers: reclaim and spirit-healer activation go through the core's own handlers (typed `WorldPackets::Misc::ReclaimCorpse`
into `HandleReclaimCorpse`, `WorldPackets::NPC::SpiritHealerActivate` into `HandleSpiritHealerActivate`), so the reclaim delay,
resurrection sickness, the 25 % durability loss and the bones are the core's. `StaleCorpseBonesCompat` wakes a body that still
has a same-map corpse before a release (`BuildPlayerRepop` refuses it otherwise, `Player.cpp:4697-4702`). No raw object pointer is
kept between ticks: the rescue keeps two distances and a stall clock (`policy/death/GhostRescuePolicy.h`).

## 2. The cut (each one makes the bot do LESS)

| Legacy | W1 |
|---|---|
| Ghost-stall hop, detailed-move teleport, 10-minute graveyard teleport, cross-map corpse resurrect + entrance teleport | never (`CorpseRunTeleportsEnabledCompat`/`CorpseRunCrossMapEnabledCompat` are false on F); the ghost walks |
| `RelocateHopelessBot`, `SendStrandedLowbieHome` after a revive | never (no-ops on F) |
| Spirit healer by direct `ResurrectPlayer` + durability | the core's handler (sickness, durability, bones); the bot walks to the healer and activates it |
| Ghost without corpse: repop + direct revive | activate a healer in reach, else walk to it, else stand (the host rescue covers a stuck ghost) |
| `repop`, `self resurrect`, `unstuck`, `bg reset objective force` | unregistered |
| `rpg`, `travel`, `tfish`, `maintenance`, `attack tagged`, `start duel`, `collision`, `group`, `guild` | unregistered/not in the kit |
| `need for quest`, travel target values | read as the missing-value stand-in (false/empty) |

## 3. Registry

Added by `forever/tools/slice_gen_context.py` (`SLICE_W1_KEYS`): strategy `grind`; actions `attack anything, avoid elite, idle
wander, find corpse, revive from corpse, spirit healer`; triggers `no target, dangerous elite near, dead, corpse near, val`; values
`grind target, can fight elite, dangerous elite, graveyard, best graveyard, should spirit healer`, plus `can move around`
(the live boot showed the free kit's `attack anything` and `idle wander` read it; it is `MaintenanceValues.h`'s inline value, and the
missing-value stand-in answered false, so the bot stood still). `slice_registry_check.py` (a) = 0.
`can fight elite` is the inline `CanFightEliteValue`; the one symbol it needs that is not in the slice
(`ShouldSellValue::GroupMemberLeavingForVendor`, `MaintenanceValues.cpp`) is stubbed in `SliceLinkStubs.cpp` with the "do less"
answer (true: a group member is leaving, so no elite is ever picked; a free bot has no group and never reaches it).

## 3b. Shared seams (ownership)

W6a (item usage and upgrades) is another developer's branch. W1 therefore keeps to its own blocks: the key list is `W1_KEYS` in
`forever/tools/slice_gen_context.py` (W6a adds its own block); the one stand-in is the explicit `W1 (world life)` block at the end
of `forever/src/slice/SliceLinkStubs.cpp` (the slice-9 item block is untouched); the compat helpers are the new
`forever/src/compat/TbCompat_W1.h` + `ai/cmangos-compat-w1.h` pair; the new `.tb free` row is appended at the end of the command
table in `forever/src/TbScripts.cpp`. `ItemUsageValue.cpp`, the `PoolServices`/`TbHostOwnership.cpp` pool stubs and
`TbCompat_W6.h` are not touched: the item value stays exactly as L9 left it (it answers "no usage", the W6a seam).

## 4. Gates

| Gate | Result |
|---|---|
| Census, `forever/SLICE-W1.txt` (270 files) | `files 270  compile clean 270  with errors 0  error places 0`, run folder `wt-w1-s8-SLICE-W1-20261120T125238Z-2669463` |
| Module ON (`TORTOISEBOTS_SLICE1=ON`, `tortoisebots`) | `CMAKE_RC=0 MAKE_RC=0` (slot 8) |
| Module OFF (`TORTOISEBOTS_SLICE1=OFF`, `tortoisebots`) | `CMAKE_RC=0 MAKE_RC=0` |
| Worldserver link ON | `CMAKE_RC=0 MAKE_RC=0` (`worldserver`, 71.8 MB, contains the W1 rescue marker, no `flee manager` row) |
| Generator / registry | `slice_gen_context.py --check` up to date; `slice_registry_check.py` (a) = 0 (21 strategies / 74 actions / 111 triggers / 193 values) |
| Policy tests (standalone, g++ C++17) | `test_ghost_rescue_policy` 19 checks PASS; `test_free_kit_policy` 72 checks PASS; `test_free_grind_policy` 50 checks PASS |
| Wiring | `python3 -m unittest forever/tools/test_w1_wiring.py` 22 tests OK; `test_slice9_l9_wiring.py` 11 OK (its list pointer moved to SLICE-W1.txt) |

## 5. Live test

Server: `forever-test2` (free; release world DB, separate binary in `~/forever-run/bin`, `aiplayerbot.conf` in
`~/forever-run/etc`), our `worldserver` built in slot 8 with `TORTOISEBOTS_SLICE1=ON` copied over the release binary (backup
`bin/worldserver.pre-w1`, restored after). Console only, no client, no commander.

Setup (the spec's D5 order adjusted to what the host commands allow: `.tb level`/`.tb tele` need a logged-in bot, and
`.tb tele` is a setup teleport of a host bot; `tele=` is read as a delta from after this setup):

1. `account create` x2, `.tb create` Wh (human warrior) and Wo (orc warrior).
2. `tb login Wo free` at its creation spot: gate a (free=yes engine=yes, the nc list is exactly the free kit) and the
   masterless log path.
3. `tb login Wk` (no master, stands), `tb level Wk 10`, `tb train Wk`, `tb tele Wk 1 1007.78 -4446.22 11.2022` (the Durotar
   row of `game_tele`, read from the world DB), `tb free Wk on`: the level-10 bot of the brief, at the Razor Hill end of
   Durotar where the mobs are level 5-9.
4. 15 minutes of `tb state Wk` every 30-60 s, one `.tb kill Wk` in the middle, `tb state` every 5-10 s through the corpse run;
   `world.sh log` keeps the `[W1] death/revive/rescue` lines.

Deviations from the spec's setup, both from the brief: one level-10 bot (not 12), and Durotar for the level-fit mobs (Northshire
is grey to a level-10 bot and its grind pick refuses grey).

## 6. Live numbers and open points

Server `forever-test2` (release world DB, `worldserver` replaced by our ON build, release binary restored and the server stopped
afterwards), console only, about 12 minutes of the level-10 window plus a first exploratory run:

| Bot | Level | Kills | Deaths | XP | Bag slots free | Teleports | Death / revive |
|---|---|---|---|---|---|---|---|
| **Wkorc** (orc warrior, level 10, Durotar `game_tele` 308, `tb login` then `tb level 10`, `tb train`, `tb tele`, `tb free on`) | 10 | **10** | **1** | 931/7600 | 18 -> 14 (4 looted items) | 2 (1 setup + 1 core release) | one corpse run: reclaim after 21 s at 19.0 yd, then HP 50 % -> full |
| Wo (`tb login Wo free`, orc warrior, Valley of Trials) | 1 -> 3 | 30 | 1 | 675+ | 18 -> 49 (looted bags/items) | 1 | reclaim after 51 s at 26.7 yd |
| Wh (human warrior, Northshire) | 1 -> 2 | 15 | 6 (5 were `.tb kill`) | 385 | 18 -> 25 | 6 | reclaim 17-22 s at 4.9-24.9 yd |
| Wk (exploratory, human level 10 placed in Durotar) | 10 | 3 | 5 | 480 | 18 -> 11 | 4 | reclaim `secs=116/118 yd=17.6/19.5`; healer x2 at death count > 3 |

Gates of the brief, on `Wkorc` (the level-10 bot) with the two level-1 bots as a second sample:

- **a** boot: 3 engines, `free=yes engine=yes`, non-combat strategies exactly `arms;buff;default;food;free;grind;loot;roll` (the
  `free` entry is the movement marker). Every `slice: missing` at boot is in the leave-out table; `can move around` was the one
  gate found missing live (its false stand-in stopped all movement) and is registered now.
- **b** grind: `grind=` named level 8-9 creatures in Durotar (`3100`) and level 1-4 ones in the two starting valleys
  (`299`, `3098`, `3124`, `38`, `69`); no pick above the +4 cap and no grey (the idle fallback refuses them).
- **c** kills and loot: `kills` 0 -> 10 for Wkorc, `bagfree` 18 -> 14; `bot_events.csv` shows `StoreLootAction` rows (`Ruined
  Pelt` 4865, `Broken Fang` 7073); Wo's bag capacity grew to 49 (looted bags). Money stayed 0 for all three (no coin in these
  drops).
- **d** rest: HP dipped in fights (Wkorc 241 -> 74 -> 200; Wo 76 -> 84) and the next pull waited for it. `resting=` was only
  sampled with `rest_why=dead`; no food existed in the bags (`eating=no`; U6: `playercreateinfo_item` has no class-1 row and F's
  item pool has no classic low-level food with a food spell), so eating was NOT exercised - see the open point.
- **f** corpse run: Wkorc, `.tb kill`d: `life=ghost`, `corpserun=walk` -> `reclaim`, `[W1] revive bot=Wkorc how=reclaim secs=21
  yd=19.0`, alive at 50 % and full after regen, `tele` grew by exactly the core's release teleport (2 = setup + release). The
  exploratory human bot walked its ghost 527 -> 34 yd over ~70 s (`ghostyd` samples) and reclaimed.
- **h** spirit healer (extra): after the death count passed 3 on the exploratory bot, `[W1] revive bot=Wk how=healer secs=3` (it
  died beside the healer): the core's handler revived it, durability fell 100 -> 75, no sickness (level 10 is below
  `Death.SicknessLevel = 11`).
- **j** rescue: `rescue=0` for every bot; the five-minute rule never fired.
- **k** `tb perf`: 736,199 ticks, module (sessions + engines) average **27 us**, worst 949 ms (one engine-start tick covering
  three logins), 3 bots; no crash, no `ASSERTION FAILED`, clean shutdown.

Open points:

- **Food**: see d. The `food` strategy is in the kit, but the live item pool has no usable low-level food; the bot waits for
  regen, which is what the spec allows ("without food it waits for regen"). Adding real food item data is the remaining live
  check for "eats/drinks".
- **Gear**: the level-10 warrior ran on creation gear; `.tb equip` refused the test items (`EQUIP_ERR_NOT_EQUIPPABLE`, then
  `EQUIP_ERR_NOT_IN_COMBAT` while fighting), so the 10 kills came from a naked warrior fighting level 8-9 mobs. W6a (auto-equip)
  is the slice that makes this better; W1 does not touch item value.
- **Spec deviations**: level 10 (the brief) instead of the spec's 12; Durotar (the brief allows it) and an orc, because Northshire
  is grey to a level 10; the setup teleport happens after login (`.tb tele` addresses a host bot only) and shows as one `tele`.
- **U1/U2**: no null-master dereference was seen; `IsFreeBot` flipped only the intended gates (idle fallback, elite camp, the
  sub-10 death-count healer skip, loot quickening). The unexpected gate was `can move around`, now registered.

## 7. Review fix round (free vs. commanded, and what a free bot may pull)

Two high findings from the Codex review of the W1 diff, both fixed in place.

**A master turns free mode off.** A bot has one identity. `BotMgr::BindMaster` is now the one binding step: a non-empty master
clears `free`, resets the ghost bookkeeping, and (on a live brain) calls the new `engine::BindMaster`, which takes the master
pointer, runs the full reset the engine's own adoption path runs (travel/grind targets purged, the current path stopped) and
rebuilds the commanded kit. Every binding path lands there: `.tb group` (`GroupWith`), the commander binding, a run and the
engine's adoption (`SetMaster`, through `BindBotMaster`). Free can only come back through an explicit `.tb free <bot> on`,
which still refuses a bot that has a master or a party.

**A free bot only starts fights it is allowed to start.** New pure decision `policy/world/FreeGrindPolicy.h`
(`FreeGrindFacts` -> `FreeGrindRefusal`); `GrindTargetValue` fills the facts from the unit and asks it in both new-pull paths
(the possible-targets pick and the idle fallback). Refused: players, friendly units, player-controlled units (pets, guardians,
totems), quest givers, vendors, trainers, flight masters, innkeepers, guards, civilians, kills that lower a standing, and mobs
another player tapped. Self-defence is unchanged: the attackers loop of `FindTargetForGrinding` runs first and answers anything
already fighting the bot or its group.

Tests (all fail on the W1 code before this round):

| Test | Before |
|---|---|
| `forever/tools/test_w1_wiring.py`, `FreeAndCommanded.*` (4) | `GroupWith`/`SetMaster` set the master directly; no `BotMgr::BindMaster` |
| `forever/tools/test_w1_wiring.py`, `FreeGrindTargets.*` (3) | no free rule in `GrindTargetValue`; the facts are not read |
| `forever/tools/test_free_grind_policy.cpp` (50 checks) | the header does not exist, so it does not compile |

The scene in the standalone policy test includes a player and a quest giver in range next to an ordinary mob: only the mob is
picked.

## 8. Fix round 2 (the free rule stays ForeverCore's; the Turtle corpse run keeps its own answers)

Two points from the lead.

**The free-bot target rule is ForeverCore's.** On Turtle `RandomBotFacade::IsFreeBot` is true for every pool random bot and every
free alt (`runtime/PlayerbotRuntimeFacade.cpp`), and those bots may today pick an opposing-faction player or another NPC. Asking
`FreeGrindPolicy.h` for them would change what the pool fights. The W1 compat pair therefore carries the gate,
`FreeBotStartFightRuleCompat()`: `ai/cmangos-compat-w1.h` answers false (the scan's original answer; no fact is read), the
ForeverCore side answers true. Both new-pull paths test it first, before `FreeBotMayStartFight`. Proof:
`tools/test_free_grind_target_wiring.py` pins the Turtle false, the Forever true and the gate-before-rule order in both paths,
and runs in the default `verify_all` unit-test discovery.

**The four `verify_all` failures and the one error were W1's.** A throwaway detached worktree of `tbf/main` (6e8e0494a) runs the
same three modules green (`Ran 30 tests ... OK (skipped=4)`), so W1 broke them:

| Failing pin | Cause | Fix |
|---|---|---|
| `test_cast_cancel_wiring` (`"move_revive"` 5 != 3) | W1 added two healer walks labelled `move_revive` | the two new walks are `move_healer`; the three legacy sites keep their label |
| `test_bot_revive_door_wiring` (`return atEntry->id;`, `dungeonMap->ghostEntranceMap`) | W1 reads the trigger id and the entrance map through the compat pair | the pin now asserts `AreaTriggerIdCompat` / `MapGhostEntranceMapCompat` and the Turtle shim answering `entry->id` / `entry->ghostEntranceMap` |
| `test_corpse_entrance_wiring` (`at->destination.mapId/.x/.y/.z`, `atEntry->mapid`, `corpseMap->ghostEntranceMap`, the timeout's first line) | the same move behind the pair, including the ForeverCore-only timeout gate | the pins assert the compat-mediated source and the Turtle shim's `destination.*` / `ghostEntranceMap` / `CorpseRunTeleportsEnabledCompat` true |

Both entrance teleports also passed orientation 0 where the Turtle code passed `destination.o` (a genuine port slip): new
`AreaTeleportOrientationCompat` (Turtle `destination.o`, ForeverCore `Loc.GetOrientation()`) feeds `TeleportTo`.

**The Turtle build was failing on W1's own helpers.** `vm-build ~/wt-w1 --wait` at b2fd98ec3 failed in five translation units:
`ai/cmangos-compat-w1.h` passed the graveyard team as `uint32` where the Turtle core's `GetClosestGraveYard` takes `Team` (now
`Team(team)`), the same pair read `WorldSafeLocsEntry::id` (the core spells it `ID`; the helper had no caller, so it is gone),
and `DeadValues.cpp` built `GuidPosition(ObjectGuid::Empty, ...)` (the Turtle core has no `ObjectGuid::Empty`; both cores have
the default constructor, now `ObjectGuid()`). The Turtle module then compiles: `COMPILE_SECONDS=146 CMAKE_RC=0 MAKE_RC=0`,
`mangosd` and `realmd` written.

| Gate | Result |
|---|---|
| Census `forever/SLICE-W1.txt`, slot 8 | `files 270  compile clean 270  with errors 0  error places 0  includes of a header ForeverCore does not have 0` |
| Generator / registry | `slice_gen_context.py --check` up to date; `slice_registry_check.py` (a) = 0 |
| Policy tests | `test_free_grind_policy` 50 checks PASS |
| Wiring | `forever/tools/test_w1_wiring.py` 22 tests OK; `tools/test_free_grind_target_wiring.py` 2 OK; the three Turtle pins above green |
| `tools/verify_all.sh` | unit discovery `Ran 2673 tests ... OK (skipped=19)`; the local host-contract step fails on `../tortoise-wow` (that checkout is on another branch and has no `src/game/SessionTransport.h`) exactly as on `tbf/main`, and the DBC step is skipped (no `../tortoise-docker/data/dbc` here); the same script on the VM is `vm-test tb` below |
| Module ON / OFF (`FOREVER_SLOT=8`, `tortoisebots`) | `CMAKE_RC=0 MAKE_RC=0` both (slot 8; build.sh's trailing error grep exits 1 when it finds none) |
| Turtle `vm-build` / `vm-test tb` | `vm-build ~/wt-w1 --wait`: `COMPILE_SECONDS=146 CMAKE_RC=0 MAKE_RC=0`, `mangosd` and `realmd` written, job rc=0; `vm-test tb ~/wt-w1`: `RESULT: PASS kind=tb dirty=11` |

## 9. Integration into main (batch 6, core pin 25fc7ca), 2026-11-20

`integ/batch-6` (off `tbf/main` 6e8e0494a, the batch-5 merge) merges `tbf/world/w1` (4 commits, `c6ff3dc08`) into a tree on
the new pin `25fc7ca3a68b`: **no conflict in any file**. W1's list is the union (`forever/SLICE1..SLICE11.txt` are subsets of
`forever/SLICE-W1.txt`, 270 files), so it stays the list CMake and the registry read; the key lists union (`W1_KEYS` kept,
`slice_registry_check.py` (a)=0, (b) 218 against main's 224, W1 registering six keys); the `W1 (world life)` block in
`SliceLinkStubs.cpp` and the `TbCompat_W1.h` + `cmangos-compat-w1.h` pair are W1's own; `TbDashboard.*` and
`tools/observability/` are main's, file for file; every doc keeps both sides (`TB-COMMANDS.md`, `UNPORTED.md`, `CENSUS.md`).

**25fc7ca causes no compile break in the module**, so this merge carries no fix commit: the tree built and linked ON and OFF on
the new pin at the first attempt, and the census of the 270-file list is clean without a code change. The core is byte-for-byte
the pinned worktree (file-hash list of `src`, `sql` and the module excluded: equal).

| Gate (slot 32, core 25fc7ca; slot 3 was in use by another worker, so this batch took a private core copy) | Result |
|---|---|
| Module OFF (`FOREVER_SLOT=32 build.sh 14 tortoisebots`) | `CMAKE_RC=0 MAKE_RC=0 TOTAL_SECONDS=28` |
| Worldserver ON (`FOREVER_CMAKE_EXTRA=-DTORTOISEBOTS_SLICE1=ON build.sh 14 worldserver`, full core + module) | `CMAKE_RC=0 MAKE_RC=0 TOTAL_SECONDS=58`, `bin/Release/bin/worldserver` 72,052,480 bytes, 0 `error:`, 0 `undefined`; `strings` carries `[W1] death bot=`, `Loading free bots.`, `` `.tb free <bot> on\|off` `` |
| Census `forever/SLICE-W1.txt` (270) | `files 270  compile clean 270  with errors 0  error places 0 (in 0 files)  includes of a header ForeverCore does not have 0` (`census-runs/integ6-32-SLICE-W1`) |
| Census `forever/SLICE11.txt` (263) | `files 263  compile clean 263  with errors 0  error places 0 (in 0 files)  includes of a header ForeverCore does not have 0` (`census-runs/integ6-32-SLICE11`) |
| `slice_gen_context.py --check` / `slice_registry_check.py` | `SliceAiObjectContext.cpp is up to date` / `(a)=0  (b)=218` |
| Policy tests, standalone (`policy-integ6.sh`: 478 of `clean_tests.txt` + the three W1 ones) | `475 PASS, 4 COMPILE_FAIL, 2 RUN_FAIL`; the six are main's (`test_combat_monitor_service`, `_race`, `test_decision_capture_files`, `test_run_trace_writer`; `test_log_file_rotation`, `test_decision_capture_codec`) |
| `test_free_grind_policy` / `test_free_kit_policy` / `test_ghost_rescue_policy` | 50 / 72 / 19 checks PASS |
| Run/class/policy C++ tests (`gate-forever.sh`) | 13 PASS, 0 FAIL (`test_run_orders` 209 checks, `test_run_c6` 28, `test_run_cancel`, `test_run_liquid`, `test_run_facts`, `test_run_facts_s9` 42, `test_run_tasks` 148, `test_run_gather` 21, `test_cc_order` 33, `test_slice10_roles` 28, `test_tb_self_target` 37, `cmd_binding_check` 57, `dash_runs_check`) |
| `forever/tools/test_w1_wiring.py` / wiring discovery / `test_forever_no_direct_queries.py` | 22 tests OK / `Ran 149 tests ... OK (skipped=6)` / 5 tests OK |
| `tools/verify_all.sh` (laptop) | `Ran 2673 tests ... OK (skipped=19)` (main: `Ran 2671 ... OK (skipped=19)`), `482 standalone policy tests verified` (main: 482); the host-contract step fails on `../tortoise-wow` exactly as on `tbf/main` (that checkout is on another branch), the DBC step is skipped: no new failure |
| Turtle `vm-build` / `vm-test tb` | `vm-build ~/wt-integ6 --wait` rc=0: `COMPILE_SECONDS=9 CMAKE_RC=0 MAKE_RC=0`, `mangosd` and `realmd` written, module-tree-hash 32c7a70b7e4ac2d7, tree sha 93218437d dirty=0; `vm-test tb ~/wt-integ6`: `RESULT: PASS kind=tb` (rc=0, 1220 s, sha 93218437d dirty=0; unit discovery `Ran 2673 tests ... OK`, `482 standalone policy tests verified`, config defaults and usage verified) |

## 10. Live on the new pin (run2, 2026-11-20)

Server `~/forever-run2` (core 25fc7ca binary from the slot-32 ON build; run2's `sqlsrc` checked out at 25fc7ca for the run
(the new binary needs the new core's `sql/`, PIN-MOVE-25FC7CA-NOTES section 9) and `world.sh update` applied its files; the four
databases dumped to `/mnt/forever/pin-move-dump/run2-pre-integ6-dump.sql.gz` before the first start, then re-imported and the
old binary (`worldserver.pre-integ6`, `cmp` identical) put back). Boot clean: `ready...`, `grep -c 'ASSERT|Segmentation|Aborted'`
= 0, `tb status` "enabled, 0 bot session(s)", the only new lines are the two config notices for batch 5's keys.

The scenario: `Mhol` (human warrior, level 20, `.tb gm on`) stands next to Rezlak the quest giver in Durotar
(999.8, -4414.4, 14.5); `Pmob` (human warrior, level 20, no GM flag — a plain enemy player for the bot) 2 yd from it; `Wint`
(orc warrior, `tb login` + `tb level 10` + `tb train`, then `tb free on`) at (1010, -4400, 17), 15-30 yd from all three, among
the Elder Mottled Boar spawns (entry 3100). Console only, no client. `AiPlayerbot.AllowedLogFiles` gained `grind_target.csv`
for the window and was restored.

| Check | Result |
|---|---|
| Free mode | `free=yes`, `nc=arms;buff;default;food;free;grind;loot;roll`, `engine=yes` (a masterless bot runs its own kit) |
| It grinds, 10 minutes (15:28:54-15:38:59 UTC) | `kills` 0 -> **4**, `deaths` **3**, `xp` **272**/7600, `bags` 18 -> 17, `dur` 76, `tele` 4 (the setup + three core release teleports), `rescue` 0; the target picks name level 8-9 boars and scorpids (`grind=3100:8:..`, then `3122:8:..`), never a grey and never above +4 |
| Never the quest giver | `grind_target.csv`: 141 x `Rezlak` (entry 3293, friendly=1), every one `ignored (free bot: quest giver, vendor, trainer, flight master or innkeeper).` |
| Never the player | 98 x `Pmob` (entry 0, hostile=1), every one `ignored (free bot: player).` |
| Never the GM character | `Mhol` never appears in the candidate log (a GM-flagged unit is not in the bot's world for targeting), `hp=449/449`, `deaths=0`, `kills=0` over the whole window |
| Corpse run | all three deaths: `[W1] death bot=Wint level=10` then `[W1] revive bot=Wint how=reclaim secs=28 yd=18.3`, `secs=33 yd=7.4`, `secs=11 yd=37.1` (no healer leg, no rescue) |
| Rest and eat | `hp` 92 -> 171 -> 241 with `resting=yes rest_why=hp;eating` and then `eating=no resting=no`; food came from loot this time |
| A master ends free mode | `.tb group Wint Mhol` -> `BotMgr: Wint is no longer a free bot (a master is bound)`, `Wint joined the party of Mhol`; `state` then `free=no`, `dm=154.9`, `nc=arms;default;dps assist;follow;food;loot;roll` (no `grind`, no `free`), `engine=yes` |
| Tick cost | `tb perf` 289,598 ticks; module (sessions + engines) 6 us average, 176 ms worst; 3 bot sessions; no crash, no ASSERT |

Note on the kill rate: 4 kills and 3 deaths in the 10 minutes, against 10 kills and 1 death in W1's own 12-minute window. The spot
is not the same one (Rezlak's cluster holds 7 boar spawns within 120 yd against the spawn field of `game_tele` 308) and the bot
fights on its creation `Battered Axe`; it lost the first exchanges after the mob respawns closed in, then killed steadily. Nothing
in the run suggests a code difference: the free pick, the kit, the corpse run and the master handover all answered as W1's own live
section records.

Restore: worldserver stopped, `bin/worldserver` back from `worldserver.pre-integ6` (`cmp` identical), `sqlsrc` back at 6517af05,
`aiplayerbot.conf` back from its `.pre-integ6` copy, the four databases dropped and re-imported from the dump (no
`auctionhousebot*` table, 28 characters as before); run2 booted again on the old core (`ready...`, no ASSERT, `tb status`
answers) and was stopped.
