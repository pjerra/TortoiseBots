---
id: developers-guild-supply
title: Guild Supply Chain
category: developers
summary: "How the Forever host runs guild orders: store, step policy, providers, delivery, permissions and the addon transport."
tags: [developer, guild-orders, guild-bank, supply]
relates_to:
  - play-guild-orders
  - developers-architecture
  - developers-host-api
---

# Guild Supply Chain

The Forever build runs guild orders in the host, not in the legacy order actions. This page names the pieces and the rules they enforce.

## Store and contract

- `forever/src/host/TbHostGuildOrders.cpp` owns the order store. Rows live in the characters table `tortoise_bots_guild_order` (`forever/sql/characters/2026_11_20_01_tb_guild_orders.sql`). Every characters read is guarded by `information_schema` first, because the core aborts the world on a missing table or column; a missing table disables orders with one log line naming the migration.
- `runtime/GuildOrderContract.h` is the shared record (`OrderRecord`), the state and reason enums, and the token formatters both the `.tb` verbs and the addon use.
- `LoadOrders` reads the rows once at world start. `GiveOrder` writes a row and feeds the step history (below). `SetOrderWorking` starts and stops the per-bot tick.

## Per-tick step choice

`TickBotOrder` runs on the world thread from the bot manager's per-bot pass. It builds an input from the bot's bags, its profession knowledge, the auctioneer and delivery reach, and calls the pure policy in `runtime/GuildOrderWorkPolicy.h`:

1. Count `delivered + in bags` against the ordered amount.
2. When the amount is reached, deliver to the guild bank, else by mail.
3. Otherwise acquire: a craft order tries craft, then gather, then the auction house; a farm order gathers (or fishes); a buy order only uses the auction house.
4. With nothing to try, the order ends `cant` with reason `no source` (or a fishing-specific verdict).

A full-bags farm order first tries `ai::MakeRoomForGuildOrderLoot` (`ai/playerbot/strategy/actions/LootRollAction.cpp`): it destroys grey items first, cheapest stack first, and never touches green or better, quest items, tools, ammo, reagents, keys, the hearthstone or bags. With nothing to destroy the order ends `cant` with reason `bagsfull`.

## Providers

`TbHostGuildOrders.h` defines three seams; the host installs its implementations and tests can replace them:

- **Gather/craft:** `HostGatherCraftProvider` uses the host profession errands (`host/TbHostProfession.cpp`), fishing (`host/TbHostGuildOrderFish.cpp`) and the bot's own recipe spells. A node must already be within 150 yards; the bot does not plan a trip to a remote farm zone. Skinning is gated by `host/TbHostGuildOrderSkin.cpp`, which indexes `skinning_loot_template` at boot and lets the loot stack take a Working farm order's skin loot.
- **Auction:** `HostAuctionBuyProvider::StepBuyOnAuction` finds the nearest faction auctioneer within 300 yards, walks to it, queries the item's listings once, and buys the first affordable listing through `ai::DecideAhBuy` (`ai/playerbot/policy/ah/AhBuyPolicy.h`). It never re-lists a surplus and has no reach-deadline/camping logic. The gold budget is the bot's own purse, reduced by the order's `gold=` cap and what it already spent.
- **Delivery:** `HostDeliveryProvider::Deliver` tries a guild-bank vault within 300 yards first, deposits whole stacks through the core's guild-bank handler, and otherwise mails the items. Mail recipients follow `DecideMailRecipient` (`host/TbHostGuildPure.h`): the giver when a player gave the order, else the guild leader only when that leader is a bot; a real player is never mailed by default.

## Guild bank and gold

- Orders deposit items; they never withdraw them. There is no guild-gold withdrawal, so `AiPlayerbot.GuildOrderBankWithdraw` and `GuildOrderBankGold` are not read here.
- `host/TbHostGuildOrderQueues.cpp` is the refund queue: a row holding `guildGoldHeld` is walked to a guild banker and the money repaid; a repaid stub is deleted. Nothing on this build creates a new debt, but the queue keeps old rows honest.

## Permissions and the bot rank

- `host/TbHostGuildOrderAccess.cpp` implements the permission check (`MayGiveOrders`) and the access report. A real player in the bot's guild whose rank may edit officer notes may give orders.
- The switch is `TortoiseBots.World.GuildOrderBotRank` in `tortoisebots.conf`, not `AiPlayerbot.GuildOrderBotRank`.
- A bot joining a guild with a real player is moved to the stored bot rank (always a real rank above the guild master). The server does not open bank tabs on the bots' behalf: `BuildAccessReport` reads the tab minimum ranks from `guild_bank_tabs` and reports which tabs the bot rank reaches, so a permission the master has not granted shows as denied.
- `IsOrderBot` decides which characters the rank rule may touch: pool characters, online or offline, never a player's own character. There is no warmed "management gate": the rank reach is a read-only report, and the guild-management commands are not blocked by the module.
- `AccessLine` (`host/TbAddonOrdersPure.h`) formats the access report into `TBM:ORDER_ACCESS`; no bank row means no tabs.

## Addon transport

`host/TbAddonOrders.cpp` registers the `order` verb on the TBM addon channel. It reshapes the read handlers (`host/TbHostGuildOrderRead.cpp`) into TBM lines so the two transports cannot drift, and mutates through the same store and permission gates as `.tb guildorder`. It advertises `guild-orders`, `guild-order-steps` and `guild-order-nobuy`.

- Farm and craft orders are accepted. A Buy order is refused with `buy-disabled`; an enchant order is refused as not available on this server.
- Search, preview, list, access and steps are read-only. Search returns at most 30 rows, usable rows first, then by name match, then name.
- `.tb guildorder` remains the GM/debug path for the same store.

## Step history

`OrderHistory` (in `TbHostGuildOrderRead.cpp`) keeps up to 16 entries per bot, memory only. `GiveOrder` appends one entry per `(state, reason, supplyStep, supplyItemId)` change; `.tb guildorder steps` and the addon's `order <bot> steps` read it oldest first with an age in seconds. It is not persisted, so a restart, a new order or a reassign starts empty.

## Not in this build

Sub-orders, the need list, keep/restock rounds, the order time limit, guild-gold spending, the relist ledger and the farm-zone planner described in the Turtle order engine are not compiled here; their host files are replaced by the single store, the providers above and the addon refusal paths.
