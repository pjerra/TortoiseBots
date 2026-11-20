# Review Fixes Notes: Mail Compat, Gathering Order/Timer, Gear Swap Cleanup (`fix/wl2r-misc`)

Branch: `fix/wl2r-misc` (based on `feat/world-life-2`).

## Findings Fixed

### Finding 11 (MEDIUM — Turtle regression in `MailAction.cpp`)
- **What was wrong**: `TakeMailProcessor::Process` (`MailAction.cpp:186`) aborted on `mailbox.IsEmpty()`, and `TakeMailProcessor::After` (`MailAction.cpp:284`) returned `confirmedTakes > 0`, which returned `false` for text-only mail (no money, no items) on Turtle where it previously returned `true`.
- **Fix**: Added pure helpers `ai::MailProcessShouldAbortMissingMailbox` and `ai::MailTakeAfterResult` in `ai/playerbot/policy/town/TownErrandPolicy.h` and compat twins `MailRequiresMailboxCompat()` / `MailTakeAfterResultCompat(uint32)` in `ai/cmangos-compat-w2town.h` (Turtle: `false` / always `true`, matching `origin/main`) and `forever/src/compat/TbCompat_W2Town.h` (Forever: `true` / `confirmedTakes > 0`, keeping the "no proof, no ok" rule). Updated `MailAction.cpp` to use both.
- **Test (`forever/tools/test_r_misc.cpp`)**:
  - FAIL before: `FAIL: mail-turtle-text-only-after-true` / `FAIL: mail-turtle-empty-mailbox-continues` (`FAIL: test_r_misc (6 passed, 10 failed)`)
  - PASS after: `PASS: test_r_misc (16 checks, 0 failed)`
- **Live check**:
  - Forever: at Stormwind mailbox with no ready mail, `tb order Dashmag mail #m1` -> `tb state Dashmag #m2` still shows `ord=<n>:fail:mail-none-taken`. With a text-only mail on Turtle, `TakeMailProcessor::After` returns `true` as before.

### Finding 12 (MEDIUM — Gathering danger check order & shared `freeGatherMs` timer)
- **What was wrong**: `BuildGoNodeFacts` called `HasDangerNearby(bot)` for every `GAMEOBJECT_TYPE_CHEST` in range before filtering by skill/readiness/tool/bags, and `TbBotMgr.cpp` used a single `static uint32 freeGatherMs = 0;` shared across all free bots so only one free bot could gather per 5 s window.
- **Fix**:
  - Added `SelectBestGatherNode` and `FreeGatherCooldownTracker` in `ai/playerbot/policy/profession/GatherNodePolicy.h`.
  - Updated `BuildGoNodeFacts` (`checkDanger = false` by default), `FindBestGatherNode`, `StartGatherNode`, and `HasGatherableNodeInSight` in `forever/src/host/TbHostProfession.cpp` to filter by skill, readiness, range, tool, and bag space first, running `HasDangerNearby(bot)` only once for the chosen node.
  - Added `tbhost::prof::AdvanceFreeGatherTimer` and `tbhost::prof::ClearFreeGatherTimer` (guid-keyed via `FreeGatherCooldownTracker`), wired into `TbBotMgr.cpp` and cleared in `BotMgr::OnPlayerLogout` and the finished-session cleanup loop.
- **Test (`forever/tools/test_r_misc.cpp`)**:
  - FAIL before: `FAIL: gather-danger-check-only-on-chosen-node`, `FAIL: gather-zero-danger-checks-when-no-valid-node`, `FAIL: gather-timer-bot2-4000ms-independent`, `FAIL: gather-timer-bot1-5000ms-ready`, `FAIL: gather-timer-bot2-5000ms-ready` (`FAIL: test_r_misc (6 passed, 10 failed)`)
  - PASS after: `PASS: test_r_misc (16 checks, 0 failed)`
- **Live check**:
  - With two free bots with Herbalism (`TortoiseBots.World.Professions = 1`) near two Silverleaf nodes, both bots gather independently without starving each other's 5 s timer; non-herb chests nearby do not trigger `HasDangerNearby`.

