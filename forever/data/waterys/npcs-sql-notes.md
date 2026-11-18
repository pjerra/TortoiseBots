# TortoiseBots NPCs → Forever: SQL draft notes

Draft: `tortoisebots-npcs-forever.sql` (same directory). **Not applied.** No server was started; every
row below was generated from the files named here, and every id claim was checked against the extracted
TDB dumps.

`client_build: "client DB2 70938 (2026-11-17); world/hotfixes TDB 2026-10-14"`

Scope: the `<Mercenary Hire>` recruiters (68 templates, 67 spawns), the credit-only creatures the
hire/world-buff migrations create (95100-95121), and their greeting texts. **There is no separate
"world buff vendor" NPC.** World buffs are served by the six capital recruiters
(`runtime/policy/pvp/WorldBuffPolicy.h` + `runtime/WorldBuffService`), so no vendor template, spawn,
gossip menu or text is created here.

---

## 1. Source rows (TortoiseBots world migrations, final state)

The assignment named `20260918120000_world.sql`, but that file was superseded in-place by later
migrations. The draft reproduces the **current** TB state:

| File | Lines | Rows | What |
|---|---|---|---|
| `20260918120000_world.sql` | 21-89 / 97-185 | 68 creature_template + 67 creature | original recruiter release; **superseded**, used only to cross-check |
| `20260925100000_world.sql` | 20-86 | 67 UPDATEs | opposite-gender display + new name per recruiter (95046 keeps its display) |
| `20260928100000_world.sql` | 24-92 / 102-190 / 193-261 | 68 creature_template + 67 creature + 67 UPDATEs | **current base**: explicit spawn guids 4000000-4000066, inline re-apply of the 20260925 updates |
| `20261005090000_world.sql` | 28 / 48-50 | 6-entry npc_flags UPDATE + 2 creature_template | `npc_flags = 3` on the six capital recruiters; world-buff credit creatures 95100/95101 |
| `20261005120000_world.sql` | 17-18 | 2 UPDATEs | rename 95100 → "Onyxia or Nefarian", 95101 → "Enemy player in Silithus" |
| `20261012220000_world.sql` | 8-9 | 1 UPDATE | spawn 4000033 (95032 Kerra) `movement_type` 2 → 0 (no waypoints existed) |
| `20261013090000_world.sql` | 35-47 | 12 creature_template | hire-unlock credit creatures 95110-95121 |
| `20261026120000_world.sql` | 6-22 | 136 npc_text + 136 broadcast_text (via `INSERT ... SELECT`) | greetings; regenerated as literals here because Forever's `broadcast_text` lives in the hotfixes DB and has a different schema |

Row count audit of the regenerated data: 68+14 creature_template, 82 creature_template_model,
68 creature_classic_level, 67 creature, 136 npc_text, 136 broadcast_text.

---

## 2. Renumbering (frozen in the draft)

| TB range | Use | New Forever range |
|---|---|---|
| 95000-95067 | recruiters (template + spawn `id`) | **1000000-1000067** (`new = 1000000 + (old - 95000)`) |
| 95100, 95101 | world-buff kill credits (never spawned) | **1000070, 1000071** |
| 95110-95121 | hire-unlock credits (never spawned) | **1000080-1000091** |
| npc_text / broadcast_text 95000-95067 | PvE-only greeting (no `AiPlayerbot.HireAllowPvP`) | **1000000-1000067** |
| npc_text / broadcast_text 95100-95167 | greeting variant without the red PvE line | **1000100-1000167** |

Text ids stay `entry` and `entry+100` because `host/HireRecruiterScript.cpp` `GossipText()` returns the
creature entry, and the greeting must be reachable by that id. Keeping the old 95xxx text ids while the
creature entries move to 1000xxx would silently fall back to `DEFAULT_GOSSIP_MESSAGE`.

The credits keep the same relative layout (70/71, 80-91) so the code constants stay readable.

### Free-id check (all from `TDB_full_world_forever_2026_10_14.sql` / `..._hotfixes_...sql`)

Parsed every `INSERT` for each table (quoted strings with `''` and backslash escapes handled) and counted
ids:

