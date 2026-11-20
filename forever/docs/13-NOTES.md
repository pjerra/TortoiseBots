# Issue #13: `.tb ignore` — the leave-it-alone list

## What

`.tb ignore <bot|party [leader]> add <guid>|remove <guid>|list|clear [#token]`
(TB-COMMANDS.md section 14): one memory-only list of up to 8 mobs per
commander, forgotten after 30 min or when the mob dies on a commanded bot's
map. Owned per commander (`.tb master`), not per bot.

Honoured by every engine target pick through one predicate,
`IsIgnoredMobCompat(bot, guid)` (compat pair `TbCompat_I13.h` +
`ai/cmangos-compat-i13.h`, Turtle twin always false so Turtle behaviour is
unchanged): the dps/tank/assist/pull/defence/grind tournaments skip listed
mobs (`PossibleAttackTargetsValue::IsValid`), raid-mark focus and the runner
finish do too, and a listed mob can never stick as a current target (it fails
`IsValid` and is absent from "possible attack targets", so `InvalidTargetValue`
- `g++ -std=c++17 -O1 -Wall -Wextra forever/tools/test_ignore13.cpp && /tmp/test_ignore13`: `54 checks, 0 failed` (a cap change 8→7 fails it: `2 failed`)
- `python3 -m unittest forever/tools/test_ignore13_wiring.py`: OK; `--mutate` exits 1
- `python3 forever/tools/slice_registry_check.py`: `summary: (a)=0`
- `python3 forever/tools/slice_gen_context.py --check`: up to date
- ON build (fixed script) `$BIN/tbf-build.sh <worktree> tortoisebots`: exit 0, `[310/310] Linking CXX static library libtortoisebots.a` (no warnings in new/edited files; pre-existing `sTerrainMgr`/`-Woverflow` only)
- OFF build (fixed script) `TB_SLICE=OFF $BIN/tbf-build.sh <worktree> tortoisebots`: exit 0, `[28/28] Linking CXX static library libtortoisebots.a`
- census `census:forever/SLICE9.txt`: exit 0, `files 268 compile clean 268 with errors 0 error places 0`
## Touch notes (shared files, tiny hunks)

- `ai/cmangos-compat-shim.h`, `forever/src/compat/forever-compat-shim.h`: one include line each (the i13 twin).
- `forever/src/host/TbH3.cpp`: one include + one `i13::SelfTestRecords` call (grammar selftest).
- `forever/src/TbScripts.cpp`: APPEND-only: the `ignore` command row + its include.
- `forever/src/host/TbOrders.cpp`: one include + one `NoteExplicitTarget` line in each of the attack/assist, pull and focus branches of `Run()`.
- `forever/src/host/TbCc.cpp`: `ApplyCcOrder` refuses a listed mob (`ignored`).
- `ai/` target values (`PossibleAttackTargetsValue`, `RtiTargetValue`, `DpsTargetValue` runner, `GrindTargetValue`), `CcGuard::IgnoredMobNear`, `BreaksCcInArea`, warrior/rogue splash: each a 2-4 line guard behind the compat predicate (no-op on Turtle).
- `forever/docs/TB-COMMANDS.md`: `ignored` code + section 14 (samples specified, first boot replaces them).

No new engine keys (`I13_KEYS` not needed: the engine asks the host through
the compat predicate), no slice stub block, no DB, no config.

## Live steps (not run: no live server in this environment)

1. GM account: `.tb master party Mtank Mcmd`, engines on.
2. Pull a pack near a neutral beast; `.tb threat party Mtank` → beast `mob=<guid>`.
3. `.tb ignore party Mtank add <guid> #a` → `status=ok ... msg=Bots%20will%20leave%20...`.
4. Aggro the beast onto a bot (body pull): bots keep `tgt=` of other mobs / `tgt=` empty, never the beast; `.tb state` shows no `tgt=<beast>`.
5. Have the mage bot cast Blizzard on the pack with the beast within 30 yd: no cast (cast_delivered frozen); move the party 40+ yd away: Blizzard goes out.
6. `.tb order party Mtank attack <guid>` → runs, and `.tb ignore ... list` no longer names it.
7. `.tb cc Mmage moon <guid>` → `error code=ignored`.
8. Kill the beast → next `.tb ignore ... list` (or any op) drops it; wait 30 min → `list` is empty.

## Untested

- No live boot (steps above unrun); samples in TB-COMMANDS.md section 14 are specified.
- Channels started before a mob is listed are not cancelled mid-cast (the next cast is refused).
- Despawn of a live mob (not death) is forgotten by the 30 min expiry only.
- A legacy chat `.bot attack` naming a listed mob is refused engine-side (only the `.tb order` attack/pull/assist/focus path un-lists, where explicit order wins).
