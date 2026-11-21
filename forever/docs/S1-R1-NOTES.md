# S1-R1 (chat links): one core-free parser, a compat pair, `.tb link`

Round S1-R1 of `~/TortoiseBots-audit/FOREVER-SMALL-SLICES-SPEC.md` (section 3), the five Lead decisions accepted (D3 here is
prepared, not made: the quest branch's `ExtractQuestIdCompat` is untouched). Worktree `~/wt-s1r1`, branch `slice/s1r1` off
`tbf/main` 495ffd943; the fix round's gates use core d02645dd21 and slot 6 (the lead moved the round off slot 2, whose core predates `IsKickRequested`). Zero core changes.

## 1. The parser (`forever/src/compat/TbChatLinkPure.h`, namespace `TortoiseBots::chatlink`)

Core-free: `<cstdint>`, `<string_view>` and the standard containers only, so `forever/tools/test_chat_link.cpp` compiles it with a bare
`g++ -std=c++17 -I.`. The item scan and numeric field rule are shared with the bot brain through `ai/playerbot/ChatLinkScan.h`. Eight
functions:

| Function | Answer |
|---|---|
| `LinkId(text, tag)` | the id of the FIRST `\|H<tag>:<digits>`; the tag must end at the `:` (so `item` does not match `\|Hitemset:`); a digit run over 9 digits is not an id and the scan goes on |
| `QuestId(text)` | the `quest` link, else the trimmed text of 1-9 digits (the quest branch's bare-number rule), else 0 |
| `SpellId(text)` | the `spell` link, then the `enchant` recipe link, else a bare number, else 0. `talent` (its RankID lookup is not ported) and `trade` (another layout) answer 0: LESS, never more |
| `AllIds(text, tag)` | every id of every such link, once each, in a `std::set` |
| `ItemLinks(text)` | the id and qualifier of every `Hitem:` link, in written order, from the one scan in `ai/playerbot/ChatLinkScan.h` that `ChatHelper::parseItemQualifiers` also reads; an unterminated link ends the scan, and a link without a 1-9 digit id is not an item |
| `ItemQualifiers(text)` | the qualifier of every link `ItemLinks` answers (between `Hitem:` and the next `\|`), in the same order |
| `LegacyItemQualifier(q)` | the 1.12 qualifier layout `id:enchant:randomProperty[:suffix]`: 3 or 4 fields keep their fields, each written as a number (`ai::chatlink::FieldValue`: 1-9 digits, else 0); a longer (modern) one becomes `id:enchant:0` |
| `EncodedValue(text)` | grammar-2 encoding of a reply value: `[A-Za-z0-9._-]` and the qualifier's `:` stand, every other byte is `%XX` |

Why the last rule: ForeverCore's item link has 13 fields and its field 3 is a **gem** (`HyperlinkTags.cpp:283`, `LinkTags::item::StoreTo`, at
the pin: `itemId, EnchantId, GemItemId[0..2], ...`), while 1.12 writes the random property there
(`ItemQualifier::ItemQualifier`, `ai/playerbot/strategy/values/ItemUsageValue.cpp:71`, reads field 3 as `randomPropertyId`). The port has no
random property (`item random properties` row, `UNPORTED.md`), so keeping the modern field would name items with a random property of 1-5
that does not exist. The real client proves the split: a Turtle client writes `|cff1eff00|Hitem:10090:0:960:0|h[Gothic Plate Helmet of the
Gorilla]|h|r` (960 = the Gorilla random property, from `~/TortoiseBots-audit/donated/exports-20261103/data/upload_11021/WoWCombatLog.txt`).

## 2. The compat pair and the two call sites

`forever/src/compat/TbCompat_CL.h` + `ai/cmangos-compat-cl.h` + the dispatch `ai/playerbot/TbCompatCL.h` (`__has_include`, the P11 pattern).
The Turtle side is the original expression, moved not changed: `handler->ExtractSpellIdFromLink(&source)` and the qualifier identity. The
ForeverCore side answers `chatlink::SpellId` / `chatlink::LegacyItemQualifier`.

* `ai/playerbot/PlayerbotAI.h:71`: `uint32 extractSpellId(std::string str) { return ExtractSpellIdCompat(this, str); }` (was the
  `UnportedBlockResultCompat(Feature::ChatLinks, ...)` block at :70-75) and the header includes `playerbot/TbCompatCL.h`. The class also
  declares the helper a friend (`template <class Handler> friend std::uint32_t ExtractSpellIdCompat(Handler*, std::string);`): the core's
  `ExtractSpellIdFromLink` is protected and `PlayerbotChatHandler` inherits `ChatHandler` protected, so a helper outside the class can only
  reach it as a friend (the first Turtle build failed on exactly this: `error: 'uint32 ChatHandler::ExtractSpellIdFromLink(char**)' is
  protected within this context`). The same friend is declared for `ExtractQuestIdCompat`: `tbf/main` (W3, 0d27298a9) calls it from
  `PlayerbotAI.cpp:140` and its Turtle body (`ai/cmangos-compat-unported.h:126`) has the same protected call with no friend, so the Turtle
  build of the unmodified main stops there (`error: 'char* ChatHandler::ExtractKeyFromLink(char**, const char*, char**)' is protected
  within this context`) - a pre-existing break this round repairs, behaviour unchanged (friendship is compile-time only).
* `ai/playerbot/ChatHelper.cpp:408`: `for (ai::chatlink::ItemLink const& link : ai::chatlink::ItemLinks(text))` then
  `qualifiers.insert(ItemLinkQualifierCompat(link.qualifier));`, and the file includes `playerbot/TbCompatCL.h` and
  `playerbot/ChatLinkScan.h`. The scan moved into the shared header; `parseGameobjects`, `parseWorldEntries`, `parseValue` and the two
  `find(...) != -1` tests keep their behaviour with `std::size_t`/`std::string::npos` positions.

Both cores keep their own semantics: on Turtle the helper is the same call the member made before (so no Turtle behaviour change), on
ForeverCore it is the 1.12 layout read out of the modern text.

## 3. `.tb link <text> [#token]` (`forever/src/host/TbChatLinkCmd.{h,cpp}`, row appended at the end of `tbTable`)

Works in BOTH builds (no engine, no bot): it answers with the same parser through the pair, so the console shows what the brain would get.
Reply, grammar version 2 (TB-STATE-GRAMMAR-V2): `link-begin`, one `link quest= spell= items= quals=` record, `link-end count=1 records=1`;
`error code=missing-argument` for no text and `error code=bad-token` for a malformed `#` word, refused before the text is read. `items=` and
`quals=` come from one `ItemLinks(text)` scan, so a link is never in one list and not the other. Every value passes `EncodedValue`, whose
safe set is `[A-Za-z0-9._-]` plus the qualifier's `:` (the command needs no encoder from the engine build: it cannot use `Reply`, TbH3.cpp is
filtered out of the default build); a space, `=` or newline from the text therefore cannot add a key or split the record. The two fixed
refusal texts are written pre-encoded.

