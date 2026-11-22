# BG1: battlegrounds, queue, join, leave, death (round BG1)

`BATTLEGROUNDS-SPEC.md` round BG1. The round sits behind `TortoiseBots.Bg.Enable` (default 0). Nothing in it needs a core change: every
call is a public `WorldSession`/`Player`/`BattlegroundMgr`/`ScriptMgr` member the core itself uses.

## What it does

- **Fill a human's queue** (`host/bg/TbBgQueue.cpp`, 5 s while a non-bot session waits): free pool bots of the *human's* bracket and
  faction are queued through `WorldSession::JoinBattlegroundQueue` until each side holds `HumanBgSideTarget(min, max)` — uninvited
  queue plus invitations still in flight, because the core's `GetQueuedPlayersCount` skips invited groups. At most 5 bots per side
  per check. `TortoiseBots.Bg.MinPlayersPerTeam` (default 5) is the module minimum, because the pinned core's BattlemasterList rows
  for 489/529 say 1 per team. A side still short after 180 s gets one system line to the waiting humans (`have of target`). A queue
  whose other side has no bots at all is left empty instead of holding a half match.
- **Creation veto** (`WorldScript::CanCreateBattleground`, the core's own hook, called after its free-slot fill): the core starts a
  match as soon as each side reaches its own minimum (1), so the hook holds a new instance back while a human waits and the fill can
  still reach the target. Once the target is held (or the 180 s deficit window passed) the instance starts; the core's free-slot fill
  then invites the waiting bots into it. A bracket with no human and only the module's bots never creates an instance unless
  `Bg.Autonomous` is on (D4.3). Every fill pass also calls `BattlegroundMgr::ScheduleQueueUpdate` for each bucket with a human demand
  or a module-queued bot, because the core re-evaluates a queue on a join, a decline, a leave and an invite timeout — never on an
  accept, which left the bots waiting after the human entered with nothing to move them.
- **Accept** (`PlayerScript::OnBattlegroundInvite`): after a rolled 2-8 s, through the typed `CMSG_BATTLEFIELD_PORT` packet and
  `HandleBattleFieldPortOpcode`; the D4 refusals that still matter at that moment are re-checked, the caps too (they count every bot
  the module holds). A dead or fighting bot still accepts (the core revives a dead accepter and combat does not block entry). A bound
  bot accepts only after its human has **entered** the instance (`Battleground::GetPlayers()`, not merely been invited); if the
  human never gets in, the invitation is declined one margin before the core's 90 s invite window ends (`INVITE_ACCEPT_WAIT_TIME`). A free bot follows the same
  presence rule for any human: it enters only an instance a human is **inside**, and gives the slot back at the end of the
  invite window if none arrives. An invited human does not count (his invite can lapse, leaving the bots alone in the
  instance).
- **Queue cleanup**: a bot *the module queued* that got a master or a run leaves with the core's own
  `BattlegroundQueue::RemovePlayer` + `RemoveBattlegroundQueueId` pair, which also schedules the bucket's update. A module bot whose
  bucket lost its human leaves after a 10 s grace (`why=no-human`), and any queue entry older than 9 min is dropped
  (`why=queue-timeout`), so a pool session never waits in a queue forever. A bound bot a human's group queued follows that human:
  when the human cancels his queue, logs out or unbinds it, the bot leaves the queue at once (`why=master-gone`); the core's own
  leave of bound bots runs only on the human's `CMSG_BATTLEFIELD_LEAVE` inside a match, so without this the bot would stay queued
  with no human and no end. Only a *running match* holds a bound bot's logout (`TbBotMgr`); a queue entry never does.
- **Leave**: a free bot leaves `STATUS_WAIT_LEAVE` after a rolled 10-30 s (`LeaveBattleground()`) and an unfinished match only at
  `Bg.MaxMinutes` (`LeaveBattleground(true, true)`, no Deserter, D5). A bound bot leaves with its human (the core does that), or after
  60 s in an instance its human never reached (`why=master-gone`, no Deserter). The pool scheduler holds a bot's session only while it
  is *inside* a match (its rolled end is deferred); a queued bot holds nothing — its entry leaves with the session and the fill queues
  another. MaxMinutes is also what lets `.tb pool stop` finish.
- **Death**: a dead bot in a WSG/AB releases at once (typed `CMSG_REPOP_REQUEST`) and only treats the release as done when the core
  set the ghost flag; it queues at the nearest area spirit healer (walking into the core's 20 yd range if it must) and waits for the
  wave, reading the core's own `GetSpiritHealerGUID()` rather than a cached flag. W1's `GhostRescue` is off inside a battleground.
- **Data**: `host/bg/TbBgMapData` loads the flag stands (179830/179831), the five AB node banners (180087-180091) and both start
  locations from the core's spawn store and world-safe-locs rows at boot; a missing one turns that map off with one error line. No
  position is typed in: the only float constants live in `policy/bg/BgLimits.h`.
- **Status and events**: `.tb bg status`, `.tb bg events [since=N]` (TB-COMMANDS section 22).

## Pure half (core-free, tested)

`ai/playerbot/policy/bg/`:

- `BgIds.h` — the maps (489/529), list ids (2/3), GO entries, world states; `forever/tools/test_bg1_wiring.py` greps the pinned core.
- `BgQueuePolicy.h` — D4 eligibility in the spec's order, D5's queue cleanup and follow rule, and the bucket rules the host reads
  every pass: `BgSideNeed` (queued and invited count), `BgVetoCreate` (the creation veto), `BgAcceptEligible` (the refusals that
  still matter at accept), `BgAcceptCapsOver` (an invitation enters an instance that already exists: only Bg.MaxBots is re-read,
  never the instance cap), `BgCapsOfTracks` (D9: every held bot counts; a running match is identified by its instance and a queued
  bucket is a separate reservation, so a match running in the same bracket does not pay for a new queue; a leftover entry whose
  humans are already inside reserves nothing, and only the fill's own bucket is exempt), `BgBoundQueueHeld` with
  `BgHumanInTrackMatch` (only the human's entry into this track's own battleground and instance holds the queue), `BgQueuedDrop`
  (orphan grace and hard timeout) and `kBgDeficitMs`.
- `BgJoinLeavePolicy.h` — the accept and leave rolls, `BgHumanEntryInviteAnswer` (a free bot needs a human inside its instance, a
  bound bot its own human; no "invited" input), `BgInviteDeadlinePassed` and `BgMasterLeave` with `kBoundMasterGraceMs`.
- `BgLimits.h` — the only float constants.

## Tests

```
g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/test_bg_queue_policy.cpp       # 53 checks
g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/test_bg_bucket_policy.cpp      # 70 checks: need/veto/drop/accept/caps/leftovers/follow
g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/test_bg_join_leave_policy.cpp  # 8234 checks: rolls, invites, MaxMinutes, master grace
g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/test_pool_schedule_policy.cpp  # the BG hold and the stop skip
g++ -std=c++17 -O1 -Wall -Wextra tools/test_human_bg_demand_policy.cpp              # the min-5 case (reuses the shared policy)
python3 -m unittest forever.tools.test_bg1_wiring                                   # the wiring pins (62 tests; the core-text pins skip without a checkout)
BG_MUTATE=1 python3 -m unittest forever.tools.test_bg1_wiring                       # each pin fails on its own mutation (37)
python3 forever/tools/slice_gen_context.py --check                                  # clean
```

The pure cases are the ones that can fail before their code exists; the host paths (demand bracket, caps snapshot and own bucket,
orphan drop and bound follow including the human's own battleground, veto wiring, bound-master enter and leave, the presence rule
for invitations, the managed-map join gate, map off reason) are pinned on the source text, and `BG_MUTATE=1` reverts each
guarantee in turn and requires the suite to fail (37 mutations). The `test_human_bg_demand_policy.cpp` case exercises the shared
`HumanBgDemandPolicy.h` that BG1 reuses rather than new code.

Gates: module ON and OFF (`-DTORTOISEBOTS_SLICE1=ON|OFF`), the full `worldserver` link, and `tbf-premerge`. The audit note
The round's audit note carries the R0 facts used and the live checks the lead's queue still owes (L-Q).
There was no live run in this round.

## Open item: D4.2 short pool

D4.2 says offline in-bracket pool characters are logged in for a short pool. This round uses only bots the pool already has online:
the W0 pool is level 1, so a WSG 10-19 or AB 20-29 queue is never filled from the pool as shipped (L-Q uses the test kit's level 19/29
bots). Logging in a bracket-matching pool character on demand belongs to the pool scheduler (its login rate, accounts and target) and
is not part of BG1: **D4.2 is open, to be closed with the pool work behind BG3**.

## Risks

- The match's own minimum is the core's (1 per side on the pinned rows). The veto keeps a new instance from starting before the target
  is held, but once it starts the first invitations are the core's pick; the rest follow through the free-slot fill within a queue
  update (5 s at most).
- The W0 pool is level 1: a bracket with no levelled pool bot gets the 180 s line instead of bots.
- Arenas are out: an arena invite has no map in the data loader and a bot inside an arena is not managed.
