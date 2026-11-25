---
id: server-settings-world-population
title: World population & immersion settings
category: server
summary: The module's bot pool settings, the immersion keys the Forever build acts on, and the keys that do nothing here.
tags: [config, world, population, immersion]
relates_to:
  - world-living-world
  - world-social
  - server-bot-pool
  - server-configuration
---

# World Population & Immersion Settings

On the Forever build the world's bot population is the module's own pool, configured in `tortoisebots.conf`. The Turtle version's random-bot population keys (`AiPlayerbot.MinRandomBots` and its neighbours) were never ported and are not read here.

Free pool bots run the bot engine automatically once `TortoiseBots.Enable = 1` and the pool is enabled and started; they do not need `TortoiseBots.Slice.EngineByDefault = 1` or `.tb engine <bot> on`. A player's own bot (`.bot add`) runs it too. Only a bot a GameMaster logs in by hand stays on the simple follow-and-assist behaviour unless that switch is on or `.tb engine <bot> on` is used. See [Living world & autonomous bots](../../world/living-world.md#before-anything-moves).

## Population: the pool

| Setting | Default | What it does |
| :--- | :---: | :--- |
| `TortoiseBots.Pool.Enable` | `0` | The bot pool: module-created level-1 characters on module-created accounts, logged in on a schedule. Off by default. |
| `TortoiseBots.Pool.MaxCharacters` | `50` | Ceiling on the pool's total size. `0` creates nothing. |
| `TortoiseBots.Pool.CensusComposition` | `1` | New characters follow the live server's class, race and gender shares. `0` keeps the balanced vanilla sequence. |
| `TortoiseBots.Pool.CensusSpec` | `1` | A pool bot's spec is rolled with the live server's spec shares. `0` keeps the pool's own roll. |
| `TortoiseBots.Pool.CreatePerMinute` | `30` | How many characters `pool create` may make per minute. `0` creates nothing. |
| `TortoiseBots.Pool.TargetOnline` | `0` | How many pool bots `.tb pool start` keeps in the world. `0` runs the schedule but logs nobody in. |
| `TortoiseBots.Pool.LoginsPerMinute` | `20` | Login cap per minute (and at most one per second). |
| `TortoiseBots.Pool.SessionMinutes.Min` / `.Max` | `30` / `90` | A session's length, rolled per login. |
| `TortoiseBots.Pool.OfflineMinutes.Min` / `.Max` | `5` / `20` | How long a bot stays away between sessions. |
| `TortoiseBots.Pool.PopulationLow` / `.PopulationHigh` | `10` / `50` | The presets behind `.tb pool population low` and `high`. |
| `AiPlayerbot.LevelLadder` | `1` | `aiplayerbot.conf`: chooses logins by level band, not only by the bot offline longest. `0` restores the plain order. |
| `AiPlayerbot.LevelLadderBandSize` | `5` | `aiplayerbot.conf`: levels per band, 1-5, 6-10, ... 56-59, and the level cap alone. |
| `AiPlayerbot.LevelLadderMaxLevelShare` | `10` | `aiplayerbot.conf`: percent of `TargetOnline` the level cap's band may hold; a hard cap, the rest stay offline as a reserve. |
| `AiPlayerbot.LevelLadderLogMinutes` | `5` | `aiplayerbot.conf`: how often the online/target/pool count of every band is logged. |
| `TortoiseBots.Pool.Reset` | `"off"` | Rebuilds the pool on world startup. Read only at startup. |

The scheduler logs in one bot per second at most, alternates factions and takes the bot that has been offline longest first. The level ladder (on by default) puts the band with the largest shortfall first and the highest level inside it, with the level cap's band held to its percent of the target as a reserve of offline max-level characters; `LevelLadder = 0` turns it off. One session per account is the rule, so the online count is capped by the number of accounts the pool sits on. A bot that gets a master leaves the count and is never logged out by the pool. A lowered target logs out the newest sessions first, never a dead or fighting bot. Characters, commands and the reset's safety checks are in [The bot pool](../bot-pool.md). `.reload config` re-reads every pool key except `TortoiseBots.Pool.Reset`, which is read only at world startup.

