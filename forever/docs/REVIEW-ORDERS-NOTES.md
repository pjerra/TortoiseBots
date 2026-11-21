# Review Fixes: Guild & Guild Orders (PR #97, W9)

Branch: `fix/wl2r-orders`.

## Findings fixed

### Finding 2 (HIGH): Guild bank deposit could swap/withdraw an item
- **What was wrong:** `tbhost::guild::DepositItem` (`forever/src/host/TbHostGuild.cpp:250`) hardcoded `packet.BankSlot = 0`. In ForeverCore's `Guild::_MoveItems`, a non-`NULL_SLOT` destination slot that already holds a different item triggers a swap, pulling the bank item into the bot's bag while `afterCount < beforeCount` still reported success.
- **Fix:** Added `tbhost::guild::kGuildBankAutoSlot = 255` (`static_assert(kGuildBankAutoSlot == NULL_SLOT)`) and `VerifyBankDeposit` in `forever/src/host/TbHostGuildPure.h`. `DepositItem` now sends `NULL_SLOT` (`255`) so the core picks a mergeable stack or free slot, and checks the source bag slot afterwards so any unexpected item in that slot fails with `bank-refused`.
- **Test:** `forever/tools/test_r_orders.cpp`
  - FAIL before: `FAIL: F2: kGuildBankAutoSlot must be NULL_SLOT (255) (line 26: kGuildBankAutoSlot == 255)` and `FAIL: F2: swapped foreign item in bag slot must fail (line 29: !VerifyBankDeposit(10, 0, 4234, 19019))`
  - PASS after: `PASS: test_r_orders (all checks passed, 0 failed)`

### Finding 3 (HIGH): Whole-stack delivery & mailing a real player on console orders
- **What was wrong:** `HostDeliveryProvider::Deliver` ignored `count` and sent the entire bag stack (e.g. 200 items for an order of 5). Also, console-created orders set `giverGuidLow = botGuidLow`, which fell back to `GetLeaderGuid(guildId)` and mailed the guild leader even when that leader was a real player (non-bot).
- **Fix:**
  1. Added `PlanDeliveryStack` in `TbHostGuildPure.h` and `SplitDeliveryStack` in `TbHostGuildOrders.cpp`, using `Player::SplitItem` into a free bag slot so only the missing `count` is banked or mailed (no free items, no item loss; fails with `bags-full` if no slot can hold the split).
  2. Added `DecideMailRecipient` in `TbHostGuildPure.h` and `ResolveMailTarget` in `TbHostGuildOrders.cpp`: guild bank delivery is always tried first; mail fallback only mails a real player if that player is the explicit requester (`giverGuidLow != 0 && giverGuidLow != botGuidLow`). Console-created orders that fall back to a non-bot guild leader are refused with `real-player-mail-refused` (marking the order `Cant` / `BankDenied`) instead of mailing a real player. Justification: refusing with a named code (`real-player-mail-refused`) when the guild bank is unavailable prevents unsolicited mail to real players while preserving guild bank delivery and player-requested mail.
- **Test:** `forever/tools/test_r_orders.cpp`
  - FAIL before: `FAIL: F3a: stack of 200 with missing=5 must split to 5` and `FAIL: F3b: console order must refuse mailing real-player guild leader`
  - PASS after: `PASS: test_r_orders (all checks passed, 0 failed)`

### Finding 4 (HIGH): Auction gold cap applied per purchase, not across the order
- **What was wrong:** `TickBotOrder` passed `rec.goldCap` on every AH tick without tracking cumulative spend, and `HostAuctionBuyProvider` treated `maxCopper == 0` as uncapped.
- **Fix:** Added `ahGoldSpent` (`OrderRecord::ahGoldSpent` in `runtime/GuildOrderContract.h`, `ah_gold_spent` column in `forever/sql/characters/2026_11_20_01_tb_guild_orders.sql`, loaded/saved in `TbHostGuildOrders.cpp`) and `RemainingOrderGoldCap` / `EffectiveAhBudget` in `TbHostGuildPure.h`. `TickBotOrder` tracks actual copper spent (`moneyBefore - moneyAfter`), persists `ahGoldSpent`, and stops AH purchases (`canBuyAh = false`) once `ahGoldSpent >= goldCap`.
- **Test:** `forever/tools/test_r_orders.cpp`
  - FAIL before: `FAIL: F4: remaining budget must subtract ahGoldSpent` and `FAIL: F4: exhausted gold cap must yield 0 budget`
  - PASS after: `PASS: test_r_orders (all checks passed, 0 failed)`

