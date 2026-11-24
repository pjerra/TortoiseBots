# W5 Notes: Flights, Boats, Zeppelins, and Tram (`A-w5-flights-boats-tram`)

Branch: `feat/w5-flights` (based on `main`, commit `495ffd943`).

## What was done

1. **Tram check (Step 1)**:
   - Checked ForeverCore (`GameObject.cpp:140+`) for `GAMEOBJECT_TYPE_TRANSPORT` (type 11): supported via `go->ToTransportBase()`, `sTransportMgr->GetTransportAnimInfo(entry)`, `GO_DYNFLAG_LO_STOPPED`, `AddPassenger`, and `RemovePassenger`.
   - Checked `forever/data/waterys/zone-levels.csv`: map 369 ("Deeprun Tram?") has 26 creature spawns; no live DB access is available from this worker to verify `gameobject` spawns of type 11 on map 369.
   - `TbHostTransport` supports both MO transports (type 15, `Map::GetTransport` / `Map::GetTransports`) and type-11 `GAMEOBJECT_TYPE_TRANSPORT` (`Map::GetGameObject` -> `ToTransportBase()`). Lead query to confirm tram spawns on `forever-db`:
     ```sql
     SELECT g.guid, g.id, gt.name, gt.type, g.map, g.position_x, g.position_y, g.position_z
     FROM gameobject g
     JOIN gameobject_template gt ON gt.entry = g.id
     WHERE g.map = 369 AND gt.type = 11;
     ```

2. **Pure policies (`ai/playerbot/policy/travel/TaxiChoicePolicy.h`, `TransportWaitPolicy.h`)**:
   - `DecideTaxiChoice`: refuses unknown endpoints (`unknown-node`), missing routes (`no-route`), busy/combat/rooted bots (`busy`), and fares above `botMoneyCopper` (`money`, answers `Walk`).
   - `BuildClassicFlightLinks` / `IsClassicContinentMap`: pure runtime flight-link builder filtering to classic maps `0`/`1` and faction masks.
   - `DecideTransportWait`: idles when `!mapReady` (`FindMap() == nullptr`), boards stopped transports within reach (`Board`), leaves at destination dock (`Leave`), and gives up after `totalPathTimeMs + marginMs` (`GiveUp`).
   - `IsAllowedClassicTransportRoute`: allows classic routes (`0`, `1`, and `369` for tram) and rejects custom routes unless explicitly allow-listed.