| Table / DB | Checked range | Rows found | Whole-table facts |
|---|---|---|---|
| world `creature_template` | 1000000-1000200 | **0** | 230,319 rows, max id 8,102,000 |
| world `creature_template_model` | 1000000-1000200 | **0** | 381,584 rows |
| world `creature_classic_level` | 1000000-1000200 | **0** | 11,025 rows |
| world `creature_classic_stats` | 1000000-1000200 | **0** | 10,380 rows (no rows added by this draft) |
| world `creature` (`id` and `guid`) | ids 1000000-1000200; guids 4000000-4000066 | **0 / 0** | 79,422 rows, guid min 20,000,001, max 26,209,144 |
| world `npc_text` | 95000-95167 and 1000000-1000200 | **0 / 0** | 15,774 rows, max id 20,253,849 |
| **hotfixes** `broadcast_text` | 95000-95167 | **164 of 168 taken** | 237,844 rows, max id 328,288 |
| **hotfixes** `broadcast_text` | 1000000-1000167 | **0** | — |

**`id-ranges.md` is wrong about broadcast_text.** It says "npc_text + broadcast_text 95000-95167 free
(0 rows in Forever world)". `broadcast_text` is not in the world DB at all; in the hotfixes DB 164 of
those 168 ids already exist (vanilla NPC lines, e.g. 95000 "You can't stop all of us."). That is the main
reason the greeting ids moved to 1000000+. (`id-ranges.md` is shared; not edited here.)

Also verified:
- all 68 final display ids and the invisible display **11686** exist in world `creature_model_info`;
- the 23 faction ids used by the recruiters (12, 29, 35, 55, 64, 68, 69, 80, 83, 84, 104, 120, 371, 474,
  694, 794, 855, 894, 994, 1494, 1682, 1689, 1693) are all in use by existing `creature_template` rows.
  The world DB has no `faction_template` table (faction data is client DB2), so real friendliness could
  not be read here — see §9.

---

## 3. `creature_template`: TB (80 cols, CMaNGOS) → Forever (42 cols, Trinity/1.60)

Written with an explicit column list; nothing TB-only is referenced.

### Mapped 1:1 (same meaning, same value domain confirmed in the Forever core)

| TB column | Forever column | Notes |
|---|---|---|
| `entry` | `entry` | renumbered (§2) |
| `name` | `name` | final name after 20260925/20260928 updates |
| `subname` | `subname` | `Mercenary Hire` / `''` for credits |
| `faction` | `faction` | copied (validity checked by "already used by TDB rows") |
| `npc_flags` | `npcflag` | TB 1 = gossip; 3 = gossip+questgiver. Forever `UNIT_NPC_FLAG_GOSSIP=0x1`, `UNIT_NPC_FLAG_QUESTGIVER=0x2` (`src/server/game/Entities/Unit/UnitDefines.h:320-321` of the pinned core) → identical |
| `speed_walk` / `speed_run` / `scale` | same | 1 / 1.14286 / 1 |
| `rank` | `Classification` | 0 = normal in both enums; all rows are 0 |
| `dmg_school` | `dmgschool` | 0 |
| `base_attack_time` / `ranged_attack_time` | `BaseAttackTime` / `RangeAttackTime` | 2000 |
| `unit_class` | `unit_class` | 1 (warrior) |
| `unit_flags` | `unit_flags` | 0 |
| `beast_family` | `family` | 0 |
| `trainer_class` | `trainer_class` | 0 |
| `type` | `type` | 7 = humanoid |
| `movement_type` | `MovementType` | 0 on every template; per-spawn movement is in `creature` (§6) |
| `racial_leader` | `RacialLeader` | 0 |
| `xp_multiplier` | `ExperienceModifier` | 1 |
| `regeneration` | `RegenHealth` | TB 3 (health+power regen) → Forever boolean 1 |
| `flags_extra` | `flags_extra` | 0; `CREATURE_FLAG_EXTRA_*` bit meanings match, value 0 either way |
| `script_name` | `ScriptName` | `tortoise_mercenary_hire` (see §7) |

### Guessed / defaulted (no TB value carried)

