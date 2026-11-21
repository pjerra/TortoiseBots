# S2: pushback and failed casts (FOREVER-SMALL-SLICES-SPEC section 3, S2)

Branch `slice/s2` from `tbf/main` 495ffd943, core 25fc7ca. One worker-day. Nothing here is a new bot behaviour: the host hands the engine two
events the core already sends and the Turtle engine already reads.

## 1. What changed

| Event | Turtle | ForeverCore before | ForeverCore now |
|---|---|---|---|
| pushback (`SMSG_SPELL_DELAYED`) | the bot session's packet table copies it, `PlayerbotAIPackets.cpp:202-217` adds `delaytime` (<= 1000) to the engine's internal update | the session dropped everything except the two failure packets: the engine kept waiting the original bar | the session reads the core's typed packet, the engine's wait grows by the delay |
| cast failed at the end of the bar (`SMSG_CAST_FAILED`) | logged (`CAST_FAILED_ASYNC`), `SpellInterrupted` came only from `SMSG_SPELL_FAILURE` | logged; the engine waited on a bar whose spell had already failed | logged and `SpellInterrupted` (D4): the wait is cut to the rest of the GCD / react delay, the next tick plans at once |

### Files

| File | Change |
|---|---|
| `forever/src/TbBotSession.h` | `CastResult::Kind::SpellDelayed`; `CastResult::delayMs`; `CastResultCounts::delayed`; `InjectSpellDelay`; `CaptureSpellDelay`; the queue and `SendPacket` docs name the third packet |
| `forever/src/TbBotSession.cpp` | `SendPacket` routes `SMSG_SPELL_DELAYED` to the new reader; `CaptureSpellDelay` (`ObjectGuid Caster, uint32 ActualDelay`, own caster only, copies the packet first, `malformed` on a short one); `PushCastResult` counts `delayed`; `InjectSpellDelay` (typed `WorldPackets::Spells::SpellDelayed` through `Player::SendDirectMessage`, injected flag, same refusals as `InjectCastResult`) |
| `forever/src/host/TbSpellDelayPure.h` | `TortoiseBots::sd::PushbackExtraMs(int64_t)`: 1..1000 -> the delay, anything else -> 0 (the legacy `<= 1000` rule) |
| `forever/src/host/TbEngineBrain.cpp` | `DeliverCastResult`: `SpellDelayed` -> action log `SPELL_DELAYED delay=<n> extra=<n>` + `pai->NoteSpellDelayed(extra)`; the async `CastFailed` path ends with `pai->SpellInterrupted(spellId)`; `InjectSpellDelay` (engine wrapper + OFF stub); `.tb where`'s cast-result line prints `delayed` |
| `forever/src/host/TbEngineBrain.h` | `InjectSpellDelay` declaration; `Tick` doc |
| `forever/src/TbScripts.cpp` | `.tb pushback <bot> <ms>` row (appended last in `tbTable`) and `HandlePushback`; `.tb castlog` prints `kind=SpellDelayed delay=<n>`; `CastResultKindName` |
| `forever/src/host/TbPartyState.cpp` | state record `delayed=<n>` appended after the cast-result counters |
| `ai/playerbot/PlayerbotAI.h` | public `NoteSpellDelayed(uint32)`: the engine's wait grows (`IncreaseAIInternalUpdateDelay` is protected in `PlayerbotAIBase`); unused on Turtle, so no Turtle behaviour changes |
| `ai/playerbot/UnportedFeatures.h`, `forever/docs/UNPORTED.md` | the two rows ("incoming: cast failed trigger", "incoming: spell delay") reworded "host path (S2)"; both stay (the legacy cases still reference `IncomingSpellState`) |
| `forever/docs/TB-COMMANDS.md` | `.tb pushback` (section 2) and the `delayed` state key |
| `forever/SLICE-SD.txt` | W1's census/build list (this round changes no legacy source; its only file outside `forever/` is a header) |
| `forever/tools/test_spell_delay.cpp`, `test_sd_wiring.py` | see section 3 |

### Shared lines touched (other rounds work in the same files)

