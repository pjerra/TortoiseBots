# TBF #159: the vanilla dungeon plans, re-anchored to this world (2026-11-23)

Branch `waterys/dungeon-plans-159` off `origin/dev` (09463517a). Zero core changes, zero module code changes: plan rows
(`forever/sql/world/2026_11_23_10` to `_26`), their spawn data (`forever/data/plans`), the generator and its test.

## 1. Why the rows had to change

The plan files are the agent's (`forever-agent/dungeons/<plan>.json`), written on Turtle WoW. `plan-export --forever`
turns them into this module's rows, but every unit row keeps its Turtle position, and `.tb plan check` matches unit rows
to the pinned world DB's creature spawns (TbPlanMatch.h: 5 yd matched, 15 yd near, one-to-one). On dev, the Deadmines rows
(`2026_11_20_11`, an export of forever-agent b7c87854) check offline as **161 matched, 15 near, 47 missing**. The misses:

* **patrols**: the Turtle patrol row stands at the first point of Turtle's path; this world's patroller has its own spawn
  point and path (`creature_addon.PathId`), often 5-20 yd away;
* **one patrol named by two pull groups**: both rows want the same spawn, so one of them is always missing;
* **Turtle-only creatures**: Defias Companion 3450 (in 8 Deadmines rows) has no spawn on map 36 here;
* **Turtle-only packs**: the Deadmines hall (leg 5) has three packs that are not in this world, at points the navmesh
  does not reach either.

The good news: this world's classic spawns are Turtle's spawns with 20000000 added to the id (spawn 79144 there is
20079144 here, same entry, same place), so nearly every pack member is still where the plan says.

## 2. What the generator does

`forever/tools/plan_rows_forever.py --sql <export> --out <file>` (the docstring has the rules) reads the exporter's
rows and the map's spawn list (`forever/data/plans/spawns-map<N>.tsv`, and the patrollers' waypoints in
`paths-map<N>.tsv`, both written from `TDB_full_world_forever_2026_10_14.sql`: the TDB this core names), then:

1. pack, boss and first rows take the nearest free spawn of their entry within 15 yd;
2. a patrol row that repeats an earlier step's patrol is dropped (the creature dies at the first step); the others take
   the free patroller whose spawn or waypoint path passes nearest (25 yd);
3. a pack row still unmatched takes the nearest free spawn within 30 yd (logged);
4. rows of entries with no spawn on the map are dropped and the entry leaves the step's `entries`; a boss step for a
   boss with no spawn is dropped too, unless the boss is an event boss of this world (Balnazzar 10813: Dathrohan turns
   into him);
5. a pull step left with no unit row is dropped and its leg renumbered;
6. per-plan fixes (section 3).

Every kept row is written at its spawn's exact position. The source text names the agent commit and the TDB.
`forever/tools/test_plan_rows_forever.py` re-runs the module's match against the committed spawn lists for every file
(every unit row must match, one-to-one), checks the text widths against the columns and the step numbering, and tests
the rules on a made-up map.

Export command (forever-agent dc169d89, built from source; the shipped bin/ is older and has no `--forever` check):
`forever-agent plan-export --forever --allow-draft --approach-hops --mmaps <test server mmaps> --out <dir>/ <plan>`.
`--skip-legs death_prophet_rakameg` (Razorfen Downs) and `--skip-legs duke_dreadmoore,scorn` (Scarlet Graveyard): legs
that only walk to a Turtle-only boss (Scorn is the Scourge invasion's, not a dungeon spawn).

## 3. Deadmines: the doors of this world

From the in-game checks on this world (client 70959 on the repack, sessions 17-18):

| object | in this world | plan |
|---|---|---|
| Heavy Door 17153 (factory) | opens on a click | `open` + `wait_object` (as exported) |
| Mast Room Door 16400 (after Sneed) | opens on a click; the core also links it to Sneed's death | **new `open` before the wait** |
| Foundry Door 16399 (after Gilnid) | opens on a click; lever 101834 next to it opens nothing | **new `open` before the wait** |
| Iron Clad Door 16397 | blown open by the Defias Cannon 16398 (needs Defias Gunpowder 5397, from the chest 17155) | `loot` 17155, `open` 16398, `wait_object` 16397 active_alt (as exported) |
| Heavy Door 17154 (west area) | not checked in game | `open` + `wait_object` (as exported) |

An `open` on a door that is already open does nothing (`TbRunTasksPure.h` `AlreadyUsed`: "already open: not used
again"), so the new rows are safe when Sneed's or Gilnid's death opened the door first.

## 4. The plans

Offline `.tb plan check` = the module's match run on the committed spawn list (my copy reproduces the live numbers of
Ragefire Chasm, 95 matched 1 near 7 optional, and Wailing Caverns, 193 matched). Navmesh = forever-agent
`plan-check --forever` on the test server's mmaps (the plan file's own points, before the drops; "detour" findings are
warnings about long ways round to a pull spot, not failures).

