# W4-A1: the travel node graph on ForeverCore

Round W4-A1 of `FOREVER-W0-W4-SPEC.md` part 2 (round table). Files: `ai/playerbot/TravelNode.cpp` and its both-core pair
(`forever/src/compat/TbCompat_WL4n.h` + `ai/cmangos-compat-wl4n.h`, wrapper `ai/playerbot/TbCompatWL4n.h`); the `TravelNodeMap` /
`TravelPath` stand-ins of the Travel block in `forever/src/slice/SliceLinkStubs.cpp` are gone; `forever/SLICE-W4.txt` is the
merged W1 list plus this round's legacy file, and `CMakeLists.txt` reads it.

## 1. What runs

| Piece | Where |
|---|---|
| The node store: `ai_playerbot_travelnode`, `_link`, `_path` read through the schema guard | `TravelNodeMap::loadNodeStore` |
| A*, with `TravelRoutePolicy` costs | `TravelNodeMap::getRoute`, `TravelNodePath::getCost` |
| Taxi and transport links loaded, never used | `TravelLinkUsableCompat` in `getCost` (F: false for 3/4) |
| The generator's map passes on the world thread on F | `TravelGenParallelCompat` around the four `std::async` sites |
| Loading the map data of a far tile | `MmapTileLoadCompat` (used by round B; F: `TerrainInfo::LoadMapAndVMap` + `LoadMMap`) |

## 2. The cut (each one makes the bot do LESS)

| Legacy | W4-A1 on F |
|---|---|
| A taxi (`flightPath`) or transport (`transport`) link | the edge cost is -1, the A* never takes it; the route is refused |
| A missing node table or column | empty graph, one line, travel does nothing (the core would abort instead) |
| Generator passes on worker threads | one thread (the generation boot's world thread), Turtle unchanged |

## 3. Shared lines touched

- `forever/src/slice/SliceLinkStubs.cpp`: the TravelNodeMap/TravelPath stand-in lines deleted; one new line, `TravelMgr::LoadMapTransfers`
  (TravelMgr.cpp is W4-A2; TravelNode.cpp calls it from the generator). The TravelMgr destinations and the slice-9/W1 blocks are untouched.
- `CMakeLists.txt`: `TB_SLICE_LIST` now `forever/SLICE-W4.txt` (the comment and the FATAL_ERROR text updated).
- `forever/SLICE-W4.txt`: new; the merged W1 list line for line, then `ai/playerbot/TravelNode.cpp`.

## 4. Gates

(to fill)

## 5. Live
