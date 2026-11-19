# Slice 3, package H3 (host): the order and state surface

Branch `slice3/h` (worktree `~/wt-slice3-h`), default build slot (no `FOREVER_SLOT`), branched from `slice2/integ` at ac3bba771. Core read only at the pin 6517af05. Not pushed, no boot.
The interface a client is written against is `forever/docs/TB-COMMANDS.md`; this file says how it was built and what could not be checked without a boot.

## 1. What was added

| File | New | What |
|---|---|---|
| `forever/src/host/TbH3.h` | yes | declarations: the nine command handlers, the engine hooks, the shared helpers |
| `forever/src/host/TbH3.cpp` | yes | token split, value escape, reply (`Say`/`Fail`), addressing (`bot` / `party [leader]`), the `.tb master` bindings. Core headers only |
| `forever/src/host/TbOrders.cpp` | yes | `.tb order`, `.tb role`, `.tb master`, the order queue, `BeforeEngineTick`, `IsCommandedBot` |
| `forever/src/host/TbPartyState.cpp` | yes | `.tb state`, `.tb threat`, `.tb party` (roster and forming) |
| `forever/src/host/TbTestKit.cpp` | yes | `.tb learn`, `.tb equip`, `.tb itemscan` (core headers only) |
| `forever/docs/TB-COMMANDS.md`, this file | yes | docs |

All five `.cpp` files are compiled with and without `TORTOISEBOTS_SLICE1` (they are not `TbHost*`); the engine-facing parts of `TbOrders.cpp` and `TbPartyState.cpp` are behind the macro exactly as
`TbEngineBrain.cpp` does it, and every handler starts with "is the engine built in?" and answers `error=engine-not-built` when it is not. The option-OFF object list gains four objects:
`TbH3.cpp.o`, `TbOrders.cpp.o`, `TbPartyState.cpp.o`, `TbTestKit.cpp.o` (the other two files of the old list that include the new header, `TbScripts.cpp` and `TbEngineBrain.cpp`, are recompiled, not new).

### Lines added to existing files (all of them)

* `forever/src/TbBotMgr.h`: 3 lines in the public part, after `Count()`: a comment line, `ObjectGuid MasterOf(ObjectGuid) const` and `bool FollowStatus(...) const` (inline one-liners over `_bots`; they only read).
* `forever/src/TbScripts.cpp`: `#include "host/TbH3.h"` after the `TbEngineBrain.h` include, and nine rows at the END of the `tbTable` list (`order, role, master, party, state, threat, learn, equip, itemscan`, all `RBAC_PERM_COMMAND_GM`, `Console::Yes`).
* `forever/src/host/TbEngineBrain.cpp`: `#include "host/TbH3.h"` and one call in `engine::Tick`: `TortoiseBots::h3::BeforeEngineTick(brain->bot, brain->ai);` between `DeliverCastResults` and `UpdateAI`.
* `forever/src/host/TbHostOwnership.cpp`: `#include "host/TbH3.h"`; `BotCommands::IsInCommandedGroup(Player* /*bot*/) { return false; }` became `(Player* bot) { return TortoiseBots::h3::IsCommandedBot(bot); }` (2 lines changed, 1 comment line added).
* `forever/conf/tortoisebots.conf.dist`: two keys appended (`TortoiseBots.Slice.RestHealthPct`, `RestManaPct`).

Nothing outside `forever/` was touched; `forever/src/slice/*` and `forever/src/compat/*` are untouched.

## 2. Threads (why the queue needs no lock)

* Console command: `World::ProcessCliCommands` (World.cpp:3166), called from `World::Update` at World.cpp:2592 (after the maps update): the world thread.
* GM chat command: `WorldSession::HandleChatMessageOpcode` -> `ChatHandler(this).ParseCommands` (ChatHandler.cpp:233). `CMSG_CHAT_MESSAGE_*` are `PROCESS_THREADUNSAFE` (Opcodes.cpp:334-338), which `WorldSessionFilter::Process` returns true for only in
  `World::UpdateSessions` (WorldSession.cpp:70-97; World.cpp:3119, called at :2421) and `MapSessionFilter` refuses (the map threads never run them): the world thread.
* The engine tick: `OnBeforeMapsUpdate` (TbScripts.cpp, World.cpp:2470, `sScriptMgr->OnWorldBeforeMapsUpdate`) while no map updates: the world thread.

So the three that touch the order queue (command, tick, logout hook) never overlap; the queue and the `.tb master` table are plain `std::map`/`std::deque` with no lock. The spec's "assert it" was not added as a runtime
check: the core has no "is world thread" call at this pin, and a latch on first use would only record whichever thread came first. The claim rests on the citations above and is on the boot list (section 9).

## 3. The order table as built

