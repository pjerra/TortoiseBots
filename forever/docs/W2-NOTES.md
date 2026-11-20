# W2 Notes: Vendor, Repair, Trainer, and Mail Read (`D-w2-vendor-repair-trainer`)

Branch: `feat/w2-town` (based on `feat/world-life`, commit `f296224b9`).

## What was done

1. **Pure policy (`ai/playerbot/policy/town/TownErrandPolicy.h`)**:
   - `VendorItemPickAllowed`: gates vendor items on `PlayerConditionId`, `ExtendedCost`, vendor conditions, reputation rank, and limited-stock count before calling `BuyItemFromVendorSlot`.
   - `ShouldSellItemW2`: greys only (`quality == 0`), never quest items (`itemClass == 12`), never soulbound or equipped items, non-zero sell price.
   - `ShouldRepairW2`: autonomous threshold (`< 30%` average or `< 10%` lowest) vs ordered (`< 100%`).
   - `TrainerRankDueAndAffordable`: checks known status, class/race fit, level, prerequisites, and reputation-discounted copper cost.
   - `SupplyTargetForLevelAndClass`: food, water (mana classes), and ammo (hunters) targets by level bracket from `forever/data/waterys/supplies-by-level.csv`.

2. **Host seam (`forever/src/host/TbHostTown.h`, `forever/src/host/TbHostTown.cpp`)**:
   - `tbhost::town::NpcIfCanInteract`: calls `Player::GetNPCIfCanInteractWith(guid, flags, UNIT_NPC_FLAG_2_NONE)`.
   - `tbhost::town::SellItem`: calls `CanSellItemToVendor` + `SellItemToVendor`, logs `[W2] sell` / `[W2] sell-refused`.
   - `tbhost::town::CheckVendorItemAllowed` + `BuyItemFromVendorSlot`: enforces `PlayerConditionId` (`MeetPlayerCondition`), `ExtendedCost == 0`, `sConditionMgr->IsObjectMeetingVendorItemConditions`, reputation, and stock; verifies bag count increased after `Player::BuyItemFromVendorSlot` (never reports a buy not seen in bags); logs `[W2] buy` / `[W2] buy-refused`.
   - `tbhost::town::RepairAll`: repairs weapons and `DurabilityRepairAll(true, discountMod, false)` (`guildBank=false`), logs `[W2] repair`.
   - `tbhost::town::IsTrainer`, `IsTrainerOf`, `GetTrainerSpells`, `CanTeachTrainerSpell`, `TeachTrainerSpell`: uses `Creature::GetTrainerId()` + `sObjectMgr->GetTrainer(id)` + `Trainer::TeachSpell(npc, bot, spellId)`, logs `[W2] train` / `[W2] train-refused`.
   - `tbhost::town::GetMails`, `GetMailItem`, `GetUnreadMailCount`, `TakeMailMoney`, `TakeMailItem`, `DeleteMail`: reads `Player::GetMails()` / `GetMItem()` directly (no `MasterPlayer`), dispatches `WorldPackets::Mail::MailTakeMoney` / `MailTakeItem` / `MailDelete` through `WorldSession::HandleMail*`, logs `[W2] mail`.

3. **Compat pair (`ai/playerbot/TbCompatW2Town.h`, `forever/src/compat/TbCompat_W2Town.h`, `ai/cmangos-compat-w2town.h`, `forever/compat/MapNodes/MasterPlayer.h`)**:
   - Wraps vendor, repair, trainer, mail, and durability DB2 accessors (`WeaponSubClassCost`/`ArmorSubClassCost`, `DurabilityQualityEntry::Data`). Turtle side preserves the original expressions with zero behaviour change.

4. **Legacy slice & context (`forever/SLICE9.txt`, `forever/tools/slice_gen_context.py`, `forever/src/slice/SliceLinkStubs.cpp`)**:
   - Added `SellAction.cpp`, `BuyAction.cpp`, `RepairAllAction.cpp`, `TrainerAction.cpp`, `MailAction.cpp`, `BudgetValues.cpp` to `forever/SLICE9.txt`.
   - Added `W2_KEYS` to `forever/tools/slice_gen_context.py` registering actions `sell, buy, repair, trainer, mail` and budget values `free money for, total money needed for, money needed for, repair cost, max repair cost, min repair cost, has all money for`.
   - Added the `"W2"` block in `forever/src/slice/SliceLinkStubs.cpp` for `ShouldSellValue::CantAffordNextSpell`, `ai::ParkVendorErrand`, and `TravelMgr::SetNullTravelTarget`, and removed the superseded `RepairCostValue::RepairCost` stub.

