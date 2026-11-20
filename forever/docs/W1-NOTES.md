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
| Policy tests (standalone, g++ C++17) | `test_ghost_rescue_policy` 19 checks PASS; `test_free_kit_policy` 72 checks PASS |
| Wiring | `python3 -m unittest forever/tools/test_w1_wiring.py` 15 tests OK; `test_slice9_l9_wiring.py` 11 OK (its list pointer moved to SLICE-W1.txt) |

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
