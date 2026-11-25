---
id: server-admin-commands
title: Administrator & GM Commands
category: server
summary: The GameMaster and server-console commands of the ForeverCore build, covering pool, dungeon fill, battlegrounds, guild orders, runs and diagnostics.
tags: [admin, gm, commands, bot-pool, diagnostics]
relates_to:
  - server-bot-pool
  - server-configuration
  - server-dashboard
  - server-run-trace
---

# Administrator & GM Commands

Every command on this page is a **GameMaster or server-console** command. Players use `.bot` (see [What bots can do](../play/features.md)); admins use `.tb`.

All `.tb` commands are registered by the module and are available while it is enabled. Commands that need the bot engine refuse when the module was built with `TORTOISEBOTS_SLICE1=OFF`, naming the missing engine: the command-channel commands answer `error code=engine-not-built`, and a human-facing one says so in its own words (`.tb addonhint` answers `TortoiseBots: addonhint: the engine is not built in (this build has no add-on channel).`).

## Permissions

| Commands | Who may run them |
| :--- | :--- |
| Most `.tb` commands | GameMaster, or the server console. |
| `.tb create`, `.tb pool create` | The account-create permission (the same right `.account create` needs). |
| `.tb pool prune confirm` | The account-delete permission (the same right `.account delete` needs). `.tb pool prune` alone is a dry run and needs only GM. |
| `.tb pool` | GameMaster or server console. |

Answers are one line per result, prefixed `TortoiseBots:`. A refusal carries a short code, for example `TortoiseBots: run: refused no-bot: ...`.

## The bot pool

The pool owns its own accounts (`TBPOOL0001` ...) and characters. See [Managing the bot pool](bot-pool.md) for the registry and reset.

| Command | What it does |
| :--- | :--- |
| `.tb pool create <n>` | Create up to `n` new pool characters, one at a time at `TortoiseBots.Pool.CreatePerMinute` and never past `TortoiseBots.Pool.MaxCharacters`. Needs `TortoiseBots.Pool.Enable = 1`. |
| `.tb pool prune` | Dry run: show failed pool rows and the accounts/characters they would remove. |
| `.tb pool prune confirm` | Remove them after re-checking the database. Requires the account-delete permission. |
| `.tb pool start` | Start the login scheduler (also restarts one that is draining). |
| `.tb pool stop` | Drain every pool bot, at five logouts per second. A bot in a running battleground is held until its match ends or `TortoiseBots.Bg.MaxMinutes`. |
| `.tb pool status` | One status block: scheduler state and target, online bots and factions, pool size, returns due, battleground holds, logins in the last minute, the drain queue, the last pass cost, and the reset line `reset=... phase=... targets=... applied=...`. |
| `.tb pool population <n\|low\|high>` | Change the online target for this run. `low`/`high` use `TortoiseBots.Pool.PopulationLow` (10) and `PopulationHigh` (50). `.reload config` restores `TargetOnline`. |

`.tb pool status` adds `capped by accounts: N` when the target is above the accounts the pool can still use (one session per account; a bound bot keeps its account).

## Test characters

| Command | What it does |
| :--- | :--- |
| `.tb create <account> <name> <race id> <class id> [gender]` | Create a level 1 character on a pool account, or on an account named in `TortoiseBots.Create.TestAccounts`. A real player's account is refused. |
| `.tb login <character> [master\|free]` | Log a character in as a bot. With no master it follows the caller; `free` makes it a masterless bot that plays on its own. |
| `.tb logout <character>` | Log a bot out. |
| `.tb free <bot> on\|off` | Let a logged-in masterless bot play on its own, or make it stand still. |
| `.tb group <bot> <leader>` / `.tb ungroup <bot>` | Put a bot in a group, or take it out. |
| `.tb master <bot\|party [leader]> <commander\|none>` | Bind a bot or a party to a commander. A run needs the party bound first. |
| `.tb engine <bot> on\|off` | Turn the real bot engine on for one bot, or back to the spike's follow/assist behaviour. |