With `TortoiseBots.Pool.CensusComposition = 1` (the default) new characters follow what real WoW Forever players run (aotc.gg, read 2026-11-24): the class by its share of its faction, the race within the faction and only where ForeverCore can make the pair, and the gender by the class's female share. The faction still alternates, so the pool stays 50/50 Alliance and Horde. The two Skyborne races (95 High Order, 96 Windshaper) are part of the mix. At `0` the pool uses the balanced vanilla sequence it had before. With `TortoiseBots.Pool.CensusSpec = 1` (the default) a pool bot's spec tree is rolled with the live spec shares of its class; `0` keeps the pool's own roll, and a commanded bot always gets the spec its role needs. The Server tab of the [dashboard](../dashboard.md) compares the pool with the census.

## Free-bot behaviour (`aiplayerbot.conf`)

### `AiPlayerbot.EnableGreet`
Turns the "greet a passing real player" row on or off.
**Default:** `1` · **Values:** `0` off, `1` on · **Restart:** needed

### `AiPlayerbot.GreetCooldown`
Seconds before a bot may greet the same player again.
**Default:** `600` · **Values:** seconds · **Restart:** needed

Leaving and coming back is a new meeting, but the cooldown still spaces the wave.

### `AiPlayerbot.RandomBotGreet`
Lets masterless pool bots greet players, not only bots with a player master.
**Default:** `0` · **Values:** `0` off, `1` on · **Restart:** needed

Bots never greet other bots, whatever this says.

### `AiPlayerbot.PassingBuffs`
Pool bots buff (and sometimes heal) real players they pass.
**Default:** `1` · **Values:** `0` off, `1` on · **Restart:** needed