## 4. Tests

* `forever/tools/test_chat_link.cpp`: first case `modern_item_gem_not_random_property`
  (`|cnIQ4:|Hitem:19019::3::::::60:::::::::|h[Thunderfury]|h|r` -> `19019:0:0`), plus the quest, spell, enchant, talent, trade, bare-number,
  two-item, dedup and `|Hitemset:` cases and the 1.12 sample `|cffffffff|Hitem:2512:0:0:0|h` unchanged. Failing first: with the old behaviour
  (the qualifier returned as written) the run printed `FAIL line 22: LegacyItemQualifier("19019::3::::::60:::::::::") == "19019:0:0"` and
  exited 1. Review fixes: a 511-character message of five modern links whose fifth starts at index 416 must finish and read all five
  (the old `uint8 pos` loop was run against the same text under `timeout 10` and killed at rc 124, see §7); an enchant above 2^31
  (`3000000000`) and above 2^32 (`5000000000`) -> `19019:0:0`; a non-numeric or space/`=`/newline field -> 0; the real unterminated
  `|Hitem:19019` -> empty; `items=`/`quals=` from one scan (a `Hitem:` with no digit id is in neither, every qualifier starts with its id);
  `EncodedValue` for space, `=`, newline and `%`.
* `forever/tools/test_cl_wiring.py`: `PlayerbotAI.h` calls the helper and holds no `Feature::ChatLinks`/`UnportedBlockResultCompat` in that
  member; `ChatHelper` reads `ai::chatlink::ItemLinks` and holds no 8-bit chat position; `ItemQualifier` reads all three fields through
  `ai::chatlink::FieldValue` and holds no `stoi`; both sides of the pair define both helpers and the Turtle side is the original call;
  `.tb link` is registered, names no engine type, uses one `ItemLinks` scan and encodes its qualifier; `ExtractQuestIdCompat` untouched;
  TB-COMMANDS documents the command; `SLICE-CH.txt` is `SLICE-W1.txt` plus this round's files. Ten mutations (old unported block back,
  friend gone, raw qualifier back, 8-bit scan back, row removed, an engine reply type, two scans, no encoder, `stoi` back, the round's file
  out of the list) each raise, plus `--mutate` exits 1.

