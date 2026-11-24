---
id: server-settings-guild-orders
title: Guild order settings
category: server
summary: Configuration switches for guild bank access and the bot rank that guild orders use.
tags: [config, guild, orders, guild-bank, rank]
relates_to:
  - play-guild-orders
  - server-settings-economy
---

# Guild Order Settings

Guild orders use the guild bank and one shared bot rank. The rank switch lives in `tortoisebots.conf`; see [How configuration works](../configuration.md). Bank tab permissions still apply.

## Summary

| Setting | Default | What it does |
| :--- | :---: | :--- |
| [`TortoiseBots.World.GuildOrderBotRank`](#tortoisebotsworldguildorderbotrank) | `1` | Gives every bot in a guild with a real player one shared rank. |
| [`AiPlayerbot.CraftToolsAlways`](#aiplayerbotcrafttoolsalways) | `0` | Keeps the tools crafting recipes need in the bot's bags. |

## Settings

### `TortoiseBots.World.GuildOrderBotRank`
Whether bots in a guild with a real player share one rank for guild bank access.
**Default:** `1` · **Values:** `0` off, `1` on · **Restart:** no, read on demand (`.reload config` applies it)

When on, a bot joining such a guild takes the guild's bot rank (the lowest by default), so all bots share one rank the master can open bank tabs to. A rank you give a bot by hand stays. Guilds made only of bots keep their own ranks. With the switch off the server applies no ranks and the access report shows no rank.

### `AiPlayerbot.CraftToolsAlways`
Keeps the tools a crafting recipe needs in the bot's bags.
**Default:** `0` · **Values:** `0` off, `1` on · **Restart:** needed

When on, a bot never treats a profession tool as spare loot: the tools a crafting spell needs stay in its bags, so it never sells or auctions one. Off, a tool is treated like any other item the bot does not need. [`TortoiseBots.AutoTools`](../configuration.md#forevercore-module-keys-tortoisebotsconf) grants tools a bot is missing; this key keeps the ones it has.

See [bot rank and bank access](../../play/guild-orders.md#guild-bank-access).

## Features not in the Forever build

### `AiPlayerbot.GuildOrderPlayerBuy`
**Not on WoW Forever yet.** Players cannot give Buy orders from the addon: it refuses one with `buy-disabled`, whatever this key says. Only the GM command `.tb guildorder create <bot> <item> <amount> buy` accepts a Buy order. Farm and craft orders still buy missing materials on the auction house themselves.

### Bank withdrawals and guild gold

**Not on WoW Forever yet.** Orders never take items or gold from the guild bank; a bot spends only its own gold. `AiPlayerbot.GuildOrderBankWithdraw` and `AiPlayerbot.GuildOrderBankGold` therefore have no effect. The `gold=<copper>` cap on an order does still cap the bot's own auction spending.

### Sub-orders

**Not on WoW Forever yet.** A craft order does not ask another guild bot for a missing material; an item it cannot make or farm ends the order. `AiPlayerbot.GuildOrderSubOrders` and `AiPlayerbot.GuildOrderSubOrderDepth` do nothing.

### `AiPlayerbot.GuildOrderAuctionVisits`
**Not on WoW Forever yet.** The Forever auction step is one search at the nearest faction auctioneer within 300 yards, so there is no counted-visit exit.

### `AiPlayerbot.GuildOrderBotRank`
**Not on WoW Forever yet.** This key has no effect; the switch is `TortoiseBots.World.GuildOrderBotRank` above.
