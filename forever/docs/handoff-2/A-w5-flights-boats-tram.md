# A. W5: flight paths, boats, zeppelins, tram

**Waits for our W4** rounds A1, B (the graph ships as SQL here) and C, merged and B's SQL loaded (README). Host seams,
policies and ordered commands can be written before that.

## Done looks like

A bot learns every flight master it talks to, flies known routes and pays the fare, and boards and leaves ships,
zeppelins and the tram, all through the core's own taxi and transport code, and W4's travel uses these legs when they
are the cheaper way.

**Live acceptance** (world-life plan, W5 row): a Horde bot goes Orgrimmar to Undercity by zeppelin; an Alliance bot
goes Menethil to Auberdine by boat and Stormwind to Ironforge by tram; each also takes one flight; each arrives within
one transport cycle. Log money (fare), taxi mask count, map id; `tele=` unchanged.
**From a client**: the forever-agent client's taxi round (R6: node status, show nodes, activate, flight and landing
watch) flies the same route next to the bot, and its world summary counts *other* units with `UNIT_FLAG_ON_TAXI`
(`UnitDefines.h:187`), which shows the bot flew rather than teleported. Boats and zeppelins have no client round yet.

## Scope and legacy files

Plan: 5 worker-days, biggest risk "boarding a moving `Transport` headless; waits that never end (Deeprun Tram)". Area: 11 files, 1,302 lines, 42
census places, plus the transport/flight-path parts of `TravelNode.cpp` (14 references to
`TravelNodePathType::transport`/`flightPath`). Legacy: `strategy/actions/TaxiAction.cpp` 112,
`RememberTaxiAction.cpp` 50, `TaxiRouteGuard.h` 42, `TaxiPathPolicy.h` 24, `TransportSnapPolicy.h` 40, the transport
sites in `MovementActions.cpp` (4) and `PlayerbotAI.cpp` (taxi 2, transport 2). Leave `DebugTransportCommands.cpp`
(841) unregistered.

## ForeverCore APIs (25fc7ca)

Taxi (client flow in `forever/data/waterys/core-howto/flight-paths.md`):
- Learn on visit: typed `WorldPackets::Taxi::TaxiQueryAvailableNodes` into
  `WorldSession::HandleTaxiQueryAvailableNodesOpcode` (`TaxiHandler.cpp:72`): it checks the NPC, then
  `SendLearnNewTaxiNode` (`:129`) sets the mask bit (`PlayerTaxi::SetTaximaskNode`, `PlayerTaxi.h:59`).
- Known: `m_taxi.IsTaximaskNodeKnown(node)` (`PlayerTaxi.h:51`). Node of a flight master:
  `sObjectMgr->GetNearestTaxiNode` (`ObjectMgr.h:1147`); hop cost `GetTaxiPath` (`:1148`).
- Route: `TaxiPathGraph::GetCompleteNodeRoute(from, to, player, nodes)` (`Entities/Taxi/TaxiPathGraph.h:30`),
  `GetReachableNodesMask` (`:31`). Stores `sTaxiNodesStore`/`sTaxiPathStore` (`DB2Stores.h:279-280`), faction masks
  `sHordeTaxiNodesMask`/`sAllianceTaxiNodesMask` (`:380-381`).
- Fly: typed `WorldPackets::Taxi::ActivateTaxi` into `HandleActivateTaxiOpcode` (`TaxiHandler.cpp:159`; both ends must
  be known, `:180`) or `Player::ActivateTaxiPathTo(nodes, npc, ...)` (`Player.h:1318`, `Player.cpp:24504`); it refuses
  on short money (`ERR_TAXINOTENOUGHMONEY`, `Player.cpp:24664`), in combat, rooted. In flight: `IsInFlight()`
  (`Unit.h:1044`); landing runs `CleanupAfterTaxiFlight` (`Player.cpp:24714`). `CONFIG_INSTANT_TAXI` turns flights
  into teleports: it must be off on the test realm.

Transports:
- `Transport` (`Entities/Transport/Transport.h`): `AddPassenger(obj, offset)` (`:46`), `RemovePassenger` (`:47`),
  `GetTimer()` (`:85`), `IsStopped()` (`:87`), `GetExpectedMapId()` (`:104`). Find by guid: `Map::GetTransport(guid)`
  (`Map.h:447`); a passenger's ride: `WorldObject::GetTransport()` (`Object.h:537`).
