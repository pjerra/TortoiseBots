# W0-B: the pool scheduler (world slice W0, round B)

Scheduler of the bot pool on ForeverCore, core pin `d02645dd21` (decisions D1/D2 of `FOREVER-W0-W4-SPEC.md` part 1): the
`.tb pool start|stop|status` console words and the once-a-second pass that keeps `TortoiseBots.Pool.TargetOnline` level-1 pool
bots of both factions online, each for a rolled session and each back after a rolled offline time. Built on round W0-A (the
registry, the composition and the creation); zero core changes, everything off by default. Worktree `~/wt-w0b`, branch
`world/w0b`.

Spec: `~/TortoiseBots-audit/FOREVER-W0-W4-SPEC.md` row W0-B, section 1.2 (login/logout/free play) and 1.4 (the tick budget).

## 1. What runs

| Piece | Where |
|---|---|
| The schedule's decisions: count, alternating picks, the 60 s window, session end, the dead wait, the drain orders | `ai/playerbot/policy/world/PoolSchedulePolicy.h` (pure), test `forever/tools/test_pool_schedule_policy.cpp` |
| The translation: registry rows and BotMgr bots to `PoolBotState`, the plan back to `Login`/`Logout`, the stamps to the rows | `forever/src/host/pool/TbPoolScheduler.{h,cpp}` (namespace `PoolScheduler`) |
| `last_login` / `last_logout` writes (read back like every other registry write) | `forever/src/host/pool/TbPoolRegistry.{h,cpp}`: `MarkLogin`, `MarkLogout` |
| `.tb pool start`, `.tb pool stop`, `.tb pool status` | `forever/src/TbScripts.cpp`: three `poolTable` rows, `HandlePoolStart/Stop/Status` |
| The pool mark on a session: `bool pool` on `Bot`, `IsPoolBot`, `Login(..., free, pool)` | `forever/src/TbBotMgr.{h,cpp}`, marked `// W0` |
| The world tick: `PoolScheduler::Update(diff)` right after `BotMgr::Update`, only then `pool::CreateUpdate` | `forever/src/TbScripts.cpp`, `OnBeforeMapsUpdate` |
| Config | `forever/conf/tortoisebots.conf.dist`, the `# W0 pool` block: `TargetOnline = 0`, `LoginsPerMinute = 20`, `SessionMinutes.Min = 30`/`.Max = 90`, `OfflineMinutes.Min = 5`/`.Max = 20` |
| The row of `.tb pool ...` in the command doc | `forever/docs/TB-COMMANDS.md` section 2 |

`Pool.Enable = 0` still gates everything, and `.tb pool start` is explicit: the pool never starts itself at boot (player
control first). With the key on and the tables there, the schedule runs from the first start.

## 2. The decisions the code makes

