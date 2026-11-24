# Player commands, round R5: chat grammar B (small legacy action files)

Spec: `~/TortoiseBots-audit/PLAYER-COMMANDS-SPEC.md` (TBF #129 + #132), round R5 of section 6. Branch `feat/player-commands-r5`,
stacked on `feat/player-commands-r4` (draft PR #204), which is stacked on R2 (PR #197). No pin move, no ForeverCore core change.

## What the round adds

Six chat words (ten trigger keys) whose action needs one small legacy file each, now compiled into the module and registered:

| Word (trigger keys) | Action key | Legacy file | Whisper tier (legacy gate, unchanged, D-5) |
|---|---|---|---|
| `who` | `who` | `WhoAction.cpp` | tactical (any group member the gate lets in) |
| `c`, `items`, `inventory`, `inv` | `item count` | `TellItemCountAction.cpp` | tactical |
| `ue` | `unequip` | `UnequipAction.cpp` | owner or GM |
| `quests` | `quests` | `ListQuestsActions.cpp` | owner or GM (unlisted word: the gate's fail-closed default) |
| `leave` | `leave` | `LeaveGroupAction.cpp` | owner or GM |
| `rep`, `reputation` | `reputation` | `TellReputationAction.cpp` | owner or GM (unlisted word: fail-closed default) |

* `PC_CHAT_B_KEYS` in `forever/tools/slice_gen_context.py`, joined into `KEYS`; `SliceAiObjectContext.cpp` regenerated.
* The six sources are on `forever/SLICE-W4.txt` (the list the module build reads).
* One hand-written strategy change: an `inventory` TriggerNode next to `inv` in `ChatCommandHandlerStrategy.cpp`. `inventory` was a
  registered trigger key that no node watched (the same dead-word trap as R4's `use`), so the word did nothing; the wiring test fails
  for it.
* Permission tiers are the whisper gate's (`PlayerbotAI::AuthTierForCommand`, D-5, not edited): a led bot answers `who` and `c` to any
  group member the gate admits; `ue`, `leave`, `quests`, `rep` are the owner's (the gate's `IsBotOwnerOrGm`) or a GM's. `.bot` is not
  involved (the chat words go through the bot's own whisper path). A new wiring test pins both lists against the gate source.

## Compat (the files differ by a few lines from the Turtle originals)

New helper pair `forever/src/compat/TbCompat_PC5.h` (F) and `ai/cmangos-compat-pc5.h` (T), reached through
`ai/playerbot/TbCompatPC5.h`; the legacy action files call the helpers instead of the Turtle engine:

* `AutoStoreItemCompat` (unequip): the typed `AutoStoreBagItem` packet on F, the legacy packet on T.
* `BotLeaveGroupCompat` (leave): the typed `LeaveGroup` packet on F (the core's handler refuses a battleground and a bot without a
  group), the disband packet on T.
* `SkillMaxOfCompat`, `GuidLinkNumberCompat` (who), `ReputationRankSpanCompat` (rep: F has no public rank table; the eight rank
  sizes are the same, the ladder starts at `Reputation_Bottom`).
* Existing helpers reused: `UnitInMapCompat`, `FactionOfTemplateCompat`, `FactionEntryOfCompat`, `FactionNameCompat`,
  `AreaNameCompat`. `rep` also gained two null checks the original lacked (no faction template / entry: answer nothing).
* No file exceeded the ~80-line compat budget; none was dropped.

Left out of the round: `ll` and `ss` (`LootStrategyAction.cpp`, `SkipSpellsListAction.cpp`), not in the words assigned to this round.

Residual: the trade-skill link in a `who <skill>` reply carries the character's database counter where Turtle carried the 64-bit guid;
the live check reads one back on the client.

## Review (Codex adversarial)

* Codex [medium]: `who`, `c`, `items`, `inventory`, `inv` are Tactical on the legacy gate, which lets in anyone `LevelFor` admits, and that
  admits outsiders for a random bot, so a stranger could read a bot's class/spec/gear or inventory. Refuted for this module, not changed:
  a line reaches the legacy gate only after the host's own rule (`TbChatIn.cpp` `Authorized`: the bot's master, its commander, or a real
  member of the bot's group); a stranger's whisper is dropped as `not-authorized` before the legacy code runs (the S1-R2 live check
  already saw `chatlast=not-authorized`). The new test `test_tactical_words_reach_only_the_bots_own_group` pins both halves. The live row
  adds a stranger's `who` and `c`: no answer.

## Tests and gates

| Gate | Result |
|---|---|
| `python3 -m unittest forever/tools/test_chat_grammar_wiring.py` | 10 tests OK (R4 + R5, incl. the 8-mutation R5 test) |
| `python3 forever/tools/slice_gen_context.py --check` | up to date |
| `python3 forever/tools/slice_registry_check.py` | `(a)=0` |
| module build, slot 6 (`tortoisebots` and `worldserver`, SLICE1=ON) | CMAKE_RC=0, MAKE_RC=0 |
| `tbf-premerge ~/wt-tbf-pcmd5 6` | `PREMERGE: PASS` (no-direct-queries test, Turtle build, module ON, module OFF) |

The failing case was written first: `test_who_trigger_and_action_registered` (`who` had a trigger but no action key). Not run: the
per-file census of the new list (the module and worldserver builds compile and link every file on it).
`test_w1_wiring` / `test_wl0_wiring` / `test_class_quest_spells_wiring` fail on the R2 base already (they pin the pre-R2 `Login` signature
and a build-box path); this round does not touch them.

## Live

Queued on forever-test2 (`LIVE-QUEUE.md`, spec A-10): the owner whispers `who`, `c`, `quests`, `rep` (with a selected mob), `ue <slot item>`,
`who <skill>`; a non-owner group member whispers `who` and `c` (answered) and `ue`, `leave`, `quests` (refused with "Only this bot's owner
(or a GameMaster) may do that."); the owner whispers `leave` and the bot leaves the party.

## Fix round (live batch 4 on forever-test2, rows 16-23, 36, 41)

### 1. `quests` crashed the worldserver (SIGSEGV, exit 139, twice)

Diagnosis of the batch worker, checked against the code: right about the value, one detail off about the read.

* `ListQuestsAction::ListQuests` builds `PlayerTravelInfo info(bot)` on every call (`ListQuestsActions.cpp:60`, before any detail test). The
  constructor reads `AI_VALUE(focusQuestTravelList, "focus travel target")` (`TravelMgr.cpp:72`). The direct read in `ListQuestsActions.cpp:108`
  and the `travel target` read (`:73`) only run for the `travel` sub-command; `travel target` is registered, so they are not the crash.
* The slice did not register `focus travel target` (`ValueContext.h:501` has it), and its missing-value stand-in
  (`SliceMissingKeyGuard.h`) lists `std::set<uint32>&` but not the by-value `std::set<uint32>` (`typedef focusQuestTravelList`,
  `TravelValues.h:103`). `GetValue<focusQuestTravelList>` is a non-virtual template that `dynamic_cast`s the stand-in to `Value<T>`:
  no such base, null, `->Get()` faults. The log line `slice: missing value focus travel target` is the guard reporting the miss first.
* Fix: (a) `focus travel target` registered (`PC_CHAT_B_KEYS` value block, copied creator, `FocusTravelTargetValue` is inline in
  `TravelValues.h`, already included); `experience` and `honor` too (`LeaveGroupAction.cpp:144`, `MEM_AI_VALUE` dereferences a missing one,
  `StatsValues.h:231/245`, inline); no engine call, so no compat pair was needed. (b) the stand-in now also covers `focusQuestTravelList`
  and `WorldPosition` (`current position` `ReleaseSpiritAction.h:305`, `home bind` `TravelNode.cpp:1743`: the same latent crash for
  any bot that reached them). `slice_registry_check.py` now prints no `[stand-in lacks: ...]` line, and the test fails if one comes back.
  The action itself holds no null pointer: the crash was in a value lookup, so no action guard applies.
* Set found by `forever/tools/test_pc_r5_fix.py::r5_read_set`: every value-macro / `GetValue` string in the six R5 action sources and
  their headers (`slice_registry_check.refs_in`), the `PlayerTravelInfo` constructor body, and the three name tables it loops over
  (`TravelMgr.h:121-146`). Ten of the table names stay unregistered on purpose (bool/uint8/uint32, answered false/0 by the stand-in; the
  test pins the list and that the stand-in covers each type).

### 2. Owner-tier words on the owner's other game account (ADR 0005, PLAYER-COMMANDS-SPEC D-2 as amended)

`PlayerbotAI::IsBotOwnerOrGm` ended on `AccountIdOfCharacterCompat(bot) == senderAccount` (game accounts). It now ends on
`BotOwnedBySenderCompat(bot, fromPlayer, senderAccount)` (`PlayerbotAI.cpp:2908`), a both-core pair (`TbCompat_Owner.h` F,
`cmangos-compat-owner.h` T, switch `TbCompatOwner.h`). Turtle keeps the game-account comparison (no Battle.net there). ForeverCore calls
`TortoiseBots::player::CallerOwnsBotForChat` (`TbPlayerCmd.cpp`), which is the `.bot` read: `ScopeOf(sender)` (the sender's Battle.net
account, valid only while its own game account is still linked) and `scope.Owns(botGameAccount)` (`Battlenet::AccountMgr::GetIdByGameAccount`,
ForeverCore `BattlenetAccountMgr.cpp:183`, a login-DB read through the core, no module query), bot game account from
`sCharacterCache->GetCharacterAccountIdByGuid` (`CharacterCache.h:75`); nothing is kept between messages; the verdict is the pure
`ChatOwnerAllowed` (`TbPlayerAuthPure.h`, g++-tested). GMs pass earlier in `IsBotOwnerOrGm`. The tiers are unchanged
(`AuthTierForCommand` not edited): the Tactical words stay open to the bot's own group, a stranger or a group member who is not the owner is
still refused "Only this bot's owner (or a GameMaster) may do that." Re-check live: owner-tier words (`repair`, `trainer`, `u`, `use`, `loot`,
`bb`, `quests`, `rep`) to the bot on the owner's other game account now act; `quests` on a same-account and an other-account bot answers and
the server stays up; a stranger (B/C) is still refused.

### Review of the fix (Codex adversarial)

* Codex [high]: a durable `BotRecord` / `OwnedCharacter` owner that differs from the sender still falls through to the Battle.net helper.
  Refuted for ForeverCore, not changed: the host's `BotManager::FindBot` returns null and `GetOwnedCharacter` returns false for every bot
  (`TbHostBotManager.cpp:56-64`: no legacy record on this host), so the two record branches never run and the helper is the only owner path,
  which is exactly the `.bot` rule (`Decide`, `TbPlayerAuthPure.h`: the bot's game account's Battle.net account against the caller's). On
  Turtle the helper is the old comparison and the record branches are unchanged. `test_forever_has_no_durable_owner_record_for_the_fallback_to_override`
  pins both answers.
