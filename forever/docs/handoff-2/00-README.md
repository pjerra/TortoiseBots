# ForeverCore port: five world-life items for Sagiroth (context pack 2)

Repository `TortoiseBots/TortoiseBotsForever`, base branch `main`. The module lives under `forever/` and builds inside a
ForeverCore checkout (Karrz1/TrinityCore, branch `forever`) at the commit in `forever/PINNED_CORE`: **25fc7ca** now (the
first pack said 6517af05; the pin moved). Every core `file:line` below was read at 25fc7ca. **Zero core changes**: if an
item seems to need one, write it down in the PR and stop that part.

This pack builds on your open PR #93 (`feat/sagiroth-items`): W6a item value and equip upgrades (`.tb gear <bot> upgrade`,
`RandomItemWeights.cpp`, the weight-scale SQL), W2 town visits (`.tb order <bot> sell|buy|repair|train|mail`, the
`tbhost::town` seam in `forever/src/host/TbHostTown.*`, `TownErrandPolicy.h`) and W3 quests. Where this pack says "your
W2 seam" or "your W6a value" it means the code in that PR.

| File | Item | Plan size (worker-days) |
|---|---|---|
| `A-w5-flights-boats-tram.md` | W5: learn flight nodes on visit, fly known routes (paying), board and leave ships, zeppelins, tram | 5 |
| `B-w6b-gear-generation.md` | W6b: level- and spec-fit gear sets from ForeverCore item templates, `.tb gear <who> set` | 5 |
| `C-w7-professions.md` | W7: pick professions, gather nodes, craft for self, skill-up | 3 |
| `D-w8-auction-house.md` | W8: the bot as a player on ForeverCore's auction house | 4 |
| `E-w9-guilds-and-orders.md` | W9: bot guilds, ranks, the guild bank, the guild order board and delivery | 8 |

The plan sizes are the first estimate. Our W4 grew from 6 to about 14 worker-days once it was specced against the core;
expect the same kind of growth here, mostly in the live parts.

## Order and what each waits for

1. **B (W6b) can start now.** It needs only W6a, which is in your PR #93. It also unblocks the client test bench: 29 of
   its 51 trials are skipped for want of items on the tester's own character, and a `.tb gear <me> set` fills that gap.
2. **C (W7)**: the ordered half (learn at a trainer, gather a node in sight, craft) can start now on your W2 trainer seam
   and slice 9's chest opening. The autonomous half (walking to nodes along a route, to a profession trainer in another
   town) waits for our **W4 round C** (travel actions).
3. **A (W5) waits for our W4**, which we are building now. W4 has rounds A1 (graph load and route on F), A2
   (destinations), B (graph generation: **the graph ships as module SQL in round B**), C (travel actions, the one rescue
   teleport), D (RPG visits), then a live round. W5 needs **A1, B and C merged and B's SQL loaded**; it does not need D.
   Until then you can write W5's host seams, pure policies and ordered commands on your own files.
4. **D (W8) last**: it needs W2 (mail for the money and items the AH sends, the money budget), W6a (what is an upgrade,
   what is worth selling) and W7 (crafts and mats). Walking to a capital's auctioneer needs W4; before that, only an
   auctioneer within the W2 walking rule (bots only visit an NPC in their current zone whose navmesh path is at most
   300 yards; otherwise they skip the visit) is visited.
