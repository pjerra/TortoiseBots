---
id: server-settings-economy
title: Economy & auction house settings
category: server
summary: Configuration settings for the simulated auction house market, vendor trips, and bot economy.
tags: [config, economy, ah, auction-house, vendor]
relates_to:
  - world-economy
  - world-social
  - server-settings-world-population
---

# Economy & Auction House Settings

These settings configure the bot-run Auction House market, vendor selling thresholds, travel mount rules, and level-up gear upkeep. They go in `aiplayerbot.conf`; see [How configuration works](../configuration.md).

Administrative auction house commands are documented in [Admin commands](../admin-commands.md).

## Summary

| Setting | Default | What it does |
| :--- | :---: | :--- |
| [`AiPlayerbot.AhMarketEnabled`](#aiplayerbotahmarketenabled) | `1` | Lets free bots post their surplus on the Auction House. |
| [`AiPlayerbot.AhMarketInterval`](#aiplayerbotahmarketinterval) | `120` | Seconds between seller passes. |
| [`AiPlayerbot.AhMarketBatchSize`](#aiplayerbotahmarketbatchsize) | `1` | Sellers visited per pass (1..5). |
| [`AiPlayerbot.AhMarketBuyer`](#aiplayerbotahmarketbuyer) | `1` | Free bots standing at an auctioneer buy upgrades with their own gold. |
| [`AiPlayerbot.AhMarketChanceBuy`](#aiplayerbotahmarketchancebuy) | `10` | Percentage chance per pass that the buyer scan runs. |
| [`AiPlayerbot.AhMarketBudgetUs`](#aiplayerbotahmarketbudgetus) | `2000` | Microsecond time budget for one buyer page scan. |
| [`AiPlayerbot.AhMarketMaxOperations`](#aiplayerbotahmarketmaxoperations) | `32` | Maximum item probes in one buyer page scan. |
| [`AiPlayerbot.AhMarketMaxSpendPerBot`](#aiplayerbotahmarketmaxspendperbot) | `0` | Maximum copper for one buyer purchase (0 = no cap). |
| [`AiPlayerbot.AuctionPriceRefreshInterval`](#aiplayerbotauctionpricerefreshinterval) | `60` | Read but unused by the Forever build (see below). |
| [`AiPlayerbot.AhSellBlacklist`](#aiplayerbotahsellblacklist) | `18256` | Item ids bots never post to the Auction House. |
| [`AiPlayerbot.VendorBatchMinCount` / `VendorBatchMinBagSpace`](#aiplayerbotvendorbatchmincount--vendorbatchminbagspace) | `8` / `60` | Vendor-trip thresholds for level 5+ bots. |
| [`AiPlayerbot.LowLevelVendorBatchMinCount` / `LowLevelVendorBatchMinBagSpace`](#aiplayerbotlowlevelvendorbatchmincount--lowlevelvendorbatchminbagspace) | `3` / `25` | Vendor-trip thresholds for bots below level 5. |
| [`AiPlayerbot.LowLevelVendorMaxDistance`](#aiplayerbotlowlevelvendormaxdistance) | `600` | Maximum yard radius below level 5 bots travel to a vendor. |
| [`AiPlayerbot.MountBreakEvenFactor`](#aiplayerbotmountbreakevenfactor) | `1.5` | Safety factor on the distance before a bot mounts up. |
| [`AiPlayerbot.PoolGearTopUpOnLevel`](#aiplayerbotpoolgeartopuponlevel) | `0` | Opt-in: fill a pool bot's empty or outdated gear slots when it levels up. |

## Settings

### `AiPlayerbot.AhMarketEnabled`
Lets free bots post their surplus on the Auction House.
**Default:** `1` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Also needs [`TortoiseBots.World.Auction = 1`](../configuration.md#forevercore-module-keys-tortoisebotsconf). When both are on, free bots whose bags hold sellable stock walk to a faction auctioneer within 500 yards in their own zone and post it; a bot already standing at an auctioneer posts at once. Listings go through the core's auction handlers, so deposits and throttles are the core's.

The bots must be online, which means the pool has characters and a target; see [Managing & Resetting the Bot Pool](../bot-pool.md) for `TortoiseBots.Pool.Enable` and `TortoiseBots.Pool.TargetOnline`. A failed attempt is retried no sooner than the next interval.

</details>

### `AiPlayerbot.AhMarketInterval`
Seconds between seller passes.
**Default:** `120` · **Values:** seconds (5..3600) · **Restart:** needed

<details>
<summary>How it works in detail</summary>

The seller loop and the buyer roll run on this interval. A seller that is still walking keeps its own 45-second walk timeout.

</details>

### `AiPlayerbot.AhMarketBatchSize`
Sellers visited per seller pass.
**Default:** `1` · **Values:** integers (1..5) · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Caps how many eligible bots one pass considers. The rotation starts past the block it just handled, so the same bot is not always picked first. A visited bot posts every item its own sell rules accept.

</details>

### `AiPlayerbot.AhMarketBuyer`
Free bots standing at a faction auctioneer buy upgrades there with their own gold.
**Default:** `1` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

The buyer never walks or teleports: only a free, idle, ungrouped bot already within interact range of a faction auctioneer is considered. It pages the house, keeps uncommon-or-better weapons and armour it can use, skips its own and its account's listings, and buys only what its own item-usage value calls an upgrade. The price ceiling is three times the item's unit value (or four times the vendor buy price, ten times the sell price, when no unit value is known), and the bot's purse bounds it. Purchases go through the native buyout.

</details>

### `AiPlayerbot.AhMarketChanceBuy`
Percentage chance per market pass that the buyer scan runs.
**Default:** `10` · **Values:** percentage (0..100; values above 100 are treated as 100) · **Restart:** needed

<details>
<summary>How it works in detail</summary>

One roll per pass when `AiPlayerbot.AhMarketBuyer` is on. `100` scans on every pass; `0` never buys.

</details>

### `AiPlayerbot.AhMarketBudgetUs`
Microsecond time budget for one buyer page scan.
**Default:** `2000` · **Values:** microseconds (100..20000) · **Restart:** needed

<details>
<summary>How it works in detail</summary>

The scan stops probing items when this budget is reached, so a pass cannot stretch the world tick.

</details>

### `AiPlayerbot.AhMarketMaxOperations`
Maximum item probes in one buyer page scan.
**Default:** `32` · **Values:** operations (1..256) · **Restart:** needed

<details>
<summary>How it works in detail</summary>

A hard ceiling even when time remains within `AhMarketBudgetUs`. The scan also never probes more than eight items per page.

</details>

### `AiPlayerbot.AhMarketMaxSpendPerBot`
Maximum copper amount for one buyer purchase.
**Default:** `0` · **Values:** copper amount (0 = no cap) · **Restart:** needed

<details>
<summary>How it works in detail</summary>

A candidate whose cheapest listing is above this price is skipped. It does not track cumulative spend.

</details>

### `AiPlayerbot.AuctionPriceRefreshInterval`
Read into the settings object but no Forever build code uses it, so changing it does nothing.
**Default:** `60` · **Values:** seconds (5..3600) · **Restart:** needed

<details>
<summary>How it works in detail</summary>

The price-mirror refresh that reads this value lives in `runtime/PlayerbotRuntimeFacade.cpp`, which the Forever build does not compile.

</details>

### `AiPlayerbot.AhSellBlacklist`
Item ids bots never post to the Auction House.
**Default:** `18256` (Imbued Vial) · **Values:** comma-separated item ids · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Bots also never post items any vendor sells without a stock limit: any player can buy those at the vendor price, so no auction listing can be fair. Vendor-buy-to-flip uses the same rule.

</details>

### `AiPlayerbot.VendorBatchMinCount` / `VendorBatchMinBagSpace`
Vendor-trip thresholds for bots at level 5 or higher.
**Default:** `8` / `60` · **Values:** item count / bag-space percent · **Restart:** needed

<details>
<summary>How it works in detail</summary>

A bot travels to a vendor once its vendor-usable stock reaches the item count and its bags are at least the given percent full. It also heads to a vendor early when selling would cover the gold for its next trainable class ability rank.

</details>

### `AiPlayerbot.LowLevelVendorBatchMinCount` / `LowLevelVendorBatchMinBagSpace`
Vendor-trip thresholds for bots below level 5.
**Default:** `3` / `25` · **Values:** item count / bag-space percent · **Restart:** needed

<details>
<summary>How it works in detail</summary>

Beginner bots carry small starter bags, so the lower pair lets them sell their first loot and afford early spells.

</details>

### `AiPlayerbot.LowLevelVendorMaxDistance`
Maximum yard distance below level 5 bots may travel to a vendor.
**Default:** `600` · **Values:** yards (`0` disables the beginner radius) · **Restart:** needed

<details>
<summary>How it works in detail</summary>

General travel below level 5 is restricted to keep fragile characters near their starting camp. This radius is the vendor exception: a vendor farther than this is not chosen.

</details>

### `AiPlayerbot.MountBreakEvenFactor`
Safety factor applied to the break-even distance before bots mount up.
**Default:** `1.5` · **Values:** float multiplier (`0` = mount for any trip) · **Restart:** needed

<details>
<summary>How it works in detail</summary>

A bot compares the remaining travel distance with the break-even distance for its best usable mount: run speed times cast time divided by the speed bonus, times this factor. With `1.5` that is roughly 350 yards on a 9% mount, 50 yards on a 60% mount and 30 yards on a 100% mount.

</details>

### `AiPlayerbot.PoolGearTopUpOnLevel`
Fills empty and clearly outdated equipment slots when a masterless pool bot reaches a new level.
**Default:** `0` · **Values:** `0` off, `1` on · **Restart:** needed

<details>
<summary>How it works in detail</summary>

A pool bot at level 5 or higher gets its empty slots, and grey or white items six or more item levels below its level, filled from the gear generator the fresh seed uses. Green or better gear is never replaced; the shirt and tabard slots are left alone. The check runs once per level (after a restart, once at the current level) and only out of combat. Hired and guild-claimed bots are never touched.

</details>

## Features and keys the Forever build does not use

### `AiPlayerbot.AhBot.Enabled`
**Not on ForeverCore yet.** The offline AHBot stock service (seller account, listing pool, house targets, `AhBot.*` keys) is not built; the host answers "no auction bot sellers".

### `AiPlayerbot.AhBot.Buyer.Enabled`
**Not on ForeverCore yet.** The offline AHBot buyer is not built either.

### `AiPlayerbot.PoolMoneyTopUp`
**Not on ForeverCore yet.** The level-up money top-up for pool bots is not built.

### `AiPlayerbot.BotLootRateUncommon` / `AiPlayerbot.BotLootRateRare`
**Not on ForeverCore yet.** These keys are not in the Forever template. The extra green and blue roll only ever runs for a "random bot", and no bot is one in the Forever build, so both multipliers do nothing.

### Unused economy keys

`AiPlayerbot.AhMarketBuyValue` and `AiPlayerbot.AhMarketVariance` are read into the settings object and appear in the shipped template, but no code the Forever build compiles uses them: the organic buyer prices with the fixed three-times-unit-value ceiling above. Setting either changes nothing.

Legacy synthetic-supply keys (`AiPlayerbot.AhMarketSyntheticSupply`, `AhMarketChanceSell`, `AhMarketMaxQuality`, `AhMarketMaxLevel`, `AhMarketDynamicLevel`, `AhMarketLevelRefresh`, `AhMarketBidMin`, `AhMarketBidMax`, `AhMarketTimeMin`, `AhMarketTimeMax`) are not read by any build here; the config reader ignores unknown keys, so old lines load cleanly and do nothing.
