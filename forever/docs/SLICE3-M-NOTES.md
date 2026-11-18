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
