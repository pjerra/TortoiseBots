# Data and notes from Waterys (received 2026-11-17)

| File | What |
|---|---|
| `items.csv` | Every item id TortoiseBots names in code, SQL or config (2,398 ids: autogear templates, hire-gear tiers, consumables/poisons/ammo-adjacent items, world-buff items, quest/camp tools). Status against Forever: 1,143 OK, 64 CHANGED, 1,191 MISSING. `replacement` is a same-name Forever id when one exists; `note` says why. |
| `talents.csv` | The 28 premade spec builds TortoiseBots ships (9 classes) mapped onto Forever's own Talent/TalentTab DB2: status, Forever talent ids and max ranks (positional candidate mapping; see note), points. |
| `talents-detail.csv` | Per allocated talent: Turtle talent id + rank spells, Forever talent id + max rank, and how it was matched (`spell`, `position`/`row`, `position-custom`, or none). |
| `forever-item-traps.csv` | The known item traps checked directly: 16922 and 18803 absent, 19019 live Thunderfury, 17802 deprecated, 289294 second live id. |
| `forever-spell-ids.csv` | 259 class spells TortoiseBots uses by name: status, ForeverCore rank 1 id, all rank ids, source, replacement. 222 OK, 2 CHANGED, 29 MISSING (Turtle-only or later expansions), 6 PSEUDO. |
| `bosses.csv` | 230 rows for 7 raids (206 bosses, 23 adds, 1 trash): Turtle entry, ForeverCore entry, source. 27 are absent from ForeverCore's `creature_template`. |
| `spawn-id-sites.txt` | 16 places in the bot code that find a creature or game object by database spawn id taken from the GUID. On ForeverCore the GUID carries entry and map, not the spawn id, so each needs another lookup. |
| `TBM-PROTOCOL.md` | The addon-to-server contract of TortoiseBotsManagerForever, unchanged from TBM 2.4.1. |
| `TBMF-PORT-NOTES.md` | What changed in the addon for the 1.60.1 client, what is verified, open questions. |
| `forever-feature-compare.md` | TortoiseBots against ForeverBots (PlayerbotV2), area by area. |

The addon itself (`TortoiseBotsManagerForever-2.4.1-forever.1.zip`) is not in this folder; it belongs in its own
top-level folder or repository.

## Before these ids drive code

167 of the 259 spell rows name ForeverBots source code as where the id came from, not ForeverCore's data. The rule
for this port is that the core's own data wins (DB2 and the world database). So every id is checked against a running
ForeverCore server (name and rank for that id) before a class strategy uses it. Until that check is recorded here,
treat the table as a lead, not as proof.

## How items.csv and talents.csv were built (round 4)

Sources, best first: the Forever world DB (`armory_item`, TDB_full_world_forever_2026_10_14.sql),
the client's `ItemSparse.db2` (1.60.1, WDC5 sparse, read with the pinned core's layout), and the
Rack's TDB item rows. The item candidates come from TortoiseBots' own files: `aiplayerbot.conf.dist.in`
(`GearProgressionSystem` autogear), `data/sql/world` (`tortoise_bots_hire_gear`), `tools/hire_gear/data`
CSVs, and item ids in code (consumable/poison/stone ladders, first-aid, camp/quest/tool constants, food and
water lists). All checks were offline; no server was started.

Talents: TB's links are rank digits in (tabpage, row, column) order. The Turtle `Talent.dbc` from
`helper3/update-v1221/evidence/dbc` gives that list; each Turtle talent is matched to a Forever talent in
the same tab by shared rank spell (authoritative), then by tree position when the spells were renumbered or
are Turtle custom ids. `forever_talent_id`/`max_rank` are therefore candidates: 377 of 501 allocated talents
matched by spell id, 108 by position (82 of them Turtle-custom), 15 have no counterpart. Every build is
CHANGED relative to Forever because Turtle's trees contain custom talents; the note gives the unmapped
points and any rank cap that is lower on Forever.