## 5. Lists, rows, docs

* `forever/SLICE-CH.txt` = `forever/SLICE-W1.txt` verbatim + the round's seven paths (the census reads the `.cpp`; headers listed for the
  record).
* `forever/docs/UNPORTED.md` and `ai/playerbot/UnportedFeatures.h`: the row and enum text reworded to `quest link: waits for D3` (call sites
  2 -> 1: only `PlayerbotAI.cpp`'s `extractQuestId` is left). The enum key `ChatLinks` and the row stay until the quest branch calls
  `chatlink::QuestId` (D3, whoever merges second), then both go.
* `forever/docs/TB-COMMANDS.md`: section 17 for `.tb link`, and the "build without the engine" bullet names it as the one exception.

## 6. Shared lines touched (other rounds own the files; only these lines)

| File | Lines |
|---|---|
| `ai/playerbot/PlayerbotAI.h` | `+1 #include "playerbot/TbCompatCL.h"`; the four lines of `extractSpellId` become one, `+2` lines of the friend declaration it needs |
| `ai/playerbot/ChatHelper.cpp` | `+1 #include "playerbot/TbCompatCL.h"`, `+1 #include "playerbot/ChatLinkScan.h"`; the `parseItemQualifiers` body (one `ItemLinks` loop + insert); `std::size_t`/`std::string::npos` positions in `parseGameobjects`, `parseWorldEntries`, `parseValue` and the two `find(...) != -1` tests |
| `ai/playerbot/strategy/values/ItemUsageValue.cpp` | `+1` include; the three field conversions in the `ItemQualifier(std::string)` body |
| `ai/playerbot/UnportedFeatures.h` | the `X(ChatLinks, ...)` text only |
| `forever/src/TbScripts.cpp` | `+1 #include "host/TbChatLinkCmd.h"`; `+1` row at the end of `tbTable` |
| `forever/docs/UNPORTED.md` | the one row |
| `forever/docs/TB-COMMANDS.md` | the engine-less bullet, the new section 17 |
| `forever/SLICE-W1.txt` | not edited |

New files only elsewhere: the parser, the pair, the wrapper, the shared scan (`ai/playerbot/ChatLinkScan.h`), the command, both tests,
`SLICE-CH.txt`.

## 7. Gates

* Census, `FOREVER_CORE=~/ForeverCore-25fc7ca FOREVER_SLOT=1 FOREVER_CENSUS_LIST=forever/SLICE-CH.txt forever/tools/vm-census.sh "" 14`:
  `files 287  compile clean 287  with errors 0  error places 0 (in 0 files)  includes of a header ForeverCore does not have 0`
  (run folder `wt-s1r1-s1-SLICE-CH-20261120T194807Z-1452976`); repeated on the finished tree in the private slot 91
  (`wt-s1r1-s91-SLICE-CH-20261121T003322Z-1407229`) -> the same line, `files 287 compile clean 287 with errors 0`.
* Tests: `RESULT: PASS (test_chat_link)`; `python3 -m unittest forever/tools/test_cl_wiring.py` -> OK; `--mutate` -> exit 1.
* Registry/generator: `slice_gen_context.py --check` -> `SliceAiObjectContext.cpp is up to date`; `slice_registry_check.py --quiet` ->
  `(a)=0 (b)=228 keys to register or stub later` (unchanged from tbf/main: the round registers no key).
* ForeverCore ON build, first attempt: `FOREVER_SLOT=1 FOREVER_CMAKE_EXTRA=-DTORTOISEBOTS_SLICE1=ON build.sh 12 worldserver` ->
  `MAKE_RC=1`, and the errors are not this round's files: `TbCompat_I13.h` not found and `CcGuard::IgnoredMobNear` missing while
  `RemoveAuraAction.cpp`/`GenericSpellActions.cpp` compile. Cause: `build.sh`'s shared tree is keyed on the CORE fingerprint and the CMake
  options, not on the module, so two slots building the same core with the same options share one tree and each job re-syncs its own module
  into it; another job of that key replaced the module under this build (the tree held that branch's `WL4` files and lacked this tree's
  `TbCompat_I13.h` when the log was read; a dry-run sync then showed 455 differences). The re-run below uses a private key.

* ForeverCore ON build: `FOREVER_SLOT=91 FOREVER_CMAKE_EXTRA="-DTORTOISEBOTS_SLICE1=ON -DTORTOISEBOTS_S1R1_OWN=1" build.sh 12
  worldserver` -> `CMAKE_RC=0 MAKE_RC=0` (key `core-af0b412975f7-opt-74f93307`, 296 targets; binary
  `~/forever-build/worldserver-s1r1-final`, sha256 `8954f27968e97a8d65267fefb22722058314d2c7aaac304c4e738b2391d80830`).
* ForeverCore OFF build: `FOREVER_SLOT=91 build.sh 12 tortoisebots` -> `CMAKE_RC=0 MAKE_RC=0` (key
  `core-af0b412975f7-opt-78bf025f`, `TbChatLinkCmd.cpp.o` built). The private marker option keeps this round off the shared ON key,
  where two slots' modules overwrite each other (see the first attempt above).
* Slot 1 was replaced twice under this round (its core and module became another branch's; the first OFF attempt then measured that
  tree, including a stale PCH seeded from a 6517af05 build). The stable uploads and builds therefore use a private slot 91
  (`src-91`/`build-91`), which no other worker uses; the 25fc7ca pin was re-uploaded there and checked
  (`WorldSession.cpp` sha256 `c80cc70...`).

* Turtle unit tests on the final tree: `vm-test tb ~/wt-s1r1` (log
  `~/TortoiseBots-audit/vm-test-logs/wt-s1r1-20261121-004900-690549-091f.log`) -> `Ran 2673 tests`,
  `FAILED (failures=1, skipped=61)`, the one failure `test_guild_order_relist_wiring.test_mail_take_settles_the_ledger_at_any_order_state`
  (`HandleMailTakeMoney(` absent from `MailAction.cpp`): it fails identically on tbf/main's own files (checked by running that test against
  `git show tbf/main:...` copies), a W2/guilds leftover, not this round. The live action oracle flagged `Mage stay FAILED x9+ at 4.0s` in 2 of
  3 runs (1 PASS); the same line is in logs from 2026-11-04 (e.g. `wt-zg-batch9-20261104-130857.log`), so it is a pre-existing flake.
