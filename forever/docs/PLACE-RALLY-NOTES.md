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
6. **Holding the slot, and runs.** The slot *is* the bot's stay position, so the engine's own `return to stay position` walk brings a bot back after a fight. In combat `stay` is
   kept out of the combat state (a combat `stay` blocked `ReachTargetAction`, `MovementAction` and healer LOS movement), so melee still step in
   and the bot returns to the slot when the fight ends; a fall back keeps `stay` and adds `passive` until the bot reaches its spot. `place::Update()` adds the net under the cases that walk gives up in: outside combat, more than 3 yd
   (`kSlotToleranceYd`) from the slot, not casting, not moving, at most a fresh `come` every 5 s. A bot that leaves the map/instance, dies, is unbound or
   logs out drops out of the hold, and so does a bot a dungeon run drives (the run owns its movement; a walk-back `come` would read as an outside order and
   pause the run) or one a newer order of someone else left off its stay — a whisper shortcut (`follow`, `free`, …) counts: it runs in the engine's chat path and advances no order number, so the row ends on the stay being gone (only its own `come` still in the queue is exempt, #316). Any other loss of the stay ends the row the same way, including a rebuilt tank kit: `ApplyKit` (`.tb role`, and the 1 Hz check that re-applies the kit the bot's spec asks for) resets the strategies, so the bot gets a new kit and is not walked back against it. A place command typed while a run goes on still moves the bots — the run pauses
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
   counts for a commander's own chat: a whisper to one of its own bots, or party/raid chat that holds at least one of the commander's bots; it runs
   the same `h3::CheckBotAccess` as `.tb cmd`, and a non-commander's text is left alone (`ResolveBinding` decides). The bare aliases (`back`, `fallback`, `resume`, `rally`) only count when the whole message is the command, so ordinary conversation ("back
   off, pull!") is never an order.
9. **The hold's rows are released, not just forgotten.** Dropping a row (a new placement, a fall back replacing a place hold, a death, a map change, a
   run taking the bot, a newer order off stay, an unreachable slot) releases the bot: `follow` while this commander still commands it (`h3::QueueOrder`),
   or `h3::ReleasePlaceStay` when it changed commander — that clears the stay only while the placement's own `come` (by `seq`) is still the bot's last
   order, so the old commander never orders a bot it no longer owns. A placement replaces only the rows of the bots it addressed; a placement that put
   nobody leaves the hold exactly as it was (a fall back always replaces the whole hold).
