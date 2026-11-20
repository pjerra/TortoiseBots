# Commander port, package P1: the binding card

Spec: `~/TortoiseBots-audit/FOREVER-COMMANDER-PORT-SPEC.md` (P1, section 5; decisions D1 and D2 in "Lead decisions"). Branch `cmd/p1` from `tbf/main` feab6c882.
Command reference: `forever/docs/TB-COMMANDS.md` section 11.

## What it is

`.tb commander status [<commander>] [#token]` shows a COMMANDER CARD per commander: the binding, its bots (class, role, level, hp, power, position, raid
subgroup), the commander (online, map, position), the lead, the party counter, `boundat`, the last orders, and for a card that runs the `run` record of
`.tb run status`. The same cards are available to the dashboard as the Turtle `CommanderTelemetry::BindingInfo` (`cmd::AppendBindings`).

## Files

| File | What |
|---|---|
| `forever/src/host/cmd/TbCmdBindingPure.h` | pure: `BuildCards` (pairs + runs + metas -> cards), `MakeOrder`, `SplitRunOrder`, `RoleOfBrainLines`, `BindingOf` (card -> `CT::BindingInfo`). Standard library + `runtime/CommanderTelemetry.h` only |
| `forever/src/host/cmd/TbCmdBinding.{h,cpp}` | reads h3's table, the run table and the live players; `.tb commander status`; the three hooks; `AppendBindings`. With the option OFF the command answers `engine-not-built` and the hooks do nothing |
| `forever/tools/cmd_binding_check.cpp` | the standalone check (incl. the golden COMMANDERS datagram) |
| `forever/tools/test_cmd_p1_wiring.py` | wiring pins; with `CMD_P1_MUTATE=1` (run on the VM) three mutations of the pure half must FAIL the check |
| `forever/docs/TB-COMMANDS.md` section 11 | the reply records |

## Hooks into other files (one line each)

| File | Line | Why |
|---|---|---|
| `host/TbOrders.cpp` HandleMaster | `cmd::NoteMaster(who.bots, who.leader, commander);` before the end record | the lead named and `boundAt`; a release drops the record |
| `host/TbOrders.cpp` HandleOrder | `cmd::NoteOrder(outcomes, verb, rest);` before the end record; `outcomes` gets one `{bot, refusal code}` per addressed bot (`refuse` pushes the code, a queued bot pushes an empty one) | each card's own order log |
| `host/run/TbRun.cpp` (the run tick) | `cmd::NoteRunOrder(rec.leader, order, answer.accepted, answer.code, answer.text);` right after `Issue`, plus the include | the run's own orders in the log. The spec named `Issue` itself; the one caller is the better place until P3 lifts `Issue` to a `Party` (then move it into `Issue`) |
| `TbScripts.cpp` | the `commander` row of the `.tb` table, plus the include | the command |

## The binding rules as built

* **h3's table is the only owner (D1).** The card code never writes a binding (`SetCommander`, `BindBotMaster` do not occur in it; the wiring test pins it).
  It reads `h3::CommanderBindings()`, which already drops a binding that BotMgr no longer backs (a re-login lapses it by itself).
* **One card per commander.** Keyed by the commander guid counter, never by a group (mutation `key`). A commander with bots in two groups is one card; two
  commanders in one party are two.
* **One card per run (the failing case).** A running (not ended) run is a card: key = the commander of the run's leader when there is one (the run never
  rebinds its own leader), else the leader (`system=yes`). The pairs the run made (members -> tank) are dropped, so a run shows once (mutation `twice`); a
  card that runs keeps its commander's other bots. A run the card code skips would lose `run` and `system` (mutation `skiprun`).
* **A run's bots are its bound ones.** The card holds the leader plus the party members h3's table binds to it (`RunMembers`); a real player or an unbound
  bot in the leader's party is never counted in `bound` or listed as a member. While the leader is off-world (loading, teleporting) the run's own bound
  members supply the same list, so the card does not empty.
* **Every card is collected.** `BuildCards` returns all of them; the Turtle telemetry cap (twenty) is applied only by `AppendBindings` while serializing the
  dashboard payload, never to the list the state is pruned from. `.tb commander status` shows every card.
* **D2: guids only.** Nothing stores a group number. `group` in the reply and the JSON is `group->GetGUID().GetCounter()` (the value `GroupIdW2` returns; the
  helper is inlined here because `TbCompat_W2.h` is not reachable from host files without the wide-pass wrapper) read from the LEAD's live group at each
  read; the lead is found by guid. `GetDbStoreId` does not occur (pinned). No database call.
