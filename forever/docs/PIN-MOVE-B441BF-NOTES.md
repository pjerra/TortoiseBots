# Pin move d3358c21dc -> forever-bots b441bf74e0 (0.1.39), 2026-11-21

Branch `pin/forever-bots-b441` off `tbf/main` (b37e45e61). Round 2 of the 0.1.39 move; it also closes the follow-ups of the
post-merge review of PR #119 (which pinned `forever` d3358c21dc and was merged without a review or CI run). No ForeverCore
core change: the core was read from a clean worktree (`git -C <core checkout> worktree add --detach <core-b441 worktree>
b441bf74e0`, sparse like `<core checkout>`: no `sql/`).

## 1. Why b441bf74e0 and what it is

Karrz asked for the pin to be `forever-bots` b441bf74e0 ("0.1.39: bots and core since 0.1.38"). It is `forever` d3358c21dc
plus his PlayerbotV2 module: `git diff d3358c21dc b441bf74e0` touches no file under `src/server`, `src/common` or `sql/base`;
the additions are `src/modules/PlayerbotV2/**`, `sql/playerbot_v2`, `sql/playerbot_shared`, one `command` help row under
`sql/custom`, tests and build scripts. PlayerbotV2 is built only with `-DBUILD_PLAYERBOT_V2=1` (default 0); off, it registers
nothing. So the core code this module compiles against is the one of d3358c21dc, and the #119 findings (heal-roll change,
the 0.1.39 `ScriptMgr` hooks `OnCommand`, `OnGossipSelect`, `OnItemUse`, the unchanged `Group.cpp` order that
`TbScripts.cpp:322-326` relies on) carry over unchanged.

## 2. What changed in this repository

| File | Change |
|---|---|
| `forever/PINNED_CORE` | `b441bf74e048934d4289e64cef5e49cd02b0b8c5` |
| `CMakeLists.txt` | header says branch `forever-bots`; **new guard**: `BUILD_TORTOISEBOTS` together with `BUILD_PLAYERBOT_V2` is a `message(FATAL_ERROR)` ("two bot systems must never load together") |
| `forever/tools/test_playerbot_v2_guard.py` | new: configures a stand-in core project with this repository as the module, both ways (see section 4) |
| `forever/src/compat/TbCompat_I2.h` | the stale "no ProcessItemCast script hook (OnItemUse)" comment: 0.1.39 has `PlayerScript::OnItemUse`, the port does not override it (behaviour unchanged) |
| `forever/docs/PORT-PLAN.md` | rule 3 "Re-checked at b441bf74e0" entry |

Why two bot systems must not load together: two `.bot` command sets, two session drivers on the `PlayerScript` hooks, two
group and auction-house loops. The guard stops the configure, so no such binary can be made from this module. The other half
of the plan (the `forever-package pack` refusal of a build dir with `BUILD_PLAYERBOT_V2:BOOL=ON`, a `playerbot-v2*` library
or `zz-forever-bots.conf`) lives in the packaging repository and is not part of this branch.

## 3. Cites

`forever/tools/check_core_cites.py --core <core-b441 worktree> --old d3358c21dc --new b441bf74e0 --docs`: 1 cite into 784
"changed" core files (every one of them under `src/modules/PlayerbotV2/`), same 0, MOVED 0, GONE 0. The one listed line,
`TbHostPetFactory.cpp:7` `BotSetupPipeline.cpp:735-830`, is a false hit: the comment cites Karrz's reference repository
(`<reference checkout>`), and the core now has a file of the same base name under PlayerbotV2 (`Fleet/BotSetupPipeline.cpp`). Not
applied. So the refresh moved no cite, as expected from a core with no `src/server` change, and every cite stands as at
d3358c21dc (PR #119 refreshed them).

## 4. Gates (slot 8, core b441bf74e0)

Core: `<core-b441 worktree>` (clean worktree, sparse, no `sql/`) rsynced into the VM's `src-8` only (`rsync -aL --delete
--exclude .git --exclude /src/modules/TortoiseBots`; a second `rsync -nci` lists 0 lines). Module tree: this worktree through
`slot-sync-build.sh 8 <target> -DTORTOISEBOTS_SLICE1=<OFF|ON>`. Slots 0-7 and 9+ were not touched; the shared ccache was not
cleared. The configured tree has `BUILD_PLAYERBOT_V2:BOOL=OFF` (checked in `shared/<key>/build/CMakeCache.txt`).

| Gate | Result |
|---|---|
| `test_playerbot_v2_guard.py` | 3 tests OK; with `PV2_GUARD_MUTATE=1` (guard removed) the same run FAILS (1 failure: the both-modules configure no longer stops) |
| module `tortoisebots` target, `SLICE1=OFF` | `CMAKE_RC=0 MAKE_RC=0 TOTAL_SECONDS=68`, 0 `warning:` lines |
| module `tortoisebots` target, `SLICE1=ON` | `CMAKE_RC=0 MAKE_RC=0 TOTAL_SECONDS=892` |
| `worldserver`, `SLICE1=ON` (full core build, shared tree `core-df783309b99e-opt-85c8cb4e`) | `CMAKE_RC=0 MAKE_RC=0 TOTAL_SECONDS=827`, 0 `error:`/`FAILED:` lines, `bin/Release/bin/worldserver` 75,460,744 bytes; `CMakeCache`: `BUILD_PLAYERBOT_V2:BOOL=OFF` |
| `tbf-premerge <module worktree> 8` (merge clean, 2 commits over `tbf/main`) | `slice1-ON: PASS`, `slice1-OFF: PASS`; **`turtle: FAIL rc=3`**, so `PREMERGE: FAIL turtle`: not this branch. The Turtle build queue was gone: the build VM's Turtle build directory was a symlink to a mount that no longer existed; after recreating the empty directory, `vm-build` stops at a missing queue control file. The branch changes nothing outside `forever/` except the guard in the root `CMakeLists.txt`, which the Turtle build does not read. Re-run `tbf-premerge` once the Turtle build tree is restored |
| zero core change | `git -C <core-b441 worktree> status --short` empty |
| `slice_gen_context.py --check` | `SliceAiObjectContext.cpp is up to date` |
| `slice_registry_check.py` | `(a)=0  (b)=300` |
| the 45 `forever/tools/test_*_wiring.py` | 43 OK; `test_chat_in_wiring.py` and `test_cl_wiring.py` FAIL with "SLICE-CH.txt lacks SLICE-W1.txt paths: ai/playerbot/TravelMgr.cpp, ai/playerbot/strategy/values/TravelValues.cpp": **the same two fail on `tbf/main` b37e45e61** (list files, not this move) |
