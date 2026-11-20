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
| `.tb lootnear <bot>` | test helper: the bot takes every item it may take from dead creatures within 10 yd through the core's `Player::StoreLootItem` (the item-loot path and its hooks, without the engine) |
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
  The LAST word of every line of a tokened command is `#<token>` (the caller's last word of the command, `#` and 1 to 32 characters of `A-Za-z0-9_.:-`; a malformed one is refused with a note in the error's `msg=`).
* **Markers.** ONLY `error` is a one-line reply. Every other tokened reply, an order's answer to one bot included, starts with `<command>-begin #token` and ends with `<command>-end count=<lines between> records=<records> ... #token`;
  the markers are named after the COMMAND (`state`, `party`, `threat`, `order`, `role`, `master`, `learn`, `equip`, `itemscan`, `grammar`), the data lines between them carry their own record kind as first word. `count=` counts every line between the markers, continuation lines included; `records=` counts the lines that are not continuations. Other totals on an end line (`bots=`, `resting=`) count records.
* **Keys** are `[a-z][a-z0-9_]{0,23}`; a key appears at most once per line.
* **Values are percent-encoded**: every byte outside `[A-Za-z0-9._-]` is written `%XX` (upper-case hex): space `%20`, `=` `%3D`, `:` `%3A`, `;` `%3B`, `,` `%2C`, `#` `%23`, `|` `%7C`, `%` `%25`, `+` `%2B`; `_` stays literal.
  **An empty value is the empty string** (`tgt=`); `none` has no special meaning anywhere and the module never writes `none`, `-1` or `0` for "no value" or "unknown". An unknown number is an omitted key (for example `manapct` is absent for a bot that has no mana). Hex is upper-case only.
  An empty list is the empty value; an empty ITEM (`a;;b`, a trailing `;`) is never written.
* **Structure** inside a value uses characters the encoding never produces for data: `;` separates list items, `:` separates the parts of one item, `,` separates coordinates. Each part is encoded on its own:
  `co=close;protection;tank%20assist`, `top=Mtank:343;Mdpsb:338`, `pos=-9100.80,-2005.30,132.90`, `ord=41:ok`, `target=party:Mtank`, `class=2:8`.
* **Numbers** are decimal, **booleans** `yes`/`no`.
* **Length.** A line is at most 3,000 bytes (the token included). A record that is longer continues on `<kind>+ <identity key=value, `bot=` for a bot record> <more keys> #token` lines that follow their base line immediately. A single value longer than a line (a very long strategy list) is cut at a list item.
  Free text (`msg=`) is cut to 400 bytes before encoding.
* **Errors**: `error code=<word> cmd=<word> [msg=<encoded text>] #token`, exactly those keys, one line, nothing else for that command. A per-bot refusal inside an accepted command is the same words on the bot's own line: `order bot=.. verb=.. status=refused code=.. msg=..`.
  Codes: `engine-not-built`, `missing-target`, `no-such-player`, `not-a-bot`, `no-party`, `no-bots`, `ambiguous-party`, `missing-verb`, `unknown-verb`, `missing-argument`, `unexpected-argument`, `bad-argument`, `bad-target`, `bad-role`, `bad-id`, `too-many`,
  `no-engine`, `queue-full`, `not-in-group`, `needs-player-sender`, `no-selection`, `mark-empty`, `no-such-spawn`, `target-not-found`, `target-dead`, `not-attackable`, `no-pull-strategy`, `no-commander`, `other-map`, `too-far`, `nothing-to-assist`, `self-master`, `missing-commander`,
  `missing-leader`, `duplicate`, `party-refused`, and `action-not-registered` (with `name=<the legacy action key>`). Slice 4 adds (section 8): `no-trigger`, `not-allowed`, `leader-refused`, `leader-inside`, `not-leader`, `lfg-group`, `not-empty`, `locked`, `no-entrance`,
`already-on-map`, `in-combat`, `dead`, `not-dead`, `teleporting`, `teleport-refused`, `timeout`, `stuck`, and the entry refusals `level`, `full`, `difficulty`, `not-found`, `other`. A refusal by the core itself (no permission, unknown command) is the core's own untagged text, not this grammar.
* **Addressing.** `<bot>` is a character name (a logged-in host bot). `party <leader>` is every host bot in the party of that character. `party` with no leader is, in this order: the bots bound (`.tb master`) to the sender, or to the one commander there is, or the members of the one bot-led party there is;
  anything else needs the leader's name (`error code=ambiguous-party`). Names are case-insensitive. The `target=` of a reply is `<name>`, `party:<leader>` or `party:commander:<name>` as parts.