3. **Host seams (`forever/src/host/TbHostTaxi.{h,cpp}`, `forever/src/host/TbHostTransport.{h,cpp}`)**:
   - `tbhost::taxi`: `NearestFlightMaster`, `NodeOfFlightMaster`, `Learn` (typed `WorldPackets::Taxi::TaxiQueryAvailableNodes` -> `HandleTaxiQueryAvailableNodesOpcode`), `KnownNodes`, `KnownNodeCount`, `ResolveNodeArg`, `Quote` (`TaxiPathGraph::GetCompleteNodeRoute` + `sObjectMgr->GetTaxiPath` + `GetReputationPriceDiscount` + `ApplyClassicLegacyPriceMod`), `Fly` (typed `WorldPackets::Taxi::ActivateTaxi` -> `HandleActivateTaxiOpcode`, never instant flight or teleport), `InFlight`, `TickLandingWatch` (`[W5] fly-landed`), and `BuildRuntimeFlightLinks` (ready for W4; not wired into W4's `TravelLinkUsableCompat` because W4 is not merged yet).
   - `tbhost::ride`: `StartRide`, `TickRide`, `IsOnBoard`, `CurrentTransportGuid`. Re-finds transport by GUID each tick (`Map::GetTransport` / `Map::GetGameObject`), idles when `FindMap()` is null during far-teleports, walks onto deck + `AddPassenger` on board, and `RemovePassenger` at destination dock. Logs `[W5] ride-wait`, `[W5] ride-board`, `[W5] ride-leave`, `[W5] ride-giveup`, `[W5] ride-refused`.

4. **Ordered commands, state, and tick hooks (`forever/src/host/TbOrders.cpp`, `TbPartyState.cpp`, `TbEngineBrain.cpp`)**:
   - Appended `.tb order <bot|party [leader]> taxi-learn`, `fly <node id|name>`, `ride <transport entry>`.
   - Appended `taxi=<known count>`, `onTaxi=<0|1>`, `transport=<guid|none>` to `.tb state`.
   - Wired `tbhost::taxi::TickLandingWatch` and `tbhost::ride::TickRide` into `TbEngineBrain.cpp` `Tick`.

## Gate results

- **Pure unit tests (`forever/tools/test_w5_flights.cpp`, standalone g++ C++17)**:
  `PASS: test_w5_flights (7 checks, 0 failed)`
- **Wiring test (`forever/tools/test_w5_flights_wiring.py`)**:
  `Ran 5 tests in 0.001s` -> `OK`
  `--mutate` -> `FAILED (failures=1)` (`mutate_exit=1`)
- **Registry & context checks**:
  `summary: (a)=0  (b)=228 keys to register or stub later`
  `SliceAiObjectContext.cpp is up to date`
- **`tortoisebots` module build (`TORTOISEBOTS_SLICE1=ON`)**:
  `[335/335] Linking CXX static library src/server/modules/TortoiseBots/libtortoisebots.a`
  `BUILD OK tortoisebots ON 70ea47564`
- **`worldserver` full link (`BUILD_TORTOISEBOTS=1`, `TORTOISEBOTS_SLICE1=ON`)**:
  `[336/336] Linking CXX executable bin/RelWithDebInfo/bin/worldserver`
  `BUILD OK worldserver ON 70ea47564`
- **`worldserver` OFF link (`BUILD_TORTOISEBOTS=0`)**:
  `[1847/1847] Linking CXX executable bin/RelWithDebInfo/bin/worldserver`
  `BUILD OK worldserver OFF 70ea47564`
- **Census (`forever/SLICE-W5.txt`)**:
  `tbf-census.sh` (using `forever-builder-clang:local` against the GCC-configured `/build` `compdb`) reported `files 286  compile clean 0  with errors 286  error places 0 (in 0 files)  includes of a header ForeverCore does not have 0` due to the GCC/Clang flag mismatch in `compdb`; all 286 files in `SLICE-W5.txt` (= `SLICE-W1.txt`) compiled cleanly in the GCC `tortoisebots` and `worldserver` ON builds above.

## Live test steps (for combined lead run)

1. Run the tram query above on `forever-db` (read-only) to confirm whether type-11 spawns exist on map 369.
2. **Flight (Alliance or Horde)**: position bot `Testbot` next to a flight master with copper for the fare, and ensure `CONFIG_INSTANT_TAXI` is `0`:
   ```
   tb engine Testbot on
   tb state Testbot #s0
   tb order Testbot taxi-learn #o1
   tb order Testbot fly <known destination node id|name> #o2
   tb state Testbot #s1
   ```
   Expected: `taxi=` increases after `taxi-learn`; during flight `onTaxi=true` and `[W5] fly-start ... fare=... money_before=... money_after=...`; on arrival `[W5] fly-landed ...` and `onTaxi=false` with `tele=` unchanged. With insufficient copper: `[W5] fly-refused ... code=money`.
3. **Boat / Zeppelin**: position Horde bot at Orgrimmar zeppelin tower (entry `164871` or Undercity-Orgrimmar entry) or Alliance bot at Menethil dock:
   ```
   tb order Testbot ride <transport entry> #o3
   tb state Testbot #s2
   ```
   Expected: `[W5] ride-wait`, `[W5] ride-board`, `transport=<guid>` while aboard, far-teleport acked cleanly, and `[W5] ride-leave` at the destination dock within one transport cycle.

## What waits for W4

- Wiring `tbhost::taxi::BuildRuntimeFlightLinks` and `tbhost::taxi`/`tbhost::ride` into W4's `TravelNode` / `TravelLinkUsableCompat` once `origin/world/w4a1`/`w4a2` and W4 round B/C are merged.

## Live result (board ticket #150, 2026-11-22, Waterys Island Test: core b441bf74, main d0d13b9f6)

Run by Audrey in game (Asdasdasd, Horde GM) with host bots; state read from the server log and the database before/after
(`out/w3-liveproof-150.md` in the worker notes). Setup note: a bot needs `.tb master <bot> <player>` AND `.tb party <player> <bot>`
(a bot outside the commander's party loses the binding on the next tick, so no engine starts).

- **Flight: PASS.** `taxi-learn` at Doras (Orgrimmar) then `fly 25` (Crossroads): `[W5] taxi-learn bot=Homeleei npc=3310 node=23
  before=0 after=1 total_known=2`, `[W5] fly-start ... from=23 to=25 fare=110 money_before=100000 money_after=99890`,
  `[W5] fly-landed ... dest=25`; DB taximask gained node 23; copper -110 per flight; `.tb state` `ord=..:ok onTaxi=no` at the Crossroads.
  A player who teleports away while the bot flies pulls it along by the party catch-up: take it out of the party (`.tb ungroup`) after
  `onTaxi=true` (the taxi goes on).
- **Zeppelin (`ride 164871`, Orgrimmar -> Undercity): FAIL**, own bug ticket. The Approach step walks to the transport's origin instead of
  a deck/boarding point (the deck is not on the navmesh); `SampleTransportDeckZ` never saw the deck (`ride-board-refused code=off-deck
  has_deck=0`, 5 of 5, also with the bot teleported onto the docked deck); `AddPassenger` still ran at a wrong spot (the bot was carried
  beside the hull, then the party follow pulled it off and it fell); the order stayed `pending:aboard` after the bot was off the
  transport. Fix direction: board from a dock point with a confirmed deck surface, fail when a boarded bot is no longer on the transport,
  pause follow/stay movement during a ride.
- **#215 fix (zeppelins and boats):**
  - *Deck check:* the core never turns collision on for an MO transport's model (`Transport::Create` sets the go state before
    `CreateModel`, and `GameObject::SetGoState` skips transports), and `GameObjectModel::IntersectRay` answers "no hit" for a model
    with collision off: that was the `has_deck=0`. The deck ray now switches the model's collision on for the one query and puts it
    back as it was.
  - *Approach:* the deck is sampled on a 2 yd grid within 26 yd of the transport's origin, at the bot's height band; the sample
    nearest the bot is the boarding point (`[W5] ride-deck-point`). The bot walks the navmesh path to the dock edge, then one straight
    step onto the deck; it boards only with the deck under it. Board reach from the origin is now 30 yd (was 12).
  - *Stale state:* a boarded bot that is no longer a passenger for 5 s fails the order with `fail:lost-transport`
    (`[W5] ride-giveup ... code=lost-transport`).
  - *Follow vs ride:* while a ride runs (waiting, stepping on, aboard) the engine tick is skipped unless the bot is in combat, and the
    host catch-up teleport is refused with `stuck=blocked:riding`.
  - Tests: `forever/tools/test_ride.cpp` (boarding point, approach mode, lost transport, movement hold) and
    `forever/tools/test_w5_flights_wiring.py` (`test_ride_fixes_215`).
