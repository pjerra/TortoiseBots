# Player commands, round R1: the `.bot` entry, tiers, `help`, `who`

Spec: `~/TortoiseBots-audit/PLAYER-COMMANDS-SPEC.md` (TBF #129 + #132), round R1 of section 6. Branch `feat/player-commands-r1` from `tbf/main`;
no pin move by this branch (`PlayerScript::OnCommand` exists at the pin it branched from, d3358c21dc; the `tbf/main` merge
later moved the pin to b441bf74e0, which has the same hook and the Battle.net mapping).

## What it is

* **The entry (D-1).** `host/player/TbPlayerCmd.cpp` registers `tortoisebots_player_cmd` (a `PlayerScript`) in `AddSC_tortoisebots`. Its
  `OnCommand` is called by the core with the text after the leading `.` or `!`, before the core's lookup and before any RBAC check
  (`ChatHandler::ParseCommands`). It claims the text only when the first word is `bot`, any case, followed by the end or a space:
  `botany`, `bots` and `bothelp` stay the core's business. With `TortoiseBots.Enable = 0`, or in a build without `TORTOISEBOTS_SLICE1`, `Dispatch`
  returns false and the core answers as for any other unknown command. The console keeps `.tb`; `.bot` is never called for it.
* **The rule (D-2, D-4, D-7, D-8).** `host/player/TbPlayerAuthPure.h` is C++17 and names no core type (like `TbGuardPure.h`): `TierOf`, `Decide`,
  `CodeOf`, `ClaimsCommand`, `MayLoginOwned`, `SummonCooldownLeft`. The host half `FactsOf` fills `Caller`/`BotFacts` from the bot table, the
  character cache, the commander binding, the bot's party and the run gate.
* **Round 1 replies.** `.bot` and `.bot help` print the usage rows for the caller; `.bot who` (aliases `roster`, `list`) prints the count then one
  line per bot: name, level, class, map id, `follow|stay|free`, `engine|spike`. `TortoiseBots.Player.Commands = 0` refuses every non-GM verb with
  `players-off`; an unknown verb answers `unknown-verb`.
* **Config (D-13).** `forever/conf/tortoisebots.conf.dist` gains `TortoiseBots.Player.Commands` (default 1) and `TortoiseBots.Player.MaxBots`
  (default 4, 0-4), read by `player::LoadConfig` from `tortoisebots_world::OnConfigLoad` (first load and reload). The code fallbacks equal the
  template values.

## Rules as built

* **Ownership is the Battle.net account (D-1 amendment, 2026-11-22).** The caller's Battle.net id is its own session's
  (`WorldSession::GetBattlenetAccountId`, the core's identity of the player). For a line that names bots (`who`) the game accounts that
  Battle.net account holds are read from the login DB once (`SELECT id FROM account WHERE battlenet_account = ?`, the core's own
  query in `LoginRESTService.cpp`), and a bot is owned when its character's game account (`sCharacterCache->GetCharacterAccountIdByGuid`)
  is in that set. The session's Battle.net id counts only while the caller's own game account is still in that set (a session keeps
  its login id after a live unlink); otherwise the caller owns nothing. Two game accounts of one Battle.net account therefore own the same bots; a character on another Battle.net account is a
  stranger. Account 0 is "unknown" and never a match (a session without a Battle.net id owns nothing: fail closed). Nothing is cached
  across lines (review fix: the first build kept a game-account to Battle.net map until a reload, so a live `.bnetaccount unlink` or
  relink left the old owner seeing, and from R2 ordering, the moved account's bots); one login-DB query per `.bot who` whatever the
  roster size. `Caller.account`/`BotFacts.account` carry Battle.net ids; `BotFacts.account` is the caller's id when the bot is the caller's, else 0.
* **The order of `Decide` is the spec's:** not a bot (`not-a-bot`), in a running dungeon run (`in-run`, every caller, GMs included), GM, owner, then
  the tactical-only faces: master, commander (`cmd::IsCommandedBy`), real-player leader of the bot's group. Everything else is `not-yours`. A bot
  leading the group is not a real player (`leaderIsBot`).
* **Two tiers now, four later.** `TierOf` already classifies the whole round-1..3 set (`help` none; `follow stay attack flee who roster list stats`
  tactical; `add logout remove summon` owner; everything unknown owner, fail closed), so the rule is tested once for R2/R3. R1's `Dispatch` serves
  only `help` and `who`; the other words answer `unknown-verb` until their round lands.
* **`who` lists online bots only (D-10).** The caller's own bots are those on the same Battle.net account, the caller as master, the caller as commander, or
  a real player leading the bot's party — the same four faces as D-4, without the GM widening. `who all` is the GM's every-bot listing; a player's
  `all` names no more than `who`. A run hides nothing here (a run refuses orders, not the listing). Movement reads `free` (the free kit), `stay`
  (the last order verb), else `follow`; the brain reads `engine` when the bot has a brain, else `spike`.
* **Replies (D-6).** `TortoiseBots: help: <row>`, `TortoiseBots: who: <n> bot(s)`, `TortoiseBots: who: <Name> level <n> <class> map <id> <move>
  <brain>`, and `TortoiseBots: <verb>: refused <code>: <text>`. The verb comes first, the bot's name once a verb names one. No line ever names an
  account id, an account name or the owner's other characters; the wiring test pins that the reply lines never format them.

## Files

New: `forever/src/host/player/TbPlayerAuthPure.h`, `forever/src/host/player/TbPlayerCmd.h`, `forever/src/host/player/TbPlayerCmd.cpp`,
`forever/tools/test_player_auth.cpp`, `forever/tools/test_player_cmd_wiring.py`, `forever/docs/PC-R1-NOTES.md`.
Changed: `forever/src/TbScripts.cpp` (include, `player::LoadConfig` in `OnConfigLoad`, `player::AddScripts` in `AddSC_tortoisebots`),
`forever/conf/tortoisebots.conf.dist` (the two keys). No core file changed.

`TbPlayerCmd.cpp` is compiled in both builds by the module glob; the engine-off half defines `LoadConfig`, `AddScripts` and a false `Dispatch`, and
`AddScripts` registers nothing there.

## Tests

* `forever/tools/test_player_auth.cpp` (89 checks, 0 failed) — run
  `g++ -std=c++17 -O1 -Wall -Wextra -I forever/src forever/tools/test_player_auth.cpp -o /tmp/test_player_auth && /tmp/test_player_auth`.
  Failing case first: `owner_same_account_follow_allowed` FAILED on the header before the ownership branch (`FAIL ...:50`, `FAIL ...:51`,
  87 checks / 3 failed), and passes after it; the amendment's case (`two game accounts of one Battle.net account own the same bots`) was
  added with its host-side failing test below. Required cases: `stranger_follow_refused_not_yours`, `master_logout_refused`,
  `leader_real_player_follow_allowed`, `leader_bot_not_counted`, `gm_any_verb_allowed`, `zero_account_never_owner`,
  `in_run_refused_even_for_gm`, `unknown_verb_is_owner_tier`, `claims_bot_not_botany`, `claims_case_insensitive`, `may_login_owned_cap`,
  `may_login_owned_other_account_refused`, `may_login_owned_gm_any_account`, `cooldown_left_and_gm_exempt`.
* `forever/tools/test_player_cmd_wiring.py` (15 tests OK): the claim in `OnCommand`, the false return with the module or engine off, the RBAC 371
  GM test, `AddScripts`/`LoadConfig` from `TbScripts.cpp`, the reply grammar and refusal codes, no account identity in a reply, `who`'s online-only
  listing and its four faces, `FactsOf`'s sources, the core-free pure header, the key/fallback match, `test_required_cases_are_named` (every case name the spec lists) and `test_ownership_is_the_battlenet_account`
  (the amendment), `test_ownership_is_never_cached_across_commands` (review fix; it and four tests that read the new signatures fail on the cached build ce97d0072). `PLAYER_CMD_MUTATE=1` compiles the real
  unit test against eight mutated headers and all are caught: `gm-branch-dropped`, `account-zero-allowed`, `logout-tactical`,
  `leader-bot-counted`, `claim-prefix`, `in-run-after-gm`, `login-cap-dropped`, `cooldown-gm-not-exempt`.
* Live: not run. A non-GM `.bot who` today answers the core's "There is no such command" (A-1's failing observation); the live table is the
  spec's (A-1, A-2, A-3, A-6, A-7) and runs after R2/R3 put the ordering verbs in.

## Not in this round

`add`, `logout` (and the D-3 owned-login exception with `BotMgr::Login(..., owned)`), `follow`, `stay`, `attack`, `flee` (R2); `summon`, `stats`, the
`TortoiseBots.Player.MaxBots` cap in a handler and the cooldown map (R3). `MayLoginOwned` and `SummonCooldownLeft` exist here because the pure rule
is tested once; nothing calls them yet.

## Gates

| Gate | Result |
|---|---|
| `test_player_auth.cpp` | `89 checks, 0 failed` |
| `test_player_cmd_wiring.py` (`PLAYER_CMD_MUTATE=1`) | 14 tests OK; 8/8 mutations caught |
| module ON (`-DTORTOISEBOTS_SLICE1=ON`) | `CMAKE_RC=0` `MAKE_RC=0` (`TOTAL_SECONDS=28`) |
| module OFF (`-DTORTOISEBOTS_SLICE1=OFF`) | `CMAKE_RC=0` `MAKE_RC=0` (`TOTAL_SECONDS=24`) |
| `tbf-premerge ~/wt-tbf-pcmd` | PASS (Turtle build, module ON and OFF on the merged tree; see the handover report) |