An order is validated and queued by the command, which answers at once; `BeforeEngineTick` (called from `engine::Tick`, right before `UpdateAI`) runs the bot's queue through `PlayerbotAI::DoSpecificAction(key, Event("tb order", ..., owner), true)` (silent).
No `HandleCommand`, no chat strategy, no security check (the `.tb` permission is the gate). The Event's owner is the bot's commander when it is in the world, else the bot itself (spec section 2).

| Order | Action key(s) / new host code | Notes |
|---|---|---|
| `follow` | `follow chat shortcut` | ChatShortcutActions.cpp:37-112 |
| `follow <member>` / `follow none` | `follow target` (param name / `none`), then `follow chat shortcut` | ValueActions.cpp:240-288: group members only; plain `follow` does not clear a manual follow target (as the legacy chat), `follow none` does |
| `stay` | `stay chat shortcut` | ChatShortcutActions.cpp:115-139 |
| `come` | host code: `Reset()`, `position["stay"]` = the sender's position (the commander's from the console), `SetMovementStrategy("stay")`, `StayWalkOrdered(...)` | the stay strategy's own trigger `return to stay position` walks (StayStrategy.cpp:25-30); `ReturnToStayPositionAction::isPossible` (PositionAction.cpp:193-210) drops a stay position further than `reactDistance`, so the order refuses `too-far` up front |
| `attack <target>` | host code puts the unit on the bot's rti mark, `attack rti target`, then restores all eight marks | transient, because a lasting skull would make every damage bot and the tank pick it (`RtiTargetValue::CalculateFocus`, RtiTargetValue.h:30-37) and would spoil the taunt-back test f; "rti target" is a cached value, so it is `Reset()` first |
| `attack my` | the sender's selection, read when the command runs, then as `attack <guid>` | refused from the console (`needs-player-sender`); the spec's `attack my target` reads the selection at execution time, this snapshot does not race it |
| `attack mark[:word]` / `skull` | the group's own mark -> guid -> as `attack <guid>` | |
| `pull <target>` | `pull my target`, guid as the event object (PullActions.cpp:329-338) | needs the `pull` combat strategy on the bot (the tank role) |
| `endpull` | `end pull` | |
| `hold` | `ChangeStrategy("+passive", combat)` and `(+passive, non-combat)`, then checks `HasStrategy("passive")` | `passive` is not registered yet in this branch: result `fail:strategy-not-registered:passive` until the porting worker registers it |
| `free` | `ResetStrategies()`, `SetMovementStrategy("follow")` (as `engine::Create`), explicit attack target cleared | no icon is left to clear (see `attack`) |
| `assist <member>` | host code: the member's victim (else its target) at execution time, then as `attack <guid>` | |
| `co` / `nc <+x,-y>` | `ChangeStrategy(arg, combat / non-combat)` | debug |
| `.tb role` | `PlayerbotAIStorage::SetPlayerForcedRole` now, `ai->SetForcedRole` + `ResetStrategies()` (movement mode kept) at the next tick | a brain built later gets the stored role at its first tick (`BeforeEngineTick` compares `GetForcedRole()` with the stored one) |
| `.tb master` | `BotManager::BindBotMaster` / `ClearBotMaster` (host master + engine pointer) + the binding table, then a `rekit` (`ResetStrategies`) at the next tick | the commander stays outside the party |

