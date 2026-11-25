# BG2: the fight in a battleground (round BG2)

`BATTLEGROUNDS-SPEC.md` round BG2 (D9, the "port the PvP kit" row). It sits on BG1 (queue, join, leave, death; `TortoiseBots.Bg.Enable`,
default 0) and changes nothing for a bot outside a battleground. Nothing in it needs a core change: every call is a public member the core
itself uses. The class kits (`SliceAiFactory.cpp`, Sagiroth's) are not changed: BG2 adds its strategies after them.

## What a bot does in a battleground now

Before BG2 a bot inside a WSG or AB match had the class kit and nothing PvP: no `pvp` strategy, so no enemy player was ever a target
(`AttackersValue` drops a player unless the bot holds `pvp`), the engine's `enemy player target` values were not registered, the sight
distance for an enemy player was a 10 yd stub, a free pool bot kept its `grind` strategy (it looked for mobs to grind inside the match), and
`PlayerbotAI` reset the bot's strategies on every tick because the non-combat engine had no `battleground` strategy.

Now, for a bot with `InBattleground()`:

- **Combat engine:** the class kit, then `pvp` ("duel" never), and the open-world escapes off: `flee`, `cast time`, `threat`; a free bot also
  drops `follow`. A bound bot (a human master) keeps its escort movement.
- **Non-combat engine:** the class kit, then `pvp` and `battleground`; a free bot also drops `grind` and `follow`. An idle bot standing next
  to an enemy player now starts the fight (the `enemy player near` trigger is wired into both engines).
- **Who it hits** (`ai/playerbot/policy/bg/BgTargetPolicy.h`, spec D9, in this order, nearer enemy on a tie): a bound bot's human's target
  within 40 yd, the enemy flag carrier within 40 yd, an enemy hitting our flag carrier or one of our healers within 40 yd, an enemy healer
  within 30 yd, the enemy with the least health within 30 yd, whoever is attacking the bot at any range. The leash: when the bot has an
  objective (the `bg objective` position, which BG3 sets), an enemy more than 40 yd from it is not chased, except the carrier and an enemy
  already hitting the bot. A bound bot has no leash.
- **Sight:** 60 yd outside a battleground (unchanged), 40 yd inside one.
- **Flag carrier:** read from the carrier aura on the player (23333 on an Alliance carrier, 23335 on a Horde one), never from the map
  script's state (D2). `enemy flag carrier` and `team flag carrier` values walk `Battleground::GetPlayers()`. Both ids and the side
  mapping are pure (`BgIds.h`: `WsgCarrierAuraOf`/`WsgEnemyCarrierAura`/`WsgOwnCarrierAura`) and tested offline by
  `forever/tools/test_bg_flag_carrier.cpp`; the core's cast site is cited there (`battleground_warsong_gulch.cpp:925`, `CarrierSpell` at
  `:706`, the ids at `:51-54`). Live row 4 still needs a real flag pickup: the test box has no way to place the aura provably.

## Parts

| Part | Where |
|---|---|
| Target order, pure and tested | `ai/playerbot/policy/bg/BgTargetPolicy.h` (ranges in `BgLimits.h`, aura ids in `BgIds.h`) |
| The pair | `forever/src/compat/TbCompat_BG.h` (ForeverCore) and `ai/cmangos-compat-bg.h` (Turtle, the old expressions), found through `ai/playerbot/TbCompatBg.h` |
| Legacy files now in the slice (`forever/SLICE-W4.txt`) | `EnemyPlayerValue.cpp`, `PvpValues.cpp`, `PvpTriggers.cpp`, `AttackEnemyPlayersStrategy.cpp`, `BattlegroundStrategy.cpp` |
| Registered keys | `BG2_KEYS` in `slice_gen_context.py`: strategies `pvp`, `battleground`; action `attack enemy player`; trigger `enemy player near`; values `enemy player target(s)`, `has enemy player targets`, `enemy flag carrier`, `team flag carrier` |
| The kit | `ApplyBattlegroundCombatKit` and `ApplyBattlegroundNonCombatKit` in `forever/src/slice/SliceAiFactory.cpp`, run from a scope guard that is the first statement of each kit function, so the early `return` of a class switch (a tank spec) still gets them |
| Stubs | `GetMaxAttackDistance` stub removed (the real one is compiled); `WorldPvpSeekChecks::FindAssistTarget` stubbed (open-world PvP seeking stays out) |

What the legacy files lost to the pair, with the Turtle behaviour kept on the Turtle side: the cmangos `BattleGroundWS` reads (picker guids,
flag state) in `PvpValues.cpp` and `PvpTriggers.cpp`; the `m_duel` read (now the existing `DuelOpponentGuidCompat`); the Alterac Valley
sight rule; the battlemaster values (`BgMastersValue`, `BgMasterValue`, `RpgBgTypeValue`), compiled only where `TB_BG_BATTLEMASTER_PATH` is 1
(Turtle). `BgInviteActiveTrigger` lost a loop that did nothing (it returned false either way). The target policy is switched on by
`kBgTargetPolicyCompat` (true on ForeverCore, false on Turtle, where the bot keeps its old pick).

