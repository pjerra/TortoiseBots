# Slice 10, package H10 (host side of "druid, paladin, shaman")

Branch `s10/h` (worktree `~/wt-s10-h`), build slot 5 (`FOREVER_SLOT=5`, `src-5` / `build-5` on `/mnt/forever`), base = `s10/k` (the class code, notes `SLICE10-K-NOTES.md`) with `tbf/main` (feab6c882, the self-led run) merged: no conflict. Spec: `~/TortoiseBots-audit/FOREVER-SLICE10-HYBRIDS-SPEC.md` with "Lead decisions" (D1-D6). Core read only at the pin 6517af05. Not pushed to a PR.

`tbf/main` does NOT hold slice 8 (`s8/h`, `s8/k`, `s8/m` are separate branches): the factory here knows warrior, priest, druid, paladin, shaman. The merge with S8 touches the same lines (class-context `if` chain, `GetPlayerSpecTab`, `GetPlayerRoles`, the three `AddDefault*` functions, `slice_gen_context.py` KEYS, `CLASS_CONTEXTS`, the list name in CMake and the registry check): see "Needs at merge".

## 1. What changed

| Area | Files | Note |
|---|---|---|
| list | `forever/SLICE10.txt` (216 paths), `CMakeLists.txt` (`TB_SLICE_LIST` and its error text), `forever/tools/slice_registry_check.py` (`SLICE_LIST`, the three contexts in `CLASS_CONTEXTS`) | section 2 |
| registry keys | `forever/tools/slice_gen_context.py` (`SLICE10_KEYS`), regenerated `forever/src/slice/SliceAiObjectContext.cpp` | section 3 |
| factory | `forever/src/slice/SliceAiFactory.cpp` | section 4 |
| state keys | NEW `forever/src/host/TbHybrid.h`, `TbHybrid.cpp`; `forever/src/host/TbPartyState.cpp` (include + 1 call), `forever/src/host/TbH3.cpp` (include + 1 selftest call); `forever/docs/TB-COMMANDS.md` (one row) | section 5 |
| `forever/src/host/run/*` | **no file changed** (see section 6) | |

## 2. SLICE10.txt rebuilt

K10's `SLICE10.txt` was SLICE4.txt (174) + the 40 class files; `tbf/main` now has SLICE5.txt (176: SLICE4 + `DungeonPlanMgr.cpp`, `BossTacticsMgr.cpp`). Rebuilt: **SLICE5.txt (176 non-comment paths) + the 40 `.cpp` of `strategy/{druid,paladin,shaman}/` (druid 14, paladin 15, shaman 11) = 216 paths**, no duplicate, every path exists, SLICE1..SLICE5 are each a subset (`comm -23`: 0 each).

## 3. Registry keys

`slice: registered 17/48/75/170` (strategies/actions/triggers/values on `tbf/main`) becomes **17/50/88/176**: +2 actions, +13 triggers, +6 values = 21 keys. `slice_registry_check.py` against SLICE10.txt with the three contexts counted: `(a)=0`, **`(b)=231`** (252 with the contexts counted and no key added: the 21 are the difference). `slice_gen_context.py --check`: up to date.

| Kind | Key | Creator in the full build |
|---|---|---|
| action | move out of enemy contact | actions/ActionContext.h:209 |
| action | restore spec aura | :470 |
| trigger | enemy too close for melee | triggers/TriggerContext.h:156 |
| trigger | not facing target | :189 |
| trigger | rooted, tank rooted, party member rooted | :259, :260, :261 |
| trigger | enough mana | :86 |
| trigger | healer low mana | :81 |
| trigger | medium aoe heal | :212 |
| trigger | ranged medium aoe and healer should attack | :91 |
| trigger | target of attacker close | :249 |
| trigger | loose adds near | :132 |
| trigger | low mana, enemy is close | :80, :157 |
| value | has own totem, have any own totem | values/ValueContext.h:261, :263 |
| value | possible attack targets count | :208 |
| value | revive targets, boost targets | :353, :352 |
| value | healer low mana | :170 |