| plan | file | legs | steps | units | check (matched / near / missing) | dropped rows / pull steps / boss steps | navmesh |
|---|---|---|---|---|---|---|---|
| deadmines | `2026_11_23_10` | 15 | 184 | 171 | 170 / 1 / 0 | 60 / 4 / 0 | 6 of 205 points fail (1 root) - the 3 dropped hall pulls |
| blackfathom_deeps | `2026_11_23_11` | 7 | 322 | 205 | 202 / 3 / 0 | 27 / 9 / 1 | 43 of 244 points fail (2 root, 41 re-anchored after an earlier failure) |
| blackrock_depths | `2026_11_23_12` | 10 | 909 | 935 | 935 / 0 / 0 | 5 / 0 / 0 | 39 of 1000 points fail (1 root, 38 re-anchored after an earlier failure) |
| dire_maul_east | `2026_11_23_13` | 4 | 306 | 278 | 273 / 5 / 0 | 56 / 1 / 0 | ok, 275 points on the map 429 navmesh |
| dire_maul_north | `2026_11_23_14` | 5 | 151 | 244 | 239 / 5 / 0 | 29 / 0 / 0 | ok, 243 points on the map 429 navmesh |
| gnomeregan | `2026_11_23_15` | 5 | 165 | 164 | 164 / 0 / 0 | 3 / 3 / 0 | 1 of 186 points fail (0 root, 1 re-anchored after an earlier failure) |
| lower_blackrock_spire | `2026_11_23_16` | 7 | 437 | 358 | 357 / 1 / 0 | 55 / 1 / 0 | 10 of 385 points fail (1 root, 9 re-anchored after an earlier failure) |
| maraudon_orange | `2026_11_23_17` | 6 | 781 | 348 | 348 / 0 / 0 | 79 / 0 / 0 | 1 of 349 points fail (1 root, 0 re-anchored after an earlier failure) |
| maraudon_purple | `2026_11_23_18` | 6 | 1498 | 395 | 395 / 0 / 0 | 95 / 2 / 0 | 1 of 401 points fail (1 root, 0 re-anchored after an earlier failure) |
| razorfen_downs | `2026_11_23_19` | 3 | 488 | 209 | 208 / 1 / 0 | 24 / 1 / 0 | 38 of 264 points fail (2 root, 36 re-anchored after an earlier failure) |
| razorfen_kraul | `2026_11_23_20` | 9 | 430 | 177 | 177 / 0 / 0 | 18 / 8 / 1 | 64 of 287 points fail (1 root, 63 re-anchored after an earlier failure) |
| scarlet_armory | `2026_11_23_21` | 2 | 75 | 74 | 73 / 1 / 0 | 18 / 7 / 1 | 14 of 84 points fail (1 root, 13 re-anchored after an earlier failure) |
| scarlet_graveyard | `2026_11_23_22` | 3 | 80 | 93 | 93 / 0 / 0 | 15 / 6 / 0 | 20 of 119 points fail (1 root, 19 re-anchored after an earlier failure) |
| scarlet_library | `2026_11_23_23` | 3 | 83 | 77 | 77 / 0 / 0 | 8 / 2 / 1 | 4 of 84 points fail (1 root, 3 re-anchored after an earlier failure) |
| scholomance | `2026_11_23_24` | 10 | 638 | 353 | 353 / 0 / 0 | 101 / 33 / 0 | 3 of 524 points fail (1 root, 2 re-anchored after an earlier failure) |
| stormwind_stockade | `2026_11_23_25` | 5 | 96 | 90 | 90 / 0 / 0 | 0 / 0 / 0 | ok, 87 points on the map 34 navmesh |
| stratholme_live | `2026_11_23_26` | 3 | 184 | 208 | 208 / 0 / 0 | 23 / 2 / 0 | ok, 231 points on the map 329 navmesh |

What the navmesh failures are, after the drops: the off-mesh points of Deadmines (hall), Razorfen Downs (the Rakameg
leg), Scarlet Armory, Graveyard and Library were all in packs or legs that are not in this world, and none of those
points is in the files any more (checked by grep). What stays: Blackfathom Deeps' first point (-323.9 193.8 -64.7) is in
the entrance water and the mesh has no dry way to it (partial path, 10.7 yd short), so that plan needs a mapping walk
before a live run; every other root failure is a "detour" (a pull spot 28-44 yd away whose walked way is 90-450 yd: the
tank finds no firing spot within range), which the run leader's approach-stand search handles but which only a live
walk proves. **No plan except Deadmines has had a live run** (section 5).

Not exported (the exporter refuses rows this module has no format for; nothing written):

