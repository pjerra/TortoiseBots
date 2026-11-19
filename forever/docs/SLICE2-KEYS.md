# Slice 2 registry keys (package H, R0 step 3)

Input for the generator package (`forever/tools/slice_gen_context.py` and `SliceAiFactory.cpp`); nothing here is generated or registered yet.
Method: the lead's list of FOREVER-SLICE2-SPEC section 3 (69 keys: 5 strategies, 12 actions, 26 triggers, 26 values), each looked up in the full
build's registries with `slice_registry_check.py`'s parsers (read only), its creator expression and `file:line` taken from there, the class found
by `new <Class>(` in the creator and located by its declaration and its out-of-line definitions under `ai/playerbot`, then checked against
`forever/SLICE2.txt`. Every key exists in the full registries, with its creator unchanged (the generator copies that text). Paths are under
`ai/playerbot/strategy/` unless they start with `ai/`.

**Result: 69 keys, all 69 creators found, no class whose out-of-line definitions are in a file outside `SLICE2.txt`.** Classes of the form
"header only" have no `.cpp` of their own to add: their methods are inline in the header. Six `.cpp` files with a same-named header are
NOT in the list and are not needed for these keys: `values/MaintenanceValues.cpp` (819 lines: buyer-trip logic; the six drink/eat value classes are inline in
`MaintenanceValues.h`), `values/BudgetValues.cpp` (327; `ShouldGetMoneyValue` is inline in `BudgetValues.h`), `values/ItemUsageValue.cpp` (2,419; D2:
`ForceItemUsageValue` is inline in `ItemUsageValue.h`, but the header declares `ItemUsageValue` whose methods live in the .cpp: the header already compiles clean in the census, a
call to one of those methods from an inline body would show up as a link symbol in R3). Three more are comment-only stubs (the classes are inline in their headers): `values/AvailableLootValue.cpp`,
`values/HasAvailableLootValue.cpp`, `triggers/WorldPacketTrigger.cpp`.

The priest's class context `priest/PriestAiObjectContext.cpp` registers its own 203 keys (73 strategies, 75 actions, 55 triggers, 0 values) as it is, like the warrior's: they are not in this list.

## Strategies (5)

| Key | Creator in the full registry (`creators[...] =`) | Registry line | Class | Class file (header; out-of-line definitions) |
|---|---|---|---|---|
| ranged | `new RangedCombatStrategy(ai)` | StrategyContext.h:142 | RangedCombatStrategy | `generic/RangedCombatStrategy.h`; generic/RangedCombatStrategy.cpp |
| food | `new UseFoodStrategy(ai)` | StrategyContext.h:96 | UseFoodStrategy | `generic/UseFoodStrategy.h`; generic/UseFoodStrategy.cpp |
| loot | `new LootNonCombatStrategy(ai)` | StrategyContext.h:89 | LootNonCombatStrategy | `generic/LootNonCombatStrategy.h`; generic/LootNonCombatStrategy.cpp |
| cast time | `new CastTimeStrategy(ai)` | StrategyContext.h:111 | CastTimeStrategy | `generic/CastTimeStrategy.h`; generic/CastTimeStrategy.cpp |
| default | `new WorldPacketHandlerStrategy(ai)` | StrategyContext.h:100 | WorldPacketHandlerStrategy | `generic/WorldPacketHandlerStrategy.h`; generic/WorldPacketHandlerStrategy.cpp |

## Actions (12)

