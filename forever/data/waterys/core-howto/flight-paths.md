# Flight paths (taxi) — client flow vs core

Client build **1.60.1.70938** (Interface **16001**), 2026-11-17. Core: TrinityCore `6517af05`
(`forever/spike/TrinityCore`); paths are relative to that clone root.

## Client side, step by step

1. **Click a flight master**. The gossip menu sends `CMSG_TAXI_NODE_STATUS_QUERY` (`Opcodes.cpp:1117`) →
   `HandleTaxiNodeStatusQueryOpcode` (`Handlers/TaxiHandler.cpp:41`). If the current node is not yet in the
   player's taximask, `SendLearnNewTaxiNode` (`TaxiHandler.cpp:129`) learns it
   (`PlayerTaxi::SetTaximaskNode`, `Entities/Player/PlayerTaxi.h:59`) and answers `SMSG_NEW_TAXI_PATH`
   (`Server/Packets/TaxiPackets.h:105`) + `SMSG_TAXI_NODE_STATUS` (`TaxiPackets.h:43`).
2. **Open the taxi map**. `CMSG_TAXI_QUERY_AVAILABLE_NODES` (`Opcodes.cpp:1118`) →
   `HandleTaxiQueryAvailableNodesOpcode` (`TaxiHandler.cpp:72`) → `WorldSession::SendTaxiMenu`
   (`TaxiHandler.cpp:93`): the current node comes from `sObjectMgr->GetNearestTaxiNode`, the known-node mask
   from `PlayerTaxi::AppendTaximaskTo` (`PlayerTaxi.h:73`) and the reachable mask from
   `TaxiPathGraph::GetReachableNodesMask`; the answer is `SMSG_SHOW_TAXI_NODES` (`TaxiPackets.h:60`,
   opcode `Opcodes.cpp:2426`).
3. **Pick a destination**. `CMSG_ACTIVATE_TAXI` (`Opcodes.cpp:157`) → `HandleActivateTaxiOpcode`
   (`TaxiHandler.cpp:159`): NPC must have `UNIT_NPC_FLAG_FLIGHTMASTER`, both endpoints must be in the
   taximask (`m_taxi.IsTaximaskNodeKnown`, else `ERR_TAXINOTVISITED`, `TaxiHandler.cpp:180`), then the node
   list is expanded by `TaxiPathGraph::GetCompleteNodeRoute` (`Entities/Taxi/TaxiPathGraph.cpp:198` —
   direct edge if one exists, otherwise Dijkstra over the taxi graph) and handed to
   `Player::ActivateTaxiPathTo` (`Entities/Player/Player.cpp:24494`).
4. **Server-side validation and charging** (`Player::ActivateTaxiPathTo`, `Player.cpp:24494`): at least two
   nodes (`:24497`), refuses while logging out / in combat / stunned / rooted (`Player.cpp:24504`), strips mount and
   shapeshift at the flight master (`Player.cpp:24517`), walks the route adding destinations with
   `PlayerTaxi::AddTaxiDestination` (`PlayerTaxi.cpp:150`) and summing the per-hop cost, picks the mount
   display (`GetTaxiMountDisplayId`), applies the reputation discount and the classic perk price modifier
   (`Player.cpp:24639` / `:24649`), checks money (`Player.cpp:24652`) and then either teleports (only with
   `CONFIG_INSTANT_TAXI`, `Player.cpp:24665`) or replies `SMSG_ACTIVATE_TAXI_REPLY` / `ERR_TAXIOK`
   (`Player.cpp:24679`) and starts the flight through `StartTaxiMovement` (`Player.cpp:24761`).
5. **Movement**. `StartTaxiMovement` mounts the player and calls `MotionMaster::MoveTaxiFlight`
   (`Movement/MotionMaster.cpp:1078`), which installs a `FlightPathMovementGenerator`
   (`Movement/MovementGenerators/FlightPathMovementGenerator.h:33`); `LoadPath`
   (`FlightPathMovementGenerator.cpp:218`) builds the spline and per-node events fire in `DoEventIfAny`
   (`:278`). Arrival runs `DoFinalize` (`:161`) → `Player::FinishTaxiFlight` (`Player.cpp:24695`) and
   `CleanupAfterTaxiFlight` (`Player.cpp:24704`, dismount + clear taxi flags).
6. **Early landing ("stop here")**. `CMSG_TAXI_REQUEST_EARLY_LANDING` (`Opcodes.cpp:1119`) →
   `HandleTaxiRequestEarlyLanding` (`TaxiHandler.cpp:218`): `PlayerTaxi::RequestEarlyLanding`
   (`PlayerTaxi.cpp:175`) truncates the destination deque at the first already-known node, then the
   generator is reloaded and reset (`TaxiHandler.cpp:222`).
7. **Scripted / spell taxi** uses the second overload `Player::ActivateTaxiPathTo(taxi_path_id, ...)`
   (`Player.cpp:24685`), which resolves one `TaxiPathEntry` and calls the same routine with `npc == nullptr`
   (no reputation discount, no mount display fallback restrictions).

## Notes / limits

- Nodes are client DB2 data (`TaxiNodes`/`TaxiPath`/`TaxiPathNode`, `sTaxiNodesStore`, `sTaxiPathStore`);
  the core keeps only the per-character bitmask (`PlayerTaxi`, serialised in `characters.taximask`,
  `PlayerTaxi::LoadTaxiMask`, `PlayerTaxi.cpp:51`).
- Cost is computed from `TaxiPathEntry::Price` per hop, scaled by `GetReputationPriceDiscount(npc)` and the
  classic "Frequent Flier" modifier; the client shows the same total because it uses the same table.
- `CONFIG_INSTANT_TAXI` (`Player.cpp:24665`) short-circuits the whole movement generator with a teleport —
  worth knowing when a flight "takes no time" on a test realm.
- `CMSG_ENABLE_TAXI_NODE` (`Opcodes.cpp:457`, handler `TaxiHandler.cpp:35`) is the GM/unlock path, not part
  of a normal client flight.
