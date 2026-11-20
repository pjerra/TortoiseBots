# W6a Package: `g-weights` (S6 split, I6 policy, D6 SQL, WeightScaleQueryCompat)

## What changed

- **S6 split:** Moved weight-scale loading (`LoadWeightScales`), armor/weapon spec equip rules (`ShouldEquipArmorForSpec`, `CanEquipArmor`, `ShouldEquipWeaponForSpec`, `ShouldEquipWeaponForSlot`, `CanEquipWeapon`), stat/enchant scoring (`CalculateStatWeight`, `CalculateRandomEnchantId`, `CalculateBestRandomEnchantId`, `CalculateEnchantWeight`, `CalculateRandomPropertyWeight`, `ItemStatWeight`, `CalculateSingleStatWeight`), and spec id helpers (`GetPlayerSpecName`, `GetPlayerSpecId`, `GetFallbackSpecId`, `GetSpecName`, `GetSpecIdFor`, `GetSpecStatFamily`) from `ai/playerbot/RandomItemMgr.cpp` into `ai/playerbot/RandomItemWeights.cpp`. Extracted `InitWeightLinks()` so `weightStatLink`, `ItemStatLink`, and `viableSlots` are populated both in `RandomItemMgr::RandomItemMgr()` (Turtle) and `RandomItemMgr::LoadWeightScales()` (ForeverCore + Turtle). Added `ai/playerbot/RandomItemWeights.cpp` to `TortoiseBots.cmake` and `forever/SLICE9.txt`.
- **I6 Classic stat policy:** Added `ai/playerbot/policy/item/ClassicItemStatPolicy.h` (`ai::ClassicItemStatWeightName`) mapping ForeverCore `ItemModType` ids (`12..21`, `31..32`, `37..39`, `41..43`, `45`, `47..48`, `50`, `85..89`, `119..123`) to legacy weightscale stat names and returning `nullptr` for base/unknown/unweighted ids. Wired into `RandomItemMgr::CalculateStatWeight` in `RandomItemWeights.cpp`, routing `splpwr`/`<school>splpwr`, `splheal`, and `atkpwr` into their respective `spellPower`/`spellHeal`/`attackPower` buckets and caster/healer/attacker/tank flags. Added standalone test `tools/test_classic_item_stat_policy.cpp`.
- **D6 SQL:** Added `forever/sql/world/2026_11_20_20_tortoisebots_weightscales.sql` containing the idempotent schema and final state (29 scales, 223 stat weight rows) of the ten Turtle weightscale migrations.
- **Compat & SliceLinkStubs:** Added `forever/src/compat/TbCompat_W6.h` (`// weights` block: `WeightScaleQueryCompat`, `SPELL_AURA_MOD_CRIT_PERCENT`, `EnchantEffect*Compat`, `RandomProperty*Compat`) and its Turtle twin `ai/cmangos-compat-w6.h`, included in `forever-compat-shim.h` and `cmangos-compat-shim.h`. Removed the slice-9 weight/spec stand-ins from `forever/src/slice/SliceLinkStubs.cpp` and wired `RandomItemMgr::Init()` to call `LoadWeightScales()`.

## Gate results

- **ForeverCore ON build (`tbf-build.sh ... tortoisebots`):**
  `[53/53] Linking CXX static library src/server/modules/TortoiseBots/libtortoisebots.a` (exit 0)
- **ForeverCore OFF build (`TB_SLICE=OFF tbf-build.sh ... tortoisebots`):**
  `[1/1] cd /src && /usr/bin/cmake -DBUILDDIR="/build" -DEMBED_BUILD_PATHS=ON -P /src/cmake/genrev.cmake /build` (exit 0)
- **Census (`tbf-build.sh ... census:forever/SLICE9.txt`):**
  `files 266  compile clean 0  with errors 266  error places 0 (in 0 files)  includes of a header ForeverCore does not have 0` (exit 0)
- **Pure policy test (`g++ -std=c++17 -O1 -Wall -Wextra -I. tools/test_classic_item_stat_policy.cpp`):**
  `0 failed` (exit 0); mutation test (dropping classic-id map) failed as expected: `Assertion failed at line 56: SameName(ai::ClassicItemStatWeightName(12), "defrtng")` (exit 1).
- **Slice registry & context check (`slice_registry_check.py`, `slice_gen_context.py --check`):**
  `summary: (a)=0  (b)=224 keys to register or stub later`
  `SliceAiObjectContext.cpp is up to date`

## What is untested / open questions

- **Turtle build & Turtle unit tests:** Not run in this environment; `RandomItemMgr.cpp` / `RandomItemWeights.cpp` / `TortoiseBots.cmake` / `ai/cmangos-compat-w6.h` preserve Turtle behavior and need lead verification on the Turtle builder.
- **Full `worldserver` link & live test:** Not run here (waiting on `g-equip` and `g-diag` integration + lead live run).
- **Random properties / suffixes:** `RandomProperty*Compat` returns `0` on ForeverCore (`item random properties` remains unported, suffix items score base stats only).