## Party and fight commands

| Command | What it does |
| :--- | :--- |
| `.tb order <bot\|party [leader]> <verb> [arg]` | Queue one party order: `follow`, `stay`, `come`, `attack`, `pull`, `pullback`, `endpull`, `hold`, `free`, `assist`, `co`, `nc`, `release`, `sell`, `buy`, `repair`, `train`, `mail`, `gather`, `craft`, `fly`, `ride`, `ah`, `rest`, `focus`, `role`, and the run-leader verbs. |
| `.tb party <leader> <members...>` | Form a party from named bots. |
| `.tb cmd <verb> ...` | The bound commander's order surface: tactical orders, tasks (`loot`, `open`, `use`, `gossip`, `state`, `lootmethod`), and `place`/`rally`/`fall back`. It answers `not-bound` until a `.tb master` binding stands. |
| `.tb commander status [<commander>]` | Show the bound commander cards, their bots and their state. |
| `.tb cc <bot> <mark> <unit guid\|target> [family]` | Give one bot a crowd-control mark. `.tb cc <bot> clear` removes it. |
| `.tb strategy <bot\|role:...> co\|nc <+name,-name,~name>` | Change a bot's strategies for testing. `<bot> reset` returns to the defaults. |
| `.tb state <bot>` | Dump the bot's state record, including the engine's values and its hybrid state keys. The record carries `cons` (the raid consumables the bot holds, `<category>:<aura id>`), `consmiss` (the categories still due, while `TortoiseBots.RaidConsumables` is not `0`) and `consnotcovered` (the slots the table has no item for on that bot's role: an elixir leg it cannot fill, a food buff, a weapon stone or oil). |
| `.tb why <bot>` | Dump one bot's AI decisions (the why trace). |
| `.tb ignore <bot\|party> [add\|remove\|list] ...` | Keep the commander's bots off named mobs or entries. |
| `.tb revive <bot>` | Revive a dead bot. |
| `.tb tele <bot> ...`, `.tb enter <bot\|party> ...` | Teleport a bot, or send it into an instance. |

## Dungeon fill and battlegrounds

| Command | What it does |
| :--- | :--- |
| `.tb lfg join <dungeon id> <tank\|healer\|dps>` | Put the sender's own character in the module's dungeon-fill queue as a group leader. |
| `.tb lfg status [<player>]` | Show who is queued, the dungeon, the role, what is still needed and which bots filled it. |
| `.tb lfg leave` | Leave the fill queue and release its bots. |
| `.tb bg status` | Show the battleground bots: queues, brackets, instances and holds. |
| `.tb bg events [since=<seq>]` | Show the battleground event ring. |

The player-side form is `.bot lfg join|status|leave`; it follows the same rules. Battleground fill itself is off until `TortoiseBots.Bg.Enable = 1`, and only WSG and AB are wired (`TortoiseBots.Bg.Maps`).

## Guild orders

| Command | What it does |
| :--- | :--- |
| `.tb guildorder create <bot> <item id> <amount> [farm\|craft\|buy]` | Give a bot in a guild an order. The sender must be able to give orders in that guild. |
| `.tb guildorder create <bot> enchant ...` | Order an enchant (the giver's item is enchanted by the bot). |
| `.tb guildorder list\|status <bot>\|work <bot> [id]\|cancel <bot>` | Read and drive orders. |
| `.tb guildorder search <bot> <farm\|craft\|buy\|enchant> [cat=<kind>] <text>` | Search the order catalogue. |
| `.tb guildorder who [<bot\|guild id>] <craft\|farm\|enchant> <text>` | Find a bot that can make or gather the item. |
| `.tb guildorder preview <bot> <farm\|craft\|buy> <item> <amount>`, `.tb guildorder steps <bot>` | Preview an order and its steps. |
| `.tb guildorder access`, `.tb guildorder rank <rank>`, `.tb guildorder reassign <bot> <newBot>` | Bank access, the bots' shared guild rank, and moving an order. |
| `.tb guildorder meet` | Bring the parties of an enchant order together. |
| `.tb craftorder create <bot> <item id> <amount>` / `status <bot>` / `cancel <bot>` | A crafting order with its own status. |

The order store needs the character-database table `tortoise_bots_guild_order`; if it is missing the command answers `store-unavailable` and the module disables persistence instead of failing the server. See [Install](install.md#database).

## Runs, plans and traces

| Command | What it does |
| :--- | :--- |
| `.tb plan list` | List the dungeon plans loaded from the world database. |
| `.tb plan <plan> info` | Show one plan's map, legs and steps. |
| `.tb plan <plan> check <character>` | Check every unit of the plan against a fresh instance. |
| `.tb plan reload` | Re-read the plan tables (waits while a run holds a plan). |
| `.tb run start <leader> <plan> [from step <N>\|from leg <leg>] [lead=player]` | Start a bot-led run (or the opt-in player-led one). The party must be bound. |
| `.tb run status <leader\|all>` / `.tb run pause\|resume\|stop <leader>` / `.tb run events <leader> [since=<seq>]` | Follow and control the run. |
| `.tb trace start <run_id> [party [<leader>]\|bot <Name>] [driver=<...>]` / `stop [<run_id>]` / `status` | Record a run trace. Off until `AiPlayerbot.RunTrace.Enabled = 1`. |
| `.tb why <bot>` | One bot's decision trace (why trace). |

Decision capture is not a command: it starts with a run when `AiPlayerbot.DecisionCapture.Enabled = 1`. See [Decision capture](decision-capture.md).

## Diagnostics

| Command | What it does |
| :--- | :--- |
| `.tb status` | Module enabled state and the number of bot sessions. |
| `.tb addonhint status <character>` / `.tb addonhint reset <character>` | Whether the character was already told where to get the add-on (`told`, `not-told pending` while the login waits out the delay, or `not-told -`), or clear that flag so the line can be shown again. Refuses while `TortoiseBots.Player.AddonHint = 0`. |
| `.tb unported` | Every `not ported yet` feature and how many times the engine reached it. |
| `.tb perf` | Reset and print the module's per-tick timings. |
| `.tb spells <bot>`, `.tb spell <id>`, `.tb spellscan`, `.tb castlog <bot>`, `.tb castfail <bot>` | Spell book, spell data, cast log and cast failures. |
| `.tb item <item id>`, `.tb itemscan`, `.tb gear <bot>`, `.tb equip <bot> <item id> [replace]`, `.tb additem <bot> <item id>`, `.tb removeitem <bot> <item id>` | Item templates, bot gear and bags. |
| `.tb level <bot> <level>`, `.tb train <bot>`, `.tb mount <bot>`, `.tb hurt <bot> <damage>`, `.tb power <bot> <type> <amount>` | Test-kit state changes. |
| `.tb onyxia <bot> observe <boss-guid>\|ground <boss-guid> <tank>\|status\|off` | The Onyxia ground/observer test kit. |
| `.tb los <a> <b> [map=<id>]` | The server's own line of sight between two units, a unit and a position, or two positions, with the distance. Read-only; see [Line of Sight Query](../play/commands/los-query.md). |

## Configuration reload

`.reload config` re-reads the module's own `worldserver.conf.d/tortoisebots.conf` keys: enable, pool, battleground, dashboard, automation, player commands, commander bindings and their friends. It does **not** re-read `aiplayerbot.conf`: the engine reads that file when the engine configuration loads, so an engine setting needs a restart (or a failed load retried by the reload). `TortoiseBots.Pool.Reset` is read only at world startup; `.reload config` never starts a reset.

See [Configuration](configuration.md) for the files and their install rules.

## Commands from the Turtle build that do not exist here

`.bot pool ...`, `.bot ah ...`, `.ahbot ...`, `.bot camp ...`, `.bot command ...`, `.bot hire ...`, `.bot run ...` and `.bot trace ...` are not registered on ForeverCore. Use the `.tb` commands above; player commands are `.bot` and are listed by `.bot help`.