* Turtle module build: `vm-build ~/wt-s1r1 --wait` -> `QUEUE_RC=0`, `CMAKE_RC=0 MAKE_RC=0`, `COMPILE_SECONDS=5040` (job
  `1-1795215754827675480-2659800`, tree sha f0dbf0771 - every later commit on the branch is docs/notes only, the `ai/` code is the same).

## 8. Review fixes (S1-R1 fix round)

| Finding | Fix |
|---|---|
| [claude HIGH] `parseItemQualifiers` kept its position in a `uint8`; a `Hitem:` at 250 or later wrapped and the loop never ended ([codex high, same path]) | the scan lives in the shared `ai/playerbot/ChatLinkScan.h` with `std::size_t` positions and `std::string_view::npos`; `ChatHelper::parseItemQualifiers`, `parseGameobjects`, `parseWorldEntries`, `parseValue` and the two `find(...) != -1` tests are `std::size_t`/`npos`; the 511-character five-link test hangs on the old loop (rc 124 under `timeout 10`) and passes now |
| [claude MEDIUM, codex HIGH] `LegacyItemQualifier` copied the enchant raw; a core-valid `EnchantId` over 2^31 reached `std::stoi` and threw | every numeric field goes through `ai::chatlink::FieldValue` (1-9 digits, else 0; no throw), for the modern and the 3/4-field layouts; `ItemQualifier(std::string)` reads all three fields the same way and holds no `stoi`; tests over 2^31 and 2^32 |
| [claude LOW] `quals=` carried raw qualifier text, so a space, `=` or newline could add a key or a line | the command writes `EncodedValue(legacy)`; on top of that, every qualifier field is numeric, so a hostile field is 0 before it is encoded; `EncodedValue` is tested for space, `=`, newline and `%` |
| [claude LOW] the "unterminated qualifier" test was a terminated link, and nothing pinned the real one | `ItemQualifiers("\|Hitem:19019").empty()` and `ItemQualifiers("\|Hitem:19019:0:0:0").empty()` pin the real case; the `\|h` case is relabelled terminated |
| [claude LOW] `items=` and `quals=` came from two rules and could disagree | both are built from one `ItemLinks(text)` scan in `.tb link`, and `ItemQualifiers` is that same list; the test pins a bare `Hitem:5:0:0\|` and a `Hitem:abc` in both or neither |
| [codex HIGH] bots still do not read modern quest or talent links | not this round by decision: the spec's seam table reserves `ExtractQuestIdCompat` for the quest branch (D3, whoever merges second) and the talent RankID lookup is unported; `.tb link` reports the parser's answer (783 for a quest link, 0 for a talent), which is the same answer the bot gets |
| [codex HIGH] end-to-end tests through ChatHelper and ItemQualifier | neither compiles without the core, so the test compiles the code they now share (`ChatLinkScan.h`: the scan and `FieldValue`) and the wiring test pins both call sites (`ChatHelper` -> `ItemLinks`, `ItemQualifier` -> `FieldValue`, no `stoi`) |

