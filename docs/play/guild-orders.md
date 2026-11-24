---
id: play-guild-orders
title: Guild Orders
category: play
summary: How to send guild bots to farm or craft items for the guild bank, and what each order state means.
tags: [guild, orders, farm, craft, guild-bank]
relates_to:
  - play-commands-roster-and-hiring
  - play-commands-addon
  - play-getting-started
  - server-settings-guild-orders
---

# Guild Orders

In your own guild you can send a bot to fetch items for real: it crafts or gathers them and brings them to the guild bank. No cheats: the bot gathers, crafts and shops the same way a player would. Orders are given and followed in the **TortoiseBotsForeverManager** addon (the Guild tab), not from chat.

## Quick start

Before you start:

- A bot of yours in the guild. Invite a pool bot like any player (`/ginvite <name>`); it accepts when it may (see [Guild bots](getting-started.md#guild-bots)). Hired companions always refuse.
- Your guild rank must be allowed to edit officer notes.
- For bank delivery, an unlocked guild bank with a tab open to the bots' rank (see [Guild bank access](#guild-bank-access)). Without it, finished items go by mail.

Example: 20 Copper Ore from a bot:

1. Open the addon (`/tbfm`, or `/tb`), go to the **Guild** tab and press **Order** next to the bot. Search for "copper ore", pick the Farm order and set the amount to 20.
2. Press Send. The bot checks its own bags, then works the order.
3. When the 20 are together, it walks to a guild banker and deposits them. If the bank refuses, it mails them to you.
4. The order list shows the delivered count while it runs, and the row ends `done` or `mailed`.

## Order types

| Order | What the bot does |
| :--- | :--- |
| Farm | Gathers a herb or ore node it can see, or fishes. For a skinning item, the mobs it fights while the order runs can fill it. Works in the area it is already in; it does not plan a trip to a distant zone. |
| Craft | Crafts the item if it knows the recipe. It collects missing reagents from its bags and mailbox, crafts a missing intermediate, gathers a reagent node in reach, or buys a missing reagent on the auction house within the order's gold cap. With no recipe, it gathers or fishes for the item itself. |

The addon preview checks the order before it is created: an item the bot cannot source, or a craft it cannot make, is refused there.

**Not on WoW Forever yet:** Buy orders (the server refuses them), enchant orders (refused), sub-orders to other guild bots, guild bank withdrawals and guild gold, the order time limit, keep/restock orders and the materials-you-bring option. The last three are stored but do not change what the bot does.

## States

The order row shows a state and, for a refusal, a reason.

- The count is what reached the guild bank (or the giver by mail), not what the bot still carries. While an order runs, the carried figure shows items in the bot's bags and mailbox not delivered yet; once handed off (`done` or `mailed`) it drops to `0`, while a `cant` row keeps the last scanned figure.
- A finished order stays visible until you cancel it or give a new one.

| State | Meaning |
| :--- | :--- |
| `working` | The bot is crafting, gathering, fishing or buying. |
| `delivering` | Amount reached (or bags full): on the way to a guild banker or a mailbox. |
| `done` | Deposited in the guild bank. |
| `mailed` | The bank refused or could not be reached; items mailed to the giver. |
| `noskill` | The item needs a profession the bot lacks, or its skill is too low. |
| `cant` | No source the bot can use, no reachable banker or mailbox, or no room in the bags (`bagsfull`). |

## Where the bot gets items

For the ordered item, the bot checks its own bags first. It crafts when it can, otherwise it gathers a node in sight or fishes. A craft order also tries the auction house for missing materials, spending its own gold within the `gold=` cap. When nothing is left to try, the order ends `cant`.

When its bags are full and it has none of the ordered item, a farm order throws out grey or white junk to make room: grey before white, cheapest stack first, and never green or better items, quest items, tools, ammo, reagents a spell it knows uses, its hearthstone, keys or bags. With nothing it may throw out, the order stops at once as `cant` with reason `bagsfull` instead of idling at nodes it cannot loot.

## Delivery

When the amount is reached, the bot walks to a guild banker within reach and deposits into the guild bank. The bank must be unlocked and have a tab the bots' rank may deposit into; otherwise, or when no banker is within reach, the bot mails the items to you from the nearest mailbox.

- The bank may get a few more than ordered: deposits move whole stacks.
- If neither a banker nor a mailbox can be reached, the order ends `cant`.

## Guild bank access

Every bot that joins a guild with at least one real player is moved to the guild's **bot rank** (the lowest by default), so all bots share one rank.

- **Your promotions stay:** promote or demote a bot in the normal guild window and it keeps that rank, also after a restart. A later rank change made in the addon moves every bot.
- **Open the tabs yourself:** the server does not open any bank tab for the bots. Open the tab(s) they should use to the bot rank in the guild bank window; bots then deposit into them. If every reaching tab refuses, the bot mails the items instead.
- **Same rules as players:** a tab open to the lowest rank is open to every guild member.
- **Guild gold:** gold is not spent from the bank; orders use the bot's own gold only.

The addon shows the bot rank and the tabs it can reach in the order window's header. The header turns red when a bank tab is closed to the bots; open it (or pick a higher rank) before giving orders that need the bank. Guilds without an unlocked bank show no tabs.

The switch is [`TortoiseBots.World.GuildOrderBotRank`](../server/settings/guild-orders.md#tortoisebotsworldguildorderbotrank); with it off, every rank is left alone.

## Who may give orders

You must be a real player in the bot's guild, and your guild rank must be allowed to edit officer notes (set in the normal guild rank settings). Anyone else gets an error. Notes are only the permission check: orders are given in the addon, and anything written in officer notes is ignored.

## Known limitations

- A farm order works only nodes or water the bot can already see; move the bot near the herbs, ore or fishing spot first.
- A bot standing somewhere with no route to a banker or mailbox gives up that step; move it somewhere it can travel from.
- A shop or auction trip can still take the bot on a long walk.
- A `bankdenied` reason can still appear when the tab access looks right.