5. **Ordered commands & state (`forever/src/host/TbOrders.cpp`, `forever/src/host/TbPartyState.cpp`)**:
   - Added `.tb order <bot|party [leader]> sell|buy|repair|train|mail [arg] [#token]` (v2 reply grammar; defaults: `sell` -> `gray`, `buy` -> `vendor`, `train` -> `learn`, `mail` -> `take`).
   - Appended `dur=<pct>` and `mail=<unread>` to `.tb state` (`money=` and `bagfree=` were already present).

## Gate results

- **Census (`forever/SLICE9.txt`)**:
  `files 274  compile clean 274  with errors 0  error places 0 (in 0 files)  includes of a header ForeverCore does not have 0`
- **`tortoisebots` build (`TORTOISEBOTS_SLICE1=ON`)**:
  `[3/3] Linking CXX static library src/server/modules/TortoiseBots/libtortoisebots.a` (exit 0)
- **`tortoisebots` build (`TORTOISEBOTS_SLICE1=OFF`)**:
  `[75/75] Linking CXX static library src/server/modules/TortoiseBots/libtortoisebots.a` (exit 0)
- **Registry & context checks**:
  `summary: (a)=0  (b)=234 keys to register or stub later`
  `SliceAiObjectContext.cpp is up to date`
- **Pure tests**:
  `PASS: test_w2_town (7 checks, 0 failed)`
  `RESULT: PASS (209 checks)` (`test_run_orders.cpp`)
  `test_run_tasks: 148 checks, 0 failed` (`test_run_tasks.cpp`)
  `RESULT: PASS: 28 checks, 0 failed` (`test_run_c6.cpp`)
  `cmd_binding_check: 57 checks, 0 failed` (`cmd_binding_check.cpp`)
- **Wiring test (`forever/tools/test_w2_town_wiring.py`)**:
  `Ran 5 tests in 0.001s` -> `OK`
  `--mutate` -> `FAILED (failures=1)` (`mutate_exit=1`)

## Live test steps (for combined lead run)

### Alliance (Goldshire)
Position bot `Testbot` (e.g. level 10 warrior/mage) at Goldshire near vendor, smith, class trainer, and mailbox, with grey junk in bags, damaged gear, copper, and 1 mail with money + item:
```
tb engine Testbot on
tb state Testbot #s0
tb order Testbot sell #o1
tb order Testbot buy vendor #o2
tb order Testbot repair #o3
tb order Testbot train #o4
tb order Testbot mail #o5
tb state Testbot #s1
```
Expected:
- Each `tb order` returns `order bot=Testbot verb=<verb> seq=<n> status=queued #o<k>` and `order-end ... queued=1 refused=0 #o<k>`.
- `tb state Testbot #s1` shows `ord=<n>:ok` and updated `money=`, `bagfree=` (increased after `sell`), `dur=100` (after `repair`), `mail=0` (after `mail`).
- Server log contains `[W2] sell bot=Testbot ...`, `[W2] buy bot=Testbot ...`, `[W2] repair bot=Testbot ...`, `[W2] train bot=Testbot ...`, `[W2] mail bot=Testbot ...`.
- Any vendor item with `PlayerConditionId != 0` or `ExtendedCost != 0` logs `[W2] buy-refused ... reason=player-condition:...` or `extended-cost:...` and is skipped.

### Horde (Razor Hill)
Repeat the same sequence with a Horde bot positioned at Razor Hill near vendor, smith, class trainer, and mailbox.

## What waits for W1

- Autonomous town triggers/strategies (`RpgStrategy`, `RpgVendorStrategy`, `RpgMaintenanceStrategy`, `TravelStrategy`, `MaintenanceValues.cpp`, `VendorValues.cpp`, `TrainerValues.cpp`, and the 300-yard current-zone navmesh walk on masterless `free` bots).
- `ShouldSellValue::CantAffordNextSpell`, `ai::ParkVendorErrand`, and `TravelMgr::SetNullTravelTarget` remain stubbed in `SliceLinkStubs.cpp` (`"W2"` block) until W1/W4 ports `MaintenanceValues` and `TravelMgr`.