| Key | Creator in the full registry (`creators[...] =`) | Registry line | Class | Class file (header; out-of-line definitions) |
|---|---|---|---|---|
| reach spell | `new ReachSpellAction(ai)` | ActionContext.h:147 | ReachSpellAction | `actions/ReachTargetActions.h`; header only |
| reach party member to heal | `new ReachPartyMemberToHealAction(ai)` | ActionContext.h:151 | ReachPartyMemberToHealAction | `actions/ReachTargetActions.h`; header only |
| flee | `new FleeAction(ai)` | ActionContext.h:154 | FleeAction | `actions/MovementActions.h`; actions/MovementActions.cpp |
| shoot | `new CastShootAction(ai)` | ActionContext.h:160 | CastShootAction | `actions/GenericSpellActions.h`; actions/GenericSpellActions.cpp |
| drink | `new DrinkAction(ai)` | ActionContext.h:167 | DrinkAction | `actions/UseItemAction.h`; header only |
| food | `new EatAction(ai)` | ActionContext.h:166 | EatAction | `actions/UseItemAction.h`; header only |
| use | `new UseAction(ai)` | ChatActionContext.h:122 | UseAction | `actions/UseItemAction.h`; actions/UseItemAction.cpp |
| loot | `new LootAction(ai)` | ActionContext.h:172 | LootAction | `actions/LootAction.h`; actions/LootAction.cpp |
| open loot | `new OpenLootAction(ai)` | ActionContext.h:206 | OpenLootAction | `actions/LootAction.h`; actions/LootAction.cpp |
| move to loot | `new MoveToLootAction(ai)` | ActionContext.h:205 | MoveToLootAction | `actions/MovementActions.h`; actions/MovementActions.cpp |
| add all loot | `new AddAllLootAction(ai)` | ActionContext.h:175 | AddAllLootAction | `actions/AddLootAction.h`; actions/AddLootAction.cpp |
| store loot | `new StoreLootAction(ai)` | WorldPacketActionContext.h:67 | StoreLootAction | `actions/LootAction.h`; actions/LootAction.cpp |

## Triggers (26)

