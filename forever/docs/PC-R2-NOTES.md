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
* **Owned login (D-3, amended: owner = Battle.net account)** — `BotMgr::Login(..., bool owned = false)`: an owned login needs its
  owner (the master) in the world with a non-zero Battle.net id, and stamps the bot with that id (`Bot::ownerBnet`). It may share a
  game account with other bots only when that game account belongs to the owner's Battle.net account and every bot already on it is
  an owned bot of the same owner; a pool or anyone else's bot on the account keeps the one-bot-per-account rule. `useEngine = true`
  for an owned bot. `OwnedBotsOf(ownerBnet)` counts the owner's owned bots in every state (loading, away, on the way out) for the
  `Player.MaxBots` cap, whichever character or game account of the Battle.net account added them.
* **Owned bots leave with the owner** — `OnPlayerLogout`: an owned bot of the leaving master gets no master grace; it is logged out
  at once ("its owner left the world"), a pending login too (the session logs the Player out when its load finishes).
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
* `forever/tools/test_player_cmd_wiring.py` — nine R2 wiring tests (the three named failing cases, the review cases
  `test_owned_cap_counts_the_battlenet_owner` and `test_owned_bots_leave_with_their_owner`, the handler and help pins).
* `forever/docs/PC-R2-NOTES.md` — this file.

## Review round (lead, Codex adversarial)

* Codex [high]: the cap was keyed on the master's character guid, so a relog (the old bots waited out the 300 s master grace) or a
  second linked game account added four more. Fixed: the cap keys on the Battle.net id stamped on each owned bot, counted in every
  state, and owned bots log out with their owner (no grace).
* Lead: the owner-present test looked for a real session on the CHARACTER's game account, so two owned bots on a second game account
  of the same Battle.net account (the owner playing on the first) were refused, and the test said nothing about pool bots already on
  the account. Fixed by the sharing rule above.
* Lead: `.bot help` showed `add`/`logout` only to callers who already owned a bot online, so a new owner could not discover `add`.
  Shown to everyone now (every player may add its own characters).
* Residual (documented): a bot account with no Battle.net link now saves account-wide collections under Battle.net id 0 (the core's
  own behaviour for an unlinked session) instead of under a real Battle.net account that happened to share its number.

## Tests and gates

| Gate | Result |
|---|---|
| `python3 forever/tools/test_player_cmd_wiring.py` (build VM) | 24 tests OK |
| `PLAYER_CMD_MUTATE=1 python3 forever/tools/test_player_cmd_wiring.py` (build VM) | 8/8 mutations caught |
| `test_player_auth.cpp` (g++ -std=c++17 -Wall -Wextra, build VM) | 89 checks, 0 failed |
| module SLICE1=ON (slot 4), SLICE1=OFF (slot 6), worldserver link (slot 7) | CMAKE_RC=0 MAKE_RC=0 each |
| `tbf-premerge` | see the PR |

The three named failing cases were written first and failed on the pre-R2 source: `test_login_owned_skips_account_rule_only_for_owner`,
`test_bot_session_uses_bnet_id`, `test_flee_is_a_verb`. They pass after the fix. The two review cases failed on the worker's commit and pass after the review fix.

## Live

Queued on forever-test2 (`LIVE-QUEUE.md`): four owned alts over two game accounts of one Battle.net account, orders, a fifth
add refused `too-many`, a stranger refused, owner logout logs the alts out. Merge after it passes.
