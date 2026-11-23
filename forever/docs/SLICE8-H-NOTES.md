# Slice 8, package H8 (host side of "mage, rogue and crowd control")

Branch `s8/h` (worktree `~/wt-s8-h`), default build slot (no `FOREVER_SLOT`), base = the run slices S5+S6 combined (`run/s6-integ`) + the class port `s8/k` merged. Spec: `~/TortoiseBots-audit/FOREVER-SLICE-MAGE-ROGUE-SPEC.md` section 6 and the H8 row (decisions D1-D6 accepted). Not in this package: G8 (the run's CC facts and verbs), the merge, any live test, any file of `forever/src/host/run/*` or `TbOrders.cpp` (package N6 works there). Core read only at the pin 6517af05. Not pushed.

## 1. What changed

| Area | Files | Note |
|---|---|---|
| list | `forever/SLICE8.txt` (199 paths), `CMakeLists.txt` (`TB_SLICE_LIST` and its error text), `forever/tools/slice_registry_check.py` (`SLICE_LIST`) | section 2 |
| registry keys | `forever/tools/slice_gen_context.py` (`SLICE8_KEYS`), regenerated `forever/src/slice/SliceAiObjectContext.cpp` | section 3 |
| factory | `forever/src/slice/SliceAiFactory.cpp` | section 4 |
| commands | NEW `forever/src/host/TbCc.h`, `TbCc.cpp`; one-line registrations in `forever/src/TbScripts.cpp` (include + 2 rows), `forever/src/host/TbPartyState.cpp` (include + 1 call), `forever/src/host/TbH3.cpp` (include + 1 selftest call) | section 5 |
| Kick at an interrupt-immune mob | `forever/src/compat/TbCompat_P.h` (own commit) | section 6 |

## 2. SLICE8.txt rebuilt

K8's `SLICE8.txt` was `SLICE4.txt` + 22 (it did not hold the S5 loader files). Rebuilt here: **SLICE5.txt (176 lines: SLICE4's 174 paths + `DungeonPlanMgr.cpp`, `BossTacticsMgr.cpp`) + the 22 K8 `.cpp`** (10 `strategy/mage/`, 10 `strategy/rogue/`, `strategy/actions/GiveItemAction.cpp`, `strategy/values/PartyMemberWithoutItemValue.cpp`) + **`ai/playerbot/strategy/actions/CancelChannelAction.cpp`** = **199 paths**, no duplicates, every path exists, the 22 are exactly K8's (checked with a set difference against K8's file and SLICE5). The 23rd is a gap of the spec's file table: the registered key `cancel channel` (spec section 3) builds `CancelChannelAction`, whose only definition is that `.cpp`; without it the option-ON link failed with `undefined vtable for CancelChannelAction` (the only undefined symbol). The census compiles it clean. `CMakeLists.txt` reads it; `slice_registry_check.py` closes the keys against it and now counts `MageAiObjectContext.cpp` and `RogueAiObjectContext.cpp` as registered class contexts (as warrior and priest already were): without that the check reported 407 (K8's number: both contexts' own keys counted as missing).

## 3. Registry keys

`slice: registered 17/48/75/170` (strategies/actions/triggers/values) becomes **19/53/86/173**. `slice_registry_check.py`: `(a)=0`, **`(b)=212`** against `SLICE8.txt` with the class contexts counted (before: `(b)=215` against SLICE5; K8's reading with the two contexts unaware: 407). Creators are the full build's text (the check proves it).

| Kind | Key | Creator in the full build |
|---|---|---|
| strategy | hold position | StrategyContext.h:105 |
| strategy | behind (see below) | StrategyContext.h:143 |
| action | give food, give water | actions/ActionContext.h:231-232 |
| action | cancel channel | :221 |
| action | set behind, rear flank (see below) | :212-213 |
| trigger | give food, give water | triggers/TriggerContext.h:227-228 |
| trigger | low mana | :80 |
| trigger | enemy ten yards, enemy is close | :160, :157 |
| trigger | behind target, not behind target | :186-187 |
| trigger | high energy available, no energy available | :107, :104 |
| trigger | has aggro in raid group | :135 |
| trigger | rear flank needed (see below) | :188 |
| value | party member without item, food, water | values/ValueContext.h:329-331 (K8 notes "Needs from H8": `give food/water` reads them) |

**Added beyond the spec's table (a hole in it):** strategy `behind` and the three keys it fires (`set behind`, `rear flank`, `rear flank needed`; `SetBehindCombatStrategy::InitCombatTriggers`, `MeleeCombatStrategy.cpp:30-40`). The spec's rogue kit (section 2) lists `behind`, but the spec's closure read only the mage and rogue folders, never the kit's strategies, so `behind` and its keys were in no list. Without them the engine logs `slice: missing strategy behind` and the rogue never moves behind its target. Whether `rear flank` / `set behind` behave on ForeverCore's movement is a live observation (spec gate d/h); they only move the rogue.

Left unregistered on purpose, as the spec says (the extra `(b)` lines against 215): `apply oil, throw grenade, oil of immolation, thistle tea` (item casts), `enemy flagcarrier near, player has flag, enemy player target, has enemy player targets` (PvP), `grind target`, `manual saved string`, `no blink instance`, `hemorrhage back/front` (no context defines them). Each makes the bot do less.

Every strategy name of both kits resolves: slice registry or the class context's own creators (checked by script: `fire`, `combat`, `cc`, `buff`, `boost` are the class contexts' placeholder strategies).

