# `.tb` commands: the interface of the TortoiseBots module on ForeverCore

Written for the agent's command-channel round (dungeon plan A6) and for anyone driving a party from the console. The commands of slice 3
(section 3 onwards) have a **stable reply grammar**; the older commands (section 2) print free text prefixed `TortoiseBots: ` and are for a human.

## 1. Who may send, where the replies go

* A command is typed in chat by a GM character (`.tb ...`) or on the worldserver console (`tb ...`, the leading dot is optional there).
* Permission: every `.tb` row is registered with `rbac::RBAC_PERM_COMMAND_GM` (371) and `Console::Yes` (`TbScripts.cpp`, the command table). The console passes
  every permission (`CliHandler::HasPermission` is `true`, Chat.h:146). A player session needs permission 371: the core's `ChatCommandNode::IsInvokerVisible`
  (ChatCommands/ChatCommand.cpp:472-479) treats a command whose permission the account lacks as not existing: `ChatHandler::_ParseCommands` (Chat.cpp:166-179) then returns false and the
  text `.tb ...` goes out as ORDINARY SAY CHAT (unless the account holds 33, `RBAC_PERM_COMMANDS_NOTIFY_COMMAND_NOT_FOUND_ERROR`, which gets "no such command"). A client that gets no reply
  line within a few seconds should suspect the missing permission.
  * Who holds 371 on a stock database: the linked permission 198 (Moderator Commands) -> 371 (auth.rbac_linked_permissions) and the default role of security level 1
    and up (rbac_default_permissions: level 0 = 195, 1 = 194, 2 = 193, 3 = 192; read on forever-test1 on 2026-11-18, see the notes). So **an account of security
    level 1 or higher passes**; the agent's account is level 2, because `.gm on` and the engine's own GM tier (`PlayerbotAI::CheckBotCommandAuth`, only used by
    typed bot chat, which the order commands bypass) need 2.
  * A TEST account below level 1 is given the one permission, no new id and no database edit of the core's tables, from the console (test servers only):
    `rbac account grant <account name> 371 -1` (cs_rbac.cpp:62, `RBAC_PERM_COMMAND_RBAC_ACC_PERM_GRANT`; `-1` = every realm; it takes effect on the live session
    at once). To raise the level instead: `account set seclevel <account name> 2 -1` (cs_account.cpp:59). `rbac account list <account name>` shows what an account holds.
* Replies go to the SENDER: the core's `ChatHandler::SendSysMessage` (Chat.cpp:111-125) writes each line as a `CHAT_MSG_SYSTEM` packet to the sending session, so a network
  client reads them as system chat; for the console `CliHandler::SendSysMessage` (Chat.cpp:621-625) prints each line. The handler is the one the chat opcode built
  (`ChatHandler(this).ParseCommands`, ChatHandler.cpp:233), so the lines of a command always return to whoever typed it, at once, in order, before the next
  command is read. A line is one system message (up to 4095 bytes, ChatPackets.cpp:214); no line of this interface is longer than 1000 characters (a typical `state` line is 530, the longest possible about 900).
* Threads: console commands run in `World::ProcessCliCommands` (World.cpp:3166, called at :2592) and chat commands in `World::UpdateSessions` (World.cpp:3119, called at :2421;
  the chat opcodes are `PROCESS_THREADUNSAFE`, Opcodes.cpp:334-338): both on the world thread, as does the engine tick (`WorldScript::OnBeforeMapsUpdate`, World.cpp:2470).

## 1a. `.bot` (players)

Rounds R1-R3 of the player command layer (issue #129): a player types `.bot` in ordinary chat. The entry is a module `PlayerScript` hook
(`PlayerScript::OnCommand`), which the core calls before its own command lookup and before any RBAC check, so a non-GM sees it. It claims
exactly the word `bot` (any case, then end or a space): `.bot help` is ours, `.botany` stays the core's. With `TortoiseBots.Enable = 0`, or
in a build without the engine (`TORTOISEBOTS_SLICE1` off), the hook declines and the core answers as for any unknown command. The console
keeps `.tb`; `.bot` is for characters in the world. `.bot` is not listed in the core's `.help`.

**Who may order which bot.** Ownership is the **Battle.net account**: the bot character's game account belongs to the caller's Battle.net
account. A caller also gets the tactical verbs on a bot it is master of, commands through the commander binding, or leads the bot's party as
a real player. A GameMaster (RBAC 371, the `.tb` permission) may use every verb on every bot. A bot in a running dungeon run refuses every
`.bot` order to everyone, GMs included. Refusals name a stable code and never an account.

The verbs of rounds R1-R3 (`forever/docs/PC-R1-NOTES.md` .. `PC-R3-NOTES.md`; every later verb answers `unknown-verb` until its round):

| Command | What it does |
|---|---|
| `.bot` / `.bot help` | the usage rows the caller may use |
| `.bot who` (aliases `roster`, `list`) | the count, then one line per online bot the caller may order: name, level, class, map id, `follow|stay|free`, `engine|spike`. A GM's `.bot who all` lists every bot |
| `.bot add <name>` | logs one of the caller's own characters in as a bot (a GM: any character not online); it joins the caller's party. Owner verbs below need it first |
| `.bot logout <bot>` (alias `remove`) | logs the caller's bot out |
| `.bot follow <bot> [member]`, `.bot stay <bot>`, `.bot attack <bot>`, `.bot flee <bot>` | tactical orders queued for the bot's engine tick (`attack` reads the caller's selected unit; `flee` runs from the fight) |
| `.bot stats` | TB's fleet line: how many bots the caller may see are online, how many of those the caller owns, and how many run an engine |
| `.bot stats <bot>` | one line from the bot's own objects: level, class, health %, mana %, money in `g/s/c`, zone id and name, `alive|dead`, and the movement it keeps (`follow|stay|free`) |
| `.bot version` (aliases `v`, `about`, `credits`) | the build id, the `TBM:VERSION` line and the author notice (`NOTICE.md`); public metadata, shown even when `TortoiseBots.Player.Commands = 0` |
| `.bot features` | the server's player-facing switches as chat lines, the same list the addon Info window reads (`host/TbAddonInfo.cpp` kFeatures) |
| `.bot summon <bot>` | brings the bot to the caller's side through the `.tb tele` path. Owner or GM; the caller must be the bot's master (a GM need not be). Refused `teleporting`/`in-flight` while the caller or the bot is being teleported or rides a taxi, and `on-cooldown` inside the per-bot wait |
| `.bot maintenance <bot>` | refresh one owned bot's kit (reagents, bandages, hunter ammo), repair it for free, save it. Owner tier only (the caller owns the bot, or a GameMaster); pool bots are refused. Off unless `AiPlayerbot.OwnedBotMaintenanceEnabled = 1` (default 0, in `forever/conf/aiplayerbot.conf.dist`); potions, food, paid consumables and thrown top-ups are not ported |
| `.bot totem <bot\|party> <earth\|fire\|water\|air> <totem\|auto\|none>`, `set <earth>,<fire>,<water>,<air>`, `mode <auto\|prepull\|manual>`, `status` | a shaman's per-slot totem orders, read by the totem triggers (`ShamanTotemCommandPolicy.h`): `auto` keeps the subgroup pick, `none` leaves the slot empty, a totem name holds that totem (short names work: `mana spring`). `set` sets all four at once. Shamans only (`not-shaman`); a totem the bot never trained is refused `not-known`, and a `set` is validated for every slot before any slot of that bot changes. Owner tier; `status` is tactical. Values live until logout (#135 adds persistence) |
| `.bot behavior <bot\|party> <toggle> <on\|off>`, `status` | the old TB behaviour toggles (`loot`, `aoe` = "no aoe", `autocc`, `savemana` = "conserve mana", `boost`, `threat`, `potions`, `mimic`, `spread`, `hold`, `noblink`, `offheal`, `offdps`): `ChangeStrategy` on the toggle's state, read back with `HasStrategy`. Off-spec toggles (`offheal`, `offdps`) need paladin, priest, shaman or druid (`class`); a strategy this build does not register is `not-in-build`; a change that does not hold is `failed`. Owner tier; `status` is tactical and lists the toggles the class has. Values live until logout (#135 adds persistence) |
| `.bot blessing <bot\|party> <class> <blessing\|auto>`, `player <name> <blessing\|auto>`, `status` | a paladin's blessing orders (`PaladinBlessingOrderPolicy.h`): the blessing each class gets (`might`, `wisdom`, `kings`, `salvation`, `sanctuary`, `light`, or `auto` for the automatic coverage pick), or a per-character exception (`player <name> <blessing\|auto>`; it wins over the class row). A non-paladin is refused `not-paladin`; a blessing the paladin has not trained is refused `not-known` and never stored. Owner tier; `status` is tactical and lists the class rows, the blessings the bot knows and its player exceptions. Values live until logout (#135 adds persistence) |
| `.bot choice <bot\|party> <kind> <value\|auto>`, `status` | one ordered choice per kind (`ClassChoicePolicy.h`): `aura` (paladin), `aspect` and `sting` (hunter), `curse` and `pet` (warlock), `poison_mh` and `poison_oh` (rogue, by a usable poison item for that hand). `auto` keeps the automatic pick. A kind the class does not have is refused `class`; a word the bot cannot use is refused `not-known` and never stored. Owner tier; `status` is tactical and lists the kinds of the bot's class with the words it can use. Values live until logout (#135 adds persistence) |
| `.bot service <bot>` | the bare query (tactical): one line `food=<0\|1> water=<0\|1> healthstone=<0\|1> portals=<id:City,...\|->`, any rank of a conjure line counting |
| `.bot service <bot> portal <spellId>` | a mage's portal (owner tier): the policy decides first (`MagePortalPolicy.h`: `dead`, `combat`, `busy` while casting, `unknown` for a non-mage, an unknown city or a portal not learned, `group` when the caller is not in the bot's raid), then the rune is granted, the bot stops and stands, and the engine's `cast` action casts it. A build without that action answers `action-not-registered` |
| `.bot service <bot> conjure <food\|water\|healthstone>` | the highest known rank of the line through the same `cast` action (owner tier): `dead`, `not-known` when the bot has no rank of the line, `action-not-registered` without the action |
| `.bot lfg join <dungeon id> <tank\|healer\|dps>`, `.bot lfg status`, `.bot lfg leave` | the dungeon fill for the caller's own character (#160): queues it as its party's leader, lists the slots still open and the bots the fill added, and releases them. The same refusals as `.tb lfg`; the caller's own character only, never a named player |

