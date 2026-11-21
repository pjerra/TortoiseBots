# Place, rally and fall back (issue #15)

The commander verbs that put the player's bots in a shape at a chosen spot (`place`), remember a fall-back point (`rally here`) and re-form the whole
group on it (`fallback` / `fall back` / `back`). The player-facing surface is [`TB-COMMANDS.md`](TB-COMMANDS.md) section 20; this file is the build record:
what decides what, why, and what has to be checked live.

* Corpus: TortoiseBots on ForeverCore d02645dd21, module only — **no core change** (the ping is read through `ServerScript::OnPacketReceive`, the chat
  form through `PlayerScript::OnChat`/`OnChatSent`; nothing in `src/` of the core was touched).
* Source: issue #15 (`~/TortoiseBots-audit/notes/issue15.md`, ForeverCore parity analysis, 2026-11-18). The original Turtle `dev` has no `place`/`fallback`
  command of its own, so nothing was harvested from a donor tree; the built-on parts are the module's own: `.tb cmd` (P3), the order queue (`come`,
  `follow`), the stay/formation machinery and the `run` judge's ground/path helpers.

## What was built

| File | What |
|---|---|
| `forever/src/host/cmd/TbPlacePure.h` | the geometry, the rank order, the who filter, the fall-back layout, the commander-spot rule and the minimap-ping parser, pure (no core header) |
| `forever/src/host/cmd/TbPlacePlan.h` | the command words (`ParseCommand`, the one parse for `.tb cmd` and chat), the plan (`PlanPlace`/`PlanFallback`: who moves, to which slot) and the hold's rules (merge, release, pending, logout), pure |
| `forever/src/host/cmd/TbPlace.{h,cpp}` | the world half: binding, anchor, floor snap, path check, one `come` per bot, the hold, the walk back, the ping hook, the chat form |
| `forever/src/host/cmd/TbCmdOrders.cpp` | passes the whole command (verb included) to `place::Handle` before the lifted judge; cancels a pending cursor request on any later command; ends a fall back on an accepted `attack`/`pull`/`pullback`/`follow` |
| `forever/src/host/TbOrders.{h,cpp}` | `.tb order` also cancels a pending cursor request; `ReleasePlaceStay` clears only the stay a placement's own `come` wrote |
| `forever/src/TbScripts.cpp` | `tortoisebots_server` (`OnPacketReceive` → the ping), the player's `OnChat`/`OnChatSent` (the chat form), `place::Update()` in the 1 Hz tick, `place::NoteLogout` |
| `forever/tools/test_place_pure.cpp` | the pure geometry test (all shapes, gaps, rings, slot order, frames, who words, fall-back layout, ping layout) |
| `forever/tools/test_place_plan.cpp` | the behaviour test: the words `.tb cmd` hands over, the plan (who moved, which slot), the commander-spot rule, the fall-back front both ways, the hold's merge/cancel/logout rules and the wrap-safe clock |
| `forever/tools/test_place_wiring.py` | the wiring pins for each verb, the routing, the reply format, the read-only ping and the chat hooks (+ a mutation mode) |
| `forever/docs/TB-COMMANDS.md` | section 20: the verbs, the reply shapes, the refusals |

## Decisions worth knowing

1. **The geometry is one pure function per shape.** `LayoutShape(shape, gap, count, targetHalf)` returns slots in *assignment order*: slot 0 is the front
   slot and the slot order is the rank order (tank, melee, ranged, healer, name breaks a tie). So "tanks front, then melee, ranged, healers at the back" is
   one rule for every shape, and the test pins it: the row shapes' y never grows backwards, the circle starts at the front and goes clockwise, the arc
   starts at its centre, the line is centre-out, the spread grid is 2.5 gaps apart.
2. **Rings.** `circle`/`arc` radius = max(the circumference that holds every bot one gap apart, the target's combat reach + one gap). Round a target the
   ring therefore never crosses the target, and past 35 yd the shape is **refused** (`too-far`, `Layout::tooFar`) rather than shrunk onto the target. The
   ceiling is `kMaxRadiusYd = 35`, also applied per slot for the row shapes.
3. **Anchors.** `here`: the whole shape stands along the player's facing, with its nearest slot `kHereClearYd = 2` yd out — so no bot is ever on the
   player's own spot, and the offset uses the layout's own extent so every shape obeys it. `target`: a ring is centred on the target with slot 0 on the
   player's side (the tank between the player and the target, the group wrapping round it); the row shapes stand between the player and the target with the
   tank row `targetHalf + gap` clear of it (nearest the target) and the rest behind it. No slot is ever inside the target. `cursor`: centred on the player's ping, facing away from the player; the ping is only read.
4. **The cursor wait does not block the world thread.** The command answers `status=waiting` + `pending=yes` at once and parks the request; the server's
   packet hook completes it on the commander's `CMSG_MINIMAP_PING` (the same opcode, read with `MinimapPingClient::Read`'s layout: an optional flags
   byte with bit 7 = a party index after the floats, plus the vanilla 8-byte two-float body) and the 1 Hz tick expires it after `kCursorWaitSeconds = 30`
   with `error code=timeout`. The completion and the timeout reply with the command's own `#token`, and no bot is touched before the ping.
