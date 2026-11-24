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

Free bots only act on their own when the engine is on: `TortoiseBots.Enable = 1`, the pool enabled and started, and `TortoiseBots.Slice.EngineByDefault = 1` (or `.tb engine <bot> on`). See [Living world & autonomous bots](../../world/living-world.md#before-anything-moves).

## Population: the pool

| Setting | Default | What it does |
| :--- | :---: | :--- |
| `TortoiseBots.Pool.Enable` | `0` | The bot pool: module-created level-1 characters on module-created accounts, logged in on a schedule. Off by default. |
| `TortoiseBots.Pool.MaxCharacters` | `50` | Ceiling on the pool's total size. `0` creates nothing. |
| `TortoiseBots.Pool.CreatePerMinute` | `30` | How many characters `pool create` may make per minute. `0` creates nothing. |
| `TortoiseBots.Pool.TargetOnline` | `0` | How many pool bots `.tb pool start` keeps in the world. `0` runs the schedule but logs nobody in. |
| `TortoiseBots.Pool.LoginsPerMinute` | `20` | Login cap per minute (and at most one per second). |
| `TortoiseBots.Pool.SessionMinutes.Min` / `.Max` | `30` / `90` | A session's length, rolled per login. |
| `TortoiseBots.Pool.OfflineMinutes.Min` / `.Max` | `5` / `20` | How long a bot stays away between sessions. |
| `TortoiseBots.Pool.PopulationLow` / `.PopulationHigh` | `10` / `50` | The presets behind `.tb pool population low` and `high`. |
| `TortoiseBots.Pool.Reset` | `"off"` | Rebuilds the pool on world startup. Read only at startup. |

The scheduler logs in one bot per second at most, alternates factions and takes the bot that has been offline longest first. One session per account is the rule, so the online count is capped by the number of accounts the pool sits on. A bot that gets a master leaves the count and is never logged out by the pool. A lowered target logs out the newest sessions first, never a dead or fighting bot. Characters, commands and the reset's safety checks are in [The bot pool](../bot-pool.md). `.reload config` re-reads every pool key except `TortoiseBots.Pool.Reset`, which is read only at world startup.

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

## Gear

### `AiPlayerbot.AutoEquipUpgradeLoot`
Bots equip an upgrade they loot or win.
**Default:** `1` · **Values:** `0` off, `1` on · **Restart:** needed

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

## Learning, pets & the party

### `AiPlayerbot.PetAutoTrain`
Hunter and warlock pets train themselves on every level-up.
**Default:** `1` · **Values:** `0` off, `1` on · **Restart:** needed

Pool bots and hired companions only; your own alts keep what you taught them.

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
| `TortoiseBots.ClassQuestSpells` | `0` | Bots learn the class-quest spells the running world offers (stances, forms, totems, pets, racials). |
| `TortoiseBots.AutoProfessions` | `1` | Level-5 pool bots learn a class-sensible pair of primary professions plus First Aid and Cooking. |
| `TortoiseBots.AutoTools` | `1` | Missing profession tools are granted in a free bag slot. |
| `TortoiseBots.AutoFood` | `1` | Level-fitting vendor food and water are granted on login and level-up. |
| `TortoiseBots.AutoHunterPet` | `1` | Level-10 hunters receive their race's starter pet and its spells. |
| `TortoiseBots.World.Professions` | `0` | Free bots gather herb and ore within 150 yards in their zone. |
| `TortoiseBots.World.Fishing` | `1` | Free bots fish near where they stand, one session an hour, at most five casts in five minutes. |
| `TortoiseBots.World.GroupQuests` | `1` | Free bots with the same group quest form a party nearby and disband when done. |
| `TortoiseBots.World.CapitalErrands` | `1` | Bank, mailbox, trainer and auction errands, bot duels, and learning riding and buying a mount at 40/60. |
| `TortoiseBots.World.Guilds` | `1` | Guild invites, the 1000-member line and the owner's guild whispers. |
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

The generated [key census](../config-keys.md) lists every advertised key and whether a compiled part of the build reads it. The keys below are shipped in the Forever template or were carried over from the Turtle version, but nothing acts on them here, so setting one changes nothing. They are grouped so a reader coming from the Turtle docs finds what replaced them.

### `AiPlayerbot.MinRandomBots` / `MaxRandomBots`
Do nothing. Population is the pool: `TortoiseBots.Pool.TargetOnline` and `TortoiseBots.Pool.MaxCharacters` above.

### `AiPlayerbot.RandomBotAutologin`
Does nothing. The pool scheduler logs bots in; start it with `.tb pool start`.

### `AiPlayerbot.RandomBotLoginWithPlayer`
Does nothing. The pool schedule owns logins.

### `AiPlayerbot.RandomBotLoginAtStartup`
Does nothing. The pool schedule owns logins.

### `AiPlayerbot.RandomBotAutoCreate`
Does nothing. Pool characters are made by `.tb pool create` and the pool's refill.

### `AiPlayerbot.RandomBotPoolReset`
Does nothing. The reset is `TortoiseBots.Pool.Reset`, read only at world startup.

### `AiPlayerbot.RandomBotAccountPrefix`
Does nothing. The pool creates its own accounts (the `TBPOOL<nnnn>` pattern).

### `AiPlayerbot.PinnedBots`
Does nothing. No rotation table reads it.

### `AiPlayerbot.RandomBotStartLevelMin` / `RandomBotStartLevelMax`
Do nothing. Pool characters are created at level 1 and level up by playing.

### `AiPlayerbot.LevelLadder`
Keeps its line in the template but nothing acts on it: the dashboard only reports its value. There is no level ladder on this build.

### `AiPlayerbot.LevelLadderBandSize`
Does nothing. See `LevelLadder`.

### `AiPlayerbot.LevelLadderMaxLevelShare`
Does nothing. See `LevelLadder`.

### `AiPlayerbot.LevelLadderLogMinutes`
Does nothing. See `LevelLadder`.

### `AiPlayerbot.RandomBotLineage`
Does nothing. There are no lineage alts on this build.

### `AiPlayerbot.LineageLowBandReserve`
Does nothing. See `RandomBotLineage`.

### `AiPlayerbot.LineagePerHour`
Does nothing. See `RandomBotLineage`.

### `AiPlayerbot.LineagePoolCeiling`
Does nothing. See `RandomBotLineage`.

### `AiPlayerbot.LineageJoinParentGuild`
Does nothing. See `RandomBotLineage`.

### `AiPlayerbot.RandomBotTimedLogout`
Keeps its line in the template but nothing acts on it: the pool's session lengths decide logouts.

### `AiPlayerbot.MaxRandomBotInWorldTime`
Does nothing. See `RandomBotTimedLogout`.

### `AiPlayerbot.MinRandomBotRandomizeTime` / `MaxRandomBotRandomizeTime`
Do nothing. There is no unseeded-gear backstop sweep.

### `AiPlayerbot.RandomBotStallRelogMinutes`
Does nothing. There is no experience-stall relog.

### `AiPlayerbot.RandomBotMaxLevel`
Does nothing. The gear tables are capped by `RandomGearMaxLevel`.

### `AiPlayerbot.minEnchantingBotLevel`
Does nothing. Gear enchanting comes from the profession upkeep (`TortoiseBots.World.EnchantGear`), not from a seed roll.

### `AiPlayerbot.EnableRandomTeleports`
Does nothing. New pool characters are not scattered; they start at their race's start position.

### `AiPlayerbot.RandomBotEvenStartZones`
Does nothing. The pool's character composition decides race and start zone.

### `AiPlayerbot.RandomBotStartZoneWeights`
Does nothing. See `RandomBotEvenStartZones`.

### `AiPlayerbot.RandomBotFactionRatio`
Does nothing. Pool composition balances the factions.

### `AiPlayerbot.RelocateHopelessDeaths`
Does nothing.

### `AiPlayerbot.AbandonedInstanceGraceSec`
Does nothing. Free-bot instance entry is capped by `TortoiseBots.Free.MaxPerMap.*` instead.

### `AiPlayerbot.AmmoStacksNonHunter`
Does nothing. There is no ammo kit on this build.

### `AiPlayerbot.RandomBotInvitePlayer`
Does nothing. The engine's own invite row is not part of the build; bots form quest parties through `TortoiseBots.World.GroupQuests`.

### `AiPlayerbot.RandomBotGroupNearby`
Does nothing. Same reason as `RandomBotInvitePlayer`.

### `AiPlayerbot.RandomBotGuildNearby`
Does nothing. Guild membership runs through `TortoiseBots.World.Guilds`.

### `AiPlayerbot.RandomBotFormGuild`
Does nothing. Founding guilds is `TortoiseBots.World.GuildForming`.

### `AiPlayerbot.GuildMaxBotLimit`
Does nothing. The 1000-member line is built in.

### `AiPlayerbot.GuildOrderBotRank`
Does nothing. The guild-order bot rank is `TortoiseBots.World.GuildOrderBotRank`.

### `AiPlayerbot.ProfessionsGrowWithLevel`
Does nothing. `TortoiseBots.AutoProfessions` grants the professions.

### `AiPlayerbot.BotProfessionSpecializations`
Does nothing.

### `AiPlayerbot.RandomBotShowHelmet` / `RandomBotShowCloak`
Do nothing. Helmet and cloak visibility is not changed.

### `AiPlayerbot.PoolBotTradeMode`
Does nothing. A stranger can never trade with a pool bot; there is no mode key.

### `AiPlayerbot.TradeActionExcludedPrefixes`
Does nothing. Chat addon prefixes cannot open a trade window because no trade window is driven from chat.

### `AiPlayerbot.TurtleMountAtLevel`
Keeps its line in the template but nothing acts on it: the mount seeding it configured is not part of this build. Bots buy mounts at 40 and 60.

### `AiPlayerbot.LevelUpMounts`
Keeps its line in the template but nothing acts on it. Mounts come from the capital errand, not from a level-up grant.

### `AiPlayerbot.AllowRepairedTaxiPaths`
Does nothing in practice: bots make no flight plans on this build.

### `AiPlayerbot.BotInstantFlight`
Does nothing in practice: bots make no flight plans on this build.

### `AiPlayerbot.UseWanderAsDefaultFollowStrategy`
Does nothing. The wander strategy is not part of the build.

### `AiPlayerbot.WanderMinDistance`
Keeps its line in the template but only the `wander` strategy reads it, and that strategy is not part of the build.

### `AiPlayerbot.WanderMaxDistance`
Does nothing here. See `WanderMinDistance`.

### `AiPlayerbot.AutoLearnQuestSpells`
Keeps its line in the template but nothing acts on it. Class-quest spells are `TortoiseBots.ClassQuestSpells`.

### `AiPlayerbot.AutoLearnQuestSpellsAlts`
Does nothing. See `AutoLearnQuestSpells`.

### `AiPlayerbot.AutoLearnTrainerSpells`
Keeps its line in the template but nothing acts on it. Class spells are `TortoiseBots.AutoLearnOnLevel`.

### `AiPlayerbot.AutoLearnDroppedSpells`
Does nothing.

### `AiPlayerbot.PetAutoTrainAlts`
Does nothing. Pet training covers pool bots and hires only.

### `AiPlayerbot.RandomGearUpgradeEnabled`
Does nothing. There is no seed-time gear roll; use `PoolGearTopUpOnLevel` or `.bot autogear`.

### `AiPlayerbot.RandomGearProgression`
Keeps its line in the template but nothing acts on it.

### `AiPlayerbot.RandomGearMaxSourceTier`
Does nothing. There is no seed-time gear roll to restrict.

### `AiPlayerbot.RandomGearSeedEpicChance`
Does nothing. There is no seed-time gear roll.

### `AiPlayerbot.AutoPickTalents`
Does nothing. Talents are `TortoiseBots.AutoTalentsOnLevel`.

### `AiPlayerbot.XPRate`
Keeps its line in the template but nothing acts on it: the experience bonus action is not part of the build. Bots level at the server's own rate.

### `AiPlayerbot.RndBotCheats`
Does nothing. The cheat mask is compiled as a stub that always answers "no cheats", so no bot receives a repair, breath or item cheat.

### `AiPlayerbot.CampEnabled`
Keeps its line in the template but nothing acts on it: the camp action is not part of the build. The "stay at an inn hub" leisure trip still exists.

### `AiPlayerbot.CampChancePct`
Does nothing. See `CampEnabled`.

### `AiPlayerbot.CampNightChancePct`
Does nothing. See `CampEnabled`.

### `AiPlayerbot.CampMinLevel`
Does nothing. See `CampEnabled`.

### `AiPlayerbot.CampSitDurationSec`
Does nothing. See `CampEnabled`.

### `AiPlayerbot.CampKeepRestedXp`
Does nothing. See `CampEnabled`.

### `AiPlayerbot.CampJoinChancePct`
Does nothing. See `CampEnabled`.

### `AiPlayerbot.CampJoinMinSec`
Does nothing. See `CampEnabled`.

### `AiPlayerbot.CampJoinMaxSec`
Does nothing. See `CampEnabled`.

### `AiPlayerbot.CampGreetChancePct`
Does nothing. See `CampEnabled`.

### `AiPlayerbot.CampGreetCooldownSec`
Does nothing. See `CampEnabled`.

### `AiPlayerbot.RandomBotNonCombatStrategies`
Does nothing. A free bot's kit is built by the engine, not from a config string.

### `AiPlayerbot.RandomBotCombatStrategies`
Does nothing. See `RandomBotNonCombatStrategies`.

### `AiPlayerbot.CombatStrategies`
Does nothing. The dashboard reports the value; no engine reads it.

### `AiPlayerbot.AllowIsolatedCustomStartingZones`
Keeps bots out of three isolated custom starting zones from the Turtle data. No Forever zone carries those ids, so it changes nothing here.