* **In a build without the engine** every command below (and `grammar`) answers `error code=engine-not-built cmd=<command> msg=...`.
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
| `pull` | the same targets | `pull my target` with the unit's guid as the event object (tank only), then the party hold (package L: the other bots wait until the pull LANDED and the pack is on the tank: 3 s at the earliest, 10 s at the latest, at once when a mob hits them or the tank is under 60 %, 15 s after the order if it never lands) | as `attack`, and the bot has no `pull` combat strategy |
| `endpull` (or `end pull`) | - | `end pull` | - |
| `hold` | - | strategy `+passive` in combat and non-combat (`hold=yes` in the state line) | the strategy is not registered |
| `free` | - | `ResetStrategies`, movement `follow`, the explicit attack target cleared | - |
| `assist` | a party member's name | one shot: `attack` on what that member is attacking when the order runs | the member is not in the party, or has no attackable victim now |
| `co` / `nc` | `+name,-name` | `ChangeStrategy(arg, combat / non-combat)`: a debug switch | characters other than letters, digits, space, `+ - , _` |
| `release` | - | `release` (the dead bot releases its spirit; inside a dungeon the core revives it at the instance's entrance, section 8) | the instance has no entrance point (`no-entrance`: the release is held, never a graveyard outside) |

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

### `.tb role <bot|party [leader]> tank|healer|dps|none [#token]`
Stores the role (`PlayerbotAIStorage::SetPlayerForcedRole`) and in the bot's next engine tick sets the AI's forced role and rebuilds the kit. `role-begin`, `role bot=Mdpsb role=dps seq=7 status=queued` (`status=stored` when it has no engine yet), `role-end count=.. records=.. role=.. target=.. bots=.. queued=..`.

### `.tb master <bot|party [leader]> <commander|none> [#token]`
Binds the bots to a commander who stands OUTSIDE their party (decision D2): host master and engine master become the commander, `IsInCommandedGroup` answers true for them, and a commanded bot keeps that master (the engine's adoption step skips it) and follows it. `none` releases them.
`master-begin`, `master bot=Mtank commander=Mcmd commander_in_party=no seq=44 status=queued`, `master-end count=.. records=.. target=.. commander=.. bots=.. queued=..`.

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
| `pos` `map` `inst` | ONE key `x,y,z` with two decimals; map id; instance id (no separate x/y/z keys) |
| `dl` `dm` | yards to the party leader / to the master (`self`, `othermap`, a number, empty when there is none) |
| `ord` `lastorder` | `<seq>:<ok\|queued\|fail%3A<code>>` of the last order, and its verb; both empty when the bot has had no order |
| `co` `nc` | combat and non-combat strategies, a `;` list |
| `cast_queued` `cast_delivered` `cast_dropped` `cast_pending` | the cast-result queue counters (absent without a bot session) |
| `eating` `drinking` | a seated food / drink aura is on the bot |
| `resting` `rest_why` | the rest gate: `resting=no` means out of the old rest conditions; `rest_why` is a `;` list of `dead`, `combat`, `hp`, `mana`, `eating`, `drinking` |
| `life` | slice 4: `alive`; `dead` (a body that will release); `awaiting-res` (a body held by the crew rule, or with a resurrection pending); `ghost`; `running-back` (alive again, more than 30 yd from its commander or leader, until it is within 20 yd) |
| `relhold` | slice 4: why a BODY on an instance map waits (commanded bots): `rezzer` (a living priest, paladin, shaman or druid of the party is on the map), `combat` (a member fights), `wipe` (nobody is left: it releases now), `no-entrance` (the instance has no entrance point: the release is held); empty otherwise |
| `resreq` | slice 4: the party member or commander whose resurrection offer is pending for this dead bot; empty when none |
| `corpse` `corpsemap` | slice 4, dead bots only: `x,y,z` of the body (unreleased) or of the corpse a ghost has, and its map id |
| `outside` | slice 4: `yes` when the party's leader is inside an instance and this bot is not in that instance, else `no` |
| `entered` `entry` `block` `blockmsg` | slice 4: how the last map change the host started happened (`trigger`, `order` (a direct entry or `.tb instance leave`), `setup` (`.tb tele`); empty = none yet); `entry` is the phase of a running entry (`walking`, `waiting-leader`, `teleporting`; empty when none); `block` is the code of the last refused entry or lost teleport (empty = none) and `blockmsg` says it in words |
| `catchup` `practice` | slice 4: how many open-world catch-up teleports the host made for this bot (never into or inside an instance); `yes` after `.tb revive` until the next combat ends |

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
* `.tb learn <bot> <spell id>...`: `learn-begin`, `learn bot=.. spell=<id> result=<learned|known|unknown|invalid|not-learned> name=..`, `learn-end count=.. bot=.. learned=..`. Core path: `SpellMgr::IsSpellValid`, `Player::LearnSpell`. At most 40 ids.
* `.tb equip <bot> <item id> [replace]`: `equip-begin`, ONE record `equip bot=.. item=<id> name=.. result=<equipped|refused> [slot=..] why=<empty|EQUIP_ERR_...|unknown-item|bags-full|swap-failed> [code=..] [replaced=yes] [note=..]`, `equip-end count=1 records=1 bot=..`.
  `Player::CanEquipNewItem` then `Player::EquipNewItem`; a taken slot refuses (`EQUIP_ERR_NOT_EQUIPPABLE` or `EQUIP_ERR_NO_SLOT_AVAILABLE`); with `replace` the item is stored in the bags and the core's `Player::SwapItem` puts it on.
* `.tb itemscan <bot> <item id>...`: `itemscan-begin`, per id `itemscan bot=.. item=<id> exists=<yes|no> [name= class=<c>:<sc> inv= req= quality= ilvl= canequip=<yes|no> slot=<n, only when yes> why=<empty|EQUIP_ERR_...>]`, `itemscan-end count=.. bot=.. exist=.. canequip=..`.
  `canequip` is the core's check for THIS bot with swap = true. Nothing is created or changed. The ids come from the data collaborator's list; the module holds no id.

## 7. Config keys

`TortoiseBots.Slice.RestHealthPct` (default 90) and `TortoiseBots.Slice.RestManaPct` (default 80), `forever/conf/tortoisebots.conf.dist`; read on each `.tb state`.

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
`forever/src/host/run/TbRun.h`: one `RunRec` per run, keyed by the leader's guid (leader, plan, the Turtle `DungeonRun::Run` as the progress, `startedMs`/`endedMs`, `pauses`, `deaths`, `wipes`, the event ring of 200, `stopped`/`stopReason`, the known boss states). **Nothing created or ticked a run in S5**; the S6 glue (section 10) does, and writes these records:
`run lead= plan= state=running|paused|done|stopped step= steps= [leg= legname= kind=] phase= note= pull= pulls= boss= deaths= wipes= pauses= elapsed= [why=] [ev=]`
* `step` = the index of the step the run is on (`steps` once it is past the last one); `leg`, `legname` and `kind` describe that step and are absent past the last step. `phase` is the Turtle run's phase (`begin gather walk pull task rest wait hold`), `note` what it does now or why it paused (free text, cut to 400 bytes, then `trunc=yes`).
* `pull` = the plan's pull steps the run has passed that it did not record as skipped, deferred or given up (its own lists); `pulls` = the plan's pull steps. `boss` = `<entry>:alive|dead` for each plan boss the glue has READ; a boss it never read is not listed (a boss is never reported alive by default).
* `elapsed` seconds since the start, frozen at the end of the run; `why` exactly when the run is paused (the run's own note) or stopped (the reason it was given); `ev` = the seq of the newest event, absent when there is none.
* `event seq= t= kind= [step=] [leg=] [unit=] why=`: `t` seconds since the start, `kind` = start step pull fight-end rest death wipe revive pause resume done stop, `unit` a creature entry; `step`, `leg` and `unit` are absent when the event has none.
* `.tb state`: the end line of a party whose leader has a run gains `run=<lead name> runstate=<state>` (addendum A8 allows new keys; absent when there is no run).

## 10. Slice 6: the tank bot leads a dungeon run (package C6)

`.tb run` starts, watches and stops a run of the Turtle dungeon-run policy (`runtime/DungeonRunPolicy.h`, unchanged) on a plan of section 9. The **tank bot is the run's leader** (decision D1, a "system run"): `start` binds every other member of the tank's party to the tank as its commander (`.tb master` binding, the same host calls), and from then on nobody orders a bot from outside. Once a second the module reads the party and the step (`ReadFacts`, package F6), asks the policy for its one order, and hands that order to the executor (package O6: `move`, `pull`, `rest`, `revive party` ...). The fights stay the bots' own combat AI. All replies follow the grammar of section 3: markers `run-begin` / `run-end` (the verb is the end line's `verb=`), `error` on one line, the token last. In the default build (option OFF) every `.tb run` form answers `error code=engine-not-built`.

```
.tb run start <leader> <plan> [#token]
.tb run stop <leader> [reason words] [#token]
.tb run pause <leader> [#token]
.tb run resume <leader> [#token]
.tb run status <leader|all> [#token]
.tb run events <leader> [since=<seq>] [#token]
```

**`start`** checks, in this order, and changes nothing when it refuses: the leader is a host bot in the world (`not-bot`); the plan is loaded (`not-loaded`, `no-plan`); the executor's own check (`no-engine`, `not-leader`, `no-tank`: the leader leads its party, has the tank role (`.tb role <leader> tank`, which the facts' `leadIsTank` reads) and every member is a host bot with a running engine); the party is not a raid (`group-raid`); no live run of this leader (`already-running`; an ended run is replaced); leader and members stand in the same instance of the plan's map (`wrong-map`); every member is a host bot in the world (`not-bot`, `player-in-group`); **every other member is already commanded** (`not-commanded`: bind the party first, `.tb master party <leader> <commander>`; a run does not rebuild an engine kit); the host accepts the binding (`bind-failed`, everything given back). The reply is one `run` record (`state=running`) and `run-end verb=start`.

