# Review Notes: Commander & Party Ownership Guard (`guard`)

## What was missing
Mutating `.tb` commands (`.tb cmd`, `.tb order`, `.tb role`, `.tb master`, `.tb party <leader> <member>...`, `.tb gear`, `.tb quest`, `.tb pet`, `.tb cc`, `.tb strategy`, `.tb ignore`, `.tb enter`, `.tb tele`, `.tb revive`, `.tb kill`, `.tb instance reset|leave`, `.tb gm`, `.tb run`, `.tb learn`, `.tb equip`, and the legacy mutating commands in `TbScripts.cpp`) allowed a player GM session to act on a host bot owned by another commander/master or in a party led by another real player.

## Fix
- Added pure decision `TortoiseBots::h3::guard::CheckControl(Subject const&)` and `CodeOf(Verdict)` in `forever/src/host/TbGuardPure.h` taking plain data (`caller`, `binding`, `master`, `masterIsBot`, `partyLeader`, `partyLeaderIsBot`):
  - Empty `caller` (GM console, no player session) is allowed.
  - Non-empty `binding != caller` -> `foreign-bot`.
  - Non-empty `master != caller` when `!masterIsBot` -> `foreign-bot`.
  - Non-empty `partyLeader != caller` when `!partyLeaderIsBot` -> `foreign-lead`.
  - Caller's own bots, bots bound to caller, bots in caller's party, bot-led uncommanded parties, and free bots are allowed.
- Wired `CheckBotAccess` / `CheckPartyLeadAccess` in `forever/src/host/TbH3.h` / `TbH3.cpp` (with `TbH3Off.cpp` stubs for `TORTOISEBOTS_SLICE1=OFF`):
  - `ResolveAddress(..., requireControl = true)` guards every mutating addressed command (`order`, `role`, `master`, `gear`, `enter`, `tele`, `revive`, `kill`, `cc`, `strategy`, `pet`, `ignore`), while read-only `state` and `threat` pass `requireControl = false`.
  - `ResolveTestTarget` guards `learn`, `equip`, `hurt`, `power`.
  - Wired `TbCmdOrders.cpp` (`HandleCmd`), `TbCmdTasks.cpp` (`ResolveCommanderParty`), `TbPartyState.cpp` (`HandleParty` form), `TbOrders.cpp` (`HandleMaster` commander check), `TbH4.cpp` (`HandleInstance` reset/leave, `HandleTele` form 1, `HandleGm`), `TbHostQuest.cpp` (`HandleQuest` mutating verbs), `TbRunCommands.cpp` (`HandleRun` start/stop/pause/resume), and `TbScripts.cpp` (`RequireOwnedBot` / `RequireBot`).
- Documented `foreign-bot` and `foreign-lead` in `forever/docs/TB-COMMANDS.md`.

## Pure test (`forever/tools/test_guard.cpp`)
- FAIL before: `test_guard: 11 checks, 6 failed`
- PASS after: `test_guard: 11 checks, 0 failed`

## Live checks for the lead
With two player characters `PlayerA` and `PlayerB`, and `PlayerA` owning `BotA` (via `.tb master BotA PlayerA` or `.tb group BotA PlayerA`):
- As `PlayerB`: `.tb order BotA stay #g1` -> `error code=foreign-bot cmd=order ... #g1`
- As `PlayerB`: `.tb role BotA dps #g2` -> `error code=foreign-bot cmd=role ... #g2`
- As `PlayerB`: `.tb master BotA none #g3` -> `error code=foreign-bot cmd=master ... #g3`
- As `PlayerB`: `.tb order party PlayerA stay #g4` -> `error code=foreign-lead cmd=order ... #g4`
- As `PlayerA`: `.tb order BotA stay #g5` -> `order-begin` / `order bot=BotA verb=stay ... status=queued` / `order-end`
- On worldserver console: `tb order BotA stay #g6` -> `order-begin` / `order bot=BotA ... status=queued` / `order-end`
