# Slice 8, package K8 (rounds R0 and K8): the mage and rogue class code

Branch `s8/k` (worktree `~/wt-s8-k`), build slot 2 (`FOREVER_SLOT=2`), off the port branch (slices 1-4 + the wide pass). Spec: `~/TortoiseBots-audit/FOREVER-SLICE-MAGE-ROGUE-SPEC.md` (decisions D1-D6 accepted: no Sap, no stealth openers, no poisons, the mage gives water to the priest). Core read only at the pin 6517af05. Not pushed. H8 (host: factory, keys, `.tb cc`, `.tb strategy`), G8 (run CC facts) and the merge are NOT in this package.

## 1. R0: the list and the baseline

`forever/SLICE8.txt` = `SLICE4.txt` (174 paths) + 22 `.cpp`: 10 in `strategy/mage/`, 10 in `strategy/rogue/`, `strategy/actions/GiveItemAction.cpp`, `strategy/values/PartyMemberWithoutItemValue.cpp` (every path checked to exist, no duplicates). The spec's caveat holds: the S5/S6 lists are appended at merge. The spec's table says "22 `.cpp`" and the file list matches it.

Census baseline in slot 2 (`FOREVER_SLOT=2 FOREVER_CENSUS_LIST=forever/SLICE8.txt forever/tools/vm-census.sh "" 14`), before any K8 edit:
`files 196  compile clean 184  with errors 12  error places 17 (in 10 files)  includes of a header ForeverCore does not have 2`.
The 17 places are the spec's 17 exactly (names: RangedCombatStrategy 4, MeleeCombatStrategy 4, SetOwnerGuid, IsImmuneToSpellEffect, CalculatePowerCost, setItemTarget, m_targetMask, TRIGGERED_OLD_TRIGGERED, UNIT_STAT_PENDING_STUNNED, MINI_PET, plus RogueActions.h:298). One thing the spec did not say: the 8 `*CombatStrategy` places are not "the .cpp is in SLICE2" but a missing include: `GenericMageStrategy.h/.cpp` and `DpsRogueStrategy.h` include `"RangedCombatStrategy.h"` / `"MeleeCombatStrategy.h"` without the folder (the 2 "headers ForeverCore does not have" in `fatal.tsv`), which only resolves on the Turtle build's own search path.

## 2. Files ported (census before / after)

| File | Before | Change | After |
|---|---|---|---|
| `mage/GenericMageStrategy.h` (2), `.cpp` (2) | 4 | include with the folder: `playerbot/strategy/generic/RangedCombatStrategy.h` (as `priest/GenericPriestStrategy.h:12` does) | 0 |
| `rogue/DpsRogueStrategy.h` (2), `.cpp` (2) | 4 | same, `.../MeleeCombatStrategy.h` | 0 |
| `mage/MageActions.h:104` | 1 | `IsImmuneToSpellEffectCompat(target, spellInfo, i, false, bot)` (existing, TbCompat_P.h:115; its F side bounds the effect index) | 0 |
| `mage/MageTriggers.cpp:50` | 1 | `SpellPowerCostCompat(spellInfo, bot)` (existing, TbCompat_Unit.h:521; T side is the same `Spell::CalculatePowerCost` call) | 0 |
| `rogue/RogueActions.cpp:42` | 1 | `StunnedOrPendingStunCompat(unit)` (new) | 0 |
| `rogue/RogueActions.h:298` | 1 | `GetCombatReachCompat(bot, target, false, 0.0f)` (existing, TbCompat_Unit.h:72; T side is the 3-argument call) | 0 |
| `rogue/RogueActions.h:569-581` | 3 | the item-cheat branch of `ApplyPoisonAction` moved into `PoisonItemCheatCastCompat<BotUseItemSpell>` (new) | 0 |
| `actions/GiveItemAction.cpp:53` | 1 | `ItemSetOwnerCompat(item, target)` (new) | 0 |
| `values/PartyMemberWithoutItemValue.cpp:18` | 1 | `PetIsMiniPetCompat(pet)` (existing, TbCompat_V.h:206) | 0 |
| `PlayerbotAISpells.cpp:1836` (not a K8 file, no error place) | n/a | `out.state = InterruptSpellStateCompat(current)` (new), see section 4 | n/a |

