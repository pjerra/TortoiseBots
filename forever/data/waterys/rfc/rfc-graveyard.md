# Ragefire Chasm release graveyard — Forever

Audrey's test character **Packet** is Alliance (human mage, race 1) and released inside RFC (map 389).
Both factions end up at the **same** graveyard because the RFC zone links have no team condition.

## Selection

- RFC map 389: `Map.db2` `AreaTableID=2437`, `CorpseMapID=1`, `CorpseX=1816.755859`,
  `CorpseY=-4423.371582`.
- `graveyard_zone` rows for GhostZone **2437** (Ragefire Chasm):
  - safe loc **32** — map 1 `(233.264, -4793.62, 10.1872)`, comment "Durotar, Razor Hill GY"
  - safe loc **850** — map 1 `(1177.69, -4464.07, 21.3552)`, facing `52.4774`, comment
    "Durotar, Northern Durotar GY"
- No `conditions` row (SourceType 27, graveyard) exists for 2437/32/850, so `GetClosestGraveyardInZone`
  is not team-filtered. For an instance corpse, the core compares each linked graveyard's map-1 position
  with the instance entrance `CorpseX/Y` (`ObjectMgr.cpp:7330-7488`). Safe loc 850 is **640 yd** from the
  RFC entrance; safe loc 32 is **1,626 yd**, so **850 is chosen for both Alliance and Horde**.
- `instance_template` for 389 is `classic_instance_ragefire_chasm`; it does not override the graveyard.
  The relevant `areatrigger_teleport` is the instance entrance/exit, not a ghost destination.

**Answer: Alliance gets safe loc 850, map 1, (1177.69, -4464.07, 21.3552), facing 52.4774 ("Northern
Durotar GY"). Horde gets the same 850 in this data.** If a future DB adds a `CONDITION_TEAM` row for
safe loc 32 or 850, re-run the same check; the current TDB has none.

## Nearby NPCs (60 yd of the Alliance spot)

| Distance | Entry | Name | Faction | Hostile to Alliance? |
|---:|---:|---|---:|---|
| 17.7 yd | 6491 | Spirit Healer | 35 | no (friendly spirit healer) |
| 33.5 yd | 3100 | Elder Mottled Boar | 189 | no (neutral beast; faction template has no enemy set) |
| 51.4 yd | 3100 | Elder Mottled Boar | 189 | no |
| 55.6 yd | 5951 | Hare | 31 | no (neutral critter) |

No hostile NPCs within 60 yd. The first potentially hostile spawns are Dustwind Harpies (entries 3115/3116,
faction 514) around 105 yd; nothing guards the graveyard itself. Source: TDB 2026-10-14 `creature` map 1 +
`creature_template.faction`, positions from `creature`; hostility from `FactionTemplate.db2` 70959.

The live repack DB was not queried for this (MySQL was not running and task rules allow using the TDB dump);
if the live `graveyard_zone`/`world_safe_locs`/`creature` rows differ, re-check them with SELECT.