The spec's table plus `low mana` (the restoration shaman's combat rows) and `enemy is close` (the holy paladin's): K10's notes list both as shared with slice 8's list (registered once there too; at the merge keep one). Left unregistered on purpose, each making the bot do less: values `manual saved string` (D5), `travel target traveling`, `enemy player target`, `has enemy player targets`, `grind target`; triggers `enemy flagcarrier near`, `player has flag`, `new pet`, `has (greater) blessing of salvation`, `resist aura swap over`, `not sensing undead`, `kick`, `aquatic form`, `behind target`, `not behind target` (the feral dps and the `behind` strategy: out of the slice, D2); actions `apply oil/stone`, `oil of immolation`, `throw grenade`, `mana potion`, `stoneshield potion`, `remove (greater) blessing of salvation`, `set pet stance`, `sense undead`, `switch to melee/ranged`, `cancel channel`, `powershift`, `reach party member for totem`, `repentance or shield`. Those named by a kit's own strategies (bear: `remove blessing of salvation`, `has blessing of salvation`; both druid generics: `new pet`, `set pet stance`, `apply oil/stone`, `aquatic form`; the paladin non-combat: `not sensing undead`, `sense undead`, `apply oil/stone`) print one `slice: missing ...` line each at boot (the engine skips them): expected, not errors. Every strategy name of every kit resolves (checked by script against the class context's own creators and the slice registry).

## 4. Factory (`SliceAiFactory.cpp`)

Contexts: `GuardedContext<DruidAiObjectContext>`, `<PaladinAiObjectContext>`, `<ShamanAiObjectContext>` (compiled as they are). The kit comes from ONE function, `HybridKitOf(player, orderedRole)`: only a **tank order** changes it (a druid ordered to tank is the bear, a paladin ordered to tank the protection paladin); every other order, or none, is the healer kit (restoration druid, holy paladin, restoration shaman). The ordered role is read as the warrior's is: the player-wide stored role (`.tb role`), else the bot's own forced role.

| Kit | Combat | Non-combat | Reaction |
|---|---|---|---|
| bear druid (tab 1) | tank feral, tank assist, pull, close, tank face, cure, buff | tank feral, tank assist, follow, cure, buff, food, default | tank feral |
| restoration druid (tab 2) | restoration, dps assist, ranged, cure, buff, cast time, heal interrupt (+ threat) | restoration, dps assist, follow, cure, buff, food, default | restoration |
| holy paladin (tab 0) | holy, dps assist, ranged, cure, buff, aura, blessing, cast time, heal interrupt (+ threat) | holy, dps assist, follow, cure, buff, food, default, aura, blessing | holy |
| protection paladin (tab 1, recorded only) | protection, tank assist, pull, close, tank face, cure, aura, blessing, buff | protection, tank assist, follow, cure, buff, food, default, aura, blessing | protection |
| restoration shaman (tab 2) | restoration, dps assist, ranged, cure, totems, buff, cast time, heal interrupt (+ threat) | restoration, dps assist, follow, cure, buff, food, default | restoration |

`(+ threat)` comes from the existing `KeepsPartyThreatBackoff` rule, as for the priest. **Deviation from the spec's two tank kits:** the spec lists `threat` in the bear and protection lists; the two tank kits here return before the threat rule, as the warrior's tank kit does (slice 3 decision: `KeepsPartyThreatBackoff` is false for a tank, a tank never backs off the threat it holds, and the legacy tank kits (AiFactory.cpp:807-853, :782-805) do not carry it). Add `threat` to the two lists if the live test shows a need (one word each). No `totems` in the shaman's non-combat kit (D5: totems drop in combat only; spec section 2's non-combat kit has none). Left out as the spec says: `aoe`, `cc`, `boost`, `flee`, `pull back`, `offheal/offdps`, `stealth`, `oom melee`, `loot`; and (not in the spec, same reason) the druid's under-level-10 `leveling` kit.

Talent tab (`GetPlayerSpecTab`, read by `GenericDruidStrategy.cpp:68`, `GenericDruidNonCombatStrategy.cpp:173`, `GenericPaladinNonCombatStrategy.cpp:30`, `ShamanWeaponUpkeepAction.cpp:16`): druid bear 1 (feral), otherwise 2 (restoration); paladin protection 1, otherwise 0 (holy); shaman 2 (restoration: the weapon upkeep then starts at Flametongue, not Windfury); every other class 0 as before. The tab follows the ORDERED role (the same stored/own read as the kit), never talents.

Roles (`GetPlayerRoles`): the ordered role first (unchanged); with none, a druid, a paladin and a shaman are HEALERS (the tabs above). A druid or shaman ordered to DPS keeps the healer kit (cat, balance, enhancement and elemental are out: D2) and reports the ordered role DPS; a **shaman ordered to tank keeps the restoration kit** (the Turtle shaman tank kit is out): do not give a shaman the tank order in this slice.

## 5. State keys (`TbHybrid.cpp`, appended to every bot record after `practice=`)

`form=<id|none>` (`Player::GetShapeshiftForm`), `totems=<entry>:<sec left>;...` (the four totem summon slots `m_SummonSlot[1..4]`, read only, `Totem::GetTotemDuration`), `imbue=<enchant id>:<sec>` (main-hand `TEMP_ENCHANTMENT_SLOT`: id and duration), `seal=<aura id>` (a paladin's own aura of the paladin family named "Seal of ..."), `paura=<aura id>` (its own "... Aura"). Core API only (no engine call), side-effect free, empty when there is nothing to report; the names, not ids, decide seal and aura (no id is written in the code). **`power=` is not added:** the record has carried `power=<mana|rage|energy>` with `pw=`/`pwmax=`/`manapct=` since slice 3 and a key appears once per line. `.tb grammar selftest` prints three examples (`state-h10-bear`, `-shaman`, `-paladin`; illustrative ids). Off-hand imbues are not reported (a restoration shaman wields a shield).

## 6. The run's role mapping: no run file changed

The run reads roles only through `PlayerbotAI::IsTank/IsHeal` (TbRunFacts.cpp:281-299, TbRunOrders.cpp:755 `no-tank`, TbPartyState.cpp:74): the stored/own forced role first, then `ContainsStrategy(STRATEGY_TYPE_TANK)` (the `tank feral` strategy is one), then `GetPlayerRoles`. `.tb role Mbear tank` therefore makes the bear the run's lead tank (the `no-tank` refusal passes) and its kit has `pull`, which TbOrders.cpp:844 requires of a tank. `canResurrect` already counts paladin and shaman (the druid's Rebirth is combat-only: out, as the spec says); `usesMana` is false for a bear (accepted, spec section 4). Nothing in `forever/src/host/run/*` was edited: zero one-line registrations there.

## 7. Needs at merge

* S8's H8 and this branch both edit the class-context chain, `GetPlayerSpecTab`, `GetPlayerRoles`, the three `AddDefault*` functions of `SliceAiFactory.cpp`, `KEYS` in `slice_gen_context.py`, `CLASS_CONTEXTS`, the list constant in `slice_registry_check.py` and `CMakeLists.txt`. Union: one list `SLICE10.txt` = S8's 199 + this branch's 40 class files (the 22 K8 files are disjoint from them); register `low mana` and `enemy is close` once.
* `SliceAiFactory.cpp` combined: the S8 `GetPlayerSpecTab` returns 1 for a mage and a rogue; this branch's returns the hybrid tabs: one `switch` per class.