- Schedules: `TransportMgr::GetTransportTemplate(entry)` (`TransportMgr.h:162`): `TotalPathTime`, `PathLegs` (map,
  segments with `Delay` = dock stops), `GetNextPauseWaypointTimestamp(time)`. The tram is a type-11
  `GAMEOBJECT_TYPE_TRANSPORT` (`SharedDefines.h:3177`) with `GetTransportAnimInfo` (`TransportMgr.h:164`), not a
  `transports` row.
- A client boards via `MovementHandler.cpp:529-554` (move carrying a transport guid); a headless bot sends no moves,
  so the host makes the same calls.
- Cross-map rides: the core far-teleports passengers (`Transport::TeleportTransport`, `Transport.h:107`); the bot
  session acks it (`forever/src/TbBotSession.cpp:271-279`).

## Files to touch (suggested)

- Host seam `forever/src/host/travel/TbHostTaxi.{h,cpp}` (`tbhost::taxi`: Learn, KnownNodes, Quote, Fly, InFlight) and
  `TbHostTransport.{h,cpp}` (`tbhost::ride`: NextDock, Board, Leave, OnBoard).
- Pure `policy/travel/TaxiChoicePolicy.h` (fly or walk), `TransportWaitPolicy.h` (when to give up) + tests.
- Compat pair `forever/src/compat/TbCompat_WL5.h` + `ai/cmangos-compat-wl5.h`: W4's `TravelLinkUsableCompat` gets a
  real F body for `flightPath` and `transport` links (W4 owns the declaration; you own the F body: agree that one line).
- Generation: taxi and transport nodes are added by an own `.tb travelgen` sub-verb and shipped as an additional file
  (`forever/sql/world/<date>_tb_travelnodes_transport.sql`), so W4's walking graph is not regenerated.
- `.tb order` verbs `fly <node>`, `board <transport>`; `.tb state` `taxi= taxinodes= ride=`; `W5_KEYS`.

## UNPORTED rows removed

`taxi`, `incoming: taxi`, `transports`. Anything not done (the tram if its data is missing, custom ships) stays a
documented "refused leg", not a stand-in that pretends.

## Rules that bite here

- Guids/ids only; re-find each tick with `FindMap()`; mid-teleport = idle tick.
- Check money against the quoted fare first: the team's bots failed flights silently without gold. Short money = walk.
- Never fly to or from an unknown node, never use `CONFIG_INSTANT_TAXI` or `BotInstantFlight`, no teleport to a dock.
- A ride that never comes: give up after one `TotalPathTime` plus a margin, mark that leg failed for a while, re-route.

## Tests (failing case first)

- **Failing case:** `TaxiChoicePolicy` picks a flight whose fare is above the bot's money; it must answer "walk" and log
  `[W5] fly-refused code=money`.
- `TransportWaitPolicy`: waiting one cycle minus 1 s keeps waiting; one cycle plus margin gives up; a stopped transport
  at the dock within reach boards; a moving one does not.
- Wiring: no `TeleportTo` in W5 files; `TravelLinkUsableCompat` is true for a taxi link only when both nodes are known;
  `--mutate` removes the money check and must FAIL.

## Decisions already made

Blizzlike: learn by visiting, pay every fare, no instant flight, no teleport but the rescues; Zephras airships out of
travel routes until the owner says otherwise.

## Open for you to settle

1. **Where the flight graph comes from.** Recommendation: not from SQL. Build flight links at load from the DB2 stores
   between W4's flight-master `npc` nodes; cross-check `service-points/flight-masters.csv`. Only docks and transport
   links go into the shipped SQL.
2. **The tram.** Our sweep found no `transports` row and no type-15 spawn, but the tram cars are type 11 in map 369, so
   the sweep looked in the wrong place. First step: count type-11 gameobject spawns on map 369 (one guarded SELECT at
   generation). If they are absent, the tram leg stays refused and the acceptance swaps "tram" for the
   Stormwind-Ironforge flight.
3. **The other custom ships** (Auberdine-Stormwind Harbor, Tanaris-Riverglades). Recommendation: out by default, like
   Zephras, behind a config list of allowed transport entries.
4. **Boarding.** Recommendation: wait at the dock point; when the transport `IsStopped()` within reach, walk onto the
   deck, then `AddPassenger(bot, offset)`; leave with `RemovePassenger` when it stops at the target dock. If a moving
   deck makes the bot fall through, stop and report rather than snap it (no teleports).
