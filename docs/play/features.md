---
id: play-features
title: What Bots Can Do
category: play
summary: Catalog of what the ForeverCore build does, with its player commands, bot engine, pool, guild orders, auction market, queues, switches and defaults.
tags: [features, capabilities, settings, defaults]
relates_to:
  - play-getting-started
  - play-commands
  - play-known-limitations
  - server-configuration
---

# What Bots Can Do

This page describes the TortoiseBots module as it runs on **ForeverCore**. Features of the Turtle WoW version that are not built here are listed at the end: [Not on ForeverCore yet](#not-on-forevercore-yet).

How to read the tables:

- **On?** — the shipped default. "Always" means there is no switch.
- **Where** — the line in `tortoisebots.conf` (`TortoiseBots.*`, installed in `worldserver.conf.d/tortoisebots.conf`) or `aiplayerbot.conf` (`AiPlayerbot.*`) that changes it. Only the server admin can change it.
- **More** — where it is explained.

Two things must be true before any row below works:

1. The server is built with the bot engine (`TORTOISEBOTS_SLICE1=ON`) and the module is enabled (`TortoiseBots.Enable = 1`, off by default). Without the engine there are no bots and `.bot` is unknown; without the switch the module loads but does nothing.
2. Pool bots need the pool turned on and started; your own alts are logged in by you with `.bot add`.

New here? Read [Getting Started](getting-started.md) first.

## Your Own Characters

| Feature | On? | Where | More |
| :--- | :---: | :--- | :--- |
| Log one of your own characters in as a bot; it joins your party | Yes | `TortoiseBots.Player.Commands = 1` | [`.bot add`](commands/roster-and-hiring.md#logging-in-and-out) |
| Up to four of your characters online as bots at once | Yes | `TortoiseBots.Player.MaxBots = 4` | [Roster](commands/roster-and-hiring.md) |
| `.bot logout` logs one out | Always | — | [Roster](commands/roster-and-hiring.md) |
| `.bot who` lists the bots you may order; a GM may add `all` | Always | — | [Inspecting](commands/bot-panel.md) |
| `.bot stats [bot]` gives one line about the fleet or one bot | Always | — | [Inspecting](commands/bot-panel.md) |
| `.bot summon <bot>` brings a bot to your side | Yes | `AiPlayerbot.OwnedBotSummonCooldown = 300` (seconds between summons of the same bot) | [Summon](commands/roster-and-hiring.md#summon) |
| `.bot follow`, `stay`, `attack`, `flee` give a bot one order | Always | — | [Whisper commands](commands/whispers.md) |
| `.bot maintenance <bot>` refills the kit and repairs | No | `AiPlayerbot.OwnedBotMaintenanceEnabled = 0` | [Maintenance](commands/roster-and-hiring.md#maintenance) |
| `.bot autogear <bot>` dresses an owned bot in fitting gear | No | `AiPlayerbot.OwnedBotAutogearEnabled = 0` | [Autogear](commands/roster-and-hiring.md#gearing) |
| `.autogear` for your own player character | GM only | `AiPlayerbot.PlayerAutogearForAll = 0` | [Autogear](commands/roster-and-hiring.md#autogear) |
| A player whose client never sent an add-on message is told once where to get the add-on | No | `TortoiseBots.Player.AddonHint = 0` | [Addon](commands/addon.md#installation) |

The bot answers `TortoiseBots: <verb>: ...` on the channel you typed on. A refused order names a short code (`refused too-many: ...`), never an account.

## Party Controls

| Feature | On? | Where | More |
| :--- | :---: | :--- | :--- |
| `attack`, `stop`, `follow`, `stay`, `flee`, `guard`, `come`, `hold` orders | Always | — | [Party actions](commands/party-actions.md) |
| `pull`, `pullback`, `end pull` with seconds or `ranged`/`body` | Always | — | [Addon actions](commands/party-actions.md#addon-action-buttons) |
| `interrupt` picks a party member who can interrupt | Always | — | [Addon actions](commands/party-actions.md#addon-action-buttons) |
| `focus skull` marks your target and sends the party at it | Always | — | [Addon actions](commands/party-actions.md#addon-action-buttons) |
| `cc clear` clears a crowd-control mark | Always | — | [Crowd control](commands/party-actions.md#crowd-control) |
| Assigning a party CC mark (`cc <mark>`) | No | — | [Known limitations](known-limitations.md) |
| Behaviour toggles: `loot`, `aoe`, `autocc`, `savemana`, `boost`, `threat`, `potions`, `mimic`, `spread`, `hold`, `noblink`, `offheal`, `offdps` | Always | — | [`.bot behavior`](commands/bot-panel.md) |
| A shaman's totem set, a paladin's blessing, an aura/aspect/poison/sting/pet choice | Always | — | [`.bot choice`](commands/bot-panel.md) |
| `rest`, `drink`, `eat`, `repair`, `sell`, `learn`, `release` orders | Always | — | [Whisper commands](commands/whispers.md) |

These are the party orders both the `.tb order` console command and the addon's `action` verb accept. Chat users get the four movement/combat orders (`.bot follow`, `stay`, `attack`, `flee`); the rest are reached through the addon or as a server admin with `.tb order`.

## Combat & Classes

| Feature | On? | Where | More |
| :--- | :---: | :--- | :--- |
| Class rotations for all nine classes, role and spec from talents | Always | — | [Class overview](../classes/overview.md) |
| Party bots help each other against the same attackers | Yes | `AiPlayerbot.ShareTargets = 1` | [Shared behaviour](../classes/overview.md#shared-bot-behaviors) |
| Tank opens a pull with a ranged weapon | Yes | `AiPlayerbot.RangedPull = 1` | [Addon actions](commands/party-actions.md#addon-action-buttons) |
| Bots step out of harmful ground effects | Yes | `AiPlayerbot.HazardEvasionDistance = 12` | [Raid reactions](dungeons-and-raids.md#hazards) |
| Hunter: Aspect of the Viper when low on mana | Yes | `AiPlayerbot.HunterViperOnLowMana` | [Hunter](../classes/hunter.md) |
| Paladin: keeps Judgements up | Yes | `AiPlayerbot.PaladinJudgementUpkeep` | [Paladin](../classes/paladin.md) |
| Rogue: Feint and Vanish to drop threat in dungeons | Yes | `AiPlayerbot.RogueDungeonThreatDrops` | [Rogue](../classes/rogue.md) |
| Warlock: demon picked by spec | Yes | `AiPlayerbot.WarlockPetBySpec` | [Warlock](../classes/warlock.md) |

## Dungeons & Raids

| Feature | On? | Where | More |
| :--- | :---: | :--- | :--- |
| Bots fight, interrupt, crowd-control and loot in an instance | Always | — | [Dungeons & raids](dungeons-and-raids.md) |
| Party bots roll on group loot | Always | — | [Loot rules](dungeons-and-raids.md#loot) |
| A commanded bot's body in a dungeon releases and returns after the backstop | Yes | `TortoiseBots.Slice.DeadBackstopSeconds = 180` | [Wipes](dungeons-and-raids.md#dead-bots) |
| A bot far behind holds before a skipped pack | No | `AiPlayerbot.DungeonFollowHoldBeforePacks = 0` | [Following](dungeons-and-raids.md#following) |
| Run leader walks a stored dungeon plan (a server-admin command) | Yes | `AiPlayerbot.DungeonSelfClear = 1` | [Run leader](dungeon-run-leader.md) |
| Raid consumables: the table, the role rules and the `.tb state` read-outs (bots do not use them yet) | No | `TortoiseBots.RaidConsumables = 0` | [Known limitations](known-limitations.md#features-that-are-not-on-forevercore-yet) |
| Boss-specific raid tactics (Molten Core to Naxxramas) | No | — | [Not on ForeverCore yet](#not-on-forevercore-yet) |
| Resistance gear swapped before a raid boss's school (not armed automatically yet) | No | `TortoiseBots.RaidResistGear.Enable = 0` | [Resist gear](resist-gear.md) |
| Turtle custom raids (Emerald Sanctum, Karazhan) | No | — | [Not on ForeverCore yet](#not-on-forevercore-yet) |

## The Living World

| Feature | On? | Where | More |
| :--- | :---: | :--- | :--- |
| A pool of bots that log in and out on a schedule | No | `TortoiseBots.Pool.Enable = 0`, then `.tb pool start` | [Bot pool](../server/bot-pool.md) |
| Free bots quest, grind, loot, vendor, repair and train | Always | — | [Living world](../world/living-world.md) |
| Free bots run capital errands (mail, bank, trainer, auctioneer) and duel at the gates | Yes | `TortoiseBots.World.CapitalErrands = 1` | [Living world](../world/living-world.md) |
| Free bots fish at a nearby hole | Yes | `TortoiseBots.World.Fishing = 1` | [Professions](../world/living-world.md) |
| Free bots disenchant junk and enchant their own gear | Yes | `TortoiseBots.World.Disenchant = 1`, `TortoiseBots.World.EnchantGear = 1` | [Professions](../world/living-world.md) |
| Free bots gather herbs and ore | No | `TortoiseBots.World.Professions = 0` | [Professions](../world/living-world.md) |
| Free rogue bots pick pockets and open lockboxes | Yes | `TortoiseBots.World.Pickpocket = 1`, `TortoiseBots.World.Lockboxes = 1` | [Rogue](../classes/rogue.md) |
| Free bots mail lockboxes to a guild rogue and get the contents back | Yes | `TortoiseBots.World.LockboxMail = 1` | [Social](../world/social.md) |
| Nearby free bots form a party for a shared group quest | Yes | `TortoiseBots.World.GroupQuests = 1` | [Social](../world/social.md) |
| Free bots take part in guilds (accept an invite, answer guild whispers) | Yes | `TortoiseBots.World.Guilds = 1` | [Social](../world/social.md) |
| A player asks a bot-run guild for an invite by whisper | No | `TortoiseBots.World.GuildAcceptPlayers = 0` (needs `TortoiseBots.World.Guilds = 1`) | [Social](../world/social.md) |
| Free bots found their own guilds | No | `TortoiseBots.World.GuildForming = 0` | [Social](../world/social.md) |
| Free bots who are in a guild mail surplus gold and materials to guildmates | Yes | `TortoiseBots.World.GuildMail = 1` | [Social](../world/social.md) |
| Free bots seek world PvP | Yes | `AiPlayerbot.WorldPvpSeek = 2`, `AiPlayerbot.WorldPvpSeekBotPercent = 25` | [PvP](../world/queues.md) |
| Free bots chase rare spawns | Yes | `AiPlayerbot.HuntRares = 1` | [Living world](../world/living-world.md) |
| Free bots pick a destination and travel there by foot, flight path or boat/zeppelin | Always | — (`AiPlayerbot.GenerateTravelNodes = 1` builds and saves the graph) | [Travel](../world/living-world.md) |
| Travel for quests, grind spots, towns, vendors and repairs, and leaving an outgrown zone | Yes | — | [Living world](../world/living-world.md) |
| Travel to trainers, the auction house, mail, bosses or gathering nodes as a destination | No | — | [Not on ForeverCore yet](#not-on-forevercore-yet) |
| Autonomous alt-bots (your alt keeps playing after it leaves your group) | No | — | [Not on ForeverCore yet](#not-on-forevercore-yet) |
| Camp spots and Traveler's Tents | No | — | [Not on ForeverCore yet](#not-on-forevercore-yet) |

## Levelling, Spells and Gear

| Feature | On? | Where | More |
| :--- | :---: | :--- | :--- |
| Bots learn their class trainer spells for free on login and level-up | Yes | `TortoiseBots.AutoLearnOnLevel = 1` | [Class spells](../world/living-world.md#class-spells) |
| Bots spend free talent points on a premade build | Yes | `TortoiseBots.AutoTalentsOnLevel = 1` | [Talents](getting-started.md#levelling-spells-and-gear) |
| Pool bots learn two professions, First Aid and Cooking at level 5 | Yes | `TortoiseBots.AutoProfessions = 1` | [Professions](../world/living-world.md) |
| Pool and free bots get the gathering and crafting tools they need | Yes | `TortoiseBots.AutoTools = 1` | [Professions](../world/living-world.md) |
| Hunter bots get a starter pet and its control spells | Yes | `TortoiseBots.AutoHunterPet = 1` | [Hunter](../classes/hunter.md) |
| Pool and free bots get level-fitting food and water | Yes | `TortoiseBots.AutoFood = 1` | [Living world](../world/living-world.md) |
| Bots learn the class-quest spells a player would earn | No | `TortoiseBots.ClassQuestSpells = 0` | [Class spells](../world/living-world.md#class-spells) |
| Bots keep gear that fits their level as they level | Yes | `AiPlayerbot.RandomGearProgression = 1` | [Gear](../world/living-world.md#gear-evaluation--upgrades) |
| Bots equip better gear they loot | Yes | `AiPlayerbot.AutoEquipUpgradeLoot = 1` | [Gear](../world/living-world.md#gear-evaluation--upgrades) |
| Reputation and PvP items in generated gear | No | `AiPlayerbot.RandomGearAllowReputation = 0`, `AiPlayerbot.RandomGearAllowPvP = 0` | [Gear](../world/living-world.md#gear-evaluation--upgrades) |
| Bots learn to ride and buy their race's mount with their own gold | Yes | `AiPlayerbot.LevelUpMounts = 1` | [Mounts](../world/living-world.md#mount-rules) |
| Shop and other-race mounts | No | `AiPlayerbot.RandomBotShopMounts = 0` | [Mounts](../world/living-world.md#mount-rules) |

## Guild Orders

| Feature | On? | Where | More |
| :--- | :---: | :--- | :--- |
| Send a bot in your guild to farm, buy or craft an item for the guild bank | Yes | `.tb guildorder create <bot> <item> <amount> [farm\|craft\|buy]` | [Guild orders](guild-orders.md) |
| A dedicated crafting order with a status board | Yes | `.tb craftorder create <bot> <item> <amount>` | [Guild orders](guild-orders.md) |
| Mail an item to a guild enchanter with the subject `disenchant` and get the materials back | Yes | `TortoiseBots.World.GuildMail = 1` | [Guild orders](guild-orders.md) |
| Give the guild's bots one shared rank so they can open a bank tab | Yes | `TortoiseBots.World.GuildOrderBotRank = 1` | [Guild orders](guild-orders.md) |

## Economy (Auction House)

| Feature | On? | Where | More |
| :--- | :---: | :--- | :--- |
| Bots sell junk and repair at vendors | Always | — | [Economy](../world/economy.md) |
| Free bots post their own loot at a nearby auctioneer | No | `TortoiseBots.World.Auction = 0` (also needs `AiPlayerbot.AhMarketEnabled = 1`) | [Auction house](../world/economy.md#living-auction-house) |
| Free bots buy upgrades on the auction house | No | `TortoiseBots.World.Auction = 0` (also needs `AiPlayerbot.AhMarketBuyer = 1`) | [Auction house](../world/economy.md#living-auction-house) |
| A separate offline auction-house seller (`AiPlayerbot.AhBot.*`) | No | — | [Not on ForeverCore yet](#not-on-forevercore-yet) |

## Dungeon Fill & Battlegrounds

| Feature | On? | Where | More |
| :--- | :---: | :--- | :--- |
| `.bot lfg join <dungeon id> <tank\|healer\|dps>` fills your group with free bots | Yes | `AiPlayerbot.RandomBotLftEnabled = 1` | [Dungeon fill](../world/queues.md) |
| Bots keep the role they have; no borrowing a role | Yes | `AiPlayerbot.RandomBotLftAllowRoleBorrow = 0` | [Dungeon fill](../world/queues.md) |
| Battleground fill for human WSG and AB queues | No | `TortoiseBots.Bg.Enable = 0` | [Battlegrounds](../world/queues.md) |
| Bot-only battleground matches | No | `TortoiseBots.Bg.Autonomous = 0` | [Battlegrounds](../world/queues.md) |
| Alterac Valley, arenas and the random queue | No | `TortoiseBots.Bg.Maps = "wsg,ab"` | [Battlegrounds](../world/queues.md) |

## Server & Monitoring

| Feature | On? | Where | More |
| :--- | :---: | :--- | :--- |
| Web dashboard feed (roster, map, activity, issues) | Yes | `TortoiseBots.Dashboard.Enable = 1` | [Dashboard](../server/dashboard.md) |
| Run trace of a party, started by a server admin | No | `AiPlayerbot.RunTrace.Enabled = 0` | [Run trace](../server/run-trace.md) |
| Decision capture of a run leader | No | `AiPlayerbot.DecisionCapture.Enabled = 0` | [Decision capture](../server/decision-capture.md) |
| Module log lines in their own category | Yes | `Logger.tortoisebots = 3,Console Server` | [Logging](../server/settings/logging.md) |
| GameMaster `.tb` commands for the pool, queues and diagnostics | Always | — | [Admin commands](../server/admin-commands.md) |

## Not on ForeverCore yet

These are part of the Turtle WoW build that this page used to describe, or of old TortoiseBots releases. None of them is built into the Forever module; none has a setting here.

- **Hired companions and recruiters.** No `.bot hire`, no mercenary contracts, no recruiter world buffs, no gear tiers.
- **Autonomous alt-bots.** An own alt that leaves your group does not start playing by itself.
- **Claimed guild bots.** A pool bot cannot be claimed as your own by a guild invite.
- **Boss and raid tactics.** No per-boss interrupt, positioning or add rules; `AiPlayerbot.RaidReactions` is off and the boss-tactic tables are not loaded.
- **Turtle custom raids and dungeons.**
- **Travel destinations outside the reduced set.** Bots travel for quests, grind spots, towns, vendors, repairs and leaving an outgrown zone; a trainer, auction house, mailbox, boss or gathering node is not a travel destination.
- **Camp spots.**
- **The offline auction-house seller.** The organic bot market exists, the separate seller service does not.
- **Lineages and pool adoption.** The Forever pool creates and resets its own characters; it does not adopt old accounts or roll level-1 alts from level 60s.
- **`Enemy player` battleground roles and AV.** Only WSG and AB are wired.

For the full technical reason and the exact list of `not ported yet` features, see [Known limitations](known-limitations.md).
