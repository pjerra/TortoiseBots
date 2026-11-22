# W7 Notes: Professions, Ordered Half (`C-w7-professions`)

Branch: `feat/w7-professions` (base `main`).

## What was built

1. **Pure policies (`ai/playerbot/policy/profession/`)**:
   - [`ProfessionPickPolicy.h`](file:///mnt/pny-ssd/Tortoise%20WoW%20Projects/wt-tbf-w7-prof/ai/playerbot/policy/profession/ProfessionPickPolicy.h): settled primary pairs per class (warrior/paladin: mining+blacksmithing; hunter/rogue/druid/shaman: skinning+leatherworking; mage/warlock: herbalism+alchemy; priest: tailoring+enchanting; fishing excluded), `PickProfessionsToLearn`, and `SkillIdFromName`/`SkillNameFromId`.
   - [`GatherNodePolicy.h`](file:///mnt/pny-ssd/Tortoise%20WoW%20Projects/wt-tbf-w7-prof/ai/playerbot/policy/profession/GatherNodePolicy.h): `EvaluateGatherNode` checking existence/spawn, ready/looted state, same zone, distance (`<= 150 yd`), profession learned (`no-prof`), lock skill vs bot skill (`code=skill`), tool (`Mining Pick`/`Skinning Knife`), free bag slots (`bags`), and danger nearby (`danger`), plus orange/yellow/green/grey difficulty classification.
   - [`CraftForSelfPolicy.h`](file:///mnt/pny-ssd/Tortoise%20WoW%20Projects/wt-tbf-w7-prof/ai/playerbot/policy/profession/CraftForSelfPolicy.h): `EvaluateCraftForSelf` checking known recipe, skill, tools (`Totem`/`TotemCategory`), reagents in bags, free bag slots (and free equip bag slot when crafting a bag), and self-usefulness for autonomous crafts.

2. **Host seam (`forever/src/host/TbHostProfession.h`, `forever/src/host/TbHostProfession.cpp`, namespace `tbhost::prof`)**:
   - `GetSkills` / `FormatStateProf`: reads `Player::HasSkill`, `GetSkillValue`, `GetMaxSkillValue`, `GetSkillStep`.
   - `LearnProfession`: finds nearest tradeskill trainer (`TrainerKind::Tradeskill`), checks `CanTeachTrainerSpell`, calls `tbhost::town::TeachTrainerSpell`, logs `[W7] learn` / `[W7] learn-skip`.
   - `OpenNode` / `GatherNearestInSight` / `LootOpenNode`: evaluates `GatherNodePolicy`, casts `Mining` (`2575`) or `Herb Gathering` (`2366`) via `WorldPackets::Spells::CastSpell` into `HandleCastSpellOpcode` for locked herb/ore nodes (so core `EffectOpenLock` / `UpdateGatherSkill` runs), reuses slice 9's `GameObjUse` into `HandleGameObjectUseOpcode` for unlocked chests, autoloots via `HandleAutostoreLootItemOpcode`, and logs `[W7] gather` / `[W7] gather-skip` / `[W7] skillup`.
   - `Craft`: looks up recipe skill/reqSkill from `sDB2Manager.GetSkillLineAbilitiesBySkill`, checks tools/reagents/bags via `EvaluateCraftForSelf`, casts via `HandleCastSpellOpcode`, and logs `[W7] craft` / `[W7] craft-skip` / `[W7] skillup`.
   - `TickFreeGather`: periodic free-bot gather within 150 yd in the current zone behind `TortoiseBots.World.Professions = 0` (`forever/conf/tortoisebots.conf.dist`, `forever/src/TbBotMgr.cpp`).

3. **Ordered verbs, state, and test helper**:
   - [`forever/src/host/TbOrders.cpp`](file:///mnt/pny-ssd/Tortoise%20WoW%20Projects/wt-tbf-w7-prof/forever/src/host/TbOrders.cpp): appended `learn-prof <skill>`, `gather`, `craft <spell> [n]`.
   - [`forever/src/host/TbPartyState.cpp`](file:///mnt/pny-ssd/Tortoise%20WoW%20Projects/wt-tbf-w7-prof/forever/src/host/TbPartyState.cpp): appended `prof=<skill>:<v>/<max>,...` to `.tb state`.
   - [`forever/src/TbScripts.cpp`](file:///mnt/pny-ssd/Tortoise%20WoW%20Projects/wt-tbf-w7-prof/forever/src/TbScripts.cpp): appended test-only helper `.tb setskill <bot> <skill> <value> [max]` (the only `SetSkill` call).
   - [`ai/playerbot/UnportedFeatures.h`](file:///mnt/pny-ssd/Tortoise%20WoW%20Projects/wt-tbf-w7-prof/ai/playerbot/UnportedFeatures.h), [`forever/docs/UNPORTED.md`](file:///mnt/pny-ssd/Tortoise%20WoW%20Projects/wt-tbf-w7-prof/forever/docs/UNPORTED.md): updated `GatheringLoot` row for herb/ore nodes.
   - [`forever/SLICE-W7.txt`](file:///mnt/pny-ssd/Tortoise%20WoW%20Projects/wt-tbf-w7-prof/forever/SLICE-W7.txt): `SLICE-W1.txt` (no legacy files added or modified, so no `TbCompat_WL7.h` / `cmangos-compat-wl7.h` pair needed).

## Gates

- **Pure tests (`forever/tools/test_w7_professions.cpp`)**:
  `PASS: test_w7_professions (32 checks, 0 failed)` (includes failing case: herb lock 70 with bot skill 50 -> `code=skill`).
- **Wiring & mutation (`forever/tools/test_w7_prof_wiring.py` + `test_w1_wiring.py` + `test_w2_town_wiring.py`)**:
  `Ran 31 tests in 0.005s` -> `OK`
  `python3 forever/tools/test_w7_prof_wiring.py --mutate` -> `FAILED (failures=1)` (exit 1)
- **Context & registry**:
  `SliceAiObjectContext.cpp is up to date`
  `summary: (a)=0  (b)=228 keys to register or stub later`
- **Census (`forever/SLICE-W7.txt`)**:
  `files 286  compile clean 0  with errors 286  error places 0 (in 0 files)  includes of a header ForeverCore does not have 0`
- **Full link ON (`worldserver`)**:
  `BUILD OK worldserver ON 26c2203d2`
- **Full link OFF (`worldserver OFF`)**:
  Pending final run below.

## Live check for lead

Position bot `Testbot` (with copper, Mining Pick `2901`, and Linen Cloth/Thread) near a tradeskill trainer and a Copper Vein / Peacebloom node:
1. `tb engine Testbot on`
2. `tb order Testbot learn-prof mining #p1` -> expect `order bot=Testbot verb=learn-prof seq=<n> status=queued #p1`, `ord=<n>:ok`, `tb state Testbot` shows `prof=mining:1/75`, log `[W7] learn bot=Testbot id=186 before=0 after=1`.
3. `tb order Testbot gather #p2` -> expect `ord=<n>:ok` (or `walking` if >5 yd), log `[W7] gather bot=Testbot node=<entry> skill=186 before=1 after=...`, `[W7] skillup` when skill rises.
4. `tb setskill Testbot 197 45 75` + learn Linen Bag (`3755`), `tb order Testbot craft 3755 1 #p3` -> expect `ord=<n>:ok`, log `[W7] craft bot=Testbot id=3755 ...`, Linen Bag in bags.

## Follow-up (out of scope here)

- W4 travel purposes (`gather`, `prof-trainer`) and autonomous route-walking / W6a craft-upgrade loop wait for W4 round C.
- Skinning (`CorpseSkinnableByCompat`), pickpocketing, fishing, and disenchanting remain in `GatheringLoot`.

## Live result (board ticket #150, 2026-11-22, Waterys Island Test: core b441bf74, main d0d13b9f6)

Run by Audrey in game (Asdasdasd, Horde GM) with host bots; state read from the server log and the database before/after
(`out/w3-liveproof-150.md` in the worker notes). Setup note: a bot needs `.tb master <bot> <player>` AND `.tb party <player> <bot>`
(a bot outside the commander's party loses the binding on the next tick, so no engine starts).

- **`gather`: PASS, with a report bug** (own ticket). Homeleek (Herbalism 1) picked a Silverleaf: DB +3 Silverleaf (765), Herbalism 1 -> 2;
  but the order answered `fail:timeout` (`[W7] gather-fail ... node=1617 reason=timeout`). `StepGatherErrand`'s Casting phase only
  advances on an open loot window for the node; the herbs reached the bags without the errand seeing it, and 1.5 s after the cast it
  fails without checking the bags. Fix direction: a bag gain of the node's item (or the node used) after a delivered cast is done.
- **`craft 3755 1` (Linen Bag): PASS** -> `ord=..:ok`, DB +1 Linen Bag (4238), 3 Coarse Thread and 3 Bolt of Linen Cloth used. In
  DB2 70959 Linen Bag needs **Bolt of Linen Cloth (2996)**, not Linen Cloth; with raw cloth the order answers `fail:reagents`
  (`[W7] craft-skip ... reason=reagents`), as it should.