5. **E (W9) after W7 and W8**: orders are fulfilled by gathering and crafting (W7) and buying on the AH (W8), and
   delivered by mail (W2's mail plus sending, new in W9) or to the guild bank; walking to bankers and farm zones needs
   W4. The Turtle guild-orders work is large and mostly pure policies with tests: E names what to reuse.

## Our W4, what it gives you

- Graph tables `ai_playerbot_travelnode`, `_link`, `_path` read through a guarded reader (`TravelNodeQueryCompat`, built
  on `WorldQueryCompat`); missing tables = empty graph, travel does nothing.
- Link types `taxi` and `transport` are **loaded but never used** on ForeverCore: `TravelLinkUsableCompat` answers false
  for them, and a route that needs one is refused so the bot picks another target. **W5 is the item that flips that.**
- W4's generator makes start, npc, area-trigger, zone-mean and helper nodes; **no taxi, transport or portal nodes**.
- Travel purposes behind a purpose gate: vendor/repair only when W2's keys are registered, quest only when W3's are. W7
  (gather, profession trainer) and W8 (auctioneer) add their purposes the same way.
- One teleport only: the hard-stuck rescue (no 5 yd progress in 5 min, no player within 100 yd) back to the last travel
  node reached. `AiPlayerbot.BotInstantFlight` is forced off on ForeverCore.
- Compat pair names: wide passes took `TbCompat_W2.h`/`W3.h`, so the world slices use **WL**: W4 is
  `forever/src/compat/TbCompat_WL4.h` + `ai/cmangos-compat-wl4.h`. Suggested here: `WL5`, `WL6b`, `WL7`, `WL8`, `WL9` (your
  `TbCompat_W6.h` and `W2Town` stay as they are).

## How to build and test

```
git clone -b forever https://github.com/Karrz1/TrinityCore.git ForeverCore
git -C ForeverCore checkout $(cat TortoiseBotsForever/forever/PINNED_CORE)
ln -s /path/to/TortoiseBotsForever ForeverCore/src/modules/TortoiseBots
cmake -S ForeverCore -B build -DSERVERS=1 -DSCRIPTS=static -DBUILD_TORTOISEBOTS=1 -DTORTOISEBOTS_SLICE1=ON
cmake --build build --target worldserver        # `ninja tortoisebots` = module-only compile gate
```

Gates every item passes before its PR (quote the result lines, as you did in #93):

| Gate | Pass line |
|---|---|
| Census on the item's list (`forever/SLICE-W5.txt` = W4's list + yours; `SLICE-W6B.txt`, `SLICE-W7.txt`, `SLICE-W8.txt`, `SLICE-W9.txt` = `SLICE-W1.txt` + yours) | `with errors 0  error places 0` |
| `worldserver` option ON / OFF | both link; OFF: new commands answer `error code=engine-not-built` |
| Pure checks (`forever/tools/test_*.cpp`, standalone g++ C++17) | `PASS` / `0 failed`, the named failing case first |
| Wiring pins (`forever/tools/test_*_wiring.py`) with a `--mutate` mode | OK; mutation FAILS |
| `slice_registry_check.py`, `slice_gen_context.py --check` | `(a)=0`, up to date |
| Turtle build + unit tests | only when a file outside `forever/` changed |
| Live boot, the item's acceptance check | as written in the item file |

## Rules that bite (all five items)

- **Every module DB query goes through a guarded reader**: `WorldQueryCompat` (`forever/src/compat/TbCompat_S5.h:61`)
  or `CharacterQueryCompat` (`forever/src/host/cmd/TbCmdBindingStore.cpp:91`). ForeverCore's connection **aborts the
  server** on a query naming a missing table or column. Prefer the core's loaded stores (DB2 stores, `sObjectMgr`
  caches, `TransportMgr`, `AuctionHouseMgr`) over SQL; none of A-D needs a characters-DB write of its own (E does: the order store)
  (taxi mask, auctions and skills are saved by the core).
- **A fallback does LESS**, never more, and never reports success it did not see (a flight that did not start, an
  auction the core did not confirm, a craft that left the bags unchanged). Every stand-in is a row in
  `ai/playerbot/UnportedFeatures.h` + `forever/docs/UNPORTED.md`; porting deletes the row, its enum entry and helpers.
- **No raw object pointers across ticks.** Keep guids and ids (flight master guid, transport guid, auctioneer guid,
  auction id, node guid) and re-find each tick (`Map::GetTransport(guid)` `Map.h:447`, `AuctionHouseObject::GetAuction(id)`
  `AuctionHouseMgr.h:292`).
- **`FindMap()`, not `GetMap()`**, on a bot that may be teleporting: zeppelins and ships carry their passengers across
  maps with a far teleport; the bot session acks it (`forever/src/TbBotSession.cpp:271-279`). While `FindMap()` is null
  the bot does nothing that tick.