Not ported, on purpose: `BattleGroundTactics.cpp` (cmangos battleground internals, hand paths), the battlemaster queue actions (BG1 queues
through the core), the objective play and the `warsong`/`arathi`/`alterac` strategies (BG3, BG4). `BattlegroundStrategy.cpp` is compiled and its
`battleground` strategy is registered, but its triggers are not: the strategy is inert until BG3 registers the objective keys, and exists now
because `PlayerbotAI::UpdateAIGroupMaster` resets the strategies of a bot inside a match on every tick while the non-combat engine lacks it.

## Binding a bot inside a match (live row 5)

`.tb master <pool bot> <human>` was refused inside a match, as it is outside one: `not-allowed`, "neither a host bot nor your own character"
(the N5 rule, `TbH3.cpp` `MayLeadBots`). That rule keeps a third real player from making himself the commander of someone else's bots, but it
also blocked the live row: an operator binding a pool bot to the human he watches. BG2 lifts it ONLY inside one battleground, where both
already stand in the same match, on the same team, out of reach of anyone outside the instance: `host/cmd/TbMasterBindPure.h`
(`Judge`/`CodeOf`/`DetailOf`) allows the bind when both `GetBattlegroundId()`s are equal and non-zero and `GetBGTeam()` matches, and refuses
with `other-instance` or `other-team` otherwise. Every other refusal is unchanged, including the text outside a match; a party command is
decided per bot, and one where NO bot may bind refuses before anything changes. Tested by `forever/tools/test_master_bind_pure.cpp`; the
handler wiring is pinned in `test_bg2_wiring.py`. `NoteMaster` is handed only the bots that really bound, so a refused member of a party never
joins the commander card.

### A battleground bind is its own kind, per bound bot (the cold reviews' F1, F2, F3)

A bind made inside one match does not live by the world binding rules. Those read the lead's group, and inside a match that group is the
battleground raid: `groupIsBg` released the record as `group-raid` on the first 1 Hz tick, and usually `leader-changed` before that (the bot does
not lead the raid). The release also ran the in-party party rule against the raid, which hands the human the raid's lead when the bot happened to
hold it.

The kind belongs to each bound BOT, not to the commander's record (F2: a command-wide flag applied to the whole record let a standing world
binding release a new BG bind, or lose its row and rules, and a later world bind turned a BG bind back into a stored one). The handler reads the
kind per bot at the moment it binds (`master_bind::IsBgBind`: both in the same instance, same team) and hands `cmd::NoteMaster` one
`life::Kind` per bound bot plus the COMMANDER's instance. The pure header (`TbCmdBindingLifePure.h`, the "kind is per bound bot" section) decides:

- `ApplyBind`: a bot that really bound takes the kind of this bind; a bg bot goes into the in-memory `BgBook` (bot, commander, instance), a
  normal bot leaves it; a refused bot changes nothing, and a command where nothing bound changes nothing at all;
- `NormalMembers` / `NormalRecordUpdate`: the commander's record (members, followers, lead, stored row, the 1 Hz life rules, the group and login
  hooks, `KeepBindings`) is made of his NORMAL bots only. A bg-only bind leaves it exactly as it was (`Keep`); a normal bind rebuilds it from the
  normal bots (`Rebind`); a record member rebound inside the match leaves it (`Shrink`, or `Drop` when it was the last: record and row go, no
  release). A commander may hold both kinds at once;
- `CheckBg` (1 Hz, and at a logout with that side offline): each bg member on its own facts. `bg-bot-left` releases that bot alone (it left
  the instance, logged out, or has no live row); `bg-commander-left` releases all of that commander's bg members; `bg-match-ended` when both are
  gone from the instance or none was recorded; `bg-team-changed` when both stand in the instance on different teams (a mid-match transfer: the
  order gate refuses the commander from that moment, and the bind must not outlive it either); `disabled` when the module is off. A member h3
  now gives to someone else is dropped, not released;
- `ReleaseReachesParty`: only a normal in-party record's release runs the party rule. A bg release (`ReleaseBgMember`) never touches a group, so
  the raid's leader stands; the log line is `the battleground bind of <bot> to <commander> was released: ... (<code>)`;
