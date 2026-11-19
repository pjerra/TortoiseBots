# Slice 10, packages K10d, K10p, K10s and D10: the druid, paladin and shaman class code, and their kits

Branch `s10/k` (worktree `~/wt-s10-k`), build slot 5 (`FOREVER_SLOT=5`, `src-5` / `build-5` on `/mnt/forever`), off `tbf/main` e57bd1849. Spec: `~/TortoiseBots-audit/FOREVER-SLICE10-HYBRIDS-SPEC.md` with "Lead decisions" (D1-D6 as recommended). Core read only at the pin 6517af05. Not in this package: R0 (the real key closure is only listed below), H10 (host: factory, roles, kits, keys, state keys), M10, the live test.

## 1. The list and the baseline

`forever/SLICE10.txt` starts from **`SLICE4.txt` (tbf/main's newest list: 174 paths)**, not from SLICE8.txt (that list exists only on the unmerged `s8/k` branch), plus the 40 `.cpp` of spec section 3 (druid 14, paladin 15, shaman 11; every path checked to exist, no duplicates): 214 paths. The spec's "in SLICE1-8" files (ClassStrategy, PullStrategy, GenericActions, ChangeStrategyAction, GenericTriggers, PartyMemberValue, PartyMemberToDispel, TargetValue, ThreatValues, HasTotemValue) are all already in SLICE4.txt. At merge the S8 list and this one are unioned (the 40 are disjoint from S8's 22).

Census baseline in slot 5 (`FOREVER_SLOT=5 FOREVER_CENSUS_LIST=forever/SLICE10.txt forever/tools/vm-census.sh "" 14`), before any K10 edit:
`files 214  compile clean 208  with errors 6  error places 13 (in 6 files)  includes of a header ForeverCore does not have 0`.
Places: PaladinTriggers.cpp 4, ShamanTriggers.h 3, DruidActions.cpp 2, PaladinActions.cpp 2, BalanceDruidStrategy.cpp 1, ShamanTriggers.cpp 1. The spec counted 14: its `FORM_MOONKIN` place (BalanceDruidMana.h:14) is 0 as it predicted (`TbCompat_W2.h:196`).

## 2. Files ported (census before / after)

| Pkg | File | Before | Change | After |
|---|---|---|---|---|
| K10d | `druid/DruidActions.cpp:30` | 2 | `member->GetVisibility() == VISIBILITY_OFF` -> `PlayerHiddenGmCompat(member)` (existing, TbCompat_Unported.h:342; T side is the same expression) | 0 |
| K10d | `druid/BalanceDruidStrategy.cpp:53` | 1 | `ai->GetBot()->AttackStop(true)` -> `AttackStopCompat(ai->GetBot(), true)` (new) + include `playerbot/TbCompatS10.h` | 0 |
| K10p | `paladin/PaladinActions.cpp:463` | 2 | `PlayerHiddenGmCompat(member)` | 0 |
| K10p | `paladin/PaladinTriggers.cpp:329` | 2 | `PlayerHiddenGmCompat(member)` | 0 |
| K10p | `paladin/PaladinTriggers.cpp:103-104` | 2 | `info->rank` -> `CreatureRankCompat(creature)` (existing, TbCompat_Unit.h:506); the `info &&` guard stays | 0 |
| K10s | `shaman/ShamanTriggers.cpp:69` | 1 | `info->rank` -> `CreatureRankCompat(creature)` | 0 |
| K10s | `shaman/ShamanTriggers.h:631` (Purge) | 1 | the `GetAurasByType` loop -> `ForEachAuraOfTypeCompat(target, type, lambda)` (existing, TbCompat_Unit.h:461), the loop body unchanged inside the lambda (`return false` where it was `continue`) | 0 |
| K10s | `shaman/ShamanTriggers.h:911, :953` | 2 | `pet->getPetType() != MINI_PET` -> `!PetIsMiniPetCompat(pet)` (existing, TbCompat_V.h:208) | 0 |

The other 34 class `.cpp` compiled clean already. Census after (all three classes, final commit): `files 214  compile clean 214  with errors 0  error places 0 (in 0 files)  includes of a header ForeverCore does not have 0`.

`tools/test_balance_mana_melee.py` compiles `OomMeleeStrategy::OnStrategyRemoved` against stubs: it got a one-line stub for `AttackStopCompat` (the test fails without it; passes after).

## 3. The compat pair

`forever/src/compat/TbCompat_S10.h` (F) and `ai/cmangos-compat-s10.h` (T), found through `ai/playerbot/TbCompatS10.h` (`__has_include("TbCompat_S10.h")`), the same wrapper pattern as K8: the two shim headers are untouched.

| Helper | Turtle side (the original expression) | ForeverCore side | Core cite |
|---|---|---|---|
| `AttackStopCompat(Unit*, bool targetSwitch)` | `unit->AttackStop(targetSwitch)` | `unit->AttackStop()`, the flag is dropped | Unit.h:739, Unit.cpp:6262-6289 |

Different answer: none for the only caller (a Player, a balance druid leaving its melee kit): F's body drops the victim, clears the target, interrupts the melee swing and sends the stop packet; the one thing T's `targetSwitch` changes (the creature-only "reset only at real combat stop" block) F does for creatures unconditionally (:6281-6284), which a Player never reaches. The helper exists for `CombatActions.cpp:45` (`bot->AttackStop(true)`, not in SLICE10): when that file enters a list it must use the helper.

Reused, not added: `PlayerHiddenGmCompat` (F: `!isGMVisible()`), `CreatureRankCompat` (F maps the classification and `isWorldBoss` to the T numbers; rare stays outside "elite" on both, as the original test), `ForEachAuraOfTypeCompat` (F visits one AuraEffect per effect with an index below 3; an aura whose spell has no mirror entry is skipped: the Purge walk answers false for it, which is less), `PetIsMiniPetCompat` (F: false, mini pets are not `Pet` objects there).

## 4. Registry keys H10 must register (`slice_registry_check.py` against SLICE10.txt, with the druid, paladin and shaman contexts counted as registered)

Run here with the list constant and `CLASS_CONTEXTS` patched in a throw-away copy (not committed): `(a)=0  (b)=252`; as committed (SLICE4.txt) `(a)=0  (b)=215`, unchanged. Of the 252, these are named by the three class folders (the rest are SLICE4's generic files, S3-S5's business). Spec section 3 is the authority for which to register; this is what the sources actually name:

* **Register (used at 18-22):** values `has own totem`, `have any own totem` (both `bool`), `possible attack targets count` (`uint8`), `revive targets` (`std::list<ObjectGuid>`), `boost targets` (`std::list<ObjectGuid>`), `healer low mana` (`Unit*`; the trigger of the same name too); triggers `enemy too close for melee`, `not facing target`, `rooted`, `party member rooted`, `tank rooted`, `enough mana`, `healer low mana`, `medium aoe heal`, `ranged medium aoe and healer should attack`, `target of attacker close`, `loose adds near`; actions `move out of enemy contact`, `restore spec aura`. Already on S8's H8 list, so register once: triggers `low mana`, `enemy is close`, `behind target`, `not behind target`; action `switch to melee`/`switch to ranged` (named only by the balance strategy, which the kits leave out).
* **Leave unregistered (do less):** values `manual saved string` (D5), `enemy player target`, `has enemy player targets`, `grind target`; triggers `enemy flagcarrier near`, `player has flag`, `new pet`, `has blessing of salvation`, `has greater blessing of salvation`, `resist aura swap over`, `not sensing undead`, `aquatic form`, `kick` (the druid file names a rogue key), and the three druid `... on attacker` and `insect swarm` names that exist nowhere; actions `apply oil`, `apply stone`, `oil of immolation`, `throw grenade`, `mana potion`, `stoneshield potion`, `remove blessing of salvation`, `remove greater blessing of salvation`, `set pet stance`, `sense undead`, `cancel channel`, `powershift`, `repentance or shield`, `reach party member for totem`. The key `greater` is a parser artefact (a string prefix), not a key.
* Strategies: none of the class folders names an unregistered strategy.
* `slice_gen_context.py --check`: `SliceAiObjectContext.cpp is up to date` (the generator and its SLICE10_KEYS are H10's).

## 5. D10: kits (`forever/data/slice10/`)

`kits-level18.csv` and `kits-level22.csv`, format of `waterys/rfc/kits-level18.csv` (`role,slot,item_id,name,source,reason`), five roles: `bear druid` (tank), `restoration druid` (recorded), `holy paladin`, `protection paladin` (recorded only, D1), `restoration shaman`; 19 rows per role: 17 slots (`finger2`, `trinket2` as the warrior kit has them) plus `food` and `drink`. Written by `gen_kits.py` in the same folder from SELECTs on the world database of the forever test server (read only: `armory_item`, `npc_vendor`, `quest_template`, `creature_loot_template`, `creature_template`) and `food-water-5-25.csv`; no id comes from memory. Rules (all in the script): `requiredLevel <= level`, `itemLevel` between level-8 and level+9, quality 1-3, a source in the tables (quest first, then vendor, a drop only by a clear margin) with the race of the role's character (night elf druid, dwarf paladin and shaman), class bit of the item, stat weights per role (bear: stamina, agility, strength, defense, armor; healers: intellect, healing, spell power, mana per 5 s; protection: stamina, defense, armor), armor type bear/resto druid and shaman leather, paladin mail (a cloak is cloth; a type without a candidate falls back to the next type, then to the piece with the most armor), a two-hander (staff or two-hand mace) for the bear, a one-hand weapon (the shaman's for the imbue) plus a shield (paladin, shaman) or a held item (resto druid). Level 18: 11 EMPTY rows over the five roles (head: none sourced; bear and protection paladin no trinket; no relic anywhere); level 22: 6 EMPTY rows.

**Not done, by the brief:** `.tb itemscan` / `.tb equip` need a live server (none started here). Before the kits drive a test (M10), run `.tb itemscan` over each role's ids and read the `EQUIP_ERR_*` per row: the armor-type rule is the classic one (shaman leather and paladin mail below 40), not measured; some picks are Turtle/Forever custom ids (`27xxxx`, `28xxxx`) with `requiredLevel 0` (their `itemLevel` is the guide); a drop row names the creature entry. The spell lists `{druid,paladin,shaman}-spells-1-22.csv` of spec section 5 are not in this package (the brief asks for kits).

## 6. Needs from H10

* Registry keys: section 4 (the "register" list, minus what H8 already registers); `SLICE10_KEYS` in `slice_gen_context.py`.
* `SliceAiFactory.cpp`: the druid, paladin and shaman class contexts (the factory knows warrior, priest, mage, rogue), `GetPlayerRoles` and the kits of spec section 2 (bear `tank feral` ..., holy, resto shaman; protection recorded), forced role first as slice 3.
* State keys of spec section 7 (`form=`, `power=`, `totems=`, `imbue=`, `seal=`, `paura=`).
* Kits: the D10 files above feed `.tb itemscan` + `.tb equip`.

## 7. Needs at merge

1. `CMakeLists.txt` option list: `TB_SLICE_LIST` to SLICE10.txt (union with S5/S6/S8 lists); `slice_registry_check.py:26` likewise, and add the three class contexts to `CLASS_CONTEXTS` there.
2. Optional: `#include "TbCompat_S10.h"` in `forever/src/compat/forever-compat-shim.h` and `#include "cmangos-compat-s10.h"` in `ai/cmangos-compat-shim.h`; not required, the wrapper finds the pair.
3. `CombatActions.cpp:45`: use `AttackStopCompat` when that file enters a list.
4. UNPORTED.md: no new row and no new call site (the helpers reused route nothing to `Unported`).

## 8. Differences to watch live (spec observations c, e, f, g)

* Hidden GM masters are skipped by the heal scans through F's `isGMVisible()`: a GM with `.gm visible off` as commander is never healed or buffed, as on Turtle.
* Paladin judgement and shaman tank rows read the creature rank through the classification: a dungeon boss that F marks `Normal` with a boss flag reads world boss (`isWorldBoss`), an elite reads elite.
* Purge: an aura effect with an index of 3 or more is not seen (mirror limit).

## 9. Gates (final commit)

Filled in below at the end of the work.
