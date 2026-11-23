# PROOF-1 notes: the GM observer and the run's self-recover

Spec: `~/TortoiseBots-audit/specs/SPEC-TortoiseBotsForever-proof-1.md` (RFC milestone proof, problems 1 and 2). Branch `run/proof-1`, based on `tbf/dev`
(016bf53a2) with `tbf/run/wc-r9` merged in (clean; brings the healer gate and the walk back that pulls respawned packs, which the self-recover reuses).

## Part A: a GM observer may start/pause/resume/stop a `.tb run`

Cause: `CheckBotAccess` follows one hop from the bot's commander (the run's bot commander, e.g. Pcmd) to that commander's commander; for a bot commander
that is empty, so `binding` stays the bot commander and `guard::CheckControl` refuses every real caller with foreign-bot.

Decision (narrow; no other command changes):
* `TbGuardPure.h`: `ObserverMayControlRun(optionOn, senderIsGm, chainEndsAtPlayer)` = `optionOn && senderIsGm && !chainEndsAtPlayer`.
* `TbH3.cpp`/`TbH3.h`: `CheckRunObserverAccess(handler, bot, code, detail)`. `CheckBotAccess` first; on a refusal it follows `CommanderGuid` up to 4 hops from
  the bot, stopping at an empty guid or a non-bot guid. `chainEndsAtPlayer` is true only when the chain stops at a non-empty guid that is not a host bot.
  `senderIsGm` = the session's security `>= SEC_GAMEMASTER`; `optionOn` = `TortoiseBots.Run.GmObserver` (default true). On the exemption it clears
  `code`/`detail` and logs `TortoiseBots run: GM observer <name> allowed on <bot> (bot commander)`; otherwise the original refusal stands. An
  `ObjectGuid` overload serves `stop`/`pause`/`resume`, where `rec->leader` may not be in the world (an empty guid allows, as `CheckBotAccess` does).
* `TbH3Off.cpp` stub returns true (the option-OFF build's guard allows like `CheckBotAccess` there).
* `TbRunCommands.cpp`: `start` asks it for the leader (and, when allowed, skips `CheckPartyLeadAccess` for that leader) and for every member in the loop;
  `stop` and `pause`/`resume` ask it for the leader. `status` and `events` are untouched.
* `TortoiseBots.Run.GmObserver = 1` documented next to `TortoiseBots.Create.TestAccounts` in `forever/conf/tortoisebots.conf.dist`.

## Part B: the run recovers by itself after "regroups did not hold"

Today the policy pauses after `kAutoRegroupsPerStep` (2) of its own regroups at a step, and both remedies in the note are lines from outside the run.

Decision: the host answers that pause itself, with what a player party does. `TbRunRecoverPure.h` (`TortoiseBots::run::recover`):
`kMaxRecoversPerStep = 1`, `kMaxRecoversPerRun = 3`, `kRecoverFarYd = 400.0f`; `IsRegroupsDidNotHoldPause` matches the policy's phrase
`" regroups by itself at this step did not hold"` anywhere in the note (both policy sites: the lead revived away and the wipe). `Decide(state, step, note,
farthestYd, optionOn)` answers only this pause (every other pause: empty `why`), starts the per-step count over on a new step, and gives a reason in every
case: option off, a member 400+ yd away, the step cap, the run cap; otherwise it grants the recover and counts it.

`TbRun.cpp`: `RunRec::recover` (`recover::State`). Right after the gather block (after `DungeonRun::Tick`, before the stall/approach stands, the executor
and `Observe`), when the run is paused on this note: `Decide` runs with `facts.maxMemberYd` and `TortoiseBots.Run.SelfRecover` (default true). On a
recover, if a host bot of the party is dead and not a ghost the tick issues `revive party` through the run's own `Issue(rec, ...)` (flagged `fromRun`,
baseline taken, so it is never an outside order), then `DungeonRun::Resume(run)` begins the policy's fresh regroup (revive wait, gather, rest, the
wc-r9 walk back that pulls respawned packs one at a time), and the event `self-recover` carries `Decide`'s reason. On a refusal the event
`self-recover-stop` carries the reason and the pause stands. No teleport, no GM revive, no summon, no rebuff order (the bots' out-of-combat buff
strategies run during the rest).