10. **The clock and the walk-back.** `Update` gates on a `uint32` clock with a wrap-safe compare (`TimeReached`, `int32(now - target) >= 0`), never an
   `int64` wall comparison. A walk-back re-runs the placement's own `VerdictFor` first (no-path, liquid path and detour included) and gives up after three failed checks, logging the rule that refused; the row is then released. The queued order outlives the cap (it may run up to 15 s, the fall back's cap is 10 s), so it carries the row's passive owner (`OrderSpec::passiveToken`): once the cap released that record the come only moves the bot, where before it claimed the passive under its own new number and left the bot passive for good.
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
6. **A whispered `follow` ends a placed hold (#316).** `.tb cmd place circle here`, then whisper `follow` to one of the placed bots: within a
   second the bot's `mv=` is follow and it never walks back to its slot (`slot=` gone from `.tb commander status`); before the fix the hold
   survived and pulled it back. Same with a whisper `free`. Then `.tb cmd fallback` and whisper `follow` to a bot on the way: its `passive`
   ends with the hold, and a `fallback` whose walk-back order never runs (engine off, `.tb engine <bot> off`, then `fallback`) still has no
   `passive` left on the bot at the 10 s cap (`state` shows `passive` neither in reaction nor in combat).

## Open items

* The add-on "Positions" tab (issue #15's addon line) — later add-on round.
* `resume` is accepted by `.tb cmd` only for the fall back; the run's own `resume` is `.tb run resume` and is untouched.
* A `place` hold is memory only: a server restart forgets the slots (the bots keep their stay positions for the life of the engine session).

## Gate lines

These are the exact lines of the runs that judged this round (slot 7, core d02645dd21, through the slot lock).

### Module build, TORTOISEBOTS_SLICE1=ON

`tar -C <worktree> --exclude=.git -cf - . | ssh <build-host> "~/forever-build/slot-sync-build.sh <slot> tortoisebots -DTORTOISEBOTS_SLICE1=ON"`

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

### Turtle build, `vm-build <worktree> --wait`

```
worktree <worktree> sha=<sha>-dirty dirty-entries=12 files=4076 (tracked as on disk + untracked-not-ignored)
queued job 1-1795235285501126854-3959797 (priority 1)
QUEUE_SLOT=0
QUEUE_WAITED_SECONDS=0 (enqueue to runner pick-up)
CORE_SELECT: default core CORE_BASE=<core-root> CORE=<core-slot> BUILD=<build-slot>
module files changed/added/removed: 63
COMPILE_SECONDS=36 CMAKE_RC=0 MAKE_RC=2
BUILD FAILED, first errors in <build-root>/logs/<stamp>-s0-errors.txt
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
BUILD FAILED, first errors in <build-root>/logs/<stamp>-s0-errors.txt
QUEUE_RC=1 QUEUE_RUN_SECONDS=49
--- first 60 error lines
logs/20261121T042932Z-s0-make.log:./../../src/modules/TortoiseBots/ai/cmangos-compat-unported.h:126:44: error: 'char* ChatHandler::ExtractKeyFromLink(char**, const char*, char**)' is protected within this context
logs/20261121T042932Z-s0-make.log:./../../src/modules/TortoiseBots/ai/cmangos-compat-unported.h:126:44: error: 'ChatHandler' is not an accessible base of 'PlayerbotChatHandler'
logs/20261121T042932Z-s0-make.log:make[3]: *** [modules/CMakeFiles/modules.dir/build.make:1378: modules/CMakeFiles/modules.dir/TortoiseBots/ai/playerbot/PlayerbotAI.cpp.o] Error 1
```

So the Turtle gate is red on the branch itself, before this round's changes; it is not judged by this round's module work (ForeverCore ON and OFF are green), and nothing in `ai/` was touched here.

## Round 2 (M2): terrain, anchors, rally points and the card

Round 2 extends the landed command (`COMMANDER-SHAPES-SPEC.md` section 2). Nothing above is rewritten; every round-1 word parses and plans as before. No core change, no SQL, no config key.

### What was added

| Area | Rule |
|---|---|
| Gap | `tight`/`normal`/`wide` or a number 1.5..12.0 yd with at most one fraction digit (`5` = `5.0`); a bare number is never a who name. `GapSpec` carries the preset and the yards. |
| Facing | `face:<deg>` 0..359, the game's own degrees (0 = +X, 90 = +Y). It replaces the front for `here`, `cursor`, `tank` and `at:`; with `target` it is refused. |
| Anchors | `tank`: the first tank in rank order among every bound bot alive/in world/on the commander's map and instance/still commanded; slot 0 behind the tank, ring never narrower than its reach plus a gap; only `circle`, `arc`, `spread`; the tank is removed from the selection and its hold row is untouched (`anchor_bot=`). `at:<x>,<y>[,<z>]`: floor-snapped, facing away from the player (or `face:`), at most 100 yd. |
| Terrain | Per slot, in order: `too-far`, `no-ground` (25 yd search from the anchor's z), `ledge` (more than 5 yd from the anchor z), `liquid` (`run::liquid::PointRefusedOnMap`), `unreachable`, `liquid-path` (`SegmentPrefixRefusedOnMap` per leg), `detour` (`IsDetour`: path > 2.5× straight + 10 yd). The nudge tries 1, 2, 3 yd towards the frame centre, skipping a cleared spot or a point within half a gap of an accepted slot; the row says `nudged=`. |
| Fall back | A bot whose slot still fails after the nudge is queued anyway: `slot=-1 to=centre|commander` (centre when it passes `JudgeSlot`, else the commander's own position), its refusal in `code=`, a `to=` row counted as placed. Only `dead`, `other-map` and order-queue refusals leave a fall-back bot unqueued. |
| Rally | Bare `rally` = read-back (`status=shown set=yes|no`); `rally clear` (`cleared=`); `rally at:` judged (`no-ground`, `liquid`, 100 yd). `fallback rally|last` insists on a stored point (`no-rally`); `here` ignores it; `at:` is a typed point; `from=` gains `point`. |
| Card | `place::ViewFor(commander)` and the pure `CardHoldKeys`: `binding` gets `hold=place|fallback`, `rally=`, `rmap=`, `rinst=`; a `member` gets `hold=`/`slot=` only when it holds a row. |
| Persistence | Still memory only (decision in the spec: the binding store is five fixed fields, and after a restart the bots are elsewhere). |

### Tests (each fails without its change)

`test_place_pure.cpp`: `TestGapYards` (5 -> 5.0; 4 lays out as normal; 1.4, 12.5, -3, 5e1, nan, 5.x refused), `TestFaceOverride` (0/90/180, 360/-1/empty, target+face refused), `TestPointAnchor` (`at:10,20`, facing away, within 1 yd, malformed points), `TestTankAnchor` (clear of the tank, ring floor, slot 0 behind, spread centre cell empty, `line tank` refused), `TestClearSpots` (the one-spot wrapper equals the round-1 output for every shape).

`test_place_plan.cpp`: `TestLedgeSlotIsRefused`, `TestLiquidSlotIsNudged`, `TestNudgeGoesInward`, `TestPathVerdicts`, `TestTankExcluded`, `TestFallbackRefusedStillDisengages`, `TestRallyAndFallbackWords` (incl. `ChooseFallback`), `TestCardHoldKeys`.

`test_place_wiring.py` adds `test_terrain_rules_are_the_run_rules`, `test_new_anchor_and_wire_words`, `test_anchor_tank_is_removed`, `test_card_keys_are_written_once`, and two mutation pairs (the liquid probe, the card's `ViewFor`).

### Mutations (applied once in a scratch copy, each caught, then reverted)

| Mutation | Test that went red |
|---|---|
| M-1 drop the ledge compare | `TestLedgeSlotIsRefused` (`code == "ledge"`, `placed == 3`) |
| M-2 `probe.liquid` never called | `TestLiquidSlotIsNudged` (7 checks) |
| M-3 nudge step sign flipped | `TestNudgeGoesInward` (2 checks) |
| M-4 `face:` uses −sin | `TestFaceOverride` (2 checks) |
| M-5 anchor tank left in the selection | `test_anchor_tank_is_removed` (+2 wiring pins) |
| M-6 refused fall-back bots not queued | `TestFallbackRefusedStillDisengages` (3 checks) |
| M-7 `fallback last` falls through to the commander | `TestRallyAndFallbackWords` (`no-rally`) |
| M-8 `CardHoldKeys` slot on every member | `TestCardHoldKeys` (`SlotOf` an unheld bot) |

### Gate lines (slot 4, core d02645dd21, through the slot lock)

Disk before the gates: `df -h <tb-volume> <core-volume>` -> `<tb-volume> 59G 41G 16G 73%`, `<core-volume> 79G 69G 5.6G 93%`.

`tar -C <worktree> --exclude=.git -cf - . | ssh <build-host> "~/forever-build/slot-sync-build.sh <slot> tortoisebots -DTORTOISEBOTS_SLICE1=ON"`

```
combined build: slot 4 key core-9b88bd2d6584-opt-85c8cb4e
build token 1 of 3 (slot 4)
module sync: 1 changed/new/deleted files
CMAKE_RC=0
MAKE_RC=0
TOTAL_SECONDS=49
```

The option OFF (the same command without the extra option):

```
combined build: slot 4 key core-9b88bd2d6584-opt-78bf025f
build token 1 of 3 (slot 4)
module sync: 2 changed/new/deleted files
CMAKE_RC=0
MAKE_RC=0
TOTAL_SECONDS=5
```

The full worldserver link:

`tar -C <worktree> --exclude=.git -cf - . | ssh <build-host> "~/forever-build/slot-sync-build.sh <slot> worldserver -DTORTOISEBOTS_SLICE1=ON"`

```
combined build: slot 4 key core-9b88bd2d6584-opt-85c8cb4e
build token 1 of 3 (slot 4)
module sync: 0 changed/new/deleted files
CMAKE_RC=0
MAKE_RC=0
TOTAL_SECONDS=8
```

Unit and wiring tests:

```
$ g++ -std=c++17 -O1 -Wall -Wextra -Werror -I. forever/tools/test_place_pure.cpp -o /tmp/test_place_pure && /tmp/test_place_pure
all 2001 checks passed
$ g++ -std=c++17 -O1 -Wall -Wextra -Werror -I. forever/tools/test_place_plan.cpp -o /tmp/test_place_plan && /tmp/test_place_plan
all 186 checks passed
$ python3 forever/tools/test_place_wiring.py
Ran 15 tests in 0.008s

OK
$ PLACE_MUTATE=1 python3 forever/tools/test_place_wiring.py
MUTATION CAUGHT: place verbs fall through to the judge
MUTATION CAUGHT: ping packet written instead of read
MUTATION CAUGHT: walk back leaves the row's order number stale
MUTATION CAUGHT: dropped row follows past a newer order
MUTATION CAUGHT: the liquid stand rule is never asked
MUTATION CAUGHT: the card never reads the hold
```

The premerge gate (Turtle build, then the module ON and OFF from a clean merge of the branch into `tbf/main`):

```
$ tbf-premerge <worktree>
merge: clean (1 commits over tbf/main)
COMPILE_SECONDS=17 CMAKE_RC=0 MAKE_RC=0 turtle: PASS
slice1-ON: PASS
slice1-OFF: PASS
PREMERGE: PASS
```

(The older Turtle failure quoted above is from the round-1 branch; the clean merge of this branch builds the Turtle target.)

### Live checks for the lead's queue (not run: no live server in this round)

1. **Water.** `place spread wide at:<a point by a lake shore>`: no `pos=` in the water; rows on the shore read `nudged=` or `code=liquid`; `.tb where` at each `pos=` shows no liquid.
2. **Ledge.** `place line wide here` facing off a ledge (≥ 6 yd drop): the slots past the edge are `ledge` or nudged back onto the upper floor; nobody walks down.
3. **Tank anchor.** `place circle tank` while the tank holds a mob: the tank keeps its target and spot, the others ring it, `anchor_bot=` is the tank.
4. **Fall back, refused slot.** `rally at:` next to water, `fallback last` mid-fight: a bot whose slot was wet is `to=centre` and still drops its target.
5. **Card.** After 1 and after a fall back, `.tb commander status` shows `hold=`, `slot=` on exactly the held bots and `rally=`; `rally clear` removes `rally=`.
6. **Cursor + face.** A real minimap ping completing `place line wide cursor face:90`; the line faces the typed bearing.
7. **Whisper.** Whisper `rally clear` and `back` to one bot after a fight.


## Round 2 review fixes

The three cold reviews (`claude-cmd-m2`, `codex-cmd-m2`, `hydrogen-cmd-m2`) are the source of this section. Every critical/high/medium
finding has a fix and a test that fails without it; the low findings that were one-liners are fixed too. Where a review disagreed with
the spec, the review won; the deviations are named in the rows.

| Finding | Fix | Test |
|---|---|---|
| Claude 1 / Codex M / Hydrogen C: a refused `rally at:` wrote the rally first | `DecideRally` decides the point (judging `At` through the host rule) before the host commits `Memory().rallies[key] = decision.value` | `TestRallyDecision` (a refused point is not stored; an older point untouched), wiring `test_rally_state_is_committed_after_the_judge` |
| Hydrogen C: `fall back`'s `come` never stopped the fight | `OrderSpec::disengage`: `AttackStop`, `InterruptNonMeleeSpells(false)`, the pet's `AttackStop`, `Reset`, `+passive` in all three states; `ReleasePlaceDisengage` clears it on arrival | `PlanFallback` rows carry `disengage`; wiring pins on the `come` branch |
| Claude 2 / Hydrogen H / Codex H: two facing conventions (`playerFront`/`tankFacing` were `(sin, cos)`) | ONE helper, `FrontFromOrientation`, with the core's convention `x += cos(o)`, `y += sin(o)` (`Position::RelocateOffset`, `WorldObject::GetNearPoint2D`); `FaceFront` and TbThrown use it too | `TestFaceOverride`/`TestWalkVerdict` convention checks; wiring `test_facing_is_the_core_convention`; mutation "the player facing is 90 degrees off" |
| Codex H: `fall back` to the commander was never judged | both emergency destinations run `JudgeSlot`; a failing commander point leaves the bot refused | `TestFallbackCommanderValidated` |
| Codex H: hold walk-backs used the weaker `Reachable` | the walk back calls `VerdictFor` (no path, liquid path, detour); the verdict words the log | wiring `test_walk_back_and_fall_back_ends`; `TestWalkVerdict` |
| Hydrogen C/H: `at:` only 2D-checked, unbounded floor search, no reachability | finite check, `IsValidMapCoord(x,y,z)`, 100 yd on every axis, `GroundUnder` (bounded vmap read from `anchor + 5 + 2`, never the 10,000 yd retry), dry, commander path `VerdictFor` | wiring `test_typed_point_bounds`; `TestWalkVerdict` |
| Hydrogen H: per-leg liquid check refused a bot walking out of water | `WalkVerdict` asks `run::liquid::PathPrefixRefused` once over the whole path | `TestWalkVerdict` (wet prefix + dry end accepted; dry→wet refused) |
| Hydrogen M: `place` set `+stay` in combat and reset mid-fight | a place `come` does not `Reset` while in combat and removes `stay` from the combat state; fall back keeps it with `passive` | wiring pins on the `come` branch |
| Hydrogen M: `attack`/`pull` ended a fall back without clearing the hold | `ReleasePlaceHold` clears the placement's `stay`/`passive` when the stay position is still the row's spot | wiring `test_walk_back_and_fall_back_ends` |
| Hydrogen M: a `Target` nudge could enter the boss | `StepRules::guardHalf`: a candidate nearer than the target's combat reach is skipped | `TestTargetNudgeGuard` |
| Claude 3: a `no-slot` bot was sent to the centre (the commander's spot) | a fall back nudges such a slot off the commander/tank spot (`StepRules::nudgeCleared`); a place still leaves it empty | `TestFallbackFront` under-commander case, `test_fallback_emergency` |
| Claude 4 / Hydrogen M: `TestTankExcluded` did its own erasing; no rally map test | the exclusion is `SelectBots(..., anchorName)` used by the host and driven in the plan test; `StoredRallyFor` is the map/instance filter | `TestTankExcluded`, `TestStoredRallyFilter` |
| Claude 5: `Nan`/`Inf` names refused as a gap; `5.` accepted | `LooksNumeric` only treats a word that starts like a number; a gap needs one fraction digit after `.` | `TestGapYards` |
| Hydrogen H: chat and the deferred ping skipped `CheckBotAccess` | `CheckAccess` (lead + bots) runs in `Handle` and in `CompleteCursor`; `HandleChat` takes the channel group and requires it to hold a bot | wiring `test_access_on_every_entry` |

Deviations from `COMMANDER-SHAPES-SPEC.md`: section 2.4's "commander's position, no terrain judgement" is replaced by a
judged commander point (an unstandable destination stays unqueued); section 2.3's `at:` rule gains the player's path
verdict; section 1's combat stay is now non-combat only for a place (the engine's reach/LOS actions are otherwise
blocked), while a fall back keeps it with `passive`.


### Gate lines (review fix round, commit d387be9d)

Run through the slot lock on a clean committed tree (`d387be9dd153933cfe7a2e2b03737333a24af701`, `git status --short` empty).

Module, `TORTOISEBOTS_SLICE1=ON` (`git archive HEAD | ... slot-sync-build.sh <slot> tortoisebots -DTORTOISEBOTS_SLICE1=ON`):

```
combined build: slot 5 key core-9b88bd2d6584-opt-85c8cb4e
module sync: 258 changed/new/deleted files
CMAKE_RC=0
MAKE_RC=0
TOTAL_SECONDS=3
```

The same tree's first build (the compile that caught and then passed the `PositionEntry::isSet()` const fix) reported `TOTAL_SECONDS=35`; the committed content is byte-identical, so the rebuild only re-synced.

Module, `TORTOISEBOTS_SLICE1=OFF`:

```
combined build: slot 5 key core-9b88bd2d6584-opt-21be6ecd
module sync: 258 changed/new/deleted files
CMAKE_RC=0
MAKE_RC=0
TOTAL_SECONDS=2
```

Full worldserver link, `TORTOISEBOTS_SLICE1=ON`:

```
combined build: slot 5 key core-9b88bd2d6584-opt-85c8cb4e
module sync: 0 changed/new/deleted files
CMAKE_RC=0
MAKE_RC=0
TOTAL_SECONDS=8
```

`build-5/make.log`: `[2/2] Linking CXX executable bin/Release/bin/worldserver`.

Policy and wiring:

```
$ g++ -std=c++17 -O1 -Wall -Wextra -Werror -I. forever/tools/test_place_pure.cpp -o /tmp/test_place_pure && /tmp/test_place_pure
all 2016 checks passed
$ g++ -std=c++17 -O1 -Wall -Wextra -Werror -I. forever/tools/test_place_plan.cpp -o /tmp/test_place_plan && /tmp/test_place_plan
all 215 checks passed
$ g++ -std=c++17 -O1 -Wall -Wextra -Werror -I. forever/tools/test_thrown.cpp -o /tmp/test_thrown && /tmp/test_thrown
57 checks, 0 failed
$ python3 forever/tools/test_place_wiring.py
Ran 20 tests in 0.028s
OK
$ PLACE_MUTATE=1 python3 forever/tools/test_place_wiring.py
MUTATION CAUGHT: ... (8 of 8)
$ python3 tools/verify_okf.py
OKF verification PASSED (60 nodes validated, graph edges consistent).
$ git diff --check
(no output)
```

Fail-before proof: each pure/plan fix was reverted in a scratch copy and the named test turned red (per-leg liquid -> `TestWalkVerdict`; rally written
before the judge -> `TestRallyDecision`; `nudgeCleared=false` -> the under-commander fall-back case; `guardHalf=0` -> `TestTargetNudgeGuard`; an
unchecked commander destination -> `TestFallbackCommanderValidated`; the anchor exclusion removed -> `TestTankExcluded`; the map/instance filter
removed -> `TestStoredRallyFilter`; `nan`/`inf` back in `LooksNumeric` -> `TestGapYards`; the fraction-digit rule removed -> `TestGapYards`). The
host-only pins were proven by the mutation mode (facing swap, rally entry created before the decision).

`tbf-premerge <worktree> 6`:

```
merge: clean (4 commits over tbf/main)
COMPILE_SECONDS=7 CMAKE_RC=0 MAKE_RC=0 turtle: PASS
slice1-ON: PASS
slice1-OFF: PASS
PREMERGE: PASS
```

`RESULT: PASS kind=tb sha=d387be9d dirty=0`

Re-run on the final docs-only tip (`55507b4`, same module sources): module ON and OFF `CMAKE_RC=0 MAKE_RC=0 TOTAL_SECONDS=1` each,
`slice1-ON: PASS`, `slice1-OFF: PASS`, `PREMERGE: PASS` (5 commits over tbf/main), `RESULT: PASS kind=tb sha=55507b4 dirty=0`.

## Round 3 (fix round 2): the placement-only combat path and the passive's exits

The cold reviews' final round (`claude-cmd-m2` "Final", `codex-cmd-m2-r2`) are the source. Codex r2's one finding is the absence of live runtime
evidence, and this round has no live server (the documented live checks below stay open); everything else is fixed here.

**The high: every non-fall-back `come` took the placement's combat path.** The round-2 fix keyed the new combat handling on `!o.disengage`, so a run's
lead walk (`run/TbRunOrders.cpp`), a run's walk to a task stand (`run/TbRunTasks.cpp`) and a plain `.tb order come` no longer ran `ai->Reset()` in a
fight and lost `stay` from the combat state: a fighting tank kept its target and had no "return to stay position" to answer the pull. Fixed with
`OrderSpec::placed`, set only by `place`'s `QueueCome`: `PlanCome(placed, disengage, inCombat)` (pure, `TbPlacePlan.h`) decides the reset, the
disengage stops and the combat-state `stay`, and `Run` reads it. Every other `come` is back to tbf/main: `ai->Reset()` always, no combat-state change,
no passive. Fall-back rows keep their own `disengage` path.

**The medium: the fall back's `+passive` could outlive its order.** It now ends on arrival, when the bot has stopped short in a fight (walk ended, after
a 1.5 s grace: a blocked or crowded slot), or after a 10 s cap (`DisengageEnds`), and every exit path releases it: a dropped row (`ReleaseRow`, which
covers death, a change of commander/unbind, another map, a run taking the bot, a newer order), `.tb cmd place follow`, `resume`, a replaced hold, a
cancelled/substituted fall back, the ending orders (`attack`/`pull`/`pullback`/`resume`), `follow`, and the commander's logout (`NoteLogout`, new
`ReleasePlacePassive`). Deeper low findings:

| Finding | Fix | Test |
|---|---|---|
| Codex r2 M: no live runtime evidence | not run in this round (the brief forbids a live run); the checks stay listed and open | — |
| Claude low: a foreign bound bot refused the whole command | `CheckAccess` refuses the whole command only on the binding's lead; `PickBots` asks `CheckBotAccess` per bot and refuses that bot's row (`status=refused`, the check's `code=`) | wiring `test_access_on_every_entry` |
| Claude low: a placed `come` typed mid-fight moves only after the fight | documented in TB-COMMANDS section 20 (placement's own `come` only; ranged may leave the slot during the fight) | — |
| Claude low: `at:` is stricter than the slot rules (the commander must walk there) | documented in TB-COMMANDS section 20 | — |
| Claude low: a `no-slot` fall-back bot whose nudges all fail still lands on the commander's spot when no rally is stored | documented as the residual in TB-COMMANDS section 20; the emergency `come` still fires (round-1 spec) | — |
| Claude low: `TbPlace.cpp` is covered by source pins only | accepted (Hydrogen M6 in round 2); the host half is proved by the wiring mutations and the build | — |

### Tests (each fails without its change)

* `TestComePolicy` (`test_place_plan.cpp`): a run's walk and a plain `come` reset in combat with no passive and no combat-state change; a placed
  `come` in a fight does not reset and keeps `stay` out of the combat state; out of combat it resets; a fall back stops the fight and holds passive.
* `TestDisengageEnds`: arrival ends the hold; a bot stopped short in a fight ends it after the grace but not while still walking; the 10 s cap ends it
  even while walking and out of combat.
* Wiring (`test_place_wiring.py`): `PlanCome(o.placed, ...)` and `come.reset`/`come.keepCombatStayOut`/`come.passive`; `ReleasePlacePassive` in
  `ReleaseRow`; `DisengageEnds(...)` in `Update`; the lead-only access check and the per-bot one. Mutation mode adds "a run's come takes the
  placement's combat path" and "a dropped fall-back row keeps its passive" (10 of 10 caught).

Fail-before: with `PlanCome` reverted to the round-2 rule, "a run's come takes the placement's combat path" is caught by the mutation pin and
`TestComePolicy`'s run-walk case fails; with `DisengageEnds` reduced to `distance <= tolerance`, `TestDisengageEnds` fails; with the `ReleaseRow`
release removed, "a dropped fall-back row keeps its passive" is caught.

### Gate lines (round 3, commit 27ba14b3d, through the slot lock, slot 5)

Module, `TORTOISEBOTS_SLICE1=ON` (`git archive`-free tar of the committed tree into `slot-sync-build.sh 5 tortoisebots -DTORTOISEBOTS_SLICE1=ON`):

```
combined build: slot 5 key core-9b88bd2d6584-opt-85c8cb4e
module sync: 2 changed/new/deleted files
CMAKE_RC=0
MAKE_RC=0
TOTAL_SECONDS=48
```

Module, `TORTOISEBOTS_SLICE1=OFF`:

```
combined build: slot 5 key core-9b88bd2d6584-opt-21be6ecd
module sync: 297 changed/new/deleted files
CMAKE_RC=0
MAKE_RC=0
TOTAL_SECONDS=28
```

Full worldserver link, `TORTOISEBOTS_SLICE1=ON` (`slot-sync-build.sh 5 worldserver -DTORTOISEBOTS_SLICE1=ON`):

```
combined build: slot 5 key core-9b88bd2d6584-opt-85c8cb4e
module sync: 0 changed/new/deleted files
CMAKE_RC=0
MAKE_RC=0
TOTAL_SECONDS=8
```
`build-5/make.log`: `[2/2] Linking CXX executable bin/Release/bin/worldserver`.

Run and policy tests (standalone, g++ -std=c++17 -O1 -Wall -Wextra -Werror -I.):

```
test_run_orders  RESULT: PASS (276 checks)
test_run_wc      test_run_wc: 727 checks, 0 failed (plan: 302 steps, 31 legs, 193 unit rows)
test_run_c6      RESULT: PASS: 28 checks, 0 failed
test_run_cancel  RESULT: PASS
test_place_pure  all 2016 checks passed
test_place_plan  all 227 checks passed
test_place_wiring.py            Ran 20 tests, OK
PLACE_MUTATE=1 test_place_wiring.py  10 of 10 mutations caught
python3 tools/verify_okf.py     PASSED (60 nodes)
```

`test_run_facts` (the RFC facts table) fails on this tree and on tbf/main alike: `FAIL: the table has 246 rows, Facts has 251 fields`.
`forever/src/host/run/*`, `forever/tools/test_run_facts.cpp` and `forever/docs/RUN-S6-F-FIELDS.tsv` are identical to tbf/main, so the five
new `Facts` fields predate this round and no run walk is affected. `tools/verify_all.sh` exceeded the 300 s command window in this
environment and is not claimed as run.

`tbf-premerge ~/wt-cmd-m2 5`:

```
merge: clean (8 commits over tbf/main)
COMPILE_SECONDS=20 CMAKE_RC=0 MAKE_RC=0 turtle: PASS
slice1-ON: PASS
slice1-OFF: PASS
PREMERGE: PASS
```

## Round 4 (fix round 3, #259): the fall back releases only the passive it set (F1)

The dev-based cold review (`6b1a3d95`) found one blocking issue: the fall back's passive release was unconditional, so a newer dev order
that sets `passive` itself (`flee`, `hold`, an owner's `+passive`) had its passivity stripped at the fall back's exit (arrival/cap in
`place::Update`, a dropped row, `place follow`, `resume`, `logout`, `follow`) and a fleeing bot started fighting again.

**The fix: the record of whose passive it is.** The fall back's `come` records that IT added the passive (`BotRec::placePassiveSeq`, set only
when the bot was not already passive in any state, so an owner's `+passive`/`hold`/`flee` is never the row's; a walk-back `come` of the same
row carries the record on under the row's new number). Every order that sets `passive` itself (`hold`, `flee`, `co/nc +passive`, a fall back's
`come`) records its number (`BotRec::lastPassiveSeq`) and, when newer than the fall back's `come`, takes the row's record over. The release
(`ReleasePlacePassive(Player*, seq)`, all seven exit sites pass the row's `seq`) drops passive only for the row that owns the record, and only
while no newer order that sets `passive` itself is the bot's last one. The decision is pure (`PlaceClaimsPassive`, `OrderSetsPassive`,
`PlacePassiveReleasable` in `TbPlacePlan.h`), so the 21 Nov rule "passive is released on every exit path" still holds for the fall back's own
passive: a row that set it and was replaced/ended by a plain `come`, `attack`, `follow` or `free` releases it exactly as before.

Boundary (documented, not fixed): the core `.bot co +passive` typed *on a bot mid-fall-back* is not an order (`PlayerbotAI::HandleCommand`), so
the order record cannot see it; the fall back still releases the passive it set in that one case. The `+passive` typed *before* the fall back is
covered (the row never claims a passive that was already on). The module's order form (`<owner> .tb order <bot> co +passive`) is covered.

Tests (fail on the reviewed head; the three helpers do not exist there):

* `TestFallbackPassiveOwnership` (`test_place_plan.cpp`): fall back then `flee` and fall back then `hold` keep the passive (at the 10 s cap and at
  the stopped-short end); `<owner> co +passive` after the fall back keeps it too while `co -passive` does not block the row's release; an owner's
  `+passive` before a fall back is never released (the row has no record); a fall back alone releases on every exit shape (its own `come` number,
  a walk-back number, an older hold that does not block it, a newer `attack`/`follow`/`free`/plain `come`).
* Wiring (`test_place_wiring.py`): all seven exit sites pass `row.seq` and no bare `ReleasePlacePassive(bot)` remains; the release consults
  `PlacePassiveReleasable`, the `come` records `placePassiveSeq`, and the runner records `OrderSetsPassive`. Mutation mode adds "the fall back drops
  passive whatever set it" and "the fall back's come records no passive of its own" (12 of 12 caught).

Fail-before: with `PlacePassiveReleasable` returning `claimed` alone, the flee/hold cases of `TestFallbackPassiveOwnership` fail; with the
`ReleasePlacePassive` guard reduced to the old unconditional clear, "the fall back drops passive whatever set it" is caught.

## Round 5 (fix round 4, #259): the fall back's 10 s cap restarted with every walk back (F1b)

**Live 11-24 (forever-test3, row F1b): FAIL.** A plain `fallback here` with no passive before: passive stayed on three of four bots at 2 s,
8 s and 17 s (and 40 s after an earlier `fallback last`), `hold=fallback` on every row, the bots idle off their slots; only `resume` cleared it.

**Cause (`place::Update`, TbPlace.cpp).** The cap and the stopped-short grace were measured as `now - row.walkMs`, but `walkMs` is the walk
back's own clock: every `kWalkBackEveryMs` (5 s) a bot that stands idle out of combat more than `kSlotToleranceYd` off its slot gets its `come`
re-queued (`AfterWalkBack` writes `walkMs = now`; a failed path check writes it too). So the elapsed time never reached `kDisengageMaxMs`
(10 s); out of combat the stopped-short end does not apply and the bot was not on its slot, so no exit path ever fired, and each re-queued
`come` (still `disengage`) set the passive again. Not the F1 ownership record: the release itself was never asked. The F1 record behaves (the
walk back's `come` carries it, its number is the row's).

**Fix.** `HoldRow::disengageMs`: the fall back's own start, written once with the row (`StartRowClock`) and never by a walk back.
`RowDisengageEnds(row, distance, inCombat, walkEnded, now)` measures the cap and the grace from it; `kWalkBackEveryMs` moved into
`TbPlacePlan.h` so the test drives the same interval. The walk back keeps its own clock (it serves the slot after the passive ends).

Tests: `TestFallbackCapSurvivesWalkBacks` (`test_place_plan.cpp`) drives Update's 1 Hz pass with the 5 s walk back on an idle bot 8 yd off its
slot: the hold ends at exactly 10 s (on the reviewed head's clock it never ends in 40 s: 3 checks fail), a failed path check does not move the
cap, the in-fight grace counts from the start. Wiring: Update calls `RowDisengageEnds(..., now)`, no `now - row.walkMs` is left, the row starts
with `StartRowClock`; mutation mode adds "the fall back's cap restarts with every walk back" and "the walk back restarts the fall back's cap".

Open for the live re-run: why the bots did not walk to their slots at all (positions unchanged for 40 s). The engine's passive multiplier
lets `return to stay position` through ("stay" part), so the passive alone does not explain it; the re-run must show whether the bots walk.

## Round 6 (bug round, #316): a hold a whisper superseded, and the passive's owner

Found by an adversarial review of the code already on dev; both are placements only, no new verb, no setting.

**(1) A whispered `follow` did not end a placed hold.** The row ended only on a newer order number (`last.seq > row.seq`) whose stay was gone.
A chat shortcut (`follow`, `free`, `stay`, …) runs in the engine's chat path (`PlayerbotAI::HandleCommand` -> the shortcut actions): it drops
the stay, advances no `h3` order number, so the row's `come` was still the bot's last order and the row survived; once the bot had followed far
enough, the walk back queued another `come` and pulled it to the old slot. The row now ends on **the stay being gone** (`RowStayGone`), whatever
removed it: only its own `come` still waiting in the queue is exempt (its stay has not been written yet), and a bot with no engine reads no
strategy and is left as before. `RowEnd::NewerOrder` became `RowEnd::StayGone` (same release: `ClearStay`, never a `follow` that would fight
the newer word). Limit, documented: a whisper that lands inside the same second in which a walk back was already queued is still overridden by
that one queued `come`; the row then ends on the next loss of the stay.

**(2) A walk back that never ran stranded the bot passive.** `AfterWalkBack` replaced the row's `seq` with the new `come`, while the fall back's
passive record (`BotRec::placePassiveSeq`) still named the fall back's first `come`. The 10 s cap then called `ReleasePlacePassive` with the new
number: it found no record, answered "nothing to release" (true), the `||` short-circuited and `row.disengage` was cleared - the passive stayed
on the bot with nothing left to remove it. `HoldRow::passiveSeq` now names the order that OWNS the passive (`StartRowClock` sets it from the
fall back's own `come`; `AfterWalkBack` never moves it), every passive release site asks `PassiveOwner(row)`, and the flag clears only through
`DisengageSettled(row, passiveGone)`, which keeps it for the next 1 Hz pass when the release could not run (no engine). The cap therefore still
removes the passive when the walk-back order never runs.

Tests:

* `TestWhisperFollowEndsTheHold` (`test_place_plan.cpp`): place, then a whispered follow, driven across three walk-back intervals at Update's
  1 s step: the row ends on the first pass and no walk-back `come` is queued (the old rule's condition is checked to be false in that state); the
  queued-`come` exemption, the live stay and the no-engine case.
* `TestWalkBackPassiveOwner` (`test_place_plan.cpp`): fall back `seq` 7 owns the passive, the walk back moves `seq` to 9 and the owner stays 7;
  the cap still fires; the settled flag clears only on a release that ran, and a release that could not run keeps the row for the next pass.
* Wiring (`test_place_wiring.py`): the seven passive-exit sites all pass `PassiveOwner(row)` and none passes `row.seq`; `DisengageSettled` is
  the only place the flag clears; Update ends a row through `RowStayGone(ai != nullptr, ai && …stay, ownComeQueued)`. Mutation mode adds "a
  whispered follow leaves the placed hold standing", "the walk back hands the passive to its own new order" and "the cap clears the flag without
  dropping the passive" (15 of 15 caught).

Fail-before: mutation mode restores exactly the pre-fix lines (`last.seq > row.seq && …`, `row.passiveSeq = seq` in the walk back,
`row.disengage = false`) and each pin fails on it; the pure tests' failing states are the pre-fix ones (no newer order yet the hold lived; the
passive's record naming another number than the one the release asked with).

## Round 7 (review fix, #321): the walk back that runs keeps the row's passive

Review of round 6: the fix covered a walk-back order that never ran, and regressed the ordinary case where it does run. When the queued
`come` executed, `TbOrders` saw a carried record, wrote the walk back's new number into `BotRec::placePassiveSeq` and noted it as the last
order that set `passive` itself. The cap then called `ReleasePlacePassive` with the row's owner (number 7), the record said 12, so the release
answered "not the row's" (true), `DisengageSettled` cleared the flag, and the bot stayed passive for good - the live symptom of (2), on the
normal path.

**One record, one token.** The bot's record is now `place::PassiveBook` (`owner`, `last`) beside the row rules in `TbPlacePlan.h`, with the
three transitions the host uses:

* `RanComePassiveOwner(book, alreadyPassive, seq)`: the `come` that just ran owns the passive only if it added it (`PlaceClaimsPassive`); a
  walk back carries the row's token, never its own newer number.
* `NoteRanPassive(book, verb, arg, disengage, seq)`: a `come` records its owner's token as the last order that set `passive`; `hold`, `flee`
  and `co/nc +passive` still become the newer owner and take the row's record away.
* `TakeOwnerPassive(book, token)`: drops `passive` only for the record's owner and only while no newer `passive` order took it over; the
  record is consumed either way, so a second pass drops nothing.

`TbOrders.cpp` keeps `place::PassiveBook placePassive` in `BotRec` and calls these; the row (`HoldRow::passiveSeq`, `PassiveOwner`) is
unchanged. The release site's request is therefore the same number the record holds, in all three cases.

Tests: `TestExecutedWalkBackPassiveOwner` (`test_place_plan.cpp`) drives the row helpers and the record together: the fall back with no walk
back, the walk back that runs (new `row.seq`, unchanged owner and `last`), and the walk back that stays queued - each drops the passive exactly
once and settles the row; a `flee` or `hold` after the executed walk back owns the passive and the row's release drops nothing; a bot the
owner already held passive never gets a record (its walk back included). Wiring (`test_place_wiring.py`): the release asks
`TakeOwnerPassive`, the `come` claims through `RanComePassiveOwner`, the runner notes through `NoteRanPassive`, and `passive_book_pin` pins the
three bodies. Mutation mode adds "the walk back's come takes the passive over" and "the fall back drops passive whatever set it" (18 of 18 caught).

Fail-before: with `RanComePassiveOwner` returning the new number for a carried `come`, `TestExecutedWalkBackPassiveOwner` fails five checks
(owner, last, the release and its one-shot behaviour) while the no-walk-back and never-run cases pass.

#315 (the battleground bind's authorisation) is recorded in `forever/docs/BG2-NOTES.md`; the same round, a separate commit.
