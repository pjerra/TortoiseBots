---
id: play-commands-whispers
title: Whisper Commands
category: play
summary: The words a bot obeys when you whisper it or speak in its party, the permission tiers behind them, and what Forever does not answer yet.
tags: [commands, whispers, delegation]
relates_to:
  - play-commands
  - play-commands-party-actions
  - play-commands-bot-panel
---

# Whisper Commands

You can command one bot directly by whispering it (`/w <BotName> <words>`). In the bot's party, the same words in party chat (or as party leader) reach it too.

## Who a bot listens to

A bot reads only a whisper, a party line or a party-leader line:

- The sender must be a **real player** in the world. A bot, a creature or the console cannot order a bot.
- The sender must be the bot's **master**, the **commander** it is bound to, or a **member of the bot's group**. A stranger's whisper is dropped.
- The bot must be a **host bot running the AI engine**. Bots without an engine do nothing with chat.
- Lines longer than **255 bytes** are dropped, and addon-channel messages never command a bot.
- The bot's `chat` strategy must be on. Every class kit gets it, so an ordinary bot always has it.

## Permission tiers

Commands fall into three tiers, checked before the bot acts:

| Tier | Who may use it | Examples |
| :--- | :--- | :--- |
| **Group member** | The bot's master, its commander, or a member of its group | `follow`, `stay`, `attack`, `flee`, `pull`, `rti`, `position`, `who`, `stats`, `spells`, `c`, `help` |
| **Owner or GM** | The bot's owner (the same Battle.net account as the bot's character), or a GameMaster | Economy and inventory words (`s`, `b`, `bb`, `u`, `e`, `ue`, `mail`, `ah`, `repair`), quest and training words (`quests`, `accept`, `quest reward`, `share`, `drop`, `trainer`), strategy edits (`co`, `nc`, `de`, `react`, `all`), `pet`, `speak`, `roll` |
| **GM only** | A GameMaster | `debug`, `cdebug`, `cs`, `log`, `set value`, `reset ai`, `reset strats`, `reset values` |

Ownership is the Battle.net account, read fresh on every line: two game accounts of one Battle.net account own the same bots. An unknown word fails closed to Owner or GM.

A line from someone the bot may not receive is dropped without a reply. An admitted line that fails its tier answers in chat (`Only this bot's owner (or a GameMaster) may do that.`); a GM-only word from a non-GM answers `That command is restricted to GameMasters.` Other chat kinds stay silent.

## Chaining with `\\`

A whisper or party line can carry several commands separated by the configured separator. The default is two backslashes:

```text
/w Tanky follow\\co +focus
```

The bot splits the line and runs the words in order. The separator is `AiPlayerbot.CommandSeparator`; with it empty, nothing is split.

## Words that work

The rest of this page is the Forever word list, grouped the way the bot's own command help groups them.

<a id="inventory--items"></a>
### Inventory and equipment

| Word | Parameters | Tier | What it does |
| :--- | :--- | :--- | :--- |
| `c` / `items` / `inv` / `inventory` | none | Group member | Lists equipped and carried items. |
| `e` / `equip` | `<item link or name>` | Owner or GM | Equips the item from the bags. |
| `ue` | `<slot>` | Owner or GM | Unequips the item in that slot. |
| `u` / `use` | `<item link or name>` | Owner or GM | Uses an item from the bags. |
| `loot` / `add all loot` | none | Owner or GM | Loots nearby corpses, nodes and chests. |

### Vendors, repairs and money

| Word | Parameters | Tier | What it does |
| :--- | :--- | :--- | :--- |
| `s` | `[gray\|all\|<item>]` | Owner or GM | Sells junk (`s gray`), everything sellable (`s all`) or the named item at an open vendor. |
| `b` | `<item link or name>` | Owner or GM | Buys the item from the open vendor. |
| `bb` | `<item link or name>` | Owner or GM | Buys back an item sold by mistake. |
| `repair` | none | Owner or GM | Repairs at a repair merchant. |
| `mail` | `[?\|take\|delete\|read <filter>]` | Owner or GM | Reads, takes or deletes mail at a nearby mailbox. |
| `ah` | `[vendor\|itemlink money]` | Owner or GM | Auctions items at a nearby auction house. |
| `ah bid` | `[itemlink money]` | Owner or GM | Bids on an auction within the budget. |
| `ah cancel` | `[itemlink\|all]` | Owner or GM | Cancels the bot's own auctions. |

### Quests

| Word | Parameters | Tier | What it does |
| :--- | :--- | :--- | :--- |
| `quests` | none | Owner or GM | Lists active quests and objective progress. |
| `accept` | none | Owner or GM | Accepts the quest offered by the target NPC. |
| `share` | none | Owner or GM | Shares eligible quests with the party. |
| `drop` | `<quest link or name>` | Owner or GM | Abandons the quest. |
| `quest reward` | `<reward index>` | Owner or GM | Picks the reward and turns the quest in. |

### Training, spells and strategies

| Word | Parameters | Tier | What it does |
| :--- | :--- | :--- | :--- |
| `trainer` | none | Owner or GM | Learns every class spell and rank it can afford from the selected trainer. |
| `spells` | none | Group member | Lists known spells. |
| `spell` | `<spell link or name>` | Owner or GM | Describes one spell. |
| `co` / `nc` / `de` / `react` / `all` | `[+/-/~strategy]` or `?` | Owner or GM | Edits combat, non-combat, dead, reaction or all strategies. |
| `reset ai` | none | GM only | Resets the bot's strategy setup. |
| `reset strats` | none | GM only | Resets strategies to the defaults. |
| `reset values` | none | GM only | Resets the bot's AI values. |
| `reset` | none | Owner or GM | Flushes queues and resets the AI. |
| `cs` | see the bot's `help` | GM only | Edits custom strategies. |