- **World thread only.** Session handlers you call directly (taxi, AH, cast, game object use) run there.
- **Zero core changes; blizzlike only; bots never teleport** except W4's rescue (and W1's ghost rescue). No instant
  flight, no free items in the real path (test helpers are commands, off the autonomous path).
- **Census 0** on the item's list, **ON and OFF** builds link.
- **Own blocks and files; append registrations**: `W5_KEYS`/`W6B_KEYS`/`W7_KEYS`/`W8_KEYS`/`W9_KEYS` blocks in
  `forever/tools/slice_gen_context.py`; an own block in `forever/src/slice/SliceLinkStubs.cpp`; command rows appended at
  the end of the table in `forever/src/TbScripts.cpp`; `.tb state` keys as one appended block; free-kit additions in
  `policy/world/FreeKitPolicy.h` behind their own config flag (off by default). One owner per shared file per wave: say in
  the PR which rows you took.
- Replies: grammar v2, one encoder (`Enc`/`Rec`/`Reply` in `forever/src/host/TbH3.cpp`), token last, reply TRUE.
- The ForeverCore team's own bot module is GPL-2.0-only: read it for ideas, never copy code or SQL rows. The port is
  private: no mention in any public issue, PR or channel.

## Data in the repo you will want

`forever/data/waterys/`: `core-howto/flight-paths.md` and `core-howto/auction-house.md` (client flow vs core, line
numbers taken at 6517af05; the handler lines are unchanged at 25fc7ca), `service-points/flight-masters.csv` (entry,
position, faction, taxi node id), `auctioneers.csv`, `profession-trainers.csv`, `gear-replacements.csv` (class, spec,
slot, tier to an item that exists on ForeverCore), `gear-rule.md`, `items.csv`, `items-missing-classified.csv`,
`forever-item-traps.csv`, `supplies-by-level.csv`, `tbm-transcripts/professions.txt` and `autogear.txt` (what the Turtle
commands answered).

## World-DB facts that change the plan (our read-only sweep of the world DB at the pin)

- **Taxi data is in no database table.** The world DB has no taxi tables; the `hotfixes` copies are a two-row stub. The
  flight graph is the client DB2 the server loads at startup (`sTaxiNodesStore`, `sTaxiPathStore`). 66 flight-master
  spawns on maps 0/1 (the classic network); none in Elwynn or Dun Morogh, so a start-zone bot walks to the capital first.
- **Transports**: the `transports` table has 13 rows (names carry the routes: Menethil-Theramore, Menethil-Auberdine,
  Auberdine-Rut'theran, Orgrimmar-Undercity, Orgrimmar-Grom'gol, Undercity-Grom'gol, Ratchet-Booty Bay, Feathermoon
  ferry, Naxxramas; custom: two Zephras airships, an Auberdine-Stormwind Harbor ship, a Tanaris-Riverglades ship).
  Paths and timers are not in the DB; they come from DB2 through `TransportMgr`.
- **Spawns carry no zone or area id** (2 of 85,076): every per-zone index is built from coordinates.

## Decisions settled after this pack was written

1. **Tram (A):** check first. The data sweep looked at the wrong game object type (15 instead of 11); look for type-11 transports on map 369 as the first step of W5. If the tram cars are there, ride them like ships; if not, the tram is out of W5.
2. **Ships and flight links (A):** only ships whose routes connect classic zones are allowed. Flight links are built at runtime from the server's own taxi data (as the core loads it), not shipped as module SQL.
3. **Classic items (B):** an item counts as classic when its expansion field is 0 and its id is below 25000. Forever's custom items only through an explicit allow list. No random-suffix rolling in the first version (fixed items only).
4. **Profession pairs (C):** per class: mining + blacksmithing for warriors and paladins; skinning + leatherworking for hunters, rogues, druids and shamans; herbalism + alchemy for mages and warlocks; tailoring + enchanting for priests. Until walking the world (W4) lands, a bot only detours to a node within 150 yards in its current zone.
5. **Auction house (D):** commodities are in (the modern auction house sells stackables that way, so they are required); faction auction houses only, the neutral ones are out of the first version.
6. **Guilds (E):** bots only deposit to the guild bank (its contents are private in the core); bots act only within the rank rights the guild master set, and the default member rank may deposit; enchanting orders are out of the first version.
