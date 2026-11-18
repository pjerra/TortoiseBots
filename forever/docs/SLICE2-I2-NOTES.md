# Slice 2, package I2: loot (money only) and item use (food and drink)

Branch `slice2/i` (worktree `~/wt-slice2-i`), build slot 2, off `d97cc42b9`. T = Turtle core, F = ForeverCore at the pin 6517af05 (`~/ForeverCore`, paths below
are under `src/server/game/` unless they start with `src/`). Lead decisions applied: D2 (loot money only; items, gathering, skinning, quest items, rolls are named
unported features), D3 (food and drink through the core's own item cast; every other item use decided one by one). No boot, no CMake edit, no host file.

## 1. Census

`FOREVER_SLOT=2 FOREVER_CENSUS_LIST=forever/SLICE2-I2.txt forever/tools/vm-census.sh "" 14`

| | files | error places |
|---|---|---|
| before | 3 files, 5 files with errors | **180** (LootAction.cpp 76, UseItemAction.cpp 63, QuestValues.h 16 (V2's), CorpseLootPolicy.h 13, AddLootAction.cpp 12) |
| after | 2 of 3 compile clean | **16**, all in `QuestValues.h` (V2 owns it; it keeps the third file, one of the three, from reporting clean) |

My four (LootAction.cpp, AddLootAction.cpp, UseItemAction.cpp, CorpseLootPolicy.h): 164 places -> 0. UseItemAction.h compiled before and after (it was not in the error list).

How the 164 were closed: (a) a name that means the same: 3 (`LOOT_SLOT_NORMAL`, `TEXTEMOTE_CHEER`, `FindItemByEntryCompat` for F); (b) a both-core helper with the T side identity: the
`std::string` through `%s`/`%lu` varargs (about 60 places: `CStrCompat`, `GuidKeyCompat`, both existing), the four packet handlers (open, release, money, autostore), the response head, the
loot slot facts, `GroupIdKeyCompat`, `LootDistanceCompat`, `GetSelectionGuid`, `GuidFromRawCompat`, `HasSpellCategoryCooldownCompat`, `getSource`, `CreatureNameCompat`, `SendAddonMessageCompat`
(existing), `QueueGameObjectUseCompat`, `OpenItemCompat`, the item loop and the lock check (below); (c) the two cores answer differently: section 6; (d) not ported: section 5.

## 2. Money looting, done through the core (what a client's packets end in)

The Turtle side of every helper is the packet the legacy code built and the handler it handed it to, moved word for word (`ai/cmangos-compat-s2i.h`).
The ForeverCore side (`forever/src/compat/TbCompat_I2.h`):

| Step | Legacy (T) | F call | Precondition / note |
|---|---|---|---|
| open the window | `CMSG_LOOT` -> `HandleLootOpcode` (LootAction.cpp `DoLoot`) | `LootOpenCompat`: typed `WorldPackets::Loot::LootUnit{Unit=guid}` -> `WorldSession::HandleLootOpcode` LootHandler.cpp:213-259 | alive player + creature guid (:216), `IsValidLootTarget` (:224 -> :53-62: corpse dead, within `LootDistance` 30 yd (:44), `Player::isAllowedToLoot` Player.cpp:20246-20288), cast interrupt (:227-230), then `Player::SendLoot(*GetLootForPlayer(player))` :246 -> loot view `m_AELootView`, `SetLootGUID`, `UNIT_FLAG_LOOTING` (Player.cpp:10019-10046). **A refusal sends nothing**: the helper reads the answer back from `GetLootByWorldObjectGUID(guid)`; false = a failed open (OpenLootAction counts it, three failures abandon the corpse). With `CONFIG_ENABLE_AE_LOOT` the core opens up to 49 more corpses (:233-251) |
| the "loot response" event | the server's `SMSG_LOOT_RESPONSE`, delivered by the packet handler table | the same helper raises it: `GetTrigger("loot response")->ExternalEvent(packet)`, packet = owner guid (the event `WorldPacketTrigger` hands to `StoreLootAction`) | see "What the `loot response` event needs from the host" below |
| read the window | `p >> guid, loot_type, gold, items` (`StoreLootAction::Execute`), `sLootMgr.GetLoot(bot)` | `LootWindowReadCompat`: guid from the event, `Loot::gold` and `loot_type` from `Player::GetLootByWorldObjectGUID` (Player.cpp:9927; Loot.h:298, :301); `items` = 0 on purpose | no window of that guid = the T "loot error" branch (corpse abandoned or unqueued) |
| take the money | `CMSG_LOOT_MONEY` -> `HandleLootMoneyOpcode` | `LootMoneyCompat`: typed `LootMoney` -> `HandleLootMoneyOpcode` LootHandler.cpp:142-209 | walks the whole loot view; for a corpse window of a grouped player the gold is split over the group members that `HasAllowedLooter` and `IsAtGroupRewardDistance` (:150-179). **The split divides by that count (:168): zero would crash the server.** The helper repeats the same membership test first (self always passes, Player.cpp:28052-28065) and refuses (false) a window that fails it; the call site then sets `gold = 0` (the window "gave nothing" -> abandon) |
| release | `CMSG_LOOT_RELEASE` -> `HandleLootReleaseOpcode` | `LootReleaseCompat`: typed `LootRelease{Unit=guid}` -> LootHandler.cpp:261-267 -> `DoLootRelease` :269-404 | finds the loot by OWNER guid, an unknown guid does nothing; used 5 times (select another target, no loot object, end of window, "release loot" over objects and over corpses) |
| the loot object of the window | `sLootMgr.GetLoot(bot)`, `bot->SetLootGuid` | `GetLootByWorldObjectGUID`; `SetLootGuid` is a no-op (SendLoot set `LootTargetGUID`) | |

**Map context and threading.** All four handlers are `PROCESS_THREADUNSAFE` (Opcodes.cpp:690-693), i.e. the core itself runs them on the world thread. The engine ticks on the world thread
right before the maps update (World.cpp:2465 `OnWorldBeforeMapsUpdate`, :2475 `sMapMgr->Update`; R0 notes 2.2). So: `ObjectAccessor::GetCreature(*player, guid)` (:219), the AE corpse search
`Cell::VisitGridObjects` (:239) and `GetMap()->GetCreature/GetGameObject` in `DoLootRelease` (:354, :377) read the bot's map while no map worker runs; the loot writes (`OnLootOpened`,
`NotifyMoneyRemoved`, `ModifyMoney`, `ForceUpdateFieldChange`) are made by the one thread that may. Not run: this is the same assumption as findings item 2.

**What the `loot response` event needs from the host: nothing.** `LootOpenCompat` raises the event itself, into the bot's own trigger, the way the T packet handler table does (the event the
`default` strategy maps to `store loot`, WorldPacketHandlerStrategy.cpp:101-103). It needs (a) the trigger `loot response` and the action `store loot` registered, and the `default`
strategy active (D6 list; they are in the spec's 12), (b) nothing in `TbBotSession` (SMSG_LOOT_RESPONSE is not queued, R0 notes 3.1), (c) no change in PlayerbotAI. Not delivering while the
previous event is unread is deliberate: one event per trigger. **If the merge package prefers the real packet:** queue `SMSG_LOOT_RESPONSE` (`LootResponse::Write`, `WorldPackets::Loot`) in the session
like the cast results, and from the drain call `trigger->ExternalEvent(packet)` with a packet holding the `Owner` guid; the helper would then stop raising it (a one-line change in `LootOpenCompat`).
The unported row `incoming: loot` still lists "loot response" in `TbCompat_Unported.cpp:86` (one count per engine): suggest dropping that name from the group.

## 3. Item use: food and drink (decision D3), and what the spike must confirm

`CastItemSpellCompat<BotUseItemSpell>` (F) replaces the loop body of `UseAction::UseItemInternal` (T: the body moved word for word, `ai/cmangos-compat-s2i.h`). For food or drink
used from the bags on the bot itself, whose spell targets the caster, F calls `Player::CastItemUseSpell(item, spellId, targets, ObjectGuid::Empty, {0,0,0})` (Player.cpp:9707-9757), the call
`ProcessItemCast` ends in (:33833) when `WorldSession::HandleUseItemOpcode` (SpellHandler.cpp:50-64, `PROCESS_INPLACE`, Opcodes.cpp:1163) runs a client's `CMSG_USE_ITEM`. It consumes the item when the
cast finishes; the call returns nothing, so the helper reads: a refusal during this tick (`BotSession::TakeInTickRefusal`, R0 notes 3.3, the sink catches `SMSG_CAST_FAILED` from `Spell::prepare`),
the item count dropping, or a current spell / the spell's aura. Sitting: the core sits a unit that gets an aura with the `Standing` interrupt flag (Unit.cpp:3801-3802), so no sit call is made.

`ProcessItemCast`'s own checks (:33753-33826: item in the bags, equipped-only items, `CanUseItem`, arena, combat rules, binding on use, `OnItemUse` script, the `ItemUse` aura interrupt) are
not repeated: `UseItemInternal` already tests `CanUseItem`, the cooldown, the item and combat before the loop. Not done: the script hook and `RemoveAurasWithInterruptFlags(ItemUse)`.

**What a ten-minute live spike must confirm** (the first package's recipe U1, SLICE2-R0-NOTES.md section 5, with its `.tb additem` and `.tb use` commands; this helper differs from it in two points and one
check is new):
1. Water entry from `.tb item`: `legacy no`, and an `ON_USE` effect whose spell is the drink spell. A LEGACY item makes the core ignore the effect (Player.cpp:9718-9744); then drinking is dropped (D3).
   The helper makes the same test first and refuses with the feature count `item use except food and drink`, so `.tb unported` shows it.
2. The mirror says it is a drink: `proto->Spells[0].SpellCategory == 59` (food 11), class consumable, subclass `ITEM_SUBCLASS_FOOD_DRINK` (forever-compat-shim.h:249 `ITEM_SUBCLASS_FOOD`). `IsFood/IsDrink` are what let the item
   through the gate (`ItemUseUnportedCompat`); if they read false for real water the bot drinks nothing and `.tb unported` counts `item use except food and drink`. Categories 11/59 are the 1.12 numbers (ItemEffect.SpellCategoryID is a client table): UNCHECKED.
3. The drink spell in the mirror: `Targets == 0` and `EffectImplicitTargetA[0] == TARGET_UNIT_CASTER` (F ids). Otherwise the helper refuses (count).
4. **New (differs from R0 recipe):** the helper passes an EMPTY `SpellCastTargets` (a client sends none for a self spell) where the recipe used `SetUnitTarget(bot)`. If the core answers `SPELL_FAILED_BAD_TARGETS`, try
   `targets.SetUnitTarget(bot)` and change the helper.
5. Run it through the ENGINE, not the console (`.tb engine on` priest, `.tb power P mana 10`, water in the bags): the in-tick refusal is only captured inside `EngineTickScope`; from the console a refusal goes to the queue.
   Expect `A:drink - OK`, the bot seated, the drink aura within 1 s, mana rising about every 3 s, the item count -1 at the cast (or at the end: note which), and no `CAST_FAILED_ASYNC`.
6. Provoke a refusal (drink in combat, or twice at once): the action log shows the refusal code (`NoteCastCheckFail`) and the action reports failure.
7. The last item of a stack: no crash (the helper never touches the item after the call).
8. Moving bot: the helper stops it first (`StopMoving`); whether the core cancels the cast on a spline-moved bot is U3.

## 4. Tables and sizes checked (the review lessons)

* Item classes/subclasses: only through names (`ITEM_CLASS_CONSUMABLE`, `ITEM_SUBCLASS_FOOD` = F's `FOOD_DRINK` by the shim, `ITEM_SUBCLASS_SCROLL`, `ITEM_SUBCLASS_EXPLOSIVES` on the T-only target path); no number indexes a table.
* `proto->Spells[]` is 5 slots by `LegacySlotIndex` (TbItemPrototype.cpp:318-331): every loop is bounded by `MAX_ITEM_PROTO_SPELLS`. `spellInfo->Effect[]`, `EffectImplicitTargetA[]`: 3 slots, loops bound by `MAX_EFFECT_INDEX`
  (`UseItemAction.cpp` target scan, the door lock scan stops at `EFFECT_INDEX_2`); the F side reads index 0 only, after the mirror was filled. No `GetEffect(i)` call anywhere in the three files.
* `LockEntry::Type[8]` loops (`CanOpenLock`, `GetNodeGatherSkill`, the door branch, `DoorLock::kLockSlots`) are bounded by 8: F's `LockEntry` has `MAX_LOCK_CASE` = 8 slots (DB2Structure.h:2889-2899). Left as written: the whole game-object path is unported (section 5).
* `MAX_QUEST_LOG_SIZE` slot loop in `IsLootAllowed`: unreachable on F (the function returns "not allowed" first, `ItemLootingUnportedCompat`); `ITEM_QUALITY_*`: compared by name; `loot` quality 8 values on F, the code tests `>=` rare/uncommon only.
* Guids: the 128-bit guid becomes a key only through `GuidKeyCompat` (the remote-commander loot orders, the stack's give-up memory, the blocked-tell key), never `GetRawValue()`. `std::to_string(guid)` in a debug text is `std::to_string(GuidKeyCompat(guid))`.
* `printf` into `sLog.outDebug` with a `std::string` (`bot->GetName()`): `CStrCompat` at 34 sites (25 in LootAction.cpp, 9 in AddLootAction.cpp). No `PQuery`/`PExecute` in the three files.

## 5. Named unported features (the bot does LESS) and every call site

Rows added to `UnportedFeatures.h` (one block, `/* --- slice 2, package I2 --- */`, at the end of the table; the helpers are at the end of `TbCompat_I2.h`, not in `TbCompat_Unported.h`, so the two packages cannot conflict there):

| Feature (row) | Failure value on F | Call sites and what each does with it |
|---|---|---|
| `loot: items, quest items and rolls` (`LootItems`) | window item count 0; slot facts false; autostore a no-op; `IsLootAllowed` false; roll never succeeds | `StoreLootAction::Execute`: `LootWindowReadCompat` reports 0 items, the item loop does not run; the verdict `DecideAfterLootWindow(false, itemsTaken \|\| gold, ...)` is "paid out" with money and **abandon** without (an items-only corpse is abandoned: fewer opens). A corpse with money AND items is opened once more by the next sweep (its money is gone: gold 0, abandon). `IsLootAllowed`: "not allowed", the rule's own example. `ApplyBotLootBonus` (random bots only): the block is counted and skipped (UnportedBlockCompat). `LootSlotFactsCompat`, `LootAutostoreCompat`, `RollChanceCompat`: unreachable with items = 0, counted if reached |
| `loot: gathering, skinning and game objects` (`LootGather`) | `DoLoot` returns false | `OpenLootAction::DoLoot` after the body-loot branch: gather/skin casts and game-object opening are not run; `OpenLootAction::Execute` counts every `false` as an open attempt, and three abandon the object for 120 s (`NoteApproachFailure`/`IsAbandoned`), so the stack cannot spin on it |
| `item use except food and drink` (`ItemUseOther`) | refuse and report failure | `UseAction::UseItemInternal` top (`ItemUseUnportedCompat`): opening (`ITEM_FLAG_HAS_LOOT`), quest-giver items, hearthstone, potions, healthstones, bandages (also on others), grenades, jujus, scrolls, gems, enchants, keys, recipes, hand-given items: all go through `UseItemInternal` and answer the usual `use_command_item_error`, action false. Food/drink with an explicit target (unit, game object, item), a second on-use spell, a non-self spell, a LEGACY item and a non-bot session: `CastItemSpellCompat` refuses (count). `OpenItemCompat`: false |
| existing `game object use` (`GameObjectUse`) | refuse | `UseAction::UseGameObject` top (`GameObjectUseUnportedCompat`): "I can't use ..." and false for every game object use (doors, locks, chests, quest objects); `QueueGameObjectUseCompat` and `GameObjectNoInteractCompat` are behind it |
| existing `item and totem casts` (`ItemCasts`) | `SPELL_FAILED_ERROR` / false | `BotUseItemSpell::ForceSpellStart` (generic lambda over `self`, the Spell internals are looked at on T only) and `OpenLockCheck` (friend template `BotUseItemOpenLockCheckCompat`); not reached on F, since F never builds a `BotUseItemSpell` |
| existing `quest internals` (`QuestInternals`) | no requirement | `IsLootAllowed` quest loop (`QuestRequiredItemCompat`), behind the item gate |

Proper on F (no counter): `CorpseHasBodyLootFor` (`Player::isAllowedToLoot`), `HasVisiblePersonalItemFor` (`Loot::hasItemFor`, the same test `isAllowedToLoot` makes for another member's turn, Player.cpp:20271, :20284),
`LootDistanceCompat` (30 yd, LootHandler.cpp:44), `GroupIdKeyCompat`, `FindItemByEntryCompat`.

## 6. Different answers (one line each, for the live test)

* A bot in a party shares a corpse's gold with the allowed party members that are near (the master gets a part): the core's rule for every player (LootHandler.cpp:150-179); on T the same split existed through the 1.12 core.
* A corpse another player tapped is refused by `isAllowedToLoot` (the raw lootable flag is no longer ORed in, `CorpseHasBodyLootForCompat`).
* AE loot: with `CONFIG_ENABLE_AE_LOOT` one open also opens up to 49 other corpses; the money of all of them is taken with the first window's money; only the named one is released by `StoreLootAction`.
* An open the core refuses is silent (no loot error packet): the bot learns it from the empty loot view and counts a failed open; T heard a `TOO_FAR`/`DIDNT_KILL` code and abandoned at once.
* `HasVisiblePersonalItemFor`: F answers `Loot::hasItemFor(viewer)` (quest items and FFA items of the viewer), T the per-slot round-robin rule of the 1.12 loot view.
* Food/drink on F: no `OnItemUse` script, no `ItemUse` aura interrupt, no second on-use spell; the cast is not triggered.
* Debug text of the loot lines prints the guid key (a hash on F, the raw 64 bits on T).

## 7. NEEDS FROM / OFFERED

NEEDS FROM the merge package:
1. `UNIPORTED.md`: the three new rows (text in `UnportedFeatures.h`), and the count column from `.tb unported`.
2. `TbCompat_Unported.cpp:86`: the `IncomingLoot` group still lists `"loot response"`; with this package the event is raised by the open, so drop that name (the registration count of one per engine is then a false "not served").
3. V2: `LootObjectStack.cpp` must build on F with `LootAccess(loot).lootType()` = `loot->loot_type` (StoreLootAction reads it for the gather-skill decision), `Loot::hasItemFor` (exists, Loot.h:349), and must not queue a game object or a skinnable corpse for a bot that cannot gather (they only cost three failed opens each). V2 owns QuestValues.h: its 16 places are the only ones left in my census.
4. A2: the strategy `default` (WorldPacketHandlerStrategy.cpp) must register the trigger `loot response` and the action `store loot` (the spec's list has both).
5. `item usage`: `LootStrategyValue::CanLoot` reads `ItemUsage` through `StoreLootAction::IsLootAllowed`, which returns before it on F.

OFFERED (the lead dedupes): `FindItemByEntryCompat` (F; T exists in `ai/cmangos-compat-shim.h:69`), `GetRequiredLootSkillCompat` (F; T exists in shim.h:59), `GroupIdKeyCompat`, `LootDistanceCompat`,
`CorpseHasBodyLootForCompat`, `PersonalItemVisibleCompat`, `LootOpenCompat`/`LootReleaseCompat`/`LootMoneyCompat` (any package that opens or closes a loot window), `CastItemSpellCompat` (item use by the core).

## 8. Left open

* Items, gathering, skinning, game-object opening, quest items, rolls: section 5 (named, counted).
* Not run: no boot. Everything under 2 and 3 is read from the core, never executed; the spike in section 3 is the first run.
* `LootOpenCompat` needs the trigger object (`GetTrigger("loot response")`) to exist when the open is made; if the `default` strategy is off the helper does not open (false).
* Hot path: `CorpseHasBodyLootForCompat` and `HasVisiblePersonalItemFor` run on every stack refresh; neither counts.

## 9. Wiring pins re-pinned (each reads the new spelling of the SAME expression)

* `tools/test_loot_order_behaviour.py` (g++ harness of AddLootAction): gains the Turtle bodies of `GuidKeyCompat`, `CStrCompat`, `GroupIdKeyCompat`.
* `tools/test_loot_slot_right_wiring.py`: the slot read `LootItemInSlot(itemindex, ..., &qitem, &ffaitem)` and `GetSlotTypeForSharedLoot` are pinned in `LootSlotFactsCompat` (cmangos-compat-s2i.h); LootAction.cpp must call it and then `ai::BotMayTakeLootSlot(...)` with its five facts.
* `tools/test_loot_visible_personal.py` (g++ harness): gains the Turtle body of `PersonalItemVisibleCompat`; it still compiles and runs the production algorithm (now a generic lambda).
* `tools/test_no_cached_spell_target.py`: the allowlisted read `GameObject* const go = m_targets.getGOTarget();` moved with `OpenLockCheck` to `ai/cmangos-compat-s2i.h` as `self->m_targets.getGOTarget();` (reason marker moved with it).
* `tools/test_quest_source_item_wiring.py`: `UNIT_DYNFLAG_LOOTABLE` / `IsAllowedToLoot(creature)` are pinned in `CorpseHasBodyLootForCompat` (T header); the policy must call it.

## 10. Gates

See the final report of the package (census, option-off module build, slice-1 census, Turtle `vm-build` and `vm-test tb`).