| Key | Creator in the full registry (`creators[...] =`) | Registry line | Class | Class file (header; out-of-line definitions) |
|---|---|---|---|---|
| enemy out of spell | `new EnemyOutOfSpellRangeTrigger(ai)` | TriggerContext.h:153 | EnemyOutOfSpellRangeTrigger | `triggers/RangeTriggers.h`; header only |
| enemy too close for spell | `new EnemyTooCloseForSpellTrigger(ai)` | TriggerContext.h:154 | EnemyTooCloseForSpellTrigger | `triggers/RangeTriggers.h`; header only |
| enemy five yards | `new EnemyInRangeTrigger(ai, "enemy five yards", 5.0f)` | TriggerContext.h:161 | EnemyInRangeTrigger | `triggers/RangeTriggers.h`; header only |
| medium mana | `new MediumManaTrigger(ai)` | TriggerContext.h:82 | MediumManaTrigger | `triggers/GenericTriggers.h`; triggers/GenericTriggers.cpp |
| no mana | `new NoManaTrigger(ai)` | TriggerContext.h:79 | NoManaTrigger | `triggers/GenericTriggers.h`; triggers/GenericTriggers.cpp |
| high mana | `new HighManaTrigger(ai)` | TriggerContext.h:83 | HighManaTrigger | `triggers/GenericTriggers.h`; triggers/GenericTriggers.cpp |
| medium health | `new MediumHealthTrigger(ai)` | TriggerContext.h:76 | MediumHealthTrigger | `triggers/HealthTriggers.h`; header only |
| healer should attack | `new HealerShouldAttackTrigger(ai)` | TriggerContext.h:88 | HealerShouldAttackTrigger | `triggers/GenericTriggers.h`; triggers/GenericTriggers.cpp |
| healer should wand | `new HealerShouldAttackTrigger(ai, "healer should wand", false)` | TriggerContext.h:89 | HealerShouldAttackTrigger | `triggers/GenericTriggers.h`; triggers/GenericTriggers.cpp |
| melee medium aoe and healer should attack | `new TwoTriggers(ai, "melee medium aoe", "healer should attack")` | TriggerContext.h:90 | TwoTriggers | `triggers/GenericTriggers.h`; triggers/GenericTriggers.cpp |
| medium threat | `new MediumThreatTrigger(ai)` | TriggerContext.h:167 | MediumThreatTrigger | `triggers/GenericTriggers.h`; triggers/GenericTriggers.cpp |
| target of attacker | `new TargetOfAttacker(ai)` | TriggerContext.h:248 | TargetOfAttacker | `triggers/GenericTriggers.h`; triggers/GenericTriggers.cpp |
| party member critical health | `new PartyMemberCriticalHealthTrigger(ai)` | TriggerContext.h:93 | PartyMemberCriticalHealthTrigger | `triggers/HealthTriggers.h`; header only |
| party member low health | `new PartyMemberLowHealthTrigger(ai)` | TriggerContext.h:94 | PartyMemberLowHealthTrigger | `triggers/HealthTriggers.h`; header only |
| party member medium health | `new PartyMemberMediumHealthTrigger(ai)` | TriggerContext.h:95 | PartyMemberMediumHealthTrigger | `triggers/HealthTriggers.h`; header only |
| party member almost full health | `new PartyMemberAlmostFullHealthTrigger(ai)` | TriggerContext.h:96 | PartyMemberAlmostFullHealthTrigger | `triggers/HealthTriggers.h`; triggers/HealthTriggers.cpp |
| party member to heal out of spell range | `new PartyMemberToHealOutOfSpellRangeTrigger(ai)` | TriggerContext.h:158 | PartyMemberToHealOutOfSpellRangeTrigger | `triggers/RangeTriggers.h`; header only |
| group heal setting | `new AoeInGroupTrigger(ai, "group heal setting", "almost full")` | TriggerContext.h:214 | AoeInGroupTrigger | `triggers/HealthTriggers.h`; triggers/HealthTriggers.cpp |
| medium group heal setting | `new AoeInGroupTrigger(ai, "medium group heal setting", "medium")` | TriggerContext.h:215 | AoeInGroupTrigger | `triggers/HealthTriggers.h`; triggers/HealthTriggers.cpp |
| resume drink | `new ResumeDrinkTrigger(ai)` | TriggerContext.h:84 | ResumeDrinkTrigger | `triggers/GenericTriggers.h`; triggers/GenericTriggers.cpp |
| resume eat | `new ResumeEatTrigger(ai)` | TriggerContext.h:85 | ResumeEatTrigger | `triggers/GenericTriggers.h`; triggers/GenericTriggers.cpp |
| loot available | `new LootAvailableTrigger(ai)` | TriggerContext.h:109 | LootAvailableTrigger | `triggers/LootTriggers.h`; triggers/LootTriggers.cpp |
| can loot | `new CanLootTrigger(ai)` | TriggerContext.h:206 | CanLootTrigger | `triggers/LootTriggers.h`; triggers/LootTriggers.cpp |
| far from loot target | `new FarFromCurrentLootTrigger(ai)` | TriggerContext.h:205 | FarFromCurrentLootTrigger | `triggers/LootTriggers.h`; triggers/LootTriggers.cpp |
| timer | `new TimerTrigger(ai)` | TriggerContext.h:65 | TimerTrigger | `triggers/GenericTriggers.h`; header only |
| loot response | `new WorldPacketTrigger(ai, "loot response")` | WorldPacketTriggerContext.h:36 | WorldPacketTrigger | `triggers/WorldPacketTrigger.h`; header only |

## Values (26)

