# Slice S6, package O6 (orders): the executor of the run leader's order strings

Branch `run/o6` (worktree `~/wt-run-o6`), build slot 3 (`FOREVER_SLOT=3`), on `run/f6` (slices 1-4 + S5's halves + F6). Core read only at the pin 6517af05 (zero core changes). No server was started and no SQL was applied. Spec: `~/TortoiseBots-audit/FOREVER-RUN-S5S6-SPEC.md` 5.2 (orders and F executor), 5.3, decisions D1 and D3; facts' needs: `RUN-S6-F-NOTES.md` section 8 ("O6").

## 1. Files

| File | What |
|---|---|
| `forever/src/host/run/TbRunOrders.h` | the interface C6 calls (committed first): `Issue`, `TakeLateFailures`, `ForgetRunOrders`, `StartCheck`, `OrderAnswer`, `LateFailure` |
| `forever/src/host/run/TbRunOrdersPure.h` | the PURE half: the order parser, the answer texts in the Turtle shape, the move / direct / target verdicts (the Turtle judge's own `RemoteCommander::CheckMove` etc. fed with the core's facts), the tracker of accepted orders. No core or legacy type |
| `forever/src/host/run/TbRunOrders.cpp` | the core half (TORTOISEBOTS_SLICE1 only; an empty object in the OFF build): party read, floor snap + path, target resolution through `ResolveHandle`, pre-flight, queue, tracker |
| `forever/src/host/TbOrdersApi.h` (new) | `h3::OrderSpec`, `h3::PreflightOrder`, `h3::QueueOrder`: the host's order queue as a call (spec 5.2 "one exported function around Enqueue") |
| `forever/src/host/TbOrders.cpp` (append only) | the four new verbs in `Run()` (`move-direct`, `pullback`, `focus`, `rest`), `PreflightOrder`/`QueueOrder`, two helpers; the `pull` verb gains one line (below) |
| `forever/tools/test_run_orders.cpp` | the standalone test of the pure half next to the policy header |

No edit in `ai/`, `runtime/`, `forever/src/compat`, `forever/src/slice`, `TbScripts.cpp`, `CMakeLists.txt`, `TbH3.h` or `TB-COMMANDS.md` (the CMake glob picks the new files up; the new verbs are reachable through `QueueOrder` only, so `.tb order`'s grammar and its reference page are unchanged).

## 2. The interface as built

```cpp
OrderAnswer Issue(RunRec& rec, std::string const& order, uint32 nowMs, bool gather = false);
void TakeLateFailures(RunRec& rec, uint32 nowMs, std::vector<LateFailure>& out);
void ForgetRunOrders(ObjectGuid leader);
bool StartCheck(ObjectGuid leader, std::string& code, std::string& message);
```
`OrderAnswer { accepted, code, text, bots, pending, movePathRead, movePathType }`. `text` is the string the policy's `OrderRefused` and its parsers read, in the Turtle shape (`CommandShared.cpp:75-96`): a refusal is `<code>: <reason>` (so `UnreachableAnswer`, `ReactTooFarAnswer`, `LeadNoProgressRefusal`, `DirectStragglerName`, `IsPathRefusal` and the `invalid-target` prefix of the pull rules match: the standalone test runs each against the real policy header), an ACK is `party <n>` or `bot:<name> 1`. `movePathRead/Type` is T's `RunOrderCapture` for `DungeonRun::NoteMovePathType`. `gather` is `DungeonRun::IsGatherOrder(run, order)`.

**One path.** `Issue` parses (`ParseOrder`), reads the party, judges, pre-flights EVERY bot the order needs (engine present, the legacy actions registered, queue room, the unit still valid and in pull reach), and only then queues. A refusal changes no bot. `TakeLateFailures` (once per tick, before `Issue`) hands back an accepted order whose queued work failed when the bot's engine ran it (or never ran within 20 s): feed `(order, text)` to `DungeonRun::OrderRefused`. This is spec U2's fallback made real: the judge refuses everything the host path could refuse before queuing, and the rest arrives one tick later as a refusal, never as a silent success.

## 3. Order by order (spec 5.2 table, as built)

| P order | What the module does | Refused with |
|---|---|---|
| `move x y z` | the Turtle floor snap (`ChooseFloorZ`) and ONE path query from the lead (`PathFinder`), then `RemoteCommander::CheckMove` (too far for ReactDistance, no path, no progress from the lead's own spot, ends off the point); ok: lead `come x y z` (writes the stay position), every living follower `follow` (they must be bound to the lead: `not-bound` otherwise) | `too-far: ... (AiPlayerbot.ReactDistance)`, `unreachable: ...` (with the no-progress mark when the path ends where the lead stands), `off-mesh: ...`, `lead-unavailable`, `not-bound`, `no-engine`, `queue-full` |
| `move x y z direct` | the lead's checks (`CheckDirectStep`, floor under the point, line of sight) and each follower's reach, then lead `move-direct` (a straight `MovePoint(generatePath=false)`, stay position written at the floor under the point, the AI asleep for the walk, at most 4 s), followers `follow` | `too-far`, `no-ground`, `no-los`, and `too-far: <bot> is not with <lead> ...` (names the follower, as T does) |
| `walk <bot> x y z` | that bot `come x y z` after the same move judge from ITS position | the move refusals, `no-bot`, `unavailable` |
| `pull <h> [body]` | the tank's `pull` (the existing verb: `pull my target` + the party hold) | `invalid-target`, `dead-target`, `friendly`, `no-target`, `too-far` (3 x ReactDistance), `no-tank`, `no-party` |
| `pullback <h>` | new verb `pullback`: stay position written at the tank's own spot, then the steps of the chat action `pullback my target` (which the slice's action context does not hold) with the same public calls ("pull back" strategy on, `PullStrategy::BeginCommand`, `pull my target`, rolled back when refused), then the same lead-keyed hold as `pull` | as `pull` |
| `attack <h> [bot]` | the target judged as T (`CheckOrderTarget`), then `attack` (the mark route of slice 3) for the named bot, or for every living bot but healers and a tank holding another mob | the target codes, `no-bot`, `no-party`, `failed` |
| `focus <h>` | new verb `focus`: the skull on the unit in the group (it STAYS for the fight), each bot off its stay, the `rti` value set to skull (the legacy `rti` action only sets it and is not in the slice's context), `attack rti target` | the target codes, `failed` |
| `rest` | new verb `rest`: drink when mana is short, eat when health is short, "rest to full", the bot sits out 30 s; nothing short = ok | `failed` (no bot took it), `action-not-registered` |
| `revive party` | every dead bot `release` (slice 4; inside the instance the core revives it at the entrance, D2), every living member `follow`; a ghost already released is left alone | `not-dead`, `no-entrance`, `failed` |
| `summon <bot>` | refused (D3): no bot is teleported to the lead | `unsupported` |
| `escape rescue fetch return rejoin cc loot lootmethod open use ritual gossip state enter`, `pull ... ranged`, a join delay | refused | `unsupported` |
| `stay`, `follow` | not emitted by the policy: party `stay` / `follow` for C6's stop and the glue | `not-bound`, `failed` |

Binding facts for the F6 needs (section 8 of its notes): (1) every guid is a handle: `Issue` resolves it with `run::ResolveHandle(leader, handle, guid)` BEFORE queuing, and a handle that no longer resolves is `invalid-target`, never a guess; (2) `move`, `move-direct` and `pullback` write the "stay" position the way `come` does (`TbOrders.cpp` `come`: the `position` value entry "stay", plus `StayWalkOrdered`), so the facts' `leadStayKnown` is true; (3) `StartCheck` refuses a leader without the tank role (`no-tank`, "set it with `.tb role <name> tank` and let one engine tick pass": `PlayerbotAI::IsTank` is what the facts read), and `Issue` refuses pulls `no-tank` for the same reason.

## 4. Decisions made here (the spec was silent)

* **Pull anchor = the tank's own spot.** `ai::PausePartyPullHold` anchors the hold on the tank's MASTER (PullActions.cpp:109-111); with D1 the tank's master is the outside commander (a GM that may stand at the entrance), so the party would be held at the GM. For a run order (`OrderSpec::fromRun`) the order's owner is the tank itself and, after the hold ran, `AnchorPullHoldOnTank` rewrites the two entries the hold wrote (`stay`, `pull hold`) for the same members with the tank's own spot. One added line in the existing `pull` branch, off unless `fromRun` (so `.tb order pull` is unchanged).
* **A party-wide order needs one bot, a named order needs that bot.** Followers whose pre-flight fails are dropped (the ACK count says so); the lead of a move or a pull, a named bot, and the last bot of a party-wide order refuse the order. The late result is judged the same way (`Settle`: failed only when no decisive bot succeeded and one failed or never ran in 20 s).
* **Followers `follow`, they are bound to the lead.** D1 binds the tank as every member's commander; a follower bound to someone else would follow someone else, so the order is refused `not-bound`.
* **Direct steps are for the lead only** (spec 5.2): followers are not walked along the lead's line (the Turtle spline walk is not ported), so a direct step is refused unless every living follower has a complete navmesh path to the floor under the point. The RFC export has no `move_direct` row (RUN-S5-FACTS-RFC.md); this only matters for other dungeons.

## 5. Not done / differences from Turtle (said, not hidden)

* The floor snap reads no liquid and the path has no steep-slope exclusion or permissive retry (`ExcludeSteepSlopes` is unported): a water point is lifted like any point without a floor and judged by its path; the verdict is never stricter than the mesh.
* A ranged pull whose firing position cannot be reached is not detected synchronously (T judges it after two AI steps: `unreachable: The tank could not start moving ...`). Here the pull is accepted, the tank stands, and the 15 s window / the policy's pull ceiling ends it. A pause, not a wrong action.
* `pull <h> ranged`, a join delay, `revive <bot>`, `cc ...`, `loot ...` and the task verbs are refused `unsupported` (no silent drop).
* `focus` does not carry T's totem and carrier special cases (`FocusSkipsBot`: the plan of Ragefire Chasm has no totem, no carrier and no `tank_hold` row).
* The outcome of `release` for a bot that is dead but not yet a ghost can still fail inside the engine (the action's own `isPossible`): it arrives as a late failure.
* `TakeLateFailures` reads `h3::LastOrder`, which keeps ONE result per bot: a later order of the same bot supersedes an earlier one's result, which then says nothing (never a false failure).

## 6. What the merge must do

* C6: call `run::TakeLateFailures(rec, now, out)` once per run tick (feed each `(order, text)` to `DungeonRun::OrderRefused`), then `run::Issue(rec, order, now, DungeonRun::IsGatherOrder(run, order))` for the policy's order, with `NoteMovePathType` from `movePathRead/Type`; `run::StartCheck(leader, code, message)` in `.tb run start` before binding (error codes `not-bot no-engine not-leader no-tank` map onto the spec's `not-bot no-engine` plus the tank refusal); `run::ForgetRunOrders(leader)` next to `run::ForgetRun(leader)` when a run is removed.
* `TbOrders.cpp` and the new `TbOrdersApi.h` are the only files of this package outside `host/run/`; M5/M6 should expect `TbOrders.cpp` to differ from the S5 line by the append only.
* Add `forever/tools/test_run_orders.cpp` to the VM policy-test list next to `test_run_facts`.

## 7. Gates

All in slot 3 of the build VM, on the final tree (the last code commit is `O6: pullback and focus done with the calls the slice has`; later commits are notes).

| Gate | Result |
|---|---|
| option-ON build (`FOREVER_SLOT=3 FOREVER_CMAKE_EXTRA=-DTORTOISEBOTS_SLICE1=1 build.sh 12 worldserver`) | `CMAKE_RC=0 MAKE_RC=0`; `TbOrders.cpp.o` and `TbRunOrders.cpp.o` rebuilt, `libtortoisebots.a` holds `run::Issue`, `TakeLateFailures`, `StartCheck`, `ForgetRunOrders`, `h3::QueueOrder`, `h3::PreflightOrder` (the linker does not pull the `run::` ones into `worldserver` until C6 calls them) |
| option-OFF module build (`-DTORTOISEBOTS_SLICE1=OFF`, target `tortoisebots`) | `CMAKE_RC=0 MAKE_RC=0`; `TbRunOrders.cpp` is an empty object, `TbOrders.cpp` answers `engine-not-built` as before |
| census SLICE5 / SLICE4 / SLICE3 / SLICE2 / SLICE1 | `files 172 / 174 / 170 / 160 / 133, compile clean = files, error places 0`, includes of a missing header 0 (O6 adds no legacy file) |
| `slice_gen_context.py --check`, `slice_registry_check.py` | `SliceAiObjectContext.cpp is up to date`; `(a)=0  (b)=215` (unchanged from F6) |
| `test_run_orders` (g++ -std=c++17 -Wall -Wextra, VM: `cd` to the worktree on the VM, `g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/test_run_orders.cpp -o /tmp/t && /tmp/t`) | `RESULT: PASS (128 checks)`: every order string of the policy parses to its verb; every refusal text is accepted by the policy's own parsers (`ReactTooFarAnswer`, `UnreachableAnswer`, `LeadNoProgressRefusal`, `IsPathRefusal`, `DirectStragglerName`, the `invalid-target` prefix); the tracker settles ok / failed / expired / superseded / party-wide cases |
| `test_run_facts` (F6's, unchanged) | `RESULT: PASS` |
| `test_dungeon_run_policy`, `test_system_run_policy` (unchanged, VM) | `ok`, `109 checks passed` |
| Turtle `vm-build` / `vm-test tb` | not needed: nothing outside `forever/` changed |
| server / SQL | none started, none applied |

What these gates do NOT prove: the core calls themselves (that is the build, and a live run). First things to watch live (M6): the answer of the first `move` (path type, snap), `pull`/`pullback` on the first pack (the hold anchored on the tank, the tank returning to its spot), `rest` on the priest, `revive party` after a death (`release` result at the entrance), and `TakeLateFailures` (how many late failures a run produces: spec U2).