## 9. Live (forever-run2, 2026-11-21)

Server lock held for the whole test; databases dumped first (`backups/pre-s1r1-20261120T224245Z.sql.zst`, auth/characters/world/hotfixes),
`bin/worldserver.pre-s1r1` backed up, this round's binary installed (`sha256 8954f27...`, the same file as `~/forever-build/worldserver-s1r1-on`),
booted to `worldserver-daemon) ready`, then the binary and the databases restored and the restored server boot-checked (`ready`, `tb status` ->
`TortoiseBots: enabled, 0 bot session(s)`).

`.tb link` on the four spec samples, a real client link and the edge cases (console output, verbatim):
```
link-begin
link quest=0 spell=0 items=19019 quals=19019:0:0          |cnIQ4:|Hitem:19019::3::::::60:::::::::|h[Thunderfury]|h|r
link quest=783 spell=0 items= quals=                       |cffffff00|Hquest:783:1:1:60|h[A Threat Within]|h|r
link quest=0 spell=585 items=                              |cff71d5ff|Hspell:585:0|h[Smite]|h|r
link quest=0 spell=0 items=2512 quals=2512:0:0:0           |cffffffff|Hitem:2512:0:0:0|h[Rough Arrow]|h|r
link quest=0 spell=0 items=10090 quals=10090:0:960:0       |cff1eff00|Hitem:10090:0:960:0|h[Gothic Plate Helmet of the Gorilla]|h|r (a real 1.60 client link: field 3 = 960, the Gorilla random property, kept)
link quest=783 spell=783 items=                            783
link quest=0 spell=3919 items=                             |Henchant:3919|h[Engineering: Rough Dynamite]|h
link quest=0 spell=0 items=                                |Htalent:2232:-1|h[Taste for Blood]|h (talents unported: 0)
error code=missing-argument cmd=link msg=.tb%20link%20%3Ctext%3E
error code=bad-token cmd=link msg=a%20token%20is%20%23%20and%201%20to%2032%20of%20A-Za-z0-9_.%3A-%3B%20nothing%20was%20done
link-begin #l9
link quest=0 spell=0 items=19019 quals=19019:0:0 #l9
link-end count=1 records=1 #l9
```
Every reply carried the `link-end count=1 records=1` marker (elided above); the last case is the same link with a token.

## 7b. Fix-round gates (core d02645dd21, slot 6)

The failing-first evidence for the high finding: the old loop (`uint8 pos`, the same five modern links, 511 characters, fifth at index 416)
run under `timeout 10 /tmp/old_loop` printed `len=511 last=416` and then `rc=124`: it never finished. The fixed test finishes:
`g++ -std=c++17 -Wall -Wextra -I. forever/tools/test_chat_link.cpp -o /tmp/t_chatlink && timeout 20 /tmp/t_chatlink` ->
`RESULT: PASS (test_chat_link)` (rc 0).

