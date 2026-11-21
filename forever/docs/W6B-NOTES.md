# W6b: Classic Gear Generation (`feat/w6b-gear`)

## What was built

1. **Pure Policy (`ai/playerbot/policy/item/GearSetPolicy.h`, `ai/playerbot/policy/item/GearReplacementsData.h`)**:
   - `IsClassicItemCandidate`: requires `RequiredExpansion == 0`, `itemId < 25000` (or explicit allow-list `IsAllowedForeverCustomItem`, empty in v1), `quality <= 4` (Epic), `RandomBonusListTemplateId == 0`, and `!IsTrapItem(itemId)` (traps/missing/spell-not-item IDs baked from `forever/data/waterys/forever-item-traps.csv` and `items-missing-classified.csv`: `12536, 16922, 17802, 18803, 21927, 22053, 22054`).
   - `TargetMaxQualityForLevel`: Uncommon (`2`) below level 30, Rare (`3`) from 30+, Epic (`4`) only when explicitly requested via `quality=epic` / `quality=4`.
   - `LevelBracketForLevel`: `requiredLevel <= level`, item level in `[max(1, level - 12), level + 6]` for `< 60`, `[50, 100]` for `60`.
   - `ClassCanWearArmorSubClass` / `ClassCanWearWeaponForSlot` / `CandidateFitsSlotPolicy`: enforces class/spec/level armor proficiency (e.g. no plate below 40), two-hand vs off-hand mutual exclusion, dual-wield off-hand gating, ranged/wand/relic rules, and unique-slot deduplication.
   - `RankCandidatesForSlot`: orders by `(W6a score DESC, quality DESC, itemLevel DESC, itemId ASC)`.
   - `PreferredReplacementsFor`: baked from `forever/data/waterys/gear-replacements.csv` (490 `(class, spec, slot)` entries normalized to `ai_playerbot_weightscales` spec names) used at level 60 when the item fits and passes `CanEquipNewItem`, falling back to generated candidates otherwise.

2. **Host Index & Equip (`forever/src/host/gear/TbGearGen.h`, `TbGearGen.cpp`, `TbScripts.cpp`)**:
   - `InitGearIndex()`: called once in `tortoisebots_world::OnWorldInitialized()` under `TORTOISEBOTS_SLICE1`; scans `ObjectMgr::instance()->GetItemTemplateStore()` in one pass and logs count and elapsed ms.
   - `SetGearForTarget`: scores via `sRandomItemMgr.CalculateStatWeight`, checks `Player::CanEquipNewItem`, equips empty slots via `EquipNewItem`, and replaces occupied slots via `StoreNewItemInBestSlots` + `SwapItem` (refusing with `code=bags-full` if bags are full; never destroying worn gear). Slots with no fit report `code=no-item`.
   - `AuditGearIndex`: counts candidates per slot for a given level band.

3. **Commands (`forever/src/host/TbPartyState.cpp`)**:
   - `.tb gear <who> set [spec] [level] [quality] [#token]`: targets a host bot, `party [leader]`, or the GM sender's own character (`ResolveAddress(..., allowSender=true)`). Grammar v2 replies list `slot`, `item`, `ilvl`, `score`, `status`, and `code` on refusal.
   - `.tb gear audit [level] [#token]`: lists `level`, `slot`, `candidates`, and `total` indexed items.

4. **Slice, Wiring & Unported**:
   - Added `forever/SLICE-W6B.txt` (`= SLICE-W1.txt`), `forever/tools/test_gear_set_policy.cpp`, and `forever/tools/test_w6b_wiring.py`.
   - Updated `ai/playerbot/UnportedFeatures.h` and `forever/docs/UNPORTED.md` (`RandomItems`, `ItemSlotOfPrototype`) to note W6b's gear generation.

## Gate results

- **Pure policy test (`test_gear_set_policy.cpp`)**: `PASS: test_gear_set_policy (7 checks, 0 failed)` (exit 0)
- **Wiring test (`python3 -m unittest forever/tools/test_w6b_wiring.py`)**: `Ran 5 tests in 0.000s` -> `OK` (exit 0)
- **Wiring mutation (`python3 forever/tools/test_w6b_wiring.py --mutate`)**: `AssertionError: IsClassicItemCandidate missing requiredExpansion != 0 filter` (`mutate_exit=1`)
- **Context & registry**: `SliceAiObjectContext.cpp is up to date` / `summary: (a)=0  (b)=228 keys to register or stub later` (exit 0)
- **Census (`tbf-census.sh ... SLICE-W6B.txt`)**: `files 286  compile clean 0  with errors 286  error places 0 (in 0 files)  includes of a header ForeverCore does not have 0` (exit 0)
- **Module compile ON (`tbf-build.sh ... tortoisebots`)**: `[207/207] Linking CXX static library src/server/modules/TortoiseBots/libtortoisebots.a` / `BUILD OK tortoisebots ON 3e98a1cc6` (exit 0)
- **Worldserver link ON (`tbf-build.sh ... worldserver`)**: `[335/335] Linking CXX executable bin/RelWithDebInfo/bin/worldserver` / `BUILD OK worldserver ON 3e98a1cc6` (exit 0)
- **Worldserver link OFF (`tbf-build.sh ... worldserver OFF`)**: `BUILD OK worldserver OFF 3e98a1cc6` (exit 0)

## Live test steps (for lead)

1. Boot `worldserver` and verify startup log: `TortoiseBots: gear index built (<N> classic items in <ms> ms)`.
2. Run `.tb gear audit #a1` -> expect candidate counts per slot for levels 10, 30, 60 and `gear-end verb=audit total=<N> #a1`.
3. Run `.tb gear <bot> set #s1` on bots across the 9 classes at levels 10, 30, and 60 -> expect `status=equipped|kept` (or `code=no-item` where no item exists in bracket), `ilvl` inside bracket, no item `>= 25000`, and no plate below 40.
4. Run `.tb gear <gm_self> set frost 60 quality=epic #s2` -> expect GM's own character equipped with classic frost mage epics (preferring `gear-replacements.csv` items).
5. Fill a target's bags completely and run `.tb gear <bot> set #s3` on occupied slots -> expect `status=refused code=bags-full` (worn items preserved).
6. On option-OFF build, run `.tb gear <bot> set #s4` -> expect `error code=engine-not-built`.
