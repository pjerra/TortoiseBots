# A. Commander P3: one order path

**Start from `main` after Commander P2 (branch `cmd/p2`, last fix round now) has merged.** P2 edits `TbRunOrders.cpp`
(15 lines), `TbRun.cpp`, `TbCmdBinding.*` and `TbScripts.cpp`, which this item lifts.

## Done looks like

A bound commander types `.tb cmd <verb> ...` and his bots carry it out through the **same judge** the self-led dungeon run
uses, with the same answers for the same facts.

**Live acceptance:** a party of five bots bound to a GM commander (`.tb master party <tank> <commander>`) inside Ragefire
Chasm: `.tb cmd pull <32-hex guid>`, `.tb cmd move x y z`, `.tb cmd rest` each answer ACK and the bots act; a move across
lava answers a refusal, not ACK; the same verbs typed while a run is going give no double orders. `.tb commander status`
shows the orders in the card's log.

## What it is (from the Commander plan)

"Lift the run executor to `Party` (leader + members + a target resolver: run handle **or** 32-hex guid). `.tb cmd <verb> ...`
addresses the caller's binding: move [direct], pull, pullback, attack, focus, rest, revive party, stay, follow. Answers
ACK/ERR in grammar v2." Add `hold` (today `.tb order ... hold` = `+passive`, TB-COMMANDS.md section 4) and **target icons**:
the attack/pull target gets the group's raid icon so a human sees it.

Today the executor is `run::Issue(RunRec&, order, nowMs, gather)` (`forever/src/host/run/TbRunOrders.h`; body
`TbRunOrders.cpp:704`). It needs nothing from `RunRec` except `rec.leader`; handles resolve through `ResolveHandle`
(`TbRunOrders.cpp:334`), and a handle that no longer resolves is refused `invalid-target`, never guessed. Already in it:
the path read (`PathGenerator`), ground and LOS (`GetHeightCompat`, `IsInLineOfSightCompat`, `:385-388`) and the ONE liquid
test (`forever/src/host/run/TbRunLiquid.h`, `liquid::SegmentPrefixRefusedOnMap`, `:417`): status by bits like the core's
`IsInWater`, any liquid at or above z-0.6 refuses, lines sampled every 1.5 yd, no terrain = refused. Refusal codes in
`OrderAnswer::code`: `invalid unsupported lead-unavailable not-bound no-engine no-bot no-tank too-far unreachable off-mesh
no-ground no-los invalid-target dead-target friendly no-target not-dead failed not-allowed`.

## Files

- `forever/src/host/run/TbRunOrders.{h,cpp}`: a `Party` context (leader guid, member guids, resolver) replaces `RunRec&`
  in the judge; `Issue(RunRec&...)` becomes a thin wrapper. We own the run code and review this part.
- `forever/src/host/cmd/TbCmdOrders.cpp` (new): `.tb cmd` parse, the caller's binding -> `Party`, reply.
- `forever/src/TbScripts.cpp`: the `cmd` row of the `.tb` table.
- Move the `cmd::NoteRunOrder` hook from the run tick into the lifted `Issue` (CMD-P1-NOTES.md "Hooks": "until P3 lifts
  `Issue` to a `Party` (then move it into `Issue`)"), and log commander orders the same way, so the card's order log and
  the dashboard's COMMANDERS `orders[]` carry both.
- `forever/docs/TB-COMMANDS.md`: a `.tb cmd` section with real output.
- Legacy source for behaviour: `commands/Commander.cpp` 2816-4134 and 6499-6981 (the orders), pure helpers in
  `runtime/RemoteCommanderPolicy.h` (compiles on ForeverCore). Do **not** port `HandleCommanderOrder` itself.

## ForeverCore APIs (pin 6517af05)

As the run slice uses them: `PathGenerator`, `Map::GetLiquidStatus` (via `TbRunLiquid.cpp`), `Group.h:361 SetTargetIcon`.
For the party: `Group.h:311 GetLeaderGUID`, `:323 IsLeader`, `:341 GetMemberSlots`.

## UNPORTED rows

None in `forever/docs/UNPORTED.md` belong to this item. What changes is the cut list of the Commander plan: commander
orders stop being "not ported". `summon` stays refused (run decision D3).

## Decisions already made (Commander plan, lead decisions 2026-11-19)

- D4: commander orders go through `.tb cmd` and **one** order judge (the run's executor lifted to `Party`). Porting
  `HandleCommanderOrder` was rejected.
- D1: h3's table (`forever/src/host/TbH3.h`) stays the only bot -> commander owner; P3 reads the binding, never writes it.
- D2: guids only; the live group is found through the lead (`lead->GetGroup()`), never a stored group number, never
  `GetDbStoreId`.
- D5: cut the addon view (TBM lines), `.bot trace`, ritual, scout, LFG/hire links. Replies are `.tb` grammar v2 only.
- An outside order to a bot of a running run pauses the run (`why=outside-order`, TB-COMMANDS.md section 10): keep that rule
  for `.tb cmd` so the two paths never fight.

## Rules that bite here

- Keep no `Player*`/`Unit*`/`Map*` between calls: order state is (bot guid, order seq) pairs (TbRunOrders.h header comment).
- `FindMap()` before anything on a bot that may be teleporting (`TbRunOrders.cpp:72`).
- The answer is TRUE: `accepted` only when the judge found it carriable AND every queued order passed the pre-flight;
  late failures come back through `TakeLateFailures`.
- Target icons: set with the group's API from the leader's live group; with no group, no icon (less), never a fake one.

## Tests (failing case first)

From the Commander plan, section 7:
- **Failing case:** a commander order that skips the judge, e.g. `.tb cmd move` onto a lava point, is accepted today by a
  naive path; it must be refused with the run's code. Mutation: "commander orders skip the judge" must FAIL the check.
- Every verb gives the same answer from run and commander for the same facts (one table-driven check over the pure part,
  extend `forever/tools/test_run_orders.cpp`, today `PASS (128 checks)`).
- A guid target dead or gone gives `invalid-target`.
- Wiring pin: `TbCmdOrders.cpp` calls the lifted `Issue`, never the host order queue directly; `NoteRunOrder` lives in `Issue`.
- Existing `test_run_c6`, `test_run_orders`, `test_run_cancel`, `cmd_binding_check` and P2's `cmd_binding_life_check` stay green.

## Gates

Census on the slice list (P3 adds no legacy `.cpp`: expect the list unchanged and 0 places), ON and OFF `worldserver`, the
checks above, registry and context checks, the live acceptance above.
