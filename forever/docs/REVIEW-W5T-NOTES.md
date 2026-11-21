# REVIEW-W5T: wire the W5 flight links and transports into W4 travel

Branch: `feat/w5-travel`. Scope: `TravelLinkUsableCompat` (and the W4 equivalent) lets flight and transport links
count as routes; a flight link carries only a bot that knows both ends and can pay the fare; the travel executor
boards flight legs through `tbhost::taxi::Fly` and transport legs through `tbhost::ride::StartRide` (never
teleport or instant flight). No redesign of W4, no changes to `TbHostTransport.cpp`, no Turtle behaviour change.

## 1. Flight and transport links were loaded but never routes

Wrong: on ForeverCore `LinkUsableOnForever` refused `kTransport` and `kFlightPath`, so `getCost` returned -1 for
every taxi/boat/zeppelin/tram leg, `getNodeMap`/`hasRouteTo` ignored them and `LoadMapTransfers` added no transfer
for them. A cross-continent goal behind a zeppelin or a taxi was unreachable by construction.

Fix: `ai/playerbot/policy/travel/TravelGraphPolicy.h` — the gate is now `type != kTeleportSpell && type != kStaticPortal`
(D4: the host's logged rescue stays the only travel teleport). `TravelNode::getNodeMap` and
`TravelMgr::LoadMapTransfers` ask the same rule, so both follow with no further edits.

Test: `forever/tools/test_w5t_travel_links.cpp` (new, standalone g++).
FAIL before: `Assertion 'ai::travel_graph::LinkUsableOnForever(ai::travel_graph::kFlightPath)' failed.`
PASS after: `PASS: test_w5t_travel_links (7 checks, 0 failed)`.
Contract update: `forever/tools/test_travel_graph_policy.cpp` pinned the old rule (Kalimdor reaches only Kalimdor,
no cross-continent transfer); it now pins the new one (both continents reach each other over the zeppelin and the
taxi, transfers `{1,0}:2` and `{0,34}:1`). Reverting the gate fails the updated test.
Pins: `forever/tools/test_wl4_wiring.py` (`LinkTypes`, new `test_the_travel_executor_boards_through_the_w5_host_seams`).

Live check (needs round-B graph rows; the deployed graph is still empty, so this is dormant until they land): seed
two travel nodes with one `flightPath` link between them, give the bot both taxi nodes and the fare, pick a
destination past the link. Expected: `getCost` takes the link, the bot walks to the flight master, `[W5] fly-start`
with the fare, `[W5] fly-landed` at the destination, no teleport.

## 2. A flight link did not check the from node

Wrong: `getCost`'s flight branch checked the destination mask and the fare but never whether the bot knows the node
it boards at. A bot could be costed onto a link whose first step it cannot take.

Fix: new pure `ai::DecideFlightLinkUsable` in `ai/playerbot/policy/travel/TaxiChoicePolicy.h` (known both ends,
faction of each end, classic maps of each end, fare vs copper, in that order) and a per-link compat
`TravelFlightLinkUsableCompat(bot, taxiPath, cGold)` — ForeverCore gathers the node rows and asks the pure decision,
Turtle passes and its legacy branch runs unchanged. `getCost` asks it first inside the flight branch.

Test: same new file, cases unknown-from/unknown-to (`unknown-node`), wrong faction (`faction`), off-map (`no-route`),
fare above copper (`money`), exact fare flies.
FAIL before: the decision did not exist (the gate assertions above are the failing-first run).
PASS after: same `PASS: test_w5t_travel_links (7 checks, 0 failed)` line.

Live check: as in 1, plus a bot that knows only the destination node: the link costs -1, the route walks or the
target is refused (`cannot: travel`), never a boarding at an unknown master.

## 3. The travel executor teleported instead of flying or riding

Wrong: graph flight legs ran `MovementAction::UseTaxi` (gated by unported `Taxi`: returns false on ForeverCore, and
the Turtle body ends in `BotInstantFlightFinish`), graph transport legs ran `UseTransport` (gated by unported
`TransportBoarding`: returns false) with a `TeleportToCompat` fallback to the far dock under
`TransportTeleportType = 2`. Nothing boarded; the only ways across were instant flight or teleport.

Fix (`ai/playerbot/strategy/actions/MovementActions.cpp`, behaviour on Turtle byte-for-byte unchanged):
- `UseTaxi` asks `TravelTaxiFlyCompat` first: ForeverCore learns the master's node when new, then
  `tbhost::taxi::Fly` (the core's `HandleActivateTaxiOpcode` path, real flight, `TickLandingWatch` logs the landing).
  A refusal returns false, so the move fails and the travel layer drops the target (`cannot: travel`).
- `UseTransport` asks `TravelRideBoardCompat` first: ForeverCore arms `tbhost::ride::StartRide` and returns false, so
  the bot stays dispatched at the dock while the engine tick boards, rides and leaves (walk on, board where it
  stands). A refused `StartRide` is the host's own `[W5] ride-refused` line; the bot waits, never reports success.
- The `TeleportToCompat` fallback for a bot not yet aboard is gated by `TravelTransportTeleportCompat` (false on
  ForeverCore, true on Turtle), so waiting at the dock can never become a teleport.

Test: `test_the_travel_executor_boards_through_the_w5_host_seams` pins the three call sites and asserts
`BotInstantFlightFinish` never appears in the ForeverCore pair.

Live check: with a seeded cross-sea transport link, a bot at the dock with the transport entry set. Expected:
`[W5] ride-wait`, the bot stands at the dock (no teleport), `[W5] ride-board` when the transport stops in reach,
`[W5] ride-leave` at the destination dock. A refused ride (unknown entry): `[W5] ride-refused`, the bot keeps
waiting at the dock.

## What this round deliberately does not do

- Link enumeration is untouched: `generateTaxiPaths` already builds flight links from the core taxi stores on both
  cores (the same data `BuildRuntimeFlightLinks` filters for the `.tb fly` verb), and `generateTransportNodes` already
  builds the boat/zeppelin/tram links. Faction filtering at build time would be wrong for one graph shared by both
  factions; the per-bot check in `getCost` (mount ids, masks, fare) is where Turtle always did it.
- `TravelTaxiFlyCompat` learns an unknown master node before flying (the legacy executor learned it too); the fare
  itself is the path's own price, without the reputation discount `Quote` applies — conservative (may walk where a
  quoted fare would fly), never the reverse.
- `TravelRideBoardCompat` re-arms `StartRide` on every waiting tick, which resets the ride's waited timer, so
  `TickRide`'s give-up cannot fire while the travel executor keeps asking. Mid-ride nothing re-arms (`IsOnBoard`
  guard), so boarding/leaving work. Making the arm sticky (or querying it) belongs to the worker fixing the
  transport boarding checks (Z/deck, pathing, give-up): `TbHostTransport.cpp` is untouched here on purpose.
- A stale route can re-arm a finished ride (the executor consumes a pre-boarding path while the bot stands at the
  destination dock). The service arrival/target-drop layer is the backstop, as on Turtle; flagging for the same worker.
- `TryBoardFlightToTarget`/`DecideFlightPlanForTarget` (the non-graph flight plan) is not in the ForeverCore slice
  and is untouched; it keeps its Turtle behaviour.
- Round B/C pieces (generated nodes/links/paths, coverage) are unchanged: with an empty graph every gate above is
  dormant — the loader still logs one line and travel does nothing. The `cannot: travel` path for a refused link is a
  -1 cost (flight) or a dock wait with a host refusal line (transport).