Actions the order surface calls that this branch does not register (the porting worker's O3/P3/T3 keys): `follow chat shortcut`, `stay chat shortcut`, `follow target`, `attack rti target`, `pull my target`, `end pull`.
`ActionRegistered` asks the context (`AiObjectContext::GetAction`, which caches what it finds, so asking never leaks or creates a second object) when the order is queued, and a missing key answers
`status=refused error=action-not-registered name=<key>` (and the guard logs `slice: missing action` once). The code therefore works unchanged as soon as the other branch lands. `hold` needs the `passive` strategy.

`DoSpecificAction` runs the first existing engine's `ExecuteAction` only (PlayerbotAI.cpp:4094-4165 returns after the first engine, whatever the result), so a `false` is "impossible, useless or failed" without a reason: the result is
`fail:<verb>`, and the engine's own `[ACTION]` log lines say why. A finer reason would need the engine's `ActionResult`, which `DoSpecificAction` hides.

### The master binding and `IsInCommandedGroup`

`.tb master` records `bot -> commander` in `h3` (TbH3.cpp) and sets BotMgr's host master to the commander. `BotCommands::IsInCommandedGroup(bot)` (TbHostOwnership.cpp, the engine reads it at PlayerbotAI.cpp:3373)
answers true while BotMgr still names that commander as the bot's host master (`CommanderGuid` checks it), so a `.tb login <bot> <other>` or `.tb group` makes the binding lapse by itself. The engine's strip rule
(PlayerbotAI.cpp:3392-3412) skips a commanded bot, and its adoption step (:3441-3505) never adopts a real player for one (`:3479`). What else the "commanded" answer switches on is listed in TB-COMMANDS.md section 4 (threat back-off, no open-world strategies,
game objects only on an order, the bot stays when its master leaves the party).
`.tb party` (forming) calls `BotMgr::GroupWith`, which sets each bot's host master to the LEADER; run `.tb master` after it (the live-test sequence of spec section 10 does).

## 4. Replies

Every reply goes through `ChatHandler::SendSysMessage(std::string_view)` of the handler the core built for the sender: `ChatHandler::SendSysMessage` (Chat.cpp:111-125: one `CHAT_MSG_SYSTEM` packet to `m_session` per line, `\n` splits lines),
`CliHandler::SendSysMessage` (Chat.cpp:621-625) for the console. The existing `.tb` commands reply through the same calls (`PSendSysMessage` -> `SendSysMessage`, Chat.h:56-64), so nothing new is assumed: a player session gets
system chat. The slice-3 lines are not printf-formatted (a `%` in a name cannot corrupt a line) and carry the echo token as their last word. Grammar: TB-COMMANDS.md sections 3 and 5.

Line length: a typical state line is 530 characters (the strategy lists are capped at 150 each); the longest possible is about 900 (two guids, two full strategy lists, a long blocked reason). The spec's 250-character cap was not kept: a system message holds 4095 bytes (ChatPackets.cpp:214), and every key the brief asks for does not fit 250.

## 5. Permission (read at the pin, and the test server's auth database read-only)

* Every `.tb` row is `RBAC_PERM_COMMAND_GM` (371), `Console::Yes`. `ChatCommandNode::IsInvokerVisible` (ChatCommand.cpp:472-479): the console always passes (`CliHandler::HasPermission` true, Chat.h:146); a player needs 371.
* auth database of forever-test1 (read with a SELECT through the `forever-db` container, 2026-11-18): `rbac_default_permissions`: security level 0 -> 195, 1 -> 194, 2 -> 193, 3 -> 192 (realm -1); links 192 -> 193 -> 194 -> 198 -> 371 and 195 -> 199 only.
  So **security level 1 and up holds 371; level 0 does not**. Both accounts on the server (`1#1`, `2#1`) have no `account_access` row (level 0) today.
* No new permission id and no core or database change. A test account gets it with the core's own console commands: `rbac account grant <account> 371 -1`, or `account set seclevel <account> 2 -1` (needed anyway for `.gm on`). Both are test-server only.
* The engine's own GM tier (`CheckBotCommandAuth`, PlayerbotAI.cpp:2910-2938) is not in the path: the orders do not go through `HandleCommand`.

## 6. Test kit: which core APIs

* `.tb learn`: `SpellMgr::GetSpellInfo(id, DIFFICULTY_NONE)` (unknown -> refused), `SpellMgr::IsSpellValid(info, bot, false)`, `Player::HasSpell`, `Player::LearnSpell(id, false)` (the path of `.learn`, cs_learn.cpp:78-103).
* `.tb equip`: `ObjectMgr::GetItemTemplate`, `Player::CanEquipNewItem(NULL_SLOT, dest, id, swap)` (Player.cpp:11743: class/level/skill via `CanUseItem`, slot, two-hand and off-hand rules, combat, stun), `Player::EquipNewItem(dest, id, NONE, true)`.
  With `replace` (a taken slot): `CanEquipNewItem(..., swap = true)`, `StoreNewItemInBestSlots`, `Player::CanEquipItem` and the core's `Player::SwapItem(src, dest)`. The refusal text is the core's `InventoryResult` name.
* `.tb itemscan`: `GetItemTemplate` (exists?) and `CanEquipNewItem(NULL_SLOT, dest, id, true)` (can this bot equip it?). No id is written in the module. Examples for the checks come from `~/wt-forever-port/forever/data/waterys/rfc/gear-level18.csv`
  (`spec,item_id,name,slot,required_level,...`; for instance the first row `priest,4643,Grimsteel Cape,back,0,...`).
  The spec's `.tb itemscan <class> <subclass> <maxReqLevel>` (a scan of the template store) is replaced by the brief's per-id form; the data list is the source of ids.

## 7. State and rest gate

Keys, order and meaning: TB-COMMANDS.md section 5. Sources: the core for health, power (rage divided by ten), position, combat flag, group and leader; the engine for role (`IsTank`/`IsHeal`), state (`GetState`), current target (value `current target`),
strategies (`GetStrategies`), movement (`HasStrategy("follow"/"stay")`), `HasSeatedMeal` (the engine's own eating/drinking test, PlayerbotAI.cpp:358-376); BotMgr for the master, blocked-follower and master-away facts (`FollowStatus`), `BotSession::GetCastResultCounts` for the counters; the order table for `ord=`.
Rest gate: `resting=yes` while dead, in combat, health under `TortoiseBots.Slice.RestHealthPct` (90), mana under `RestManaPct` (80, mana users only), eating or drinking. The spec's `stuck=nomove:<s>` (no-movement timer) is not built: it needs a per-bot clock the
host does not keep; `stuck=` shows blocked, away or noengine.

## 8. Build and gates

Run on the build VM, default slot, tree of commit b0bb8d562 (the code of the final commit; later commits only change docs):

| Gate | Result |
|---|---|
| option-OFF module build: `~/forever-build/build.sh 12 tortoisebots` | `CMAKE_RC=0 MAKE_RC=0` (first run 31 s; after the ON build, with `FOREVER_CMAKE_EXTRA=-DTORTOISEBOTS_SLICE1=OFF`, `CMAKE_RC=0 MAKE_RC=0`, the cache reads `TORTOISEBOTS_SLICE1:BOOL=OFF` again: the default slot is restored) |
| option-ON `worldserver`: `FOREVER_CMAKE_EXTRA=-DTORTOISEBOTS_SLICE1=1 FOREVER_NINJA_EXTRA="-k 0" ~/forever-build/build.sh 12 worldserver` | `CMAKE_RC=0 MAKE_RC=0`, 183 steps, 812 s (the VM load was 30-60 from other builds), no error, no new warning in the new files (the only warning is the old `sTerrainMgr` macro redefinition of every shim file) |
| option-OFF object list | `libtortoisebots.a` holds 10 objects: the 6 of the branch point (`TbBotMgr TbBotSession TbEngineBrain TbItemPrototype TbScripts TbSpellEntry`) plus the four deliberate additions `TbH3 TbOrders TbPartyState TbTestKit`; the list a source glob gives at 21f378ae7 and at HEAD differs by exactly these four files |
| census `forever/SLICE2.txt` (`vm-census.sh`, list mode) | `files 160 compile clean 160 with errors 0 error places 0` |
| census `forever/SLICE1.txt` | `files 133 compile clean 133 with errors 0 error places 0` |
| files changed outside `forever/` | none (`git diff --stat 21f378ae7 HEAD` lists only `forever/` paths) |
| the new commands answer "the engine is not built in" in the OFF build | by construction (every handler starts with `RequireBuilt`, which tests `engine::Built()`; in an OFF build it is the stub of TbEngineBrain.cpp); not run (no boot) |

Disk on the build VM after the builds: 6.6 GB free on `/` (7.5 GB before the ON build; the stop line is 4 GB).

## 9. NEEDS FROM porting, and what a boot must still check

NEEDS FROM porting (slice/ and compat/ are theirs):
1. Register the keys the orders call: actions `follow chat shortcut`, `stay chat shortcut`, `follow target`, `attack rti target`, `pull my target`, `end pull`; strategy `passive`; strategy `pull` on the tank kit; value `rti`, `rti target`, `explicit attack target`, `position`, `last movement`, `master position` (the follow shortcut writes it, a null write is the R11 lesson).
2. `SliceAiFactory`: read the AI's forced role AND the stored one (`PlayerbotAIStorage::GetPlayerForcedRole`) through `AiFactory::GetPlayerRoles`, because `.tb role` writes both (the AI's one after the engine's next tick), and a console commander is no network master, so
   `GetPlayerRoles` ignores the AI-level role when `HasActivePlayerMaster()` is false (AiFactory.cpp:586-592): only the stored one counts for a bot-session commander.
3. The `threat` strategy added where `KeepsPartyThreatBackoff` would add it: `IsInCommandedGroup` is true for bots bound by `.tb master` (AiFactory.cpp:633-642).

Boot checklist (nothing below was run):
* the 9 rows appear under `.tb` and each command answers; the OFF build answers `error=engine-not-built` for all nine;
* a console `tb order ...` and a GM chat `.tb order ...` reach the same queue, and the replies come back to the typing session with the `#token` (observation m);
* `.tb master` + `engine on`: the bot-session commander survives the first ticks (no strip), `IsInCommandedGroup` answered true (`state` shows `dm=` and `master=`, `.tb where` shows the master);
* each order's legacy action runs (`ord=<seq>:ok`) and `attack` leaves the group's marks as they were (watch `Group::m_targetIcons`);
* `come` for a bot more than the react distance away (the refusal) and under it (the walk and the arrival);
* `.tb equip ... replace` on a bot with starter gear, a two-hander over an occupied main hand and off hand;
* `.tb learn` of 71 / 355 / 7386 on a warrior (spec U1) and `.tb itemscan` of the level-18 list for each class;
* the line length of a real `state` line, and that the escaped strategy names parse on the agent side;
* `resting=` over a real eat and drink cycle (the seated-aura test).
