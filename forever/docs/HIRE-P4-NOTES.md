# Hire port, package P4: gear tiers and contract unlocks

Issue #298, plan `forever/docs/HIRE-PORT-PLAN.md`. Code: `forever/src/host/hire/TbHireTiers.{h,cpp}` (core/engine
half), `TbHireTiersPure.h` (catalog and math), test `forever/tools/test_hire_tiers_pure.cpp`. Data:
`forever/sql/world/2026_11_25_01_tortoisebots_hire_gear.sql`,
`forever/sql/world/2026_11_25_02_tortoisebots_hire_contracts.sql`,
`forever/sql/characters/2026_11_25_03_tb_hire_unlock_kills.sql`, generator `forever/tools/gen_hire_tiers.py`.
Research (item audit, exclusions, evidence): the lead's scratchpad `reports/p4-tiers-research.md`.

## What it does

* Tier sets, tiers 1-6 (0 = the levelled kit), at level 60, for the 28 recruiter spec keys of
  `runtime/HireSpecPolicy.h`. `EquipTierSet` replaces the slot's item, clears a slot for a row with item 0, drops the
  off hand for a 2H main hand, and leaves the levelled kit's item where the set has no row.
* `HighestUnlockedTier` = `AiPlayerbot.HireGearStartTier` or the highest rewarded contract (per character, or account
  wide with `AiPlayerbot.HireGearUnlockAccountWide`).
* Contracts gossip page for capital recruiters at level 60: one line per open contract counter ("Zul'Gurub bosses
  slain: 3/8, missing: ..."), sub-page when the roster does not fit one line; senders 510 (page), 511 (sub-page,
  action = credit entry), 512 (back). The quest menu on the page is limited to the six contracts.
* Kill credit: `PlayerScript::OnCreatureKill` filters by the catalog, skips the first three Horsemen, credits the
  killer plus every non-bot group member in reward distance, stores the encounter per character and calls
  `KilledMonsterCredit`. Credit survives a retaken quest: the store is loaded at login and replayed onto an open
  quest (`SyncContracts`).

## Decisions the lead must know

1. **Tiers 1-6, not 1-7.** Tier 7 is the Tower of Karazhan raid set: Turtle-only content, no spawns on Forever; 281 of
   its 485 rows already substitute tier-6/5 items, so it would be tier 6 with holes. The ladder ends at Naxxramas.
   `TierName(7)` is empty, `HighestUnlockedTier` never returns 7. Keeping the ladder shape would need an owner call.
2. **No enchants on tier sets.** TB's 84 enchant spell ids are mostly TBC/WotLK ids; only 7 are among the 20 enchant
   spells the armoury sample proves live on Forever (`forever/data/aotc/enchants-60.csv`), and there is no
   name-to-id table for the rest. The merged enchant upkeep (`ai_playerbot_spec_enchant`) already enchants level-60
   bots with Forever-valid spells. `EquipTierSet` applies none; the table has no enchant column.
3. **The shaman "Enhancement (Tank)" menu row has no set** (Turtle-only spec, no Forever tab). A hire that picks it
   falls back to the bot's real spec tab (Enhancement), so it still gets a kit.
4. **Materials dropped, gold kept.** TB's contracts also asked for herbs/ores/scraps; those item ids are not verified
   on Forever, so the six quests are boss objectives + the gold due (50/100/200/300/400/500 g, classic negative
   money: required and deducted at turn-in).
5. **No optional maps.** `AiPlayerbot.HireGearOptionalMaps` is Turtle's auto-credit for custom maps; the Forever
   catalog has no optional maps, so P4 never reads it. The key is still parsed by the shared `PlayerbotAIConfig`.
   `AiPlayerbot.HireGearAllQuestsAvailable` (TB rewrites the quest chain at startup) is not implemented: the chain is
   the shipped default.
6. **Two surfaces P2/P3 must call**: `hire::RegisterScripts()` and `hire::LoadAtStartup()` are already wired in
   `forever/src/TbScripts.cpp`. P3's recruiter root should add the "Mercenary contracts" item for
   `kSenderContractsFirst` and route sender 510-512 to `OnContractsSelect`; P3 should call `hire::SyncContracts(player)`
   from its `OnQuestAccept` (the core has no PlayerScript quest-accept hook). P2 should quote with
   `hire::TierCostMultiplier(tier)` and call `EquipTierSet` after the spec is applied.

## Data notes

* `tortoise_bots_hire_gear` is generated, never hand-edited (`gen_hire_tiers.py --check`). 2,861 rows, 28 spec keys,
  8 classes, 6 tiers, 594 distinct items; 57 rows clear a slot; 5 race-specific rows survive TB's race overrides (4 warrior ranged, 1 warrior protection main hand).
* The P3 NPC draft (`forever/data/waterys/tortoisebots-npcs-forever.sql`) also inserts credit creatures
  1000080-1000091 with the old counter names. This package creates the eight it uses (P3 will not create credit rows);
  if the draft is applied too, both files write the same ids and names for those eight, and the four dropped ones
  (1000083/86/88/91) stay unused.
* Credit creatures 1000080-90 are created here (P3 ships none): invisible display 11686, never spawned, no gossip, no loot.
* Contracts are quests 100050-100055 on all six capital recruiters (1000017, 1000006, 1000012, 1000025, 1000018, 1000019), start and
  turn-in; the chain is PrevQuestID 1->2->...->6, MinLevel 60.

## Things to check when building/running

* Build: slice ON and OFF (this file compiles in both: the OFF half is a stub), plus the Turtle build (nothing under
  `runtime/`, `host/`, `commands/`, `ai/` was changed).
* The contracts page uses `GossipMenu::AddMenuItem(..., sender, action)` and `SendGossipMenu(creature entry, guid)`;
  the title text id is the recruiter entry, which P3's npc_text rows use.
* The kill store writes through the guarded characters helpers (synchronous, one or two statements per boss kill,
  never per tick). A server whose DB user cannot create tables still credits kills for the session; only the
  restore-after-retake path is then lost.
* Not verified at runtime: no server was built for this package. The pure test covers the catalog and math; item
  existence was checked against the client DB2 files available here (build 70399) and `hotfixes.item_sparse`. Two
  rows still point at item 16922 (priest tier 6 legs) that `forever-item-traps.csv` calls missing — confirm on the
  live 70938 data.
