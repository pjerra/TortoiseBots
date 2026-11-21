# Review Fixes: Transport Ride (`fix/wl2r-ride`, PR #97, W5)

## Findings Fixed

### Finding 5 (HIGH): 2D-only boarding check attached bots below/above docked ships
- **What was wrong**: `TbHostTransport.cpp` checked only 2D distance (`kMaxBoardReachYd = 12.0f`) before calling `AddPassenger`, so a bot standing on the ground below a docked ship (or above it) was attached and carried away.
- **Fix**:
  - Added `SampleTransportDeckZ` in `forever/src/host/TbHostTransport.cpp` using `go->m_model->IntersectRay` (from `botZ + 2.0f` straight down against the transport's `GameObjectModel` when present, falling back to `transportObj->GetPositionZ()` when no model is loaded).
  - Added `IsOnTransportDeck` and `hasDeckZ` / `botZ` / `deckZ` / `maxDeckZDiff` (`2.5f` yd) to `TransportWaitInput` in `ai/playerbot/policy/travel/TransportWaitPolicy.h`. `DecideTransportWait` refuses `Board` unless `IsOnTransportDeck` is true.
  - When within 2D board reach of a stopped transport but off-deck, `TickRide` logs `[W5] ride-board-refused ... code=off-deck` and reports `pending:off-deck` (or `fail:off-deck` if it times out).
- **Test (`forever/tools/test_ride.cpp`)**:
  - FAIL before:
    - `FAIL: finding5_below_deck_must_not_board`
    - `FAIL: finding5_above_deck_must_not_board`
    - `FAIL: finding5_no_deck_surface_must_not_board`
  - PASS after: `PASS: test_ride (0 failed)`

### Finding 7 (MEDIUM): Straight-line approach without pathfinding
- **What was wrong**: `TickRide` called `MovePoint(0, x, y, z, false)` (pathfinding disabled), walking in a straight line through geometry.
- **Fix**:
  - Added `IsApproachPathUsable` and `approachPathFailed` to `ai/playerbot/policy/travel/TransportWaitPolicy.h`.
  - In `TbHostTransport.cpp` `Approach`, calculates a path via `PathGenerator` first; if `IsApproachPathUsable` fails (`PATHFIND_NOPATH` or `< 2` points), gives up immediately with `[W5] ride-giveup ... code=no-path` and `fail:no-path`. Otherwise moves with `MovePoint(0, x, y, z, true)`.
- **Test (`forever/tools/test_ride.cpp`)**:
  - FAIL before: `FAIL: finding7_approach_nopath_gives_up`
  - PASS after: `PASS: test_ride (0 failed)`

### Finding 8 (MEDIUM): Per-tick `FindByEntry` scan, no give-up for missing tram/transport, `ride` answered `ok` immediately
- **What was wrong**:
  - When `transportGuid` was empty, `TickRide` called `FindByEntry` (scanning `map->GetGameObjectBySpawnIdStore()`) every tick.
  - When `totalPathTimeMs == 0` (e.g. a tram entry never spawned on the map), `DecideTransportWait` never returned `GiveUp`.
  - `StartRide` / `.tb order <bot> ride <entry>` returned `"ok"` immediately on start instead of waiting for arrival.
- **Fix**:
  - Added `ShouldRescanTransport` and `kRescanIntervalMs = 3000` (caches `transportGuid` and throttles rescans to once every 3s).
  - Added `missingTimeoutMs = 30000` to `TransportWaitInput` when `totalPathTimeMs == 0` (and also checks `sTransportMgr->GetTransportAnimInfo` for type-11 cycle time), giving up with `fail:not-found` if never found or `fail:timeout` otherwise.
  - Added `RideOrderResult`, `tbhost::ride::PollOrderErrand`, and `tbhost::ride::CancelOrderErrand`, wired into `forever/src/host/TbOrders.cpp`: `ride` returns `pending:waiting` / `pending:off-deck` / `pending:aboard`, `"ok"` only on `Leave` at the destination dock, or `"fail:<code >"`.
- **Test (`forever/tools/test_ride.cpp`)**:
  - FAIL before: `FAIL: finding8_never_found_gives_up`
  - PASS after: `PASS: test_ride (0 failed)`

## Live Checks (for lead)
1. **Off-deck refusal**: place bot on ground/water directly beneath a docked zeppelin (e.g. Orgrimmar tower):
   - `.tb order <bot> ride 164871`
   - Expected: order result `pending:off-deck` (or `fail:no-path` / `fail:off-deck`), `[W5] ride-board-refused ... code=off-deck`, bot is NOT attached (`transport=none`).
2. **Normal ride**: place bot at dock/tower with walkable path onto deck:
   - `.tb order <bot> ride 164871`
   - Expected: `pending:waiting` -> `pending:aboard` (`transport=<guid>`) -> `ok` on `[W5] ride-leave` at destination dock.
3. **Missing entry**: `.tb order <bot> ride 999999` (or unspawned tram entry):
   - Expected: `pending:waiting` then after ~30s `[W5] ride-giveup ... code=not-found` and order result `fail:not-found`.
