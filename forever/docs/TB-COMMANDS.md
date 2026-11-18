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

## 2. The older commands (free text, human readable)

| Command | What it does |
|---|---|
| `.tb status` | module enabled?, number of bot sessions |
| `.tb create <account> <name> <race id> <class id> [gender]` | a new level 1 character for a bot |
| `.tb login <character> [master]` | the character joins the world as a bot; follows `master` (from a player: the sender) |
| `.tb logout <character>` | the bot leaves |
| `.tb group <bot> <master>` / `.tb ungroup <bot>` | the bot joins the master's party (the master leads) / leaves it |
| `.tb raid <master>` | converts the master's party to a raid (a test helper for the "no raid" rule) |
| `.tb engine <character> on\|off` | the bot runs the real engine (PlayerbotAI) or the spike behaviour |
| `.tb where <character>` | position, health, money, brain, strategies, target, cast-result counters |
| `.tb go <bot> <dx> <dy>` / `.tb hop <bot> <dx> <dy>` | a bot without a master walks / is teleported that many yards east and north |
| `.tb attack <bot>` | the spike's attack of the nearest hostile within 40 yd |
| `.tb enter <character> <map id>` | what `Map::PlayerCannotEnter` says |
| `.tb cast <bot> <spell id>` | the core's own cast on the sender's target |
| `.tb castfail <bot> <spell> <reason> [interrupt]`, `.tb castlog <bot> [on\|off]` | inject / read cast results |
| `.tb level <bot> <1-60>`, `.tb train <bot>`, `.tb hurt <name> <pct>`, `.tb power <bot> mana\|rage\|energy <pct>`, `.tb rage <bot> <points>` | test setup |
| `.tb additem <bot> <item id> [count]`, `.tb use <bot> <item id> [self]` | items |
| `.tb spells <bot>`, `.tb spell <id>`, `.tb spellscan`, `.tb item <id>` | what the spell and item mirrors hold |
| `.tb perf`, `.tb unported` | tick cost window / the unported-feature counters |

## 3. Common grammar of the slice-3 commands

* **Addressing.** `<bot>` is a character name (a logged-in host bot). `party <leader>` is every host bot in the party of that character. `party` with no leader
  is, in this order: the bots bound (`.tb master`) to the sender, or to the one commander there is, or the members of the one bot-led party there is; anything else
  needs the leader's name (`error=ambiguous-party`). Names are case-insensitive.
* **Token.** The last word of a command may be `#<token>` (1 to 32 characters of `A-Za-z0-9_.:-`). It is echoed as the last word of EVERY reply line of that
  command, including the end line and errors. Match a reply to its command by it. A malformed `#...` word is refused (`token=rejected` on the error line).
* **Lines.** Every reply line is `<kind> key=value key=value ... [#token]`. `<kind>` is the first word and never contains `=`. A bot's line begins `state`, `order`,
  `role`, `master`, `party`, `party-join`, `threat`, `learn`, `equip`, `itemscan`; the last line of a command is `<command>-end ...` (`state-end`, `order-end`, ...)
  except errors. A client waits for the `-end` line (or an `error=` line) of its token.
* **Values** never contain a space, `=`, `|` or `#` and have no control characters: space -> `_`, `=` -> `:`, `|` -> `/`, `#` -> `+`, an empty value is `none`.
  Names of spells, items, units and strategies are escaped this way (`Redridge_Brute`, strategy `tank_assist`). Lists use `+` between items (strategies) or `,`
  between `name:number` pairs (threat). Numbers are plain decimal; fractions have one decimal.
* **Errors.** `error=<code> cmd=<command> [msg=<escaped text>] [name=<escaped key>] [token=rejected] [#token]`, and nothing else for that command. Per-bot refusals inside an
  accepted command are the same words on the bot's own line: `order bot=Tbtank verb=pull status=refused error=<code> msg=...`.
  Codes: `engine-not-built`, `missing-target`, `no-such-player`, `not-a-bot`, `no-party`, `no-bots`, `ambiguous-party`, `missing-verb`, `unknown-verb`, `missing-argument`,
  `unexpected-argument`, `bad-argument`, `bad-target`, `bad-role`, `bad-id`, `too-many`, `no-engine`, `queue-full`, `not-in-group`, `needs-player-sender`, `no-selection`,
  `mark-empty`, `no-such-spawn`, `target-not-found`, `target-dead`, `not-attackable`, `no-pull-strategy`, `no-commander`, `other-map`, `too-far`, `nothing-to-assist`,
  `self-master`, `missing-commander`, `missing-leader`, `duplicate`, and `action-not-registered` (with `name=<the legacy action key>`: the order needs an engine action this build
  does not register yet).
