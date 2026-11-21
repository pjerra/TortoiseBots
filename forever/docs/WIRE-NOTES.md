# WL2 Wiring Notes: W9 Guild Orders -> W7/W8/W2 & W8 Mat Reserve -> W7/W9

Branch: `feat/wl2-wiring` (base `feat/world-life-2`).

## What was built

1. **Pure step-choice policy (`runtime/GuildOrderWorkPolicy.h`)**:
   - `DecideOrderStep`: counts `have = delivered + inBags` against `want`; when `have >= want`, picks `DeliverBank` then `DeliverMail`; otherwise picks source in order `Craft -> Gather -> BuyAh` honouring `OrderType` (`Craft`, `Farm`, `Buy`). Without W4 travel, missing local sources or delivery endpoints answer `CannotTravel` (`"cannot: travel"`).
   - Included in `forever/src/slice/SliceGuildOrderHeaders.cpp` for census coverage.

2. **Host providers (`forever/src/host/TbHostGuildOrders.h`, `forever/src/host/TbHostGuildOrders.cpp`)**:
   - `HostGatherCraftProvider` on `tbhost::prof` (`HasGatherableNodeInSight`, `FindCraftSpellForItem`, `Craft`, `GatherNearestInSight`).
   - `HostAuctionBuyProvider` on `tbhost::ah` (`FindNearestFactionAuctioneer` within W2 300 yd walking rule in current zone, `QueryMarketByItemId`, `DecideAhBuy`, `BuyCommodity` / `BuyItem`).
   - `HostDeliveryProvider` on `tbhost::guild::DepositItem` (tries tabs 0..4 when a guild bank is within 300 yd) and `tbhost::guild::SendMail` (when a mailbox is within 300 yd; falls back to mail if bank tabs refuse).
   - Replaced the `Null*Provider` defaults with the host providers.

3. **Order loop & `.tb guildorder work <bot> [id]` (`forever/src/host/TbHostGuildOrders.cpp`, `forever/src/TbBotMgr.cpp`)**:
   - `.tb guildorder work <bot> [id] [#token]` activates the per-tick order loop (`TickBotOrder` in `BotMgr::Tick`) and returns grammar v2 `guildorder op=work ... have= want= state=`.
   - Runs one step per tick, confirms item counts in bags / deposits / sent mail before advancing `delivered`, and logs `[W9] order|step|deliver|refuse id= bot= item= have= want=`.

4. **W8 trade-good reserve (`ai/playerbot/policy/ah/AhSellPolicy.h`, `forever/src/compat/TbCompat_WL8.h`, `ai/playerbot/strategy/actions/AhAction.cpp`)**:
   - Replaced `AhReserveTradeGoodsForW7()` with `ShouldReserveTradeGood(itemId, itemClass, neededByOwnProfessions, neededByGuildOrder)`.
   - `tbhost::prof::IsReagentForBotProfessions` checks a one-pass DB2 reagent cache for the bot's `ProfessionPairForClass` + learned craft skills; `tbhost::guild_orders::IsItemNeededByGuildOrder` checks open orders of the bot's guild (direct item, craft reagents via `IsReagentForItem`, and `needItems`).
   - Other trade goods may be posted on the AH (still never below `vendorSellPrice + depositPerUnit`).

## Gates

- **Pure tests**:
  - `test_w8_ah`: `PASS: test_w8_ah (8 checks, 0 failed)` (includes failing case: trade good needed by own profession or guild order is reserved; unrelated trade good posts).
  - `test_w9_order_work`: `PASS: test_w9_order_work (6 checks, 0 failed)` (includes failing case: missing local source returns `CannotTravel` / `"cannot: travel"`, never `Complete`).
- **Wiring & mutation**:
  - `python3 -m unittest forever/tools/test_w9_guild_wiring.py forever/tools/test_w8_ah_wiring.py`: `Ran 12 tests in 0.003s` -> `OK`.
  - `python3 forever/tools/test_w9_guild_wiring.py --mutate`: `FAILED (failures=2)` (`mutate_exit=1`).
- **Registry & context**:
  - `summary: (a)=0  (b)=228 keys to register or stub later`
  - `SliceAiObjectContext.cpp is up to date`
- **Census (`SLICE-W9.txt` & `SLICE-W8.txt`)**:
  - `SLICE-W9.txt`: `files 287  compile clean 287  with errors 0  error places 0 (in 0 files)  includes of a header ForeverCore does not have 0`
  - `SLICE-W8.txt`: `files 287  compile clean 287  with errors 0  error places 0 (in 0 files)  includes of a header ForeverCore does not have 0`
- **Full link ON (`worldserver`)**:
  - `BUILD OK worldserver ON f5b2ab2fd`
- **Full link OFF (`worldserver OFF`)**:
  - Run after final commit; recorded in `report-wire.md`.

## Live check for lead

Position bot `Testbot` in a guild with a player (who has `GR_RIGHT_EOFFNOTE`), holding `Linen Cloth` + `Coarse Thread` and knowing `Linen Bag` (`3755`), near a mailbox / guild bank (within 300 yd):
1. `tb guildorder create Testbot 4234 2 craft #w1`
2. `tb guildorder work Testbot #w2` -> expect `guildorder op=work bot=Testbot item=4234 have=0 want=2 state=working #w2`
3. Wait a few ticks -> expect `[W9] step ... action=craft result=crafted`, `[W9] deliver ... mode=bank|mail result=done`, `tb guildorder status Testbot #w3` shows `have=2 want=2 state=done|mailed`.
4. Position `Sellbot` (Warrior, Mining+Blacksmithing) near an auctioneer holding `Copper Ore` (reserved for Mining/BS) and `Linen Cloth` (not needed by BS or any open guild order) -> `tb order Sellbot ah sell` posts `Linen Cloth` and keeps `Copper Ore`.
