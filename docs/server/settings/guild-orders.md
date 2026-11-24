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

## Settings

### `TortoiseBots.World.GuildOrderBotRank`
Whether bots in a guild with a real player share one rank for guild bank access.
**Default:** `1` · **Values:** `0` off, `1` on · **Restart:** no, read on demand (`.reload config` applies it)

When on, a bot joining such a guild takes the guild's bot rank (the lowest by default), so all bots share one rank the master can open bank tabs to. A rank you give a bot by hand stays. Guilds made only of bots keep their own ranks. With the switch off the server applies no ranks and the access report shows no rank.

See [bot rank and bank access](../../play/guild-orders.md#guild-bank-access).

## Features not in the Forever build

### `AiPlayerbot.GuildOrderPlayerBuy`
**Not on WoW Forever yet.** Players cannot give Buy orders at all: the addon and the server refuse a Buy order, whatever this key says. Farm and craft orders still buy missing materials on the auction house themselves.

### Bank withdrawals and guild gold

**Not on WoW Forever yet.** Orders never take items or gold from the guild bank; a bot spends only its own gold. `AiPlayerbot.GuildOrderBankWithdraw` and `AiPlayerbot.GuildOrderBankGold` therefore have no effect. The `gold=<copper>` cap on an order does still cap the bot's own auction spending.

### Sub-orders

**Not on WoW Forever yet.** A craft order does not ask another guild bot for a missing material; an item it cannot make or farm ends the order. `AiPlayerbot.GuildOrderSubOrders` and `AiPlayerbot.GuildOrderSubOrderDepth` do nothing.

### `AiPlayerbot.GuildOrderAuctionVisits`
**Not on WoW Forever yet.** The Forever auction step is one search at the nearest faction auctioneer within 300 yards, so there is no counted-visit exit.

### `AiPlayerbot.CraftToolsAlways`
**Not on WoW Forever yet.** Recipe tools on Forever come from [`TortoiseBots.AutoTools`](../configuration.md#forevercore-module-keys-tortoisebotsconf), which gives free and pool bots the tool of every profession they know, so a craft order does not need this key. `AiPlayerbot.GuildOrderBotRank` is also not read; the switch is `TortoiseBots.World.GuildOrderBotRank` above.
