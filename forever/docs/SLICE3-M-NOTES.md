# Slice 3, package M: merge, link, boot

Branch `slice3/integ` (worktree `~/wt-slice3-integ`), build slot 2 (`FOREVER_SLOT=2`), started at `slice3/p`, merged `slice3/h`. Core read only at the pin 6517af05. Not pushed.
Boot driver: `~/forever-build/m3-sess.sh` on the build VM (copy kept in this file's section 9); my binary is `~/forever-build/m-on/worldserver`.

## 1. Merge and the two NEEDS lists

`slice3/h` merged with `--no-ff` without a conflict (the host worker only added single lines to existing files, the porting worker owns `forever/src/slice/*` and the compat pair).

NEEDS FROM host (porting notes section 9), each checked in the merged tree:

| Need | Result |
|---|---|
| `IsInCommandedGroup` true for a bound bot | `TbHostOwnership.cpp:161` answers `h3::IsCommandedBot` (the `.tb master` binding while BotMgr still names that commander). TRUE at the first boot. But see section 6: the engine's adoption step took the binding away from four of five bots on the first tick (fixed here) |
| `NoteHoldOrder` stand-in | the host does not define it: the stand-in in `SliceLinkStubs.cpp:505` stays (counted under `player summon request`, "does less") |
| `BOT_TEXT` answers empty, `TellPlayerNoFacing` | `PlayerbotAI::TellPlayerNoFacing` now returns on an empty text (`ai/playerbot/PlayerbotAIChat.cpp`), like `SayToGuild`/`Whisper`/`TellMaster` next to it. Turtle text is never empty, so no Turtle change |
| the Event owner for the shortcuts | the host passes the commander (or the bot itself) as `Event` owner (`TbOrders.cpp` `Run`) |

NEEDS FROM porting (host notes section 9):

| Need | Result |
|---|---|
| actions `follow chat shortcut`, `stay chat shortcut`, `follow target`, `attack rti target`, `pull my target`, `end pull`, `attack my target`, strategy `passive`, values `rti`, `rti target`, `explicit attack target`, `position`, `last movement`, `master position` | all registered (SLICE3-KEYS.md; `slice_gen_context.py --check` current). Live: every order verb answered `status=queued` (no `action-not-registered`) |
| factory reads the stored forced role | yes (`OrderedRole`); live `.tb state` shows `role=tank/healer/dps rolesrc=forced` and the kits as the spec says |
| `threat` where `KeepsPartyThreatBackoff` says | see section 6 |

## 2. CMake

`TORTOISEBOTS_SLICE1` keeps its name and reads `forever/SLICE3.txt` (170 files). Option-ON `worldserver` linked at the first try (`CMAKE_RC=0 MAKE_RC=0`, 0 undefined symbols): the porting worker's three new stand-ins were enough. **No link stand-in was added by this package.**

## 3. Option-OFF object list

The default build now holds 7 objects: the 6 of slice 1 (`TbBotMgr TbBotSession TbScripts TbItemPrototype TbSpellEntry TbEngineBrain`) plus ONE stub, `host/TbH3Off.cpp`, which answers `error=engine-not-built cmd=<name> msg=... [#token]` for the nine commands. `TbH3.cpp`, `TbOrders.cpp`, `TbPartyState.cpp`, `TbTestKit.cpp` are filtered out of the OFF build and `TbH3Off.cpp` out of the ON build (CMakeLists.txt). `ar t libtortoisebots.a` at the final commit is in section 8.

## 4. Link stand-ins

None added (section 2).

## 5. Boot sessions (build VM `~/forever-run`, binary `~/forever-build/m-on/worldserver`, boots used: 3 of 8)

Driver `~/forever-build/m3-sess.sh` (check / start [gdb] / ready / `c "<line>" [s]` / log / stop): records the id of the container IT started and refuses every console line, and the stop, when `forever-world` is not that container; it keeps the other worker's binary as `bin/worldserver.pre-m3` and puts it back on stop. The server was free at every start (one wait of 5 minutes after boot 1, when the other worker's server was up). Characters: `Mtank` (warrior, tank), `Mpriest`, `Mdpsa..c` (warriors), `Mcmd` (priest, the commander, NOT in the party), all level 18 on account 1; two `game_tele` rows (`MPackAWest`, `MPackBWest`, deleted afterwards) because `.tele` has no coordinates form and `.tb hop` keeps the old z.
* Boot 1: setup, `.tb train`, `.tb learn`, `.tb itemscan`, `.tb equip`; found two bugs (section 6), stopped to rebuild.
* Boot 2: the party and the kits; ended by **"World Thread hangs for 65894 ms, forcing a crash!"** while the VM's load average was 120 (my own Turtle `vm-build` plus other workers' Go and C++ builds; `ssh` and `docker ps` timed out for 30 s at the same time). The last console line before it was `.tb hurt Mcmd 100`. Treated as starvation, not a module hang: boot 3 ran the same sequence under the gdb image at load 15-30 with no hang. NOT proven (boot 3 never reached the load of boot 2).
* Boot 3 (gdb image, 40 minutes): all of section 7.

