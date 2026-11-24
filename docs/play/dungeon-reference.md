---
id: play-dungeon-reference
title: WoW Forever 5-Man Dungeon Reference
category: play
summary: The dungeon plans the run leader uses on WoW Forever, with each plan's map and boss order, and the dungeons that have no plan yet.
tags: [dungeon, reference, bosses, instances, plans]
relates_to:
  - play-dungeons-and-raids
  - play-dungeon-run-leader
  - play-known-limitations
---

# WoW Forever 5-Man Dungeon Reference

Companion bots follow you through a dungeon; they do not explore or clear it on their own. The exception is the [dungeon run leader](dungeon-run-leader.md) (`.tb run`, a GameMaster command, on by default but still work in progress): it walks a stored dungeon route by itself ([settings](../server/settings/dungeons-raids.md#aiplayerbotdungeonselfclear)).

- Every route is a plan in the world database; the table below lists the plans the Forever install ships.
- Creature entries are Forever world-database entries, not Turtle WoW entries.
- Boss order is the order of the plan's boss steps. An optional or event boss the plan does not mark as a step is not listed.
- The plan rows' own `comment` column records their state: `shadowfang_keep` is marked `live-checked`, `ragefire_chasm` and `wailing_caverns` are `mapped (not live-checked)`, and the rest are `draft (not mapped, not live-checked)`. Treat a draft plan as likely to get stuck.

For general pulling, crowd control, and wipe recovery tactics, see the [Dungeon & Raid Tactics Guide](dungeons-and-raids.md).

## Shipped plans

| Dungeon | Map | Plan | Bosses in plan order | Run status |
| :--- | :---: | :--- | :--- | :--- |
| Ragefire Chasm | 389 | `ragefire_chasm` | Oggleflint (11517), Taragaman the Hungerer (11520), Jergosh the Invoker (11518), Bazzalan (11519) | Mapped (not live-checked) |
| Wailing Caverns | 43 | `wailing_caverns` | Kresh (3653), Lord Cobrahn (3669), Lord Pythas (3670), Skum (3674), Lord Serpentis (3673), Verdan the Everliving (5775), Mutanus the Devourer (3654) | Mapped (not live-checked) |
| The Deadmines | 36 | `deadmines` | Rhahk'Zor (644), Sneed's Shredder (642), Gilnid (1763), Mr. Smite (646), Captain Greenskin (647), Edwin VanCleef (639) | Draft (not mapped, not live-checked) |
| Blackfathom Deeps | 48 | `blackfathom_deeps` | Ghamoo-ra (4887), Lady Sarevess (4831), Gelihast (6243), Old Serra'kis (4830), Twilight Lord Kelris (4832), Aku'mai (4829) | Draft (not mapped, not live-checked) |
| Blackrock Depths | 230 | `blackrock_depths` | High Interrogator Gerstahn (9018), Houndmaster Grebmar (9319), Bael'Gar (9016), General Angerforge (9033), Golem Lord Argelmach (8983), Lord Incendius (9017), Fineous Darkvire (9056), Ambassador Flamelash (9156), Magmus (9938), Emperor Dagran Thaurissan (9019) | Draft (not mapped, not live-checked) |
| Dire Maul (East) | 429 | `dire_maul_east` | Zevrim Thornhoof (11490), Hydrospawn (13280), Lethtendris (14327), Alzzin the Wildshaper (11492) | Draft (not mapped, not live-checked) |
| Dire Maul (North) | 429 | `dire_maul_north` | Guard Mol'dar (14326), Guard Fengus (14321), Guard Slip'kik (14323), Captain Kromcrush (14325), King Gordok (11501) | Draft (not mapped, not live-checked) |
| Gnomeregan | 90 | `gnomeregan` | Viscous Fallout (7079), Electrocutioner 6000 (6235), Crowd Pummeler 9-60 (6229), Mekgineer Thermaplugg (7800) | Draft (not mapped, not live-checked) |
| Lower Blackrock Spire | 229 | `lower_blackrock_spire` | Highlord Omokk (9196), Shadow Hunter Vosh'gajin (9236), War Master Voone (9237), Mother Smolderweb (10596), Quartermaster Zigris (9736), Halycon (10220), Overlord Wyrmthalak (9568) | Draft (not mapped, not live-checked) |
| Maraudon (orange side) | 349 | `maraudon_orange` | Noxxion (13282), Razorlash (12258), Landslide (12203), Tinkerer Gizlock (13601), Rotgrip (13596), Princess Theradras (12201) | Draft (not mapped, not live-checked) |
| Maraudon (purple side) | 349 | `maraudon_purple` | Lord Vyletongue (12236), Celebras the Cursed (12225), Landslide (12203), Tinkerer Gizlock (13601), Rotgrip (13596), Princess Theradras (12201) | Draft (not mapped, not live-checked) |
| Razorfen Downs | 129 | `razorfen_downs` | Mordresh Fire Eye (7357), Glutton (8567), Amnennar the Coldbringer (7358) | Draft (not mapped, not live-checked) |
| Razorfen Kraul | 47 | `razorfen_kraul` | Roogug (6168), Aggem Thorncurse (4424), Death Speaker Jargba (4428), Overlord Ramtusk (4420), Agathelos the Raging (4422), Charlga Razorflank (4421) | Draft (not mapped, not live-checked) |
| Scarlet Monastery (Armory) | 189 | `scarlet_armory` | Herod (3975) | Draft (not mapped, not live-checked) |
| Scarlet Monastery (Graveyard) | 189 | `scarlet_graveyard` | Interrogator Vishas (3983), Bloodmage Thalnos (4543) | Draft (not mapped, not live-checked) |
| Scarlet Monastery (Library) | 189 | `scarlet_library` | Houndmaster Loksey (3974), Arcanist Doan (6487) | Draft (not mapped, not live-checked) |
| Scholomance | 289 | `scholomance` | Instructor Malicia (10505), The Ravenian (10507), Lorekeeper Polkelt (10901), Lady Illucia Barov (10502), Jandice Barov (10503), Rattlegore (11622), Vectus (10432), Ras Frostwhisper (10508), Doctor Theolen Krastinov (11261), Lord Alexei Barov (10504) | Draft (not mapped, not live-checked) |
| The Stockade | 34 | `stormwind_stockade` | Targorr the Dread (1696), Dextren Ward (1663), Kam Deepfury (1666), Hamhock (1717), Bazil Thredd (1716) | Draft (not mapped, not live-checked) |
| Stratholme (live side) | 329 | `stratholme_live` | Archivist Galford (10811), Balnazzar (10813), Cannon Master Willey (10997) | Draft (not mapped, not live-checked) |
| Shadowfang Keep | 33 | `shadowfang_keep` | Rethilgore (3914), Razorclaw the Butcher (3886), Baron Silverlaine (3887), Commander Springvale (4278), Odo the Blindwatcher (4279), Fenrus the Devourer (4274), Wolf Master Nandos (3927), Archmage Arugal (4275) | Live-checked |

A plan also carries its own walk points, pull groups, doors, loot, object use and waits. The [dungeon run leader](dungeon-run-leader.md) explains which steps the run can perform.

## Dungeons with no plan yet

The Forever install ships no plan for these dungeons:

- Uldaman, Zul'Farrak and Sunken Temple: the route format has no row for their item, ritual or wait steps.
- Dire Maul (West), Scarlet Monastery (Cathedral) and Stratholme (undead side): their plans need hold steps the route format does not have.
- Upper Blackrock Spire: its rows are a raid plan, not a dungeon plan.

A dungeon with no plan can still be cleared by a human leader with companion bots; only the self-driving run leader needs one.

## Not on WoW Forever yet

These Turtle WoW dungeons are **not on WoW Forever yet**: Crescent Grove, Dragonmaw Retreat, Emerald Sanctum, Frostmane Hollow, Gilneas City, Hateforge Quarry, Karazhan Crypt, Lower Karazhan Halls, Upper Karazhan Halls, Stormwind Vault, Stormwrought Ruins, Timbermaw Hold and Windhorn Canyon.