Summon rules (spec D-8): every map, instance, combat and death rule is the shared `TeleportBotNextTo`'s (the function
`.tb tele <character> to <character>` calls), and its refusal codes pass through unchanged (`in-combat`, `in-flight`, `dead`, `teleporting`,
`already-on-map`, `not-in-group`, `teleport-refused`, entry codes, ...). Players get no revive. The wait is
`AiPlayerbot.OwnedBotSummonCooldown` (in `aiplayerbot.conf`; default 300 s, 0 = no wait, capped at 3600) and is stamped only by a summon
whose teleport started, so a refused try costs nothing; GameMasters are not held by it. The other `AiPlayerbot.OwnedBotSummon*` keys are
not read here. The refusal `not-a-bot`/`not-found` names no such online bot, `not-yours` a bot outside the caller's reach, `not-master` a
bot that follows someone else, and `in-run` any verb that names a bot inside a running dungeon run (GMs included; `.tb run pause` is the
GM's way).

Replies: `TortoiseBots: help: <row>`, `TortoiseBots: who: <n> bot(s)`, `TortoiseBots: who: <Name> level <n> <class> map <id> <move> <brain>`,
`TortoiseBots: stats <Bot>: ok level <n> <class>, <hp>% hp, <mana>% mana, <g>g <s>s <c>c, zone <id> <Name>, <alive|dead>, <move>`, and
`TortoiseBots: <verb> <Bot>: ok <text>` or `TortoiseBots: <verb>: refused <code>: <text>` with the codes `usage`, `unknown-verb`,
`players-off`, `not-found`, `not-a-bot`, `not-yours`, `in-run`, `not-leader`, `already-online`, `too-many`, `no-selection`,
`not-master`, `in-flight`, `on-cooldown`. A refusal never names an account.
`TortoiseBots.Player.Commands = 0` refuses players with `players-off`; GMs keep the command. `TortoiseBots.Player.MaxBots` (default 4) caps
how many of a player's own characters may be in the world at once when `.bot add` arrives. `.bot maintenance` answers
`TortoiseBots: maintenance <Name>: ok <n> item(s) granted, repaired, saved`, or `TortoiseBots: maintenance: refused <code>: <text>` with
`pool` (a pool bot), `disabled` (the flag is off) or `dead`, after the owner-tier `not-a-bot`, `not-yours` and `in-run`.

**The choice verbs (`totem`, `behavior`, `blessing`, `choice`, `service`; #133 rounds 1 and 2).** The scope word is one bot's name
or `party`: the bots in the caller's group the caller may order by the line's tier, in name order, at most 40 (a refused bot is
counted, not fatal; there is no `raid` word, party covers a raid group). Every bot of a line is decided by the pure ownership rule
first, and a `set` is validated for every target before the first value changes. A write replies `TortoiseBots: <verb> <Name>: ok
<what>` per bot, then for `party` the count line `TortoiseBots: <verb> party: <n> set, <m> refused`; a per-bot refusal is
`TortoiseBots: <verb> <Name>: refused <code>: <text>` with `usage`, `no-party`, `no-bot`, `not-a-bot`, `not-yours`, `in-run`,
`no-engine`, `not-shaman`, `not-paladin`, `not-known`, `class`, `not-in-build`, `failed`, `dead`, `unknown`, `group` or
`action-not-registered`. Persistence is #135: a value set here lives until logout.

## 2. The older commands (free text, human readable)

| Command | What it does |
|---|---|
| `.tb status` | module enabled?, number of bot sessions |
| `.tb create <account> <name> <race id> <class id> [gender]` | a new level 1 character for a bot. Needs the account-create right (as `.tb pool create`), and only a pool account (`TBPOOL<nnnn>`) or an account named in `TortoiseBots.Create.TestAccounts` takes one; any other account is refused |
| `.tb login <character> [master\|free]` | the character joins the world as a bot; follows `master` (from a player: the sender), or `free` = a masterless bot that levels on its own (W1: grind, food, loot, roll, buff; open-world death and corpse run). Without a master and with the engine on (`TortoiseBots.Slice.EngineByDefault`, an owned `.bot`) the bot runs the free kit too; with the engine off it stands, as the spike does for a bot with no master |
| `.tb free <bot> on\|off` | W1: turns the free kit on or off for a logged-in host bot (on: it must have no master and be in no party; off: a masterless bot stands again). Giving a bot a master (`.tb group`, the commander binding, a run) turns free off by itself. A free bot only starts fights with ordinary hostile or neutral NPCs: never a player or anything a player controls, a quest giver, vendor, trainer, flight master, innkeeper, guard or civilian, a kill that lowers a standing, or a mob another player tapped (it still defends itself) |
| `.tb logout <character>` | the bot leaves |
| `.tb group <bot> <master>` / `.tb ungroup <bot>` | the bot joins the master's party (the master leads) / leaves it. A bot that was free stops being free (the free kit and its ghost bookkeeping go). A full party (five) refuses the bot, or with `TortoiseBots.Raid.Enable = 1` becomes a raid first; a raid takes bots up to 40 (`TortoiseBots.Raid.Enable = 0` refuses a raid). A sixth member is refused when the party has a commander binding or a run (both end when it becomes a raid): `<leader>'s party has a commander binding (<bot>): a sixth member would make it a raid and end the binding, so it was not added; .tb raid <leader> converts the party on purpose` |
| `.tb raid <master>` | converts the master's party to a raid (the core's Group::ConvertToRaid). With `TortoiseBots.Raid.Enable = 1` a host join does this by itself when it would be the sixth member (see `.tb party` and `.tb layout`, section 4) |
| `.tb engine <character> on\|off` | the bot runs the real engine (PlayerbotAI) or the spike behaviour. A bot with no master has nothing to follow: the engine runs it as a free bot (and that bot may not be in a party). The switch lives until logout; a later `.tb login` takes `TortoiseBots.Slice.EngineByDefault` again |
| `.tb where <character>` | position, health, money, brain, strategies, target, cast-result counters |
| `.tb go <bot> <dx> <dy>` / `.tb hop <bot> <dx> <dy>` | a bot without a master walks / is teleported that many yards east and north |
| `.tb attack <bot>` | the spike's attack of the nearest hostile within 40 yd |
| `.tb lootnear <bot>` | test helper: the bot takes every item it may take from dead creatures within 10 yd through the core's `Player::StoreLootItem` (the item-loot path and its hooks, without the engine) |
| `.tb enter <character> <map id>` | what `Map::PlayerCannotEnter` says; a free bot is also refused (`map-cap`) once its map holds its kind's `TortoiseBots.Free.MaxPerMap` cap (dungeon 5, raids 10/20/40) |
| `.tb cast <bot> <spell id>` | the core's own cast on the sender's target |
| `.tb castfail <bot> <spell> <reason> [interrupt]`, `.tb castlog <bot> [on\|off]` | inject / read cast results |
| `.tb pushback <bot> <ms>` | inject a real `SMSG_SPELL_DELAYED` of the bot's own cast (a pushback): the engine's wait grows by the reported delay on its next tick and the action log writes `SPELL_DELAYED`; 0 or more than 1000 ms adds nothing, as the legacy handler had it |
| `.tb level <bot> <1-60>`, `.tb train <bot>`, `.tb hurt [<character>] <pct>`, `.tb power [<character>] mana\|rage\|energy <pct>`, `.tb rage <bot> <points>` | test setup (`hurt`/`power` with the name left out act on the sender's own character) |
| `.tb mount <bot> [status\|train\|buy]` | the mount errand by hand: `status` prints the bot's level, riding skill and the nearest riding trainer or racial mount vendor within 150 yd with its price; `train`/`buy` pay that NPC now (the bot's own gold, nothing is granted) and need the bot within interaction range. A free bot near the pair does the same by itself under `TortoiseBots.World.CapitalErrands` |
| `.tb additem <bot> <item id> [count]`, `.tb removeitem [<character>] <item id> [count]`, `.tb use <bot> <item id> [self]` | items |
| `.tb spells <bot>`, `.tb spell <id>`, `.tb spellscan`, `.tb item <id>` | what the spell and item mirrors hold |
| `.tb pool create <n>` / `.tb pool prune [confirm]` / `.tb pool start` / `.tb pool stop` / `.tb pool status` / `.tb pool population <n\|low\|high>` | the bot pool (W0): make n level-1 pool characters at `TortoiseBots.Pool.CreatePerMinute` (a fresh bot's first login then rolls its start level between `AiPlayerbot.RandomBotStartLevelMin` and `.Max`, default 1-60, and gives it one spawn kit at level 5 or above when `TortoiseBots.Pool.GearOnCreate` is on); list (`confirm` = remove) the unfinished creation rows; start the scheduler (target `TortoiseBots.Pool.TargetOnline`, alternating factions, `LoginsPerMinute` per 60 s, sessions and offline times rolled inside the configured ranges); stop it (every pool bot logs out, 5 per second); one status line (`state= target= online= alliance= horde= size= due= logins_60s= drain= last_pass_us=`, plus `capped by accounts: <n>` when the target is above the accounts left to the pool). One session per account, so the pool holds at most one character of each account; a bot that got a master (`.tb group`, a commander binding) leaves the pool's count and is never logged out by it, and its account is out of the pool's reach until the binding ends (a target above the accounts left shows in `capped by accounts`); a lowered target never logs out a dead or fighting bot; `population` changes the online target at runtime (`low`/`high` are the `TortoiseBots.Pool.PopulationLow`/`High` presets, a number is used as-is; the pool adds or logs out bots gradually, and `.reload config` puts the configured target back) |
| `.tb guildorder create <bot> <item id> <amount> [farm\|craft\|buy]` / `create <bot> enchant <spell link or id>` / `list` / `cancel <bot>` / `status <bot>` / `work <bot> [id]` / `meet <bot>` / `access [<bot\|guild id>]` / `rank <rank>` / `reassign <bot> <newBot>` | guild orders (W9): the bot farms, crafts or buys the items and delivers them to the guild bank or by mail; `status` shows `done/total` and the step it is on. `create … enchant` places one permanent enchant the bot knows (its Enchanting skill covers it; the giver must be a character) and `meet` is the giver's Meet press, standing in the bot's capital: the bot walks over, takes missing reagents in a materials trade, opens the enchant trade (put the item in the "will not be traded" slot), casts the enchant and confirms it with a whisper (`status` shows `ready`, `coming`, `enchanting`, then `done`). `access` reports the guild's bot rank, the bank tabs that rank may deposit into and whether you may order or change the rank; `rank` (promote+demote right) moves every module-owned bot of your guild to that rank and stores it in `tortoise_bots_guild_bot_rank`; `reassign` moves an order to another guild bot (same permission as giving orders; the target must have no running order) |
| `.tb guildorder search <bot> <farm\|craft\|buy\|enchant> [cat=<kind>] <text>` / `who [<bot\|guild id>] <craft\|farm\|enchant> <text>` / `preview <bot> <farm\|craft\|buy> <item> <amount>` / `preview <bot> enchant <spell link or id>` / `steps <bot>` | read-only guild order lookups: which orderable items the bot can farm (herb/ore nodes), craft (a known recipe) or buy (AH), which permanent enchants the bot knows (name, slot, required skill, verdict, reagents with bags+mail counts), which online guild bots can make, gather or cast it, what a new order would use and cost (bags, mail, recipe or enchant reagents, AH listing, delivery), and the order's step timeline. `cat=herb\|ore\|leather\|fish\|cloth\|drop\|other`; they never change an order |
| `.tb craftorder create <bot> <item id> <amount>` / `status <bot>` / `cancel <bot>` | a personal craft order, no guild needed: the crafter bot gets missing reagents (sub-craft, a nearby node that drops them, the auction house within the order's gold cap), keeps a `done/total` counter and mails the items to the ordering character (from the console: to the bot itself) |
| `.tb trace start <run_id> [party [<leader>] \| bot <Name>] [driver=<model>:<effort>:<run>]` / `.tb trace stop [<run_id>]` / `.tb trace status` | the opt-in run trace (`AiPlayerbot.RunTrace.Enabled`, default 0): records the party's bots as a bt-trace v1 JSONL file plus `<run_id>.summary.json` under `AiPlayerbot.RunTrace.Dir` (relative to `LogsDir`), which `tools/trace_score.py` scores. `party` with no leader uses the caller's group; from the console (no character) name the leader or use `bot <Name>`. One run at a time; a stop writes the end record and the summary on the writer thread. `status` names the run and its counters to a GM |
| `.tb why <bot> [seconds]` | one bot's AI decisions for a short window (default 30 s, at most 120) as `TBWHY\|` lines in the server log: each tick's chosen action and relevance, the top five that lost and why, the target and its victim, the active strategies, and the chat orders the bot received. One trace per server; it also ends when the bot leaves the world |
| `.tb perf`, `.tb unported` | tick cost window / the unported-feature counters |

## 3. Reply grammar, version 2 (every tokened reply of the commands below)

Lead decision 2026-11-20; the one encoder is `Enc`/`Rec`/`Reply` in `forever/src/host/TbH3.cpp`, and `.tb grammar selftest` prints fixed records so a client can test its parser against the module.

* **Lines.** A reply line is words separated by ONE space. The first word is the record kind (`state`, `party`, `threat`, `order`, `role`, `master`, `learn`, `equip`, `itemscan`, `statcensus`, `ignore`, `link`, `error`). Every other word is `key=value`.
  The LAST word of every line of a tokened command is `#<token>` (the caller's last word of the command, `#` and 1 to 32 characters of `A-Za-z0-9_.:-`; a malformed one is refused with a note in the error's `msg=`).
* **Markers.** ONLY `error` and the `los` diagnostic record (section 24: one line, the answer of every `.tb los` query, a stable interface for tools) are one-line replies. Every other tokened reply, an order's answer to one bot included, starts with `<command>-begin #token` and ends with `<command>-end count=<lines between> records=<records> ... #token`;
  the markers are named after the COMMAND (`state`, `party`, `threat`, `order`, `role`, `master`, `learn`, `equip`, `itemscan`, `statcensus`, `grammar`, `ignore`, `link`, `hear`), the data lines between them carry their own record kind as first word. `count=` counts every line between the markers, continuation lines included; `records=` counts the lines that are not continuations. Other totals on an end line (`bots=`, `resting=`) count records.
* **Keys** are `[a-z][a-z0-9_]{0,23}`; a key appears at most once per line.
* **Values are percent-encoded**: every byte outside `[A-Za-z0-9._-]` is written `%XX` (upper-case hex): space `%20`, `=` `%3D`, `:` `%3A`, `;` `%3B`, `,` `%2C`, `#` `%23`, `|` `%7C`, `%` `%25`, `+` `%2B`; `_` stays literal.
  **An empty value is the empty string** (`tgt=`); `none` has no special meaning anywhere and the module never writes `none`, `-1` or `0` for "no value" or "unknown". An unknown number is an omitted key (for example `manapct` is absent for a bot that has no mana). Hex is upper-case only.
  An empty list is the empty value; an empty ITEM (`a;;b`, a trailing `;`) is never written.
* **Structure** inside a value uses characters the encoding never produces for data: `;` separates list items, `:` separates the parts of one item, `,` separates coordinates and the plain numbers of one tuple (`resist=`, `res=`). Each part is encoded on its own:
  `co=close;protection;tank%20assist`, `top=Mtank:343;Mdpsb:338`, `pos=-9100.80,-2005.30,132.90`, `ord=41:ok`, `target=party:Mtank`, `class=2:8`.
* **Numbers** are decimal, **booleans** `yes`/`no`.
* **Length.** A line is at most 3,000 bytes (the token included). A record that is longer continues on `<kind>+ <identity key=value, `bot=` for a bot record> <more keys> #token` lines that follow their base line immediately. A single value longer than a line (a very long strategy list) is cut at a list item.
  Free text (`msg=`) is cut to 400 bytes before encoding.
* **Errors**: `error code=<word> cmd=<word> [msg=<encoded text>] #token`, exactly those keys, one line, nothing else for that command. A per-bot refusal inside an accepted command is the same words on the bot's own line: `order bot=.. verb=.. status=refused code=.. msg=..`.
  Codes: `engine-not-built`, `missing-target`, `no-such-player`, `not-a-bot`, `no-character`, `no-party`, `no-bots`, `ambiguous-party`, `missing-verb`, `unknown-verb`, `missing-argument`, `unexpected-argument`, `bad-argument`, `bad-target`, `bad-role`, `bad-id`, `too-many`,
  `no-engine`, `queue-full`, `not-in-group`, `needs-player-sender`, `no-selection`, `mark-empty`, `no-such-spawn`, `target-not-found`, `target-dead`, `not-attackable`, `no-pull-strategy`, `no-commander`, `other-map`, `too-far`, `nothing-to-assist`, `self-master`, `missing-commander`,
  `missing-leader`, `duplicate`, `party-refused`, and `action-not-registered` (with `name=<the legacy action key>`). BG2 adds `other-instance` and `other-team` (`.tb master`, section 4: a commander who is another real player, and the bot and the player are not inside the same battleground instance, or are on different teams). Slice 4 adds (section 8): `no-trigger`, `not-allowed`, `leader-refused`, `leader-inside`, `not-leader`, `lfg-group`, `not-empty`, `locked`, `no-entrance`,
`already-on-map`, `in-combat`, `dead`, `not-dead`, `teleporting`, `teleport-refused`, `timeout`, `stuck`, `foreign-bot` (owned by another commander or master), `foreign-lead` (in a party led by another real player), and the entry refusals `level`, `full`, `difficulty`, `not-found`, `other`, and `ignored` (`.tb cc` names a `.tb ignore` mob, section 14).
Section 20 adds `no-tank`, `no-rally`, `no-slot`, `no-fallback`, `cancelled` and the per-slot `ledge` (a floor more than 5 yd above or below the anchor), `liquid` (a stand point in water or lava), `liquid-path` (a path that enters it) and `detour` (a path far longer than the straight line). A refusal by the core itself (no permission, unknown command) is the core's own untagged text, not this grammar.
* **Addressing.** `<bot>` is a character name (a logged-in host bot). `party <leader>` is every host bot in the party of that character. `party` with no leader is, in this order: the bots bound (`.tb master`) to the sender, or to the one commander there is, or the members of the one bot-led party there is;
  anything else needs the leader's name (`error code=ambiguous-party`). Names are case-insensitive. The `target=` of a reply is `<name>`, `party:<leader>` or `party:commander:<name>` as parts.
  `.tb role`, `.tb equip`, `.tb learn`, `.tb hurt` and `.tb power` also accept the character of the sender (the GM who typed the command), by name; another real player's
  character is refused (`not-a-bot`), and the console can only name host bots. With the name left out, the first word is already the command's own argument (a role, a power
  kind, an id or a percent) and the target is the sender's own character: `.tb role tank`, `.tb equip <item id>`, `.tb learn <spell id>`, `.tb hurt <pct>`, `.tb power mana <pct>`.
* **In a build without the engine** every command below (and `grammar`) answers `error code=engine-not-built cmd=<command> msg=...`; the one exception is `.tb link` (section 17), which
  parses text and needs no engine.
* **GUID text.** `32 uppercase hex digits`: the two 64-bit words of the `ObjectGuid`, high word first, as `tgt=`, `victim=` and `mob=` print them and as `.tb order ... attack|pull` reads them (a leading `0x` is accepted); empty for none. Another way to name a creature: `spawn:<database spawn id>` (a creature of the bot's map).

### `.tb grammar selftest [#token]`
Prints `grammar-begin`, five `selftest` records (a value holding space, `=`, `:`, `;`, `#`, `%`, `_`, `,`, `+`, `|` and a two-byte character, and an empty value; a list with an empty item that is dropped, a list of parts, an empty list; a position with two fraction digits next to `map=`, a boolean, a number and a fraction;
a key given twice (the second wins); a record over 3,000 bytes whose long list moves to a `selftest+ bot=...` continuation line that repeats the identity key), `grammar-end count=<lines> records=<records> ... ready=no why=...` (a `why` list present because `ready=no`) and one `error` line. Real output of the final boot of package L:
```
grammar-begin #g2
selftest case=specials value=a%20b%3Dc%3Ad%3Be%23f%25g_h%2Ci%2Bj%7Ck%C3%A9 empty= #g2
selftest case=list items=x%20y;a%3Bb;50%25 pairs=Mtank:343;a%3Ab:1 none= #g2
selftest case=coords pos=-601.26,-4250.10,38.94 map=389 flag=yes number=-17 fraction=12.35 #g2
selftest case=once key=second #g2
selftest bot=Mtank case=overlong #g2
selftest+ bot=Mtank list=strategy%20number%200;strategy%20number%201;...(cut at an item, under 3,000 bytes) #g2
grammar-end count=6 records=5 note=fixed%20records ready=no why=resting:Mpriest;far:Mdpsb #g2
error code=selftest cmd=grammar msg=a%20b%3Dc%3Bd #g2
```

## 4. Orders (real output, package L boot 5)

### `.tb order <bot|party [leader]> <verb> [argument] [#token]`

Each bot of the address gets the order QUEUED at once; the engine runs it right before that bot's next tick (so within one world tick), through the legacy action. The reply is the queue answer, not the result. The result shows in `.tb state` as `ord=<seq>:<ok|fail%3A<code>>`.

| Verb | Argument | What runs (legacy action key) | Refused when |
|---|---|---|---|
| `follow` | none \| `none` \| a party member's name | `follow chat shortcut`; with a name: `follow target` first; `none` clears the manual follow target. A COMMANDED bot (bound by `.tb master`) follows its commander, the tank included (package L, `MasterTargetCompat`) | the name is not another member of the bot's party |
| `stay` | - | `stay chat shortcut` | - |
| `come` | - | the stay position = where the SENDER stands (the bound commander from the console), `stay` movement, a fresh stay walk | different map, or further than the engine's react distance (`too-far`; the host's own catch-up keeps a commanded bot within that distance, so it is not seen in practice) |
| `attack` | `my` \| `skull`/`star`/... \| `mark` \| `mark:<word>` \| `spawn:<id>` \| 32-hex guid | the unit goes on the bot's rti mark for the call, `attack rti target` runs, the marks are put back (`marks=0` on `state-end`) | no such unit, dead, not attackable, the bot is in no party; `my` from the console |
| `pull` | the same targets, then `[seconds 0-60]` and/or `ranged` \| `body` (any order, each once) | `pull my target` with the unit's guid as the event object (tank only); `ranged`/`body` are recorded on the pull for this command only and rolled back when the request refuses; then the party hold (package L: the other bots wait until the pull LANDED and the pack is on the tank: the order's seconds or 3 s, 10 s at the latest, at once when a mob hits them or the tank is under 60 %, 15 s after the order if it never lands) | as `attack`; the bot has no `pull` combat strategy; `ranged` and `body` together, a sign, more than 60 seconds or any other word (`bad-argument`) |
| `interrupt` | the same targets | one bot is picked (the first in party order with a castable interrupt: `counterspell`, `silence`, `kick`, `pummel`, `shield bash`, `bash`, `hammer of justice`, `earth shock`, `death coil`); it selects the target and runs that action, queuing it as a mature pass-through action when the direct call refuses while the target still casts (issue #131, section 23) | `no-target`, `dead-target`, `not-casting`, `no-interrupt` (with one `name:reason` per probed bot in `msg`) |
| `endpull` (or `end pull`) | - | `end pull` | - |
| `hold` | - | strategy `+passive` in combat and non-combat (`hold=yes` in the state line) | the strategy is not registered |
| `free` | - | `ResetStrategies`, movement `follow`, the explicit attack target cleared | - |
| `assist` | a party member's name | one shot: `attack` on what that member is attacking when the order runs | the member is not in the party, or has no attackable victim now |
| `co` / `nc` | `+name,-name` | `ChangeStrategy(arg, combat / non-combat)`: a debug switch | characters other than letters, digits, space, `+ - , _` |
| `release` | - | `release` (the dead bot releases its spirit; inside a dungeon the core revives it at the instance's entrance, section 8) | the instance has no entrance point (`no-entrance`: the release is held, never a graveyard outside) |
| `stop` | - | `CombatStopWithPets(true)`, `Reset(false)`, `MotionMaster::Clear`, the explicit attack target cleared (issue #131, section 23). It adds no `passive`: a bot hit again defends itself | the bot is dead |
| `flee` | - | `-wait for attack` in every engine, `CombatStopWithPets(true)`, `Reset(false)`, `MotionMaster::Clear`, then the `flee chat shortcut` (owner = the commander, else the bot) | the action is not registered |
| `guard` | - | `guard chat shortcut`: guards the spot the bot stands on (`mv=guard`) | the action or the `guard` strategy is not registered |
| `stance` | `near` \| `tank` \| `turnback` \| `turn` \| `behind` | the `stance` action with the canonical word (`turn` reads back as `turnback`); the order is `ok` only when the `stance` value names the new stance | another word (`bad-argument`), no word (`missing-argument`) |
| `engage` | `[seconds 0-60]` | `StartEngageWindow(commander guid, seconds)` (default 4): the bots hold until the commander's first hit | more than 60, or not a number |
| `rebuff` | - | opens the force-rebuff window (begin ms = now, pending/proposed false); the bot's normal buff pass then tops the party up | the bot is in combat (`in-combat`) |

One bot and a party (both have the markers):
```
order-begin #o1
order bot=Mdpsa verb=follow seq=1 status=queued #o1
order-end count=1 records=1 verb=follow target=Mdpsa bots=1 queued=1 refused=0 #o1
order-begin #o10
order bot=Mtank verb=stay seq=2 status=queued #o10
order bot=Mpriest verb=stay seq=3 status=queued #o10
order bot=Mdpsa verb=stay seq=4 status=queued #o10
order bot=Mdpsb verb=stay seq=5 status=queued #o10
order bot=Mdpsc verb=stay seq=6 status=queued #o10
order-end count=5 records=5 verb=stay target=party:commander:Mcmd bots=5 queued=5 refused=0 #o10
order-begin #o2
order bot=Mdpsc verb=stay status=refused code=no-engine msg=the%20bot%20has%20no%20running%20engine%20%28%60.tb%20engine%20%3Cbot%3E%20on%60%29 #o2
order-end count=1 records=1 verb=stay target=Mdpsc bots=1 queued=0 refused=1 #o2
error code=unknown-verb cmd=order msg=bogus #o12
```
`seq` numbers orders over the whole server; the bot's last one is `ord=` in its state line. An order older than 15 s that the engine could not run is dropped with `fail:expired`.

### `.tb role [<bot|character|party [leader]>] tank|healer|dps|none [#token]`
Stores the role (`PlayerbotAIStorage::SetPlayerForcedRole`) and in the bot's next engine tick sets the AI's forced role and rebuilds the kit. `role-begin`, `role bot=Mdpsb role=dps seq=7 status=queued` (`status=stored` when it has no engine yet), `role-end count=.. records=.. role=.. target=.. bots=.. queued=..`. The `tank` role is refused (`status=refused code=no-tank-kit`) for a class the slice gives no tank kit (a shaman, priest, mage, rogue: the restoration/healing kit is all it has); a paladin may hold it (the protection kit, recorded only), a druid gets the bear kit and a warrior the protection kit. A refusal also clears a tank bit the bot already has (`cleared=yes`: the stored role, the AI's own and the kit built from them), so an order from an older build, `.bot role` or the dungeon finder cannot leave a restoration shaman read as a tank.
The target is a host bot, the sender's own character, or `party [leader]` (host bots only); `.tb role tank` (no name) means the sender's own character, whose role is stored for the live side to read back.


### `.tb master <bot|party [leader]> <commander|none> [#token]`
Binds the bots to a commander who stands OUTSIDE their party (decision D2): host master and engine master become the commander, `IsInCommandedGroup` answers true for them, and a commanded bot keeps that master (the engine's adoption step skips it) and follows it; the binding's lead is the bot the command named. When a player leading his own party names that party (`.tb master party Mcmd`), the lead is a bot of it (a tank first), never a bot bound elsewhere. `none` releases them.
The commander is a host bot or the sender's own character; the one exception (BG2, live row 5) is INSIDE a battleground, where an operator may bind a pool bot to the real player he watches (`host/cmd/TbMasterBindPure.h`): BOTH must be inside the SAME match instance and on the SAME team, else the refusal is `error code=other-instance` or `error code=other-team` (per bot inside an accepted party command). The rule is decided purely and pinned by `forever/tools/test_master_bind_pure.cpp`; every other refusal of the plain rule is unchanged (`not-allowed`, same text). The bound pair is checked on every order: a commander who left the match (or was transferred), a bot that left, or a side change denies the order at once (`cmd::IsCommandedBy` -> `life::MayCommandBg`, #315), and the 1 Hz clean-up then releases the bind: `bg-commander-left`, `bg-bot-left`, or `bg-team-changed` for a transfer to the other side.
`master-begin`, `master bot=Mtank commander=Mcmd commander_in_party=no seq=44 status=queued`, `master-end count=.. records=.. target=.. commander=.. bots=.. queued=..`.

A binding ends by itself when the party it was made on stops being that party: the lead leaves the world or the party, the group leadership goes to anyone but the lead bot, or - for a binding made in the party - its commander (a third real player, or a bot that is not this lead; a commander outside the party gets no such exception), another player joins a commander-outside binding, the party becomes a raid or grows past five, the commander leaves an in-party binding, the lead lies dead past `TortoiseBots.Slice.DeadBackstopSeconds`, or the commander is away past `TortoiseBots.Slice.MasterGraceSeconds` (a server shutdown keeps it). The release restores the followers' own follow targets, clears h3's table and logs one line with the reason (`lead-left`, `lead-dead`, `group-changed`, `leader-changed`, `player-joined`, `group-raid`, `group-full`, `commander-left-party`, `commander-away`, `unbound`, `disabled`). With `TortoiseBots.Commander.KeepBindings = 1` the binding is also written to the characters table `tb_commander_binding` (character guids only) and restored after a restart once the commander and the lead are back in the world; the party is formed again through the lead and every member is bound again.

### `.tb party <leader> <member>... [#token]` (forms) and `.tb party <leader> [#token]` (roster)
Forming: up to four host bots join the leader's party (up to 39 with `TortoiseBots.Raid.Enable = 1`: the join that would be the sixth member turns the party into a raid first, and the
`party-join` record of a member that joined a raid carries `sub=<1-8>`, the subgroup it took; in a raid that has a layout (`.tb layout` below) a newcomer takes the seat of its role, nobody else moves, and a healer's record also says whom it heals (`heals=<tank>|raid`); the end line says `raid=yes|no`); each bot's HOST master becomes the leader: run `.tb master` afterwards (in a raid: `.tb master`, then `.tb layout`). Replies `party-begin`, `party-join bot=.. leader=.. status=joined|refused [code= msg=]`, `party-end count=.. leader=.. joined=.. asked=.. note=..`. A member that is already in another party (the core keeps a persisted group with its offline members) refuses the whole command before anything is joined, with the one-line `error code=party-refused cmd=party msg=<member> is already in another party (<members>): .tb ungroup <member> first`; nothing is changed.
Roster (real output):
```
party-begin #p9
party member=Mtank leader=yes online=yes bot=yes class=warrior level=18 role=tank engine=yes master=Mcmd commanded=yes #p9
party member=Mpriest leader=no online=yes bot=yes class=priest level=18 role=healer engine=yes master=Mcmd commanded=yes #p9
...
party-end count=5 leader=Mtank bots=5 raid=no commander=Mcmd #p9
```
(`commander=` is empty, one name, or `mixed`: it is what the party's MEMBERS are bound to; the leader's own binding is only on its line, so a run's party reads the run's commander (the tank) as one name. An offline member has only `member`, `leader`, `online=no`.)
In a raid each roster line also carries `sub=<1-8>` (the member's subgroup) and, when it has one, `mark=maintank|assistant` (the group's main-tank or assistant mark).

### `.tb layout <leader> [#token]`
Puts the leader's RAID into the raid layout of the legacy commander (`runtime/RaidLayoutPolicy.h`, `RaidLayout::Plan`): the main tank (the tank that already has the
group's main-tank mark, else the leader when it is a tank, else the first tank in the roster) heads subgroup 1 and each off-tank the next subgroup; the tank healers
sit with their tank (one to two per tank, after the raid keeps a third of the healers), the raid healers spread over the subgroups, the damage dealers fill the free seats.
The moves never overfill a subgroup: every member slot counts, an offline member keeps its seat (it is planned where it sits), and a plain move is made only when its subgroup has a free seat (`Group::HasFreeSlotSubGroup`; the core's `Group::ChangeMembersGroup` does not check it, only the client's handler does). A member whose target is full swaps with one that leaves it, or stays where it is (`planned=`). The main tank gets the
main-tank mark, each off-tank the assistant mark, and a tank healer that is a host bot with an engine heals its tank first (`focus heal targets`: the tank, then itself). A raid healer gets no list (an old one is cleared on every layout), and when a tank leaves the group its healers' lists are cleared, so they heal the whole raid again.
Every member in the world is planned, real players too, so your own seat may change. Roles: the role `.tb role` stored first, then a bot's engine, else the class and talents (`PlayerbotAI::IsTank` / `IsHeal`): a layout right after `.tb party` finds the tanks and healers even before `.tb master`. A healer's focus needs its engine, so the order is `.tb party`, `.tb master party <leader> <commander>`, then `.tb layout` (a layout before `.tb master` answers `focus=no`; run it again). Who may
name the leader: as `.tb party` (a host bot or the sender's own character). The layout is remembered for the group until it disbands or the server restarts, so later joiners
are seated by role; before a joining healer is seated the record is brought up to date (a tank that left or is no tank any more is dropped with its healers, a healer that left is not counted), so it never heals a tank who is not there. Run `.tb layout` again after roles change.
Replies: `layout-begin`; one `layout member=<name> role=tank|healer|dps sub=<1-8> [tank=main|off] [heals=<tank> focus=yes|no | heals=raid] [planned=<1-8>]` per member
(`focus=yes`: the healer's engine got the focus); `layout-end count=.. leader=.. members=<n> tanks=<n> focus=<n> misplaced=<n> groups=1=<names>;2=<names>...`.
Real output is recorded in `forever/docs/RAID-GROUPS.md` once the live check has run. `planned=<1-8>` on a line and `misplaced=` above 0 mean the core did not
take a move. Errors: `missing-leader`, `unexpected-argument`, `no-such-player`, `not-allowed`, `no-party`, `not-raid` (a party: nothing to lay out), `no-tank`
(no member in the world has the tank role), `layout-refused`, plus the access codes of `.tb party`.

## 5. State

### `.tb state <bot|party [leader]> [#token]`
`state-begin`, one `state` line per bot (more than one line when it exceeds 3,000 bytes), `state-end`. Keys in this order:

| Key | Meaning |
|---|---|
| `bot` `role` `rolesrc` `class` `level` | name; `tank`/`healer`/`dps` or empty; `forced` or `auto`; lower-case class; level |
| `hp` `hpmax` `hppct` | health, maximum, percent |
| `power` `pw` `pwmax` `manapct` | main power, value, maximum (RAGE IN RAGE POINTS), mana percent (absent for a bot with no mana) |
| `st` `incombat` | the engine's state `combat` / `non-combat` / `dead`; the core's in-combat flag |
| `tgt` `tgtentry` `tgthp` `victim` | the engine's current target (GUID text, entry, health percent) and the unit the core says the bot is fighting; empty when none |
| `hold` `mv` | the passive strategy is on; movement `follow` / `stay` / `guard` or empty |
| `stuck` | empty; `blocked:<reason>`; `away:<seconds left>`; `noengine` |
| `engine` | a PlayerbotAI runs the bot. Attached when the bot is in the world and on its master's map, or inside an instance one of its party's other members stands in (another character, in the world, not between maps, in the bot's exact instance: who leads the group does not matter, so a leader bot with its party beside it keeps its engine and a lone leader keeps none; it follows the party's leader there, so a party that entered while its commander stayed outside keeps its engine); a bot that lands elsewhere reads `no` until it stands on the master's map again |
| `pos` `map` `inst` | ONE key `x,y,z` with two decimals; map id; instance id (no separate x/y/z keys) |
| `dl` `dm` | yards to the party leader / to the master (`self`, `othermap`, a number, empty when there is none) |
| `ord` `lastorder` | `<seq>:<ok\|queued\|fail%3A<code>>` of the last order, and its verb; both empty when the bot has had no order |
| `co` `nc` | combat and non-combat strategies, a `;` list |
| `cast_queued` `cast_delivered` `cast_dropped` `cast_pending` | the cast-result queue counters (absent without a bot session) |
| `delayed` | S2: pushbacks (`SMSG_SPELL_DELAYED` of the bot's own cast) the session accepted since the engine started (absent without a bot session) |
| `eating` `drinking` | a seated food / drink aura is on the bot |
| `resting` `rest_why` | the rest gate: `resting=no` means out of the old rest conditions; `rest_why` is a `;` list of `dead`, `combat`, `hp`, `mana`, `eating`, `drinking` |
| `life` | slice 4: `alive`; `dead` (a body that will release); `awaiting-res` (a body held by the crew rule, or with a resurrection pending); `ghost`; `running-back` (alive again, more than 30 yd from its commander or leader, until it is within 20 yd) |
| `relhold` | slice 4: why a BODY on an instance map waits (commanded bots): `rezzer` (a living priest, paladin, shaman or druid of the party is on the map), `combat` (a member fights), `wipe` (nobody is left: it releases now), `no-entrance` (the instance has no entrance point: the release is held); empty otherwise |
| `resreq` | slice 4: the party member or commander whose resurrection offer is pending for this dead bot; empty when none |
| `corpse` `corpsemap` | slice 4, dead bots only: `x,y,z` of the body (unreleased) or of the corpse a ghost has, and its map id |
| `outside` | slice 4: `yes` when the party's leader is inside an instance and this bot is not in that instance, else `no` |
| `entered` `entry` `block` `blockmsg` | slice 4: how the last map change the host started happened (`trigger`, `order` (a direct entry or `.tb instance leave`), `setup` (`.tb tele`); empty = none yet); `entry` is the phase of a running entry (`walking`, `waiting-leader`, `teleporting`; empty when none); `block` is the code of the last refused entry or lost teleport (empty = none) and `blockmsg` says it in words |
| `catchup` `practice` | slice 4: how many open-world catch-up teleports the host made for this bot (never into or inside an instance); `yes` after `.tb revive` until the next combat ends |
| `free` `kills` `deaths` `xp` `xpnext` | W1: the free-bot flag; creatures the bot was credited with since login; deaths since login; experience into the level and the level's next total |
| `grind` | W1: the `grind target` value as `<entry>:<level>:<yards>`, empty when it holds none |
| `corpserun` `ghostyd` | W1: a dead bot's leg: `release` (a body about to release), `walk` (ghost walking to its corpse), `delay` (inside the reclaim radius, waiting out the core's reclaim delay), `safe-spot` (inside the radius but too many hostiles: stepping to a safe point), `reclaim` (inside the radius, nothing in the way), `healer` (the spirit-healer leg), `rescue` (the host's five-minute ghost rescue ran); empty while alive. `ghostyd` = whole yards to the corpse when the ghost and the corpse share the map |
| `rescue` `tele` | W1: ghost rescues so far (the host's only teleport besides the core's release) and every teleport the bot took, any cause (the core's release teleport counts) |
| `sick` `dur` `bags` | W1: resurrection sickness is on the bot; the lowest equipped durability percent; free bag slots (`bags` is `bagfree` under the W1 name) |
| `form` `totems` `imbue` `seal` `paura` | slice 10 (appended after `practice=`): `form` = the shapeshift form id the bot is in (bear 5, cat 1, ghost wolf 16) or `none`; `totems` = `<creature entry>:<seconds left>;...` for the bot's live totems (summon slots 1-4, read only), empty when none; `imbue` = `<enchant id>:<seconds left>` of the main-hand weapon's temporary enchantment, empty when none; `seal` = the aura id of a paladin's own active "Seal of ..." (empty for every other class), `paura` = the aura id of a paladin's own active "... Aura" (empty otherwise). The power type and amount stay in `power` `pw` `pwmax`; no second `power=` is added |
| `spec` `gear` | W6a (appended after `money=`): `spec` = the bot's active weightscale spec name (empty when none); `gear` = `<slot>:<item>;...` for each occupied equipment slot (`0..18`), empty when none |
| `resist` `rgear` | #20: `resist` = the six resistances the worn gear gives, in the client's school order `<holy>,<fire>,<nature>,<frost>,<shadow>,<arcane>` (a comma tuple, like `pos=`); `rgear` = the resist set this module holds on the bot as `<school>:<pieces>` (`school` the data file's word: `fire`, `nature`, `frost`, `shadow`, ...), empty when it holds none. A hold that could not put a piece back (dead bot, full bag, refused swap) stays listed and is retried (every 5 s, then once a minute, never dropped); it is also stored in the characters database, so a crash or a restart restores the normal set on the next login |
| `chatin` `chatlast` | S1-R2 (appended after `ghostyd=`): `chatin` = `<accepted>:<refused>:<dropped>` of the bot's chat inbox (lines the filter accepted, lines it refused, lines dropped — a full inbox of 16 drops its oldest, and a line dropped on its way to the engine counts here too); `chatlast` = the word of the last decision: `accepted`, `kind`, `addon`, `self`, `not-real`, `length`, `not-authorized`, `other-subgroup`, `no-engine`, `dropped` (empty until a line was judged) |
| `stance` `rebuff` | issue #131 M1 (appended after `held=`/`combo=`): `stance` = the stance the bot is in (`near`, `tank`, `turnback`, `behind`; empty without an engine); `rebuff` = whole seconds left of a force-rebuff window, empty when no window runs |
| `cons` `consmiss` `consnotcovered` | issue #22 (appended after `runrate=`): `cons` = the raid consumables the bot holds, `<category>:<aura id>;...` per held category (`flask`, `battle`, `guardian`, `food`, `weapon`, `scroll`; a weapon row is held by one of the table's own temporary enchantments on the main hand (a rogue poison or a shaman imbue does not count) and shows `weapon:0`), empty when none; `consmiss` = the categories still due for its role, level and map, a `;` list in the table's order, empty while `TortoiseBots.RaidConsumables` is 0, the map is not a raid (or a dungeon with the key), or the bot is dead or in combat; `consnotcovered` = the slots the table has no item for on this bot's role (today a tank's `battle`, `food` and `weapon`, a caster's `guardian`, a hunter's `weapon`, a healer's `battle` and `food`), a `;` list in the table's order, empty while `TortoiseBots.RaidConsumables` is 0 or the bot has no role |

End line: `state-end count=<lines> records=<records> bots=<n> resting=<number of bots resting> ready=yes|no [why=<reason>:<bot>;...] marks=<set raid marks> target=<label> #token`.
Slice 4 adds to the end line: `alive=<n> dead=<n> ghost=<n> wipe=yes|no whole=yes|no` (`dead` = bodies; `wipe` = nobody is alive; `whole` = every addressed bot is alive, on its leader's map and within 40 yd of it).
**ready** = every bot is alive, out of combat, not resting, within follow range of its master (10 yd; 18 yd for a healer) and not blocked or away. `why` lists `<reason>:<bot>` items, reason one of `dead`, `combat`, `resting`, `far`, `blocked`.

Real output (one bot, final boot of package L; the party was inside Ragefire Chasm):
```
state-begin #t2
state bot=Mtank role=tank rolesrc=forced class=warrior level=18 hp=708 hpmax=708 hppct=100 power=rage pw=0 pwmax=100 st=non-combat incombat=no tgt= tgtentry= tgthp= victim= hold=no mv=stay stuck= engine=yes pos=-3.85,-11.03,-15.83 map=389 inst=1 dl=self dm=2.4 ord=2:ok lastorder=stay co=close;protection;protection%20raid;pull;stay;tank%20assist;tank%20face nc=default;food;protection;protection%20raid;stay;tank%20assist cast_queued=0 cast_delivered=0 cast_dropped=0 cast_pending=0 eating=no drinking=no resting=no rest_why= #t2
state-end count=1 records=1 bots=1 resting=0 ready=yes marks=0 target=Mtank #t2
```
(names in the sample are the live test's; the structure is exact.)

### `.tb threat <bot|party [leader]> [#token]`
READ ONLY (`GetSortedThreatList`, `GetLastVictim`). `threat-begin`, one line per mob that has one of the bots on its threat list, `threat-end count=<lines> mobs=<n> bots=<n> target=<label>`:
`threat mob=<GUID text> entry=426 name=Redridge%20Brute hppct=100 victim=Mtank target=Mtank size=3 top=Mtank:343;Mdpsb:338;Mdpsc:196 pos=-9091.12,-2001.40,131.00 flee=no #token` (`victim` is the mob's current threat victim, `target` what the mob is fighting, `top` the first three of the sorted list; slice 4 adds `pos=x,y,z` of the mob and `flee=yes|no`: the mob is in the fear state of a flee, or walks to the nearest helper as the core's `DoFleeToGetAssistance` makes it).

## 6. Test kit
* Who may be named: a host bot, or the sender's own character (`.tb learn`, `.tb equip`, `.tb removeitem`, `.tb hurt`, `.tb power`, and `.tb role`); another real player's character is refused (`not-a-bot`). The name may be left out when the first word is already the command's own argument, and then the command acts on the sender's own character. `.tb itemscan` stays bot-only.
* `.tb learn [<character>] <spell id>...`: `learn-begin`, `learn bot=.. spell=<id> result=<learned|known|unknown|invalid|not-learned> name=..`, `learn-end count=.. bot=.. learned=..`. Core path: `SpellMgr::IsSpellValid`, `Player::LearnSpell`. At most 40 ids.
* `.tb equip [<character>] <item id> [replace]`: `equip-begin`, ONE record `equip bot=.. item=<id> name=.. result=<equipped|refused> [slot=..] why=<empty|EQUIP_ERR_...|unknown-item|bags-full|swap-failed> [code=..] [replaced=yes] [destroyed=<n> destroyed_item=<entry>:<item guid>:<reason>[;...]] [note=..]`, `equip-end count=1 records=1 bot=..`.
  `Player::CanEquipNewItem` then `Player::EquipNewItem`; a taken slot refuses (`EQUIP_ERR_NOT_EQUIPPABLE` or `EQUIP_ERR_NO_SLOT_AVAILABLE`); with `replace` the item is stored in the bags and the core's `Player::SwapItem` puts it on.
  A replaced piece is DESTROYED instead of put in the bags only when the new piece is confirmed in the slot AND the old piece is one of the items the core creates that character with (its start-outfit data: the items the core itself creates the race and class with, the in-memory `PlayerInfo::item` list that `Player::Create` stores, read through the module's guarded access) or poor (grey) quality; it then does not take a bag slot. Everything else goes to the bags as before: quality alone does not make a piece disposable. A quest item is never destroyed: an item in the quest item class, one that bonds as a quest item, one that starts a quest, and one an item objective of a quest still in the character's log (incomplete, complete or failed) requires. Together with a bag that still holds items these go to the bags. `destroyed` is how many pieces were destroyed and `destroyed_item` their `entry:item guid:reason` (`;`-joined, `reason` is `start-outfit` or `poor`; a two-hander can displace the offhand too). Each destroy also writes one INFO server log line naming the character (guid and name), the item (entry, guid, count) and the reason. The displaced piece is remembered by GUID and found again after the swap (the swap can merge a stackable piece into a bag stack and free it: then nothing is destroyed); only a single item is destroyed, a disposable stack of more than one stays in the bags and the record says `note=kept-stack:<entry>x<count> in bags` (also an INFO log line). Only a host bot's pieces, or the sender's own character's when the sender is a GM, are ever destroyed; another real player's character is refused before anything changes.
* `.tb removeitem [<character>] <item id> [count]`: `removeitem-begin`, ONE record `removeitem bot=.. item=<id> removed=<n> left=<n>`, `removeitem-end count=1 records=1 bot=..`. The inverse of `.tb additem`: it first collects the exact stacks of that item in the target's bags (the backpack, the four worn bags and the reagent bag; never equipped pieces, the worn bags themselves, the bank, the keyring or child-equipment slots, nor a stack in an open trade) and destroys only those, through the core's Item*-based `Player::DestroyItemCount` (quest bookkeeping included), up to `count` (no count = all). Nothing outside the bags can be touched. `left` is what the bags hold after the removal, read again, never computed; it includes a stack in an open trade (the item is still in its bag slot until the trade closes), which `removed` never takes. Only the item named is touched (a quest item goes only when it is the one named). Same targets as `.tb learn` (a host bot or the sender's own character). The count is digits only, 1 to 999999999: `-1`, `+3`, `0`, `3x` and anything longer are `bad-id` (`.tb equip` and `.tb learn` take the same digits-only ids; `.tb additem` keeps the core's own parser). Persistence matches the core's `.additem` (it calls no save): the change is written with the next autosave or at logout. Errors: `missing-bot`, `no-such-player`, `not-a-bot`, `no-character`, `engine-not-built`, `missing-argument`, `bad-id`, `unknown-item`, `unexpected-argument`.
* `.tb itemscan <bot> <item id>...`: `itemscan-begin`, per id `itemscan bot=.. item=<id> exists=<yes|no> [name= class=<c>:<sc> inv= req= quality= ilvl= canequip=<yes|no> slot=<n, only when yes> why=<empty|EQUIP_ERR_...> [usage=] w=<weight> [worn=<item> wornw=<weight>]]`, `itemscan-end count=.. bot=.. exist=.. canequip=..`.
  `canequip` is the core's check for THIS bot with swap = true. `usage=` is `ItemUsageValue::QueryItemUsageForEquip` (omitted when the bot has no engine), `w=` is `RandomItemMgr::ItemStatWeight`, and `worn=` / `wornw=` are the currently equipped item in the candidate slot and its weight (omitted when the slot is empty or unusable). Nothing is created or changed. The ids come from the data collaborator's list; the module holds no id.
* `.tb statcensus`: `statcensus-begin`, one `statcensus` record per `ItemStatType` id seen with a non-zero value across all loaded item templates: `statcensus id=<stat id> items=<count> min=<min> max=<max> sample=<item id> samplename=<name> sampleval=<val>`, `statcensus-end count=.. records=.. templates=.. with_stats=.. stat_ids=..`. One pass, read only.
* `.tb gear <bot|party [leader]> show [#token]`: READ ONLY (allowed in combat; nothing is equipped): `gear-begin`, one `gear bot=.. slot=<0..18> item=<item id> ilvl=<n> score=<n> res=<holy>,<fire>,<nature>,<frost>,<shadow>,<arcane>` per OCCUPIED equipment slot (`score` is `RandomItemMgr::CalculateStatWeight` for the bot's spec, `res` the same comma tuple as `resist=`), `gear-end count=.. records=.. target=.. bots=..`. The equipment metric of #20 and #26.
* `.tb gear <bot|party [leader]> upgrade [#token]`: `gear-begin`, runs `EquipUpgradesAction` synchronously on the world thread for each addressed bot (explicit order, so commanded bots also equip bag upgrades), and diffs equipment slots `0..18` before/after: one `gear bot=.. slot=<n> old=<item id or 0> new=<item id>` per changed slot, or `gear bot=.. changed=0` when nothing changed; `gear-end count=.. records=.. target=.. bots=.. changed=<total>`. Refuses before changing anything with `error code=no-engine` or `error code=in-combat`.

## 7. Config keys

`TortoiseBots.Slice.RestHealthPct` (default 90) and `TortoiseBots.Slice.RestManaPct` (default 80), `forever/conf/tortoisebots.conf.dist`; read on each `.tb state`.

`TortoiseBots.Orders.Tactical` (default 1): the kill switch of the tactical orders (section 23). At 0 every one of them answers `error code=disabled`; read on each command, so a config reload counts at once.

`TortoiseBots.RaidResistGear.Enable` (default 0), `.Range` (default 60) and `.ReleaseSeconds` (default 15), `forever/conf/tortoisebots.conf.dist`: the resistance-gear swap around a raid boss's school. At 0 nothing swaps. With it on, a host bot grouped with a real player on a raid map may have the bag pieces of the boss's school put on in place (`Player::SwapItem`; nothing created, sold or destroyed); the hold is stored in the characters table `tortoise_bots_resist_hold`, which carries the hold's state (`releasing` 0 = the set is on, 1 = a restore was asked and did not finish, retried by the world tick and finished after a restart). Turning the key off while a set is held releases it; a bot that cannot take its pieces back yet (dead, in combat, a full bag, its own piece in the bank) keeps them on and the world tick retries (5 s apart at first, then once a minute, never dropped). `Range` (5-200 yd) and `ReleaseSeconds` (0-600 s) are read at load for the automatic boss-range arming step; nothing else reads them in this build. Read-out: `.tb state` `resist=`/`rgear=`.

`TortoiseBots.Raid.Enable` (default 0, read at startup and `.reload config`): 1 lets host joins (`.tb login <bot>` with a master, `.tb group`, `.tb party`) form raids: the sixth
member turns a party into a raid first and a raid takes members up to 40. 0 keeps the 5-man party and refuses a raid. Only these explicit commands make or fill a raid: the automatic joins (the free-bot quest groups, the hire flow, a stored commander binding's restore, `.bot add`) keep the 5-man refusal, and a sixth member is refused when the party has a commander binding or a run.

## 8. Slice 4: entering a dungeon, instances, death (package H4)

Commands of the "instance and death" slice. They follow the grammar of section 3 (markers named after the command, `count=`/`records=`, `error` on one line, the token last). **The sample lines below are REAL output of the module on ForeverCore (package M4, build VM test server, 2026-11-19): every block was copied from a boot.** `.tb grammar selftest` also prints one fixed-value record of every kind and key of this section (`selftest case=state-h4`, `state-h4-refused`, `state-end-h4`, `threat-h4`, then one `trigger enter instance leave reset tele revive kill gm` record): its output on the final binary is in section 9. The old `.tb enter <character> <map id>` (a character and a map NUMBER, free text) is unchanged.

Who may be named: a host bot, or the character of the GM who typed the command (`MayLeadBots`); a second character in a command goes through the same check (`not-allowed`). Everything runs on the world thread.
A reply is TRUE: a record says `started` when the core accepted the first step and the outcome is in the bot's `state` record (`entered=`, `block=`, `inst=`), never `ok` before the change holds; a refusal changes nothing.

### `.tb trigger <area trigger id> [#token]`
The AreaTrigger.db2 row and its `areatrigger_teleport` target. Read only.
```
trigger-begin #trg
trigger id=2230 map=1 pos=1818.40,-4427.26,-10.45 shape=box length=21.69 width=11.83 height=21.22 yaw=0.58 target=1102230 targetmap=389 targetpos=0.80,-8.23,-15.53 #trg
trigger-end count=1 records=1 id=2230 #trg
```
(`pos` is the box centre, `target` is the `world_safe_locs` id the core's handler sends the player to.) Errors: `missing-argument`, `bad-id`.

### `.tb enter <bot|party [leader]> [trigger <id> | map <id>] [direct] [#token]`
The bots enter a dungeon through its real entrance. Per bot: the engine goes down, the bot walks into the trigger's box (`MovePoint`; the walk is abandoned after 20 s without progress, the whole entry after 120 s), and then the host calls THE CORE'S OWN area-trigger handler for the bot
(`WorldSession::HandleAreaTriggerOpcode`, MiscHandler.cpp:538: box test, corpse rule, entry rule, teleport to the instance's entrance). The entry question is asked through the one guarded function (`BotMgr::CanEnterMap`/`CheckEntry`) first.
With no `trigger`/`map` the trigger is the one the leader stands in that leads to another map; `map <id>` takes the first trigger that leads into that map. The party's LEADER (a host bot among the addressed bots) enters first; the others call the handler only after it stands on the target map (the group's
recent instance, MapManager.cpp:300, so all five land in the SAME instance); if the leader is refused or fails, they are refused (`leader-refused`). `direct` = the fallback D1: no walk and no handler, the same checks, then `Player::TeleportTo` to the entrance the instance names, else to the trigger's target.
A bot already on the target map, dead, in combat or being teleported is refused. The result of a started entry is read from `state` (`entered=trigger`, `inst=`, or `block=` with `blockmsg=`); a refusal at the door says why in the same `block=` key.
```
enter-begin #e1
enter bot=Mtank trigger=2230 map=389 status=started inbox=no direct=no #e1
enter bot=Mpriest trigger=2230 map=389 leader=Mtank status=started inbox=no direct=no #e1
enter bot=Mdpsa trigger=2230 map=389 leader=Mtank status=started inbox=no direct=no #e1
enter bot=Mdpsb trigger=2230 map=389 leader=Mtank status=started inbox=no direct=no #e1
enter bot=Mdpsc trigger=2230 map=389 leader=Mtank status=started inbox=no direct=no #e1
enter-end count=5 records=5 trigger=2230 map=389 target=party:Mtank bots=5 started=5 refused=0 #e1
```
(Live: the party stood at the game_tele `RagefireChasm` spot, 1811.78,-4410.5, a few yards outside the box (`inbox=no`); all five were on map 389, instance 1, at the entrance 0.80,-8.23 within 3 s, `entered=trigger`. The refusal line of a level-9 bot is `enter bot=Mlow trigger=2230 map=389 leader=Mtank status=refused code=level msg=level%20too%20low%20for%20this%20instance%20%28needs%2010%29`, seen in the selftest.)
Refusal codes (`code=`, also the `block=` words): `level` (a minimum level), `full`, `locked`, `difficulty` (no map difficulty for the group's setting), `not-found`, `other` (any other entry condition), `already-on-map`, `in-combat`, `dead`, `teleporting`, `leader-refused`, `bad-id`, `other-map` (the bot is not on the trigger's map), `not-a-bot`, `duplicate` (an entry runs already);
later, in `block=`: `teleport-refused` (the core ported the bot elsewhere), `timeout`, `stuck`.

### `.tb instance <leader> info [map <id>] | reset | leave [#token]`
`<leader>` is a character in the world. **info**: the instance the leader is in, else (with `map`) the one its party was last in, else one a party member stands in. `players` counts without GMs; `mobs_total` = the world database's creature spawns of the map and difficulty, `mobs_alive` = those whose creature lives, or, when it is not loaded,
that have no future respawn time (a killed creature keeps one), `mobs_loaded` = creatures the map holds now; `boss` = see below; `entrance` = the instance script's entrance point (0 = none), `recent` = the party's recent instance id for the map.
```
instance-begin #i1
instance leader=Mtank map=389 name=Ragefire%20Chasm inst=1 diff=1 players=5 gms=1 mobs_alive=121 mobs_total=132 mobs_loaded=130 boss=2732:alive;2733:alive;2734:alive;2735:alive entrance=1102230 recent=1 owner=Mtank #i1
instance-end count=1 records=1 verb=info map=389 inst=1 #i1
```
Real leave and reset answers:
```
instance-begin #l1
leave bot=Mcmd map=1 status=started #l1
leave bot=Mdpsa map=1 status=started #l1
leave bot=Mdpsb map=1 status=started #l1
leave bot=Mdpsc map=1 status=started #l1
leave bot=Mpriest map=1 status=started #l1
leave bot=Mtank map=1 status=started #l1
instance-end count=6 records=6 verb=leave map=389 inst=1 bots=6 started=6 refused=0 #l1
instance-begin #rs1
reset map=389 inst=1 status=ok #rs1
instance-end count=1 records=1 verb=reset leader=Mtank instances=1 ok=1 failed=0 #rs1
```
`boss` is `<DungeonEncounter id>:<alive|dead>` for every boss of the instance script's own table (`InstanceScript::GetBossDungeonEncounter`, dead = boss state DONE; 2732 Oggleflint, 2733 Taragaman, 2734 Jergosh, 2735 Bazzalan): the creature-template flag the first version scanned for does not exist at run time (the core strips CREATURE_FLAG_EXTRA_DUNGEON_BOSS from the database value). **leave**: every host bot on that instance (the party and the commander) is teleported to the exit trigger's target (`GetGoBackTrigger`); a DEAD bot is refused (`code=dead`: its body stays in the dungeon until it is raised or released inside; moved out, its engine would release it at a graveyard outside, which for an Alliance bot is Razor Hill, a ghost until `.tb revive`); records `leave bot= map= status=started|refused [code= msg=]`, end `verb=leave ... bots= started= refused=`. **reset**: follows the core's rules
(`HandleResetInstancesOpcode` MiscHandler.cpp:985, `Group::ResetInstances` Group.cpp:1348, `InstanceMap::Reset` Map.cpp:3182) and refuses, before the core is called, with `leader-inside` (the core ignores a reset sent from an instance), `no-party`, `not-leader`, `lfg-group`, `target-not-found` (the party owns no instance), `not-empty` (anyone, a body or a GM,
is on the map) or `locked` (a boss was killed: the lock stays); otherwise the core is called and the outcome is READ: a successful reset makes the group forget the instance, so records `reset map= inst= status=ok|failed`, end `verb=reset leader= instances= ok= failed=`. After an `ok`, the next entry opens a FRESH instance (a new map object: every creature alive again, a new script); its `inst` can be the same number, because the core hands out the lowest free id again (`MapManager::GenerateInstanceId`, `_freeInstanceIds`) once the reset instance is unloaded.

### `.tb tele <character> to <character> [revive]` and `.tb tele <bot|party [leader]> <map> <x> <y> <z> [o] [revive] [#token]`
Test setup (`tag=SETUP`; `tag=PRACTICE` with `revive`). Refused: `bad-target` (the same character), `teleporting`, `in-combat` (either), `dead` (the target, or the source without `revive`), `not-allowed` (not a host bot / the sender's own character), `already-on-map` (the source stands in ANOTHER instance of the target's map:
the core aborts an entry check for a player on that map, so it is refused first), `not-in-group` (into an instance: a non-GM must be in the target's party; a GM may join any instance), the entry refusals of `.tb enter`, `teleport-refused`. Into an instance the source joins the TARGET's instance (the same instance id), through the public
`Player::TeleportTo(location, options, instanceId)`; that is how the commander (GM, not in the party) enters the tank's instance. With `revive` a dead source is raised at the teleport (`TELE_REVIVE_AT_TELEPORT`) and marked `practice`. `started` means the core began the teleport; where it ended is in the bot's `entered=`/`block=`.
```
tele-begin #t3
tele bot=Mcmd to=Mtank status=started map=389 inst=1 tag=SETUP #t3
tele-end count=1 records=1 target=Mcmd bots=1 started=1 refused=0 #t3
tele-begin #tx
tele bot=Mcmd to=Mtank status=refused code=already-on-map msg=Mcmd%20stands%20in%20another%20instance%20of%20that%20map%3A%20it%20must%20leave%20it%20first%20%28the%20core%20aborts%20an%20entry%20check%20for%20a%20player%20on%20that%20map%29 #tx
tele-end count=1 records=1 target=Mcmd bots=1 started=0 refused=1 #tx
```

### `.tb revive <bot|party [leader]> [#token]`   (PRACTICE)
The driver's shortcut: a dead bot is raised in place with full health (`ResurrectPlayer(1.0f)`, corpse bones removed). Every line carries `tag=PRACTICE`, a log line says PRACTICE, and the bot's `state` says `practice=yes` until the next combat ends. A bot that is alive is refused (`not-dead`).
```
revive-begin #rv1
revive bot=Mdpsb tag=PRACTICE status=revived practice=yes #rv1
revive-end count=1 records=1 tag=PRACTICE target=Mdpsb bots=1 revived=1 refused=0 #rv1
```

### `.tb kill <bot|party [leader]>` and `.tb gm <character> on|off`
`kill` (as `.die`; `dead` when it is already dead) and `gm` (`Player::SetGameMaster`; a GM passes the instance's player cap and owning-group tests, takes no seat, and its instances are always valid) are test setup: records `kill bot= tag=SETUP status=killed|refused`, `gm bot= tag=SETUP status=ok|refused gm=yes|no`; ends `kill ... killed= refused=` and `gm target= gm=`. Real:
```
kill-begin #kw
kill bot=Mtank tag=SETUP status=killed #kw
...
kill-end count=5 records=5 target=party:Mtank bots=5 killed=5 refused=0 #kw
gm-begin #gm1
gm bot=Mcmd tag=SETUP status=ok gm=yes #gm1
gm-end count=1 records=1 target=Mcmd gm=yes #gm1
```

### Death and the instance's entrance point (decision D2)
`.tb order <bot> release` makes the dead bot release its spirit. Inside a non-raid dungeon the core revives a released player at once (`Player::RepopAtGraveyard`, Player.cpp:5211-5262), at the instance script's entrance point when it has one (:5227-5228), else at the closest graveyard, which for Ragefire Chasm and an Alliance bot is
Razor Hill, OUTSIDE. The host therefore gives every dungeon instance an entrance (`InstanceScript::SetEntranceLocation`, public, InstanceScript.h:289) as soon as a host bot is on it: the target of the trigger the first entry used, else the first trigger that leads into the map (`ObjectMgr::GetMapEntranceTrigger`). A dungeon whose instance has NO script
or no trigger leading into it gets none, and then a release is HELD (`relhold=no-entrance`, `.tb order release` is refused with `no-entrance`): the bot waits at its body, never a graveyard revive outside.
Commanded bots (`.tb master`) answer the engine's lease question as `Dungeon` (the legacy dungeon-crew rule of `RunReleaseHold::Decide`): on an instance map a dead bot waits while a living priest, paladin, shaman or druid of its party is on the map or a member fights (`relhold=rezzer`/`combat`, `life=awaiting-res`), and releases only on a wipe (`relhold=wipe`);
a resurrection offered by a party member or the commander is accepted at once (`accept resurrect` with the offerer as the event's object; a stranger's offer is not answered); `.tb state` reports `resreq=`. After a release the bot is `life=running-back` until it is within 20 yd of its commander (or leader). No host teleport ever moves a dead bot or a commanded bot into or inside an instance (the open-world catch-up is unchanged and counted in `catchup=`).

## 9. Slice 5: dungeon plans and the run header (package H5)

`.tb plan` reads the dungeon plan tables the module loads at boot (`ai_dungeon_plan`, `_leg`, `_step`, `_leg_need`, `_boss`, `ai_boss_tactic` and the ForeverCore-only `ai_dungeon_step_unit`, SQL in `forever/sql/world`, delivery in `forever/sql/README.md`).
It follows the grammar of section 3: markers `plan-begin` / `plan-end` (addendum A2: named after the COMMAND, the verb is the end line's `verb=`), `count=`/`records=`, `error` on one line, the token last.
**The sample lines below are REAL output of the module on ForeverCore (package M5, build VM test server, 2026-11-19, the Ragefire Chasm rows exported by forever-agent 63fc3f78: 9 legs, 80 steps, 103 unit rows).** `.tb grammar selftest` also prints one `plan`, `leg`, `unit`, `unit` (optional), `run` and `event` record built by the real writers (fixed example values).
In the default build (option OFF) `.tb plan` answers `error code=engine-not-built`.

`list`, `info` and `check` are read only; `reload` re-reads the plan tables into the module's memory (no bot, world or database row changes). Errors (`code=`): `missing-argument`, `missing-verb`, `unknown-verb`, `unexpected-argument` (words), `not-loaded` (the plan table was never loaded or sealed: the loader did not run, the module is off, or the plan loader is not merged),
`no-plan` (the name is not a loaded plan), `no-bot` (`check`: the character is not in the world), `not-allowed` (`check`: the character is neither a host bot nor the sender's own: `MayLeadBots`), `wrong-map` (`check`: the character is not on the plan's map), `no-units` (`check`: the plan has no step unit rows).

### `.tb plan list [#token]`
One `plan` record per loaded plan (by name): `plan= map= legs= steps=` and `units=` (the unit rows kept for it; absent when the unit rows were not read). End: `verb=list plans=`.
```
plan-begin #p1
plan plan=ragefire_chasm map=389 legs=9 steps=80 units=103 #p1
plan-end count=1 records=1 verb=list plans=1 #p1
```

### `.tb plan <plan> info [#token]`
`plan plan= map= legs= steps= moves= pulls= bosses= [units=] cc_priority= kill_first= [source=]`: `moves` = move and move_direct steps, `pulls`, `bosses` = boss steps; `cc_priority`/`kill_first` are lists of creature entries; `source` is the plan row's `source` column (the loader does not keep it, the command reads it again; absent when that read failed).
Then one `leg leg= name= steps=` per leg a leg row or a step names. End: `verb=info plan= skipped=`: `skipped` = the plan's rows in the database (steps, legs, leg needs) that the loaded table does not hold, i.e. what the loader refused (the server log names each); absent when a count could not be read (unknown is not zero).
```
plan-begin #p2
plan plan=ragefire_chasm map=389 legs=9 steps=80 moves=25 pulls=51 bosses=4 units=103 cc_priority=11319;11324;11322 kill_first=11319 source=forever-agent%2063fc3f78%20dungeons%2Fragefire_chasm.json #p2
leg leg=0 name=tunnel steps=6 #p2
leg leg=1 name=tunnel_end steps=4 #p2
(... seven more leg records: trogg_cave 12, oggleflint 4, trogg_west 17, molten_cave 9, taragaman 7, jergosh 10, bazzalan 11 ...)
plan-end count=10 records=10 verb=info plan=ragefire_chasm skipped=0 #p2
```

### `.tb plan <plan> check <character> [#token]`
`<character>` is a host bot (or the sender's own character) standing on the plan's map, usually after `.tb enter`. For every unit row of the plan the check finds the creature SPAWN of that entry on the character's map (the instance's difficulty) nearest to the row's position, within 5 yd (`result=matched`), else within 15 yd (`result=near`), else `result=missing`;
one-to-one (greedy by distance: every pair within 15 yd, nearest first, a row and a spawn each used once; ties by leg, seq, idx, then spawn id). The distance is the 3D distance between the row and the spawn point of the creature data (`CreatureData::spawnPoint`), so it does not depend on which creatures are loaded;
`alive` says whether the creature of that spawn lives now (its loaded creature is alive, or, when it is not loaded, the map holds no future respawn time for it: a fresh instance says `yes` for all).
`yd=`, `spawn=` (the ForeverCore spawn id) and `alive=` are written for matched and near only; a missing unit carries none of them (nothing is known), and `optional=yes` when its entry has NO spawn at all on the plan's map (decision D4: a summoned pet such as entry 8996 is optional; a step does not need it).
A step is `ok` when every non-optional member of it is matched or near and at least one member was found; `steps_short` counts the steps with unit rows that are not ok. End: `verb=check plan= bot= map= inst= units= matched= near= missing= optional= steps= steps_ok= steps_short=` (`optional` counts the missing units that are optional; they are also in `missing`).
A `role=patrol` row that finds no free spawn takes the spawn a row of its own step (same leg and seq, same entry) already holds, when that spawn stands within 15 yd of it: the exporter writes the patrolling pack member twice (the pack row and the patrol row at the first path point), and they are one creature (merge package M5). The match is CACHED per (map, instance id, plan): it is what the run glue's pack lookup (S6) reads; the command recomputes it each time. The spec named the end line `check-end`; addendum A2 (markers named after the command) wins, so it is `plan-end ... verb=check`.
```
plan-begin #c1
unit leg=2 seq=2 idx=0 entry=11318 role=pack result=matched yd=0.00 spawn=20048648 alive=yes #c1
unit leg=2 seq=2 idx=1 entry=11318 role=patrol result=matched yd=0.05 spawn=20048648 alive=yes #c1
unit leg=4 seq=16 idx=0 entry=11320 role=pack result=near yd=8.61 spawn=20048642 alive=yes #c1
unit leg=6 seq=4 idx=2 entry=8996 role=pack result=missing optional=yes #c1
(... 99 more unit records ...)
plan-end count=103 records=103 verb=check plan=ragefire_chasm bot=Mtank map=389 inst=3 units=103 matched=95 near=1 missing=7 optional=7 steps=55 steps_ok=55 steps_short=0 #c1
```

### `.tb plan reload [#token]`
Reads the plan tables again through the loader (`ReloadDungeonPlans`) with every running run's plan pointer as `inUse`, and swaps them in only when every row passes (the loader's own rules). When the swap happened the unit rows are read again against the new table and the cached matches are dropped. No record lines: the end line says it. End: `verb=reload swapped=yes|no`, `why=` exactly when `swapped=no` (the loader's reason, free text), `changes=` (when swapped: how many plan changes the loader listed), `plans= steps=` of the table now in use, and `units= units_refused=` (the last unit-row read; absent when it was not read).
The same all-or-nothing reload also runs by itself after a successful `reload all` (in game, console or remote), so a plan edited in the database is in use without the extra command: core pin d02645dd21 has the `OnAfterReload` hook (#374) and the module answers it. The outcome goes to the server log as one `TortoiseBots:` line (`reloaded <n> dungeon plans ...` or `the dungeon plans kept after reload all (<reason>)`); no other core `.reload` command touches module data, so none of them reloads anything here.
```
plan-begin #p4
plan-end count=0 records=0 verb=reload swapped=yes changes=0 plans=1 steps=80 units=103 units_refused=0 #r1
```
```
plan-begin #r2
plan-end count=0 records=0 verb=reload swapped=no why=0%20plan%2C%200%20leg%2C%201%20step%2C%200%20boss%20and%200%20boss%20tactic%20rows%20refused%2C%200%20plans%20with%20bad%20unit%20lists%3B%20ai_dungeon_step%20%27rfc_broken%27%20leg%200%20seq%201%20%28pull%29%3A%20BadEntries%20%28the%20server%20log%20names%20every%20row%29 plans=1 steps=80 units=103 units_refused=0 #r2
(a copy of the plan under another name with one pull step's entries set to `abc`; the old tables stay)
```

Error examples:
```
error code=not-loaded cmd=plan msg=the%20dungeon%20plans%20are%20not%20loaded%20%28the%20plan%20loader%20has%20not%20run%2C%20or%20the%20module%20is%20off%29 #p9
error code=no-plan cmd=plan msg=nope%20is%20not%20a%20loaded%20dungeon%20plan%20%28.tb%20plan%20list%29 #p9
error code=wrong-map cmd=plan msg=Mtank%20is%20not%20on%20map%20389%20%28the%20plan%27s%20map%29 #p9
error code=no-units cmd=plan msg=the%20plan%20has%20no%20ai_dungeon_step_unit%20rows%20%28a%20plan%20exported%20without%20--forever%2C%20or%20the%20rows%20were%20not%20read%29 #p9
```

### The run header (`.tb run`, section 10)
`forever/src/host/run/TbRun.h`: one `RunRec` per run, keyed by the leader's guid (leader, plan, the Turtle `DungeonRun::Run` as the progress, `startedMs`/`endedMs`, `pauses`, `deaths`, `wipes`, the event ring of 1024, `stopped`/`stopReason`, the known boss states). **Nothing created or ticked a run in S5**; the S6 glue (section 10) does, and writes these records:
`run lead= plan= run= state=running|paused|done|stopped step= steps= [leg= legname= kind=] phase= note= pull= pulls= boss= deaths= member_deaths= wipes= pauses= elapsed= [why=] [ev=]`
* `run` = the run's id, monotonic per record (`Create` assigns it; a stop and a new start of the same leader get different ids). `.tb run decisions` takes it in place of the leader's name.
* `step` = the index of the step the run is on (`steps` once it is past the last one); `leg`, `legname` and `kind` describe that step and are absent past the last step. `phase` is the Turtle run's phase (`begin gather walk pull task rest wait hold`), `note` what it does now or why it paused (free text, cut to 400 bytes, then `trunc=yes`).
* `pull` = the plan's pull steps the run has passed that it did not record as skipped, deferred or given up (its own lists); `pulls` = the plan's pull steps. `boss` = `<entry>:alive|dead` for each plan boss the glue has READ; a boss it never read is not listed (a boss is never reported alive by default).
* `elapsed` seconds since the start, frozen at the end of the run; `why` exactly when the run is paused (the run's own note) or stopped (the reason it was given); `ev` = the seq of the newest event, absent when there is none.
* **The counters.** `deaths` and `member_deaths` are the same total: every member death edge (a member going from alive to dead; bots and real players of the party), counted by the run's 1 Hz read of the party. `member_deaths` is the explicit label, `deaths` is kept for existing readers. `wipes` counts a whole-party wipe once each time (every member dead at the same read), not the number of dead bots. `pauses` counts the run entering `paused` (each pause, whatever paused it). The done note's own words are narrower: its `wipes` is the policy's wipe counter, its `near wipes` counts `kWipeDead` or more dead with someone still alive (once per episode; the near-wipe stop's window is `AiPlayerbot.DungeonRunNearWipeStopMinutes`), and its `lead deaths` counts the lead bot's deaths only. So `deaths=15 wipes=1 ... note=plan done (1 wipe, 4 near wipes, 4 lead deaths)` is consistent: 15 member deaths included the lead's 4, one whole-party wipe, and 4 episodes that came close without wiping.
* `event seq= t= kind= [step=] [leg=] [unit=] why=`: `t` seconds since the start, `kind` = start step pull fight-end rest death wipe revive pause resume done stop, `unit` a creature entry; `step`, `leg` and `unit` are absent when the event has none.
* `.tb state`: the end line of a party whose leader has a run gains `run=<lead name> runstate=<state>` (addendum A8 allows new keys; absent when there is no run).

## 10. Slice 6: the tank bot leads a dungeon run (package C6)

`.tb run` starts, watches and stops a run of the Turtle dungeon-run policy (`runtime/DungeonRunPolicy.h`, unchanged) on a plan of section 9. The **tank bot is the run's leader** (decision D1, a "system run"): `start` binds every other member of the tank's party to the tank as its commander (`.tb master` binding, the same host calls), and from then on nobody orders a bot from outside. Once a second the module reads the party and the step (`ReadFacts`, package F6), asks the policy for its one order, and hands that order to the executor (package O6: `move`, `pull`, `rest`, `revive party` ...). The fights stay the bots' own combat AI. All replies follow the grammar of section 3: markers `run-begin` / `run-end` (the verb is the end line's `verb=`), `error` on one line, the token last. In the default build (option OFF) every `.tb run` form answers `error code=engine-not-built`.

```
.tb run start <leader> <plan> [from step <N> | from leg <number|name>] [lead=player] [#token]
.tb run stop <leader> [reason words] [#token]
.tb run pause <leader> [#token]
.tb run resume <leader> [#token]
.tb run status <leader|all> [#token]
.tb run events <leader> [since=<seq>] [#token]
.tb run decisions <leader|run> [since=<seq>] [limit=<n>] [#token]
```

**`start`** first commits the named tank's role (the same call `.tb role <leader> tank` makes: stored role, tank kit, `follow`; skipped for a class that cannot lead a run, whose refusal the check below names) and then checks, in this order, changing nothing else when it refuses: the leader is a host bot in the world (`not-bot`); the plan is loaded (`not-loaded`, `no-plan`); the executor's own check (`no-engine`, `not-leader`, `no-tank`: the leader leads its party, has the tank role (just committed; the facts' `leadIsTank` reads it), is a warrior tank or a bear druid and its combat kit carries `pull` (a shaman or a protection paladin tank order is refused `no-tank`: the role bit alone is not a tank kit; a member with a tank bit on a class that has no tank kit is read as damage, not as a tank) and every member is a host bot with a running engine); the party is not a raid (`group-raid`); no live run of this leader (`already-running`; an ended run is replaced); leader and members stand in the same instance of the plan's map (`wrong-map`); every member is a host bot in the world (`not-bot`, `player-in-group`; an offline member left by an old group is named with `.tb ungroup <member>` as the way out); **every other member is already commanded** (`not-commanded`: bind the party first, `.tb master party <leader> <commander>`; a run rebuilds only the tank's kit, through the role commit above); the host accepts the binding (`bind-failed`, everything given back). The reply is one `run` record (`state=running`) and `run-end verb=start`. The server log names which bot the AI treats as the run's tank: `TortoiseBots run: tank <name> (role already tank|set by run start, AI IsTank yes|no)`.

**`start ... from step <N>` / `from leg <L>`** (WC run 9) is a placed start, for a short live proof at a failing leg: the run begins at that step (`N` is the number `.tb run status` shows as `step=`, counted from 0, so a failing `step=82` restarts with `from step 82`; a leg's first step; a start inside a scripted event moves back to the step that starts it), nothing before it is walked, and a walk back never goes before it. The party must already stand there (`.tb tele party <leader> <map> <x> <y> <z> <o>` to the step's point, then the usual rest). A step or leg the plan lacks is refused with `no-step`.

**`start ... lead=player`** (issue #139, opt-in; the default stays the bot-led run above) starts a **player-led run**: the player who types it leads his own party and walks, `<leader>` is the tank bot that pulls (every other verb names that bot as before), nothing is rebound (the bots keep the player as their commander and follow him), and the run pulls the plan's packs and bosses near him, in the plan's order, never ordering a walk. A placed start (`from step` / `from leg`) is bot-led only: `lead=player` with those words is refused `unexpected-argument`. The refusals, the behaviour and the limits are in `forever/docs/RUN-KNOBS-139-NOTES.md` (Part 2); the master switch is `AiPlayerbot.DungeonRunPlayerLed`. The run record carries `mode=player commander=<name>`; a player's own orders never pause it, and the end of the run sends `follow` instead of `stay` (the end line's `stayed=` counts the bots that took it).

**`stop`** is the run's end for good: out of combat the executor's `stay` goes to the leader and every member, the old commander of each member is restored (`master`/`commander` as they were before `start`, unless somebody rebound the bot meanwhile), the run is `state=stopped` with `why=` the reason (default `stopped: by .tb run stop`), and the end line says `pending=no stayed=<n>` (`n` = bots whose `stay` action ran). **A stop never interrupts a fight:** with any living member or pet in combat the reply is `pending=yes` (no `stayed=`), the run gives no new order from then on, and the stop takes effect, with the same `stay` and release, on the first second without combat (`.tb run status` then shows `state=stopped`). A repeated `stop` while one is pending answers `pending=yes` again. Errors: `not-running` (already ended).

**`pause`** pauses a running run at once (`state=paused why=paused by .tb run pause`): it is a player's pause, so the run never resumes by itself from it. The binding stands meanwhile: a member the run bound to the tank keeps it through the pause and the regroup (every second the run reads the binding table as stored and puts back both what it repairs: a host master the engine's own master logic moved off the run's tank is bound to it again, and a commander entry it dropped (or that lapsed) is written again; one second of repair makes the next one a no-op; a commander somebody else set with `.tb master` stays theirs, and a member waiting out its master's grace is not re-bound). **`resume`** is the policy's own checked resume (`ResumeChecked`): the party regroups and goes on. It refuses `not-paused`, `stopping`, `player-in-group` (a real player is in the group; a system run waits until it is bots only again) and `refused` (the policy's reason, e.g. the leader is still in another part of the instance). Errors for both: `not-running`.

**`status <leader>`** is one `run` record; **`status all`** one per run (by leader name, ended runs included until the leader starts another), and its end line adds the tick cost: `runs= ticks= tick_avg_us= tick_max_us=` (run ticks since boot) and `facts_reads= facts_avg_us= facts_max_us= facts_unreadable=`.
**`decisions <leader|run> [since=<seq>] [limit=<n>]`** (#307): the read path of the decision capture (`runtime/DecisionCaptureRows.h`, `docs/server/decision-capture.md`): the newest 512 decision rows of the run's LIVE capture, oldest first, between the usual markers. The first line is `decisions run= lead= plan= capture= count= since= next= more=` (`more=yes` while a newer row waits; `next=` is the cursor to pass as `since=` and is unchanged when no row came back). Each row is one `decision` line, never split over continuation lines: `decision seq= t= dt= state= phase= step= record= order= note= say= [trunc=yes]` — `seq` is the capture's tick number (the cursor), `t` Unix milliseconds, `state running|paused|done`, `phase` the Turtle phase words, `step` from 0, `record=tick|stub|dropped` says what became of the row's capture record (`dropped`: the file has no record for this tick), `order` what the tick ordered, `note`/`say` the run's text after the tick. Free text over 240 bytes is cut and the row says `trunc=yes` (the full text is in the capture file); a tick the file recorded as a `stub` still has its row. `limit=` defaults to 50 and clamps to 1..200. Access is `CheckRunObserverAccess` (the console, a GM, the run's owner or observer), checked before the toggle: off (`AiPlayerbot.DecisionCapture.Enabled = 0`) answers `capture-off` naming it; `no-capture` means a player-led run (not captured) or a stopped one (its rows are in the folder). The row format is a stable interface (field table in `docs/play/dungeon-run-leader.md`).
**`events <leader> [since=<seq>]`**: the ring of the last 1024 events with `seq` above `since` (default all), oldest first, one `event` record each (section 9 lists the keys); the end line has `lead=`, `oldest=` (the oldest seq the ring still holds, absent with none) and `last=` (the newest seq, absent with none), so a watchdog asks `since=<last>` and sees "no new event for N minutes", and a reader whose `since` is below `oldest` knows the ring dropped events instead of there being a gap. Kinds: `start`, `step` (the run moved to a step; `why=not-cleared: ...` when a pull step none of whose members matched a spawn was skipped), `pull`, `fight-end`, `rest`, `death` (`why=` the bot), `wipe` (every member dead), `revive`, `pause` (`why=` a code or the run's note: `outside-order`, `player-in-group`, `lead-off-map`, `command`, or the policy's own reason), `resume`, `done`, `stop`. Errors: `no-run` (no run of that leader), `bad-argument`, `unexpected-argument`, `missing-argument`, `unknown-verb`.

**Pauses that end by themselves:** the leader off the plan's map or not in the world (`why=lead-off-map`, back for a few seconds: resumes), a fight that held the run past the policy's ceiling (resumes when the party is out of combat). **Pauses that wait for `resume`:** `pause`, a refused order (`why` names the order and the executor's answer; an order of the policy the module cannot carry out, such as `summon` (decision D3), `escape`, `rescue`, `fetch`, `return`, `rejoin`, `cc`, `loot`, `open`, `use`, `ritual`, `gossip`, is refused `unsupported` and pauses the run with that verb in the note), **a `.tb order` typed for a bot of the run** (`why=outside-order`: the run pauses at its next second, the note names the bot and the verb, and the order itself is carried out as typed) and a real player joining the group (`why=player-in-group`, resumes by itself when the group is bots only again).
The run ends by itself when the plan is done (`state=done`) or at `AiPlayerbot.DungeonRunMaxMinutes` (a stop with that reason).
A leader that does not lead the party any more (disbanded, handed over) ends the run: `state=stopped`.
`state-end` of `.tb state` carries `run=<leader name> runstate=<state>` for a party that has a run (section 9).
The server log gets one line per event: `TortoiseBots run: <leader> #<seq> <kind> step=<n> leg=<n> unit=<entry> <why>`.

```
run-begin #r1
run lead=Mtank plan=ragefire_chasm state=running step=0 steps=84 leg=0 legname=tunnel kind=move phase=begin note=started pull=0 pulls=51 boss= deaths=0 wipes=0 pauses=0 elapsed=0 ev=1 #r1
run-end count=1 records=1 verb=start #r1
```
```
run-begin #r2
run lead=Mtank plan=ragefire_chasm state=stopped step=17 steps=84 leg=2 legname=cave kind=pull phase=wait note=stopped%3A%20by%20.tb%20run%20stop pull=6 pulls=51 boss= deaths=0 wipes=0 pauses=1 elapsed=312 why=stopped%3A%20by%20.tb%20run%20stop ev=31 #r2
run-end count=1 records=1 verb=stop pending=no stayed=5 #r2
```
```
run-begin #r3
event seq=30 t=309 kind=fight-end step=17 leg=2 why= #r3
event seq=31 t=312 kind=stop step=17 leg=2 why=stopped%3A%20by%20.tb%20run%20stop #r3
run-end count=2 records=2 verb=events lead=Mtank oldest=30 last=31 #r3
```
```
error code=not-commanded cmd=run msg=Mdpsb%20has%20no%20commander%20yet%3A%20bind%20the%20party%20first%20%28.tb%20master%20party%20Mtank%20%3Ccommander%3E%29 #r4
```
**The samples above were built by hand from the encoder's rules, as section 9's were; the first boot replaces them with real output.**

## 11. Commander port, package P1: the binding card

`.tb commander status [<commander>] [#token]` shows every commander card (or the one commander's). A card is read from h3's table (the bots `.tb master` bound, section 4) and from the run table (section 10); the module keeps beside them only when a card was first seen, the lead `.tb master party <leader> <commander>` named, the flags the group gives the binding (`inparty`, `raid`, `playerled` = the commander himself holds the party's lead; read again with the group), and the log of the commander's last orders. Nothing here binds or releases: that stays `.tb master`, the party events of section 4 and the two graces. `group=` is the lead's live group counter, read again on every card (a group re-created around the lead keeps the binding); no group number is stored anywhere.

* **One card per commander.** The bots bound to one commander are one card wherever they stand; two commanders whose bots share a party are two cards.
* **One card per run.** A run shows once. When nobody commands the run's tank bot the card is the run's own binding (`system=yes`, the tank is the `cmd=`); when the tank was commanded before the run (`.tb master party Mtank Mcmd`), the card is that commander's and the run's members are folded into it. The `run` record of `.tb run status` follows the card's own lines.
* **Records.** `binding cmd= cguid= online= lead= [group=] bound= bots= system= inparty= raid= playerled= boundat= orders= [map= pos=]`: `bound` counts the guids bound, `bots` the ones in the world, `group` is the lead's party counter of this session (opaque: it restarts at 1 and is never stored), `playerled` means the commander of an in-party binding holds the party's lead, `boundat` unix seconds. `member cmd= bot= guid= class= role= level= hp= maxhp= power= maxpower= ptype= alive= lead= map= zone= pos= [sub=]` (the lead first; `sub` is the raid subgroup). `order cmd= at= verb= detail= ok= result=` (oldest first, at most 8; `ok=yes` means queued, `result` is `queued N of M[, refused K]` or `<code>: <message>`; the run's own orders are logged too, an exact repeat of the last one is not).
* **Errors.** `missing-verb`, `unknown-verb`, `unexpected-argument`, `no-binding` (the named commander commands no bots), `engine-not-built`.
* **The dashboard.** The COMMANDERS datagram for these cards is built by the Turtle builder (`runtime/CommanderTelemetry.h`) from `cmd::AppendBindings` (forever/src/host/cmd/TbCmdBinding.h); the dash-runs collector calls it (forever/docs/CMD-P1-NOTES.md). The datagram carries at most the Turtle limit of twenty bindings; the command's list does not.

Real output (run2 boot, five bots bound by `.tb master party Mtank Mcmd`, engines on, a run in RFC before and after; the last three member records are cut):
```
commander-begin #c12
binding cmd=Mcmd cguid=15 online=yes lead=Mtank group=1 bound=5 bots=5 system=no inparty=no raid=no playerled=no boundat=1795138919 orders=0 map=389 pos=0.80,-8.23,-15.53 #c12
member cmd=Mcmd bot=Mtank guid=10 class=warrior role=tank level=18 hp=498 maxhp=498 power=54 maxpower=100 ptype=rage alive=yes lead=yes map=389 zone=2437 pos=-18.24,-46.69,-21.85 #c12
member cmd=Mcmd bot=Mpriest guid=11 class=priest role=healer level=18 hp=407 maxhp=407 power=80 maxpower=764 ptype=mana alive=yes lead=no map=389 zone=2437 pos=3.03,-31.74,-19.26 #c12
(... three more member records ...)
commander-end count=6 records=6 bindings=1 #c12
```
```
commander-begin #c7
binding cmd=Mcmd cguid=15 online=yes lead=Mtank group=1 bound=5 bots=5 system=no inparty=no raid=no playerled=no boundat=1795138681 orders=5 map=389 pos=0.80,-8.23,-15.53 #c7
(... five member records ...)
order cmd=Mcmd at=1795138725 verb=stay detail= ok=yes result=queued%205%20of%205 #c7
order cmd=Mcmd at=1795138737 verb=move detail=0.8%20-8.2%20-15.5 ok=yes result=party%205 #c7
order cmd=Mcmd at=1795138741 verb=pullback detail=5 ok=yes result=bot%3AMtank%201 #c7
order cmd=Mcmd at=1795138748 verb=focus detail=5 ok=yes result=party%205 #c7
order cmd=Mcmd at=1795138762 verb=attack detail=5%20Mtank ok=yes result=bot%3AMtank%201 #c7
run lead=Mtank plan=ragefire_chasm state=running step=1 steps=80 leg=0 legname=tunnel kind=pull phase=pull note=fighting pull=0 pulls=51 boss= deaths=0 wipes=0 pauses=0 elapsed=39 ev=3 cmd=Mcmd #c7
commander-end count=12 records=12 bindings=1 #c7
```
One card while the run goes: the run's own orders (`move`, `pullback`, `focus`, `attack`) are in the same log. Without the engine a `stay` is logged `ok=no result=no-engine`. `.tb master party Mtank none` ends the card (`commander-begin` / `commander-end count=0 bindings=0`); `status Nobody` answers `error code=no-binding cmd=commander msg=Nobody%20commands%20no%20bots`.
A run whose tank nobody commands is a system card: `binding cmd=Mtank ... system=yes inparty=no`, the run record after it.

## 12. Slice 9: doors, chests, loot and the bags (packages O9, F9, C9)

The run leader's `open`, `loot` and `lootmethod` orders are carried out now (`forever/src/host/run/TbRunTasks.*`, package O9); before slice 9 they were refused `unsupported`. Nothing new is typed by hand: `.tb order` is unchanged and the verbs are reachable through the run only.

* **`open entry:<id>@x,y,z <bot>`** (a door, a lever, a goober, a chest): the object is the spawned game object of that entry nearest the plan's point **within 5 yd of it** (never another object of the entry: none refuses `invalid-target`); the bot walks to a stand 1.5 yd short of it (the executor's `come`), then a door or a lever is used by the typed `GameObjUse` packet and a chest by the engine's own `open loot` (the opening spell, a key the bot carries). The host judges the lock row first with the Turtle judge (`no-item`, `locked`) and refuses what the core's `Use` would let through. **A door that is not at rest (state != ready) is never used again** (the core's `Use` toggles a door): the answer is `done` with `already open (state N): not used again`. Success = the object changed state within 4 s (a chest: its loot window closed within 8 s of the cast); otherwise `failed: The object showed no change after the bot used it.`
* **`loot <handle | entry:<id>@x,y,z> <bot> [item]`** (a corpse by the handle `ReadFacts` gave, a chest by entry): the engine's loot window with the ledger watching it. `failed` texts carry the words the policy moves on: `nothing this bot may loot` (another member's turn, already looted, not tapped) and `Nothing is left to loot`.
* **`lootmethod <ffa|group|rr|master|nbg> [threshold 2-5]`**: the lead bot, the group's leader, sets it through the handler a client's window uses and the answer reads the method back from the group. The bots vote in group loot and need-before-greed and stay silent in free-for-all, round robin and master loot (no roll window there; the human master distributes); a threshold below uncommon is refused `invalid` (the core's own rule).
* **`use <item> <bot>`** is parsed and refused `unsupported` (the item-use path is not ported; no plan of the two dungeons has a use_item row).
* One task per bot; a second order for the same bot is refused `busy`, an order in a fight `in-combat` (the policy sends both again). The task's result is not the order's answer: the order is accepted at once and the result reaches the policy as the facts' `taskReply` / `taskText` (`done` payloads: `opened`, `already open ...`, `<item>:<n>,copper:<n>`; failures `<code>: <message>`).

**Facts (F9).** `wait_object` reads the object's state (plan words: 0 ready = the core's `GO_STATE_READY`, 1 active = `GO_STATE_ACTIVE`, 2 = `GO_STATE_DESTROYED`: the Iron Clad Door after the cannon) with the same 5 yd search; `wait_clear` lists the living hostile creatures within the step's radius; loot / open steps read the carrier (the lead bot, or the bot holding the item), its bags, its task and the answer; the group's loot method (the core's personal loot reads as `none`); the instance's doors from the area trigger table.

**`.tb run start`** gains two refusals, checked with the others and changing nothing: **`bags`** (a member has fewer than 12 free bag slots) and **`loot-method`** (the party's loot method is not group loot or need-before-greed). A loot step for an item whose carrier has two or fewer free slots **pauses** (`why=bags-full: ...`) instead of looting and finding the item missing.

**`run` record keys** (absent when the step has none): `carrier=<bot>` (a loot / open / use_item step), `item=<id>:held|missing` (the carrier or a party bot holds the step's item), `obj=<entry>:<ready|active|active_alt|none>` (a wait_object step). **`event` kinds** added: `open` and `loot` (a task ended: `why=done: <payload>` or `failed: <code>: <message>`, `unit` = the object's entry), `object` (a wait_object step ended on its state: `why=<entry>:<state>`), `clear` (a wait_clear ended: no hostile within the radius), `item` (a loot or open step ended with the item in a bag: `why=item <id> is in <bot>'s bags`).

**`.tb state`**: every bot record gains `bagfree=<free inventory slots>` and `money=<copper>`; the end line gains `lootmethod=<group|ffa|rr|master|nbg|none>` and `lootthr=<quality number>` (absent for a party with no group).

## 13. Slice 11: pets (package P11; the hunter and the warlock)

### `.tb pet <bot> give|call|dismiss|show [#token]` and `.tb pet <bot> order <words> [#token]`
One host bot (a name; `party` is refused `bad-target`). Every reply is `pet-begin`, one `pet` record, `pet-end`, read back from the world after the verb (never what was asked).

* `give`: a hunter of level 10 or more with no pet out and none in an active slot of its stable gets its first pet, a tameable beast picked for its race (`forever/data/slice11/hunter-starter-pets.csv`), through the core's own tame path (`Unit::CreateTamedPetFrom`, add to the map, minion, saved as the current pet, pet bar sent), Best Friend, defensive, every active spell on its autocast default, and then its spells trained by the module's planner (`AiPlayerbot.PetAutoTrain`). A map that does not take the pet, or a pet the bot cannot read back, leaves nothing behind (no slot, no saved row, no pet number), so the next check tries again. Refusals (`error code=`): `wrong-class` `level-too-low` `not-in-world` `has-pet` `stored-pet` (Call Pet brings it back) `no-starter` `create-failed` `no-map` `not-summoned`.
* `call`: the engine casts Call Pet (a hunter with no pet out). `status=started` means the cast began; the pet is out when it ends. Errors: `wrong-class` `has-pet` `no-engine`.
* `dismiss`: a hunter's pet goes by the Dismiss Pet cast (`status=started`), a warlock's demon by the pet command (`status=ok` only when the bot has no pet any more). Errors: `no-pet` `wrong-class` `no-engine`.
* `order <autocast <spell name> | aggressive | defensive | passive | follow | stay>`: handed to the engine's chat `pet` action (the words a player types). `autocast torment` is the ordered taunt: the Voidwalker taunts and the engine's tank rule leaves it on (lead decision D4). `abandon` and `dismiss` are not orders (`bad-argument`: nothing deletes a pet by a word, decision D5). Errors: `no-pet` `bad-argument` `no-engine`.
* `show`: the record with `spells=` (every spell the pet knows) and `tp_used=` `tp_total=` (training points).

The `pet` record: `bot= verb= status=ok|started|refused [entry=<creature entry>] state=alive|dead|(empty: no pet) [hppct=] [react=passive|defensive|aggressive auto=<spell ids on autocast> [loyal=<1-6>]] [note=...] #token`, end `pet-end count= records= verb= target= ok= refused= #token`.

```
pet-begin #p1
pet bot=Mhunt verb=order status=ok entry=1196 state=alive hppct=100 react=defensive auto=3150 loyal=6 #p1
pet-end count=1 records=1 verb=order target=Mhunt ok=1 refused=0 #p1
```

### `.tb state` keys for a hunter or a warlock (after `practice=`)
`pet=<entry>:<alive|dead>:<hp pct>` (empty with no pet: the grammar has no `none` word and no 0 for "not there"; `dead` also when the body is gone and the stable row has Health 0: Call Pet refuses until the pet is revived), `petreact=<state>`, `petauto=<spell ids on autocast>`, `loyal=<level 1-6>` (a hunter's pet), `ammo=<item id>:<count>` (a hunter: the ammo slot's item and how many the bags hold; empty with no ammo set), `shards=<count>` (a warlock). Absent for other classes; `petreact` `petauto` `loyal` only with a live pet.

## 14. Commander port, packages P3 & P6: `.tb cmd`

`.tb cmd <verb> [args] [#token]` sends an order or task from the caller's character to its bound bots (`.tb master`).

### Order verbs (P3: lifted `run::Issue` judge)

Routed through the same lifted executor (`run::Issue`) that the self-led dungeon run uses, with the same path, ground, LOS, liquid, and target checks.

* **Verbs.** `move <x> <y> <z> [direct]`, `walk <bot> <x> <y> <z>`, `pull|pullback <32-hex guid> [seconds 0-60] [ranged|body]`, `attack <32-hex guid> [bot]`, `focus <32-hex guid>`, `rest`, `revive party`, `stay`, `follow`, `hold`, and the placement verbs of section 20 (`place`, `place follow`, `rally here`, `fallback` / `fall back` / `back`, `resume`). (`summon` is refused `unsupported` per decision D3.) The tactical verbs (`stop`, `flee`, `guard`, `stance`, `engage`, `rebuff`, `interrupt`) answer their own `cmd` reply and are described in section 23; `cc` is the lifted judge's and answers the usual `cmd` reply (section 23, "Whole-party cc (M4)").
* **Target icons.** `attack`, `pull`, `pullback`, and `focus` set the group's `skull` raid icon on the target when the lead bot has a live group; with no group, no icon is set.
* **Run interaction.** If the bound bots are in an active `.tb run`, `.tb cmd` (and task walks queuing `come`) trigger `outside-order` pause on the next run tick so the two paths never fight.
* **Card log.** Every `.tb cmd` order (accepted or refused) is recorded on the commander's card (`.tb commander status`) and the dashboard's `COMMANDERS` feed via `NoteRunOrder`.
* **Replies (grammar v2).**
  * Accepted: `cmd-begin #token` / `cmd verb=<v> status=accepted bots=<n> result=<encoded> #token` / `cmd-end count=1 records=1 verb=<v> lead=<lead> bots=<n> #token`
  * Refused: `error code=<code> cmd=cmd msg=<encoded> #token` (e.g. `needs-player-sender`, `not-bound`, `unreachable`, `no-ground`, `no-los`, `invalid-target`, `dead-target`, `no-tank`, `engine-not-built`).

### Task verbs (P6: objects, loot windows, gossip)

Bound commanders issue task orders on the shared `TbRunTasks` engine (`forever/src/host/cmd/TbCmdTasks.cpp`):

* **`.tb cmd loot <my | 32-hex guid | entry:<id>[@x,y,z]> [<bot>] [<itemId>] [#token]`**: walks the bot (or the binding's lead) to the corpse or chest, opens its loot window via `LootOpenCompat` / `open loot`, and stores loot through `StoreLootAction`. Refuses `busy`, `in-combat`, `not-bound`, `invalid-target`, `too-far`, `no-item`, `locked`. The asynchronous result (`done: <items/copper>` or `failed: <code>: <msg>`) is read from `.tb cmd state`.
* **`.tb cmd open <my | 32-hex guid | entry:<id>[@x,y,z]> [<bot>] [#token]`**: walks to the door, lever, goober or chest and opens it (`HandleGameObjectUseOpcode` or `open loot`). A door not at rest is never toggled closed (`done: already open ...`).
* **`.tb cmd use <my | 32-hex guid | entry:<id>[@x,y,z]> [<bot>] [#token]`**: uses a game object via `HandleGameObjectUseOpcode` (or `open loot` for chests). `.tb cmd use <itemId>` is refused `unsupported` (the run-task verbs carry no `use_item` row); the console command `.tb use <character> <item id> [self]` casts an item from the bot's bags through the core's own `Player::CastItemUseSpell`.
* **`.tb cmd gossip <my | 32-hex guid | entry:<id>[@x,y,z]> [<bot>] [<index | "text" | text>] [#token]`**: walks to a `UNIT_NPC_FLAG_GOSSIP` NPC, calls `HandleGossipHelloOpcode`, and either lists the menu options (when no option is given) or validates the option via `GossipOrder::MatchOption` (refusing service/coded/missing options) and calls `HandleGossipSelectOptionOpcode`.
* **`.tb cmd lootmethod <ffa|group|rr|master|nbg> [<threshold 2-5>] [#token]`**: sets the bound party's loot method via `HandleSetLootMethodOpcode`; the bots vote in group loot and need-before-greed and stay silent otherwise (no roll window in free-for-all and round robin; the human master distributes master loot).
* **`.tb cmd state [<bot>] [#token]`**: reports each bound bot's active or last task (`task bot= busy= status=none|running|done|failed [verb=] [entry=] [text=]`).
* **`.tb cmd ritual|scout ...`**: refused `unsupported` (decision D5).

## 15. W3: Quests

### `.tb quest <bot> status|accept <questId|*>|turnin [questId]|share <questId>|drop <questId|all>|reward <auto|list|ask|reset|?> [#token]`
One host bot. Replies `quest-begin`, `quest` record(s), `quest-end` (grammar v2; `error code=engine-not-built` when `TORTOISEBOTS_SLICE1=OFF`).

* `status`: lists the bot's quest log (`id=`, `title=`, `status=incomplete|complete|failed|none|rewarded`, `objectives=<done>/<amount>;...`).
* `accept <questId|*>`: accepts a vanilla-eligible quest (`Expansion == 0` or `91xxx`/`99xxx` with spawned giver & ender) from a nearby giver (`status=accepted|refused`, `code=filtered` when refused by the vanilla filter).
* `turnin [questId]`: turns in completed quests to a nearby quest taker (`status=ok|no-turnin`), picking multi-item rewards via `ItemUsageValue` + `RandomItemMgr::ItemStatWeight`.
* `share <questId>`: shares a quest with the bot's party (`WorldPackets::Quest::PushQuestToParty`, `status=shared|refused`).
* `drop <questId|all>`: abandons the quest (`Player::AbandonQuest`, `status=dropped|refused`).
* `reward <auto|list|ask|reset|?>`: sets or queries the bot's `quest reward` option (`mode=auto|list|ask|config`).

## 16. The leave-it-alone list: `.tb ignore`

`.tb ignore <bot|party [leader]> add <guid> | remove <guid> | list | clear [#token]`

The player marks a mob "leave it alone" (a beast to tame, an escort, a mob that must stay alive): none of the commander's bots picks it (dps, tank, assist, pull, defence, grind and guard picks all skip it, even while it attacks), and no area spell goes out near it: none while it stands within ~30 yd of a bot, and no blast whose centre plus its radius reaches within ~30 yd of it. Sweeping Strikes and Blade Flurry are cancelled beside it too. Memory only: up to 8 mobs per commander, each forgotten after 30 min or when it dies on a commanded bot's map.

The owner is the bots' bound commander (`.tb master`), never the bots: every addressed bot must answer to the same commander (`no-commander`, `ambiguous-party`). `guid` is the 32 hex digits `.tb state` (`tgt=`) and `.tb threat` (`mob=`) print.

An explicit attack, pull, assist (resolving onto it) or focus order on a listed mob takes it off the list and then runs (explicit order wins). `.tb cc` on one is refused (`ignored`).

Replies are `ignore-begin`, one `ignore` record per entry (or per op), `ignore-end count=<lines> records=<records> verb= target= commander= ok= refused=`. `expires=` is the seconds left on the 30 min. In a build without the engine every form answers `error code=engine-not-built`.
```
ignore-begin #i1
ignore commander=Mcmd verb=add status=ok guid=0000000000000000F130002C4A000001 name=Ashenvale%20Bear expires=1800 msg=Bots%20will%20leave%20Ashenvale%20Bear%20alone. #i1
ignore-end count=1 records=1 verb=add target=party:Mtank commander=Mcmd ok=1 refused=0 #i1
```
```
ignore-begin #i2
ignore commander=Mcmd verb=list status=ok guid=0000000000000000F130002C4A000001 name=Ashenvale%20Bear expires=1710 #i2
ignore-end count=1 records=1 verb=list target=Mtank commander=Mcmd ok=1 refused=0 #i2
```
```
error code=duplicate cmd=ignore msg=the%20list%20already%20names%20that%20mob #i3
```
(The samples are specified, not yet live output: the first boot replaces them.)

## 17. S1-R1: chat links, `.tb link <text> [#token]`

Reads chat text with the parser the bot brain itself reads it with on ForeverCore (`forever/src/compat/TbChatLinkPure.h`, through the
compat pair `TbCompat_CL.h`), so the console answers exactly what a bot would get. It runs in BOTH builds: no bot, no engine.

`quest=` and `spell=` are the ids the parser answers (0 when the text holds no such link and is no bare number); `items=` is every
`|Hitem:` link's id and `quals=` every item qualifier in the 1.12 layout the bot's `ItemQualifier` reads, both from the same single scan,
so a link is never in one list and not the other. The modern item link puts a GEM in field 3, where 1.12 has the random property, so a
modern link's qualifier is `id:enchant:0` (this port has no random properties); a 1.12 qualifier of 3 or 4 fields keeps its fields, each
written as a number (1-9 digits, else 0), the same non-throwing rule the bot's `ItemQualifier` reads fields with. Every reply value is
grammar-encoded, so a space, `=` or newline in the text cannot add a key or split the record. Talent and trade links answer 0 spell (the
talent RankID lookup and the trade layout are not ported).
A missing text is `error code=missing-argument`; a malformed `#` word `error code=bad-token`.
Real output (console, the first boot of the round):
```
link-begin #l1
link quest=0 spell=0 items=19019 quals=19019:0:0 #l1
link-end count=1 records=1 #l1
link-begin
link quest=783 spell=0 items= quals=
link-end count=1 records=1
error code=missing-argument cmd=link msg=.tb%20link%20%3Ctext%3E
error code=bad-token cmd=link msg=a%20token%20is%20%23%20and%201%20to%2032%20of%20A-Za-z0-9_.%3A-%3B%20nothing%20was%20done
```

## 18. S1-R2: chat commands in, `.tb hear <bot> <sender> whisper|party <text> [#token]`

A real player's whisper or party line reaches a bot through the core's chat hooks (`PlayerScript::OnChat`), not through a packet: the hook
copies the guids and the text into a queue, and the world thread (the top of `BotMgr::Update`) resolves them, judges the line once per
receiving bot and queues it in that bot's inbox; the engine hands it to `PlayerbotAI::HandleCommand` at the start of its next tick
(`forever/src/host/chat/TbChatIn.cpp`, `forever/docs/S1-R2-NOTES.md`). `.tb hear` makes the same line without a client: it
runs the same rule with the `injected` marker (the sender has no session, so exactly the `not-real` test is skipped), queues it in the same
inbox, and replies with the verdict — so a console can prove what a bot would do with a line, and the delivery path itself is the live one.

The reply is `hear-begin`, one `hear` record, `hear-end count=1 records=1`:
`hear bot=Mtank sender=Mcmd kind=whisper verdict=accepted queued=yes #h1`.

The command is engine-only: without the engine there is no brain to deliver to, so `.tb hear` answers `error code=engine-not-built`. A
target that is not a host bot is `error code=not-a-bot`; a missing text `error code=missing-argument`; a kind other than `whisper` or
`party` `error code=bad-argument`; an unknown name `error code=no-such-player`.

What a bot accepts (the rule of `host/chat/TbChatInPure.h`, refusal words in this order): a whisper, a party line or a party-leader line
(delivered as a party line) — not say, yell, raid, guild or channel (`kind`); not addon traffic (`addon`); not its own line (`self`); only
from a player with a network session, so a bot's own reply never commands another bot (`not-real`); 1 to 255 bytes (`length`); only from
its master, the commander a binding names, or a member of its own group (`not-authorized`); a party line only to its own subgroup of
that group, as the core broadcasts it (`other-subgroup`); and only when it has an engine brain (`no-engine`). A line that is accepted and
never runs (its sender logged out, lost the right to command the bot, or the bot may not act this tick) is counted in `dropped` and
`chatlast` reads `dropped`. `PlayerbotAI::HandleCommand` keeps its own rules on top (security level, command separator and prefix, `.tb`
lines are the core's and never reach a bot).

Real output:
```
hear-begin #h1
hear bot=Mtank sender=Mcmd kind=whisper verdict=accepted queued=yes #h1
hear-end count=1 records=1 #h1
hear-begin
hear bot=Mtank sender=Mstranger kind=whisper verdict=not-authorized queued=no
hear-end count=1 records=1
```

## 19. S3 (ForeverCore): throws, falls and forced speed

### `.tb knock <bot> <speedXY> <speedZ>` and `.tb speed <bot> run|walk|swim <rate>`
Test setup, both builds. `.tb knock` calls the core's `Unit::KnockbackFrom` with an origin one yard in front of the bot, so the bot flies
backwards: the core moves it server-side (a bot has no client to answer `SMSG_MOVE_KNOCK_BACK`), and the host watcher waits the flight out
(no engine `StopMoving`, no cast) and applies the core's own fall damage from the flight's highest point. `.tb speed` calls
`Unit::SetSpeedRate`; it shows in the bot's state as `runrate=`.

Both print one `TortoiseBots:` line. The state record's new keys (appended after the P11 keys):
`thrown=<flights seen>` `falldmg=<health the last landing cost>` `runrate=<run speed rate, two decimals>`.

```
.tb knock Mwar 10 7
TortoiseBots: Mwar knocked back (speedXY 10.0, speedZ 7.0); the host watches the flight and lands it with the core's fall damage.
.tb speed Mwar run 0.5
TortoiseBots: Mwar run rate is 0.50 (runrate=0.50).
```

## 20. Place, rally and fall back (issue #15)

`.tb cmd` verbs that move the commander's bound bots into a shape and keep them there. They follow the grammar of section 3 (markers named after the command, `cmd-begin` / `cmd-end`, `count=` / `records=`, one `error` line, the token last) and the record kind is `cmd` (the existing `verb=`, `bot=`, `status=`, `code=`, `msg=`, `seq=` keys). The same words work as a **whisper to one of the commander's own bots** or in the **party/raid chat of the group the commander drives** — the reply lines then arrive as system messages. The chat form runs the same control check as `.tb cmd`. Only the binding's lead refuses the whole command; a bound bot owned by another commander or in another player's party is refused on its own row (`status=refused`, the control check's `code=`), so the rest of the command still runs. A party/raid line only counts when that channel holds at least one of the commander's bots. In chat only a commander's words count, and the bare aliases (`back`, `fallback`, `resume`, `rally`, `rally here`, `rally clear`) only when the whole message is the command, so ordinary conversation is never an order; a malformed `place ...` still answers with a typed error. A `place ...` line always takes the new options (`<yd>`, `tank`, `at:`, `face:`); `rally at:` and the `fallback` target words work only through `.tb cmd`.

### `.tb cmd place <shape> [tight|normal|wide|<yd>] [here|target|cursor|tank|at:<x>,<y>[,<z>]] [face:<deg>] [who...] [#token]`

Shapes: `close` (block two wide), `circle`, `arc` (half ring), `spread` (grid 2.5 gaps apart, no shared splash), `line` (one row, tanks in the middle), `column` (single file), `spear` (wedge, tip first), `arrow` (V, tip first). Gaps: `tight` 2.5 yd, `normal` 4 yd (default), `wide` 8 yd, or a number `1.5`..`12.0` yd with at most one fraction digit (`5` is `5.0`). Anchors:

* `here` (default): the whole shape along the player's facing (or `face:`), its nearest slot 2 yd from the player and never on the player's own spot.
* `target`: a ring is centred on the player's creature target, its first slot on the player's side (the tank between the player and the target, the group wrapping round it), and is never smaller than the target's size (its combat reach) plus one gap, so no slot is inside the target. The row shapes stand between the player and the target, the tank row one gap clear of the target's size (nearest the target) and the rest behind it on the player's side. The target anchor faces the target; `face:` with it is `bad-argument`.
* `cursor`: waits up to 30 s for the player's minimap ping (`CMSG_MINIMAP_PING`, read only), centres the shape there and faces it away from the player (or `face:`). A ping that never comes is `error code=timeout`, and no bot moves.
* `tank`: centred on the commander's anchor tank (the first tank in rank order among every bound bot that is alive, in the world, on the commander's map and instance and still answers to it), slot 0 behind the tank (away from what it faces) and the ring never narrower than the tank's reach plus one gap. Only `circle`, `arc` and `spread` take it (`bad-argument` for any other shape). The tank is never in the shape, its own hold row is untouched, and the head names it (`anchor_bot=`). No living tank on the map is `error code=no-tank`.
* `at:<x>,<y>[,<z>]`: centred on the typed point (floor-snapped), facing away from the player (or `face:`; within 1 yd the player's facing). A point more than 100 yd from the player (in x, y or z) is `too-far`; a point off the map is `bad-argument`, and one with no dry floor or no safe walk from the player is `no-ground`/`liquid`/`unreachable`. A typed point is judged by the commander's own walk: a spot the bots could stand on but the commander cannot walk to is refused (the slot rules then judge each bot's walk from there).

`face:<deg>` (0..359) replaces the front for `here`, `cursor`, `tank` and `at:`: the shape's front direction is `(cos(deg·π/180), sin(deg·π/180))` in world (x, y), the same number `.gps` prints as orientation × 180/π (0 = +X, 90 = +Y).

Who: a bot's name, `all`, `tanks`, `healers`, `melee`, `ranged`, `dps`, a class (`warrior`, `paladin`, ...), `group1`..`group8`; none = every bot the player commands. The options may come in any order between the shape and the first who word, each at most once (`duplicate`); a bare number is always the gap, never a who name. Slot order is always tanks, melee, ranged, healers (the name breaks a tie), and the front slot goes to the first tank. A ring more than 35 yd wide (a huge target, or more bots than the circumference holds) is refused `too-far`; a slot past 35 yd from the anchor centre is skipped (`too-far` on that bot's row).

Each slot is judged before any bot moves: a floor within 25 yd below and 5 yd above the anchor's z (`no-ground`; the core's bounded floor read, never an unbounded search), a floor no more than 5 yd above or below the anchor (`ledge`), a dry stand point by the module's one liquid rule (`liquid`: water at or above the floor, lava, slime), a walkable path (`unreachable`), a path that does not enter liquid after dry ground (`liquid-path`), and a path no longer than 2.5× the straight distance plus 10 yd (`detour`). A slot refused by the floor, ledge, liquid, path or detour rules is nudged up to three times on the straight line towards the shape's centre (1, 2, 3 yd), never onto a cleared spot or within half a gap of an accepted slot; the first dry, reachable, free candidate wins and the row says `nudged=<yd>`; a `target` ring nudge never moves a bot inside the target's combat reach. All three failing refuses the bot with the original slot's code.

Placed bots keep their slot after every fight: the slot is the bot's stay position (movement `stay`, the engine's own `return to stay position` walk, and the module walks a bot back that lost the slot outside combat; the walk-back asks the same path verdict as the placement — liquid path and detour included — and gives up after three failed checks, saying which rule refused). In a fight `stay` is kept out of the combat state, so melee still step in and healers can step for line of sight; the bot returns to its slot out of combat. This combat-time handling belongs to the placement's own `come` only: a run's walk or task stand (`come`) and a plain `.tb order come` reset the bot and keep `stay` in the combat state exactly as before, so a run's pull still gets its "return to stay position" walk. A `place` typed while the group fights therefore moves the bots when that fight ends; ranged bots may leave the slot during it. `place follow` or `follow` releases it; `attack`, `pull` and `pullback` do not.

A placement replaces only the rows of the bots it addressed: `place line tanks` then `place arc healers` keeps the tanks' rows, and a placement that put nobody leaves the old hold exactly as it was. A bot whose row is replaced or refused is released (`follow`, or only the placement's own stay when it changed commander). No slot ever stands on the commander's own spot: a circle turns by half a slot, the row shapes leave that slot empty (its bot answers `no-slot`).

```
cmd-begin #p1
cmd verb=place status=accepted bots=4 shape=circle gap=5.0 anchor=tank anchor_bot=Mtank radius=6.5 slots=4 #p1
cmd bot=Mrogue verb=place role=melee slot=0 pos=-16.10,-45.02,-21.85 seq=42 #p1
cmd bot=Mage verb=place role=ranged slot=1 pos=-14.50,-48.10,-21.85 seq=43 nudged=2.0 #p1
cmd bot=Mhunter verb=place role=ranged slot=2 status=refused code=liquid msg=the%20slot%20is%20in%20water%20or%20lava #p1
cmd bot=Mpriest verb=place role=healer slot=3 pos=-21.90,-48.10,-21.85 seq=44 #p1
cmd-end count=5 records=5 verb=place lead=Mtank bots=4 placed=3 refused=1 #p1
```
The head carries `gap=` (the typed word, or the number with one fraction digit), `anchor=` (`here`, `target`, `cursor`, `tank` or `at`), `face=` when given, `at=` (the snapped point) for the `at` anchor, and `anchor_bot=` for the tank anchor. A nudged slot's row carries `nudged=<yd>`. The commander's hold is visible on the binding card (`.tb commander status`): `hold=place|fallback` on the `binding` record, `rally=`/`rmap=`/`rinst=` when a rally point is stored, and `hold=`/`slot=` on exactly the held members.
(The markers are the command's (`cmd`), as for every `.tb cmd` verb; the sample lines are specified, not yet live output.)

A `cursor` placement answers in two replies with the same token: at once `cmd verb=place status=waiting anchor=cursor shape=.. gap=.. wait=30` and `cmd-end ... pending=yes`; then, on the ping, the normal `status=accepted` reply, or `error code=timeout cmd=cmd msg=no%20minimap%20ping%20within%2030%20s%3A%20nothing%20was%20moved #token`. A later command from the commander (any `.tb cmd`, `.tb order` or place/chat command) ends the waiting request first: its token gets `error code=cancelled cmd=cmd msg=a%20later%20command%20from%20the%20commander%20replaced%20this%20cursor%20request`, so a ping can never complete a command the commander replaced.

Per-bot refusals (`status=refused code=`): `no-character` (a bound bot not in the world), `not-in-group` (it no longer answers to this commander), `dead`, `other-map`, `no-ground` (no floor, or outside the map), `ledge` (a floor more than 5 yd above or below the anchor), `liquid` (the stand point is in water or lava), `unreachable` (no walkable path), `liquid-path` (the path enters water or lava), `detour` (the path is far longer than the straight line), `no-slot` (the commander's or the anchor tank's own spot), `too-far`, and the order queue's own codes (`no-engine`, `queue-full`, ...). Typed errors (`error code= cmd=cmd msg=`): `needs-player-sender`, `not-bound`, `missing-argument`, `bad-argument` (an unknown shape/gap/anchor word, a gap outside 1.5..12 yd, a face outside 0..359, `face:` with the target anchor, a malformed point, a shape the tank anchor does not take, or a bad `rally`/`fallback` word), `duplicate` (a gap, anchor or facing word twice), `unexpected-argument`, `bad-target` (a who name that is not one of your bots; a player/bot as the `target` anchor), `no-selection` (the `target` anchor with no target), `other-map` (the target is elsewhere), `teleporting`, `no-bots` (the selection matches nobody), `no-tank` (the tank anchor with no living tank on the map), `too-far`, `no-rally` (`fallback rally|last` with no point on this map), `timeout`, `no-fallback`.

### `.tb cmd place follow [who...] [#token]`

Releases the selected bots (`all` when no who word): each gets the engine's `follow` order and leaves the hold. `cmd verb=place status=accepted follow=yes bots=..`, one `cmd bot=.. follow=yes status=queued seq=..` per bot, `cmd-end ... released= refused=`.

### `.tb cmd rally [here|clear|at:<x>,<y>[,<z>]] [#token]`

The fall-back point is memory only, with the player's map and instance; nothing moves.

* `rally` (no word): read-back. `cmd verb=rally status=shown set=yes pos=x,y,z map=<m> inst=<i>` or `cmd verb=rally status=shown set=no`, then `cmd-end ... verb=rally set=1|0`.
* `rally here`: where the player stands.
* `rally at:<x>,<y>[,<z>]`: the typed point, floor-snapped and judged by the stand rules (`no-ground`, `liquid`), the 100 yd reach in every axis (`too-far`) and the player's own walk to it (`unreachable`, `liquid-path`, `detour`). The point is decided before anything is stored: a refused `rally at:` answers its error and leaves the stored point (or its absence) unchanged.
* `rally clear`: forgets the point (`cleared=yes`, or `cleared=no` when nothing was set).
```
cmd verb=rally status=shown set=yes pos=-9100.80,-2005.30,132.90 map=1 inst=0 #r3
cmd-end count=1 records=1 verb=rally set=1 #r3
```
A `rally here` / `rally at:` answer is `status=accepted` with the same `lead=`, `pos=`, `map=` and `inst=` keys and `cmd-end ... verb=rally set=1`; `rally at:` and `rally clear` work only through `.tb cmd` (the chat form takes bare `rally`, `rally here` and `rally clear`).

### `.tb cmd fallback [rally|last|here|at:<x>,<y>[,<z>]]` (`fall back`, `back`) [#token]

Every bot the player commands disengages (the swing, the cast and the pet's attack stop, and the bot is held passive on the way, so it does not re-engage instead of leaving) and runs to the rally point — or to the player's position when no `rally here` was given or the stored point is on another map/instance — and forms up there in the normal gap: tanks in a front row facing the threat, melee just behind them, ranged and healers on an arc behind (the healers at its ends). With a stored rally point the front is the direction from the point towards the commander (the side the group came from, so the tanks are nearest the enemy); without one it is the commander's facing. Nobody stands on the rally point, and no slot is on the commander's own spot. The formation holds (the same stay slot and walk-back as `place`) until `attack`, `pull`, `pullback`, `resume` or `follow`; a replaced fall back releases every bot whose old row it dropped.

The target words: no word is the stored point while it is on this map and instance, else the player's position (`from=memory|commander`); `rally`/`last` insist on the stored point (`no-rally` when there is none on this map, and nothing moves); `here` is the player's position with the player's facing, ignoring a stored point; `at:<x>,<y>[,<z>]` is a typed point (judged `no-ground`/`liquid`/`too-far`/`unreachable`/`liquid-path`/`detour`) with the front towards the player (`from=point`). A fall-back slot that only collides with the player's or the tank's own spot is nudged off it before anything else. A bot whose slot still fails after the nudge does not stay where it fought: it gets the same `come` to the fall-back centre when the centre itself is standable, else to the player's own position when that point is standable too, with `slot=-1`, `to=centre|commander` and its slot's refusal in `code=`. (Residual: with no stored rally the centre *is* the player's own spot, so a `no-slot` bot whose nudges all failed still ends up on it; the emergency `come` matters more there than the 1.5 yd clearance.) A point that fails the stand rules is not an emergency destination: the bot stays unqueued rather than being sent onto water, off a ledge or through a wall. Only a dead bot, one on another map and an order-queue refusal leave it unqueued. A `to=` row counts as placed.

The reply says where it formed and whether the point came from memory, the player or a typed point:
```
cmd-begin #f2
cmd verb=fallback status=accepted bots=3 gap=normal from=point rally=-9098.20,-2001.10,132.90 slots=3 #f2
cmd bot=Mtank verb=fallback role=tank slot=0 pos=-9098.20,-1997.10,132.90 seq=51 #f2
cmd bot=Mage verb=fallback role=ranged slot=1 pos=-9101.10,-2005.00,132.90 seq=52 #f2
cmd bot=Mpriest verb=fallback role=healer slot=-1 to=centre code=ledge pos=-9098.20,-2001.10,132.90 seq=53 #f2
cmd-end count=4 records=4 verb=fallback lead=Mtank bots=3 placed=3 refused=0 #f2
```
A fall back ends its passivity when the bot reaches its spot, when it has stopped short in a fight (no longer walking, after a 1.5 s grace: a blocked or crowded slot) or after 10 s on the way, whichever comes first; the bots then fight what follows them and still return to the spot after the fight. A row that is dropped also releases it at once: the fall back is cancelled by another command, replaced by a new one, the bot dies, leaves the world or the binding ends. It releases only the `passive` it set itself: a `hold`, a `flee` or a `+passive` order that came before (or after) the fall back keeps its own, so a fleeing bot does not start fighting again when the fall back ends. `attack`, `pull` and `pullback` end the hold by themselves (the order is carried out as usual, and the emergency stay/passivity is cleared so the bots can move); `follow` ends it and returns the bots to following.

### `.tb cmd resume [#token]`

Ends a fall back and puts the bots that formed up back on follow (`cmd verb=resume`, one row per bot, `cmd-end ... released= refused=`). Before a historical row turns into an order the bot is re-checked: if it no longer answers to this commander it is `status=refused code=not-in-group` and its row is dropped (only the placement's stay is cleared, never an order to the old commander's bot). A `place` hold is not touched — that is `place follow` or `follow`. With no fall back standing: `error code=no-fallback cmd=cmd msg=the%20bots%20are%20not%20holding%20a%20fall%20back ...`.

The add-on "Positions" tab (shape, gap, who, Here/Target/At cursor buttons) is **not** part of this round; it stays an add-on round of its own.

## 21. Wailing Caverns: the gossip step, the event holds and the end of the plan

The run's plan grammar gains the two step kinds the Wailing Caverns event needed on ForeverCore (`forever/docs/RUN-WC-NOTES.md`).

* **`gossip <npc entry> <bot> <option>`** (a plan's `gossip` row: the policy sends it with the row's `param` as the menu number): the bot walks to the NPC and talks through the two packets a client sends — `CMSG_GOSSIP_HELLO`, then `CMSG_GOSSIP_SELECT_OPTION` with the menu id and the option id of the line picked from the menu the core built (`GossipOrder::MatchOption`: a vendor, trainer, flight, bank or other service line is never picked). Only the NPC a `gossip` step of the bot's own run names, or an entry in `AiPlayerbot.BotGossipNpcs`, is talked to: anything else is refused `not-allowed` before a packet is sent. The answer text is the menu line; a menu without the named option is `no-option: No option N. The menu offers: ...`, no NPC `no-npc`. The `run` record's `event kind=gossip` marks the end. On this core a database menu option carries sender = the menu id and action = the order index, so the option's own type is what tells a service line (`MakeGossipOption`): a make-inn, auction, battlemaster, stablemaster, banker, mailbox or talent line is refused, a plain line beside them is picked.
* **`hold` rows ended by instance data** (`flags` 8, `entry` = the data id, `param` = the value) and **by its units** (`entries` + `radius`: every unit dead after one was seen awake): `eventData` is the instance's own `GetData(entry)` (no instance script: -1, and the hold pauses saying so), a units hold lists the living creatures of `entries` within the radius with `eventDown` counting the dead. Wailing Caverns reads data 4 = 4 for the gate: the four Fanglords' own smart_scripts set Enc 0-3 = 3 on death, and the instance then sets data 4 = SPECIAL. It does not wait on data 5: on the pinned core Mutanus's smart_script sets boss state 4 (type 1), not instance data 5, so data 5 never reads 3; his boss step's own kill read ends the run. An escort hold with `flags` 65536 (`kPlanFlagEscortNpc`) reads the event's start NPC (the gossip row before it) around the lead bot: dead or gone for 10 s it stops the event with that reason, once (the run goes back to the gossip, so a respawned Disciple can start it again); alive but still within 5 yd of his stand after 30 s of the hold with no wave up, the gossip did not start the escort and the run stops with that reason too. The gossip looks back to the nearest gossip row through the holds between them (`EventGossipStartStep`). A hold unit's x, y and its engaged/victim reads are filled, so the ranged stand on the side the event comes from.


## 22. BG1: battlegrounds, queue and status (round BG1)

`BATTLEGROUNDS-SPEC.md` decision D11 and `forever/docs/BG1-NOTES.md`. The whole round sits behind `TortoiseBots.Bg.Enable` (default 0):
with it off the hooks do nothing and no bot queues, accepts or leaves by itself. **The reply shapes below are specified, not yet live
output** (BG1 shipped without a live run); the live checks are listed in the notes for the lead's queue. Grammar version 2: one
`bg ...` record per line, one `bg-end` line with the token last.

### `.tb bg status [#token]`

The config line, the loaded maps, one line per instance holding bots, one line per bot inside and one per bot waiting in a queue.
`role` and `objective` are `-` until the WSG/AB rounds compute them.
```
bg: enabled=1 maps=wsg,ab fill=1 autonomous=0 min_per_team=5 max_instances=2 max_bots=50 max_minutes=45 queued=6 inside=9 events=15
bg maps: wsg map=489 list=2 enabled=1 stands=2 ab map=529 list=3 enabled=1 nodes=5
bg instance=3 map=wsg status=wait-join score=0-0 alliance=1h+5b horde=0h+6b
bg bot=Mwarrior role=- objective=- map=wsg instance=3 team=alliance alive=1 move_age=2s
bg queued bot=Mother map=wsg bracket=10-19 since=3s invited=0
bg-end verb=status lines=5 #s1
```
`bg maps` says `enabled=0` and the boot log carries the one error line when a flag stand, node banner or start location is missing in the
core's data; a disabled map gets no fill.

### `.tb bg events [since=<seq>] [#token]`

The ring of the last 1024 events, oldest first. `since=` asks for the events above a seq (a watchdog keeps `last=` and sees that nothing
new arrived); a `since` below `oldest=` means the ring dropped events instead of there being a gap. Kinds: `queue`, `join`, `leave` (with
`why=`: `match-over`, `max-minutes`, `master-gone`, `declined`, `ineligible` (`bound`/`run`/`left-pool`), `no-human`, `queue-timeout`,
`logout`, `out`, `queue-end`, `port-failed:N`),
and the later rounds' `role`, `objective`, `flag-pick|drop|return|capture`, `node assault|defend|occupied`, `fallback`.
```
bg event seq=15 t=214 kind=queue bot=Mwarrior map=wsg team=alliance instance=0 flag=- node=- why=
bg event seq=16 t=219 kind=join bot=Mwarrior map=wsg team=alliance instance=3 flag=- node=- why=
bg-end verb=events since=0 count=2 oldest=15 last=16 #e1
```
`t=` is seconds since the server started (`GameTime::GetGameTimeMS`), not since a match.

Queue and acceptance never go through anything but the core: `WorldSession::JoinBattlegroundQueue`, the typed
`CMSG_BATTLEFIELD_PORT` packet through `HandleBattleFieldPortOpcode`, `Player::LeaveBattleground`, and the core's own
`RemovePlayer` + `RemoveBattlegroundQueueId` pair for a queued bot that became ineligible. The match itself is only ever started by
the core: the module holds it back through `WorldScript::CanCreateBattleground` until a human's queue reaches the fill target, and
re-schedules the core's queue update each pass so bots still waiting follow the human into his instance.

## 23. Issue #131: tactical orders (M1-M4)

The orders the commander or a GM gives mid-fight: `stop`, `flee`, `guard`, `stance`, `engage`, `rebuff`, `interrupt`, the toggles `aoe`/`autocc`/`sap`/`loot`, and `corpserun`. Both surfaces queue the same order for the bot's next engine tick through the h3 queue, so the result arrives in `.tb state` `ord=` (`<seq>:<ok|fail%3A<code>>`, `lastorder=<verb>`), never in the command's own reply. An active run sees the new seq and pauses (`outside-order`). A toggle `status` is a read and answers in the command's own reply.

Every tactical order except `engage` first ends a live engage hold (`HasEngagePullWindow()` → `EndPullWindow("order")`): the hold waits for the commander's first hit, and a new order replaces it. `stop` adds no `passive`, so a bot hit again defends itself; `hold` or `flee` is the order that keeps it out.

### `.tb order <bot|party [leader]> <verb> [argument] [#token]`

The verbs and their refusals are the section 4 table rows. The reply is the usual `order-begin` / one `order bot=.. verb=.. seq=.. status=queued|refused [code= msg=]` per bot / `order-end`. A dead bot is refused `not-alive`; `rebuff` refuses `in-combat`; a missing legacy action is `action-not-registered` (with `name=`), a missing strategy `strategy-not-registered:guard`.

```
order-begin #o1
order bot=Mtank verb=stance seq=41 status=queued #o1
order-end count=1 records=1 verb=stance target=party:commander:Mcmd bots=1 queued=1 refused=0 #o1
order-begin #o2
order bot=Mpriest verb=rebuff status=refused code=in-combat msg=a%20bot%20in%20a%20fight%20does%20not%20start%20a%20rebuff #o2
order-end count=1 records=1 verb=rebuff target=Mpriest bots=1 queued=0 refused=1 #o2
```

### `.tb cmd <verb> [argument] [bot] [#token]`

Acts on every bot of the caller's binding (lead and members) that is alive, or on the one named bot (`stop [bot]`, `stance <word> [bot]`); a name outside the binding is `no-bot`, and a dead named bot is refused by its own pre-flight.

| Command | Extra keys | Result |
|---|---|---|
| `stop [bot]` | - | `result=party <n>` or `result=bot:<name> 1` |
| `flee` | - | as above |
| `guard` | - | as above |
| `stance <near\|tank\|turnback\|turn\|behind> [bot]` | `stance=<canonical word>` | as above |
| `engage [seconds 0-60]` | `seconds=<n>` (the default 4 when left out) | as above |
| `rebuff` | - | as above |
| `interrupt <guid> [bot]` | `action=<action>` | `result=bot:<name> 1`: one bot is picked (the named one only, else the first in party order with a castable interrupt) |
| `aoe\|autocc\|sap\|loot [on\|off\|status] [bot]` | `state=<on\|off\|mixed>` | `result=party <n>` or `result=bot:<name> 1` (a `status` is a read) |
| `corpserun [bot]` | - | `result=party <n>` or `result=bot:<name> 1` (no bot: every dead bound bot) |

Accepted reply: `cmd-begin` / `cmd verb=<v> status=accepted bots=<n> result=<scope> [stance=|seconds=|action=|state=] [refused=<n>]` / one `cmd bot=.. verb=.. status=refused code=.. msg=..` row per refused bot / `cmd-end count=.. records=.. verb=.. lead=.. bots=.. ok=.. refused=..`. When nothing could be queued the whole reply is the one-line `error code=<first refusal> cmd=cmd msg=..`. A refusal of one bot in a party order does not refuse the order while at least one bot queued.

```
cmd-begin #c1
cmd verb=stance status=accepted bots=5 result=party%205 stance=behind #c1
cmd-end count=1 records=1 verb=stance lead=Mtank bots=5 ok=5 refused=0 #c1
cmd-begin #c2
cmd verb=engage status=accepted bots=5 result=party%205 seconds=5 #c2
cmd-end count=1 records=1 verb=engage lead=Mtank bots=5 ok=5 refused=0 #c2
```

### Interrupt (M2)

`.tb order <bot|party> interrupt <target>` and `.tb cmd interrupt <guid> [bot]` check the target (exists, alive, `IsNonMeleeSpellCast(true)`: `no-target`, `dead-target`, `not-casting`) and pick ONE bot: the named bot only, else the first bot in party order whose probe finds an action. The probe walks TB's list (`counterspell`, `silence`, `kick`, `pummel`, `shield bash`, `bash`, `hammer of justice`, `earth shock`, `death coil`) and asks for the registered action, `HasSpell`, `IsInterruptableSpellCasting(target, action, true)` and `CanCastSpell(..., range ignored)`; a bot that can also cast it from where it stands (range enforced) wins over an earlier bot that would have to walk, and only when no probed bot is in range does the first able bot win. No bot qualifies: `no-interrupt`, and `msg` carries `name:reason` per probed bot (`dead`, `no-engine`, `other-map`, `far`, `no-spell`). Only the picked bot receives the order (`arg` = the action, `unit` = the target); its tick selects the target and runs the action. When the direct call refuses and the target still casts, the same action is queued on the current engine as `ACTION_PASSTROUGH` (with `OnCombatStarted()` first for a bot in combat), so its reach prerequisite can walk and cast; if that also refuses, `fail:interrupt`.

### Pull options (M2)

`pull|pullback <guid> [seconds 0-60] [ranged|body]` in `.tb cmd`; `pull <target> [seconds] [ranged|body]` in `.tb order`. `ai::ParsePullOrderOption` parses the words (any order, each once; `ranged` and `body` exclude each other) and the canonical text (`5 ranged`, `body`, ...) travels in the queued order. The seconds replace the party hold's join delay (`kPullJoinDelaySeconds`, 3 s, for both verbs when no seconds are given) and, for `pullback`, the strategy's DPS join delay (`AiPlayerbot.PullBackDpsJoinDelay` when no seconds are given). `ranged`/`body` call `PullStrategy::BeginCommand` for this command only, before `pull my target`, and are rolled back when the request refuses - for `pull` and `pullback` alike. The run policy's one body-pull retry after a ranged pullback refused as unreachable (`pull <guid> body`) now actually walks in (before M2 `body` was parsed and dropped). The kill switch does not cover these options: they are the judge's grammar.

### Toggles and corpse run (M3)

`aoe|autocc|sap|loot [on|off|status]` in both surfaces (`.tb cmd`: `[on|off|status] [bot]`; the two-word `auto cc` is accepted and normalised). A bare verb flips each bot's own state; `status` reads without queuing. The choice is a host setting per bot guid (memory, server uptime; no DB): `h3::BeforeEngineTick` re-enforces it every tick, so a `co`/`nc` change of the same strategy does not win and a `ResetStrategies` is covered. `free` does not clear a toggle. Defaults when never set: aoe on, autocc off, sap on, loot = the kit.

| Toggle | Strategy | Present means |
|---|---|---|
| `aoe` | `no aoe` (combat) | off |
| `autocc` | `auto cc` (combat) | on |
| `sap` | `no sap` (non-combat) | off (rogues only; a non-rogue is refused `no-rogue`) |
| `loot` | `loot` (non-combat) | on |

`.tb cmd` answers `state=<on|off|mixed>`: the wanted state for a change, the bots' current state aggregated for a `status` read. `.tb order` answers `status=read state=<on|off>` for a `status`, `status=queued` for a change.

`corpserun` (also `corpse run`) in `.tb order`, and `corpserun [bot]` in `.tb cmd` (no bot: every dead bound bot). An alive bot is refused `not-dead`. A body runs the existing `release` verb with its gates (`no-entrance`), then the W1 dead engine walks; a ghost answers `status=queued` with Run `ok` (the walk is already the engine's). `.tb state` `corpserun=` shows the leg.

### Whole-party cc (M4)

`.tb cmd cc <mark> <guid> [<bot>|<family>] [family]` and `.tb cmd cc clear [<bot> [<mark> <guid>]]` now run without a dungeon run (before M4 they answered `unsupported`). The judge's grammar (`TbRunOrdersPure.h`): with no bot, the module picks one; the run policy's own `cc <mark> <guid> <bot> [family]` is unchanged.

* `cc <mark> <guid>`: the pick. `cc <mark> <guid> <word>`: `<word>` is the family when `CcOrderFamilyKnown` accepts it and it is not `none` (`polymorph`, `shackleundead`, `banish`, `hibernate`, `seduction`, `freezingtrap`, `sap`, `repentance`, `scarebeast`, `fear`), else a bot. A bot named like a family word writes `cc <mark> <guid> <bot> none`. `<guid>` is the 32-hex text `.tb state` prints (or a run handle).
* The pick reuses the run's table (`DungeonRun::kRunCc`): over every (bot, family) pair the best wins: tier A before S before R, then the table order (polymorph before sap), then the party order. A pair qualifies when the family is an automatic pick (Freezing Trap is not), `CcFits` accepts the target's creature type, the bot carries the `cc` strategy in its combat kit and knows a rank of the spell, and the owner's `.tb cc ... deny` permissions do not deny the family's kind (fear is denied by default). A `prePull` row (Sap) also needs the target and the bot out of combat. A named family limits the pick to it. Candidates are the lead and the bound members. Kits that carry `cc` today: mage and rogue; more with no change here once the kits (#123) carry it.
* Answer: `result=bot:<name> 1 family=<token>`; the picked bot has `rticc=<mark>` and `ccfam=<token>` in `.tb state` (the same `h8::ApplyCcOrder` as `.tb cc`). No pick: `error code=no-cc-bot msg=<name>:<reason>;...` with the reasons `dead`, `no-cc-kit`, `no-fitting-spell`, `denied`, `in-combat` (Sap). Skull is refused `invalid` (the party's kill mark), as before.
* The order starts the cast, as TB's `RunCcOrder` does: once the mark, `rti cc` and `ccfam` read back, the bot selects and faces the unit, the class crowd-control action that carries the picked family is found through the action graph and run once; when the unit is out of reach the mature action is queued so its own `reach cc` prerequisite walks the bot in and the cast follows (the fight-start trigger stays the fallback). The chooser also takes the bot's `rti cc target` first, at any range, before the engaged-attacker tournament (`CcChooserTakesAssignedMark`), so an order given before its pack opens still has a target. A hand-written order with no family (`none`) starts no probe.
* `cc clear` alone clears every bound bot's crowd-control mark and its icon (`h8::ClearCcOrder` for each; `result=party <n cleared>`, with ` failed=<names>` appended when a bot could not be cleared: it keeps its mark). `cc clear <bot> [<mark> <guid>]` is unchanged.
* The run's own bookkeeping (the pending clear, the bots a run marked) is touched only when a run sent the order.

```
cmd-begin #c1
cmd verb=cc status=accepted bots=1 result=bot%3AMmage%201%20family%3Dpolymorph #c1
cmd-end count=1 records=1 verb=cc lead=Mtank bots=1 #c1
```

### State and configuration

`.tb state` gains two keys after the h8 keys: `stance=<near|tank|turnback|behind>` (empty without an engine) and `rebuff=<seconds left>` (empty when no force-rebuff window runs). `mv` reads `guard` while the guard strategy holds the bot in place.

`TortoiseBots.Orders.Tactical` (default 1) is the kill switch: at 0 every tactical verb (the pull options are the judge's and stay available) answers `error code=disabled cmd=<order|cmd> msg=TortoiseBots.Orders.Tactical%20is%200` and nothing is queued. Read on each command.

Rebuff opens the same force-rebuff window (begin ms = now, pending/proposed false) the ready-check bot uses: the bot's normal buff pass tops the party up while the two-minute window runs. It has no `force rebuff` strategy gate (that strategy is not compiled): the window values are the gate. `AiPlayerbot.ForceRebuffOnReadyCheck` does not gate this order; it gates the ready-check hold of a later round.

## Onyxia observation

`.tb onyxia <bot> observe <32-hex-boss-guid> | status | off [#token]`
adds opt-in, read-only encounter observation for a host bot with its engine
running. It accepts only entry 10184 in that bot's map-249 instance. Replies
are `onyxia-begin`, `onyxia` records and `onyxia-end` with the normal counts and
echo token. The engine-off build returns `engine-not-built`.
See [ONYXIA.md](ONYXIA.md) for fields, cleanup and sampling limitations.

`.tb onyxia <bot> ground <32-hex-boss-guid> <tank-name> [#token]`
enables the opt-in ground strategy. Configure the named host tank first, then
its same-group participants. Explicitly pull using that owner's host order;
configuration alone does not pull. Other accepted orders release control.
See [ONYXIA.md](ONYXIA.md) for damage gates, checked routes and live-test limits.

## Autogear (`.bot autogear` and `.autogear`)

* `.bot autogear <bot> [quality|ilvl]`: incremental upgrades-only re-gearing for same-account owned bots (and GMs; pool bots refused, bot must be alive, bot in active run refused). Gated by `AiPlayerbot.OwnedBotAutogearEnabled` (default `0`), capped by `AiPlayerbot.OwnedBotAutogearQualityCap` (default `2`, uncommon) and `AiPlayerbot.OwnedBotAutogearIlvlCap` (default `0`, uncapped). Warns if the equipped ranged weapon has no matching ammo in bags.
* `.autogear [player] <spec> [ilvl]`: top-level command for real players (bots refused; dead, in-combat, or shapeshifted targets refused). GM-only by default (`AiPlayerbot.PlayerAutogearForAll = 0`); when `1`, non-GMs may gear themselves only. Equips upgrades only, moves replaced items to bags or mails them if bags are full, warns on missing ranged ammo, and logs `PlayerAutogear: caller=...`.

## Guilds

Free (pool) bots take part in guilds under `TortoiseBots.World.Guilds` (default 1) and found their own under
`TortoiseBots.World.GuildForming` (default 0); both keys are documented in `forever/conf/tortoisebots.conf.dist`.

* **Accept and stay.** A free bot with an outstanding guild invite accepts it at its next world tick, unless the guild
  already holds 1000 members or is on the other side (`AllowTwoSide.Interaction.Guild` off). A refused invite is
  declined, so it is not re-read every tick. Nothing else takes a bot out of a guild: it leaves only when the guild
  reaches 1000 members. On joining, the guild-order bot rank rule (`TortoiseBots.World.GuildOrderBotRank`) still
  applies, because `tbhost::guild::AcceptInvite` is the one join path.
* **Founding.** With the forming key on, a free bot above level 10 with the core's charter price in its own purse buys
  a charter at a petitioner NPC within 30 yd, nearby free bots of its faction sign it, and the bot turns it in at a
  petitioner. The charter is paid by the bot; no money is granted. Log: `[W9] guild-charter`, `guild-sign`,
  `guild-founded` (and `guild-* -refused`).
* **Owner whispers** (the whisper must reach the bot at all: the sender is its master, its commander or in its party;
  among those, only its master or a game master may manage the guild, others are refused): `guild invite <name>`, `guild join`, `guild leave`, `guild promote <name>`, `guild demote <name>`,
  `guild remove <name>`, `guild leader <name>`. Invite, promote, demote and remove need the bot's own rank right;
  leader needs the bot to be the guild master. The bot answers in a whisper, and the server log carries
  `[W9] guild-whisper bot=<bot> from=<sender> cmd=<verb> target=<name> result=ok|fail:<code>`.
* **Guild chat.** `/w <bot> speak /g <text>` makes the bot say the line in its guild chat, through the guild's real
  speak right (a rank without `GR_RIGHT_GCHATSPEAK` says nothing).
* **Asking for an invite.** Under `TortoiseBots.World.GuildAcceptPlayers` (off by default; turn it on by setting it to
  `1`), with `Guilds = 1`, a guildless player whispers a bot of a bot-run guild (its leader is a pool bot) one of `ginv`,
  `ginvite`, `guild inv`, `can i join`, `can i join your guild`, `can i join the guild`, `invite me`,
  `invite me to your guild`, `invite me to the guild`, `inv me to guild` (case, extra spaces and a trailing `?!.`/`please`/`pls` do not matter)
  and gets the core's normal guild invite: from the whispered bot when its rank may invite, else the online pool
  bot of the lowest rank sends it (a player's owned alt in the guild is never used). The bot answers with one whisper
  (`Sent you an invite to <Guild>.`, `I asked <Officer> to invite you to <Guild>.`, or a refusal that says what
  happened: already in a guild, full, other faction, no officer online, not in a guild, not a bot-run guild; and, when
  the invite itself is refused, its own reason: ignoring the inviter, an invite already waiting, no one able to invite
  right now, or `I could not invite you right now.`). One request per player per 20 s; repeats are answered silently
  and the line never reaches the engine's chat commands. Log: `[W9] guild-request bot=<bot> from=<player> guild=<id>
  inviter=<name|-> result=invited|<code>`.

## Diagnostics (#142)

The GM whisper words are the legacy bot commands; permission is the bot engine's own tier table
(`PlayerbotAI::AuthTierForCommand`: `debug`, `cdebug`, `log` and `set value` are GameMaster-only).

* `/w <bot> debug` (or `cdebug`) turns the bot's action-line feedback on; `debug off` turns it off;
  `debug help` lists the subcommands. The lines themselves are the `debug` strategy's
  (`PlayerbotAIConfig::CanLogAction` reads it), so `co +debug` / `co -debug` work the same way.
* `/w <bot> debug <remote>` (**no space before the subcommand** in the action name: the engine's own
  `debug <remote>` path answers these): `state`, `position`, `tpos`, `target`, `hp`, `combat`, `strategy`,
  `action`, `values`, `travel`, `traveldetail`, `budget`. `debug action` prints the last action the engine
  ran, which is the headless "action line".
* `/w <bot> log [debug|detail|minimal|basic|?]` sets or reads the bot's legacy log level.
* `/w <bot> set value group size <n>`, `guild size <n>`, `rpg style <n>` set the saved preferences.
* The debug subcommands that need the 1.12 client (fake spell visuals and `SpellAuraHolder` auras,
  transports and map-ray probes, travel-node readers, the position path planner, item/trade/mail
  readers) answer `debug <name>: not ported to ForeverCore (...)`; their files are not in the build.
  The engine text diagnostics (values, actions, strategies, combat facts, quest/level state) are real.

Server-side diagnostics:

* `BOTPERF` — one `TortoiseBots: BOTPERF passUs=... maxUs=... ticks=... coreMs=... bots=... engines=...
  owned=... mastered=... free=... pool=...` line in the `tortoisebots` log every 30 s of tick time while
  the module is enabled. `passUs`/`maxUs` are the module pass (the same window `.tb perf` reports), `coreMs`
  the rest of the world tick, and the six counts are read once from the live bot table.
* `action_counts.csv` — with `AiPlayerbot.ActionCountsLog = 1`, the engine counts every executed action and
  the module writes the cumulative snapshot every 5 minutes of tick time (first write truncates and writes
  the header; later writes append). The file lives under the engine's log directory.
* `CastCancel` rows in `bot_events.csv` — with `AiPlayerbot.RaidBossDiagnostics = 1` and `bot_events.csv` in
  `AiPlayerbot.AllowedLogFiles`, every module-side cast cancel writes one row (site, slot, spell, cast
  percent, target, distance moved).

## Add-on hint (#46)

A real player whose client never speaks the `TBM` addon prefix is told once where to get
`TortoiseBotsForeverManager`:

```
TortoiseBots: this server's bots have a control panel, the TortoiseBotsForeverManager add-on.
Get it at <TortoiseBots.Player.AddonHintUrl> (shown once).
```

* "Has the add-on" is one TBM message from that character in the session (`OnAddonMessageSent`,
  self-whisper or group; the bot sessions are never candidates). `WorldSession::IsAddonRegistered`
  cannot answer this: it is true for a session that registered no prefix at all.
* The line goes out `TortoiseBots.Player.AddonHintDelay` seconds after login (default 45, clamped
  10..300), at most 20 per world tick, as a system message. Bots and the console never see it;
  GameMasters and every higher security level are skipped.
* "Once" is per character and lives in the characters database: the table
  `tortoise_bots_player_flag` (`characters/2026_11_24_10_tb_player_flag.sql`), row `flag='addon-hint'`.
  The row is written when the hint is sent AND on the first TBM message of a character without it
  (a player who later removes the add-on is not told again). All rows due in one world tick are
  written in one statement. A missing table logs one warning and degrades to once per server run.
* Config (`tortoisebots.conf.dist`, Player section): `TortoiseBots.Player.AddonHint` (0 = off, the
  default; set 1 to enable), `TortoiseBots.Player.AddonHintDelay` (45),
  `TortoiseBots.Player.AddonHintUrl` (the TortoiseBotsForeverManager releases page by default; empty
  drops the "Get it at ..." clause). Only the engine build (`TORTOISEBOTS_SLICE1`) has it.

`.tb addonhint status|reset <character>` (GM, console yes):

| Command | Answer |
|---|---|
| `.tb addonhint status <character>` | `TortoiseBots: addonhint <Name>: told` (the row exists), `...: not-told pending` (a login is waiting for the delay) or `...: not-told -` |
| `.tb addonhint reset <character>` | deletes the row and the in-memory entries, then `TortoiseBots: addonhint <Name>: reset`; the character can be told again |

Refusals: `TortoiseBots: addonhint: usage: .tb addonhint status|reset <character>.` and
`TortoiseBots: addonhint: not-found <name>.`

## 4. Addon order window (`TBM` `order` verb)

`TBFM` (TortoiseBotsForeverManager) speaks the guild-order window over the addon channel. The module
answers the `order` verb (`forever/src/host/TbAddonOrders.cpp`) and advertises the capability tokens
`guild-orders`, `guild-order-nobuy` and `guild-order-steps`; `RegisterVerb` also advertises the verb
itself, so the addon routes the command natively instead of the `.tb` bridge.

Accepted payloads (the reply shapes are the ones `BotOrder.lua:57-75` parses):

| Payload | Answer |
|---|---|
| `order list` | `TBM:ORDER_BEGIN`, `TBM:ORDER_ACCESS`, one `TBM:ORDER` per row, `TBM:ORDER_END` |
| `order access` | one `TBM:ORDER_ACCESS` |
| `order rank <idx>` | `ACTION_ACK order rank:<idx> 1 rank`, then a fresh `TBM:ORDER_ACCESS` |
| `order reassign <bot> <newBot>` | `ACTION_ACK order bot:<bot> 1 reassign` |
| `order <bot> cancel` | `ACTION_ACK order bot:<bot> 1 cancel` |
| `order <bot> steps` | `TBM:ORDER_STEPS_BEGIN`, `TBM:ORDER_STEP` rows, `TBM:ORDER_STEPS_END` |
| `order <bot> preview <farm\|craft> <item> <n> [bring=..]` | one `TBM:ORDER_PREVIEW` |
| `order <bot> <farm\|craft> <item> <n> [hours\|inf] [keep] [gold=..] [bring=..]` | `ACTION_ACK order bot:<bot> 1 <farm\|craft>`, then a fresh list snapshot |
| `order search <bot> <farm\|craft> [cat=..] <text>` | `TBM:ORDER_ITEMS_BEGIN`, `TBM:ORDER_ITEM` rows, `TBM:ORDER_ITEMS_END` |
| `order search <bot> enchant <text>` | an empty `TBM:ORDER_ENCHANTS` pair |
| `order search <craft\|enchant> <text>` | `TBM:ORDER_WHO` rows (empty for enchant) |
| `order <bot> enchant <spell>` / `meet` | `ACTION_ERR` (enchant orders are not implemented) |

Rules:

* `order list` and `access` read the existing order store and access report; `search`, `who`, `preview`
  and `steps` replay the existing `.tb guildorder` read verbs through a capturing `ChatHandler` and
  reshape their stable records, so the two transports cannot drift. Mutations call the same store,
  access and reassign functions with the same permission gates (`MayGiveOrders`, `MayChangeBotRank`).
* Buy orders are refused and `guild-order-nobuy` is always advertised (owner rule: no Buy option for
  players). Enchant search answers an empty pair; enchant create/meet refuse.
* The store has no live `TBM:ORDER` push yet, so a newly created order is reported with a list snapshot
  right after the ACK; progress then follows the addon's 30 s `order list` poll.
* Fields the module's read records do not carry are sent as their protocol's "none" value, never
  invented: `zone`/`difficulty`/`chance`/`minLevel` on search items, `minutes`/`zone`/`difficulty` on the
  preview. A preview reagent's shortfall rides the `farm` lane (the order sources it), not the
  player-must-send lane.
* The `ORDER_ACCESS` gold bit comes from the bot rank's own `guild_rank.rights` withdraw-gold right and
  only when the guild has a bank tab.

## Dungeon finder fill (#160)

The 1.60 client has no dungeon finder (the core answers its dungeon list empty), so the fill keeps its own queue and
builds the party itself. A player queues as one role; every `AiPlayerbot.RandomBotLftUpdateInterval` (default 15000 ms,
clamped to 5000-60000) the fill adds one free host bot until the party has the composition the core's finder asks for:
one tank, one healer, three damage. The player's own role takes one of those slots, so a solo damage player gets a tank,
a healer and two damage bots. `AiPlayerbot.RandomBotLftMaxFillsPerInterval` (default 1, at most 5) caps the bots one
pass may add; with the defaults four bots take about a minute. `AiPlayerbot.RandomBotLftEnabled = 0` answers `disabled`
and groups nobody.

A candidate is a host bot in the world with a running engine, free (`BotMgr::IsFreeBot`: no master, not grouped), level
10 or more, of the player's team, not in a battleground or its queue, not teleporting, not on a taxi, not in combat and
alive. It fills a slot its own spec covers (`AiFactory::GetPlayerRoles`); with `AiPlayerbot.RandomBotLftAllowRoleBorrow
= 1` a bot of a class with a kit for the slot may borrow it (a warrior tank, a druid bear, a protection paladin). The
fill prefers a natural spec, then the smallest level gap to the player, then the lowest guid.

Each bot joins through `BotMgr::GroupWith` (the core's own invite/accept path; the player leads), gets the group's LFG
role bit (`Group::SetLfgRoles`, which the engine's role resolver reads) and its kit role (the `.tb role` commit, then
`follow`). When the player leaves by `.tb lfg leave`, logs out or leaves the party, every bot of the fill is ungrouped,
its role cleared and it plays free again. A bot taken over by someone else meanwhile is left where it is.

| Command | What it does |
|---|---|
| `.tb lfg join <dungeon id> <tank\|healer\|dps> [#token]` | queues the sender's own character; the sender must lead the party (or have none). Refused `not-ready` (the engine config is not loaded), `disabled`, `bad-role`, `no-such-dungeon` (not in the client's dungeon list), `already-queued`, `raid`, `not-leader`, `party-full`, `needs-player` (from the console) |
| `.tb lfg status [<player>] [#token]` | one `lfg` line per entry: player, dungeon id and name, role, `waiting|complete`, `need=` (tank first) and `filled=` |
| `.tb lfg leave [#token]` | releases the bots and drops the entry; `not-queued` when there is none |

Real output:
```
lfg-begin #f1
lfg player=Flivmage dungeon=39 dungeon_name=Stratholme role=dps state=waiting need=tank;healer;dps;dps filled= #f1
lfg-end count=1 records=1 state=queued entries=1 #f1
lfg-begin #f2
lfg player=Flivmage dungeon=39 dungeon_name=Stratholme role=dps state=waiting need=healer;dps;dps filled=tank:Mtank #f2
lfg-end count=1 records=1 state=ok entries=1 #f2
lfg-begin #f3
lfg player=Flivmage dungeon=39 dungeon_name=Stratholme role=dps state=complete need= filled=tank:Mtank;healer:Mpriest;dps:Mmage;dps:Mrogue #f3
lfg-end count=1 records=1 state=ok entries=1 #f3
```

The fill's `Server.log` lines (grep `LFG fill`):
```
TortoiseBots: LFG fill: Flivmage queued dps for dungeon 39 (Stratholme); need tank, healer, dps, dps
TortoiseBots: LFG fill: Mtank joins as tank (Warrior 60), party 2/5
TortoiseBots: LFG fill: party complete for Flivmage: tank=Mtank, healer=Mpriest, dps=Mmage,Mrogue
TortoiseBots: LFG fill: released Mtank (player left)
```

`.bot lfg` is the same service for the caller's own character: `TortoiseBots: lfg: queued dps for Stratholme (39); need
tank, healer, dps, dps`, `TortoiseBots: lfg: waiting for Stratholme (39) as dps; need ...; filled ...`,
`TortoiseBots: lfg: left; released 2 bot(s)`, and a refusal as `TortoiseBots: lfg: refused <code>: <text>`.

## Spell-school switch read-out (#48 round 1)

### `.tb school <bot|party [leader]> [#token]`
READ ONLY: what the school switch sees of one bot and its current target (the unit it is fighting, else the one it selected).
The switch itself is round 2; this round only answers the question, so a live test can prove the reads before anything casts.
`school-begin`, one `school` line per bot, `school-end`.

| Key | Meaning |
|---|---|
| `bot` | the bot's name |
| `tgt` | the entry of the target the reads are about, `0` when the bot has none |
| `immune` | the schools the target is immune to, bits `1 << school` (sum of the template row, the core's own immunity read and the data row) |
| `taken` | the damage the target takes from each school as a percent, seven numbers in school order `normal,holy,fire,nature,frost,shadow,arcane` (100 = plain damage; the Death Talon frost vulnerability reads above 100 on `frost`) |
| `main` | the main school of the bot's class and spec (the first row of `school-nukes.csv`), or `-` for a spec with no rows (every healer and melee spec) |
| `pick` | the school the switch would cast instead, or `-` to stay (also `-` while the switch is off) |
| `src` | where the statement came from: `core` (the template, an aura immunity or a damage-taken aura), `data` (`creature-schools.csv` is the only source) or `none` |

End line: `school-end count=<lines> records=<records> bots=<n> on=yes|no gain=<percent> target=<label> #token`.

```
school-begin #s1
school bot=Flivmage tgt=14887 immune=0 taken=100,100,100,100,150,100,100 main=frost pick=- src=core #s1
school-end count=1 records=1 bots=1 on=yes gain=10 target=Flivmage #s1
```

Config keys (`forever/conf/tortoisebots.conf.dist`): `TortoiseBots.SpellSchool.Enable` (default `0`; off = no bot ever
switches, and no spell name is looked up) and `TortoiseBots.SpellSchool.VulnerableMinPct` (default `10`; how many percentage
points above the main school's taken percent a fallback school must take before a caster leaves its main school; `0` to
`1000`).

## 24. Issue #308: the server line of sight, `.tb los <a> <b> [map=<id>]`

The agent's plan tools estimate sight with a flat navmesh ray, which disagrees with the server both ways. This command asks
the SERVER, with the same core call the bots' own attack-target visibility check makes, and answers with the distance so a
plan scout can record true sight for a pull stand. READ ONLY: no movement, no target change, no combat state is touched.

### The sides

Each side is one word:

| Side | Form | Notes |
|---|---|---|
| a character or bot | `Mtank` | an in-world character by name (`FindOnline`); the console may name any of them |
| the caller's selection | `target` | refused as `bad-argument` when the caller has no selection (the console has none) |
| a guid | `0000000000000000000000000000002A` or `0x0000000000000000000000000000002A` | the 32 hex digits the module prints (`.tb state`), or the core's `0x` form. A player is found in the whole world; a creature on the caller's map, then on the other side's map |
| a position | `-601.25,-4250.5,38.9` | three numbers `x,y,z` on the other side's map |

Two positions are on no side's map, so a position-position query needs `map=<id>` (1-65535: the loaded copy of that map,
instance 0 or else the lowest instance id; the answer comes from that copy, so a position-position query cannot name
another instance). `map=` with a unit side is refused: the unit's own map and instance are the ones queried, and a
position side borrows them.

### The answer

ONE line, whatever happened (this format is a stable interface for tools):

```
los a=<name|guid|x,y,z> b=<...> map=<id> los=<0|1> dist=<yards, one decimal> same_map=<0|1> in_world=<0|1> [reason=<code>]
```

| Key | Meaning |
|---|---|
| `a`, `b` | the side as resolved: a unit's name, a guid's 32 hex digits when it did not resolve, the word as typed for an unresolved name or `target`, `x,y,z` with two decimals for a position |
| `map` | side a's map id (a position-position query: the `map=` word); empty when no side was placed |
| `los` | `1` when the server's line of sight between the two is clear, else `0` (also `0` on a refusal) |
| `dist` | the horizontal distance in yards, one decimal, with height never added: between units the number the bots' attack gate compares (`GetDistance2d`, both combat reaches subtracted), between a unit and a position only the unit's combat reach subtracted, between two positions the plain centre-to-centre distance; empty on a refusal before both sides were placed |
| `same_map` | `1` only when both sides are on one map AND in the same instance of it; `0` for two copies of one dungeon; empty otherwise |
| `in_world` | `1` when both sides are in the world (a position: its map is loaded), else `0`; empty when a side was not resolved |
| `reason` | present only on a refusal: `bad-argument`, `unknown-unit` (GameMaster and console only), `not-in-world`, `different-maps` (a different map, or two instances of the same map), `no-access` or `rate`; on `no-access` the world keys are empty and only the typed words show |

The caller's `#token` is the last word, as everywhere else. A refusal does not print `error`: the `los` line is the answer,
so a tool reads the same record either way.

```
los a=Mtank b=Kobold map=389 los=1 dist=12.3 same_map=1 in_world=1
los a=Mtank b=00000000F1300E80000000000000002A map=389 los=0 dist=31.7 same_map=1 in_world=1
los a=1.00,2.00,3.00 b=4.00,5.00,6.00 map=389 los=0 dist=4.2 same_map=1 in_world=1
los a=Nobody b=Mtank map= los=0 dist= same_map= in_world= reason=unknown-unit
```

### Which core call, and why it is the bots' answer

`a` and `b` as units: `IsWithinLOSInMapCompat(a, b, true)` — the shared compat pair over the core's
`WorldObject::IsWithinLOSInMap(to, LINEOFSIGHT_ALL_CHECKS, VMAP::ModelIgnoreFlags::Nothing)` (ForeverCore
`Object.h:364`). That is exactly the filter behind "I dont see my rti attack target": the `rti target` value drops a marked
unit when `bot->IsWithinLOSInMap(unit)` is false (`ai/playerbot/strategy/values/RtiTargetValue.h:97`), and the action then
answers that line (`ai/playerbot/strategy/actions/AttackAction.cpp:163`). The flags are the core's default:
`LINEOFSIGHT_ALL_CHECKS` = static VMAP geometry plus game-object models (when the server's gobject-LOS option is on), with
no model ignored. The core's height offsets apply: the ray starts and ends at a player's z + collision height, or at a
creature's hit-sphere point toward the other side (`Object.cpp:534-558`).

A unit and a position: `IsWithinLOSCompat(unit, x, y, z, true)` — the bots' own point check (movement, flee and stance
code use it), cast FROM the unit, so the unit's height offsets apply at its end. Two positions:
`IsInLineOfSightCompat(map, x1, y1, z1, x2, y2, z2, true)`, the same core call without a viewer; its phase is the empty
one, and a stretch of map that is not loaded answers `los=0` (no data is never read as clear).

The query answers GEOMETRY only. The attack gate's other rules (distance, friendliness, dead, evade, aura immunity) are not
applied; `dist=` is there so the caller can compare with its own sight distance.

### Who may ask

The console and a GameMaster (security level 2 and up) always. Anyone else only for a unit of their own — their own
character, or a host bot in a run or party they lead or observe, through the shared `.tb run` checks
(`CheckRunObserverAccess` / `CheckPartyLeadAccess`) — and never for another player's character: that side answers
`no-access`, so the command cannot be used to probe players. A creature is nobody's, and two positions name no unit at all,
so a non-GM caller needs at least one unit of its own in the query.

A caller below GameMaster cannot tell an online foreign character from a name that resolved to nothing: both answer the
same `no-access`, and a `no-access` answer shows only the words it typed — `a=`, `b=` as typed, and `map=`,
`same_map=`, `in_world=` empty — never a resolved name, a map id or a world state. So the command is no presence, map or
guid oracle. A GameMaster and the console keep the precise `unknown-unit`.

Rate: five queries a second per caller (a five-token bucket, one token back every 200 ms). A caller is a character; the
console and SOAP have none, so every tool call shares one bucket and together they get five a second. The sixth answers
`reason=rate`; a refused query still costs its token, so a spammer cannot mint answers faster than five a second.
