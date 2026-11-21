# S1-R2 (chat commands in): `PlayerScript::OnChat` -> an inbox -> `PlayerbotAI::HandleCommand`

Round S1-R2 of `~/TortoiseBots-audit/FOREVER-SMALL-SLICES-SPEC.md` (section 3), on top of `slice/s1r1` (chat links, reviewed and fixed).
Worktree `~/wt-s1r2`, branch `slice/s1r2`; gates on core d02645dd21. Zero core changes.

## 1. The way a line reaches a bot

```
client / agent                 core (world thread)                     module
/p stay or /w Mtank stay  ->   Handlers/ChatHandler.cpp                 -
                               .tb lines parsed first (:233)            -
                               links validated (:241)                   -
                               Player::Whisper -> OnPlayerChat          -> PlayerScript::OnChat(sender, type, lang, text, receiver)
                               ChatHandler party case -> OnPlayerChat   -> PlayerScript::OnChat(sender, type, lang, text, group)
                                                                          both copy guids + text into the ingress (64, mutex)
                                                                       -> BotMgr::Update -> chatin::DrainArrivals (world thread)
                                                                          resolve sender/receiver/group, one Judge per bot -> inbox (16)
                                                                       -> engine::Tick -> engine::DeliverChat -> PlayerbotAI::HandleCommand
                                                                          -> chatCommands -> HandleCommands -> the trigger -> the action
```