### Movement and combat

| Word | Parameters | Tier | What it does |
| :--- | :--- | :--- | :--- |
| `follow` | `[member]` | Group member | Follows the master or the named member. |
| `stay` | none | Group member | Holds position. |
| `free` | none | Group member | Releases stay and fights on its own. |
| `attack` | none | Group member | Attacks your target. |
| `tank attack` | none | Group member | Sends the tank at your target. |
| `flee` | none | Group member | Runs from the fight. |
| `max dps` | none | Group member | Spends everything on damage for a while. |
| `save mana` | `[on\|off]` | Group member | Trades damage for mana. |
| `pull` | none | Group member | Pulls your target. |
| `end pull` | none | Owner or GM | Aborts a pull; the tank returns to follow. |
| `attack rti` | `<mark>` | Group member | Attacks the enemy with that raid mark. |
| `rti` | `<mark>` | Group member | Sets the raid mark the bot works on. |
| `formation` | `[style]` | Group member | Sets or shows the follow formation. |
| `position` | none | Group member | Reports the bot's coordinates and facing. |
| `los` | none | Group member | Reports line of sight to the current target. |
| `range` | `[distance]` | Owner or GM | Sets or shows the engagement range. |
| `attackers` | none | Group member | Lists what is attacking the bot. |
| `possible attack targets` | none | Group member | Lists hostile targets the bot can reach. |
| `jump` | none | Group member | Jumps. |
| `wait <seconds>` | `<seconds>` | Group member | Pauses the AI for up to 20 seconds. |

### Pet

| Word | Parameters | Tier | What it does |
| :--- | :--- | :--- | :--- |
| `pet` | `[aggressive\|defensive\|passive]` | Owner or GM | Sets the pet stance. |
| `pet autocast` | `<spell name>` | Owner or GM | Toggles autocast on a pet spell. |

### Reporting and help

| Word | Parameters | Tier | What it does |
| :--- | :--- | :--- | :--- |
| `help` | none | Group member | Prints the bot's command help in chat. |
| `stats` | none | Group member | Prints money, free bag slots, repair cost, XP to next level and power. |
| `who` | `[skill or item]` | Group member | Prints a short character summary: spec, area and who it plays with; with a word, a skill or trade query. |
| `rep` / `reputation` | none | Owner or GM | Reports reputation standings. |
| `leave` | none | Owner or GM | Leaves the current group. |

### Recovery and chat

| Word | Parameters | Tier | What it does |
| :--- | :--- | :--- | :--- |
| `release` | none | Owner or GM | Releases spirit after dying. |
| `revive` | none | Owner or GM | Resurrects at the Spirit Healer. |
| `corpse run` | none | Owner or GM | Runs the ghost back to its corpse. |
| `logout` | none | Owner or GM | Logs the bot out cleanly. |
| `logout cancel` | none | Owner or GM | Cancels a logout in progress. |
| `speak` | `<text>[/channel text]` | Owner or GM | Makes the bot say the text in chat or a channel. |
| `d` / `do` | `<action name>` | Owner or GM | Runs a registered AI action by name. |
| `roll <need\|greed\|pass\|auto>` | `[item]` | Owner or GM | Votes on open loot rolls. |
| `follow target` | `<name\|none>` | Group member | Makes the bot follow that party member; `none` clears the custom target. |
| `debug` / `cdebug` | see the bot's `help` | GM only | Turns engine debug output on or off. |
| `log` | see the bot's `help` | GM only | Sets the bot's log level. |
| `set value` | `<name> <value>` | GM only | Writes one AI value. |

<a id="guild-management"></a>
### Guild management

The module's guild driver answers these whispers itself, before the AI command list. They work only while `TortoiseBots.World.Guilds` is on (default `1`), and only the bot's master or a GameMaster may use them:

| Word | What it does |
| :--- | :--- |
| `guild invite <player>` | Invites the named character to the bot's guild. |
| `guild join` | Accepts an outstanding guild invite. |
| `guild leave` | Leaves the bot's guild. |
| `guild promote <player>` / `guild demote <player>` | Moves a member one rank up or down. |
| `guild remove <player>` | Kicks the member. |
| `guild leader <player>` | Hands guild leadership to the member. |

A bot refuses any of these with `Only my owner can manage my guild.` when the sender is neither its master nor a GM.

## Not on ForeverCore yet

These words belong to the Turtle build and are not answered here: the `.bot command <Bot> ...` proxy, `hire`, `portal`, `cast`, `castnc`, `talents`, `save ai`, `load ai`, `list ai`, `ll`, `ss`, `bank`, `gbank`, `craft`, `outfit`, `keep`, `t`, `nt`, `wts`, `talk`, `doquest`, `skill`, `faction`, `flag`, `go`, `move style`, `grind`, `home`, `taxi`, `invite`, `join`, `lfg`, `summon`, `bg free`, `give leader`, `warning`, `stance`, `chat`, `emote`, `buff`, `ready`, `focus heal`, `buff target`, `revive target`, `boost target`, `rebuff`, `guard` and `runaway`.

## See also

- [Party actions](party-actions.md) for the party-wide orders the addon sends.
- [Bot Panel](bot-panel.md) for the inspection and gear words.