* `forever/src/TbBotSession.h` / `.cpp`: S2-only files; the additions are new lines and the `SendPacket` branch.
* `forever/src/host/TbEngineBrain.cpp`: S1 adds `DeliverChat`, S3 adds `NoteThrown`; S2 only edits inside `DeliverCastResult` (two blocks), adds one include, one `Describe` string and one function + stub.
* `forever/src/TbScripts.cpp`: one row appended after `free`, one handler after `HandleCastFail`, one helper in the anonymous namespace, two includes; no existing row changed.
* `forever/src/host/TbPartyState.cpp`: three appended lines after the cast-result counters in `StateRec`.
* `ai/playerbot/PlayerbotAI.h`: three added lines after `SpellInterrupted`.
* `ai/playerbot/UnportedFeatures.h`, `forever/docs/UNPORTED.md`: only the two rows of this round.
* `forever/docs/TB-COMMANDS.md`: two added rows.

No `slice_gen_context.py` key block: S2 registers no strategy, action, trigger or value, so `SD_KEYS` would be empty and adding it to the
`_join(...)` list would only create a merge point. The generator's `--check` stays "up to date".

## 2. Why it is safe

* The packet is copied and read by the core's own `operator>>` in the order `SpellDelayed::Write` writes it; a short packet is a
  `ByteBufferException` and only increments `malformed`. A broadcast of another caster's pushback is dropped before it reaches the queue
  (the packet goes to the whole set; `Spell::Delayed` sends it with `SendMessageToSet(..., true)`).
* Only the bot's own in-flight cast bar is affected: `NoteSpellDelayed` adds to the engine's countdown (`PlayerbotAIBase::UpdateAI`), it
  never touches the core's `Spell`. A stale or injected delay costs at most 1 s of engine wait, exactly the legacy rule.
* `SpellInterrupted` acts only when the failed spell is the engine's "last spell cast" and clears it; a second failure of the same cast is a
  no-op. No "cast failed" trigger is fed and nothing is whispered (D4).
* Fix round (Codex [high]): the legacy `SpellInterrupted` counts whole wall seconds (`time(0)`, `PlayerbotAI.cpp:3184`) and does nothing when the
  failure lands in the second the cast started, so a sub-second failure (target died 0.4 s into the bar) left the engine waiting out the whole
  bar. **Turtle-side known limit, left unchanged:** Turtle keeps the whole-second rule. In the ForeverCore host, `RearmAfterCastFailure`
  (`TbEngineBrain.cpp`) calls the legacy `SpellInterrupted` first and, when that left the cast in place, clears it and cuts the wait on a
  millisecond clock (`sd::FailureWaitMs`: the rest of the global cooldown, else the react delay; wrap-safe) through the new public
  `PlayerbotAI::NoteCastFailureWait`. The cast start is stamped in `getMSTime` after each engine step that changed the engine's "last spell
  cast" (`NoteCastStart`), at most one 50 ms step late. It applies to both `SMSG_SPELL_FAILURE` and the async `SMSG_CAST_FAILED`; the action log
  writes `CAST_FAILURE_REARM spell= wait=` when it acts. Live check c) should now show the cut wait for sub-second failures too.
* The fallback (no integer the engine trusts) does LESS: no delay is added.

## 3. Tests

* `forever/tools/test_spell_delay.cpp` (standalone): `pushback_500_extends_wait_500` first; 1 and 1000 kept; 0, 1001, -1 and both int64 ends
  add nothing.
  `failure_same_second_cuts_wait_to_gcd_rest` (a failure 400 ms into the bar, same wall second: the wait is the rest of the GCD, below the bar),
  plus GCD end, no GCD and ms-clock wrap cases.
* `forever/tools/test_sd_wiring.py`: reads the session reader (opcode in `SendPacket`, caster compare, read order, queue kind, `delayed`
  counter), the injector (typed packet through `SendDirectMessage`), the engine delivery (`PushbackExtraMs`, `SPELL_DELAYED`,
  `NoteSpellDelayed` with its `return;`, `RearmAfterCastFailure` on both failure paths, `NoteCastStart` after the engine step), the command row and the state key, and the doc. `--mutate` resumes one
  mutation that must fail (the caster test).