* **Hook** `forever/src/host/chat/TbChatIn.{h,cpp}`: `class tortoisebots_chat : public PlayerScript` overrides both `OnChat` forms and never
  writes the text; it copies the sender's guid, the receiver's (or the group's) guid, the kind, the language and the text into an
  `Arrival` (`TbChatInPure.h`) and touches no `Player`, `Group`, `BotMgr` or binding state, because `Player::Whisper` is a public script
  entry that need not run on the world thread. Registered by `TortoiseBots::chatin::AddScripts()` (one line in `AddSC_tortoisebots`). The
  hook exists in both builds; without the engine it returns at once (no bot has a brain, so `Judge` answers `no-engine` anyway).
* **Drain of the ingress** `chatin::DrainArrivals()` at the top of `BotMgr::Update` (world thread, before any bot's engine step): it looks
  the sender up by guid, finds the group by guid, and for each receiving bot runs the same `Consider` `.tb hear` uses. A party line is
  judged with `group->SameSubGroup(sender, member)` — the core broadcasts `/p` to `group->GetMemberGroup(sender)` only
  (ChatHandler.cpp:386), so a raid member's line commands its own subgroup, not all eight; the guid overload is used because it reads the
  member slots the broadcast reads (a battleground's original group is not either player's current `GetGroup()`).
* **Filter** `forever/src/host/chat/TbChatInPure.h` (core-free, `TortoiseBots::chatin`): `Verdict Judge(ChatInFacts)`, in the spec's
  refusal order `kind`, `addon`, `self`, `not-real`, `length`, `not-authorized`, `no-engine`. A party-leader line is accepted and stored as
  `CHAT_MSG_PARTY` (the kind `PlayerbotAI::HandleCommand` reads). The core's kind/language numbers are `static_assert`ed in `TbChatIn.cpp`
  (`CHAT_MSG_PARTY` 0x02, `CHAT_MSG_PARTY_LEADER` 0x31, `CHAT_MSG_WHISPER` 0x07, `LANG_ADDON` 183, `LANG_ADDON_LOGGED` 184 at
  d02645dd21), so a core that moves them fails the build. *The spec's failing case says "kind 51"; at this pin `CHAT_MSG_PARTY_LEADER`
  is 0x31 = 49 (SharedDefines.h:6532), so the test uses the core's constant and pins it with the static_assert.*
* **Ownership (D2)**: the bot's master (`BotMgr::MasterOf`), the commander a binding names (`cmd::IsCommandedBy`, TbCmdBinding.h:60), or a
  member of the bot's own group — the legacy rule of `PlayerbotAIPackets.cpp:376-384`. A raid is one `Group`, so any real member of the
  bot's raid may whisper it (legacy and D2 both allow it); the narrower party case is the subgroup rule above. Sender must be a real
  player: `RealPlayer` writes out `isRealPlayer_Helper`'s body (TbCompat_Unit.h:79, "session with a network transport") because that header
  is written against the legacy prelude and the hook is a core-headers-only file in both builds. A bot's own whisper or party reply re-enters the hook and is refused as
  `not-real`, so two bots cannot command each other. `PlayerbotAI::HandleCommand` keeps its own rules afterwards (security level, separator, prefix;
  `.bot`/`.tb` lines never reach it: the core parses them first).
* **Inbox**: per bot guid counter, 16 lines `{sender guid words, kind, lang, text, injected}`, arrival order, oldest dropped and counted;
  one mutex for every box and the 64-line ingress. Cleared when the bot logs out (`BotMgr::OnPlayerLogout`, one line) so a later bot
  reusing the guid starts clean.
* **Drain** `engine::DeliverChat(brain)` in `TbEngineBrain.cpp`, called right after `DeliverCastResults` and before
  `BeforeEngineTick`/`UpdateAI`: the sender is looked up again (`ObjectAccessor::FindConnectedPlayer`), a gone sender, a sender that may
  no longer command this bot (`chatin::StillAuthorized`, the admission rule read again on the world thread: a party removal or a lost
  binding since admission drops the order) or `!AllowActivity()` (the legacy packet gate, PlayerbotAIPackets.cpp:233) drops the line and
  counts it, else `pai->HandleCommand(kind, text, *sender, lang)`. `Inbox::Drop()` adds to `dropped` and sets `chatlast=dropped`, so
  `.tb state` never reads `accepted` for a command that never ran. At most `InboxCapacity` lines per step. The
  engine's own `chatCommands` queue and `HandleCommands` (reaction pass) are unchanged: `reaction pass forced` already counts a queued
  command (`runtime/ReactionWorkPolicy.h`), so a line is read even by an idle hired bot.
* `.tb` lines, link validation, heard-message recording and guild-order answers stay out: the first two are the core's, the last two are
  the seam's (spec section 2).

## 2. Engine kit

* `chat` is added to the reaction engine for **every class** (`SliceAiFactory.cpp`, the full factory's `AiFactory.cpp:1988` without
  `react`, `avoid aoe`, `potions`): its reaction triggers are the chat commands.
* Files added to the build (`forever/SLICE-W1.txt`, the list CMake reads) and to `SLICE-CH.txt`: `strategy/generic/ChatCommandHandlerStrategy.cpp`
  and `actions/{StatsAction,HelpAction,ListSpellsAction,TellCastFailedAction,TellTargetAction,RtiAction,RangeAction,SaveManaAction,TellLosAction}.cpp`
  (the spec's list). `HelpAction` also needs `PlayerbotHelpMgr.cpp` … see section 5.
* `CH_KEYS` in `forever/tools/slice_gen_context.py` (appended last): strategy `chat`; actions and triggers of the spec's table. Registered
  trigger -> action:

| Command | Trigger -> action |
|---|---|
| follow, stay, attack | trigger `follow`/`stay`/`attack` (new) -> `follow chat shortcut`, `stay chat shortcut`, `attack my target` (slice 3) |
| tank attack, flee, max dps | trigger -> `tank attack chat shortcut`, `flee chat shortcut`, `max dps chat shortcut` |
| stats, help, spells, spell, los, rti, range, save mana, co, nc, formation | same-name trigger -> same-name action |
| attackers, possible attack targets | trigger -> `tell attackers`, `tell possible attack targets` (both classes in `TellTargetAction.cpp`) |
| wait <s>, logout, logout cancel | inside `PlayerbotAI::HandleCommand` (:3080-3140); no trigger |
| out (nothing registered) | `cast`/`castnc` (CastCustomSpellAction.cpp), the quest/item/trade/guild commands (seam), `jump` (D5) |

* The strategy's **other 118 trigger names stay unregistered** (it names 138 in `supported` + `InitReactionTriggers`; 19 are registered
  here, `pet` was since slice 11). Each gets one `slice: missing trigger <name>` line at the first reaction pass (the slice's
  `MissingKeyLog`, once per name per process, looked up because every trigger node of the strategy is visited), and the command does
  nothing (LESS). The registry check sees 43 more trigger names than before (63 -> 106 referenced and unregistered). The full list, and
  who owns each name later:
  * **seam (quest/item/trade/guild, or a package already specced)**: `accept`, `q`, `quests`, `quest reward`, `r`, `drop`, `share`, `e`,
    `equip`, `ue`, `u`, `c`, `items`, `inv`, `ll`, `ss`, `t`, `nt`, `s`, `b`, `bb`, `loot`, `add all loot`, `repair`, `trainer`, `bank`,
    `gb`, `mail`, `sendmail`, `ah`, `ah bid`, `ah cancel`, `craft`, `keep`, `guild invite`, `guild join`, `guild promote`, `guild demote`,
    `guild remove`, `guild leave`, `guild leader`, `talents`, `skill`, `faction`, `unlock traded item`.
  * **a kit this slice does not have**: `pet`, `focus heal`, `buff`, `buff target`, `boost target`, `revive target`, `self res`,
    `corpse run`, `revive`, `release`, `talk`, `grind`, `guard`, `free`, `wander`, `runaway`, `warning`, `ready`, `rebuff`,
    `wait for attack time`, `pull`, `pull back`, `pull rti`, `pullback`, `end pull`, `attack rti`, `portal`, `summon`, `go`, `where`,
    `who`, `leave`, `invite`, `join`, `lfg`, `home`, `position`, `stance`, `move style`, `outfit`, `destroy`, `emote`, `rep`,
    `reputation`, `log`, `debug`, `cdebug`, `cs`, `chat`, `cheat`, `flag`, `ra`, `bg free`, `doquest`, `speak`, `set value`, `reset ai`,
    `reset strats`, `reset values`, `save ai`, `load ai`, `list ai`, `hire`, `give leader`, `react`, `all`, `de`, `wts`, `taxi`,
    `teleport`, `jump`, `cast`.
  * Both groups are what the strategy *asks for*. A trigger that stays unregistered never fires, so its (also unregistered) action is
  never looked up either: the one line per name above is the whole log. A registered trigger whose action were missing would log
  `slice: missing action <name>` when it fires; every trigger registered here names an action registered here.

## 3. Host

* `.tb hear <bot> <sender> whisper|party <text> [#token]` (`forever/src/host/chat/TbChatCmd.{h,cpp}`, row at the end of `tbTable`): runs
  `Judge` with `injected` (only `not-real` is skipped) into the same inbox; reply `hear-begin` / `hear bot= sender= kind= verdict= queued=` /
  `hear-end count=1 records=1`; `no-such-player`, `not-a-bot`, `missing-argument`, `bad-argument` refusals. Engine build only: the
  option-OFF build's stub is in `TbH3Off.cpp` (`error code=engine-not-built cmd=hear`, the CMake filter keeps `TbChatCmd.cpp` out of it).
* `.tb state`: appended after `ghostyd=` — `chatin=<accepted>:<refused>:<dropped>` and `chatlast=<word>` (empty until a line was judged;
  `dropped` when the last accepted line was thrown away on its way to the engine, `other-subgroup` when a party line was not for this bot).
* TB-COMMANDS.md section 18 documents the command and the two keys.
* Rows: `incoming: chat message` now reads "host hook (S1-R2); the 1.12 packet case stays dead" (`UNPORTED.md`,
  `UnportedFeatures.h`; the enum entry stays, the legacy packet case still uses it).

## 4. Tests

* `forever/tools/test_chat_in.cpp` (pure, `g++ -std=c++17 -Wall -Wextra -I.`): failing case first `party_leader_line_from_master_accepted`
  (0x31 from the master -> accepted, delivered as PARTY); stranger `not-authorized`; group member and commander-outside-the-group accepted;
  bot sender `not-real`; self `self`; addon `addon`; raid/say/guild `kind`; 255 bytes accepted, 256 and empty `length`; no brain
  `no-engine`; `.tb hear`'s injected line (only `not-real` skipped, every other rule still applies); the inbox (16, ordered, 17th drops
  the oldest, counters, last word); the subgroup rule (a party line from another subgroup `other-subgroup`, a whisper's field not
  consulted, a stranger still `not-authorized` first); `Inbox::Drop` (counted, last word `dropped`); the ingress (64, ordered, 65th drops
  the oldest and counts it, guids and text intact); and the hostile block — a 500-byte line of 200 item links, 128 two-byte characters
  (256 bytes), 200-byte multi-byte text, 30,000 pushes into the ring — all under the test's own 20 s alarm.
  Failing-first evidence: with `CHAT_MSG_PARTY_LEADER` taken out of the accepted kinds (the state before this round, nothing listening)
  the same run printed `FAIL line 54: v.accepted()` (and lines 55, 56), rc 1.
  This round's cases fail before it. With the subgroup rule taken out of the header (`if (facts.kind != KindWhisper &&
  !facts.sameSubGroup)` -> `if (false)`) the run prints
  `FAIL line 231: Judge(otherSubGroup).code == Code::OtherSubGroup` (and 232); with `Inbox::Drop`'s `last_ = Code::Dropped;` reverted to
  `Code::Accepted` it prints `FAIL line 264: box.Last() == Code::Dropped` (and 265). Against the round-start header
  (`git show HEAD:forever/src/host/chat/TbChatInPure.h`) it does not compile: `error: 'struct TortoiseBots::chatin::ChatInFacts' has no
  member named 'sameSubGroup'` — the field, the code, `Drop()` and the `Ingress` ring are new.
* `forever/tools/test_chat_in_wiring.py` + `--mutate` (33 mutations, each must raise): hook registered; both `OnChat` forms overridden, the
  text never written, and no `Consider`/`BotMgr`/`IsBot`/`Group` read inside them; `DrainArrivals` resolving by guid, finding the group,
  routing the party line by `SameSubGroup`, judging every bot; the drain called in `BotMgr::Update` before the bots' engine steps, in
  `Tick` before `UpdateAI`, after the cast results, bounded, re-finding the sender, revalidating `StillAuthorized` and counting
  `chatin::Drop`; the subgroup rule after the ownership rule; `Judge` in `Consider` and `Hear` from `.tb hear`; `chat` in the reaction kit
  and every registered key in the generated context and in `CH_KEYS`; each action's `.cpp` in the build list; no throwing conversion and
  no narrow index in the new text handling; the core's numbers pinned; `InboxCapacity` 16, `IngressCapacity` 64 and `MaxTextBytes` 255 in
  one place; the state block, the logout cleanup, the rows and the docs; `RtiAction`'s two `getObjectGuid()` arguments; the Turtle slot
  helper's slot hint and `ItemVisitors`' first-slot helper; `SLICE-CH.txt` = `SLICE-W1.txt` + this round's files.
* Gate lines, final tree:
  `g++ -std=c++17 -Wall -Wextra -I. forever/tools/test_chat_in.cpp -o /tmp/t_chatin && timeout 30 /tmp/t_chatin` ->
  `RESULT: PASS (test_chat_in)` (rc 0);
  `python3 -m unittest forever/tools/test_chat_in_wiring.py` -> `Ran 2 tests in 0.394s` ... `OK`;
  `python3 forever/tools/test_chat_in_wiring.py --mutate` -> exit 1;
  `python3 forever/tools/slice_gen_context.py --check` -> `SliceAiObjectContext.cpp is up to date`;
  `python3 forever/tools/slice_registry_check.py --quiet` -> `summary: (a)=0  (b)=296 keys to register or stub later` (was 228 before the
  round: +68 names, 43 of them triggers, the rest the actions' names);
  `python3 tools/verify_okf.py` -> `OKF verification PASSED (60 nodes validated, graph edges consistent)`;
  `git diff --check` -> clean.
* `bash tools/verify_all.sh` (the module's own suite, not one of this round's gates) hung far past its own 900 s limit on this workstation
  and was stopped; its OKF step passes on its own, and this round's own tests are the two above. The fix round runs the Turtle build (see
  its gate line in section 6): the round's `ai/` edits are `RtiAction`'s two `getObjectGuid()` arguments, `ItemVisitors`' call to the
  existing `EquipSlotOfProtoCompat`, and the Turtle slot helper's slot hint — all the legacy spellings, so the Turtle tree compiles them.
* Engine suite of the fix round (not one of this round's gates; run because `ai/` files changed):
  `python3 -m unittest discover -s tools -p 'test_*.py'` did not finish within 900 s on this workstation and was stopped. Module by module
  (249 modules, 10 at a time, 240 s each): 244 pass, `test_guild_order_relist_wiring` fails its `HandleMailTakeMoney(` pin — the same
  failure at the round-start commit in a clean worktree (`git worktree add --detach /tmp/wt-head HEAD`), so it is a guild-order mail compat
  rename from another slice, not this round's — and the four data/SQL coverage modules (`test_boss_coverage`, `test_boss_data`,
  `test_boss_tactics_sql`, `test_dungeon_plan_sql`) need more than 240 s each on this workstation (they walk the plan and SQL trees, which
  this round does not touch); their rerun with a 900 s limit per module was still going when the round closed.

## 5. Shared lines touched (other rounds own the files; only these lines)

| File | Lines |
|---|---|
| `forever/src/TbScripts.cpp` | `+1` include, `+1` row at the end of `tbTable`, `+1` `chatin::AddScripts();` in `AddSC_tortoisebots` |
| `forever/src/host/TbEngineBrain.cpp` | `+1` include (`ObjectAccessor.h`), `+1` include (TbChatIn.h), `+1` call in `Tick`, the new `DeliverChat` |
| `forever/src/host/TbPartyState.cpp` | `+1` include, `+1` `chatin::AppendState(rec, guid);` before `return rec` |
| `forever/src/TbBotMgr.cpp` | `+1` include, `+1` `chatin::Forget(leaving);` in `OnPlayerLogout`, `+1` `chatin::DrainArrivals();` at the top of `Update` |
| `forever/src/slice/SliceAiFactory.cpp` | `+1` `reactionEngine->addStrategy("chat");` |
| `forever/tools/slice_gen_context.py` | the own `CH_KEYS` block, appended to `_join(...)` last |
| `forever/slice/SLICE-W1.txt` | `+11` lines (header comment + the round's ten legacy files) |
| `CMakeLists.txt` | the option-OFF filter names `chat/TbChatCmd` (comment says why) |
| `ai/playerbot/strategy/actions/RtiAction.cpp` | `+1` include, `SetTargetIcon` through the new compat (ForeverCore takes the changer's guid), both guids through `getObjectGuid()` |
| `ai/playerbot/strategy/ItemVisitors.h` | `-2/+1`: the first-slot listing calls `EquipSlotOfProtoCompat` (S9, `FindEquipSlot(proto, NULL_SLOT, true)`) instead of the slot-hint helper |
| `ai/cmangos-compat-unported.h` | `ItemProtoFitsSlotCompat` passes the caller's `slot` as the hint (the 1.12 original `FindEquipSlot(proto, slot, true) != slot`) |
| `forever/src/compat/TbCompat_Unported.h` | the same helper's F comment (the hint is the caller's slot; F answers false) |
| `ai/playerbot/strategy/actions/{StatsAction,ListSpellsAction}.cpp` | the 1.12 update fields, skill-line fields and the repair estimate through the same compat (section 6) |
| `ai/playerbot/strategy/actions/HelpAction.{h,cpp}` | `ChatActionContext` (which does not compile here) out; the list comes from `ChatCommandKeys` |
| `ai/playerbot/strategy/generic/ChatCommandHandlerStrategy.h` | `+1` `GetSupported()` accessor (the list `help` shows) |
| `forever/src/slice/SliceLinkStubs.cpp` | `-9` lines: TellLosAction's three stand-ins go (the real file is compiled now), the help-text lookups join the text-manager block |
| `ai/playerbot/UnportedFeatures.h` | the `X(IncomingChat, ...)` text only |

New compat pair for the legacy file this round compiles: `forever/src/compat/TbCompat_CH.h` (ForeverCore) + `ai/cmangos-compat-ch.h`
(Turtle, the original two-argument call) + dispatch `ai/playerbot/TbCompatCH.h` — `SetTargetIconCompat(group, index, unit, changedBy)`.
`Group::SetTargetIcon` on ForeverCore takes the guid of whoever changed the mark (Group.h:361) because the packet echoes it; a bot passes
its own guid, written the legacy way (`bot->getObjectGuid()`, mapped to `GetGUID` by the F shim) so the Turtle tree compiles it too. Turtle
behaviour is unchanged (the helper is the original call).

## 6. Gates (core d02645dd21, slot 8)

The ten legacy files and the two new `forever/src` files the spec lists do not compile as they stand on this core; the census of the ten
said so first (64 error places in 7 files, 45 of them `no member named 'x'`). They were fixed with the both-core pair of section 5 rather
than edited away: update fields (`PLAYER_XP`, `PLAYER_NEXT_LEVEL_XP`, `PLAYER_REST_STATE_EXPERIENCE`, `ITEM_FIELD_*`), the durability
stores' fields, the skill-line fields, `FindEquipSlot` on a prototype, and `help`'s chat context. Nothing invents a value: the core's own
`Item::CalculateDurabilityRepairCost` and `RestMgr::GetRestBonus` answer where this core has the number; the skill ranks are the core's
`MinSkillLineRank`/`TrivialSkillLineRankHigh` (the fields its own skill-up math reads); the two questions the core cannot answer keep the
existing LESS answers (`ItemProtoFitsSlotCompat` -> false, the help texts -> none).

### Census

`FOREVER_CORE=~/ForeverCore-d02645dd FOREVER_SLOT=8 FOREVER_CENSUS_LIST=forever/SLICE-CH.txt forever/tools/vm-census.sh "" 14`, on the finished
tree -> `files 299  compile clean 299  with errors 0  error places 0 (in 0 files)  includes of a header ForeverCore does not have 0`
(run folder `wt-s1r2-s8-SLICE-CH-20261121T052937Z-1205350`). The first run of the round measured 287 files (the list as it stood before the
ten legacy files were added) and was clean; the second, with the complete list, reported the 64 error places section 6 fixes; the third and
this one are the same 299 files clean.

### Builds (through the slot-7 lock, core d02645dd21; the fix round)

* ON, full link:
  `tar -C ~/wt-s1r2 --exclude=.git -cf - . | ssh yulon-ubuntu2 "~/forever-build/slot-sync-build.sh 7 tortoisebots -DTORTOISEBOTS_SLICE1=ON"`
  -> `combined build: slot 7 key core-9b88bd2d6584-opt-85c8cb4e`, `build token 1 of 3 (slot 7)`, `module sync: 403 changed/new/deleted files`,
  `CMAKE_RC=0`, `MAKE_RC=0`, `TOTAL_SECONDS=108`.
* OFF, full link:
  `tar -C ~/wt-s1r2 --exclude=.git -cf - . | ssh yulon-ubuntu2 "~/forever-build/slot-sync-build.sh 7 tortoisebots"` ->
  `combined build: slot 7 key core-9b88bd2d6584-opt-78bf025f`, `build token 2 of 3 (slot 7)`, `module sync: 403 changed/new/deleted files`,
  `CMAKE_RC=0`, `MAKE_RC=0`, `TOTAL_SECONDS=27`.

### Turtle build (the module's other core, fix round)

* `vm-build ~/wt-s1r2 --wait` -> `worktree /home/perzi/wt-s1r2 sha=f25615ef1-dirty dirty-entries=13 files=4087`,
  `module files changed/added/removed: 82`, `COMPILE_SECONDS=2343 CMAKE_RC=0 MAKE_RC=0`, `TOTAL_SECONDS=2348 (compile 2343)  module-tree-hash=0adf67e74139e3f9`,
  `QUEUE_RC=0 QUEUE_RUN_SECONDS=2348`, `job 1-1795241256000042007-260049 rc=0 slot=0`. The Turtle core it built is
  `core-e836b85b8d5c-s0` (the runner's default), so the known `ai/cmangos-compat-unported.h` `ExtractKeyFromLink` break of PR #99 did not
  appear: there was no error at all. `vm-test tb` was not run (not one of this round's gates).

### Builds of the first round (through the slot-8 lock, core d02645dd21)

* ON, full link:
  `tar -C ~/wt-s1r2 --exclude=.git -cf - . | ssh yulon-ubuntu2 "~/forever-build/slot-sync-build.sh 8 worldserver -DTORTOISEBOTS_SLICE1=ON"`
  -> `combined build: slot 8 key core-9b88bd2d6584-opt-85c8cb4e`, `build token 1 of 3 (slot 8)`, `module sync: 347 changed/new/deleted files`,
  `CMAKE_RC=0`, `MAKE_RC=0`, `TOTAL_SECONDS=56`. The first attempts failed and the errors were this round's: `TbChatCmd.cpp` read `Player`
  without `Player.h`; `TbChatIn.cpp` pulled `TbCompat_Unit.h` without the prelude it is written against; `TbScripts.cpp` missed
  `host/chat/TbChatIn.h`; then the link found `TellLosAction`'s three stand-ins in `SliceLinkStubs.cpp` (the real file is compiled now, the
  stand-ins are gone).
* OFF, full link:
  `tar -C ~/wt-s1r2 --exclude=.git -cf - . | ssh yulon-ubuntu2 "~/forever-build/slot-sync-build.sh 8 worldserver"` ->
  `combined build: slot 8 key core-9b88bd2d6584-opt-78bf025f`, `build token 1 of 3 (slot 8)`, `module sync: 1 changed/new/deleted files`,
  `CMAKE_RC=0`, `MAKE_RC=0`, `TOTAL_SECONDS=33`. (The module-target run before it, the brief's command, answered the same
  `CMAKE_RC=0` / `MAKE_RC=0`, `TOTAL_SECONDS=11`.) The first OFF attempt failed in `TbChatIn.cpp`: including the prelude pulled
  `TbCompat_Quest.h` -> `playerbot/policy/quest/QuestFilterPolicy.h`, which the OFF build has no include path for; the hook is core-headers
  only now.

## 7. Live checks (not run this round)

The live server was not used (brief). When it is, with Mcmd as commander and Mwar + Mpriest in his party:
a) whisper `follow`, `stay`, `follow` to Mwar: it stops and resumes within 2 s (`mv=` in `.tb state`); b) party `attack` on a target:
both bots engage (`tgt=`, `stmg`, the engine's `target:` line in `.tb where`); c) whisper `stats` to Mpriest: a reply whisper arrives at
the agent; d) a second agent account outside the party whispers `stay`: ignored, `.tb state` shows `chatlast=not-authorized` and
`chatin=0:1:0`; e) `spell` + a Smite link (`|cff71d5ff|Hspell:585:0|h[Smite]|h|r`): the reply names spell 585; f) 30 minutes with a
command every minute: no crash, no reply loop (a bot's reply is `not-real` and cannot re-enter), `.tb state` counters move only up.
Also: `.tb hear <bot> <sender> whisper stats` with the bot's brain before the next tick -> `verdict=accepted queued=yes`, then the reply.

## 8. Review rounds (FIX-S1R2)

The cold reviews of the round (`claude-s1r2.md`, `codex-s1r2.txt`) found five and two places; each is fixed here, with the test that
fails without the fix named.

1. **Party line in a raid commanded the whole raid** (`TbChatIn.cpp` group hook judged every member of the `Group`). The core broadcasts
   `/p` to the sender's subgroup only (ChatHandler.cpp:386), and the Turtle side never saw the other subgroups' packets. The hook now
   queues the group's guid and the world thread judges each bot with `group->SameSubGroup(senderGuid, member->GetGUID())` (the member-slot
   read the broadcast uses); `Judge` refuses a party or party-leader line for another subgroup as `other-subgroup`, after `not-authorized`.
   Test: pure cases 11 (`FAIL line 231` with the rule removed) and the wiring pins on `SameSubGroup`, its position after the ownership rule,
   and the `Resolve` call (mutation caught: `the party line must be routed by the sender's own subgroup, as the core broadcasts it`).
2. **`RtiAction.cpp:90` passed `bot->GetGUID()`** — the ForeverCore spelling, which the Turtle tree does not know (`getObjectGuid` is the
   legacy call; the F shim maps it). It is `bot->getObjectGuid()` again. Test: the wiring pin (fails on the round-start file with
   `RtiAction still names the ForeverCore-only GetGUID()`; the Turtle `vm-build` is the compile proof).
3. **`ListSpellsAction.cpp:207` changed Turtle behaviour**: `ItemProtoFitsSlotCompat` answered with the first eligible slot
   (`FindEquipSlot(proto, NULL_SLOT, true)`) instead of the caller's slot hint, hiding a ring, trinket or one-hander under finger2, trinket2
   or off-hand. The Turtle helper is now `bot->FindEquipSlot(proto, slot, true) == slot` (the original `!= slot` expression); the first-slot
   listing keeps that meaning through `EquipSlotOfProtoCompat` (S9), which is what `ItemVisitors.h:294` now calls. Test: wiring pins on
   both bodies and on `ItemVisitors` (fails on either round-start file).
4. **The `TbChatIn.cpp` ownership comment was false**: `Group::IsMember` covers the whole raid, so a real raid member may whisper-command
   any bot in the raid (legacy and D2 both allow it). The comment now says so and names the subgroup rule as the narrower case; the notes line
   is corrected in section 1. Test: the wiring pin on the sentence (a reverted comment is mutation `the ownership comment still claims a raid
   stranger is refused`).
5. **A line dropped at the drain was not counted**: `.tb state` showed `chatin` accepted and `chatlast=accepted` for a command that never
   ran. `Inbox::Drop()` adds to `dropped` and sets the last word `dropped`; the drain calls `chatin::Drop` for a gone sender, a lost
   authority and `!AllowActivity()`. `AllowActivity()` stays the gate: it is the legacy packet path's (`PlayerbotAIPackets.cpp:233`) and a
   bot with a real master or a real player in its group is `HAS_REAL_PLAYER_MASTER`/`IN_GROUP_WITH_REAL_PLAYER` (bracket `{0,0}`, always
   active). Test: the pure `Inbox::Drop` case (fails with the old last-word assignment) and the wiring pin on `chatin::Drop` (fails on the
   round-start `TbEngineBrain.cpp`: `a line dropped at the drain must be counted`).

Codex's two findings:

6. **Chat admission read mutable world state from a path that may run off-thread.** The hook now copies only the sender's guid, the target's
   guid, the kind, the language and the text into a 64-line ingress ring under the inbox mutex; `Player`, `Group`, `BotMgr` and binding
   state are read only by `chatin::DrainArrivals()` at the top of `BotMgr::Update` (world thread), which resolves the guids. A line queued
   for a receiver that has logged out or a group that is gone is dropped without a judgement. Test: the wiring pins that the two `OnChat`
   bodies name none of `Consider`, `BotMgr`, `IsBot`, `RealPlayer`, `HasBrain`, `Authorized`, `GetMembers`, `GetGroup` (fails on the
   round-start hook: `the hook reads world state off-thread (Consider()`), and the pure `Ingress` case.
7. **Authority was not revalidated when the queued command ran.** `DeliverChat` now calls `chatin::StillAuthorized(bot, sender)` (the
   admission rule, read again on the world thread) right before `HandleCommand`; a sender that left the party or lost the binding in the tick
   between admission and delivery drops the line, counted as in 5. Test: the wiring pin (fails on the round-start `TbEngineBrain.cpp`:
   `the drain must revalidate the sender's authority before the command runs`).