## 4. Factory (`SliceAiFactory.cpp`)

| | Mage | Rogue |
|---|---|---|
| context | `GuardedContext<MageAiObjectContext>` | `GuardedContext<RogueAiObjectContext>` |
| spec tab | 1 (fire) | 1 (combat); every other class stays 0 |
| role | damage | damage |
| combat | fire, dps assist, ranged, cc, buff, boost (+ threat) | combat, dps assist, close, behind, buff, boost (+ threat) |
| non-combat | fire, dps assist, follow, buff, food, default | combat, dps assist, follow, buff, food, default |
| reaction | fire | combat |

`threat` is added by the existing `KeepsPartyThreatBackoff` rule (a commanded non-tank backs off the tank's threat), as for the priest: the spec writes `threat` last in both combat kits. Left out as the spec says: `flee`, `cure`, `aoe` (both), the mage's `loot` (the priest and warrior sets have it; the spec's mage set does not), rogue `cc` (D3), `stealth`, `poisons` (D4: `.tb strategy Mrogue nc +stealth` measures stealth, section 5). `GetPlayerSpecTab` returns 1 for a mage and a rogue (the full factory's tab without talents, AiFactory.cpp:169-185; `GenericMageStrategy.cpp:119,:140` read it) and still 0 for the others.

**Kits and talents are not code.** `kits-level18.csv` and `talents-level18.csv` are applied by the live setup with the existing `.tb itemscan`, `.tb equip` and `.tb learn` (spec section 12 "Setup with no human"): the factory builds strategies, not gear. Nothing in H8 reads those files.

## 5. Commands (reply grammar 2, the one encoder, `RequireBuilt` first: engine-not-built, then a bad token)

### `.tb cc <bot> <mark> <unit guid|target> [family] [#token]`
One host bot (a name; `party` is refused `bad-target`). `<mark>` is star, circle, diamond, triangle, moon, square or cross (skull is the party's kill mark: `bad-mark`). The unit is the 32 hex digits of `.tb state`'s `tgt=` or `target` (the sender's selection; from the console `needs-player-sender`). `family` is polymorph, shackleundead, banish, hibernate, seduction, freezingtrap, sap, repentance, scarebeast, fear or none (`runtime/CcOrderFamily.h`; else `bad-family`, before any mark moves).

`h8::ApplyCcOrder(leader, bot, mark, unit, family, code, detail)` (also the call G8 uses; `leader` = the party leader, else the bot, only names who changed the icon): the unit must be on the bot's map, alive, a creature, attackable by the bot; the bot must have an engine and be in a party (with the leader); then, all undone when the read-back fails: the icon is set on the party; the bot's `rti cc` = mark and `run cc family` = family or `none`; every other party bot holding the mark as its `rti cc` loses it (exclusive ownership); every damage bot (or this one) whose `rti` was the mark is moved to `skull`; the cached `rti cc target` / `rti target` / `cc target` values are reset. No cast is started (the mage's `cc` strategy polymorphs the `rti cc` unit when the fight starts) and nothing is saved to a database (the host has no bot store: a rebuilt engine returns to the default `moon` / `none`).

```
cc-begin #c1
cc bot=Mmage verb=set status=ok mark=moon unit=0000000000000000F130002C4A000001 entry=11322 rticc=moon ccfam= #c1
cc-end count=1 records=1 verb=set target=Mmage ok=1 refused=0 #c1
```
`ccfam=` is empty for `none` (addendum A6). Refusals are `error` lines: `missing-target no-such-player not-a-bot bad-target missing-argument unexpected-argument needs-player-sender no-selection bad-mark bad-family no-engine no-party target-not-found target-dead not-attackable failed`.

### `.tb cc <bot> clear [<mark> <unit guid|target>] [#token]`
`h8::ClearCcOrder`: `rti cc` and `run cc family` back to `none`; with a mark and a unit the party's icon is removed ONLY while it still holds that unit. Record `cc bot= verb=clear status=ok rticc= ccfam= [mark= icon_removed=yes|no]`, end `cc-end ... verb=clear target= ok= refused=`.

### `.tb strategy <bot> co|nc <+name,-name,~name ...> | <bot> reset | role:<tank|healer|dps> co|nc <changes ...> [#token]`   (test setup, `tag=SETUP`)
Applies at once on the world thread (the engine is not running: it ticks before the map update) and reports what the engine holds AFTER each change (`HasStrategy`): `strategy bot= state=co|nc op=add|remove|toggle name= tag=SETUP status=ok changed=yes|no`, or `status=refused code=not-applied`; end `strategy-end ... target= state= applied= refused=`. Every name must be a strategy of the bot's context (`unknown-strategy`, nothing applied). `~name` toggles on the state the bot is in now: remove when the strategy is held there, add when it is not (`op=toggle`). `.tb strategy <bot> reset` calls `PlayerbotAI::ResetStrategies()` (the factory kit again, the forced role's line included) and answers `op=reset`; end `strategy-end target= op=reset applied=1 refused=0`. `role:<tank|healer|dps>` in place of the bot name addresses every host bot of the sender's group whose stored role (`PlayerbotAIStorage` role, the value `.tb role` writes) is that word, each with its own records; every target's engine and every name are checked before the first change, and the end record adds `bots=` (`target=role:tank`). A later `.tb role`, `.tb master` or `free` rebuilds the kit and drops the change. `.tb order <bot> co|nc ...` (slice 3) is the queued form of the same change; this one answers truthfully at once.

### `.tb state` keys appended to every bot record (after `practice=`)
`rticc=` (the bot's `rti cc`, empty for none), `ccfam=` (empty for none), `held=<entry>:<mark>` (only while the unit on that mark exists, lives and a breakable crowd control holds it now: `PossibleAttackTargetsValue::HoldsBreakableCc`), `combo=` (a rogue's combo points, `ComboPointsCompat`). `.tb grammar selftest` prints one example of each record.

In the default build (option OFF) all three answer `error code=engine-not-built` (the TbH4.cpp pattern).

## 6. The Kick-at-immune fix

K8's notes (section 4 and "Needs at merge" 4) propose it and the spec does not mention it. It is applied here as its OWN commit (`TbCompat_P.h` `IsImmuneToSpellEffectCompat`: for an `SPELL_EFFECT_INTERRUPT_CAST` effect also answer immune when `target->GetMechanicImmunityMask() & (1 << MECHANIC_INTERRUPT)`, the test `SpellInfo::CanBeInterrupted` makes, SpellInfo.cpp:1778). It makes the bot do less (no wasted Kick), matches the review rule that every failure value fails toward less, and touches only the ForeverCore side. Drop the commit to leave it out. The aura half of the core's test (`SPELL_AURA_PREVENT_INTERRUPT`) is not covered.

## 7. Needs at merge, and what the live test should look at first

* The S5/S6 lists are already in `SLICE8.txt`; nothing to append. `TB_SLICE_LIST` and the registry check read it. Other packages' notes that name `SLICE5.txt` still describe the older list.
* G8: call `h8::ApplyCcOrder` / `h8::ClearCcOrder` (`forever/src/host/TbCc.h`) from `run::Issue`; both exist in the option-OFF build too (they answer `engine-not-built`).
* Boot: `slice: registered 19/53/86/173`; no `slice: missing` line for any key of the two kits. Then spec gates a, b, c (give water to the priest: `give water` trigger and `party member without water`), then f with `.tb cc Mmage moon <Cultist guid>` and `.tb state Mmage` (`rticc=moon`, `held=` once the sheep landed).
* Gate h needs `.tb strategy Mrogue nc +stealth`.

## 8. Gates (default slot, final code tree; commits after it change docs only)

| Gate | Result |
|---|---|
| census `SLICE8.txt` | `files 199  compile clean 199  with errors 0  error places 0 (in 0 files)  includes of a header ForeverCore does not have 0` |
| census SLICE5 / 4 / 3 / 2 / 1 | every path of each list is a path of SLICE8.txt (`comm -23`: 0 each) and the census compiles each file on its own, so each list is 0 by the SLICE8 run (not five separate runs) |
| `slice_gen_context.py --check` | `SliceAiObjectContext.cpp is up to date` (19/53/86/173) |
| `slice_registry_check.py` | `(a)=0  (b)=212` (SLICE5: 215) |
| option ON `worldserver` | `CMAKE_RC=0 MAKE_RC=0`, 0 `undefined reference`; `h8::ApplyCcOrder`, `ClearCcOrder`, `HandleCc`, `HandleStrategy`, `AppendCcState`, `SelfTestRecords` and `CancelChannelAction::Execute` strong in `worldserver` |
| option OFF `tortoisebots` and `worldserver` | `CMAKE_RC=0 MAKE_RC=0` both |
| tests (VM, g++) | `test_run_orders` PASS 128, `test_run_c6` PASS 28, `test_run_facts` PASS, `test_dungeon_run_policy: ok`, system run policy 109, `test_mage_aoe_safety_policy` 14, `test_rogue_sap_approach_policy` passed, `test_rogue_threat_policy` passed, `test_cc_mark_hold_policy` 79, `test_cc_area_guard_policy` all checks passed (6 scans) |
| Turtle `vm-build` / `vm-test tb` | not needed: nothing outside `forever/` changed (root `CMakeLists.txt` is read by ForeverCore only) |

Nothing in this package was run against a live server (no boot): `.tb cc`, `.tb strategy`, the state keys and the kits are proven to compile, link and register, not to behave.