Gate lines (build VM, slot 8; `FOREVER_CORE=~/ForeverCore-25fc7ca`):

* ON worldserver (`FOREVER_CMAKE_EXTRA=-DTORTOISEBOTS_SLICE1=ON ~/forever-build/build.sh 12 worldserver`): `CMAKE_RC=0` / `MAKE_RC=0` (994 s, the binary the live test ran)
* OFF module (`~/forever-build/build.sh 12 tortoisebots`, no extra options): `CMAKE_RC=0` / `MAKE_RC=0` (1552 s, shared key `core-af0b412975f7-opt-78bf025f`)
* census, `FOREVER_CENSUS_LIST=forever/SLICE-SD.txt`: `files 286  compile clean 286  with errors 0  error places 0 (in 0 files)  includes of a header ForeverCore does not have 0`
* registry: `(a) slice keys that are not in the full build, or map elsewhere: 0`
* generator: `SliceAiObjectContext.cpp is up to date` (`--check` rc 0)
* `vm-test tb` on this worktree (`d2b2ed1d9`, then `6a35fa2b7`'s code): `RESULT: FAIL`, one unit test only:
  `test_mail_take_settles_the_ledger_at_any_order_state` (`'HandleMailTakeMoney(' not found in ... MailTakeMoneyCompat(bot, mailbox, mail);`,
  the guild-order relist pin in `ai/playerbot/strategy/actions/MailAction.cpp` — the guilds developer's area, not S2). The same run on `tbf/main`
  9753dcf9e (`vm-test tb` on a detached `tbf/main` tree) fails with exactly the same test and the same trace-score lines, so S2 adds no failure.
* Turtle compile gate (`vm-build ~/wt-s2 --wait`, job `1-1795204329035278556-1772830`): **RED — a pre-existing break, not this round's code.**
  `MAKE_RC=2` in `ai/cmangos-compat-unported.h:126`:
  `error: 'char* ChatHandler::ExtractKeyFromLink(char**, const char*, char**)' is protected within this context` /
  `error: 'ChatHandler' is not an accessible base of 'PlayerbotChatHandler'`, compiling `ai/playerbot/PlayerbotAI.cpp`. That file is
  byte-identical to `tbf/main` (`git diff tbf/main -- ai/cmangos-compat-unported.h` is empty); W3 made `ExtractQuestIdCompat` a free template,
  and the Turtle core names both link parsers `protected` in `ChatHandler` **and** `PlayerbotChatHandler` derives from it `protected`, so a free
  function can never call them. S2 does not change that code; it only forces the TU to be recompiled (the new public `NoteSpellDelayed` in
  `PlayerbotAI.h`). Any full Turtle build hits it: the S1-R1 chat-links round's build (job `1-1795204280920652060-1762147`, 20:51) failed the
  same way at their new `ai/cmangos-compat-cl.h:14` (`'ExtractSpellIdFromLink' is protected`). The one-line fix belongs to W3/S1-R1 (make the
  two parsers public in `PlayerbotChatHandler`, as `extractSpellId`'s lambda-in-member pattern already does, or route Turtle through D3's
  parser); S2 does not touch that seam. What S2's own header change costs on Turtle is nothing: `NoteSpellDelayed` is an unused public inline
  that calls `IncreaseAIInternalUpdateDelay` on the base class.

## 4. Live acceptance (forever-run2, 25fc7ca, slot-8 ON build of this branch, server lock held, DB dumped to
`/mnt/forever/s2-run2-pre.sql.gz` before and restored after; binary `bin/worldserver.pre-s2` put back and the server boot-checked)

Bots: `Mtank` (warrior 18, master) and `Mpriest` (priest 18), `.tb login ... Mtank`, both engines on, in Ragefire Chasm (map 389, the two
Earthborers 11320 / Molten Elemental nearby instead of the spec's young wolves: an instance was where saved characters stood; the mechanism
`Spell::Delayed` is the same).

Before: `bin/worldserver` copied to `bin/worldserver.pre-s2` and the four databases dumped to `/mnt/forever/s2-run2-pre.sql.gz`
(`gzip -t` OK, 261 MB). After: server stopped, the binary put back (`65678144` bytes, byte count of the pre-test file), the dump restored
(`mysql` exit 0), and the restored server boot-checked: `worldserver-daemon) ready...`, `TortoiseBots: enabled, 0 bot session(s)`, stopped.

**(a) injected pushback mid-bar** (Greater-heal family: `Heal(2054)`, 3,000 ms). Action log of Mpriest:

```
[20:57:23.947] [CAST_START] spell=Heal(2054) castTime=3000ms
[20:57:25.100] [SPELL_DELAYED] delay=500 extra=500 injected=1
[20:57:27.461] [TICK] --- AI Tick --- ...
```

The bar ends 26.947; with the pushback the first tick after it is 27.461 (bar + 500 + one engine step), not ~26.95: the engine did not act
early. `.tb pushback Mpriest 500` answered `TortoiseBots: Mpriest: sent SMSG_SPELL_DELAYED 500 ms; the engine adds 500 ms to its wait
(\`.tb state\` shows delayed=<n>).`

**(b) real pushbacks, two mobs on the casting priest.** Two Earthborers hit Mpriest during `Smite(598)`/`Heal(2054)`:

```
[20:59:09.661] [CAST_START] spell=Smite(598) castTime=2500ms
[20:59:11.112] [SPELL_DELAYED] delay=500 extra=500
[20:59:11.512] [SPELL_DELAYED] delay=500 extra=500
[20:59:13.166] [CAST_START] spell=Heal(2054) castTime=3000ms
```

The next cast starts after the Smite bar (12.161) plus both pushbacks (13.161), not before it. No second `CAST_START` while the bar ran.
`delayed` grew as they arrived: `.tb state Mpriest` -> `cast_queued=6 cast_delivered=6 cast_dropped=0 cast_pending=0 delayed=4`. The priest
died to the two mobs afterwards (it pulled them alone); no crash, no queue overflow.

**(c) a failed cast at the end of the bar** (D4). Injected: `.tb castfail Mpriest 2054 71` -> `[20:59:47.336] [CAST_FAILED_ASYNC]
spell=2054 reason=71 injected=1`, next `[TICK]` 100 ms later (react delay). Real line-of-sight failure: the tank was hopped out of sight
during a `Lesser Heal(2053)` bar:

```
[21:01:07.331] [CAST_START] spell=Lesser Heal(2053) castTime=2500ms
[21:01:09.833] [CAST_FAILED_ASYNC] spell=2053 reason=71
[21:01:09.833] [ACTION] A:reach party member to heal - OK src=party member to heal out of spell range
```

The engine re-planned in the very tick the failure was drained and the "reach spell" move to the out-of-sight patient started at once.

**Counters after all of it:** `.tb where Mpriest` -> `cast results: queued 8, delivered 8, dropped 0 (queue holds 32), pending 0, delayed 6;
refused during the engine's own tick (not queued) 0, unreadable 0`. `.tb state` -> `delayed=6`. Boot line: `slice: 72 features not ported`
(no row added or removed; only the two rows' text changed).

## 5. Uncertainties

* `ai/playerbot/PlayerbotAI.h` gains the public `NoteSpellDelayed`: `IncreaseAIInternalUpdateDelay` is protected in `PlayerbotAIBase`, and the
  host may not be a friend. The method is unused on Turtle (same behaviour there); it is the only file outside `forever/`, so the Turtle
  build and unit-test gates were run.
* `delayed` counts entries ACCEPTED into the cast-result queue (like `cast_queued`), not delivered ones; delivery is `cast_delivered`, and
  an entry dropped by a full queue is not counted as delayed. That matches the spec's "same queue and counters, delayed counter".
* The live fight used two Earthborers in Ragefire Chasm instead of two young wolves in the open world: the saved characters stood inside the
  instance and the core's `Spell::Delayed` path is mob-agnostic. Both real pushbacks and the injected one are quoted above.
* The spec's "no recast while the bar runs" is shown from the action log (no `CAST_START` between a bar's start and its pushed-back end); the
  engine's own tick cadence (50 ms) is visible in the same log and is what makes the check meaningful.
* `.tb pushback` is a test helper; it does not make the core delay the bar, only tells the engine (as the real packet does). The real path is
  the (b) case.
