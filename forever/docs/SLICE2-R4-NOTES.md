# Slice 2, package R4: the live test (a priest heals, hears failed casts, drinks; the party loots money)

Branch `slice2/integ` (worktree `~/wt-slice2-integ`), build slot 1. T = Turtle core, F = pinned ForeverCore (6517af05). Server: the build VM's `~/forever-run`, started
only through `world-gdb.sh start` (the same container with gdb in batch mode), stopped with `world.sh stop`. Binary: the slot-1 option-ON build of the merge commit
(`~/forever-build/build-1/bin/Release/bin/worldserver`, copied over `~/forever-run/bin/worldserver`). Evidence lines: the console output (`logs/world.out`) and the
engine's action log (`logs/bots/<bot>_*.log`, `AiPlayerbot.ActionLogScope = all` in the VM's copy of the conf; the committed `.dist` keeps `commanded`). Times are the
VM's clock (UTC) on 2026-11-18, taken in the same script that sent the command, so they can be subtracted.

Cast: master **Tbbeta** (level 10 priest, a spike bot, NO engine), warrior **Tbalpha** (level 10, `.tb level` + `.tb train`: Heroic Strike, Rend, Execute, Bloodrage,
Battle Shout), priest **Tbhealer** (level 10, `.tb level` + `.tb train`: Lesser Heal 2050/2052/2053, Smite 585, Power Word: Shield, Renew, Mind Blast, Shadow Word: Pain), all in
one party led by Tbbeta (`.tb login <bot> Tbbeta`), engines on for both bots; for the perf and idle runs a second priest **Tbpriest** was the third engine. The place: the
kobolds of Northshire (map 0, about -8700 -120). Boot 1 only so far (see the end).

## Step 0: slice1/integ merged

`git merge --no-ff slice1/integ` (ac3bba771, R9 to R11). Three conflicts, no more:

