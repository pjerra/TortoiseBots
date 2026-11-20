# D. W8: auction house as a player

**Comes after W2, W6a and W7.** W2 (your PR #93) gives the mail take (the AH pays and returns items by mail) and the
money budget; W6a says what is an upgrade and what is worth selling; W7 gives crafts and mats. Walking to a capital's
auctioneer needs our W4; before that, only an auctioneer within the W2 walking rule is visited.

## Done looks like

A bot sells its loot and crafts and buys upgrades and mats at its faction's auction house, through the core's own
auction handlers, at prices taken from the market that ForeverCore's own AH bot fills, and collects its money and
items from the mailbox.

**Live acceptance** (world-life plan, W8 row): a bot posts 5 items (2 commodities, 3 single items); another bot buys
one of them; the seller's mail delivers the money and the seller takes it (W2). Log `[W8] post|buy|cancel|expire bot=
item= count= price= deposit= result=`.
**From a client**: the forever-agent client's auction round (R8: hello, browse by item id, buy out one item, list own,
sell one item; single items only, no commodities) browses by the bot's item id, finds the bot's single-item posting and
buys it out; the commodity side is checked on the server only.

## Scope and legacy files

Plan scope: "sell loot and crafts, buy upgrades and mats, on F's modern AH; F's own AHBot fills the market", 4
worker-days, biggest risk "commodity vs item split; F AHBot price levels". Area: 6 files, 2,039 lines, 80 places.
Legacy: `strategy/actions/AhAction.cpp` 571 / 47, `AhAction.h`, `commands/Ah.cpp`, `runtime/AhMarketService.cpp`
856 / 33 (its seller "teleport to the auctioneer" fallback is cut). The AH bot service (`AhBotService.cpp`,
`AhBotSellerAccount.cpp`) is out: the core's AH bot replaces it.

## ForeverCore APIs (25fc7ca)

Client flow vs core: `forever/data/waterys/core-howto/auction-house.md`.
- Houses: `sAuctionMgr->GetAuctionsMap(factionTemplateId)` (`AuctionHouseMgr.h:370`), `GetAuctionHouseEntry`
  (`:385`). Postings are `AuctionPosting` (`:225`) in `AuctionHouseObject` (`:267`); `GetAuction(id)` (`:292`).
- Reading the market without packets: `BuildListAuctionItems(result, player, itemId, ...)` (`:315-317`) and
  `BuildListBuckets` (`:306-310`) fill a `WorldPackets::AuctionHouse::...Result` object you keep and read (never sent).
  `BuildListOwnedItems` (`:319`) for the bot's own postings. Commodity price: `CreateCommodityQuote` (`:326`).
- Bucket keys: `AuctionsBucketKey::ForItem` / `ForCommodity` (`:176-177`). Durations 2/8/24 h (`MIN_AUCTION_TIME`,
  `:53`). Deposit: `GetCommodityAuctionDeposit` (`:383`), item deposit inside the sell handler.
- Acting (typed packets into the handlers, so throttle, deposit, NPC check and mail are the core's):
  `HandleAuctionSellItem` (`AuctionHouseHandler.cpp:816`; a stackable is refused there, `:878`),
  `HandleAuctionSellCommodity` (`:610`), `HandleAuctionPlaceBid` (`:356`, buyout = bid at buyout),
  `HandleAuctionGetCommodityQuote` (`:161`) then `HandleAuctionConfirmCommoditiesPurchase` (`:123`),
  `HandleAuctionRemoveItem` (`:490`). Every one checks `GetNPCIfCanInteractWith(..., UNIT_NPC_FLAG_AUCTIONEER)` and
  `sAuctionMgr->CheckThrottle` (`AuctionHouseMgr.h:401`).
- The sell commits asynchronously (`AddTransactionCallback(...AsyncCommitTransaction...)`, around
  `AuctionHouseHandler.cpp:943`, commodities `:801`); the bot session runs the callbacks in `WorldSession::Update`
  (`forever/src/TbBotSession.cpp:297`). The result is known next tick: confirm by `BuildListOwnedItems`.
- Market value: `AuctionBotVanilla::UnitValue(proto)` (`AuctionHouseBotVanilla.h:49`) when the core's AH bot runs
  with its vanilla options; `MarketUnitPrice` (`:40`). These have no `TC_GAME_API`: check they link from the module.

## Files to touch (suggested)

- Pure `policy/ah/AhSellPolicy.h` (what to post, item vs commodity, price, duration), `AhBuyPolicy.h` (upgrade or mat
  worth buying under budget) + tests.
- Host `forever/src/host/TbHostAuction.{h,cpp}` (`tbhost::ah`: Market, Post, Buy, Cancel, Owned).
- Compat pair `TbCompat_WL8.h` + `ai/cmangos-compat-wl8.h`; `AuctionItemCountCompat` (`TbCompat_S9.h`, answers 0 now)
  gets its real body; census `forever/SLICE-W8.txt`.
- Ordered: `.tb order <bot> ah sell [item] | ah buy <item> [n] | ah list | ah cancel <id>`; `.tb state` block
  `ah=<posted>/<sold>`; free-kit `ah` behind `TortoiseBots.World.Auction = 0`; travel purpose `auctioneer` through
  W4's gate; `W8_KEYS`.

## UNPORTED rows removed

`auction (type only)` and, inside `ItemUsageValue`, the "auction listings answer none" part (L9's note).

## Rules that bite here

- No AH SQL: postings live in the core's memory; the core writes the characters DB. If you read the AH bot's
  `auctionhousebot_market_price`, only through `WorldQueryCompat` (the table may not exist).
- Keep the auctioneer guid and auction ids; never an `AuctionPosting*` across ticks.
- A post counts only when it shows in the owned list; a buy only when the item arrives by mail (or a money change for
  a sale). A throttle refusal = try later, logged.
- Never sell quest items, worn gear, W6a upgrades or W7 mats the bot still needs; never post below vendor sell price
  plus deposit.
- No teleport to the auctioneer (the legacy fallback is cut).

## Tests (failing case first)

- **Failing case:** a stackable item (stack size above 1) is posted through `HandleAuctionSellItem`; the core refuses it
  (`:878`). `AhSellPolicy` must route stackables to the commodity path.
- Price: never below vendor sell price + deposit; under the lowest current posting by a small step; no market and no
  `UnitValue` = do not post (less).
- Buy: an upgrade is bought only when W6a scores it better than the worn item and the price is inside the budget.
- Wiring: every AH action goes through `tbhost::ah` into the core handlers; no `AuctionHouseObject::AddAuction` or
  `RemoveAuction` called by the module; `--mutate` removes the stackable check and must FAIL.

## Decisions already made

- The core's AH bot fills the market; our module never runs a seller service or seller accounts.
- Faction houses only; buyout only (no bidding wars).

## Open for you to settle

1. **Commodities.** The plan's live check posts 2; the client round covers single items only. Recommendation: the bots
   do both (same handlers); check commodities on the server side.
2. **Neutral houses** (Booty Bay, Gadgetzan). Recommendation: out for now.
3. **How often.** Recommendation: one AH visit per bot per in-game session when bags are over 70 % full or W7 mats
   pile up; at most one AH command per bot per throttle window.
