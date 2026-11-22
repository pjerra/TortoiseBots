# Player commands, round R4: chat grammar A, registration only

Spec: `~/TortoiseBots-audit/PLAYER-COMMANDS-SPEC.md` (TBF #129 + #132), round R4 of section 6. Branch
`feat/player-commands-r4`, stacked on `feat/player-commands-r2` (PR #197) as the round plan says. No pin move, no ForeverCore core
change, no new .cpp on the build list.

## What the round adds

The chat words whose action is already registered (or whose class is already compiled) now reach the bot when the master whispers them.
One new block, `PC_CHAT_KEYS`, in `forever/tools/slice_gen_context.py`, joined into `KEYS`; the generated
`forever/src/slice/SliceAiObjectContext.cpp` carries the result (25 / 113 / 162 / 201 strategies/actions/triggers/values, from
25 / 108 / 133 / 201). No hand-written source changes: `ChatCommandHandlerStrategy.cpp` and every action file are untouched.

* **29 trigger keys, all already in the full build** (`ChatTriggerContext.h` creators copied byte for byte, as the generator does):
  `quest reward`, `drop`, `roll`, `share`, `loot`, `add all loot`, `release`, `repair`, `u`, `use`, `s`, `b`, `trainer`,
  `attack rti`, `pull`, `end pull`, `accept`, `follow target`, `revive`, `mail`, `ah`, `ah bid`, `ah cancel` (actions already
  registered by earlier rounds), plus `corpse run`, `e`, `equip`, `bb`, `free`, `position` (second group: triggers plus the actions
  whose class is already compiled).
* **5 action keys:** `buy back` (`BuyBackAction`, `BuyAction.cpp`), `corpse run` (`CorpseRunAction`, inline in
  `ReleaseSpiritAction.h`, built through `ReleaseSpiritAction.cpp`), `equip` (`EquipAction.cpp`), `free chat shortcut`
  (`ChatShortcutActions.cpp`), `position` (`PositionAction.cpp`). All five sources were already in `forever/SLICE-W4.txt`; the list
  is unchanged.

## How a whispered word reaches the action (read from the code)

`PlayerbotAI::HandleCommand` queues the line; `HandleCommands` hands it to `ExternalEventHelper::ParseChatCommand`, which tries the
longest prefix down to the first word and calls `AiObjectContext::GetTrigger(<word>)`. The trigger it gets is the
`ChatCommandTrigger` of that key; its `ExternalEvent` marks it triggered and its `Check` returns an event named after the trigger's
own name. `Engine::ProcessTriggers` binds each strategy `TriggerNode` to the trigger of the node's name, so the node's actions run.
Two consequences the round record must keep:

* **A registered trigger key is not a node.** `use` registers (the full build has it) but `ChatCommandHandlerStrategy` only carries
  the `u` node, so a literal `use` word sets a trigger no node watches: `u` is the word that works. The same is true of the legacy
  build; the round mirrors it rather than inventing a second alias.
* **`equip` and `loot` register triggers whose internal name differs from the key** (`equip` -> `e`, `loot` -> `add all loot`), again
  copied from `ChatTriggerContext.h`. The key is what the chat parser looks up, so both the key and the node's action must exist.

## Left out on purpose (the spec's list)

`pullback` (`pullback my target` is not a registered action; `.tb order pullback` exists), `de react all` (strategy edits of other
states: with the behaviour toggles), `guard`, `wander`, `grind`, `runaway`, `warning` (their strategies are not registered), the value
setters (`boost target`, `buff target`, `revive target`, `wait for attack time`, `self res`), `portal` (round 7) and `jump` (D5).

## Files

* `forever/tools/slice_gen_context.py` — +18/-1 lines: the `PC_CHAT_KEYS` block with its left-outs, and the `_join` entry.
* `forever/src/slice/SliceAiObjectContext.cpp` — generated, +37/-3 lines (5 chat actions, 29 chat triggers, the two counters).
* `forever/tools/test_chat_grammar_wiring.py` — new, 243 lines: the block, the generated registry, the strategy's `TriggerNode`
  actions and the build list, with 7 mutations.
* `forever/docs/PC-R4-NOTES.md` — this file.

## Tests and gates

| Gate | Result |
|---|---|
| `python3 -m unittest forever/tools/test_chat_grammar_wiring.py` | 4 tests OK (32 subtest words) |
| `python3 forever/tools/test_chat_grammar_wiring.py --mutate` | exits non-zero, as required |
| `python3 forever/tools/slice_gen_context.py --check` | `SliceAiObjectContext.cpp is up to date` |
| `python3 forever/tools/slice_registry_check.py` | `(a)=0`; `(b)` 300 -> 280 keys (the R4 words left that list) |
| census of `forever/SLICE-W4.txt` | unchanged: the list, and every source it names, is untouched by this branch |
| `tbf-premerge ~/wt-tbf-pcmd4 6` | see the round report (Turtle build + module ON/OFF + the no-direct-queries test) |

The failing case was written first and failed on the pre-R4 source: `test_repair_trigger_registered` (`'repair' not found in {...}`
the generated trigger registry) and `test_r4_block_and_generated_registry` (`forever/tools/slice_gen_context.py has no PC_CHAT_KEYS
block`). Both pass after the block.

## Live

A-9 of the spec is queued on forever-test2 (`LIVE-QUEUE.md`) with the rest of the player-command walks: an owner whispers `repair`,
`trainer`, `release` (dead bot), `u <item>`, `e <item>`, `position`, and a non-owner group member whispers `repair` and must get
"Only this bot's owner (or a GameMaster) may do that." The reachability caveats above (`use`) are what that walk checks.
