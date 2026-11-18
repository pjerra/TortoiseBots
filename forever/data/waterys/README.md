# Data and notes from Waterys (received 2026-11-17)

| File | What |
|---|---|
| `service-points/*.csv` | 19 files: flight masters (61, with taxi node), innkeepers (47), repair vendors (322), reagent/ammo vendors (380), class trainers (9 per-class files), profession trainers (289), stable masters (44), bankers (27), auctioneers (29), mailboxes (111), graveyards + spirit healers (1,251). |
| `tactic-spells.csv` | 1,451 boss spell ids from TB's tactics (`ai_boss_tactic`, `boss_abilities.csv`) over 40 raids/dungeons and 389 bosses: existence in client 70938 spell data and static cast evidence on Forever. |
| `quests.csv` | The 23 quest ids TB names (world-buff quests 90000-90013, hire unlocks 90100-90106, Turtle mount 40302, one help-text example): 17 MISSING, 5 id-reuse CHANGED, 1 OK. |
| `sql-inventory.csv` | 360 migrations (319 world, 37 character, 4 rollback, 1 optional plan), 934 file/table rows: 48 CREATE tables (all module-owned), core tables/columns touched, MySQL 8 and Trinity 1.60 schema risks. 19 rows would fail on Trinity's schema; 0 on MySQL 8. |
| `config-knobs.csv` | All 4,283 active `AiPlayerbot.*` knobs: 3,493 NEW_DEFAULT_NEEDED (3,192 autogear item ids, 297 talent links, 4 id lists), 14 TURTLE_ONLY (Turtle mount + shaman tank build), 776 CARRY_OVER, with suggested Forever defaults. |
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

## Round 5: SQL and config inventory

`sql-inventory.csv` was built by splitting every SQL statement (comments/strings aware) in
`data/sql/world`, `data/sql/character`, `tools/optional-plans` and `tools/sql/rollback`, resolving
dynamic `ALTER` statements hidden behind `information_schema` + `PREPARE`, and comparing every touched
core table/column with the bundled Trinity 1.60 schema (the WoW Forever repack's `sql/base`). Own tables are
the module's `ai_*`, `tortoise_bots_*` and `ahbot_items` tables (all created by the migrations).
MySQL 8 risks checked: `TYPE=` engines, zero dates, `VALUES()` in ON DUPLICATE, `ADD/DROP COLUMN IF
[NOT] EXISTS`, unquoted reserved words. None is used; the PREPARE-based ALTERs are explicitly MySQL 8
and MariaDB safe.

`config-knobs.csv` classifies every active `AiPlayerbot.*` line by family. NEW_DEFAULT_NEEDED means the
default embeds data that does not carry over (Turtle item ids in the autogear templates, talent links,
AhBot id lists); CARRY_OVER means generic behaviour; TURTLE_ONLY means the feature only exists on Turtle
(Swift Riding Turtle quest 40302 and the custom shaman "tank" tree). Suggested Forever values are in the
last column; empty/no-value suggestions mean "fill from the Forever data files when the port has them".

## Round 6: service points, tactic spells, quests

Client build is checked in every data row (`client_build`): the client is now 1.60.1.70938
(2026-11-17 `.build.info`), so the DB2 used for taxi/skill/spell names and ids was extracted fresh from
it with the repack extractor into a throw-away folder (same 1,133 files, same layout hashes as the earlier
70888 extraction). The extracted files carry the client's own static header `WOWSTATIC_1_60_1_70399` (the
DB2 static-data version embedded in the 1.60 client); they were extracted from the installed 1.60.1.70938
client, whose `.build.info` and the extractor both report 70938. `client_build` names the client, not that
Two DB2 extractions are in play: repack `server/data/dbc` = 70888 (`70888 (repack server/data/dbc, 2026-11-11)`,
`.client-build` marker, keys 2026-11-07) and a fresh throw-away extraction from the installed 1.60.1.70938
client (2026-11-17, since deleted). Both are 1,133 files with the same layout hashes and the same static
header `WOWSTATIC_1_60_1_70399`. `client_build` always names the extraction actually read
(`70888 (repack server/data/dbc, 2026-11-11)` vs `client DB2 70938 (2026-11-17)`); the two were never
diffed file-by-file, so any row where a 70888-vs-70938 delta would matter must be re-verified on live 70938.