| Key | Creator in the full registry (`creators[...] =`) | Registry line | Class | Class file (header; out-of-line definitions) |
|---|---|---|---|---|
| party member to heal | `new PartyMemberToHeal(ai)` | ValueContext.h:168 | PartyMemberToHeal | `values/PartyMemberToHeal.h`; values/PartyMemberToHeal.cpp |
| party member to dispel | `new PartyMemberToDispel(ai)` | ValueContext.h:194 | PartyMemberToDispel | `values/PartyMemberToDispel.h`; values/PartyMemberToDispel.cpp |
| buff targets | `new BuffTargetsValue(ai)` | ValueContext.h:354 | BuffTargetsValue | `values/TargetValue.h`; header only |
| patchwerk soaker to shield | `new PatchwerkSoakerShieldValue(ai)` | ValueContext.h:126 | PatchwerkSoakerShieldValue | `ai/RaidTankHealing.h`; RaidTankHealing.cpp |
| drink duration | `new DrinkDurationValue(ai)` | ValueContext.h:421 | DrinkDurationValue | `values/MaintenanceValues.h`; header only |
| eat duration | `new EatDurationValue(ai)` | ValueContext.h:422 | EatDurationValue | `values/MaintenanceValues.h`; header only |
| drink unfinished | `new DrinkUnfinishedValue(ai)` | ValueContext.h:418 | DrinkUnfinishedValue | `values/MaintenanceValues.h`; header only |
| rest to full | `new RestToFullValue(ai)` | ValueContext.h:419 | RestToFullValue | `values/MaintenanceValues.h`; header only |
| should drink | `new ShouldDrinkValue(ai)` | ValueContext.h:417 | ShouldDrinkValue | `values/MaintenanceValues.h`; header only |
| should eat | `new ShouldEatValue(ai)` | ValueContext.h:420 | ShouldEatValue | `values/MaintenanceValues.h`; header only |
| spell ready | `new SpellReadyValue(ai)` | ValueContext.h:255 | SpellReadyValue | `values/SpellCastUsefulValue.h`; values/SpellCastUsefulValue.cpp |
| custom position | `new CustomPositionValue(ai)` | ValueContext.h:274 | CustomPositionValue | `values/PositionValue.h`; header only |
| nearest npcs no los | `new NearestNpcsValue(ai, sPlayerbotAIConfig.sightDistance, true)` | ValueContext.h:142 | NearestNpcsValue | `values/NearestNpcsValue.h`; values/NearestNpcsValue.cpp |
| loot target | `new LootTargetValue(ai)` | ValueContext.h:220 | LootTargetValue | `values/AvailableLootValue.h`; header only |
| available loot | `new AvailableLootValue(ai)` | ValueContext.h:221 | AvailableLootValue | `values/AvailableLootValue.h`; header only |
| has available loot | `new HasAvailableLootValue(ai)` | ValueContext.h:222 | HasAvailableLootValue | `values/HasAvailableLootValue.h`; header only |
| can loot | `new CanLootValue(ai)` | ValueContext.h:219 | CanLootValue | `values/AvailableLootValue.h`; header only |
| always loot list | `new AlwaysLootListValue(ai, "always loot list")` | ValueContext.h:225 | AlwaysLootListValue | `values/AlwaysLootListValue.h`; values/AlwaysLootListValue.cpp |
| skip loot list | `new AlwaysLootListValue(ai, "skip loot list")` | ValueContext.h:226 | AlwaysLootListValue | `values/AlwaysLootListValue.h`; values/AlwaysLootListValue.cpp |
| should loot object | `new ShouldLootObject(ai)` | ValueContext.h:224 | ShouldLootObject | `values/LootValues.h`; values/LootValues.cpp |
| stack space for item | `new StackSpaceForItem(ai)` | ValueContext.h:223 | StackSpaceForItem | `values/LootValues.h`; values/LootValues.cpp |
| force item usage | `new ForceItemUsageValue(ai)` | ValueContext.h:312 | ForceItemUsageValue | `values/ItemUsageValue.h`; header only |
| nearest corpses | `new NearestCorpsesValue(ai)` | ValueContext.h:152 | NearestCorpsesValue | `values/NearestCorpsesValue.h`; values/NearestCorpsesValue.cpp |
| loot strategy | `new LootStrategyValue(ai)` | ValueContext.h:228 | LootStrategyValue | `values/LootStrategyValue.h`; values/LootStrategyValue.cpp |
| should get money | `new ShouldGetMoneyValue(ai)` | ValueContext.h:392 | ShouldGetMoneyValue | `values/BudgetValues.h`; header only |
| inventory item ids | `new InventoryItemIdValue(ai)` | ValueContext.h:243 | InventoryItemIdValue | `values/ItemCountValue.h`; header only |