* `python3 -m unittest forever/tools/test_cl_wiring.py` -> `Ran 2 tests` ... `OK`; `python3 forever/tools/test_cl_wiring.py --mutate` -> rc 1.
* `python3 forever/tools/slice_gen_context.py --check` -> `SliceAiObjectContext.cpp is up to date`; `python3 forever/tools/slice_registry_check.py --quiet` ->
  `summary: (a)=0  (b)=228 keys to register or stub later` (unchanged from `tbf/main`).
* ForeverCore ON build, through the slot-6 lock (the lead's slot; slot 2's core predates `IsKickRequested`):
  `tar -C ~/wt-s1r1 --exclude=.git -cf - . | ssh yulon-ubuntu2 "~/forever-build/slot-sync-build.sh 6 tortoisebots -DTORTOISEBOTS_SLICE1=ON"`
  -> `combined build: slot 6 key core-9b88bd2d6584-opt-85c8cb4e`, `build token 1 of 3 (slot 6)`,
  `module sync: 325 changed/new/deleted files`, `CMAKE_RC=0`, `MAKE_RC=0`, `TOTAL_SECONDS=1694`.
* ForeverCore OFF build, the same command with no extra -> `combined build: slot 6 key core-9b88bd2d6584-opt-78bf025f`,
  `build token 2 of 3 (slot 6)`, `module sync: 325 changed/new/deleted files`, `CMAKE_RC=0`, `MAKE_RC=0`, `TOTAL_SECONDS=39`.
  (The wrapper shell printed `ON_DONE rc=1`/`OFF_DONE rc=1`: `build.sh`'s last command is `grep -n -E "error:|FAILED:" make.log`, whose
  no-match status is 1; neither build printed an error line.)
* `bash tools/verify_all.sh` -> `Ran 2673 tests in 173.704s`, `FAILED (failures=1, skipped=19)`, the one failure
  `test_guild_order_relist_wiring.GuildOrderRelistWiringTest.test_mail_take_settles_the_ledger_at_any_order_state`
  (`HandleMailTakeMoney(` absent): the same pre-existing W2/guilds failure recorded in §7, not this round.
* Turtle module build: `vm-build ~/wt-s1r1 --wait` -> `QUEUE_RC=0`, `CMAKE_RC=0 MAKE_RC=0`, `COMPILE_SECONDS=73`, job
  `1-1795222932522782800-3105717`, tree `77781c95f`, `module-tree-hash=9e3a0c697bdd0e5b`.
* Census, `FOREVER_CORE=~/ForeverCore-d02645dd FOREVER_SLOT=6 FOREVER_CENSUS_LIST=forever/SLICE-CH.txt forever/tools/vm-census.sh "" 14`:
  `files 287  compile clean 287  with errors 0  error places 0 (in 0 files)  includes of a header ForeverCore does not have 0`
  (run folder `wt-s1r1-s6-SLICE-CH-20261121T020912Z-2709193`).
* Turtle unit tests: `vm-test tb ~/wt-s1r1` -> `Ran 2673 tests`, `FAILED (failures=1, skipped=61)`, the same
  `test_mail_take_settles_the_ledger_at_any_order_state` failure (`RESULT: FAIL kind=tb wt=wt-s1r1 sha=ff1747009`, log
  `~/TortoiseBots-audit/vm-test-logs/wt-s1r1-20261121-021516-1959063-679a.log`); the live oracle's `Mage stay FAILED x9+ at 4.0s` in
  2 of 3 runs is the pre-existing flake recorded in §7. This round's two tests are in the 2673. The brief did not ask for a live run.

## Round 2 fix round (the last one): narrow indexes, signed random property, throwing conversions

Review `~/TortoiseBots-audit/reviews/claude-s1r1-final.md`. The parsers are in `ai/playerbot/ChatLinkScan.h` (core-free), so one test proves what the world thread runs.

