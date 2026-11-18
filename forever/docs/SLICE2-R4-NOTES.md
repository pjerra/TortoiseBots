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