| Forever column | Value | Why |
|---|---|---|
| `KillCredit1/2` | 0 | not used by TB |
| `femaleName` | `''` | TB has no female name |
| `TitleAlt`, `IconName`, `StringId` | NULL | not used |
| `RequiredExpansion`, `VignetteID`, `VehicleId`, `movementId`, `WidgetSetID`, `WidgetSetUnitConditionID` | 0 | Trinity-only fields |
| `BaseVariance`, `RangeVariance` | 1 | core default |
| `unit_flags2`, `unit_flags3` | 0 | TB `dynamic_flags` is 0 on every row (see dropped) |
| `AIName` | `''` | AI comes from the CreatureScript, not SmartAI |
| `CreatureImmunitiesId` | 0 | TB immunity masks are 0 |
| `VerifiedBuild` | 0 | custom row (same convention as the core's hotfix loader) |

### Dropped (no Forever column at all — this is the important loss list)

`display_id1..4` and `mount_display_id` → **not columns on Forever `creature_template`**; see §4.
`gossip_menu_id` → replaced by the `gossip_menu` table + script gossip. The module builds its own menu,
so no `gossip_menu`/`gossip_menu_option` rows are created (§8).
`level_min/level_max` → Forever levels come from `creature_classic_level`; carried over in that table (§5).
`health_min/max`, `mana_min/max`, `armor`, `dmg_min/max`, `attack_power`, `dmg_multiplier`,
`ranged_*`, resistances → derived by the 1.60 core (`Creature::UpdateLevelDependantStats`, classic
stat tables + config). **TB's handcrafted numbers (1002 health, 400 armor, 1-1 damage) are not
reproducible 1:1 and were dropped.** At level 30 the Forever VMaNGOS table gives class-1 base health 955,
so live health will differ from TB's 1002; adding a `creature_classic_stats` health multiplier to fake
1002 would be invented data, so none was added (and `Classic.ForeverCreatureHealth` may change the base
anyway).
`detection_range` (20), `call_for_help_range` (5), `leash_range` (0) → no columns; core defaults.
`dynamic_flags` (0) → Trinity split this into `unit_flags2/3`; value 0 makes the split moot.
`type_flags` (0) → no column in this schema.
`inhabit_type` (3 = ground), `civilian` (1), `phase_quest_id` (0), `loot_id`, `pickpocket_loot_id`,
`skinning_loot_id`, `gold_min/max`, `equipment_id`, `trainer_*`, `vendor_id`, `spell_id1..4`,
`spell_list_id`, `pet_spell_list_id`, `spawn_spell_id`, `auras` (NULL), `mechanic/school_immune_mask`,
`immunity_flags` → no equivalent columns; all zero/NULL except `civilian` and `inhabit_type`.
**`civilian=1` is a real loss:** Forever has no civilian column, so the recruiters are not marked
civilian. TB's `unit_flags` were 0 too, so they were probably not PvP-safe anyway — verify live (§9).

---

## 4. Display / model decision

Forever has **no display columns on `creature_template`**. Displays live in
`creature_template_model (CreatureID, Idx, CreatureDisplayID, DisplayScale, Probability, VerifiedBuild)`,
loaded by `ObjectMgr::LoadCreatureTemplateModels`; `creature.modelid` is a per-spawn override.

Decision: one `creature_template_model` row per template, `Idx=0`, `CreatureDisplayID = TB display_id1`
(`display_id2..4` are 0), `DisplayScale=1`, `Probability=0` (the core ignores probability when a
template has a single model, `Creature.cpp:120`). Spawns use `creature.modelid = 0` ("use template").
The 14 credit-only creatures get display **11686** (invisible), exactly like TDB's own credit rows
(e.g. 19618 `Warpmaster Lyssendra Credit`). 82 model rows total.

---

## 5. Level: `creature_classic_level`

TB level 30/30 is expressible: 68 rows `(entry, 30, 30)` in `creature_classic_level`, which
`Creature::ApplyLevelScaling` consults per entry (spawn-level table `creature_classic_level_spawn` is
empty/absent in TDB, so it falls through to the entry table). Credit creatures get no level row: they
are never spawned and no stat is ever rolled for them.

---

## 6. `creature` spawns: TB (18 cols, CMaNGOS) → Forever (28 cols)

| TB | Forever | Notes |
|---|---|---|
| `guid` | `guid` | kept as TB's 4000000-4000066 (see below) |
| `id` | `id` | renumbered |
| `id2/id3/id4` | — | CMaNGOS alternate entries; 0 |
| `map` | `map` | copied (rows use maps 0/1/42/451) |
| `position_x/y/z`, `orientation` | same | copied verbatim |
| `spawntimesecsmin`, `spawntimesecsmax` | `spawntimesecs` | Forever has one value → used **max** (min==max on 65 rows; 95041 300/360 and 95062 260/400 collapse to 360/400) |
| `wander_distance` | `wander_distance` | copied (only 95004 has 1.0) |
| `health_percent`, `mana_percent` | `curHealthPct` | 100; mana percent has no column and is always 0/100 in TB |
| `movement_type` | `MovementType` | copied after the 20261012 fix (all 0 except 95004 = 1) |
| `spawn_flags`, `visibility_mod` | — | no columns; both 0 |
| — | `zoneId`, `areaId` | 0,0 (core resolves from position; TDB rows do the same) |
| — | `spawnDifficulties` | `'0'` (all maps, matching every TDB row) |
| — | `phaseUseFlags`, `PhaseId`, `PhaseGroup`, `terrainSwapMap` | 0,0,0,-1 defaults |
| — | `modelid` | 0 (template model) |
| — | `equipment_id` | 0 |
| — | `currentwaypoint` | 0 |
| — | `npcflag`, `unit_flags`, `unit_flags2`, `unit_flags3`, `ScriptName`, `StringId` | NULL/NULL/NULL/NULL/''/NULL — per-spawn overrides unused |
| — | `VerifiedBuild` | 0 |

67 spawn rows across 65 recruiters (1000029 and 1000062 twice). **No spawn for 1000042, 1000066,
1000067** — same as TB (Hern/95042 had no innkeeper spawn; 95066/95067 have no base spawn either). The
template still exists for all three, so an innkeeper can be added later without new templates.

### Guid strategy

TB's explicit guids 4000000-4000066 are kept. Justification: the Forever world DB's lowest creature guid
is 20,000,001 and nothing occupies 4000000-4000066 (`creature` is the only table keyed by it here; no
`creature_addon`/`game_event_creature`/`pool_members` rows were created). Keeping TB's guids makes the
draft diffable against TB comments. Idempotency: `DELETE ... WHERE guid BETWEEN 4000000 AND 4000066 OR
id BETWEEN 1000000 AND 1000091` removes both our guids and any stray spawn carrying a renumbered entry.

---

## 7. `ScriptName` / script registration assumption

`ScriptName = 'tortoise_mercenary_hire'` on all 68 recruiters, `''` on the credits. This is kept **only
because the port registers it**: `host/HireRecruiterAdapter.cpp:45` constructs
`CreatureScript("tortoise_mercenary_hire")`, and `TortoiseBots.cmake:156-157` compiles
`HireRecruiterScript.cpp` + `HireRecruiterAdapter.cpp` into the module. If the module is built without
the `host/` adapters, the script name is dead and the NPCs must not carry it.

---

## 8. Text / broadcast handling

- `npc_text` lives in the **world** DB: 136 rows, `(ID, Probability0=1, BroadcastTextID0=ID)`; other seven
  slots stay 0. One text per greeting, same text for male and female.
- `broadcast_text` lives in the **hotfixes** DB (Forever's world DB has no such table): Section B of the
  SQL file. Columns written: `ID`, `Text`, `Text1`, `LanguageID=0`, `VerifiedBuild=0`.
  `VerifiedBuild=0` is the Trinity "custom hotfix row" marker used by `(VerifiedBuild > 0) = ?`;
  if the port's loader does not pick it up, re-insert with `VerifiedBuild = 70938` (client build) —
  in the verify list (§9).
- Text is materialised as literals (the TB migration generated it with `INSERT ... SELECT
  SUBSTRING(name, 11)`; a runtime SELECT across renumbered entries would work but is harder to audit).
  Both variants are present: `entry` (PvE-only line, `|cff9b0000...|r`) and `entry+100` (no red line,
  used when `AiPlayerbot.HireAllowPvP = 1`), matching `GossipText()`.
- **No `gossip_menu` / `gossip_menu_option` / `points_of_interest` rows.** The module builds the menu in
  code (senders 501-512) and calls `SendGossipMenu(GossipText(...), guid)`; a DB menu would collide with
  those sender/action ids. `gossip_menu_id` has no column on Forever's `creature_template` anyway.
- Credit creatures have no gossip/text rows (never spawned, never right-clicked).

---

## 9. Must be verified live / code dependencies of the port

1. **Renumber every hardcoded TB entry range in the module** (SQL alone is not enough):
   - `runtime/policy/pvp/WorldBuffPolicy.h:59-60` credits 95100/95101 → 1000070/1000071;
     `:79-84` capital list 95017/95006/95012/95025/95018/95019 → 1000017/1000006/1000012/1000025/
     1000018/1000019; `:90-91` and `:109-111` recruiter range 95000-95067 → 1000000-1000067.
   - `runtime/policy/hire/HireQuestPolicy.h:26-31` recruiter range.
   - `runtime/HireUnlockPolicy.h:261-272` hire-unlock credits 95110-95121 → 1000080-1000091.
   - `host/HireRecruiterScript.cpp:172` `GossipText()` range `95000..95067` → `1000000..1000067`
     (otherwise everyone gets `DEFAULT_GOSSIP_MESSAGE`, and `entry+100` never resolves).
   - `host/HireRecruiterScript.h:11` comment only.
2. **Gossip rendering**: after apply, right-click each recruiter and confirm the greeting appears (this
   proves world `npc_text` + hotfixes `broadcast_text` + §9.1 line up) in both `HireAllowPvP` states.
   Also confirm the hire wizard and the world-buff/contracts branches show on the six capital NPCs.
3. **Faction friendliness**: faction ids are valid (used by existing TDB rows) but not verified against
   the client's FactionTemplate DB2 (no faction table in the world DB). Check a recruiter per city is
   friendly and not attackable.
4. **Civilian/attackability**: TB's `civilian=1` was dropped (no Forever column). Watch a capital
   recruiter in a contested city.
5. **Placement**: positions come from TB's spawns "1.5 yards to the side" of TB innkeepers. Forever's
   innkeeper spawn rows are not guaranteed identical (Hern/95042 has no spawn in either DB, 95066/95067
   also lack one). Confirm visually; the three template-only entries can be spawned later.
6. **Quest givers**: `npcflag=3` on the six capital recruiters assumes the unlock quests exist. The quest
   rows (90000-90013, 90100-90106) and their `creature_questrelation`/`creature_involvedrelation` rows
   are **out of scope here** (quest ids 90100-90103 are blocked per `id-ranges.md` and belong to the quest
   round). Without them the "!" is simply absent — no error.
7. **broadcast_text loading**: confirm the hotfixes rows with `VerifiedBuild=0` are served; fall back to
   `70938` if not.
8. **Random-movement spawn 1000004** (wander 1.0) and the double spawns (1000029, 1000062) do not wander
   into walls / duplicate-stack.
9. **Conditions**: none are created. If TB later adds `conditions` for these NPCs/texts, they follow the
   same renumbering and need their own check.

---

## 10. Rows intentionally not produced

| Thing | Why |
|---|---|
| world buff "vendor" NPC | does not exist in TB; buffs are a recruiter gossip branch |
| `gossip_menu`, `gossip_menu_option`, `points_of_interest` | script gossip only; no BTs/conditions, so nothing to key |
| `creature_addon`, `creature_template_addon` | TB mount/emote/auras are 0/NULL |
| `creature_classic_stats` | would only fake TB's handcrafted health/armor/damage numbers |
| `quest_template`, `creature_questrelation`, `creature_involvedrelation` | quest round; ids partly blocked |
| `locales_*` | none exist on this core (TB migrations also say "English text only") |
| `creature_equip_template` | TB `equipment_id` 0 |