1. HIGH `ChatHelper.cpp` `parseMoney` (`for (uint8 i ...)` over `text.length()`, spun forever from 256 characters made of digits and g/s/c): now `ai::chatlink::ParseMoney`
   (`size_t`, sums saturate at 2^32-1 instead of wrapping; same result for every ordinary amount). Reached from `ChatHelper::parseable` (the text filter, ChatHelper.cpp:~890),
   `SendMailAction.cpp:45`, `TradeAction.cpp:58`, `AhAction.cpp:142` and `:381`. Sweep (git grep for `uint8/uint16/int8 i|pos`, `.size()`/`.length()`/`find(` loops) over ChatHelper.cpp,
   ChatFilter.cpp, the four actions and `ai/playerbot`; sites changed:
   * `ChatHelper.cpp` `parseMoney` (above), the only narrow counter in the file (every other loop there is `size_t`/iterator).
   * `AhAction.cpp:138` and `:377` `int pos = text.find(" ")` -> `size_t pos`.
   * `strategy/NamedObjectContext.h` (parses the qualifier text of `ItemQualifier(string)`): `MultiQualify` `uint8 i` -> `size_t i`; `getMultiQualifiers` `int8 level` -> `int level` and the loop
     `while (next < view.size() || level < 0)` -> `while (next < view.size())` (a qualifier with one more `}` than `{` kept the loop running past the end of the string).
   * `ChatFilter.cpp`: no narrow string index exists (the `int j < filters.size()` loop is over filter objects, not text); `TradeAction.cpp` `uint8 i` loops are over the 7 trade slots.
   Not changed, by decision: `PlayerbotAIItems.cpp:960/976` (`uint8 i` over two fixed item-id lists, not chat text).
2. MEDIUM regression: `ItemQualifier(std::string)` reads the random property with `ai::chatlink::SignedFieldValue` (optional sign, up to 10 digits, must fit int32; a valid negative
   such as -1234 or -2147483648 is kept; an out-of-range or garbage field is refused as 0; never throws), on every core incl. Turtle. `LegacyItemQualifier` (ForeverCore `.tb link`)
   keeps the sign of field 3 for the 1.12 layout (3 or 4 fields); the modern layout still has random property 0.
3. MEDIUM throwing conversions on chat text, replaced by `ParseUnsigned` / `ParseSigned` (no throw, overflow and garbage refused, same result for valid input):
   `ChatHelper.cpp` `ExtractAllQuestIds/ItemIds/SkillIds/FactionIds` (4 `std::stoi`), `parseItemsUnordered` (2), `parseGender`, `parseTeam`, `parseClass`, `parseRace`;
   `ChatFilter.cpp` `@random=N`, `@fixedrandom=N` (`stoul`; a bad number now means "no match, message unchanged"), `@quest=N` (`stoi`);
   `NamedObjectContext.h` `getMultiQualifierInt` (`stoi`). No `std::sto*` is left in ChatHelper.cpp or ChatFilter.cpp (`atoi`/`atol` do not throw).
4. Codex's quest/talent item stays deferred by decision (quest seam belongs to the quest branch D3, talent lookup unported).

Tests: `forever/tools/test_chat_link.cpp` now arms its own 20 s `alarm` (a hang fails the run with rc 124; checked by putting a `uint8` counter back in `ParseMoney`), a 600-character digit/g/s/c string and 600 digits + `g`,
`ParseUnsigned`/`ParseSigned` overflow, garbage and sign cases, `SignedFieldValue` with -1234, -27, INT32_MIN, -2147483649, -5000000000, `-`, and `LegacyItemQualifier` with a negative random property.
`forever/tools/test_cl_wiring.py` has `ChatTextParsersWiring` (no narrow string counter, no `std::sto*` in the two files, the sign kept, 7 mutations).

Round 2 gate lines: `g++ -std=c++17 -Wall -Wextra -I. forever/tools/test_chat_link.cpp` + `timeout 30` -> `RESULT: PASS (test_chat_link)` (rc 0);
`python3 -m unittest forever/tools/test_cl_wiring.py` -> `Ran 4 tests` ... `OK`; slot 8 ON (`-DTORTOISEBOTS_SLICE1=ON`) `CMAKE_RC=0`, `MAKE_RC=0`, `TOTAL_SECONDS=3014`
(core-9b88bd2d6584-opt-85c8cb4e); slot 8 OFF `CMAKE_RC=0`, `MAKE_RC=0`; Turtle `vm-build ~/wt-s1r1 --wait` job `1-1795228247915661069-3462955` rc=0, `QUEUE_RC=0`, `module-tree-hash=285fb8d96cde15d1`.