5. **One order per slot, not one order string.** The lifted judge (`run::Issue`) issues one order to a party; `place` needs a different destination per
   bot, so it queues the module's own `come` order per bot (`h3::QueueOrder`: pre-flight, then stay position + stay movement + ordered walk + AI reset,
   so the order also drops the bot's target). Every bot's slot is judged first: same map and instance as the commander, alive, valid coordinates, a floor
   from `GetHeightCompat` (25 yd search) and a real path from the same `PathGenerator` the move judge reads (`PathCalculateCompat`/`PathActualEndCompat`,
   at least two points, the end within 3 yd). A slot that fails any of them is skipped with its code and that bot is not queued at all.
6. **Holding the slot, and runs.** The slot *is* the bot's stay position, so the engine's own `return to stay position` walk brings a bot back after a fight (it is
   in the stay strategy's combat triggers too). `place::Update()` adds the net under the cases that walk gives up in: outside combat, more than 3 yd
   (`kSlotToleranceYd`) from the slot, not casting, not moving, at most a fresh `come` every 5 s. A bot that leaves the map/instance, dies, is unbound or
   logs out drops out of the hold, and so does a bot a dungeon run drives (the run owns its movement; a walk-back `come` would read as an outside order and
   pause the run) or one a newer order of someone else left off its stay. A place command typed while a run goes on still moves the bots — the run pauses
   itself with `outside-order`, exactly as for a hand-typed `.tb cmd` order. `follow` and `place follow` release the hold; `attack`/`pull`/`pullback` do not (the placed bots must keep their slots
   through every fight).
7. **Fall back.** The rally point is memory per commander (`rally here`: the player's position, map, instance). `fallback` uses it while the player is on the
   same map and instance, else the player's position; the reply says which (`from=memory|commander`). The formation is `FallbackSlots` (pure): a tank row
   half a gap ahead of the point, a melee row half a gap behind, ranged and healers on a 140° arc behind them (the healers at the ends), so nobody stands on
   the rally point. The front is the direction from a stored rally point towards the commander (the side the group came from, so the tanks are nearest the
   enemy); without a stored point it is the commander's facing. Rows run through the same floor/path judgement and the same `come` order. The hold is
   flagged `fallback`: `attack`, `pull`, `pullback`, `resume` or `follow` ends it (the first four by `place::NoteOrdered`, queued nothing; `resume` sends
   `follow` to every bot that formed up after re-checking that it still answers to this commander).
8. **Only the commander's own bots.** `cmd::ResolveBinding` is the binding (the existing commander check `.tb cmd` uses) and every bot is re-checked with
   `cmd::IsCommandedBy` before it may take a slot; a bound bot that fails it (or is not in the world) is a refused row, never placed. The chat form only
   counts for a commander's own chat: a whisper to one of its own bots, or party/raid chat, and a non-commander's text is left alone (`ResolveBinding`
   decides). The bare aliases (`back`, `fallback`, `resume`, `rally`) only count when the whole message is the command, so ordinary conversation ("back
   off, pull!") is never an order.
9. **The hold's rows are released, not just forgotten.** Dropping a row (a new placement, a fall back replacing a place hold, a death, a map change, a
   run taking the bot, a newer order off stay, an unreachable slot) releases the bot: `follow` while this commander still commands it (`h3::QueueOrder`),
   or `h3::ReleasePlaceStay` when it changed commander — that clears the stay only while the placement's own `come` (by `seq`) is still the bot's last
   order, so the old commander never orders a bot it no longer owns. A placement replaces only the rows of the bots it addressed; a placement that put
   nobody leaves the hold exactly as it was (a fall back always replaces the whole hold).
10. **The clock and the walk-back.** `Update` gates on a `uint32` clock with a wrap-safe compare (`TimeReached`, `int32(now - target) >= 0`), never an
   `int64` wall comparison. A walk-back re-runs `Reachable` first and gives up after three failed checks, logging once; the row is then released.
11. **The cursor request is cancellable.** A pending `place ... cursor` is ended by any later command from the commander — every `.tb cmd` verb, every
   `.tb order`, and the place/chat forms — with `error code=cancelled` on its own token. A malformed ping leaves the request waiting. Logout forgets the
   commander's rallies, holds and pending request.
12. **Replies** follow grammar v2 with the `cmd` markers and record kind: one head record (`verb=`, `status=`, `bots=`, `shape=`, `gap=`, `anchor=`,
   `radius=`, `slots=`/`rally=`/`from=`/`wait=`), one `cmd bot=..` row per bot (`role=`, `slot=`, `status=queued|refused`, `pos=`, `seq=`, `code=`,
   `msg=`), one end record with the counts (`placed=`, `released=`, `refused=`). Typed errors are the vocabulary of `TB-COMMANDS.md`: `missing-argument`,
   `bad-argument`, `duplicate`, `unexpected-argument`, `bad-target`, `no-selection`, `no-bots`, `not-bound`, `needs-player-sender`, `other-map`,
   `teleporting`, `too-far`, `no-slot`, `cancelled`, `bad-token`, `timeout`, `no-fallback`. The order is also recorded on the commander's card (`.tb commander status`, `NoteRunOrder`).
13. **Add-on "Positions" tab: out of this round.** The shape/gap/who/Here/Target/At-cursor buttons need the TBM add-on round; the command-side surface is
    complete and answers with everything a tab would show.

## Verification

### Review fixes (the fix round for the cold reviews)

Each finding of `claude-module-15.md` and `codex-module-15.txt` has a test that fails without the fix; the test names are from
`test_place_plan.cpp` unless it says otherwise.

| Finding | Fix | Test |
|---|---|---|
| Claude 1 (critical): `.tb cmd` sliced the verb off, so `place circle` reached nothing and `fall back` ran `back` | `TbCmdOrders.cpp` passes `args` unchanged; `place::Handle` parses with `ParseCommand` (the chat form's parse too) | `TestCommandWords` (`{ "fall", "back" }` is one fall back; `{ "fall" }` is a missing argument, never `back`), routing pin |
| Claude 2: the fall-back front pointed at the rally point | `FallbackFront`: from a stored rally point it faces the commander | `TestFallbackFront` (tank row towards the commander; facing without a point) |
| Claude 3: the wiring tests only pinned strings | the behaviour test drives the full `.tb cmd` words through `ParseCommand` and the plan, and checks who moved and to which slot | `TestPlaceCircle`, `TestPlaceRefusalsArePerBot`, `TestCommanderSpot` |
| Claude 4 / codex 3: a subset placement replaced or erased the whole hold | `StorePlace` merges by bot (the addressed rows only), a placement that put nobody keeps the old hold; every dropped row is released | `TestHoldBookkeeping` |
| Claude 5 / codex 4: a later command did not cancel a pending cursor request | `CancelPending` on every `.tb cmd`/`.tb order`/place command, answering the old token with `cancelled` | `TestHoldBookkeeping` (`TakePending`/`Forget`), wiring pins |
| Claude 6: a slot could stand on the commander's own spot for `target`/`cursor` | `ClearCommanderSpot` for every anchor and for the fall back | `TestCommanderSpot`, `TestFallbackFront` |
| Claude 7: an unreachable slot was walked back forever | the walk-back re-checks `Reachable`, gives up after three failures, logs once and releases the row | `TestHoldBookkeeping` (`WalkGiveUp`), wiring pins |
| Claude 8: the `Update` gate used an `int64` wall compare | a `uint32` `nextMs` with `TimeReached` | `TestClock` |
| Claude 9: rallies were never pruned | `PlayerScript::OnLogout` → `place::NoteLogout` → `Forget` | `TestHoldBookkeeping` (`Forget`), wiring pin |
| Codex 1: `resume` could order a bot the commander no longer owns | every historical row is re-checked (`CommanderGuid`, `IsCommandedBy`); a stale row is refused and cleared, never ordered | wiring pin on the resume checks; `TestHoldBookkeeping` release rule |
| Codex 2: dropping a row left the bot's stay behind | `ReleaseRow`: `follow` while ours, `ReleasePlaceStay` (guarded by the placement's `seq`) after a commander change | `TestHoldBookkeeping` (`ReleaseOn`), wiring pins |
| Claude 10 (info): `.tb cmd` needs `RBAC_PERM_COMMAND_GM`, chat does not | kept as intended (the chat form is the in-group commander's own words; `.tb cmd` is the GM command surface), now written down here | - |

Unit tests:

```
$ g++ -std=c++17 -O1 -Wall -Wextra -Werror -I. forever/tools/test_place_pure.cpp -o /tmp/test_place_pure && /tmp/test_place_pure
all 1508 checks passed
$ g++ -std=c++17 -O1 -Wall -Wextra -Werror -I. forever/tools/test_place_plan.cpp -o /tmp/test_place_plan && /tmp/test_place_plan
all 112 checks passed
$ python3 forever/tools/test_place_wiring.py
Ran 11 tests in 0.069s

OK
$ PLACE_MUTATE=1 python3 forever/tools/test_place_wiring.py
MUTATION FAILED AS EXPECTED:
MUTATION FAILED AS EXPECTED:
```

Builds and the Turtle build: see the "Gate lines" section at the end (copied verbatim from the runs).

## Live checks (not run in this round — no live server)

1. **Place a circle round a target.** `.tb master party Mtank Mcmd`, `.tb cmd place circle normal target` with a mob targeted: the reply lists five
   `pos=` values on one ring at least `reach + 4` yd from the mob's centre, slot 0 (the tank) on the player's side of the ring; every bot walks there and
   `state` reads `mv=stay` with `pos` at its slot. Pull the pack onto the tank, kill it: every bot walks back to the same `pos=` (compare before/after),
   and the tank stays on slot 0.
2. **Fall back after a pull.** `.tb cmd pull <guid>` (or `.tb order party pull`), then `.tb cmd rally here` at the camp, pull a pack, then `.tb cmd fallback`
   mid-fight: every bot drops its target (no `tgt=`/`victim=` right after the answer), runs to the rally point and stands in the three rows (tank front,
   melee behind, ranged/healers on the arc, nobody on the player's spot); the reply's `rally=` is the point the player stood on. Mobs that follow are fought
   and the group returns to its rows; `.tb cmd state` shows `tgt=` empty once nothing follows. `.tb cmd resume` returns them to following; `error
   code=no-fallback` answers a second `resume`.
3. **Cursor ping.** `.tb cmd place line wide cursor Mtank Mpriest` from a party with a group: the answer is `status=waiting`/`pending=yes` with the token;
   a minimap ping (the client's Ctrl+click on the minimap) within 30 s answers with the slots centred on the pinged point and the line facing away from the
   player; without a ping the same token gets `error code=timeout` after 30 s and no bot moves. A ping that arrives with no `cursor` command waiting
   changes nothing (the core's own ping handler still broadcasts it).
4. **Release.** `.tb cmd place follow` after a placement: every bot follows the commander again, `mv=follow`, and a fight no longer ends with a walk back
   to the old slot. `.tb cmd place circle` a second time on the same commander replaces the hold (no two slots fight over one bot).
5. **Chat form.** Whisper `place arc here` to one of the bots: the reply lines arrive as system messages and the whole group forms the arc; whisper `back`
   to one bot after a fight: the group falls back to the stored rally point; a non-commander player's party chat "back" does nothing.

## Open items

* The add-on "Positions" tab (issue #15's addon line) — later add-on round.
* `resume` is accepted by `.tb cmd` only for the fall back; the run's own `resume` is `.tb run resume` and is untouched.
* A `place` hold is memory only: a server restart forgets the slots (the bots keep their stay positions for the life of the engine session).

## Gate lines

These are the exact lines of the runs that judged this round (slot 7, core d02645dd21, through the slot lock).

### Module build, TORTOISEBOTS_SLICE1=ON

`tar -C ~/wt-place-rally --exclude=.git -cf - . | ssh yulon-ubuntu2 "~/forever-build/slot-sync-build.sh 7 tortoisebots -DTORTOISEBOTS_SLICE1=ON"`

```
combined build: slot 7 key core-9b88bd2d6584-opt-85c8cb4e
build token 1 of 3 (slot 7)
module sync: 1 changed/new/deleted files
CMAKE_RC=0
MAKE_RC=0
TOTAL_SECONDS=44
```
(`~/forever-build/build-7/make.log`: `[2/3] Building CXX object .../TbPlace.cpp.o` — the module file this round rewrote compiled in the ON tree; the only diagnostic is the compat's pre-existing `sTerrainMgr` macro redefinition warning.)

### Module build, the option OFF (the same command without the extra option)

```
combined build: slot 7 key core-9b88bd2d6584-opt-78bf025f
build token 1 of 3 (slot 7)
module sync: 3 changed/new/deleted files
CMAKE_RC=0
MAKE_RC=0
TOTAL_SECONDS=3
```

### Unit and wiring tests

```
$ g++ -std=c++17 -O1 -Wall -Wextra -Werror -I. forever/tools/test_place_pure.cpp -o /tmp/test_place_pure && /tmp/test_place_pure
all 1508 checks passed
$ g++ -std=c++17 -O1 -Wall -Wextra -Werror -I. forever/tools/test_place_plan.cpp -o /tmp/test_place_plan && /tmp/test_place_plan
all 115 checks passed
$ python3 forever/tools/test_place_wiring.py
Ran 11 tests in 0.005s

OK
$ PLACE_MUTATE=1 python3 forever/tools/test_place_wiring.py
MUTATION FAILED AS EXPECTED:
MUTATION FAILED AS EXPECTED:
$ python3 forever/tools/slice_gen_context.py --check
SliceAiObjectContext.cpp is up to date
$ python3 tools/verify_okf.py
OKF verification PASSED (60 nodes validated, graph edges consistent).
$ git diff --check
(no output)
```

### Turtle build, `vm-build ~/wt-place-rally --wait`

```
worktree /home/perzi/wt-place-rally sha=7bdf6de7e-dirty dirty-entries=12 files=4076 (tracked as on disk + untracked-not-ignored)
queued job 1-1795235285501126854-3959797 (priority 1)
QUEUE_SLOT=0
QUEUE_WAITED_SECONDS=0 (enqueue to runner pick-up)
CORE_SELECT: default core CORE_BASE=/mnt/tb/tb-build/src/core-e836b85b8d5c CORE=/mnt/tb/tb-build/src/core-e836b85b8d5c-s0 BUILD=/mnt/tb/tb-build/build-s0
module files changed/added/removed: 63
COMPILE_SECONDS=36 CMAKE_RC=0 MAKE_RC=2
BUILD FAILED, first errors in /mnt/tb/tb-build/logs/20261121T042805Z-s0-errors.txt
QUEUE_RC=1 QUEUE_RUN_SECONDS=41
--- first 60 error lines
logs/20261121T042805Z-s0-make.log:./../../src/modules/TortoiseBots/ai/cmangos-compat-unported.h:126:44: error: 'char* ChatHandler::ExtractKeyFromLink(char**, const char*, char**)' is protected within this context
logs/20261121T042805Z-s0-make.log:./../../src/modules/TortoiseBots/ai/cmangos-compat-unported.h:126:44: error: 'ChatHandler' is not an accessible base of 'PlayerbotChatHandler'
logs/20261121T042805Z-s0-make.log:make[3]: *** [modules/CMakeFiles/modules.dir/build.make:1378: modules/CMakeFiles/modules.dir/TortoiseBots/ai/playerbot/PlayerbotAI.cpp.o] Error 1
```

The failure is in the Turtle half (`ai/cmangos-compat-unported.h`, a file this branch does not touch: `git diff --name-only tbf/main -- ai` is empty), not in the module's `forever/` files. The same job on `tbf/main` in a detached worktree (no change of this round) fails identically:

```
worktree /tmp/tb-main sha=5f1ac1dae-dirty dirty-entries=0 files=4078
module files changed/added/removed: 13
COMPILE_SECONDS=45 CMAKE_RC=0 MAKE_RC=2
BUILD FAILED, first errors in /mnt/tb/tb-build/logs/20261121T042932Z-s0-errors.txt
QUEUE_RC=1 QUEUE_RUN_SECONDS=49
--- first 60 error lines
logs/20261121T042932Z-s0-make.log:./../../src/modules/TortoiseBots/ai/cmangos-compat-unported.h:126:44: error: 'char* ChatHandler::ExtractKeyFromLink(char**, const char*, char**)' is protected within this context
logs/20261121T042932Z-s0-make.log:./../../src/modules/TortoiseBots/ai/cmangos-compat-unported.h:126:44: error: 'ChatHandler' is not an accessible base of 'PlayerbotChatHandler'
logs/20261121T042932Z-s0-make.log:make[3]: *** [modules/CMakeFiles/modules.dir/build.make:1378: modules/CMakeFiles/modules.dir/TortoiseBots/ai/playerbot/PlayerbotAI.cpp.o] Error 1
```

So the Turtle gate is red on the branch itself, before this round's changes; it is not judged by this round's module work (ForeverCore ON and OFF are green), and nothing in `ai/` was touched here.
