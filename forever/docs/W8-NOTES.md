# W8 Notes: Auction House as a Player (`D-w8-auction-house`)

Branch: `feat/w8-auction` (based on `main`, commit `495ffd943`).

## What was done

1. **Pure policies (`ai/playerbot/policy/ah/AhSellPolicy.h`, `ai/playerbot/policy/ah/AhBuyPolicy.h`)**:
   - `DecideAhSell`:
     - Routes stackables (`maxStackSize > 1`) to `AhPostKind::Commodity` and single items (`maxStackSize <= 1`) to `AhPostKind::Item` (`HandleAuctionSellItem` refuses `GetMaxStackSize() > 1`).
     - Never sells quest items (`itemClass == 12`), soulbound/equipped items, conjured items, non-empty bags, blacklisted items (`AhItemSellBlacklisted`), or W6a upgrades.
     - **W7 seam (`AhReserveTradeGoodsForW7`)**: W7 (professions) is being built in parallel and is not merged; trade goods (`itemClass == 7`) are reserved (never sold) until W7 lands.
     - Never posts below `vendorSellPrice + depositPerUnit`. Undercuts the lowest market listing by 5% (min 1c), or falls back to `UnitValue` when no market listing exists. If both market and `UnitValue` are 0, refuses to post (LESS rule).
   - `DecideAhBuy` & `AhClampCommodityBuyQty`:
     - Buyout-only (never buys own auctions); upgrades are bought only when W6a scores them strictly better than the worn item (`w6aNewScore > w6aWornScore`) and total price fits `budgetCopper`.

2. **Host seam (`forever/src/host/TbHostAuction.h`, `forever/src/host/TbHostAuction.cpp`)**:
   - `tbhost::ah::NpcIfCanInteract` + `IsFactionAuctionHouse`: checks `GetNPCIfCanInteractWith(..., UNIT_NPC_FLAG_AUCTIONEER, UNIT_NPC_FLAG_2_NONE)` and excludes neutral auction houses (`houseId == 7`).
   - `tbhost::ah::QueryMarketByItemId` / `QueryMarketByName` / `Owned`: reads `sAuctionMgr->GetAuctionsMap(...)` via `BuildListAuctionItems`, `BuildListBuckets`, and `BuildListOwnedItems` into local packet objects (never sent).
   - `tbhost::ah::UnitValue`: calls `AuctionBotVanilla::UnitValue(proto)` (verified to link into `worldserver` from the module).
   - `tbhost::ah::PostItem`, `PostCommodity`, `BuyItem`, `BuyCommodity` (quote + confirm), `Cancel`: dispatch through `WorldSession::HandleAuctionSellItem`, `HandleAuctionSellCommodity`, `HandleAuctionPlaceBid`, `HandleAuctionGetCommodityQuote`, `HandleAuctionConfirmCommoditiesPurchase`, `HandleAuctionRemoveItem`.
   - `ConfirmPendingPosts`: confirms async-committed posts on the next tick via `BuildListOwnedItems` and logs `[W8] post ... result=ok`.
   - `IsWorldAuctionEnabled()`: reads `TortoiseBots.World.Auction` (default `0`).
   - Structured `[W8]` log lines: `[W8] post|buy|cancel bot= item= count= price= deposit= result=`.

3. **Compat pair & legacy slice (`ai/playerbot/TbCompatWL8.h`, `forever/src/compat/TbCompat_WL8.h`, `ai/cmangos-compat-wl8.h`, `forever/src/compat/TbCompat_S9.h`, `forever/src/compat/TbCompat_Unported.h`, `ai/playerbot/strategy/actions/AhAction.cpp`)**:
   - `AhAction`, `AhBidAction`, and `AhCancelAction` in `AhAction.cpp` route through `TbCompatWL8.h` / `tbhost::ah` on ForeverCore while preserving the original Turtle code untouched.
   - `AuctionItemCountCompat` in `TbCompat_S9.h` now has its real body (`itemCount` or `sAuctionMgr->GetAItem`).
   - Added `ai/playerbot/strategy/actions/AhAction.cpp` to `forever/SLICE-W8.txt` (and `forever/SLICE-W1.txt` so `CMakeLists.txt` builds it), added `W8_KEYS` (`ah, ah bid, ah cancel`) to `forever/tools/slice_gen_context.py`, and regenerated `forever/src/slice/SliceAiObjectContext.cpp`.
   - Removed the `auction (type only)` row from `ai/playerbot/UnportedFeatures.h` and `forever/docs/UNPORTED.md`.

4. **Ordered commands & state (`forever/src/host/TbOrders.cpp`, `forever/src/host/TbPartyState.cpp`)**:
   - Added `.tb order <bot|party [leader]> ah sell [item] | ah buy <item> [n] | ah list | ah cancel <id> [#token]`.
   - Appended `ah=<posted>/<sold>` to `.tb state`.

## Gate results

- **Census (`forever/SLICE-W8.txt`)**:
  `files 287  compile clean 287  with errors 0  error places 0 (in 0 files)  includes of a header ForeverCore does not have 0`
- **`worldserver` build (`TORTOISEBOTS_SLICE1=ON`)**:
  `[336/336] Linking CXX executable bin/RelWithDebInfo/bin/worldserver` -> `BUILD OK worldserver ON f812791eb`
- **`worldserver` build (`TORTOISEBOTS_SLICE1=OFF`)**:
  pending (run after final commit; recorded in `report-w8.md`).
- **Registry & context checks**:
  `SliceAiObjectContext.cpp is up to date`
  `summary: (a)=0  (b)=228 keys to register or stub later`
- **Pure tests**:
  `PASS: test_w8_ah (8 checks, 0 failed)`
- **Wiring test (`forever/tools/test_w8_ah_wiring.py`)**:
  `Ran 6 tests in 0.006s` -> `OK`; `--mutate` -> `FAILED (failures=1)` (`mutate_exit=1`).

## Live test steps (for combined lead run)

Position seller bot `Sellbot` and buyer bot `Buybot` near a faction auctioneer (e.g. Stormwind or Orgrimmar) with `Sellbot` holding 3 single items and 2 stackable items (non-trade-goods, or with `AhReserveTradeGoodsForW7` wired after W7) and copper for deposits, and `Buybot` holding copper:
```
tb engine Sellbot on
tb engine Buybot on
tb state Sellbot #s0
tb order Sellbot ah sell #o1
tb state Sellbot #s1
tb order Sellbot ah list #o2
tb order Buybot ah buy <item-id> #o3
tb order Sellbot ah cancel <auction-id> #o4
tb order Sellbot mail #o5
tb state Sellbot #s2
```
Expected:
- `tb order` returns `status=queued` and `order-end ... queued=1 refused=0`.
- Server log shows `[W8] post ... result=pending` followed next tick by `[W8] post ... result=ok`, `[W8] buy ... result=ok`, `[W8] cancel ... result=ok`.
- `tb state Sellbot #s1` shows `ah=<posted>/<sold>` updated.
- After `Buybot` buys one posting, `Sellbot` receives the proceeds by mail and collects them with `tb order Sellbot mail`.

## Open points / follow-ups

- **W4 travel purpose**: no W4 travel purpose (`auctioneer`) added yet; bots only use an auctioneer within the W2 walking rule.
- **W7 trade goods**: `AhReserveTradeGoodsForW7()` currently returns `true` (reserves all trade goods) until W7 lands.