## 6. Bugs found and fixed here

| # | What | Fix |
|---|---|---|
| M1 | `.tb equip <bot> <item> replace` never replaced a one-hand weapon, a shield or a robe: with swap=false the core answers a taken slot with `EQUIP_ERR_NOT_EQUIPPABLE` (20; `Player.cpp:11821`, `FindEquipSlot` returns NULL_SLOT) as well as `NO_SLOT_AVAILABLE` (9; `:11831`), and the host only took 9 as "taken" | `TbTestKit.cpp`: `replace` also on 20; the second check with swap=true names the real refusal. Live: Wingblade, Marbled Buckler, Zealot's Robe, a two-hander over main+off hand: `result=equipped replaced=yes` |
| M2 | **The commander does NOT survive the master rule for four of five bots** (the spec said it does): `IsInCommandedGroup` was true, so the strip rule (`PlayerbotAI.cpp:3392`) skipped, but the adoption step (`:3441-3505`) still ran (`!master \|\| !HasActivePlayerMaster()`: a bot-session commander is no network master), took `GetGroupMaster()` = the tank leader, and the check `!(commanded && IsRealPlayer(newMaster))` does not stop a BOT leader. Seen: `master=Mtank commanded=no` for the priest and the damage warriors, no `threat` strategy | new both-core helper `CommandedBotKeepsMasterCompat(commanded)` (`TbCompat_S3.h` F: `commanded`; `ai/cmangos-compat-s3.h` T: `false`) added to that condition in `PlayerbotAI.cpp`. Turtle: unchanged (needs a Turtle `vm-build`/`vm-test`: file outside `forever/`). Live afterwards: all five `master=Mcmd commanded=yes`, `threat` on the four non-tanks |
| M3 | `TellPlayerNoFacing` on an empty text | section 1 |

A log line `slice: threat back-off for <bot>: commanded=.. tank=..` stays in `SliceAiFactory.cpp` (one line per kit build).

## 7. The host worker's "not verified without a boot" list