- **What counts.** The online count is the pool's own: a pool bot (`Login(..., pool=true)`) that is `Loading`, `Online` or
  `Leaving` counts; a bot that has a master does not. `BindMaster` clears `free`, so the scheduler reads `bound` as
  "not free and has a master". The policy sets it on every switch to `Online` (so a master bound while the bot was still
  loading is seen on that pass, not one pass late) and clears it when the binding ends: `.tb free on`, `.tb ungroup`, or any
  path that clears the master (`BotManager::ClearBotMaster`, a run handing the bot back). A cleared bot rejoins the count with
  a fresh session, so a bot that spent hours bound does not end the moment it rejoins. A bound bot that leaves the world (the
  master grace, the owner's login, a shutdown) is offline and unbound, and the pool logs it in again after its offline time. A
  bound bot is never logged out by the pool (not on its session's end, not on a lowered target, not on a stop). Its slot is
  refilled: the bind leaves the count on the pass that sees it, and that same pass plans one login for another due bot on an
  account no bot holds. If every account already holds a session (the live pool's ten accounts for a target of ten), no extra
  session can be started without two characters of one account in the world (D2), the pass plans nothing, and the cap says why
  (`capped by accounts`, the bind's log line).
- **One session per account (D2).** A bot that is `Loading`, `Online` or `Leaving` occupies its account for every other
  character of it; the host also marks the account online when the world has a session on it (`sWorld->FindSession`) or any
  bot of the module sits on it (`BotMgr::BotAccounts`, asked for once per pass: pool or not, loading or in the world).
  `BotMgr::Login` refuses a second bot of an account for the same reason (`AccountHasBot`). The pool puts up to ten characters on one account, so the online count is
  capped by the number of accounts the pool's characters sit on. A bound bot keeps its account's session for as long as the
  binding lasts and the pool never logs it out, so that account is out of the pool's reach: the cap leaves it out
  (`PoolAccountCount`), and `.tb pool status` appends ` capped by accounts: N` when the target is above what is left, as the
  bind's own log line says. The pool's own tracks derive the occupancy in the policy (the test compiles that rule with ten
  characters per account).
- **Logins.** At most one per pass (a pass is a second), at most `LoginsPerMinute` in any 60 s window (sliding, half-open), and
  only under the target. Candidates are `Offline`, not bound, due (`now >= offlineUntil`) and on an account no character of
  which holds a session (the rule above; the owner may be loading a character). A pending shutdown plans no logins
  (`sWorld->IsShuttingDown()`); the logouts already asked for still run. Factions alternate from the faction of the last pick;
  a faction with no candidate falls back to the other, and the alternation continues from the bot that was picked. Inside the
  faction the bot that went offline longest ago goes first (`last_logout`, never-logged-out at the head).
- **Sessions and offline times.** Both are rolled when they start: `RollPoolLengthMs(min, max, rand32())`, a uniform 32-bit roll
  scaled onto the range (0 is the minimum, 0xffffffff the maximum). `last_login` is written at the login, `last_logout` when the
  session is seen gone. The rolled end/offline time is not stored (the schema is W0-A's), so a restart cannot reconstruct it:
  every row with `last_login > last_logout` gets `OfflineMinutes.Min` from boot, every row that had logged out gets its
  `last_logout + Min` (not its rolled duration), and every session that was running is replaced by a fresh roll. A restart
  therefore shortens arbitrary offline periods, makes them all fall due together, and re-rolls the active sessions. It does
  less, never invents a value.
- **Session end.** A session whose end has passed is logged out once (`phase` becomes `Leaving`, so no second request). A living
  bot in combat keeps its session until the fight ends; a later pass logs it out then. A bot whose body is still there waits
  until it is alive again (W1's corpse run) or until 10 minutes after the end; then it is logged out anyway and the line says
  `dead (the corpse run had its 10 min)`. The line is `pool logout <name>: reason=session, session <n> min` (the live check's
  `reason=session` and the length inside the window).
- **Lowered target and stop.** A target below the online count gives up the newest sessions first (least played), but never a
  dead or a fighting bot: it takes the next newest instead, and a later pass takes those two when they are alive and out of
  combat. `.tb pool stop` drains every pool bot, dead or fighting included (the explicit stop is unconditional). Both are paced
  at 5 logouts per second (`kPoolLogoutsPerSecond`): 20 bots drain in 4 s, 200 in 40 s, and one world tick never tears down a
  hundred sessions. The spec fixes the 5/s only for stop; the same rate for a lowered target is the deliberate choice against a
  teardown burst. `reason=target` / `reason=stop` in the lines.
- **A vanished row keeps an online bot.** `Refresh` drops a track only once its bot is offline. A registry row that disappears
  while the bot is `Loading`, `Online` or `Leaving` no longer removes it from the count or from the drain: it is logged out by
  the next session end or stop, and only then is its track dropped.
- **A disabled pool drains.** `.reload config` with `Pool.Enable = 0` (or a table that went away) stops new logins and drains
  the bots that are in the world; the tracks are kept although the registry no longer answers, and no stamps are written while
  the key is off. The registry's `last_logout` keeps the truth that the row will read next boot.
- **A refused login** (the character is in the world by hand, the account is loading, the party rule) is one `WARN` line and a
  60 s retry, never a loop. The registry's stamp writes failing is one `ERROR` line from the registry; the schedule carries on
  with its own clock.
- **The creation never shares a tick with a login** (spec 1.4): `PoolScheduler::Update` answers true when it started a login,
  and only then is `CreateUpdate` skipped for that tick.

## 3. Shared lines touched (W2/W3/W6a and Commander P3/P6)

| File | Lines |
|---|---|
| `ai/playerbot/policy/world/PoolSchedulePolicy.h` | the account rule (`accountId`, `PoolOccupiedAccounts`, `PoolAccountBusy`, `PoolAccountCount`), the transitions (`PoolSyncBot`, `PoolSyncMaster`, `PoolGoOffline`, `PoolStartSession`, `PoolTrackKept`), `inCombat`, `shuttingDown` |
| `forever/src/TbBotMgr.h` | `Login` gained `bool pool = false`; `IsPoolBot`; `AccountHasBot`, `BotAccounts`; `bool pool` on `Bot` (all `// W0`) |
| `forever/src/TbBotMgr.cpp` | `Login` signature, the pool/free refusal, the account refusal, `bot.pool = pool`, `AccountHasBot`, `BotAccounts` |
| `forever/src/TbScripts.cpp` | one include; `PoolScheduler::LoadConfig()` in `OnConfigLoad` and `OnWorldInitialized`; the world tick's `PoolScheduler::Update` and its `if (!poolLogin)`; three `poolTable` rows (appended after `prune`) and three handlers before `HandleFree` |
| `forever/src/host/pool/TbPoolScheduler.h/.cpp` | the scheduler; its states are the policy's `PoolBotState`/`PoolBotFacts`, the account seed is `FindSession || BotAccounts` (once a pass), the shutdown flag is `sWorld->IsShuttingDown()`, status appends the account cap |
| `forever/src/host/pool/TbPoolRegistry.h/.cpp` | `MarkLogin`/`MarkLogout` (the scheduler's two columns; the W0-A notes left them for this round) |
| `forever/conf/tortoisebots.conf.dist` | six keys and their comments in the `# W0 pool` block (the account cap, the combat wait, the restart's minimum) |
| `forever/tools/test_wl0_wiring.py` | the `Scheduler`, `ScheduleBehaviour` and `ScheduleMutations` classes |
| `forever/docs/TB-COMMANDS.md` | one row for `.tb pool ...` |

`FreeKitPolicy.h`, `SliceLinkStubs.cpp`, `UnportedFeatures.h`, `TbH3.cpp`, `RpgSubActions.cpp` and the migration are untouched.

Open for the wave's seam owner: `UnportedFeatures.h` and `forever/docs/UNPORTED.md` still read `pool and hire services` as
unported. The spec's shared-seam table gives the rewrite to W0, and W0-A deferred it until "the scheduler and the live pool are
in". It touches a legacy `ai/` file (its Turtle gate waits on PR #99), so it is left to W0-L or the seam owner, together with
the `.tb state` `pool=` key (W0-C).

## 4. Tests (failing case first)

`forever/tools/test_pool_schedule_policy.cpp` (compiled and run by the gate; `ScheduleBehaviour` runs it again in the wiring
suite): the round's five cases, in the spec's words —

| Case | What the test drives |
|---|---|
| 200 due logins with `LoginsPerMinute` 20 start at most 20 in any 60 s | 200 offline bots, 200 seconds of one pass a second: no half-open 60 s window holds more than 20, and at least 60 logins happened (the rate is used) |
| a session past its end logs out once | one ended session → one logout; the same bot as `Leaving` → none; a live session → none |
| a bound bot never logs out | bound + ended session, bound + stop, bound + lowered target: no logout, and it does not count; a bound offline bot is never logged in |
| a bot offline less than `OfflineMinutes.Min` is not picked | two due bots: only the one whose rolled offline time has run out is picked (and the other faction's) |
| target lowered from 200 to 50 logs out the newest first | 200 sessions, five per pass, 150 logouts, strictly newest-first, each bot once, then nothing |

Plus, from the fix round: **one session per account** (20 accounts × 10 characters, target 200: never two characters of an
account in the world, 20 sessions, `counted == 20` — the policy derives the occupancy itself, so the old
`FindSession`-only host would fail it); a living bot **in combat** is not logged out at its session's end, and a lowered
target skips a corpse and a fighter (the stop does not); a pending **shutdown** plans no logins while a session's end still
logs out; the **bound transitions** (a master seen on `Loading → Online`, `.tb free on` and `.tb ungroup` clearing `bound`
with a fresh session, the master-grace logout clearing it for the pool); the **refill after a bind** (ten accounts with a
character online each, ten characters per account, target 10: a bind logs one more in on the free accounts, at the same pass
inside the 60 s window, and the bound bot is in no logout of a session end, a target drop to 0 or a stop); the **account cap
after a bind** (the live shape, ten accounts × ten characters: a bind leaves nine usable accounts, `eligible` 0, no login,
and `PoolAccountCount` 9 — the status line's ` capped by accounts: 9`, 10 again once it is unbound); a **vanished row** keeping an online bot's track; the alternation and the
fallback, oldest `last_logout` first, an account with a session (the host's own flag) skipped, the corpse's ten minutes, the
rolled ranges (both ends, nothing outside, a reversed range), `LoginsPerMinute = 0` and `target = 0`, the stop's 5 per second,
and the pass cost over 2000 bots (`average < 500.0` µs; measured ~6 µs average on the build host).

`ScheduleMutations` (22 mutations of the policy header, each compiled with the same test) restores, one at a time: the login
window, the alternation, the oldest-first order, the session end, the second-logout guard, the bound bot's count and drain, the
offline window, the account rule (its host flag and its own tracks), the combat wait (session and target), the shutdown, the
bound flag's set/clear/leave, the track of a vanished row, the lowered target's order, the dead wait, the rolled maximum and
the stop's pacing. All are caught.

Wiring pins (`Scheduler` class): the tick order and the no-creation-in-a-login-tick rule, the `// W0` marks and the pool flags,
`AccountHasBot`/`BotAccounts` and the account refusal in `Login`, the account seed (`FindSession || BotAccounts`) and the shutdown flag, the
three console words, the status keys and the account cap, the scheduler's calls into the policy and the registry, the bind's
`UsableAccounts(s)` / `{} account(s) left for a target of {}` line, the six config
keys with their shipped defaults (a fallback equal to the template), and the policy test's case sentences.

## 5. Gates

Slot 6 through the slot lock (`~/forever-build/slot-sync-build.sh 6 ...`), core `d02645dd21`; the shared tree key is
`core-9b88bd2d6584-opt-85c8cb4e` (ON) / `core-9b88bd2d6584-opt-78bf025f` (OFF). The shared tree is shared with other rounds of
this core, so each gate's evidence is the slot's own mirror (`~/forever-build/build-6/...`), checked by content after the build.
The branch merged `tbf/main` first (W8's `ai/playerbot/strategy/actions/AhAction.cpp` is now in `forever/SLICE-W0.txt`: without
that line the link read `undefined reference to vtable for ai::AhAction`, while `tbf/main` alone linked because its build list
is `forever/SLICE-W1.txt`). The rows marked *this round* are the refill fix's, after every edit; the others are the first
round's and were not rerun here.

| Gate | Exact line |
|---|---|
| Worldserver link, module ON (`... slot-sync-build.sh 6 worldserver -DTORTOISEBOTS_SLICE1=ON`) — *this round* | `combined build: slot 6 key core-9b88bd2d6584-opt-85c8cb4e` / `build token 2 of 3 (slot 6)` / `module sync: 363 changed/new/deleted files` / `CMAKE_RC=0` / `MAKE_RC=0` / `TOTAL_SECONDS=15`; `[316/329] Building CXX object src/server/modules/TortoiseBots/CMakeFiles/tortoisebots.dir/ai/playerbot/strategy/actions/AhAction.cpp.o` / `[329/329] Linking CXX executable bin/Release/bin/worldserver`; sha1 `e8f39ac7fefe59bfcb327cd7f0e8ce9e92b95a68`, and the binary holds the new bind line once |
| Module ON (`... slot-sync-build.sh 6 tortoisebots -DTORTOISEBOTS_SLICE1=ON`) — *this round* | `combined build: slot 6 key core-9b88bd2d6584-opt-85c8cb4e` / `build token 2 of 3 (slot 6)` / `module sync: 0 changed/new/deleted files` / `CMAKE_RC=0` / `MAKE_RC=0` / `TOTAL_SECONDS=2` (the same tree the link build compiled) |
| Module ON, objects — *this round* | `libtortoisebots.a` holds `TbPoolCreate.cpp.o`, `TbPoolRegistry.cpp.o`, `TbPoolScheduler.cpp.o`; the scheduler object's strings hold `... {} account(s) left for a target of {}, so its slot stays empty while it is bound` once and ` capped by accounts: {}` once (checked with `ar x` + `strings` before the OFF rebuild replaced the mirror) |
| Module OFF (`... slot-sync-build.sh 6 tortoisebots`) — *this round* | `combined build: slot 6 key core-9b88bd2d6584-opt-78bf025f` / `build token 3 of 3 (slot 6)` / `module sync: 363 changed/new/deleted files` / `CMAKE_RC=0` / `MAKE_RC=0` / `TOTAL_SECONDS=4`; the mirror's `TbPoolScheduler.cpp.o` carries only the off stubs (`took a master` 0 times, `pool login of` 0 times, `pool: state=off` once) |
| Policy test (`g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/test_pool_schedule_policy.cpp`) — *this round* | `pass cost over 2000 bots: average 5.5 us, worst 5.7 us` / `RESULT: PASS (3852 checks)` |
| Wiring (`python3 -m unittest forever.tools.test_wl0_wiring`) — *this round* | `Ran 37 tests in 12.187s` / `OK (skipped=3)` |
| Whole tools discovery (`python3 -m unittest discover -s forever/tools -p "test_*.py"`) — *this round* | `Ran 277 tests in 33.083s` / `FAILED (failures=1, skipped=9)`: `test_w1_wiring.FreeHost.test_the_console_commands_lead_to_the_host` — its test wants `SetFree(GuidByName(name), ...)` and `HandleFree` passes `guid`; it fails on a pristine `tbf/main` worktree too. An earlier run of this round also failed `test_vm_census_order.VmCensusOrder.test_files_zero_is_not_a_measurement` (the census took the build box's slot lock and exited 1 where the test wants 3); it passes when the box is quiet. Both are outside W0-B |
| Mutations (`POOL_SCHEDULE_MUTATE=1 python3 -m unittest forever.tools.test_wl0_wiring.ScheduleMutations`) — first round, not rerun | `Ran 1 test in 166.665s` / `OK` — 22 mutations, each one caught by the policy test (the baseline passes first); the new cap rule has no mutation entry |
| Generator (`python3 forever/tools/slice_gen_context.py --check`) — *this round* | `SliceAiObjectContext.cpp is up to date` |
| Registry (`python3 forever/tools/slice_registry_check.py`) — *this round* | `summary: (a)=0  (b)=228 keys to register or stub later` |
| Direct queries (`python3 tools/test_forever_no_direct_queries.py`) — *this round* | `Ran 5 tests in 0.505s` / `OK` |
| Turtle build (`vm-build` / `vm-test tb`) — first round, not rerun | **waits on PR #99**: not merged on `tbf/main` (`5f1ac1dae`, #104/#105). Nothing of this round is compiled by the Turtle build (the scheduler, the pool files and the test are under `forever/`; the one file outside it, `ai/playerbot/policy/world/PoolSchedulePolicy.h`, is core-free, header-only and included by no Turtle TU, so Turtle behaviour cannot change) |

Not run: the legacy census (`forever/tools/port_census.py`) — the one new line in `forever/SLICE-W0.txt` only adds an
already-ported file to the build; no live server (round rule).

## 6. Live checks (nothing run this round)

The round is engine-side only: no live server was started. The checks for the live round, on the assigned test server:

1. Config: `TortoiseBots.Pool.Enable = 1`, `.TargetOnline = 10`, `.LoginsPerMinute = 20`, `.SessionMinutes.Min = 1`,
   `.Max = 2`, `.OfflineMinutes.Min = 1`, `.Max = 2`, `.MaxCharacters = 100` in `etc/worldserver.conf.d/tortoisebots.conf`,
   then `.reload config`.
2. `.tb pool create 100`: 100 level-1 characters (8 races, both factions; W0-A's own checks) on 10 accounts, ten characters
   each (the spec's cap). Wait for `pool create stopped ... the request is complete`. With only 20 characters (2 accounts)
   and `.TargetOnline = 10`, `.tb pool status` must instead say `capped by accounts: 2`.
3. `.tb pool start`: `pool: state=running target=10 online=0 ... size=100`, then `.tb pool status` every 30 s: exactly 10 online
   within 1-2 minutes (20 logins/min, one per second), alternating factions (`alliance=5 horde=5` plus or minus one), never
   above 10 and never two characters of one `TBPOOL<nnnn>` account; the log has one `pool login` per bot with its faction.
4. Inside 1-3 minutes: `pool logout <name>: reason=session, session 1 min` (or 2), and the same bot back after 1-2 minutes
   (`pool login`); `SELECT name,last_login,last_logout FROM tb_pool_character` moves for it, and no logout line reports a
   session length outside 1..2 min.
5. A master: `.tb group <one pool bot> <gm character>` → `online` drops by one, and the pool fills the slot with another bot on
   the next pass, unless every account holds a session. **The live pool's shape has exactly ten accounts for a target of ten**,
   so the bound bot's account is all that is left and no second character of an account may log in (D2): online stays 9 and
   `.tb pool status` must say `target=10 online=9 ... capped by accounts: 9`, with the bind's log line naming the accounts left:
   `... took a master: it leaves the pool's count and is never logged out by the pool; 9 account(s) left for a target of 10, so
   its slot stays empty while it is bound`. To see the refill itself, start a pool with more accounts than the target (200
   characters on 20 accounts, `.TargetOnline = 10`): the same bind then logs one more in on the very next pass. No `pool
   logout` line names the bound bot; `.tb state <bot>` shows `free=no`. `.tb ungroup` ends the binding: the bot rejoins
   the count with a fresh session (the next stop drains it too).
6. Target up: set `.TargetOnline = 20`, `.reload config` → logins continue up to 20 and stop there; no 60 s window with more
   than 20 `pool login` lines.
7. Target down: `.TargetOnline = 5` → `pool logout ... reason=target` for the newest sessions first, at most five in any
   second, until 5 remain.
8. `.tb kill <one bot>` a few seconds before its session's end: no `pool logout` for it while it is down; it logs out after it
   revives (or 10 minutes after its session's end), then comes back after its offline time.
9. `.tb pool stop`: `pool stop: N pool bot(s) will be logged out, 5 per second`, then `reason=stop` lines five a second,
   `state=stopped online=0` inside `ceil(N/5)` seconds, `.tb status` 0 bot sessions, `.tb pool status` `drain=0`; no
   `ASSERTION FAILED`, clean shutdown.

## 7. Unknowns and stated limits

- The rolled session and offline lengths are not stored (the schema is W0-A's): a restart gives every bot the minimum offline
  time from boot or from its `last_logout`, so arbitrary offline periods are shortened and all bots fall due together, and
  every session that was running gets a fresh length. A clean shutdown runs `LogoutAll` and no scheduler pass afterwards, so
  its rows still read `last_login > last_logout` and take the boot case. It does less, never invents a value.
- A bot bound with `.tb group` (not a persisted commander binding) is "bound" only in this process: `.tb free on`, `.tb
  ungroup`, or the logout that ends the binding clears it, and after a restart the bot is offline, reads as free and may be
  logged in again as a pool bot. A persisted commander binding re-binds it.
- A bot logged in by hand (`.tb login <name> free`) is not pool-tagged; the pool ignores it, but while its session exists its
  account is busy (`BotMgr::BotAccounts`), so the scheduler picks no character of that account.
- `offlineUntil` is the minimum offline time after a restart, and the dead wait counts from the session's end, not from the
  death itself: a bot that died long before its session's end is logged out at the end (it was alive then). A living bot in
  combat has no such cap: its session is ended only once the fight is over.
- A vanished registry row is not forgotten while its bot is online; the track goes when the bot is offline, after the logout
  the session end or stop asked for. The row itself is not recreated: the registry's next reconciliation owns that.
- An account a bound bot sits on is out of the pool's reach until the binding ends: the pool never logs that bot out, and one
  session per account means no second character of the account may enter. A pool with as many accounts as its target (the live
  pool: ten accounts, target 10) therefore runs one short while a bot is bound; the status line's `capped by accounts` and the
  bind's log line say so. With more accounts than the target, the next pass refills the slot.

## 8. The review's findings and their fixes

| Finding | Fix | Test that fails before |
|---|---|---|
| HIGH: the account guard only asked `sWorld->FindSession`, so two pool characters of one `TBPOOLnnnn` account ran at once | the policy derives the occupation from its own tracks (`PoolOccupiedAccounts`), the host seeds it with the world session and `BotMgr::BotAccounts` (pool and non-pool bots, loading and online, once a pass), and `BotMgr::Login` refuses a second bot of an account; the cap is documented in the conf and shown as `capped by accounts: N` | the policy test's 20 accounts × 10 characters case (old code: 200 sessions; now: 20, never two of one account) |
| the `Loading → Online` switch skipped `bound` | `PoolSyncMaster` runs on that switch as well as on `Online` | the transition case (a master bound while loading is seen on the same pass) + the `bound-not-seen-when-online` mutation |
| a lowered target and a session end ignored a corpse and a fight | `inCombat` is part of the state; a living bot in combat waits for the fight, a lowered target takes the next newest, and the stop stays unconditional | the combat and target-drain cases + their mutations |
| `bound` was never cleared | `.tb free on` or a binding that ended (`.tb ungroup`, `ClearBotMaster`) clears it with a fresh session; leaving the world clears it (master grace); a vanished row keeps an online track | the transition, logout, binding-end and `PoolTrackKept` cases + their mutations |
| `Refresh` dropped an online bot's track | `PoolTrackKept` keeps it until the bot is offline | the `PoolTrackKept` case + `track-dropped-while-online` |
| no shutdown guard | `PoolPassInput::shuttingDown` from `sWorld->IsShuttingDown()` plans no logins; logouts still run | the shutdown case + `shutdown-logins` |
| the restart note claimed more than it does | section 2 and 7 now state exactly what a restart reconstructs (minimum from boot or stamp, all due together, fresh session) | the notes text and the conf's `OfflineMinutes` comment |
| the live check's deviation: `online` 10 → 9 after a bind and no refill in the next 3 min | not the count (the status read 9) and not the event hooking (the plan runs every pass): the live pool had ten accounts for a target of ten, the bound bot keeps its account's session, and one session per account leaves no account to add a tenth bot on. The refill happens on the next pass whenever an account is free; the cap now leaves out the accounts bound bots hold, so `.tb pool status` reads `capped by accounts: 9` and the bind's log line names the accounts left | the refill case (20 accounts, one online each, a bind → one login in the same pass, no logout of the bound bot) and the cap case (10 accounts × 10 characters, a bind → `eligible` 0, `PoolAccountCount` 9) |