* **What is kept beside the table:** per commander `{lead guid, boundAt}`, the order log (T `OrderLog`, 8 per commander, 20 commanders; an exact repeat of
  the last order is not noted), a role cache (10 s). A release or a rebind that leaves a commander no bot prunes all three at once (`NoteMaster` ->
  `PruneToStanding`), not only at the next read; the prune reads the whole card collection.
* **The lead:** the one `.tb master party <leader> <commander>` named and still bound, else the leader of the first bound bot's party, else the first bot.
  A named lead is never overwritten by the party fallback, even when it is the lowest bound guid.
* **Orders logged:** `.tb order` once per card the addressed bots stand on (a bot of a run counts for the run's card: the tank's own commander, else the
  tank; a system run's tank addressed alone is keyed by itself). Each card gets its own queued/refused counts and its own first refusal (`TallyByCard`);
  `ok=yes` only when that card queued at least one bot. The run's orders are logged under the same key with the executor's answer.

## What the dashboard collector needs (not done here: `forever/src/dash/*` is another package)

`dash/runs` builds one binding per run from the run table and nothing for a plain commander. To show every binding with orders (spec section 5, P1):
1. in `Cycle()` after the run loop: `cmd::AppendBindings(bindings, shownRunLeaders);` where `shownRunLeaders` are the leader guid counters of the runs the
   loop pushed (the lingering ended ones included), so a card whose lead is one of them is not shown twice;
2. for the run bindings it builds itself: copy `orders` from the card of the same commander (`BindingInfo::orders`), because the run feed leaves them empty;
   the commander is `h3::CommanderGuid(leader)` there already, the same key as here.
No other change: the format is the Turtle one (golden datagram in the check).

## Stand-ins and limits (they make the card show LESS)

* mobs, objects, bosses, bot extras (cast, cc, debuffs, ready check) are not reported (`mobsReported=false`...): the card greys those layers out until P4.
* `view` is false (the TBM addon view is cut, D5); raid layout is empty (P7).
* An offline commander is named `offline #<guid>` (or its real name from the character cache); an offline bot is not listed in `bots` (`bound` counts it).

## Review round (FIX-cmdp1): fix or reject, one line each

| Finding | Verdict | Reason |
|---|---|---|
| Codex: the card cap truncates the collection the state is pruned from | fix | `BuildCards` returns every card; only `AppendBindings` applies `kMaxCommandersReported`; the check builds 30 cards. |
| Codex: a run copies every party member into its card | fix | `RunMembers` keeps the leader plus the members h3 binds to it, so humans and unbound bots are not counted; mixed-party check added. |
| Codex: one order's aggregate counts are written to every card | fix | `NoteOrder` groups per-bot outcomes by card (`TallyByCard`); each card gets its own counts and first refusal; check and wiring pin added. |
| Gemini 1: a named lead equal to the lowest bound guid is overwritten | fix | `namedLead` gates the party fallback; check added. |
| Gemini 2: an order to a system run's tank alone is not logged | fix | `CardKeyOfBot` falls back to the running run's leader; wiring pin added. |
| Gemini 3: release/rebind leaves log and repeat suppression until the next read | fix | `PruneToStanding` runs in `NoteMaster` on both paths; wiring pin added. |
| Gemini 4: an off-world run leader empties the card | fix | the run's `bound` members are candidates too, and `RunMembers` filters them like the live party; check added. |

## Gates (slot 7 on the build VM, core pin 6517af05)

| Gate | Result |
|---|---|
| census, list `forever/SLICE5.txt` (P1 adds no legacy file: its code is under `forever/src`) | `files 176  compile clean 176  with errors 0  error places 0` |
| option-ON `worldserver` (`FOREVER_CMAKE_EXTRA=-DTORTOISEBOTS_SLICE1=1`) | `CMAKE_RC=0 MAKE_RC=0`; `nm`: `cmd::HandleCommander`, `NoteMaster`, `NoteOrder`, `NoteRunOrder`, `AppendBindings` defined in `libtortoisebots.a` |
| option-OFF `worldserver` | `CMAKE_RC=0 MAKE_RC=0` |
| `forever/tools/cmd_binding_check.cpp` (VM, g++) | `cmd_binding_check: 47 checks, 0 failed` |
| `test_cmd_p1_wiring.py` | 11 tests OK; with `CMD_P1_MUTATE=1` (VM): the unmutated check passes and the three mutations (`key`, `skiprun`, `twice`) each FAIL it |
| `test_run_review_wiring.py` (names TbRun.cpp, TbOrders.cpp), `test_slice4_d4_wiring.py` | OK |
| `slice_registry_check.py`, `slice_gen_context.py --check` | `summary: (a)=0`; `SliceAiObjectContext.cpp is up to date` |
| `tools/verify_okf.py` | `OKF verification PASSED (60 nodes validated, graph edges consistent).` |
| `test_run_c6`, `test_run_orders`, `test_run_cancel` (VM) | PASS 28 checks, PASS 128 checks, PASS |
| Turtle `vm-build` / `vm-test tb` | not run: no file outside `forever/` changed |

## Live (run2, ForeverCore test server 2, `bin/worldserver.pre-cmdp1` is the restored original)

Bots Mtank Mpriest Mdpsa Mdpsb Mdpsc (+ commander Mcmd, GM, outside the party), RFC instance, engines on, `.tb master party Mtank Mcmd`:
* `.tb commander status` before any binding: `commander-end count=0 bindings=0`.
* after the bind: ONE `binding` (lead Mtank, `group=1`, `bound=5 bots=5`, `system=no`), five `member` records with `role=tank|healer|dps`, class, level, hp, power, position.
* `.tb order party Mtank stay`: `order ... ok=yes result=queued 5 of 5` in the card; with no engine the same order is logged `ok=no result=no-engine`.
* `.tb run start Mtank ragefire_chasm` with the binding standing: STILL ONE card (commander Mcmd), `orders=5` (the stay and the run's own `move`, `pullback`, `focus`, `attack`), then the `run` record (`state=running ... cmd=Mcmd`). After `.tb run stop` the run ended and the list was empty.
* `.tb master party Mtank none`: `commander-end count=0 bindings=0` (an empty list, once). `.tb commander status Nobody`: `error code=no-binding`.
* Seen live, fixed in the last commit: a run whose tank lost its commander mid-run showed as a `system=yes` card with `inparty=yes`; a system card is never in-party or player-led now (not re-run live: two flags).
* The server crashed once (`World Thread hangs for 61102 ms`) while the build VM's load average was 270 and six bots were logging in: the freeze watchdog, not the card code (no command of this package had run). The second boot was clean.

## What P2 needs (not started)

P2 = release reasons and follower restore, the threat-back-off and autonomy ledgers, lead death/leave/logout through hooks, the opt-in persistence row, `IsCommandedBy` / `IsInOwnBindingGroup`.
* **A stored binding.** The in-memory `Meta` (commander -> lead guid, boundAt) in TbCmdBinding.cpp grows the binding flags (`inParty`, `raid`, `playerLed`, `system`), the followers bound at bind time and each follower's saved follow target (the engine's follow-target value; `NoteMaster` is the one place a bind is seen, and it already ignores a bot the host refused). Guids only (D2); the live group is always found through the lead.
* **The release rule.** `runtime/RemoteCommanderPolicy.h` `CheckBinding` / `BindingState` / `DecideRelease*` compile on ForeverCore (no core include). The facts come from the lead's live group (`GetLeaderGUID`, `GetMemberSlots`, `isRaidGroup`); the card's session counter (`group=`) must be refreshed when the lead's group changes while the lead and one follower stay together (spec section 4), not compared with a stored number.
* **Hooks instead of polling.** `GroupScript` (OnAddMember/OnRemoveMember/OnChangeLeader/OnDisband) and `PlayerScript` OnLogin/OnLogout rows in TbScripts.cpp. Unknown U6 of the spec is open: whether `GroupScript` fires for the groups `BotMgr` makes (TbBotMgr.cpp:307-311); a scratch build with a log line answers it. Until then the 1 Hz sweep of T is the fallback.
* **Persistence (D3).** `forever/sql/characters/<n>_tb_commander_binding.sql` (commander_guid, lead_guid, members, flags, bound_at; the module's first characters-DB table), written and read with prepared statements or `{}` formats (findings item 18: a printf `PQuery` compiles and drops its arguments), behind `TortoiseBots.Commander.KeepBindings` (default 0). Restore when commander and lead are back in the world: `.tb party` forms the group again, `.tb master` binds each bot. A bot-only group does not survive a restart (spec section 4, unknown U1).
* **Host stand-ins.** `TbHostOwnership.cpp` answers `IsInCommandedGroup` from h3's table already; P2 swaps in `IsCommandedBy` and `IsInOwnBindingGroup` from the stored binding.
* **Not P1's job but waiting on the merge:** the dashboard collector's two lines (above), and P3 moving the `NoteRunOrder` hook from the run tick into `Issue` when `Issue` takes a `Party`.
