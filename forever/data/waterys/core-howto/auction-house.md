# Auction house — client flow vs core

Client build **1.60.1.70938** (Interface **16001**), 2026-11-17. Core: TrinityCore `6517af05`
(`forever/spike/TrinityCore`); paths are relative to that clone root. The AuctionHouseBot module is
**out of scope** here; only its hooks are listed at the end.

## Client side, step by step

1. **Right-click the auctioneer** (or "I want to browse your goods" gossip). `CMSG_AUCTION_HELLO_REQUEST`
   (`Opcodes.cpp:182`) → `HandleAuctionHelloOpcode` (`Handlers/AuctionHouseHandler.cpp:196`) →
   `SendAuctionHello` (`AuctionHouseHandler.cpp:987`): level check against
   `CONFIG_AUCTION_LEVEL_REQ`, house resolved by `AuctionHouseMgr::GetAuctionHouseEntry`
   (`AuctionHouse/AuctionHouseMgr.cpp:816`), then `SMSG_AUCTION_HELLO_RESPONSE`
   (`Server/Packets/AuctionHousePackets.h:413`).
2. **Type in the search box**. `CMSG_AUCTION_BROWSE_QUERY` (`Opcodes.cpp:178`) →
   `HandleAuctionBrowseQuery` (`AuctionHouseHandler.cpp:37`): picks the house map with
   `AuctionHouseMgr::GetAuctionsMap` (`AuctionHouseMgr.cpp:405`), converts the item-class/subclass
   filters, and calls `AuctionHouseObject::BuildListBuckets`
   (`AuctionHouseMgr.cpp:1178` for a name search, `:1335` for explicit bucket keys) which answers with
   `SMSG_AUCTION_LIST_BUCKETS_RESULT` (`AuctionHousePackets.h:439`, opcode `Opcodes.cpp:1265`).
   `HandleAuctionListBucketsByBucketKeys` (`AuctionHouseHandler.cpp:239`) is the "refresh known rows" variant.
3. **Open one result row**. `CMSG_AUCTION_LIST_ITEMS_BY_BUCKET_KEY` (`Opcodes.cpp:185`) →
   `HandleAuctionListItemsByBucketKey` (`AuctionHouseHandler.cpp:268`) →
   `AuctionHouseObject::BuildListAuctionItems` (`AuctionHouseMgr.cpp:1391`, item-id variant at `:1410`) →
   `SMSG_AUCTION_LIST_ITEMS_RESULT` (`AuctionHousePackets.h:465`). "My auctions" / "bids" are
   `CMSG_AUCTION_LIST_OWNED_ITEMS` (`Opcodes.cpp:187`) and `CMSG_AUCTION_LIST_BIDDED_ITEMS` (`:183`).
4. **Post an auction**. `CMSG_AUCTION_SELL_ITEM` (`Opcodes.cpp:192`) → `HandleAuctionSellItem`
   (`AuctionHouseHandler.cpp:816`): runtime is restricted to the classic 2/8/24 h (`:848`), the deposit is
   computed by `AuctionHouseMgr::GetItemAuctionDeposit` (`AuctionHouseMgr.cpp:463`), the money is reserved in
   `AuctionHouseMgr::PendingAuctionAdd` (`AuctionHouseMgr.cpp:654`) (returned on failure in
   `PendingAuctionProcess`, `:686`), the item leaves the bags and
   `AuctionHouseObject::AddAuction` (`AuctionHouseMgr.cpp:890`) inserts it; result is
   `SMSG_AUCTION_COMMAND_RESULT` (`AuctionHousePackets.h:382`). Stackables must use
   `CMSG_AUCTION_SELL_COMMODITY` (`:191` → handler `AuctionHouseHandler.cpp:610`).
5. **Bid / buyout**. `CMSG_AUCTION_PLACE_BID` (`Opcodes.cpp:188`) → `HandleAuctionPlaceBid`
   (`AuctionHouseHandler.cpp:356`): `CheckThrottle` (`AuctionHouseMgr.cpp:794`) first, then the minimum is
   `BidAmount + CalculateMinIncrement()` (`AuctionHouseMgr.h:247`), the previous bidder is refunded through
   `AuctionHouseObject::SendAuctionOutbid` (`AuctionHouseMgr.cpp:1825`) plus
   `SMSG_AUCTION_OUTBID_NOTIFICATION`, and a bid equal to the buyout removes the posting
   (`RemoveAuction`, `AuctionHouseMgr.h:301`) and runs the sale:
   `SendAuctionSold` (`AuctionHouseMgr.cpp:1935`, owner: money minus the house cut, by mail) and
   `SendAuctionWon` (`AuctionHouseMgr.cpp:1849`, bidder: the item by mail). Plain bids update
   `auction.Bidder/BidAmount` and notify the owner with `SendAuctionOwnerBidNotification` (`AuctionHouseHandler.cpp:470`).
6. **Auction expires**. Each house ticks its postings in `AuctionHouseObject::Update`
   (`AuctionHouseMgr.cpp:1115`); a posting whose `EndTime` is within the next minute (`:1147`) is removed
   and finished: no bidder → `SendAuctionExpired` (`AuctionHouseMgr.cpp:1962`, item mailed back),
   bidder → `SendAuctionSold` + `SendAuctionWon` (`AuctionHouseMgr.cpp:1169`). `AuctionHouseMgr::Update`
   (`AuctionHouseMgr.cpp:767`) itself only expires the per-player throttle objects.
7. **Cancel my auction**. `CMSG_AUCTION_REMOVE_ITEM` (`Opcodes.cpp:189`) → `HandleAuctionRemoveItem`
   (`AuctionHouseHandler.cpp:490`); cancel is only allowed while there is no bid on the posting.
8. **Commodity purchase** (quote → confirm): `CMSG_AUCTION_GET_COMMODITY_QUOTE` (`Opcodes.cpp:181`),
   `CMSG_AUCTION_CONFIRM_COMMODITIES_PURCHASE` (`:180`), `CMSG_AUCTION_CANCEL_COMMODITIES_PURCHASE` (`:179`)
   → handlers `AuctionHouseHandler.cpp:161`, `:123`, `:101`; the actual matching lives in
   `AuctionHouseMgr.cpp:1596` onward.

## Notes / limits

- All state is `AuctionPosting` (`AuctionHouseMgr.h:225`) inside the per-house
  `AuctionHouseObject`, keyed by faction via `GetAuctionsMap`; persistence is the `auctionhouse` table,
  loaded by `AuctionHouseMgr::LoadAuctions` (`AuctionHouseMgr.cpp:503`).
- Every handler runs `CheckThrottle` (`AuctionHouseMgr.cpp:794`) — a bot that spams browse/post gets
  `AuctionResult::AuctionHouseBusy` (`AuctionHouseMgr.cpp:806`) until the timer expires.
- **Bot hooks only**: `AuctionHouseMgr.cpp:21` includes `AuctionHouseBot.h`, and every "should this
  character receive mail/notification" branch is guarded by `sAuctionBotConfig->IsBotChar(...)`
  (`AuctionHouseMgr.cpp:1830`, `:1856`, `:1903`, `:1928`, `:1975`, `:1987`); the module is initialised in
  `World.cpp:2171` with `AuctionHouseBot.Update.Interval` (`World.cpp:939`). Bots post through their own
  module, not through the handlers above.
