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

## 3. Reply grammar, version 2 (every tokened reply of the commands below)

Lead decision 2026-11-20; the one encoder is `Enc`/`Rec`/`Reply` in `forever/src/host/TbH3.cpp`, and `.tb grammar selftest` prints fixed records so a client can test its parser against the module.

* **Lines.** A reply line is words separated by ONE space. The first word is the record kind (`state`, `party`, `threat`, `order`, `role`, `master`, `learn`, `equip`, `itemscan`, `error`). Every other word is `key=value`.
  The LAST word of every line of a tokened command is `#<token>` (the caller's last word of the command, `#` and 1 to 32 characters of `A-Za-z0-9_.:-`; a malformed one is refused with `token=rejected` on the error line).
* **Multi-line replies** start with `<kind>-begin #token` and end with `<kind>-end count=<lines between> ... #token` (`count` counts every record line, continuation lines included).
  A one-line reply has neither: an `error` line, `.tb equip`, and the answer of `order`, `role` and `master` addressed to ONE bot. A party-addressed order is `order-begin`, one `order` line per bot, `order-end`.
* **Keys** are `[a-z][a-z0-9_]{0,23}`; a key appears at most once per line.
* **Values are percent-encoded**: every byte outside `[A-Za-z0-9._-]` is written `%XX` (upper-case hex): space `%20`, `=` `%3D`, `:` `%3A`, `;` `%3B`, `,` `%2C`, `#` `%23`, `|` `%7C`, `%` `%25`, `+` `%2B`; `_` stays literal.
  **An empty value is the empty string** (`tgt=`); the word `none` is never written to mean empty. A missing optional key means "not known" (for example `manapct` is absent for a bot that has no mana), never zero.
* **Structure** inside a value uses characters the encoding never produces for data: `;` separates list items, `:` separates the parts of one item, `,` separates coordinates. Each part is encoded on its own:
  `co=close;protection;tank%20assist`, `top=Mtank:343;Mdpsb:338`, `pos=-9100.8,-2005.3,132.9`, `ord=41:ok`, `target=party:Mtank`, `class=2:8`.
* **Numbers** are decimal, **booleans** `yes`/`no`.
* **Length.** A line is at most 3,000 bytes (the token included). A record that is longer continues on `<kind>+ <first key=value of the record> <more keys> #token` lines. A single value longer than a line (a very long strategy list) is cut at a list item.
  Free text (`msg=`) is cut to 400 bytes before encoding.
* **Errors**: `error code=<word> cmd=<word> msg=<encoded text> #token`, one line, nothing else for that command. A per-bot refusal inside an accepted command is the same words on the bot's own line: `order bot=.. verb=.. status=refused code=.. msg=..`.
  Codes: `engine-not-built`, `missing-target`, `no-such-player`, `not-a-bot`, `no-party`, `no-bots`, `ambiguous-party`, `missing-verb`, `unknown-verb`, `missing-argument`, `unexpected-argument`, `bad-argument`, `bad-target`, `bad-role`, `bad-id`, `too-many`,
  `no-engine`, `queue-full`, `not-in-group`, `needs-player-sender`, `no-selection`, `mark-empty`, `no-such-spawn`, `target-not-found`, `target-dead`, `not-attackable`, `no-pull-strategy`, `no-commander`, `other-map`, `too-far`, `nothing-to-assist`, `self-master`, `missing-commander`,
  `missing-leader`, `duplicate`, `party-refused`, and `action-not-registered` (with `name=<the legacy action key>`). A refusal by the core itself (no permission, unknown command) is the core's own untagged text, not this grammar.
* **Addressing.** `<bot>` is a character name (a logged-in host bot). `party <leader>` is every host bot in the party of that character. `party` with no leader is, in this order: the bots bound (`.tb master`) to the sender, or to the one commander there is, or the members of the one bot-led party there is;
  anything else needs the leader's name (`error code=ambiguous-party`). Names are case-insensitive. The `target=` of a reply is `<name>`, `party:<leader>` or `party:commander:<name>` as parts.
* **In a build without the engine** every command below (and `grammar`) answers `error code=engine-not-built cmd=<command> msg=...`.
* **GUID text.** `32 uppercase hex digits`: the two 64-bit words of the `ObjectGuid`, high word first, as `tgt=`, `victim=` and `mob=` print them and as `.tb order ... attack|pull` reads them (a leading `0x` is accepted); empty for none. Another way to name a creature: `spawn:<database spawn id>` (a creature of the bot's map).

### `.tb grammar selftest [#token]`
Prints `selftest-begin`, six `selftest` records (a value holding space, `=`, `:`, `;`, `#`, `%`, `_`, `,`, `+`, `|` and a two-byte character; a list and a list of parts with an empty item and a `%`; coordinates, a boolean, a number and a fraction; a key given twice (the second wins); an over-long list that
shows the 3,000-byte cut and its `selftest+` continuation) and `selftest-end count=6`. Real output of boot 5 of package L:
```
selftest-begin #g1
selftest case=specials value=a%20b%3Dc%3Ad%3Be%23f%25g_h%2Ci%2Bj%7Ck%C3%A9 empty= #g1
selftest case=list items=x%20y;a%3Bb;;50%25 pairs=Mtank:343;a%3Ab:1 #g1
selftest case=coords pos=-601.2,-4250.1,38.9 flag=yes number=-17 fraction=12.35 #g1
selftest case=once key=second #g1
selftest case=overlong list=strategy%20number%200;strategy%20number%201;...(cut at 3,000 bytes) #g1
selftest+ case=overlong tail=x #g1
selftest-end count=6 note=fixed%20records #g1
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
| `pull` | the same targets | `pull my target` with the unit's guid as the event object (tank only), then the party hold (package L: the other bots wait until the pull LANDED and the pack is on the tank: 3 s at the earliest, 10 s at the latest, at once when a mob hits them or the tank is under 60 %, 15 s after the order if it never lands) | as `attack`, and the bot has no `pull` combat strategy |
| `endpull` (or `end pull`) | - | `end pull` | - |
| `hold` | - | strategy `+passive` in combat and non-combat (`hold=yes` in the state line) | the strategy is not registered |
| `free` | - | `ResetStrategies`, movement `follow`, the explicit attack target cleared | - |
| `assist` | a party member's name | one shot: `attack` on what that member is attacking when the order runs | the member is not in the party, or has no attackable victim now |
| `co` / `nc` | `+name,-name` | `ChangeStrategy(arg, combat / non-combat)`: a debug switch | characters other than letters, digits, space, `+ - , _` |

One bot (a one-line reply) and a party (begin/end):
```
order bot=Mdpsa verb=follow seq=1 status=queued #o9
order-begin #o10
order bot=Mtank verb=stay seq=2 status=queued #o10
order bot=Mpriest verb=stay seq=3 status=queued #o10
order bot=Mdpsa verb=stay seq=4 status=queued #o10
order bot=Mdpsb verb=stay seq=5 status=queued #o10
order bot=Mdpsc verb=stay seq=6 status=queued #o10
order-end count=5 verb=stay target=party:commander:Mcmd bots=5 queued=5 refused=0 #o10
order bot=Mdpsa verb=assist status=refused code=nothing-to-assist msg=Mtank%20has%20no%20attackable%20victim%20or%20target%20now #o11
error code=unknown-verb cmd=order msg=bogus #o12
```
`seq` numbers orders over the whole server; the bot's last one is `ord=` in its state line. An order older than 15 s that the engine could not run is dropped with `fail:expired`.

### `.tb role <bot|party [leader]> tank|healer|dps|none [#token]`
Stores the role (`PlayerbotAIStorage::SetPlayerForcedRole`) and in the bot's next engine tick sets the AI's forced role and rebuilds the kit. One bot: `role bot=Mdpsb role=dps seq=7 status=queued #r9` (`status=stored` when it has no engine yet). A party: `role-begin`, the lines, `role-end count=.. role=.. target=.. bots=.. queued=..`.

### `.tb master <bot|party [leader]> <commander|none> [#token]`
Binds the bots to a commander who stands OUTSIDE their party (decision D2): host master and engine master become the commander, `IsInCommandedGroup` answers true for them, and a commanded bot keeps that master (the engine's adoption step skips it) and follows it. `none` releases them.
Line: `master bot=Mtank commander=Mcmd commander_in_party=no seq=44 status=queued`; party: begin/end with `master-end count=.. target=.. commander=.. bots=.. queued=..`.

### `.tb party <leader> <member>... [#token]` (forms) and `.tb party <leader> [#token]` (roster)
Forming: up to four host bots join the leader's party; each bot's HOST master becomes the leader: run `.tb master` afterwards. Replies `party-begin`, `party-join bot=.. leader=.. status=joined|refused [code= msg=]`, `party-end count=.. leader=.. joined=.. asked=.. note=..`.
Roster (real output):
```
party-begin #p9
party member=Mtank leader=yes online=yes bot=yes class=warrior level=18 role=tank engine=yes master=Mcmd commanded=yes #p9
party member=Mpriest leader=no online=yes bot=yes class=priest level=18 role=healer engine=yes master=Mcmd commanded=yes #p9
...
party-end count=5 leader=Mtank bots=5 raid=no commander=Mcmd #p9
```
(`commander=` is empty, one name, or `mixed`; an offline member has only `member`, `leader`, `online=no`.)

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
| `hold` `mv` | the passive strategy is on; movement `follow` / `stay` or empty |
| `stuck` | empty; `blocked:<reason>`; `away:<seconds left>`; `noengine` |
| `engine` | a PlayerbotAI runs the bot |
| `pos` `map` `inst` | `x,y,z` with one decimal; map id; instance id |
| `dl` `dm` | yards to the party leader / to the master (`self`, `othermap`, a number, empty when there is none) |
| `ord` `lastorder` | `<seq>:<ok\|queued\|fail%3A<code>\|none>` of the last order, and its verb (empty for none) |
| `co` `nc` | combat and non-combat strategies, a `;` list |
| `cast_queued` `cast_delivered` `cast_dropped` `cast_pending` | the cast-result queue counters (absent without a bot session) |
| `eating` `drinking` | a seated food / drink aura is on the bot |
| `resting` `rest_why` | the rest gate: `resting=no` means out of the old rest conditions; `rest_why` is a `;` list of `dead`, `combat`, `hp`, `mana`, `eating`, `drinking` |

End line: `state-end count=<lines> bots=<n> resting=<number of bots resting> ready=yes|no [why=<reason>:<bot>;...] marks=<set raid marks> target=<label> #token`.
**ready** = every bot is alive, out of combat, not resting, within follow range of its master (10 yd; 18 yd for a healer) and not blocked or away. `why` lists `<reason>:<bot>` items, reason one of `dead`, `combat`, `resting`, `far`, `blocked`.

Real output (one bot):
```
state-begin #t1
state bot=Tank role=tank rolesrc=forced class=warrior level=18 hp=708 hpmax=708 hppct=100 power=rage pw=0 pwmax=100 st=non-combat incombat=no tgt= tgtentry= tgthp= victim= hold=no mv=follow stuck= engine=yes pos=-9100.0,-2005.0,131.0 map=0 inst=0 dl=self dm=0.0 ord=0:none lastorder= co=close;protection;pull;tank%20assist;tank%20face nc=default;follow;food;protection;tank%20assist cast_queued=0 cast_delivered=0 cast_dropped=0 cast_pending=0 eating=no drinking=no resting=no rest_why= #t1
state-end count=1 bots=1 resting=0 ready=yes marks=0 target=Tank #t1
```
(names in the sample are the live test's; the structure is exact.)

### `.tb threat <bot|party [leader]> [#token]`
READ ONLY (`GetSortedThreatList`, `GetLastVictim`). `threat-begin`, one line per mob that has one of the bots on its threat list, `threat-end count=<lines> mobs=<n> bots=<n> target=<label>`:
`threat mob=<GUID text> entry=426 name=Redridge%20Brute hppct=100 victim=Mtank target=Mtank size=3 top=Mtank:343;Mdpsb:338;Mdpsc:196 #token` (`victim` is the mob's current threat victim, `target` what the mob is fighting, `top` the first three of the sorted list).

## 6. Test kit
* `.tb learn <bot> <spell id>...`: `learn-begin`, `learn bot=.. spell=<id> result=<learned|known|unknown|invalid|not-learned> name=..`, `learn-end count=.. bot=.. learned=..`. Core path: `SpellMgr::IsSpellValid`, `Player::LearnSpell`. At most 40 ids.
* `.tb equip <bot> <item id> [replace]`: ONE line `equip bot=.. item=<id> name=.. result=<equipped|refused> [slot=..] why=<empty|EQUIP_ERR_...|unknown-item|bags-full|swap-failed> [code=..] [replaced=yes] [note=..]`.
  `Player::CanEquipNewItem` then `Player::EquipNewItem`; a taken slot refuses (`EQUIP_ERR_NOT_EQUIPPABLE` or `EQUIP_ERR_NO_SLOT_AVAILABLE`); with `replace` the item is stored in the bags and the core's `Player::SwapItem` puts it on.
* `.tb itemscan <bot> <item id>...`: `itemscan-begin`, per id `itemscan bot=.. item=<id> exists=<yes|no> [name= class=<c>:<sc> inv= req= quality= ilvl= canequip=<yes|no> slot=<n, only when yes> why=<empty|EQUIP_ERR_...>]`, `itemscan-end count=.. bot=.. exist=.. canequip=..`.
  `canequip` is the core's check for THIS bot with swap = true. Nothing is created or changed. The ids come from the data collaborator's list; the module holds no id.

## 7. Config keys

`TortoiseBots.Slice.RestHealthPct` (default 90) and `TortoiseBots.Slice.RestManaPct` (default 80), `forever/conf/tortoisebots.conf.dist`; read on each `.tb state`.