| Item | Result |
|---|---|
| the nine rows answer, replies carry `#token`, the `-end` lines | verified (console). GM chat sender: NOT RUN (needs a network client) |
| console and the order queue; `ord=<seq>:ok` | verified for follow, stay, come, attack, pull, hold, free, co (`+threat`), assist (refuses `nothing-to-assist` when the member has no victim), endpull (`fail:end-pull` with no pull running), `attack my` from the console (`needs-player-sender`), `attack spawn:<bad>` (`no-such-spawn`) |
| commander survives the master rule | only after M2 (section 6) |
| marks restored after `attack` | NOT RUN (nothing prints the group's marks) |
| `come` | verified: all five within 1 yd of the commander in 16 s (priest, tank) and 32 s (three damage warriors, who had to go around a cliff 50 yd above him). The `too-far` refusal side: NOT verified (the bot I sent to Lakeshire was teleported next to the commander by the host's own catch-up before the order ran) |
| `stay` | verified: after `stay` nobody moved while the commander walked 30 yd |
| `follow` | the order runs, but see section 8 item 1: it follows the tank, and the tank follows no one |
| `equip replace` | M1 |
| `learn` 71/355/7386 | verified on a fresh warrior (`result=learned`). On the trained bots `.tb train` had ALREADY given them: 71, 355 and 7386 come from the class skill-line pass of `.tb train` (a fresh warrior knows none; the data note says "no learn path"; the pass finds one), `.tb learn` answered `known`. Two-hander proficiencies 202/199/197 were also missing: `.tb learn` grants them |
| a real `resting` cycle | drinking verified: `rest_why=mana+drinking`, `drinking=yes`, resting=0 after. Eating: NOT verified (section 8 item 4) |
| `itemscan` / gear ids | all from `gear-level18.csv`, each confirmed by `.tb itemscan` (`canequip=yes`): Wingblade 6504, Marbled Buckler 6725, Fire Hardened Hauberk 6972, Whirlwind Sword 6977, Zealot's Robe 17043; bread 4540 and water 159 |
| line length | real `state` lines are about 600-700 characters |

### U1 (the tank kit on this core): PASS
* Defensive Stance: `.tb use Mtank 4540` prints the aura list: `auras: 71 433 1243 ...` (71 present; a damage warrior shows 2457 Battle Stance). `Charge` is refused `result=115` (`SPELL_FAILED_ONLY_SHAPESHIFT`), `A:defensive stance - USELESS` (already in it). No stance-change action ran in boot 3's log: the engine's stance handling puts it there without an `A:` line.
* Over two pulls (Mtank action log, `CAST_OK`): Taunt 11, Sunder Armor 5, Revenge 5, Demoralizing Shout 5, Shield Block 3, Thunder Clap 1, Shield Bash 1, Bloodrage 2, Last Stand 1; `tank face away - OK` 6. Thunder Clap was refused `result=108` (`SPELL_FAILED_NO_POWER`, rage) 1,600 times before its one cast; Shield Block's shield requirement holds (a shield was worn).

### Smoke pulls (Redridge, console commander 40 yd away, party at z 136 above ground 128)
* Pull 1, pack A (`.tb order Mtank pull spawn:20010110`): the party fought 7 mobs (3 brutes + the Mystic + a second group the pull chained), **the priest died** at 00:09:32 after standing as the victim of one brute for ~25 s (tank threat on it 0; the tank targeted other mobs). Three state lines: `00:08:42 tank:combat/80% priest:combat/100% ... MOB brute victim=Mtank top=Mpriest:3,Mtank:0`; `00:08:46 ... MOB 24 hp15 victim=Mtank top=Mtank:343,Mdpsb:338,Mdpsc:196`; `00:09:20 MOB 23 hp94 victim=Mtank top=Mpriest:99,Mtank:43,Mdpsa:41`. Tank hp low point 23%.
* Pull 2, pack B (3 brutes, `Mdpsa attack` ordered on the 3rd 5 s after the pull): no deaths; tank low point 52%, priest never below 100%; dps `tgt` equalled the tank's victim in most samples. `00:15:54 MOB 5D victim=Mdpsa top=Mdpsa:260` (the ordered attack), taunt `CAST_OK` at 00:15:55.259, the mob's victim was still Mdpsa at 00:15:56 and 00:15:58 and the tank again at 00:16:00 (about 4.7 s after the taunt: the 3 s gate is NOT met in this run; `MULT pull x0.010` was scaling the taunt for a moment). `high threat` vetoes were logged (`A:overpower - MULT threat x0.000`).
* d (nobody before the tank): FAILED in both pulls. The tank needed 7 s (pack A) and 12 s (pack B) to reach melee; the party was in combat at 00:08:39 (A) / 00:15:54 (B) as soon as the mobs aggroed, the damage warriors' `reach melee`/Overpower/Rend began 1-3 s before the tank's first Sunder/Thunder Clap/melee (pack A: dps Overpower 00:08:42.7-44.06, tank Sunder 00:08:44.2, first melee 00:08:46.6; pack B: dps 00:15:54.3-54.5, tank Thunder Clap 00:15:57.95, melee 00:15:59.5). Not investigated further (needs the hold window, `PausePartyPullHold`, to be read against an aggro that starts before the pull lands).
* After the pack: the priest heals the party back within 20 s; no other order was needed. Idle mobs of pack C aggroed the standing party twice and the damage warriors killed them without the tank (not a rule failure: they were attacked).

### Perf (`.tb perf`, a rolling window, not cumulative)
5 engines idle: module average 71-96 us (4,182-157,321 ticks), worst 1.2-139 ms; during a fight: 192 us (74,743 ticks). The slice-2 figure was 44 us with 3 sessions, the gate is 50 us. **Over the gate.** Suspect: `AiPlayerbot.EnableActionLog = 1` with `ActionLogScope = commanded` (the config default): bound bots now count as commanded, so each engine writes its tick log (the Mdpsb log reached 219,000 lines in the boot, 48,000 lines for the tank in one 60 s fight). The shared server's `aiplayerbot.conf` was not changed; the live-test package should repeat the measurement with `AiPlayerbot.EnableActionLog = 0` (or scope off) and the load of the VM noted (14-30 during these readings).

## 8. What the live-test package must look at first
1. **`follow` follows the tank, and the tank follows nobody.** `FollowTargetValue` reads `master target` = `PlayerbotAI::GetGroupMaster()` (PlayerbotAI.cpp:301), which returns the group leader when no real player is IN the group (the commander is outside it), so the tank's follow target is itself (`follow target: Mtank`). The spec's observation b ("every bot within 8 yd of the commander") and the claim "follow follows the commander" do not hold for the tank; the others follow the tank (they were seen walking to the commander only because the tank did, after `come`). `come` is the working order for "all to the commander's spot". This is a design question for the lead (either the tank follows the commander through a new host rule, or b is reworded), not a port bug; nothing changed here.
2. The pull hold (observation d) and the taunt gate (f) above; threat on the first seconds (`top=Mpriest:3,Mtank:0`).
3. Priest death in pack A (the priest's healing was fine; a brute hit her for 25 s with no taunt: `tank assist`/`lose aggro` picked other mobs).
4. `A:food - FAILED src=low health` for the tank and a damage warrior with 20 Tough Hunk of Bread each (`.tb use <bot> 4540` works; the engine's `food` action finds its item by `FindFoodVisitor(category 11)` over `ItemPrototype::Spells[0].SpellCategory`: check what the item mirror holds for food; drink, category 59, works). The priest also logged `A:drink - FAILED` while casting heals, then `OK`.
5. Config noise: `TortoiseBots.Slice.RestHealthPct` / `RestManaPct` / `MasterGraceSeconds` are not in the server's `worldserver.conf.d/tortoisebots.conf`: every `.tb state` prints two "Missing name" errors (defaults are used). Copy the new keys from `tortoisebots.conf.dist` at the next `setup.sh`.
6. Missing keys seen at boot (explained): the slice-2 list (packet triggers of `default`, quest/loot-roll/taxi/trade names), plus `dps` (strategy), `has blessing of salvation` and `has greater blessing of salvation` (ThreatStrategy's triggers for a paladin buff the party never has), `party member dead`, `resurrect`, `incoming heals`, `apply oil`, `check mail`, `check mount state`, `check values`, `outfit list`. None blocked a fight.
7. The spec's `stuck=nomove` and the 250-character state line cap were not built (host notes section 7).
8. The world-thread hang of boot 2 (section 5): re-run a boot at low VM load.

## 9. Gates (final commit)
Filled in at the end of this file.