* **In a build without the engine** (`TORTOISEBOTS_SLICE1` off) every slice-3 command answers `error=engine-not-built cmd=<command> ...`.
* **GUID text.** `32 uppercase hex digits`: the two 64-bit words of the `ObjectGuid`, high word first (`GetRawValue(1)` then `GetRawValue(0)`), as `tgt=` and `victim=` print them and
  as `.tb order ... attack|pull` reads them (a leading `0x` is accepted). `none` is the empty guid. Another way to name a creature: `spawn:<database spawn id>` (a creature of the bot's map).

## 4. Orders

### `.tb order <bot|party [leader]> <verb> [argument] [#token]`

Each bot of the address gets the order QUEUED at once; the engine runs it right before that bot's next tick (so within one world tick), through the legacy action. The reply
is the queue answer, not the result. The result shows in `.tb state` as `ord=<seq>:<ok|fail:<code>>` (and in the server log for a failure).

| Verb | Argument | What runs (legacy action key) | Refused when |
|---|---|---|---|
| `follow` | none \| `none` \| a party member's name | `follow chat shortcut`; with a name: `follow target` (param = name) first; `none` clears the manual follow target | the name is not another member of the bot's party |
| `stay` | - | `stay chat shortcut` | - |
| `come` | - | writes the stay position = where the SENDER stands (the bound commander from the console), `stay` movement, a fresh stay walk (the engine's own `return to stay position` trigger walks) | different map, or further than the engine's react distance (`too-far`) |
| `attack` | `my` \| `skull`/`star`/... \| `mark` \| `mark:<word>` \| `spawn:<id>` \| 32-hex guid | the unit is put on the bot's rti mark (the bot's `rti` value, default skull) for the call, `attack rti target` runs, the marks are put back | no such unit, dead, not attackable, the bot is in no party; `my` from the console |
| `pull` | the same targets | `pull my target` with the unit's guid as the event object (tank only) | as `attack`, and the bot has no `pull` combat strategy |
| `endpull` (or `end pull`) | - | `end pull` | - |
| `hold` | - | strategy `+passive` in combat and non-combat (`hold=yes` in the state line) | the strategy is not registered (`fail:strategy-not-registered:passive` as the result) |
| `free` | - | drops every order: `ResetStrategies` (the factory kit; also ends a running pull window), movement `follow`, the explicit attack target cleared | - |
| `assist` | a party member's name | one shot: `attack` on what that member is attacking (or has targeted) when the order runs | the member is not in the party, or has no attackable victim now |
| `co` / `nc` | `+name,-name` | `ChangeStrategy(arg, combat / non-combat)`: a debug switch | characters other than letters, digits, space, `+ - , _` |

`my` is the selection of the player who sent the command, read when the command runs. The raid marks are the group's own state: the order sets one, runs the action
and restores all eight, so no mark of the GM's is left changed.

Reply:
```
order bot=Tbtank verb=pull seq=41 status=queued #t7
order bot=Tbdps1 verb=pull status=refused error=no-pull-strategy msg=the_bot_has_no_`pull`_combat_strategy_(give_it_the_tank_role) #t7
order-end verb=pull target=party:Tbtank count=5 queued=1 refused=4 #t7
```
`seq` numbers orders over the whole server; the bot's last one is `ord=` in its state line. A refusal never queues anything for that bot. An order older than 15 s that the
engine could not run (the bot was teleporting, or its master was away) is dropped with `fail:expired`.

### `.tb role <bot|party [leader]> tank|healer|dps|none [#token]`

Stores the role (`PlayerbotAIStorage::SetPlayerForcedRole`, which `PlayerbotAI::IsTank/IsHeal` and `AiFactory::GetPlayerRoles` read first, whatever the master is), and in the
bot's next engine tick sets the AI's forced role and rebuilds the kit (`ResetStrategies`; the movement mode stays). A bot with no engine yet stores it and gets it at its first tick.
```
role bot=Tbtank role=tank seq=40 status=queued #t3      (status=stored: no engine yet)
role-end role=tank target=Tbtank count=1 queued=1 #t3
```

### `.tb master <bot|party [leader]> <commander|none> [#token]`

Binds the bots to a commander who stands OUTSIDE their party (decision D2): the host's master guid and the engine's master pointer become the commander
(`BotManager::BindBotMaster`), and the host answers `IsInCommandedGroup(bot) == true` for them, so the engine's "remove bot masters not in our group" rule keeps a console
(bot-session) commander. Forms: `.tb master Tbdps1 Tbcmd`, `.tb master party Tbtank Tbcmd` (three words: the party of Tbtank), `.tb master party Tbcmd` (two: the one bot party).
`none` releases them. A running engine rebuilds its kit at its next tick.
```
master bot=Tbtank commander=Tbcmd commander_in_party=no seq=44 status=queued #t4
master-end target=party:Tbtank commander=Tbcmd count=5 queued=5 #t4
```
Consequence of the "commanded" answer (engine reads of `IsInCommandedGroup`): the commanded threat back-off for the damage bots (`AiFactory.cpp:633-642`, `ThreatStrategy.cpp:264`), no
open-world non-combat strategies (`AiFactory.cpp:1158`), a game object is opened only on an order (`LootAction.cpp:105`, `LootObjectStack.cpp:763`), the bot stays when its master leaves the
party (`LeaveGroupAction.h:51`), the action log scope `Commanded` (`BotActionLog.cpp:160`).

### `.tb party <leader> <member>... [#token]` (forms) and `.tb party <leader> [#token]` (roster)

Forming: up to four host bots join the leader's party through the core's Group API (`BotMgr::GroupWith`, the rules of slice 1: no raid, five at most). Each bot's HOST master becomes
the leader: run `.tb master` afterwards to bind the commander.
```
party-join bot=Tbpriest leader=Tbtank status=joined #t5
party-formed leader=Tbtank joined=4 asked=4 note=host_master_is_the_leader_run_.tb_master_next #t5
```
Roster:
```
party member=Tbtank leader=yes online=yes bot=yes class=warrior level=18 role=tank engine=yes master=Tbcmd commanded=yes #t6
party-end leader=Tbtank count=5 bots=5 raid=no commander=Tbcmd #t6
```
(`commander=none|<name>|mixed`; an offline member has only `member=`, `leader=`, `online=no`.)

## 5. State

### `.tb state <bot|party [leader]> [#token]`

One line per bot, then the end line. Keys in this order (all always present):

| Key | Meaning |
|---|---|
| `bot` `role` `rolesrc` `class` `level` | name; `tank`/`healer`/`dps`/`none` (the engine's `IsTank`/`IsHeal`); `forced` (set with `.tb role`) or `auto`; lower-case class; level |
| `hp` `hpmax` `hppct` | health, maximum, percent |
| `power` `pw` `pwmax` `manapct` | main power (`mana`, `rage`, `energy`, ...), value, maximum (RAGE IN RAGE POINTS, the core's units divided by 10), mana percent or `none` |
| `st` `incombat` | the engine's state `combat` / `non-combat` / `dead`; core's in-combat flag `yes`/`no` |
| `tgt` `tgtentry` `tgthp` | the engine's current target: GUID text, creature entry, its health percent (`none` when it has none) |
| `victim` | the unit the core says the bot is fighting (`Unit::GetVictim`), GUID text or `none` |
| `hold` `mv` | `yes` when the passive strategy is on; movement `follow`/`stay`/`none` |
| `stuck` | `none`; `blocked:<reason>` (the master is on a map the bot cannot enter); `away:<seconds left>` (the master logged out, grace running); `noengine` |
| `engine` | `yes` when a PlayerbotAI runs the bot |
| `x` `y` `z` `map` `inst` | position (one decimal), map id, instance id |
| `dl` `dm` | distance in yards to the party leader / to the master (`self`, `othermap`, `none`) |
| `ord` `lastorder` | `<seq>:<queued\|ok\|fail:<code>\|none>` of the bot's last order, and its verb |
| `co` `nc` | combat and non-combat strategies, `+` separated, spaces as `_`, at most 150 characters each (then `...`) |
| `cast_queued` `cast_delivered` `cast_dropped` `cast_pending` | the cast-result queue counters of the bot's session (SLICE2-R3-NOTES section 9) |
| `eating` `drinking` | `yes` when a seated food / drink aura is on the bot (`PlayerbotAI::HasSeatedMeal`) |
| `resting` `rest_why` | **the rest gate.** `resting=no` means READY. `resting=yes` when the bot is dead, in combat, below `TortoiseBots.Slice.RestHealthPct` (90) of its health, below `RestManaPct` (80) of its mana (mana users only), eating or drinking; `rest_why` lists the reasons joined by `+` (`dead`, `combat`, `hp`, `mana`, `eating`, `drinking`) or `none` |

End line: `state-end count=<n> resting=<number of lines with resting=yes> target=<label>`. The party is rested when `resting=0` on it.

Example (one line, here wrapped; real lines are one):
```
state bot=Tbtank role=tank rolesrc=forced class=warrior level=18 hp=612 hpmax=612 hppct=100 power=rage pw=0 pwmax=100 manapct=none st=non-combat incombat=no tgt=none tgtentry=none tgthp=none
 victim=none hold=no mv=follow stuck=none engine=yes x=-9100.2 y=-2060.4 z=128.1 map=0 inst=0 dl=self dm=3.4 ord=41:ok lastorder=pull co=protection+tank_assist+pull+close+tank_face
 nc=protection+tank_assist+follow+food+default cast_queued=12 cast_delivered=12 cast_dropped=0 cast_pending=0 eating=no drinking=no resting=no rest_why=none #t1
state-end count=1 resting=0 target=Tbtank #t1
```

### `.tb threat <bot|party [leader]> [#token]`

READ ONLY (`ThreatManager::GetSortedThreatList` and `GetLastVictim`; nothing that adds, scales, taunts, resets or re-selects). One line per mob that has one of the bots on its threat list:
```
threat mob=<GUID text> entry=426 name=Redridge_Brute hppct=100 victim=Tbtank target=Tbtank size=3 top=Tbtank:412,Tbdps1:330,Tbdps2:290 #t2
threat-end count=3 bots=5 target=party:Tbtank #t2
```
`victim` is the mob's current threat victim (`GetLastVictim`), `target` what the mob is fighting now, `top` the first three of the sorted list (highest first), `size` the list length.

## 6. Test kit

* `.tb learn <bot> <spell id>... [#token]`: for each id `learn bot=.. spell=<id> result=<learned|known|unknown|invalid|not-learned> name=<spell name>`; then `learn-end bot=.. count=.. learned=..`.
  The core's own path: `SpellMgr::IsSpellValid`, `Player::LearnSpell`. An id the core does not know is `unknown`. At most 40 ids.
* `.tb equip <bot> <item id> [replace] [#token]`: one line `equip bot=.. item=<id> name=.. result=<equipped|refused> slot=<equipment slot> why=<none|EQUIP_ERR_...|unknown-item|...> [code=<n>]`.
  `Player::CanEquipNewItem` (class, level, skill, slot, two-hand, combat checks) then `Player::EquipNewItem`; a taken slot refuses with `EQUIP_ERR_NO_SLOT_AVAILABLE`; with `replace` the item is
  stored in the bags and the core's own `Player::SwapItem` puts it on (the old piece goes to the bags). `why` is the core's own `InventoryResult` name.
* `.tb itemscan <bot> <item id>... [#token]`: per id `itemscan bot=.. item=<id> exists=<yes|no> [name=.. class=<c>/<sc> inv=<inventory type> req=<template required level> quality=<q> ilvl=<n> canequip=<yes|no> slot=<n|none> why=<none|EQUIP_ERR_...>]`;
  then `itemscan-end bot=.. count=.. exist=.. canequip=..`. `canequip` is the core's check for THIS bot with swap = true (a taken slot does not count against it). Nothing is created or changed.
  The ids come from the data collaborator's list (`forever/data/waterys/rfc/gear-level18.csv`); the module holds no id. `.tb kit` does not exist on purpose.

## 7. Config keys

`TortoiseBots.Slice.RestHealthPct` (default 90) and `TortoiseBots.Slice.RestManaPct` (default 80), `forever/conf/tortoisebots.conf.dist`; read on each `.tb state`.
