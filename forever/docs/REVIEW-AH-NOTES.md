# Review Fixes: W8 Auction House (`ah`)

Branch: `fix/wl2r-ah`

## Findings fixed

1. **HIGH — Bulk `ah sell` posted all unbound bag items + missing config gate**
   - **What was wrong**: `AhAction::ExecuteCommand` (`ai/playerbot/strategy/actions/AhAction.cpp`) iterated every bag item without checking `ItemUsage::ITEM_USAGE_AH` or profession tools (so ammo, reagents, food, Mining Pick, Skinning Knife were posted), and `IsWorldAuctionEnabled()` (`TortoiseBots.World.Auction`) was never checked in `TbOrders.cpp` or `AhAction.cpp`.
   - **Fix**:
     - Added `IsAhProfessionTool`, `explicitItem`, `isUsageAh`, `isProfessionTool`, and `worldAuctionEnabled` to `ai/playerbot/policy/ah/AhSellPolicy.h` (`DecideAhSell`).
     - Bulk `ah sell` (`explicitItem == false`) now only posts items with `ItemUsage::ITEM_USAGE_AH`; profession tools (including Mining Pick 2901, Skinning Knife 7005, Fishing Pole 6256, Blacksmith Hammer 5956, Arclight Spanner 6219, enchanting rods, and all `IsCraftingTool` totems) are always refused (`profession-tool`), even on explicit `ah sell <item>`.
     - Added `IsWorldAuctionEnabled()` gate in `forever/src/host/TbOrders.cpp` (both at queue time with `refuse("auction-disabled", ...)` and execution time with `fail:auction-disabled`) and in `AhAction::Execute` via `AhIsWorldAuctionEnabledCompat()` (`forever/src/compat/TbCompat_WL8.h`). Documented `TortoiseBots.World.Auction = 0` in `forever/conf/tortoisebots.conf.dist`.
   - **Test**: `forever/tools/test_ah.cpp` (checks 1, 2, 3)
     - FAIL before: `FAIL before: DecideAhSell posted Mining Pick (itemId=2901) without AH usage or tool guard (shouldPost=1)`
     - PASS after: `PASS: test_ah (6 checks, 0 failed)`

2. **MEDIUM — No buy price ceiling + `ah buy <name>` picked cheapest across different items**
   - **What was wrong**: `AhBidAction::ExecuteCommand` used the bot's whole `NeedMoneyFor::anything` purse with no per-unit price ceiling, and `ah buy <name>` sorted all substring-matched items together and bought whichever different item was cheapest.
   - **Fix**:
     - Added `AhComputeMaxUnitPrice(unitValue, vendorBuyPrice, vendorSellPrice)` and `maxUnitPrice` / `worldAuctionEnabled` checks to `ai/playerbot/policy/ah/AhBuyPolicy.h` (`DecideAhBuy`). Uses `3x` AHBot `UnitValue` when available, else `4x` vendor buy price, else `10x` vendor sell price, else `10000c` (1g) fallback cap. This bounds per-unit spend even when the bot carries a large purse.
     - Added `ResolveAhBuyItemId(query, candidates)` to `AhBuyPolicy.h`. `ah buy <name>` now resolves the name to one exact item id (unique match or unique case-insensitive exact name match); multiple distinct matching items without a unique exact match fail with `ambiguous-item` (`fail:ambiguous-item` in `TbOrders.cpp` via `tbhost::ah::TakeLastFailCode`).
   - **Test**: `forever/tools/test_ah.cpp` (checks 4, 5)
     - FAIL before: `FAIL before: DecideAhBuy bought overpriced item (price=50000c, budget=100000c) with no price ceiling (shouldBuy=1)`
     - PASS after: `PASS: test_ah (6 checks, 0 failed)`

3. **LOW — `ConfirmPendingPosts` matched on `itemId` only**
   - **What was wrong**: `tbhost::ah::ConfirmPendingPosts` (`forever/src/host/TbHostAuction.cpp`) matched pending posts against `Owned` using `o.itemId == pit->itemId` only, so a second posting of the same `itemId` could be falsely confirmed by an earlier listing.
   - **Fix**: Added `ObjectGuid itemGuid` to `AhOwnedView` and `PendingPostRecord` (populated from `AuctionItem::ItemGuid` in `Owned`), and added `MatchesPendingPost` in `AhSellPolicy.h` to match single items by `itemGuid` counter and commodities by `(itemId, count, unitPrice)` with used-slot tracking.
   - **Test**: `forever/tools/test_ah.cpp` (check 6)
     - PASS after: `PASS: test_ah (6 checks, 0 failed)`

## Live checks for lead

1. With `TortoiseBots.World.Auction = 0`:
   - `.tb order <bot> ah sell` -> refused with `auction-disabled`.
2. With `TortoiseBots.World.Auction = 1` and bot near a faction auctioneer holding a Mining Pick (2901), ammo/food/reagents, and an `ITEM_USAGE_AH` item:
   - `.tb order <bot> ah sell` -> posts only the `ITEM_USAGE_AH` item; Mining Pick, ammo, food, reagents stay in bags.
   - `.tb order <bot> ah sell Mining Pick` -> `fail:profession-tool`.
   - `.tb order <buyer> ah buy Linen` (when multiple "Linen*" items are listed) -> `fail:ambiguous-item`.
   - `.tb order <buyer> ah buy Linen Cloth 1` (at normal price) -> `ok`.