## Tests (red first, then green)

`test_guard.cpp` (17 checks, 0 failed) — `TestObserverGmBotCommander`, `TestObserverGmPlayerOwned`, `TestObserverNonGm`, `TestObserverOptionOff` were red
before `ObserverMayControlRun` existed (compile error on the old header).

`test_run_recover.cpp` (31 checks, 0 failed) — was red before `TbRunRecoverPure.h` existed; `TestRecoverMatchesThePolicyText` builds the two pause notes
the way the policy builds them (the policy's own `kAutoRegroupsPerStep` and wording), `test_recover_wiring.py` pins that wording in
`runtime/DungeonRunPolicy.h` (both sites) and in the matcher, so a policy text change fails a test. Also `TestRecoverOncePerStep`,
`TestRecoverNewStepResets`, `TestRecoverRunCap`, `TestRecoverOtherPauseSilent`, `TestRecoverFarMember`, `TestRecoverOff`.

`test_run_gather.cpp` (21 checks, 0 failed) unchanged, still green.

`test_run_observer_wiring.py` (5 tests OK) — `start`/`stop`/`pause`+`resume` ask `CheckRunObserverAccess` (four call sites), no file under
`forever/src/host/cmd/` names it, the guard rule, the config default and the `.conf.dist` key.

`test_recover_wiring.py` (4 tests OK) — call order (`Tick` < gather block < recover < executor < `Observe`), the policy wording, the `revive party` order
and its baseline, the option and its `.conf.dist` key.

## Mutations (each must fail a named test)

| Mutation | Test that must fail | Result |
|---|---|---|
| drop `senderIsGm` | TestObserverNonGm | FAIL `test_guard.cpp:33` (1 failed) — caught |
| drop `chainEndsAtPlayer` | TestObserverGmPlayerOwned | FAIL `test_guard.cpp:27` (1 failed) — caught |
| ignore the option | TestObserverOptionOff | FAIL `test_guard.cpp:40` (1 failed) — caught |
| step cap 1 → 2 | TestRecoverOncePerStep | FAIL (3 checks, `test_run_recover.cpp:57`, `:67`, `:69`) — caught |
| run cap removed | TestRecoverRunCap | FAIL (4 checks, `test_run_recover.cpp:85`, `:86`, `:89`, `:91`) — caught |
| prefix match on a different pause (`note.compare(0, 7, "regroup")`) | TestRecoverOtherPauseSilent | FAIL (`test_run_recover.cpp:98` and 25 more) — caught |
| recover call moved before the gather block | test_recover_wiring.py | FAILED (failures=1, `test_call_order`) — caught |

Every mutation was applied to the built tree, the test run, and the file restored; the restored tree is what the final test run and the gate below used.

## Gates

```
base: tbf/dev
merge: clean (48 commits over tbf/dev)
pytest test_forever_no_direct_queries.py: PASS
COMPILE_SECONDS=8 CMAKE_RC=0 MAKE_RC=0 turtle: PASS
slice1-ON: PASS
slice1-OFF: PASS
PREMERGE: PASS
```

Run on `run/proof-1` (`tbf-premerge ~/wt-proof1 4`), after the two earlier runs reported the two compile errors the code now fixes (an `sLog`
pointer in `TbH3.cpp`, and `rec->leader` passed to the `Player const*` guard; the guard now has an ObjectGuid overload for a run whose leader is not
in the world). Both builds (SLICE1=ON and OFF) compile, the pure tests above and both wiring tests pass, and the test files themselves are unchanged
between the last test run and this gate.
