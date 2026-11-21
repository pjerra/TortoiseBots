# Raid groups and the raid layout on ForeverCore

Board ticket #161 ("Port bots in raid groups and the raid layout"). Acceptance: a headless check in which a 20-bot raid forms
with the layout's groups.

## What the legacy module did

- `runtime/RaidLayoutPolicy.h`: the shape of a raid. `Plan` puts the main tank (the lead bot) and each off-tank at the head of
  a subgroup, seats one or two healers with each tank once the raid keeps a third of the healers (`TankHealerCounts`,
  `RaidHealerMinimum`), spreads the raid healers and fills the free seats with damage dealers; `PlanSubgroupMoves` turns the
  plan into moves and swaps that never overfill a subgroup; `PlanJoinerSubgroup` / `PickJoinerHealTank` seat a bot that joins
  later without moving anyone else.
- `commands/Commander.cpp` (`ApplyRaidLayout`, `PlaceJoinerInRaidLayout`): the glue on the Turtle core. It applied the moves
  with `Group::ChangeMembersGroup` / `SwapMembersGroup`, set the main tank and an assistant mark on each off-tank, and gave each
  tank healer the engine's `focus heal targets` (its tank, then itself).
- Raids themselves came from the hire flow (`runtime/HireLifecycle.cpp`): a hire into a party of five converted it to a raid.

## What the Forever host had

`BotMgr::Login` and `BotMgr::JoinParty` refused every raid and capped a master's party at five ("raids are not in this slice",
R10/S1). The commander binding already knew raid bindings up to 40 (`TbCmdBindingLifePure.h`), and `.tb raid <master>` existed as
a test helper. Players worked around the cap with the core's `.group join` (a GM command, from a client only).

## The port

| Piece | Where | Notes |
|---|---|---|
| Join rules | `forever/src/host/TbRaidGroupsPure.h` `PlanJoin`, `MaxNamedMembers` | pure; `tools/test_forever_raid_groups.cpp` |
| The toggle | `TortoiseBots.Raid.Enable` (default 0) in `forever/conf/tortoisebots.conf.dist`, read in `BotMgr::LoadConfig` | 0 = the 5-man rule unchanged |
| Login / join | `BotMgr::Login` counts pending logins against 5 or 40; `BotMgr::JoinParty` converts a full party (`Group::ConvertToRaid`) after the invite and before `Group::AddMember` | the core's `AddInvite` has no size check, `AddMember` places a raid member in the first subgroup with room |
| `.tb party` | up to 39 members with raids on; `sub=` on a raid join and on the roster, `mark=maintank|assistant` on the roster | `TbPartyState.cpp` |
| `.tb layout <leader>` | `forever/src/host/TbRaidLayout.cpp` | `RaidLayout::Plan` and `PlanSubgroupMoves` unchanged from the legacy header; `Group::ChangeMembersGroup` / `SwapMembersGroups`, `SetGroupMemberFlag(MAINTANK / ASSISTANT)` |
| Later joiners | `PlaceJoiner` (called by `.tb party` for each joined member of a raid that has a layout) | `PlanJoinerSubgroup`, `PickJoinerHealTank` |
| Tank-healer focus | the slice now registers the `focus heal targets` strategy (`SliceAiObjectContext.cpp`); the value and its readers (`PartyMemberToHeal.cpp`, `RaidTankHealing.cpp`) were already in the slice | `forever/tools/slice_registry_check.py` (a) stays 0 |

Adapted, not copied: a Forever guid is 128 bits, so the policy's 64-bit member ids are member indexes; the main tank is picked
from the group (its main-tank mark, else a tank leader, else the first tank) because a Forever raid may be led by a real player
who is not a tank; real players in the raid are planned too, so the plan never overfills a subgroup around them. Roles come from
what `.tb state` reports (`role=`): the engine's answer, else the role `.tb role` stored.

## Limits

- The layout record lives in memory per group (until a restart). A bot that joins through `.tb login <bot>` with a master or
  `.tb group` takes the core's first free seat; `.tb party` and `.tb layout` seat by role.
- The layout is not undone when a raid breaks up (the legacy commander restored the old subgroups at unbind). A tank healer keeps
  its `focus heal targets` until it leaves the world or its strategies are rebuilt (`.tb role`).
- The commander binding's raid rules are unchanged (it already allowed raids up to 40).
- Raid-wide behaviour of the legacy commander (wipe threshold, gather, rest floor in `RaidLayoutPolicy.h`) is not wired here.

## Checks

- `tools/test_forever_raid_groups.cpp`: join rules, main tank pick, a 20-member layout (4 tanks, 5 healers, 11 others) and its moves.
- `tools/test_forever_raid_groups_wiring.py`: the call sites (join plan in Login/JoinParty, convert after invite and before add,
  toggle default = template, `layout` registration, stub and engine-only source, policy calls, `.tb party` cap and joiner seat, slice key).
- `forever/tools/raid_groups_check.py`: the headless live check (world console only, no client). It forms a raid of host bots with
  `.tb party`, applies `.tb layout`, reads the roster back and checks the subgroups, the five-per-subgroup cap and the marks.

### Live check

Not run yet: it needs a world server with this module and `TortoiseBots.Raid.Enable = 1`, coordinated by Fable.