### Finding 13 (MEDIUM + Low — Gear swap cleanup by guid & filter before scoring)
- **What was wrong**: `EquipCandidateInSlot` (`TbGearGen.cpp`) called `StoreNewItemInBestSlots` + `GetItemByEntry` and, on a failed swap, called `DestroyItemCount(itemId, 1, true, false)`, which could delete a pre-existing item of the same entry already owned by the target. Also (Low), `SetGearForTarget` scored the entire slot pool before filtering.
- **Fix**:
  - Added `ai::DecideFailedSwapCleanup` and `ai::FilterCandidatesForSlot` / `ai::SortCandidatesForSlot` in `ai/playerbot/policy/item/GearSetPolicy.h`.
  - Updated `EquipCandidateInSlot` in `forever/src/host/gear/TbGearGen.cpp` to use `CanStoreNewItem` + `StoreNewItem`, capture `fresh->GetGUID()`, and on any failed `CanEquipItem`/`SwapItem` destroy only `target->GetItemByGuid(freshGuid)` via `target->DestroyItem(created->GetBagSlot(), created->GetSlot(), true)`.
  - Updated `SetGearForTarget` in `TbGearGen.cpp` to call `ai::FilterCandidatesForSlot` first, score only the filtered candidates, then call `ai::SortCandidatesForSlot`.
  - Updated `forever/tools/test_w6b_wiring.py` to pin `CanStoreNewItem(`, `StoreNewItem(`, `GetItemByGuid(`, `DestroyItem(`, and forbid `DestroyItemCount(`.
- **Test (`forever/tools/test_r_misc.cpp`)**:
  - FAIL before: `FAIL: gear-cleanup-destroys-by-created-guid-only`, `FAIL: gear-cleanup-noop-when-no-created-guid`, `FAIL: gear-filter-before-scoring-size-1` (`FAIL: test_r_misc (6 passed, 10 failed)`)
  - PASS after: `PASS: test_r_misc (16 checks, 0 failed)`
- **Live check**:
  - `.tb gear <bot> set #s1` equips/swaps classic gear; if a target already has a matching item in bags and a swap fails, the pre-existing bag item is preserved.

### Addendum (Low)
1. **Stale gather/craft errands across relog**:
   - **What was wrong**: `Errands()` (`TbHostProfession.cpp:447`) was never erased on bot logout/login, so a relogged bot could resume a stale errand.
   - **Fix**: Added `GatherErrandRegistry` in `GatherNodePolicy.h` and `tbhost::prof::ClearBotErrands(ObjectGuid const&)` in `TbHostProfession.h/.cpp` (erasing both `Errands()` and `FreeGatherTimers()`), called from `BotMgr::OnPlayerLogout`, `BotMgr::Update` finished-session cleanup (`TbBotMgr.cpp`), and `OnLogin` (`TbScripts.cpp`).
   - **Test (`forever/tools/test_r_misc.cpp`)**:
     - FAIL before: `FAIL: gather-errand-cleared-on-logout` (`FAIL: test_r_misc (16 passed, 1 failed)`)
     - PASS after: `PASS: test_r_misc (17 checks, 0 failed)`
   - **Live check**: Start a gather errand, log the bot out mid-walk (`tb bot <bot> logout`), log it back in (`tb bot <bot> login`), and verify `tb state <bot>` shows no active/resumed gather errand.
2. **Craft proof**:
   - Checked and verified: for item-producing crafts (`tracksOutputItem == true`), `StartCraft` (`TbHostProfession.cpp:781`) snapshots `c.lastItemCount = bot->GetItemCount(craftedItemId, false)`, `TickErrands` (`TbHostProfession.cpp:919-920`) computes `gainedThisRun = currentCount - c.lastItemCount`, and `StepCraftErrand` (`CraftForSelfPolicy.h:132-144`) transitions to `FinishOk` (`"ok"` at `TbHostProfession.cpp:964-977`) only when `itemsGainedThisRun > 0` (otherwise `fail:craft-no-item` at `CraftForSelfPolicy.h:142-143` or `fail:timeout` at `:140-141`). No code change needed.