| File | Conflict | Resolution |
|---|---|---|
| `forever/src/TbBotSession.h` | include block: slice 2 added `<atomic> <cstdint> <deque> <mutex>` (cast-result queue), slice 1 added `<functional>` | both kept |
| `forever/src/host/TbEngineBrain.cpp` | `struct Brain`: slice 2 added `BotSession* session`, slice 1 added `uint32 teleportAcks` | both members kept |
| `forever/src/slice/SliceAiObjectContext.cpp` (GENERATED) | both sides regenerated | not hand merged: `slice_gen_context.py` had merged by itself (slice 1's new key `future travel destinations` came in with the key list), then regenerated: `8 strategies, 23 actions, 56 triggers, 167 values` (slice 2 alone was 166); `--check`: up to date |

`forever/SLICE1.txt` did not change on slice 1 after the fork and `forever/SLICE2.txt` still holds every SLICE1 file (checked with `comm`); `UNPORTED.md` merged by git. After the
merge: census `SLICE2.txt` 160 files, 0 error places; `SLICE1.txt` 133 files, 0; registry check `(a)=0 (b)=233`; option-ON `worldserver` `MAKE_RC=0` (454 s); boot 1: `slice: registered 8/23/56/167`,
no ASSERT, the smoke heal worked, slice 1's follow and assist worked (observations b and l). The merge changed no file outside `forever/` since 97718266d (`git diff --stat 97718266d HEAD -- . ':!forever'` is empty).

## Boot 1 (binary of the merge commit; before the rank fix of section "Port bugs"). Observations, evidence lines

Helpers (scratchpad scripts, not committed): `tbc <s> <console command>` (one console command through `world.sh cmd`, prints the new `world.out` lines and the VM time of
delivery), `blog <bot>` (greps the bot's newest action log), `gtest2`/`htest`/`jtest2`/`healtest`/`midcast` (one VM-side script each: send a command, wait for the exact line of the
action log that must start the next step, sample every 0.4 s), `nrun.sh` (the 20-minute run). Times below are VM UTC.

### a. Boot: PASS
`slice: registered 8/23/56/167 (strategies/actions/triggers/values)` on the first `.tb engine Tbalpha on`; no `ASSERT`/`Segmentation`/`SIGSEGV` in `world.out` for the whole boot (0, checked at the end);
`.tb spells Tbhealer`: 68 spells, `2052:Lesser Heal`, `2053:Lesser Heal`, `585:Smite`, `591` (listed with the others), `17:Power Word: Shield`, `139:Renew`, `8092:Mind Blast`. The server exited with `Inferior 1 exited normally`.

### b. Follow: PASS (recorded)
`tb go Tbbeta 15 0` then 6 s: Tbhealer 3.0 yd and Tbalpha 2.7 yd from the master (`motion type: 14`); second walk (-8 -12): priest 3.9 yd, warrior 3.9 yd. A ranged kit stands where the follow rule puts it: after the long walks
of g and j it stopped 5.8 to 5.9 yd away (the follow trigger's own range, the same value the shared code uses on Turtle).

### c. Heal out of combat: PASS
`tb hurt Tbbeta 40` (VM 21:11:28): `PUSH:lesser heal on party ... src=party member medium health`, `[CAST_START] spell=Lesser Heal(2050) ... castTime=1500ms`, `[CAST_OK] result=0` at 21:11:31.559; `tb where Tbbeta` at +5 s: 224/227.
Second try, `healtest Tbbeta 15` (hurt 21:27:21.874, master 34/227): Power Word: Shield 22.102, Lesser Heal started 23.604, 25.105, 26.606 (three casts back to back, each `CAST_OK`), samples every 0.4 s: 34 -> 40 -> 101 -> 155 -> 223 -> 227 (above 90 % after about 6.5 s). No `SPELL_FAILURE` in the chain.

### d. Heal in a fight: PASS
Fight on a Kobold Vermin (`tb attack Tbbeta`), `tb hurt Tbalpha 30` (81/271) at 21:11:55: `[CAST_START] spell=Lesser Heal(2050)` at 21:11:55.287 (the chain `A:lesser heal on party - OK src=party member low health`), five more casts to 21:12:01.296 (Renew); the warrior back to 271/271 at the end. No heal cast in any of the ~40 fights where nobody was below 90 %: the priest's casts there are `Shadow Word: Pain`, `Smite`, `Mind Blast` (observation e) and one `Power Word: Shield` on the tank at the pull (a buff trigger, not a heal).

### e. Nobody hurt: PASS
Every fight with the warrior and priest: `[CAST_START] spell=Shadow Word: Pain(589)` then `[CAST_START] spell=Smite(585) ... castTime=1500ms` + `[CAST_OK] result=0` on the master's target (`targetGuid` equal to the target of `tb attack`); `Mind Blast(8092)` when Smite is on its global cooldown.

### f. Target dies during a cast: PASS (3 of 3: two Smites, one Mind Blast)
1. 21:14:09.481 `[CAST_START] spell=Smite(585)` ... 21:14:10.983 `[SPELL_FAILURE] spell=585 reason=63` (SPELL_FAILED_INTERRUPTED; the core sends it at the end of the bar once the target is dead), next action `A:loot - OK` at 21:14:11.384 (0.4 s later).
2. 21:14:40.608 Smite ... 21:14:42.106 `[CAST_FAILED_ASYNC] spell=585 reason=13` + `[SPELL_FAILURE] spell=585 reason=13` (SPELL_FAILED_BAD_TARGETS), next action `A:loot - OK` 21:14:43.008 (0.9 s).
3. 21:23:23.628 `Mind Blast(8092)` ... 21:23:25.130 `[SPELL_FAILURE] spell=8092 reason=63`, next action `A:loot - OK` 21:23:26.032 (0.9 s).
Every failure arrives at the end of the bar (1.50 s after `CAST_START`, as the core checks the target when the cast completes); `SpellInterrupted` cleared `last spell cast` (the engine does not wait for the cast any more: next action within 1 s each time; the direct readout of the value is added to `.tb where` in the rebuild, see boot 2). About 25 fights had a kill during a bar 3 times; the other casts completed (`CAST_OK` and no failure line).

### g. A move during a heal: PASS (the cast is not cancelled; the move waits)
`gtest2`: hurt 21:24:44.235, `[CAST_START] spell=Lesser Heal(2050) castTime=1500ms` 21:24:45.867, `tb go Tbbeta 15 0` sent 21:24:45.917 (50 ms into the bar). Samples: master leaves, priest stays at (-8740.5,-118.6) for the length of the bar,
master hp 74 -> 133 between samples 3 and 4 (the heal landed at about 21:24:47.4), no `SPELL_FAILURE`/`CAST_FAILED` line, the priest moves from sample 5 (21:24:47.6) and is back at 5.9 yd (follow range) at sample 8, about 2.2 s after the bar ended (limit 6 s).

### h. Master 35 yd away and hurt: PASS
`htest 35 0`: `tb hop Tbbeta 35 0` 21:25:04.721, `tb hurt Tbbeta 30` 21:25:04.937; `[ACTION] A:reach party member to heal - OK src=party member to heal out of spell range` at 21:25:05.733 (0.8 s), the priest walks (36.9 -> 4.8 yd in 4.5 s), while it moves the cast is refused by the core's own check
(`[CAST_REFUSED] spell=lesser heal result=75` = SPELL_FAILED_MOVING, once a second), then `[CAST_START] spell=Lesser Heal(2050)` at 21:25:11.360 (6.4 s after the hurt) when it stood.

### i. Refused casts (NOT_KNOWN) per 90 s: recorded
The priest's `Heal` (level 16) and `Greater Heal` are in its chain at every heal tick and are refused by the engine's own pre-check: `[CAST_REFUSED] spell=heal result=87` and `spell=greater heal result=87` (SPELL_FAILED_NOT_KNOWN), then `A:heal on party - IMPOSSIBLE`, and the chain falls to `lesser heal`. Count in boot 1: 371 of each in the first 6 minutes of the log
(one pair per tick of a heal chain, i.e. only while somebody is hurt; none while everybody is above 90 %: the 20-minute idle run logged nothing). Same code path and same counts on Turtle (the engine's back-off is out of combat for unowned bots only, spec section 5 row 3): not a bug.

### j. Drinking through the engine: PASS
`.tb additem Tbhealer 159 5` (Refreshing Spring Water), `.tb power Tbhealer mana 10` (VM 21:26:06.526): `[ACTION] A:drink - OK src=high mana` at 21:26:07.120 (0.6 s later); mana 48 -> 57 -> 71 -> 85 -> 99 -> 114 -> 127 in 0.7 s steps (about 20 per second: the drink; the normal regeneration is about 2). Master ordered 25 yd away at 21:26:10.177: the follow
action started at 21:26:11.387 (1.2 s later), the priest left its seat (a seated bot cannot walk; the mana rate fell to about 11 per 0.7 s, regeneration while moving) and was 10.3 yd from the master at the farthest, back at 5.8 yd by 21:26:14; the drink started again by itself (`A:drink - OK` 21:26:15.012) and mana rose again at 20 per second. The sit itself was read in boot 2
(`.tb where` now prints `stand state`, boot 2 section).

### k. Money loot: PASS, and the rule on this core
Three characters, before and after kills (`kfight`: `.tb where` money before, `tb attack Tbbeta`, 12-14 s, money after; Tbbeta has NO engine, Tbalpha and Tbhealer have):
| Kill (target) | Tbbeta (master) | Tbalpha (warrior) | Tbhealer (priest) |
|---|---|---|---|
| 1 Kobold Vermin (warrior engine off) | 0 -> 0 | 8 -> 8 | 3 -> 5 |
| 2 Kobold Worker | 0 -> 0 | 8 -> 8 | 26 -> 26 (looted with the next) |
| 3 Kobold Vermin | 0 -> 0 | 8 -> 41 | 26 -> 32 |
| 4 Kobold Vermin | 0 -> 0 | 41 -> 43 | 32 -> 34 |
| 5-9 (five single kills, engines on) | 0 -> 0 each | +4, +5, +2, +4, +4 | +1, +5, +2, +4, +1 |
Each bot's own amount differs for the same corpse (4 and 1, 5 and 5, 2 and 2...), the master never gets anything, nobody's share is halved. Rows 2 and 3 show loot of an earlier corpse landing in the next window (the bots loot when the fight is over, one corpse at a time).
Reading of the core: open world, no instance: `Unit::Kill` hands every TAPPER its own `Loot` with its own `generateMoneyLoot` roll (Unit.cpp:11930-11940, `creature->m_personalLoot[tapper]`; the shared corpse of Unit.cpp:11890-11918 is only taken in a dungeon (or an encounter) with a grouped tapper:
`CONFIG_LOOT_SHARED_ENCOUNTER_LOOT`, default 1). `HandleLootMoneyOpcode` (LootHandler.cpp:142-179) splits `loot->gold` among the group members within reward distance that are ALLOWED LOOTERS OF THAT LOOT; a personal loot allows only its owner, so the "split" is by one. So: in the open world every tapper rolls and keeps its own copper (priest and warrior both tapped, each opened its own window: `A:open loot - OK`, `A:store loot - OK`);
a master that is a bot session with no engine is a tapper too (it attacked) but never opens its window: no share, nothing lost for the others. In a dungeon with a grouped tapper the corpse is shared and the copper is split among the members near the opener (the Ragefire observation at the end).
Kobold Vermin gold: `creature_template_difficulty` 1-4 copper, Kobold Worker 1-5. Items are not looted (decision D2: `ItemUsage` answers "not looted").

### l. Slice 1 b and d for the warrior with a priest in the party: PASS (3 fights)
`ltest` (attack, 2.2 s, `tb where Tbalpha`; 11 s later again): fight 1 Kobold Worker: `state: Combat`, `target reach: distance 2.17 yd, melee range 5.0, in melee range: yes`; afterwards `state: Non Combat`, `follow target: Tbbeta ... motion type: 14`. Fights 2 and 3 (Kobold Worker, Kobold Laborer) the same (0.50 and 1.04 yd).

### m. Logout orders with the priest mid-cast: PASS
`midcast` sends the command 23 ms after the priest's `[CAST_START] spell=Lesser Heal`: `tb engine Tbhealer off` (cast running), `tb logout Tbhealer` twice (bot first; relogin each time with `tb login Tbhealer Tbbeta` + engine on), `tb logout Tbbeta` (master first: `BotMgr: the master of ... is away: the bot stays and waits 300 s`, `brain: none (detached)`, `master away, 291 s left`; `tb login Tbbeta` 14 s later:
`the master ... is back: the bot follows again` for both bots, `joined the party of Tbbeta`), engine off for both bots 2 s into a fight and on again. No `ASSERT`, `Segmentation` or `SIGSEGV`, `Inferior 1 exited normally`.

### n. 20 minutes idle with three engines: PASS (boot 1)
Tbalpha, Tbhealer, Tbpriest on the engine, master Tbbeta (4 bot sessions), one `tb perf` + three `tb where` per minute for 20 minutes (`nrun.sh`, 21:32:28-21:52): `perf: ~52,000 ticks a minute; world update time avg 1.11-1.13 ms, worst 6-171 ms (a server-wide worst, mostly 6-10);
module (sessions + engines) avg 36-38 us (81 us in the first minute: the config load), worst 1-5.9 ms`; every minute all three `brain: engine`, `state: Non Combat`; errors (`ERROR`, `BOT PATH BUG`, `ASSERT`, `Segmentation`) 0; new `slice: missing` lines 0 (the 61 of the first minutes stay the same set). Limit: avg under 50 us: met (37 us).