**`stop`** is the run's end for good: out of combat the executor's `stay` goes to the leader and every member, the old commander of each member is restored (`master`/`commander` as they were before `start`, unless somebody rebound the bot meanwhile), the run is `state=stopped` with `why=` the reason (default `stopped: by .tb run stop`), and the end line says `pending=no stayed=<n>` (`n` = bots whose `stay` action ran). **A stop never interrupts a fight:** with any living member or pet in combat the reply is `pending=yes` (no `stayed=`), the run gives no new order from then on, and the stop takes effect, with the same `stay` and release, on the first second without combat (`.tb run status` then shows `state=stopped`). A repeated `stop` while one is pending answers `pending=yes` again. Errors: `not-running` (already ended).

**`pause`** pauses a running run at once (`state=paused why=paused by .tb run pause`): it is a player's pause, so the run never resumes by itself from it. **`resume`** is the policy's own checked resume (`ResumeChecked`): the party regroups and goes on. It refuses `not-paused`, `stopping`, `player-in-group` (a real player is in the group; a system run waits until it is bots only again) and `refused` (the policy's reason, e.g. the leader is still in another part of the instance). Errors for both: `not-running`.

**`status <leader>`** is one `run` record; **`status all`** one per run (by leader name, ended runs included until the leader starts another), and its end line adds the tick cost: `runs= ticks= tick_avg_us= tick_max_us=` (run ticks since boot) and `facts_reads= facts_avg_us= facts_max_us= facts_unreadable=`.
**`events <leader> [since=<seq>]`**: the ring of the last 200 events with `seq` above `since` (default all), oldest first, one `event` record each (section 9 lists the keys); the end line has `lead=` and `last=` (the newest seq, absent with none), so a watchdog asks `since=<last>` and sees "no new event for N minutes". Kinds: `start`, `step` (the run moved to a step; `why=not-cleared: ...` when a pull step none of whose members matched a spawn was skipped), `pull`, `fight-end`, `rest`, `death` (`why=` the bot), `wipe` (every member dead), `revive`, `pause` (`why=` a code or the run's note: `outside-order`, `player-in-group`, `lead-off-map`, `command`, or the policy's own reason), `resume`, `done`, `stop`. Errors: `no-run` (no run of that leader), `bad-argument`, `unexpected-argument`, `missing-argument`, `unknown-verb`.

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
run-end count=2 records=2 verb=events lead=Mtank last=31 #r3
```
```
error code=not-commanded cmd=run msg=Mdpsb%20has%20no%20commander%20yet%3A%20bind%20the%20party%20first%20%28.tb%20master%20party%20Mtank%20%3Ccommander%3E%29 #r4
```
**The samples above were built by hand from the encoder's rules, as section 9's were; the first boot replaces them with real output.**

## 10. Slice 11: pets (package P11; the hunter and the warlock)

### `.tb pet <bot> give|call|dismiss|show [#token]` and `.tb pet <bot> order <words> [#token]`
One host bot (a name; `party` is refused `bad-target`). Every reply is `pet-begin`, one `pet` record, `pet-end`, read back from the world after the verb (never what was asked).

* `give`: a hunter of level 10 or more with no pet out and none in an active slot of its stable gets its first pet, a tameable beast picked for its race (`forever/data/slice11/hunter-starter-pets.csv`), through the core's own tame path (`Unit::CreateTamedPetFrom`, add to the map, minion, saved as the current pet, pet bar sent), Best Friend, defensive, every active spell on its autocast default, and then its spells trained by the module's planner (`AiPlayerbot.PetAutoTrain`). Refusals (`error code=`): `wrong-class` `level-too-low` `not-in-world` `has-pet` `stored-pet` (Call Pet brings it back) `no-starter` `create-failed` `no-map` `not-summoned`.
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