The other 12 mage and rogue `.cpp` files compiled clean already. The spec's `IsImmuneToEffectCompat`, `CombatReachToCompat`, `PowerCostCompat`, `IsMiniPetCompat` were NOT added: the four calls are covered by helpers of slices 1-2 (one name each; Turtle sides are the original expressions).

## 3. The compat pair

`forever/src/compat/TbCompat_K8.h` (F) and `ai/cmangos-compat-k8.h` (T). Both are found through the new `ai/playerbot/TbCompatK8.h` (`__has_include("TbCompat_K8.h")` picks the F file, else the Turtle one), included by `RogueActions.h`, `GiveItemAction.cpp` and `PlayerbotAISpells.cpp`. That keeps the two shim headers untouched (listed under "Needs at merge").

| Helper | Turtle side (the original expression) | ForeverCore side | Proof / core cite |
|---|---|---|---|
| `StunnedOrPendingStunCompat(unit)` | `unit->HasUnitState(UNIT_STAT_STUNNED \| UNIT_STAT_PENDING_STUNNED)` | `HasUnitState(UNIT_STATE_STUNNED)`: no pending-stun state on F (Unit.h:281) | a mate whose stun is queued reads awake: the Sap approach keeps its clearance (less). Sap is out of the slice (D3), the rule is unreachable |
| `ItemSetOwnerCompat(item, owner)` | `item->SetOwnerGuid(owner->getObjectGuid())` | `item->SetOwnerGUID(owner->GetGUID())` (Item.h:205) | F's `MoveItemToInventory` sets it again when it differs (Player.cpp:13096-13097): harmless |
| `InterruptSpellStateCompat(spell)` | `uint32(spell->getState())` | PREPARING 1, CHANNELING 2, every other state 0 | section 4 |
| `PoisonItemCheatCastCompat<SpellType>(bot, weapon, proto, setDuration)` | the original loop of `ApplyPoisonAction::Execute`, word for word (`SetCastItem`-free `BotUseItemSpell` per on-use spell, `ForceSpellStart`), returning `count != 0` / false | `tbcompat::Unported(Feature::ItemCasts)`, false: row `item and totem casts` | no poison below level 20 (D4); the cheat is off in the slice |

The bag branch of `ApplyPoisonAction` (`ai->ImbueItem`) is row `item use packet` already. Turtle behaviour is unchanged by every edit (the helpers forward the old call); proof = Turtle `vm-build` and `vm-test tb` at the end (section 9).

## 4. The risks the spec ranks highest, settled from the core

### U1 Interrupts: can Kick fire on ForeverCore?

How the engine decides (`PlayerbotAI::IsInterruptableSpellCasting`, `PlayerbotAISpells.cpp:1810-1900`, rule `InterruptCastingPolicy.h:150-240`): the victim's cast must be on its bar (`PREPARING` with a cast time, or `CASTING`), have `PreventionType == 1`, and `InterruptFlags & 0x02` (generic) or `ChannelInterruptFlags & 0x04` (channel); the interrupt spell must have an INTERRUPT_CAST effect (68) the target is not immune to, or silence/COMBAT flags.

**The mirror fills all of them.** `TbSpellEntry.cpp:749-751` copies `InterruptFlags`, `AuraInterruptFlags`, `ChannelInterruptFlags` as the raw bits (`AsUnderlyingType()`), `:767` copies `PreventionType`; the effect array holds the first three effects by index. The bits are the client's: the core only names them differently (0x2 is `DamagePushbackPlayerOnly`, 0x4 `Stun`, SpellDefines.h:59-73), and the shim restores the old names with the same numbers (`forever-compat-shim.h:198-202`, SHIM-R3-NOTES.md:177). `SPELL_PREVENTION_TYPE_SILENCE` is 1 on both cores (SharedDefines.h:3159). Nothing to add to the mirror.

**Data check, read from the server's own DB2 files** (`forever-run/data/dbc/enUS/SpellInterrupts|SpellCategories|SpellEffect.db2`, parsed offline with a throw-away WDC5 reader; the `hotfixes` tables hold none of these spells):

