# Player commands, round R2: `add`, `logout`, `follow`, `stay`, `attack`, `flee`, owned login

Spec: `~/TortoiseBots-audit/PLAYER-COMMANDS-SPEC.md` (TBF #129 + #132), round R2 of section 6. Branch `feat/player-commands-r2`
from `tbf/main` (R1 merged as PR #195). No pin move; no ForeverCore core change.

## What the round adds

* **`.bot add <name>`** — the owner's own alt (same Battle.net account, D-3) or a GM's any character: resolves the name in the
  character cache (`not-found`), refuses a character already in the world (`already-online`), a requester in a group led by
  someone else (`not-leader`), and the owned cap (`too-many`). It calls `BotMgr::Login(guid, requester, false, false, true)` and
  answers `add <name>: ok queued for login; it joins your party`.
* **`.bot logout <name>`** (alias `remove`) — owner or GM only (D-4 Owner tier), calls `BotMgr::Logout(guid, "logged out by its
  owner")`.
* **`.bot follow [member]`, `stay`, `attack`, `flee`** — tactical tier (D-4), resolve the bot, `Decide`, then `h3::QueueOrder`
  with the matching `OrderSpec`; `attack` takes the requester's selected unit (`no-selection` when none); refusals pass the
  queue's code through. Ok text names the order seq.
* **Owned login (D-3)** — `BotMgr::Login(..., bool owned = false)`: with `owned`, the `AccountHasBot` refusal is skipped only
  when the owner's world session is present on the account (`sWorld->FindSession(accountId)` has a player in the world).
  `Bot::owned` is set; `useEngine = true` for an owned bot. `OwnedBotsOf(master)` counts the master's owned bots for the
  `Player.MaxBots` cap. The pool's one-bot-per-account rule is untouched.
* **Battle.net id fix** — `BotSession` now passes `Battlenet::AccountMgr::GetIdByGameAccount(accountId)` as the `WorldSession`
  Battle.net id (0 when none) instead of the game account id. This applies to every bot login, pool included.
* **`flee` order verb** — `TbOrders.cpp` knows `flee`, requires the `flee chat shortcut` action, runs it and returns
  `ok` / `fail:flee`.

## Ownership (D-1 amendment)

R1 already reads ownership as the Battle.net account: `ScopeOf` queries the login DB for the game accounts of the caller's
Battle.net id and checks the bot's game account against them. R2 keeps that rule for `add` and `logout`: the character's game
account must be in the caller's `OwnerScope` (or the caller is a GM). The owned-login cap is the same Battle.net account.

## Files changed

* `forever/src/TbBotMgr.h` — `Login(..., bool owned = false)`, `OwnedBotsOf`, `Bot::owned`.
* `forever/src/TbBotMgr.cpp` — owner-present skip in `Login`, `bot.owned`, `useEngine` for owned, `OwnedBotsOf`; includes
  `WorldSession.h`.
* `forever/src/TbBotSession.cpp` — `BattlenetAccountMgr.h` include and the Battle.net id argument.
* `forever/src/host/TbOrders.cpp` — `flee` in `IsVerb`, `PreflightOrder`, `Run`, and the no-argument list.
* `forever/src/host/player/TbPlayerCmd.cpp` — `HandleAdd`, `HandleLogout`, `HandleOrderVerb`, `ResolveCharacter`, `ResolveBot`,
  `OwnsAnyBot`, tier-aware `HandleHelp`, `Dispatch` routing; `host/TbOrdersApi.h` include.
* `forever/tools/test_player_cmd_wiring.py` — seven R2 wiring tests (the three named failing cases plus the handler and help
  pins).
* `forever/docs/PC-R2-NOTES.md` — this file.

## Tests and gates

| Gate | Result |
|---|---|
| `python3 forever/tools/test_player_cmd_wiring.py` | 22 tests OK |
| `PLAYER_CMD_MUTATE=1 python3 forever/tools/test_player_cmd_wiring.py` | 8/8 mutations caught |
| `g++ -std=c++17 -O1 -Wall -Wextra -I forever/src forever/tools/test_player_auth.cpp -o /tmp/test_player_auth && /tmp/test_player_auth` | 89 checks, 0 failed |
| module build | not run (no Docker/build toolchain in this environment) |

The three named failing cases were written first and failed on the pre-R2 source: `test_login_owned_skips_account_rule_only_for_owner`,
`test_bot_session_uses_bnet_id`, `test_flee_is_a_verb`. They pass after the fix.

## Live not run

No live server in this environment. The checks A-2, A-3, A-7 of the spec's live table need the build server.