Keys that share a class: `healer should attack` / `healer should wand` (HealerShouldAttackTrigger, different arguments), `group heal setting` /
`medium group heal setting` (AoeInGroupTrigger), `always loot list` / `skip loot list` (AlwaysLootListValue), `can loot` appears as a trigger and as a value.

## What stays unregistered after these 69

`slice_registry_check.py` (b) run over `SLICE2.txt` (the same parsers, list switched, nothing written): keys the slice-2 sources name by string that are in
neither the slice's registry, the warrior's nor the priest's context and not in the 69. Slice 1's own unregistered list (164 keys) is not repeated; this is
the part that is NEW with the 27 files.

**strategys: 0** (0 of them only from `WorldPacketHandlerStrategy.cpp`)


**actions: 47** (31 of them only from `WorldPacketHandlerStrategy.cpp`)

`add gathering loot`, `apply oil`, `auto loot roll`, `destroy all gray` [not in the full build], `faction`, `fish`, `go`, `loot roll`, `move out of collision`, `move to fish`, `reveal gathering item`, `set pet stance`, `smart destroy item`, `talk to quest giver`, `use fishing bobber`, `world buff`

From `WorldPacketHandlerStrategy.cpp` only: `accept invitation`, `accept summon`, `accept trade`, `add loot`, `area trigger`, `auto learn spell`, `check mail`, `finish ready check`, `guild accept`, `honor gain`, `log roll won`, `petition sign`, `pool gear top up`, `pool money top up`, `query item usage`, `reach area trigger`, `ready check`, `ready reply`, `release`, `remember taxi`, `reset ai soft`, `revive from corpse`, `taxi`, `tell cannot equip`, `tell not enough money`, `tell not enough reputation`, `trainer`, `uninvite`, `unlock traded item`, `use meeting stone`, `xp gain`

**triggers: 36** (28 of them only from `WorldPacketHandlerStrategy.cpp`)

`at war`, `collision`, `equipment audit`, `loot roll`, `need world buff`, `new pet`, `party member dead`, `val`

From `WorldPacketHandlerStrategy.cpp` only: `activate taxi`, `area trigger`, `cannot equip`, `check mount state`, `force rebuff pending`, `gossip hello`, `group invite`, `group set leader`, `guild accept`, `honorgain`, `item push result`, `levelup`, `loot roll won`, `not enough money`, `not enough reputation`, `petition offer`, `ready check finished`, `release spirit`, `revive from corpse`, `summon request`, `taxi done`, `trade status`, `trade status extended`, `uninvite`, `uninvite guid`, `use game object`, `within area trigger`, `xpgain`

**values: 2** (0 of them only from `WorldPacketHandlerStrategy.cpp`)

`item usage`, `skip go loot list`

**Finding for the lead.** The spec's "not registered" list names 12 keys. The measured list is longer because the `default` strategy (`WorldPacketHandlerStrategy`, in the 5)
installs about 50 packet triggers and actions (invitations, trade, taxi, ready check, petitions, summon, area triggers, XP/honor): each is a missing-key log line at registration
and the engine skips it. Options for the generator package: keep `default` and let those lines be counted and explained (the spec's rule: every `slice: missing` line explained), or leave `default` out
of the factory (then `WorldPacketHandlerStrategy.cpp`, 187 lines, drops out of the list as well; `loot response` and `store loot` are then reached only if another strategy maps them, which is the lead's call, since section 5 reads loot from the core's Loot object after the open).
The stand-in `SliceInertValue` (SliceMissingKeyGuard.h) must cover two more value types for the unregistered value keys: `ItemUsage` (`item usage`, as the spec says) and `std::set<uint32>&` (`skip go loot list`, read at `LootObjectStack.cpp`).
The types of registered keys need nothing from the guard.