| Spell | PreventionType | InterruptFlags | ChannelInterruptFlags[0] | Effects |
|---|---|---|---|---|
| Healing Wave 11986, Lightning Bolt 9532, Shadow Bolt 20791, Immolate 20800, Fireball 143, Frostbolt 116, Polymorph 118 | 1 | 0xF (bit 0x02 set) | 0 | |
| Arcane Missiles 5143 (channel) | 1 | 0xF | 0x7C0C (bit 0x04 set) | |
| Kick 1766, Pummel 6554, Shield Bash 72 | 2 (pacify), no SpellInterrupts row | 0 | 0 | idx1 (Kick, Pummel) / idx0 (Shield Bash) = effect 68 INTERRUPT_CAST |

So every cast the party meets passes the victim half of the rule, and Kick passes the interrupt half through effect 68 alone (the COMBAT-flag branch is false for it, the effect branch is true): consistent with the core's own gate, `SpellInfo::CanBeInterrupted` (SpellInfo.cpp:1771-1781: PreventionType silence, or the damage-cancel flags) used by `Spell::EffectInterruptCast` (SpellEffects.cpp:3111-3135).

**One real defect found, fixed through the helper above: the spell STATE.** The policy numbers PREPARING = 1, CASTING = 2 (`InterruptCastingPolicy.h:57-61`; on Turtle CASTING is a channel in progress and a missile in flight is DELAYED = 3). `PlayerbotAISpells.cpp:1836` passed `getState()` raw. F numbers PREPARING 1, LAUNCHED 2, CHANNELING 3 (Spell.h:254-262), and `LAUNCHED` is the state of a spell whose bar is over and whose missile is in flight (Spell.cpp:3945). Raw, a Fireball-style missile in flight read as "casting" (Kick on cooldown into thin air, the very failure the policy's comment names), and a real channel (3) read as nothing. `InterruptSpellStateCompat` maps F to PREPARING 1, CHANNELING 2, everything else 0, which is also exactly the core's gate (SpellEffects.cpp:3128-3129). Turtle: unchanged (`getState()`).

**Not covered by the engine's inputs (noted, not fixed here):** the core also refuses an interrupt on a target whose mechanic immunity mask holds `MECHANIC_INTERRUPT`, or that carries `SPELL_AURA_PREVENT_INTERRUPT` (SpellInfo.cpp:1779-1780). The engine's `effectImmune` reads effect and effect-mechanic immunity only (`Unit::IsImmunedToSpellEffect`, Unit.cpp:8268-8330), so a Kick at an interrupt-immune mob is cast and wasted (the bot does one useless cast per cooldown, not something new on Turtle either). Proposed one-liner for the merge, in `IsImmuneToSpellEffectCompat` of `TbCompat_P.h` (not a K8 file): for an `SPELL_EFFECT_INTERRUPT_CAST` effect also answer true when `target->GetMechanicImmunityMask() & (1 << MECHANIC_INTERRUPT)`.

### Polymorph: what breaks it, and are the engine's "do not break CC" inputs true?

* **What breaks it.** Polymorph's aura carries `AuraInterruptFlags[0] = 0x80002`: `Damage` (0x2) and `LeaveWorld` (data: SpellInterrupts row of 118; the same for ranks 12824-12826, 28271, 28272). `Unit::DealDamage` removes every aura with `SpellAuraInterruptFlags::Damage` on ANY damage that is not `NODAMAGE`: Unit.cpp:926-933 (before the "damage taken is 0" check, so an absorbed hit breaks it too), a DoT tick included (`damagetype == DOT` goes through the same block; only the aura of the damaging spell itself is spared, `RemoveAurasWithInterruptFlags` `source`, Unit.cpp:4490-4491), area damage included (every target of an AoE takes `DealDamage`). Direct damage also removes `NonPeriodicDamage` auras (Unit.cpp:1136-1137). Nothing else in this core spares a polymorphed unit; `IsInterruptFlagIgnoredForSpell` only concerns Moving/Action flags (Unit.cpp:4452-4474).
* **The engine's reads are true.** `HoldsBreakableCc` (`PossibleAttackTargetsValue.cpp:123`) and `HasBreakableCC` (:142) read `Unit::IsPolymorphed()` (Unit.cpp:11318-11329): transform spell set and `GetSpellSpecific() == SPELL_SPECIFIC_MAGE_POLYMORPH`. The transform spell is set by the aura's TRANSFORM effect (SpellAuraEffects.cpp:2138; Polymorph's effect 1 is aura 56) and `GetSpellSpecific` returns the polymorph kind for family MAGE with mask bit 0x1000000 and effect 0 `MOD_CONFUSE` (SpellInfo.cpp:3045-3046); the data for 118 and its ranks: family 3 (mage), mask 0xC1040000 (bit set), effect 0 aura 5. So `IsPolymorphed()` is true for every Polymorph rank and variant. The area guard (`BreaksCcInArea`, `GenericSpellActions.cpp:47`) and the mage's `MageAoeSafetyPolicy` use the held-mark scan, which is built on the same read: they are the only protection against area damage, as the spec says.
* **Not guarded by anything on F:** a DoT put on the unit before the polymorph, and any damage a bot does not pick (pets, traps, procs). A held unit that already carries a DoT breaks on the next tick (Unit.cpp:926-933). The run's pull rule picks a unit not yet fought (P: `PickCcOrderPlan`), so this stays an observation for live check f/g.

## 5. Stand-ins and unported rows

No link stand-in is added by K8 (option-ON link is the merge package's gate). Unported rows touched (`forever/docs/UNPORTED.md`, block at its end): `item and totem casts` +1 call site (`RogueActions.h`, the poison item cheat). Documented limits that are decisions, not rows: no Sap, no stealth openers (D3, D4), no poisons at level 18 (D4: items need level 20; section 9 of the spec).

## 6. Needs from H8

* Registry keys (`SLICE8_KEYS`): actions `give food, give water, cancel channel`; triggers `give food, give water, low mana, enemy ten yards, enemy is close, behind target, not behind target, high energy available, no energy available, has aggro in raid group`; strategy `hold position`; the spec's section 3 table is the authority. `slice_registry_check.py` closed against `SLICE8.txt` (run here by pointing `SLICE_LIST` at it, not committed): `(a)=0  (b)=407`, against `(b)=215` for SLICE4.txt; the extra 192 are the two class contexts' keys. Values `party member without item/food/water` (type `Unit*`) must be registered for `give food/water` to work.
* The mage and rogue kits, `GetPlayerRoles`, and the class contexts' creation in `SliceAiFactory.cpp` (the factory knows warrior and priest only).
* Poison and stealth are off the rogue kit (D4): `poisons` and `stealth` strategies are not in it, rogue `cc` neither.

## 7. Needs at merge

1. `CMakeLists.txt` option list: the merge package switches `TB_SLICE_LIST` from `SLICE4.txt` to `SLICE8.txt` (with the S5/S6 lists appended); `slice_registry_check.py:26` likewise.
2. Optional: add `#include "TbCompat_K8.h"` to `forever/src/compat/forever-compat-shim.h` (after `TbCompat_D4.h`) and `#include "cmangos-compat-k8.h"` to `ai/cmangos-compat-shim.h` (after `cmangos-compat-d4.h`). Not required: `ai/playerbot/TbCompatK8.h` finds the pair. Left to the lead because those two files are shared.
3. One engine line outside the owned folders: `PlayerbotAISpells.cpp:1836` (`InterruptSpellStateCompat`), plus its include line at the top. Any other package that edits that function must keep the helper.
4. Proposed (not done): the `MECHANIC_INTERRUPT` one-liner in `TbCompat_P.h` (section 4).
5. `UNPORTED.md` count line: 73 features after slice 2; K8 adds a call site to an existing row, no new row.

## 8. Differences to watch live (spec observations d, e, h)

* Kick/Shield Bash: the engine now kicks only a spell that is really on its bar (a missile in flight is no target); a channel counts as `CASTING`. Count Healing Wave starts vs interrupted (observation e).
* `StunnedOrPendingStunCompat` only matters for Sap (off).
* Give-item (`ItemSetOwnerCompat`, `MoveItemFromInventory`/`MoveItemToInventory` as the core's trade does): persistence of a given stack over logout is spec unknown U7, a live check.

## 9. Gates (final commit)

(filled in below)