- Service points: NPC categories from `creature_template.npcflag` and coordinates from the first
  `creature` spawn per entry (`spawns` column has the count); flight masters matched to the nearest
  `TaxiNodes` node (client DB2 70938 + the two custom hotfix nodes) within 80 yd. Mailboxes are
  gameobjects of type 19. Graveyards are `graveyard_zone` + `world_safe_locs`; spirit healers are all
  spawns of the spirit-healer templates. Profession names come from `trainer_spell.ReqSkillLine` resolved
  through client `SkillLine.db2`; 28 profession trainers have no trainer rows or a Turtle custom line.
- Tactic spells: every spell named by `ai_boss_tactic` (interrupt/dispel/avoid/move-out and the other
  kinds) and by `boss_abilities.csv` (column `key`) for the seven raids and the dungeons in
  `dungeons.csv`, mapped Turtle→Forever entries through `bosses.csv`/`dungeons.csv`. Existence is against
  client `SpellName`/`Spell` DB2 70938 plus hotfix `spell_name`; cast evidence is a SmartAI cast row by the
  Forever boss entry, a boss `ScriptName` (cast not statically verifiable), or none.
- Quests: ids from `quest_template` SQL, quest constants in code (`WorldBuffPolicy`, `HireUnlockPolicy`),
  and the Turtle mount comment in the config.

## Round 7: supplies, battleground constants, NPC SQL draft, core how-tos

- `supplies-by-level.csv` (127 rows): food/drink/bandage/potion/ammo/class-reagent/pet-food per 10-level
  bracket. Item ids and brackets come from TB's own ladders (`RandomItemMgr`, `MimicConsumablePolicy`,
  `ClassConsumablePolicy`, `PlayerbotFactory` first aid, `AmmoCachePolicy`); names, required level and
  buy price are client `ItemSparse` DB2 (70938, `BuyPrice` per vendor unit) plus Forever `armory_item`,
  `npc_vendor` (copper cost only) and `creature_template`. 34 rows are crafted/no-vendor and carry a blank
  vendor and price; ammo 11-20 and 41-50 have no white-quality vendor ammo, and the ladders end before
  level 51 for the other categories.
- `bg-constants.csv` (55 rows): WSG/AB/AV flag and banner gameobjects, AB nodes, graveyards, area triggers,
  captains/bosses, with TB value, Forever value and evidence. The two WSG area triggers TB uses (3646/3647)
  do not exist in this client's `AreaTrigger` DB2; Forever's WSG script uses CapturePoints with a 6 yd
  radius instead. Several TB graveyard ids are stale; Forever's TDB graveyards are listed row by row.
- `tortoisebots-npcs-forever.sql` + `npcs-sql-notes.md`: DRAFT, not applied. Recruiters 95000-95067 are
  renumbered to 1000000-1000067, credit creatures to 1000070-1000091, greetings to npc_text 1000000+ and
  hotfix `broadcast_text` 1000000+ (95000-95167 is taken in the hotfixes DB). Forever schema: display ids
  in `creature_template_model`, level in `creature_classic_level`; every guessed/dropped column is in the
  notes. World buffs are a recruiter gossip branch, not a vendor NPC.
- `core-howto/*.md` (7 files): LFG, loot and rolls, auction house, BG queue, flight paths, trainers,
  talents. Client steps and the core CMSG/SMSG handlers with `path:line` in the pinned core clone
  (`6517af05`); 1.60-specific behaviour is called out (two talent storages, classic AH runtimes, etc.).