1 in 5 per meeting for a buff, 1 in 2 for a heal on a player below 35% health, with per-pair cooldowns. See [Passing buffs](../../world/social.md#passing-buffs).

### `AiPlayerbot.TalentsInPublicNote`
Bots in a guild write their spec, talents and professions into its public note.
**Default:** `1` · **Values:** `0` off, `1` on · **Restart:** needed

### `AiPlayerbot.RpgDelay`
Milliseconds of roleplay pause at a town NPC.
**Default:** `10000` · **Values:** milliseconds · **Restart:** needed

A pool bot idling next to a service NPC it needs uses the full pause; one with nothing to do there takes a fifth of it and moves on.

### `AiPlayerbot.RandomBotShopMounts`
Restricts bots to their own race's vendor mounts.
**Default:** `0` · **Values:** `0` any vendor mount, `1` own race only · **Restart:** needed

Bots get mounts by buying them in a capital at 40 and 60, so this decides which ones are offered.

### `AiPlayerbot.RandomBotNameStyleWeights`
How new pool names are drawn from the three name styles.
**Default:** `55,20,25` · **Values:** three non-negative integers with a positive sum — race-flavour, compound, fun · **Restart:** needed

### `AiPlayerbot.RandomBotNamesFile`
An external text file of extra names and name patterns for new pool characters.
**Default:** empty · **Values:** a file path · **Restart:** needed

Empty uses the built-in name tables.

## Travel, death & getting unstuck

### `AiPlayerbot.AvoidHostileTowns`
Keeps a masterless bot out of a hostile faction's guarded towns.
**Default:** `1` · **Values:** `0` off, `1` on · **Restart:** needed

Both the destination pick and the walk itself are checked, so a route through the other side's town or an area far above the bot is refused. Neutral towns stay open.

### `AiPlayerbot.LeaveOutgrownZones`
Stops a masterless bot from taking errands in areas it outgrew.
**Default:** `1` · **Values:** `0` off, `1` on · **Restart:** needed

A destination whose area is ten or more levels below the bot is refused unless it is a capital city.

### `AiPlayerbot.WalkTargetOffset`
Yards of lateral spread added to open-world walk destinations.
**Default:** `2.5` · **Values:** yards, `0` disables · **Restart:** needed

Keeps bots from walking single file along the same line. Instances and very short trips are left exact.

### `AiPlayerbot.HuntRares`
Lets a free bot go for a living rare creature of its level within sight.
**Default:** `1` · **Values:** `0` off, `1` on · **Restart:** needed

### `AiPlayerbot.AllowRepairedTaxiPaths`
Lets a bot board the two taxi paths whose data needs repair.
**Default:** `0` · **Values:** `0` off, `1` on · **Restart:** needed

Bots do fly taxi routes. Two paths in the shipped flight data are incomplete (paths 274 and 355: missing node data). At `0` a bot refuses to board a flight that uses either path; at `1` it may fly them, which assumes the core has repaired that data.

### `AiPlayerbot.StuckTravelTeleport`
Lets a stuck masterless bot skip the rest of a trip.
**Default:** `1` · **Values:** `0` off, `1` on · **Restart:** needed

A trip that makes no progress until its retry limit teleports the bot next to its destination (or to the nearest route node), once every ten minutes, and only when no real player is within 150 yards of either end.

### `AiPlayerbot.UnstuckHearthLevelFit`
Requires the hearthstone bind point to be within ten levels of the bot.
**Default:** `1` · **Values:** `0` off, `1` on · **Restart:** needed

A bot whose bind point is far below its level repops instead of hearthing.

### `AiPlayerbot.UnstuckHearthMinDistance`
Yards from its bind point below which a bot does not hearth to get unstuck.
**Default:** `300` · **Values:** yards, `0` disables · **Restart:** needed

Hearthing from next door is no rescue; the bot repops to the nearest graveyard instead.

### `AiPlayerbot.RepopAtGraveyard`
A dead masterless bot comes back at the nearest graveyard of its faction instead of its starting zone.
**Default:** `1` · **Values:** `0` off, `1` on · **Restart:** needed

### `AiPlayerbot.DeathReleasePauseMinSec` / `DeathReleasePauseMaxSec`
The random pause, in seconds, before a free bot releases its spirit.
**Default:** `3` / `10` · **Values:** seconds · **Restart:** needed

## Questing

### `AiPlayerbot.AutoDoQuests`
Free bots quest and travel on their own.
**Default:** `1` · **Values:** `0` off, `1` on · **Restart:** needed

Off leaves the grind and travel kit in place but no questing, and stops the guild-charter errand that rides the questing strategy.

### `AiPlayerbot.BotQuestLogUpkeep`
Enables quest-log triage: drop what a solo bot can never finish, keep what it can.
**Default:** `1` · **Values:** `0` off, `1` on · **Restart:** needed

Failed quests, quests more than three levels over, elite, dungeon, raid and group quests, and quests belonging to another zone are dropped when slots run low. Finished quests are never dropped, grey quests that cannot progress are, and war-effort material turn-ins are refused at accept.

### `AiPlayerbot.AutoPickReward`
How a bot picks its reward when a quest offers a choice.
**Default:** `yes` · **Values:** `yes` (pick the best item itself), `no` (list the choices and wait), `ask` (take it when only one is useful, otherwise list) · **Restart:** needed

Only your own alts ever ask; a pool bot always picks for itself.

## Spawn

### `AiPlayerbot.RandomBotStartLevelMin` / `.Max`
The start level of a fresh pool bot, rolled uniformly on its first login.
**Default:** `1` / `60` · **Values:** levels 1 to 60 (a maximum below the minimum reads as the minimum) · **Restart:** needed

A bot is raised to the rolled level, never lowered, before its level-bound spells, talents and professions are synced. From then on it levels by playing. A bot that spawns below level 5 keeps the core's starter outfit for good; from level 5 it receives the one-time spawn kit of [`TortoiseBots.Pool.GearOnCreate`](../configuration.md#forevercore-module-keys-tortoisebotsconf) (default on).

## Gear

### `AiPlayerbot.AutoEquipUpgradeLoot`
Bots equip an upgrade they loot or win.
**Default:** `1` · **Values:** `0` off, `1` on · **Restart:** needed

### `AiPlayerbot.AutoEquipUpgradeRespectsRequiredLevel`
A pool bot does not equip a looted, quested, bought or traded upgrade above its level.
**Default:** `1` · **Values:** `0` off, `1` on · **Restart:** needed

Forever item rows often leave the required level empty (or set to the level-1 placeholder) even on high item levels, so the core alone would let a level-10 pool bot wear an item level 62 blue. With this on, the missing requirement is derived from the item level (item level minus 5, at most 60) and treated as real wherever the bot judges an item's use: the equip pass, but also a quest reward's choice, a loot roll and a vendor buy. A bot with a player master, hired and owned bots, and an explicit `.bot equip` order are not affected; `0` restores the old "if the core allows it, wear it" behaviour.

### `AiPlayerbot.EquipUpgradeThreshold`
The margin a new item must beat the worn one by.
**Default:** `1.1` · **Values:** a multiplier of the stat weight · **Restart:** needed

### `AiPlayerbot.PoolGearTopUpOnLevel`
Tops up a pool bot's gear once per level.
**Default:** `0` · **Values:** `0` off, `1` on · **Restart:** needed

It fills empty slots and replaces clearly outdated grey and white items; green or better gear is never replaced.

### `AiPlayerbot.ClaimedBotSelfEquip`
Lets a claimed guild bot at level 60 equip its own upgrades.
**Default:** `1` · **Values:** `0` off, `1` on · **Restart:** needed

### `AiPlayerbot.OwnedBotAutogearEnabled`
Allows `.bot autogear <bot>` for bots you own.
**Default:** `0` · **Values:** `0` off, `1` on · **Restart:** needed

A pool bot is always refused and keeps its own gear progression.

### `AiPlayerbot.OwnedBotAutogearQualityCap`
Highest item quality `.bot autogear` may put on an owned bot.
**Default:** `2` · **Values:** quality 0 to 5 (a larger value is read as 5) · **Restart:** needed

### `AiPlayerbot.OwnedBotAutogearIlvlCap`
Item-level ceiling for `.bot autogear` on an owned bot.
**Default:** `0` · **Values:** item level, `0` = no cap beyond the quality cap · **Restart:** needed

### `AiPlayerbot.RandomGearMaxLevel`
Item-level ceiling for generated gear.
**Default:** `100` · **Values:** item level · **Restart:** needed

### `AiPlayerbot.RandomGearMaxItemLevelAboveLevel`
How far above the bot's own level a generated item may sit.
**Default:** `1` · **Values:** levels · **Restart:** needed

### `AiPlayerbot.RandomGearBlacklist`
Item ids that never enter a generated outfit.
**Default:** `10,163,19972,58260,21524,21525,7` · **Values:** comma-separated item ids · **Restart:** needed

### `AiPlayerbot.RandomGearWhitelist`
Item ids that skip the stat-weight scoring.
**Default:** empty · **Values:** comma-separated item ids · **Restart:** needed

The other equip rules still apply to them.

### `AiPlayerbot.RandomGearAllowReputation`
Allows reputation-gated items in generated or looted gear.
**Default:** `0` · **Values:** `0` off, `1` on · **Restart:** needed

### `AiPlayerbot.RandomGearAllowPvP`
Allows PvP rank items in generated or looted gear.
**Default:** `0` · **Values:** `0` off, `1` on · **Restart:** needed

### `AiPlayerbot.RandomGearMaxSourceTier`
Highest source tier generated gear may come from.
**Default:** `0` · **Values:** `0` base (world drops, vendors, quests, crafts), `1` end-game dungeon, `2` raid · **Restart:** needed

It bounds the spawn kit, the level-up top-up and `.bot autogear`; loot, quest rewards the bot chooses and trades are not filtered by it. An item with no known ordinary source is never generated, and PvP or reputation gear needs `RandomGearAllowPvP` / `RandomGearAllowReputation`.

## Learning, pets & the party

### `AiPlayerbot.PetAutoTrain`
Hunter and warlock pets train themselves on every level-up.
**Default:** `1` · **Values:** `0` off, `1` on · **Restart:** needed

Applies to every bot the module trains, pool and owned alike; there are no hired companions on this build, and `AiPlayerbot.PetAutoTrainAlts` does nothing.

### `AiPlayerbot.AutoTrainSpells`
What a bot does at a class trainer.
**Default:** `yes` · **Values:** `yes` (pay the trainer), `free` (learn without paying), `no` (visit trainers without buying) · **Restart:** needed

A free bot always trains at a trainer it visits; `free` waives the cost, `no` leaves the spells unbought.

### `AiPlayerbot.ForceRebuffOnReadyCheck`
Holds the ready-check confirmation until the party's buffs are refreshed.
**Default:** `0` · **Values:** `0` off, `1` on · **Restart:** needed

## Immersion settings in `tortoisebots.conf`

These switch the host-side life of free bots. `.reload config` re-reads them.

| Setting | Default | What it does |
| :--- | :---: | :--- |
| `TortoiseBots.AutoLearnOnLevel` | `1` | Bots learn their class and class-line spells free on login and on level-up. |
| `TortoiseBots.AutoTalentsOnLevel` | `1` | Bots spend talent points on login and level-up following a premade build, never resetting points. |
| `TortoiseBots.ClassQuestSpells` | `0` | Bots learn the class-quest spells the running world offers (stances, forms, totems, pets, racials); a quest with no class mask counts when the spell it teaches is on the reviewed list. The class-line autotrain already covers nearly all of them. |
| `TortoiseBots.AutoProfessions` | `1` | Level-5 pool bots learn a class-sensible pair of primary professions plus First Aid and Cooking. |
| `TortoiseBots.AutoTools` | `1` | Missing profession tools are granted in a free bag slot. |
| `TortoiseBots.AutoFood` | `1` | Level-fitting vendor food and water are granted on login and level-up. |
| `TortoiseBots.AutoHunterPet` | `1` | Level-10 hunters receive their race's starter pet and its spells. |
| `TortoiseBots.World.Professions` | `0` | Free bots gather herb and ore within 150 yards in their zone. |
| `TortoiseBots.World.Fishing` | `1` | Free bots fish near where they stand, one session an hour, at most five casts in five minutes. |
| `TortoiseBots.World.GroupQuests` | `1` | Free bots with the same group quest form a party nearby and disband when done. |
| `TortoiseBots.World.CapitalErrands` | `1` | Bank, mailbox, trainer and auction errands, bot duels, and learning riding and buying a mount at 40/60. |
| `TortoiseBots.World.Guilds` | `1` | Guild invites in both directions, the 1000-member line and the owner's guild whispers. |
| `TortoiseBots.World.GuildAcceptPlayers` | `0` | A guildless player's whisper asks a bot-run guild for an invite; off by default, needs `Guilds = 1`. |
| `TortoiseBots.World.GuildForming` | `0` | Free bots above level 10 found `<Bot> Guild` from their own purse. |
| `TortoiseBots.World.GuildMail` | `1` | Bot guilds mail surplus gold and unused materials among their members. |
| `TortoiseBots.World.GuildMail.AllowMixedGuild` | `0` | Allows that mail in a guild that also holds real players. |
| `TortoiseBots.World.Pickpocket` | `1` | Rogues pick the pockets of nearby humanoids. |
| `TortoiseBots.World.Lockboxes` | `1` | Rogues open lockboxes they carry. |
| `TortoiseBots.World.LockboxMail` | `1` | A lockbox a bot cannot open is mailed to a rogue in its own guild. |
| `TortoiseBots.World.Disenchant` | `1` | Enchanters break down disenchantable gear they carry. |
| `TortoiseBots.World.EnchantGear` | `1` | Enchanters apply their best permanent enchant to their own gear. |
| `TortoiseBots.World.Auction` | `0` | Free bots use the auction house (orders and, with `AiPlayerbot.AhMarketEnabled`, their own postings). |
| `TortoiseBots.Slice.EngineByDefault` | `0` | A bot that logs in runs the real bot engine instead of the follow/assist spike. |
| `TortoiseBots.Slice.RestHealthPct` / `RestManaPct` | `90` / `80` | The bars at which `.tb state` reports a bot as rested. |

## Keys the Forever build does not act on

The generated [key census](../config-keys.md) lists every advertised key and whether a compiled part of the build reads it. The keys below are shipped in the Forever template or were carried over from the Turtle version, but nothing acts on them here, so setting one changes nothing. The table names what to use instead.

| Key | Why it does nothing / what to use instead |
| :--- | :--- |
| `AiPlayerbot.MinRandomBots` / `MaxRandomBots` | Population is the pool: `TortoiseBots.Pool.TargetOnline` and `TortoiseBots.Pool.MaxCharacters` above. |
| `AiPlayerbot.RandomBotAutologin` | The pool scheduler logs bots in; start it with `.tb pool start`. |
| `AiPlayerbot.RandomBotLoginWithPlayer` | The pool schedule owns logins. |
| `AiPlayerbot.RandomBotLoginAtStartup` | The pool schedule owns logins. |
| `AiPlayerbot.RandomBotAutoCreate` | Pool characters are made by `.tb pool create` and the pool's refill. |
| `AiPlayerbot.RandomBotPoolReset` | The reset is `TortoiseBots.Pool.Reset`, read only at world startup. |
| `AiPlayerbot.RandomBotAccountPrefix` | The pool creates its own accounts (the `TBPOOL<nnnn>` pattern). |
| `AiPlayerbot.PinnedBots` | No rotation table reads it. |
| `AiPlayerbot.RandomBotLineage` | There are no lineage alts on this build. |
| `AiPlayerbot.LineageLowBandReserve` | See `RandomBotLineage`. |
| `AiPlayerbot.LineagePerHour` | See `RandomBotLineage`. |
| `AiPlayerbot.LineagePoolCeiling` | See `RandomBotLineage`. |
| `AiPlayerbot.LineageJoinParentGuild` | See `RandomBotLineage`. |
| `AiPlayerbot.RandomBotTimedLogout` | Nothing acts on it; the pool's session lengths decide logouts. |
| `AiPlayerbot.MaxRandomBotInWorldTime` | See `RandomBotTimedLogout`. |
| `AiPlayerbot.MinRandomBotRandomizeTime` / `MaxRandomBotRandomizeTime` | There is no unseeded-gear backstop sweep. |
| `AiPlayerbot.RandomBotStallRelogMinutes` | There is no experience-stall relog. |
| `AiPlayerbot.RandomBotMaxLevel` | The gear tables are capped by `RandomGearMaxLevel`. |
| `AiPlayerbot.minEnchantingBotLevel` | Gear enchanting comes from the profession upkeep (`TortoiseBots.World.EnchantGear`), not from a seed roll. |
| `AiPlayerbot.EnableRandomTeleports` | New pool characters are not scattered; they start at their race's start position. |
| `AiPlayerbot.RandomBotEvenStartZones` | The pool's character composition decides race and start zone. |
| `AiPlayerbot.RandomBotStartZoneWeights` | See `RandomBotEvenStartZones`. |
| `AiPlayerbot.RandomBotFactionRatio` | Pool composition balances the factions. |
| `AiPlayerbot.RelocateHopelessDeaths` | Does nothing. |
| `AiPlayerbot.AbandonedInstanceGraceSec` | Free-bot instance entry is capped by `TortoiseBots.Free.MaxPerMap.*` instead. |
| `AiPlayerbot.AmmoStacksNonHunter` | There is no ammo kit on this build. |
| `AiPlayerbot.RandomBotInvitePlayer` | The engine's own invite row is not part of the build; bots form quest parties through `TortoiseBots.World.GroupQuests`. |
| `AiPlayerbot.RandomBotGroupNearby` | Same reason as `RandomBotInvitePlayer`. |
| `AiPlayerbot.RandomBotGuildNearby` | Guild membership runs through `TortoiseBots.World.Guilds`. |
| `AiPlayerbot.RandomBotFormGuild` | Founding guilds is `TortoiseBots.World.GuildForming`. |
| `AiPlayerbot.GuildMaxBotLimit` | The 1000-member line is built in. |
| `AiPlayerbot.GuildOrderBotRank` | The guild-order bot rank is `TortoiseBots.World.GuildOrderBotRank`. |
| `AiPlayerbot.ProfessionsGrowWithLevel` | `TortoiseBots.AutoProfessions` grants the professions. |
| `AiPlayerbot.BotProfessionSpecializations` | Does nothing. |
| `AiPlayerbot.RandomBotShowHelmet` / `RandomBotShowCloak` | Helmet and cloak visibility is not changed. |
| `AiPlayerbot.PoolBotTradeMode` | A stranger can never trade with a pool bot; there is no mode key. |
| `AiPlayerbot.TradeActionExcludedPrefixes` | Chat addon prefixes cannot open a trade window because no trade window is driven from chat. |
| `AiPlayerbot.TurtleMountAtLevel` | Nothing acts on it; the mount seeding it configured is not part of this build. Bots buy mounts at 40 and 60. |
| `AiPlayerbot.LevelUpMounts` | Mounts come from the capital errand, not from a level-up grant. |
| `AiPlayerbot.BotInstantFlight` | The instant-arrival shortcut it switched on is not implemented; a bot flies the real route. |
| `AiPlayerbot.UseWanderAsDefaultFollowStrategy` | The wander strategy is not part of the build. |
| `AiPlayerbot.WanderMinDistance` | Only the `wander` strategy reads it, and that strategy is not part of the build. |
| `AiPlayerbot.WanderMaxDistance` | See `WanderMinDistance`. |
| `AiPlayerbot.AutoLearnQuestSpells` | Class-quest spells are `TortoiseBots.ClassQuestSpells`. |
| `AiPlayerbot.AutoLearnQuestSpellsAlts` | See `AutoLearnQuestSpells`. |
| `AiPlayerbot.AutoLearnTrainerSpells` | Class spells are `TortoiseBots.AutoLearnOnLevel`. |
| `AiPlayerbot.AutoLearnDroppedSpells` | Does nothing. |
| `AiPlayerbot.PetAutoTrainAlts` | See `PetAutoTrain`: pet training covers every bot the module trains. |
| `AiPlayerbot.RandomGearUpgradeEnabled` | The spawn kit is `TortoiseBots.Pool.GearOnCreate`; level-up gear is `PoolGearTopUpOnLevel`. |
| `AiPlayerbot.RandomGearProgression` | Nothing acts on it. |
| `AiPlayerbot.RandomGearSeedEpicChance` | The spawn kit never includes epics. |
| `AiPlayerbot.AutoPickTalents` | Talents are `TortoiseBots.AutoTalentsOnLevel`. |
| `AiPlayerbot.XPRate` | Nothing acts on it: the experience bonus action is not part of the build. Bots level at the server's own rate. |
| `AiPlayerbot.RndBotCheats` | The cheat mask is compiled as a stub that always answers "no cheats", so no bot receives a repair, breath or item cheat. |
| `AiPlayerbot.CampEnabled` | Nothing acts on it: the camp action is not part of the build. The "stay at an inn hub" leisure trip still exists. |
| `AiPlayerbot.CampChancePct` | See `CampEnabled`. |
| `AiPlayerbot.CampNightChancePct` | See `CampEnabled`. |
| `AiPlayerbot.CampMinLevel` | See `CampEnabled`. |
| `AiPlayerbot.CampSitDurationSec` | See `CampEnabled`. |
| `AiPlayerbot.CampKeepRestedXp` | See `CampEnabled`. |
| `AiPlayerbot.CampJoinChancePct` | See `CampEnabled`. |
| `AiPlayerbot.CampJoinMinSec` | See `CampEnabled`. |
| `AiPlayerbot.CampJoinMaxSec` | See `CampEnabled`. |
| `AiPlayerbot.CampGreetChancePct` | See `CampEnabled`. |
| `AiPlayerbot.CampGreetCooldownSec` | See `CampEnabled`. |
| `AiPlayerbot.RandomBotNonCombatStrategies` | A free bot's kit is built by the engine, not from a config string. |
| `AiPlayerbot.RandomBotCombatStrategies` | See `RandomBotNonCombatStrategies`. |
| `AiPlayerbot.CombatStrategies` | The dashboard reports the value; no engine reads it. |
| `AiPlayerbot.AllowIsolatedCustomStartingZones` | Keeps bots out of three isolated custom starting zones from the Turtle data. No Forever zone carries those ids, so it changes nothing here. |