- a bg member is commanded (`IsCommandedBy`) while h3 binds it to that commander AND both are still in the world, in the instance the bind was
  made in and on one team (`life::MayCommandBg`, #315). The stored member alone was one maintenance tick behind a leave or a transfer: an order
  sent in that gap was accepted. The 1 Hz `CheckBg` still ends the row (the log line and the release above); the authorisation just refuses at once.

Tested in `forever/tools/cmd_binding_life_check.cpp` (`TestKindPerBot`: the review's cases a-f plus kind moves, hand-overs and the off switch;
`TestBgCommandGate`: #315's leave between two ticks, a logout, a transfer and the bot's own leave).
`test_bg2_wiring.py`'s check 14 pins the path (per-bot kind in the handler -> `NoteMaster` reads `kinds[i]` and the handler's instance -> the book
-> the record from normal bots only -> `RunBgCheck` in the tick and at logout -> the bg release reaches no group), with a mutation for each.

## Tests

Run on the build VM (nothing runs on the laptop); `FOREVER_CORE=<a core checkout at the pin>` for the core-text pin.

```
g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/test_bg_target_policy.cpp       # 26 checks: the spec's three cases and every rule edge
g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/test_bg_flag_carrier.cpp        # 29 checks: the carrier auras and their sides (row 4)
g++ -std=c++17 -O1 -Wall -Wextra -I. forever/tools/test_master_bind_pure.cpp       # 58 checks: the BG bind verdict and kind (row 5)
python3 -m unittest forever.tools.test_bg2_wiring                                   # 16 pins
BG_MUTATE=1 python3 -m unittest forever.tools.test_bg2_wiring                      # 29 mutations, each must fail its pin
python3 -m unittest forever.tools.test_bg1_wiring                                   # BG1's pins (its pin 10 now lives in test_bg2_wiring)
python3 forever/tools/slice_gen_context.py --check                                  # clean
```

The policy cases that fail before the code exists: the carrier at 35 yd over a 10 % HP enemy at 10 yd, the leash at 41 yd, the bound bot's
human's target (the header did not exist). The wiring pins fail on the tree before BG2 because the five files are not in the list, the pair does
not exist and the kit has no battleground part.

## Gates

See the PR. Slot gates: `slice_registry_check.py`, the census of `forever/SLICE-W4.txt` (the list the build reads; it now holds the five files),
the module with `SLICE1=ON` and `OFF`, the full `worldserver` link, `tbf-premerge`.

## Live check (queued, not run in this round)

WSG match, 10v10 with pool bots and one human at level 19 (`LIVE-QUEUE.md`): the two sides fight at the flag rooms and mid-field, a free bot
does not wander off to grind, a killed bot releases and is back on the next wave (BG1) and fights again, nothing in the log repeats
"reset strategies" for a bot in the match, and `.tb state` of a bound bot shows it taking its human's target. Open: the human's own queue and
bound bots (a human who groups bots) are BG1's; AB nodes and flag objectives are BG3/BG4, so bots only fight what they meet.

#315's own check, in the same match: `.tb master <pool bot> <human>` inside the instance, an order accepted; the human leaves the battleground
(`/afk` out or the match ends) and the SAME `.tb cmd` order sent immediately is refused before the next second's log line, then the log line
`the battleground bind of <bot> to <human> was released: ... (bg-commander-left)` follows within a second. A commander transferred to the other
side (`other-team`) is refused the same way, and the next 1 Hz pass releases the bind with `bg-team-changed`; a bot that left is refused and
released with `bg-bot-left`. The bind must never authorise an order out of the match or across sides.

## Re-read (#321 review): a commander between maps, an offline pointer, the cost

The battleground authorisation asks both sides for `IsInWorld()`. That cannot refuse a legitimate command: a teleport that stays in the
match is the near path (`IsInWorld() && GetMapId() == target && same instance`, Player.cpp:1374) and never leaves the world. A far teleport
removes the player from its map at once (Player.cpp:1527) and, when the target map differs, runs `LeaveBattleground` first
(Player.cpp:1470), which clears the match id and the team (Battleground.cpp:1062-1064): the load that follows is the leave the bind is meant
to end, and the 1 Hz check reads the cleared facts. While such a load lasts the check returns false (the denial #315 wants), and it returns
true again on the next command once the player is back in the world in a match - nothing latches, and no legitimate same-match load exists to
be lost. A logout is the same false from the same tick (`RunBgCheck(leaving)` passes the leaving side as null, so a logged-out player is
never dereferenced).

Offline pointers: `IsCommandedBy` refuses a null commander or bot before any read; the battleground branch reads only the two guids,
`IsInWorld()`, `GetBattlegroundId()` and `GetBGTeam()` on players the caller already holds. The maintenance pass maps its two lookups to
null, and `FindBg`/`DropBg` work on the stored guid counters, never a Player.

Cost: no database query, no world scan, no session lookup. `L::FindBg` walks the in-memory bg book (one small entry per bg-bound bot),
`h3::CommanderGuid` is a map lookup plus `BotMgr::MasterOf` (the bot table), and the three getters read `m_bgData`. The 1 Hz pass reads
one fact set per bg-bound bot; a snapshot that asks `IsCommandedBy` per bot adds the same two in-memory lookups per bot.

## Risks

- The `battleground` strategy has no triggers yet and a free bot has no `grind` (the grind kit would send it after mobs and travel targets):
  a bot inside a match stands where it is and fights what comes within 40 yd, or what its human orders. Nothing moves a free bot across the map
  until BG3 (objective play) lands, which is the spec's order (BG3 needs BG1 and BG2); a match of bots alone therefore does not play itself out
  yet. Codex's adversarial review flagged exactly this; it is left as is on purpose, because the fallback (the grind kit) is worse.
- The `pvp` trigger fires for every class: a healer in a match also attacks an enemy in reach when no heal is due (relevance 55, below the
  heals).
- The leash is only as good as the objective position; until BG3 sets `bg objective` the leash is off.