| plan | why |
|---|---|
| dire_maul_west | 5 `hold` rows (Tendris, Illyanna, Immol'thar) |
| scarlet_cathedral | a `hold` at Mograine |
| shadowfang_keep | a `gossip` step (the courtyard door) |
| stratholme_undead | a `hold` at Baron Rivendare |
| sunken_temple | a `wait_for` on units and a `hold` at Atal'alarion |
| uldaman | `use_item`, a boss `loot_item`, a `ritual` (the Keepers' altar), `hold` |
| zulfarrak | an `area_trigger` step, `wait_for` on units, `leftover_entries` |
| upper_blackrock_spire | a raid plan (kind `raid`): the rows are a dungeon plan |

Turtle-only maps (no spawn in this world, not exported): crescent_grove, dragonmaw_retreat, emerald_sanctum,
frostmane_hollow, gilneas_city, hateforge_quarry, karazhan_crypt, lower_karazhan_halls, upper_karazhan_halls,
stormwind_vault, stormwrought_ruins, timbermaw_hold, windhorn_canyon; black_morass is the Burning Crusade's map 269.
Raids (molten_core, onyxias_lair, blackwing_lair, zul_gurub, ruins/temple of Ahn'Qiraj, naxxramas) are not in this ticket.

## 5. Live

**Deadmines: live-proven to the last boss (2026-11-23, Waterys Island Test server, Audrey's OK).**

Server: core b441bf74 (dev's pin), module = local test branch `test/159-live` (this branch + the open talent PRs #235/#249,
never pushed; the trait-tree talent builds are only there), the plan rows applied by hand (`forever/sql/README.md`), boot:
`Loaded 19 dungeon plans ... 0 rows refused`, `Loaded 4675 dungeon step units for 19 plans (0 rows refused)`.

Party: level 22, the `rfc/kits-level22.csv` gear (protection warrior with the new shield, section 6), `.tb spec` + `.tb
talents` (#235 builds: 13 points each), one bot per account, a GM commander bot (`.tb master party Dmtank Dmcmd`), roles set.
`.tb plan deadmines check Dmtank`: **units=171 matched=170 near=1 missing=0 steps=128 steps_ok=128 steps_short=0** (the
offline numbers).

| segment | time | steps | result |
|---|---|---|---|
| run 1 (`.tb run start Dmtank deadmines`) | 17:51-18:25 | 0-84 | Rhahk'Zor and Sneed's Shredder dead, 61 pulls, 9 deaths, 1 wipe (step 29, a Defias Evoker pull; the run revived itself). Paused at step 84: "no walkable path toward -186.0 -533.0 51.3": two `pull group N approach` moves of the dropped hall packs were left in the rows. **Fixed** in the generator (an approach move goes with its pull group) and the test; the rows reloaded with `.tb plan reload` (no restart) |
| run 2 (`from leg gilnid`) | 18:27-18:50 | 84-126 | Gilnid dead; 8 deaths, 1 wipe (step 101, Goblin Engineers). Paused at step 126 (`wait_clear` after the gunpowder: "waited 90 s; 1 hostiles still near the spot"); one `.tb run resume`: "no hostile within 45 yd" |
| run 2 after the resume | 18:51-19:21 | 126-184 | cannon used, the Iron Clad Door `active_alt`; Mr. Smite, Captain Greenskin and **Edwin VanCleef dead**: `state=done note=plan done (1 wipe, 2 near wipes, 3 lead deaths)` |

Doors, as planned: Heavy Door 17153 and 17154 `open ... done: opened`; Mast Room Door 16400 and Foundry Door 16399 `open ...
done: already open (state 0): not used again` (the boss's death had opened them; the new rows are harmless); the gunpowder
`loot ... done: 5397:1`, the cannon `open ... used`, `object ... 16397:active_alt`.

Totals: about 87 minutes of run time, 22 deaths, 2 wipes, 2 pauses (one plan bug, fixed; one `wait_clear` timeout). Level 22
was enough; no level-24 retry was needed. Setup findings (test-server only, no code change): `.tb create` takes only accounts
named in `TortoiseBots.Create.TestAccounts` (added for TBRAID20-25); talents auto-spend into the default tree on `.tb level`,
so `.tb spec` must come first (or `reset talents` + `.tb talents`); the kits' priest/mage daggers and rogue crossbow need the
weapon skill (`.tb learn` 1180 / 5011); a masterless tank has no engine (`.tb master party` first); loot filled the tank's
bags below the 12 free slots a run start needs (two bags each).

## 6. The protection warrior's shield (kits)

Both RFC kits gave the protection warrior a two-hander in the off hand (level 18: Big Bad Basher 276887, level 22: Bronze
Battle Axe 7958, InventoryType 17), so the tank had no shield. Now: level 18 off_hand Bear Buckler 4821 (shield, green, req
18, ilvl 23; main hand Quilboar Toothpick 276888, already a one-hander); level 22 main_hand Blurred Axe 4824 (req 22, ilvl
27), off_hand Guardian Buckler 4820 (shield, green, req 20, ilvl 25). `rfc/strict_check_kits.py` now refuses a protection
warrior off_hand that is not InventoryType 14, and its lists gained what they missed (shields as prot armour, the one-hand
axe as a warrior weapon). Run on both files: 0 failures (dev's files: exactly the 2 two-handed off hands). The armory pickle
it reads was not on this PC: a stand-in was built from 70959 ItemSparse.db2 + Item.db2 and the TDB hotfix item rows.