### Finding 6 (HIGH): Guild-order SQL delivery & updater path
- **What was wrong:** `forever/sql/characters/2026_11_20_01_tb_guild_orders.sql` lives in `forever/sql/characters/` (the characters counterpart of `forever/sql/world/` for `DBUpdater<CharacterDatabaseConnection>` via `updates_include`), but `forever/sql/README.md` omitted both `2026_11_20_01_tb_guild_orders.sql` and the `characters` `updates_include` instruction (`$/sql/tortoisebots/characters`), and there was no check script guarding module SQL paths.
- **Fix:** Documented `characters/2026_11_20_01_tb_guild_orders.sql` and `$/sql/tortoisebots/characters` in `forever/sql/README.md`, and added `forever/tools/test_sql_delivery.py` (with `--mutate`) verifying every `.sql` file under `forever/sql/` is in `world/` or `characters/`, follows `YYYY_MM_DD_NN_<what>.sql`, has a unique basename, and ends with `;\n`.
- **Test:** `forever/tools/test_sql_delivery.py`
  - FAIL before: `AssertionError: '$/sql/tortoisebots/characters' not found`
  - PASS after: `Ran 2 tests in 0.001s -> OK`; `--mutate` fails as expected.

### Finding 9 (MEDIUM) + Low: `TickBotOrder` throttle, log deduplication, and logout cleanup
- **What was wrong:** `TickBotOrder` ran every world tick (scanning grids/AH continuously) and logged `TC_LOG_INFO` on every repeated failure. Also, `workingBots` and `HostGatherCraftProvider::_pending` were never cleared on bot logout.
- **Fix:** Added `OrderTickThrottleState` in `TbHostGuildPure.h` (2s active cadence, 10s failure backoff, `INFO` only when the failure reason changes, `DEBUG` for repeats) and `tbhost::guild_orders::OnBotLogout(uint32_t)` in `TbHostGuildOrders.h/.cpp` called from `BotMgr::OnPlayerLogout` (`forever/src/TbBotMgr.cpp`) to clear `workingBots`, `tickThrottle`, and `_pending`.
- **Test:** `forever/tools/test_r_orders.cpp`
  - FAIL before: `FAIL: F9: throttled immediately after failure` / `identical failure deduped` / `short throttle after success`
  - PASS after: `PASS: test_r_orders (all checks passed, 0 failed)`

## Live checks for combined lead run
Apply `forever/sql/characters/2026_11_20_01_tb_guild_orders.sql` (or copy `forever/sql/characters/` to `<SourceDirectory>/sql/tortoisebots/characters/` with `updates_include` row `('$/sql/tortoisebots/characters', 'RELEASED')`).
1. Guild bank deposit with occupied bank slot 0:
   - Put a different item in guild bank tab 0 slot 0, give bot 5 of item 4234, run `.tb guildorder create <bot> 4234 5 farm #g1` and `.tb guildorder work <bot> #g2`.
   - Expected: bank slot 0 item stays in bank (not swapped to bot), item 4234 lands in a free bank slot, `status` reports `state=done have=5 want=5`.
2. Partial stack split delivery:
   - Give bot a stack of 20 item 4234, create order for 5, run `.tb guildorder work <bot>`.
   - Expected: only 5 are delivered (`have=5 want=5 state=done`), 15 remain in bot's bags.
3. Console order mail guard (no guild bank in reach, real-player guild leader):
   - From server console: `.tb guildorder create <bot> 4234 5 buy` (with bot near mailbox, no guild bank, real player as guild leader).
   - Expected: refuses mailing the real player (`reason=real-player-mail-refused`, `state=cant`), whereas a player-created order from that player mails them as requested.
4. Throttle & relog:
   - Run `.tb guildorder work <bot>` on an unfulfillable order; server log emits `[W9] step ...` once at `INFO`, not every tick. Log the bot out and back in; stale working/pending state is cleared.
