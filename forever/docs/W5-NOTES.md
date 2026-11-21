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
