# C. W7: professions (pick, gather, craft for self, skill-up)

**Ordered half can start now** on your W2 trainer seam (`tbhost::town::TeachTrainerSpell`, PR #93) and slice 9's chest
opening. **The autonomous half waits for our W4 round C** (travel actions): walking a route of nodes, going to a
profession trainer in another town. Also wants W6a (is the craft an upgrade).

## Done looks like

A bot picks two professions that suit it, learns them from a trainer and pays, gathers herb, ore and skinning nodes it
can work on its way, crafts useful things for itself from its own bags, and rises in skill through the core's own
spell, loot and skill code.

**Live acceptance** (world-life plan, W7 row): a herbalist and a miner bot gain 30 skill points each on their route
(skill before/after, nodes worked, nodes skipped with reason); a tailor bot crafts Linen Bags from its own cloth and
wears one. Log `[W7] learn|gather|craft|skillup bot= skill= before= after=`.
**From a client**: no client round yet; the client's vendor and trainer rounds can stand next to the bot at the
profession trainer and see the same trainer list.

## Scope and legacy files

Plan scope: "professions pick, gather nodes, craft for self, skill-up", 3 worker-days, biggest risk "recipe/skill data
from DB2". Area: 29 files, 4,252 lines, 66 places. Legacy: `commands/Professions.cpp` 371 / 28,
`strategy/values/CraftValues.cpp` 238 / 10 + `CraftValue.h`, `strategy/actions/SetCraftAction.cpp` 156,
`RevealGatheringItemAction.cpp` 82, `GatherSpotPolicy.h` 52, `policy/loot/SkinningLootPolicy.h` 67, the gathering
helpers in `LootObjectStack.cpp` (`CorpseSkinnableByCompat`, `GameObjectLootAllowedCompat`). Fishing
(`FishAction.cpp` 725, `FishingSpotPolicy.h`) is out. What the Turtle commands answered:
`forever/data/waterys/tbm-transcripts/professions.txt`.

## ForeverCore APIs (25fc7ca)

- Skills: `Player::GetSkillValue` (`Player.h:2477`), `GetPureMaxSkillValue` (`:2476`), `GetMaxSkillValue` (`:2475`),
  `HasSkill` (`:2498`), `GetSkillStep` (`:2482`), free primary slots `GetFreePrimaryProfessionPoints()` (`:2131`).
  `SetSkill(id, step, value, max)` (`:2474`) is for test helpers only.
- Skill-up is the core's, inside the spell: craft `UpdateCraftSkill` (`SpellEffects.cpp:1486`, `:1516`), gather
  `UpdateGatherSkill` in the open-lock effect (`:1800`, `:1807`), skinning (`:4082`).
- Recipes: `sDB2Manager.GetSkillLineAbilitiesBySkill(skill)` (`DB2Stores.h:554`), `sSkillLineAbilityStore` (`:234`);
  `GetSkillRaceClassInfo` (`:555`). Locks: `sLockStore` (`:191`).
- Learn: profession ranks are trainer spells: your `TeachTrainerSpell` (`Trainer::TeachSpell`); trainer truth is
  `creature_trainer` Type 2 (tradeskill); list in `service-points/profession-trainers.csv`.
- Gather: vanilla herb and ore nodes are type 3 chests with a skill lock. Open with the typed
  `WorldPackets::Spells::CastSpell` into `HandleCastSpellOpcode` (`WorldSession.h:1653`) or the slice 9 chest path
  (typed `GameObjUse` into `HandleGameObjectUseOpcode`, `:1435`), then loot through the core
  (`GameObject::GetLootForPlayer`, `GameObject.h:358`; `HandleAutostoreLootItemOpcode`, `WorldSession.h:1386`).
  Node state: `GetGoState()` (`GameObject.h:298`), `GetGOInfo()` (`:217`).
- Craft: cast the recipe spell through `HandleCastSpellOpcode`; reagents and tools are checked by the spell itself.

## Files to touch (suggested)

- Pure `policy/profession/ProfessionPickPolicy.h` (which two, from class and what the bot can use),
  `GatherNodePolicy.h` (work this node or skip: lock skill vs bot skill, grey/orange/yellow chance, danger nearby),
  `CraftForSelfPolicy.h` (craft only what the bot uses: bags, bandages, a W6a upgrade; reagents in bags) + tests.
- Host `forever/src/host/TbHostProfession.{h,cpp}` (`tbhost::prof`: Skill, Learn, OpenNode, Craft), the slice 9
  chest path reused, not copied.
- Compat pair `TbCompat_WL7.h` + `ai/cmangos-compat-wl7.h` for the legacy craft values; census `forever/SLICE-W7.txt`.
- Ordered verbs appended to your `.tb order`: `learn-prof <skill>`, `gather`, `craft <spell> [n]`; `.tb state` block
  `prof=<skill>:<value>/<max>,...`; free-kit `gather` behind `TortoiseBots.World.Professions = 0`; travel purpose
  `gather` and `prof-trainer` through W4's purpose gate; `W7_KEYS`.

## UNPORTED rows removed

`skills` (only the reads: F names the skill lines; the 1.12 `SetSkill` write stays out of the real path), the herb and
ore part of `loot: gathering and skinning` (rename it to what is left: pickpocketing, fishing, disenchant), and
skinning if you do it. `camp, rest bonus, water breathing` keeps its fishing-zone part.

## Rules that bite here

- Blizzlike: professions come from a trainer and cost money; skill only rises through the core's spell effects; no
  `SetSkill` in the real path.
- A node or a craft that did not happen is logged with the reason (lock skill, node already looted, missing reagent,
  bags full); never counted as done unless the loot reached the bags or the crafted item appeared.
- Keep node guids, not `GameObject*`; re-find each tick; a node another player took is skipped.
- No SQL: skills, recipes and locks are DB2 stores; trainer lists are your W2 cache.

## Tests (failing case first)

- **Failing case:** `GatherNodePolicy` is offered a herb node whose lock needs skill 70 for a bot with skill 50; the
  legacy path tries it and fails silently. It must skip with `code=skill`.
- Pick: a free bot without a profession and two free primary points picks a gathering pair; a bot with none free
  picks nothing.
- Craft: a Linen Bag is crafted only with the cloth in bags and a free bag slot to wear it; a recipe with a missing
  tool is skipped.
- Wiring: no `SetSkill` call outside the test helper; every open and craft goes through `tbhost::prof`; `--mutate`
  removes the lock-skill check and must FAIL.

## Decisions already made

- Gathering, skinning and crafting through the core's own spells and loot (slice 9's object path), no packets built
  for nothing.
- Fishing, enchanting orders and guild crafting are out (guild crafting is item E).

## Open for you to settle

1. **Which pairs.** Recommendation: free bots take a gathering pair (herbalism + mining, or skinning + one) so W8 has
   something to sell; tailoring and first aid only when the class wears cloth or needs bandages.
2. **Detours.** Recommendation: before W4, only nodes within the W2 walking rule (navmesh path at most 300 yd from the
   grind area); after W4, a node on the route is worked when the detour is under 60 yd.
3. **Selling crafts and mats** belongs to W8; until W8, surplus mats stay in bags or go to the vendor by W2's sell rule
   (greys only, so they stay).
