# Run knobs and the player-led run (TortoiseBotsForever issue #139)

## Owner decision (2026-11-22, taking the ticket)

**The player-led mode is built as an OPT-IN mode. The default stays decision D1: a bot tank leads the run.**
A run is player-led only when the starter asks for it with the word `lead=player` on `.tb run start`. Without the word nothing changes.

Branch `feat/run-knobs-139` on `tbf/dev`, **zero ForeverCore changes**. Everything below is module code and data: the knob table
(`forever/data/run-knobs-139.tsv`), the generated config template, and the player-led start (`forever/src/host/run/TbRunPlayerLed*.{h,cpp}`).

## Part 1: the knobs

TB reads **105** `AiPlayerbot.DungeonRun*` / `AiPlayerbot.BossStand*` keys (`ai/playerbot/PlayerbotAIConfig.cpp`, `LoadDungeonRunKeys` and
its neighbours); the config reaches TBF unchanged (the same `PlayerbotAIConfig.cpp` is in the slice list). "Read" means a source of the
TBF build reads the config member: `forever/src/**`, the files of `forever/SLICE-W4.txt`, `runtime/CombatMonitorService.cpp`
(`forever/tools/test_run_knobs.py` computes this, it is not a hand count).

| status | keys | meaning |
|---|---|---|
| read | 30 | 11 by the run glue (`ReadConfig` in `TbRunFacts.cpp`, `TickRun` in `TbRun.cpp`), 19 by the bot-side files that are compiled (threat, taunt, charm yield, aura targets, ...); for the 19 the run-glue half of the key is not ported, the table says so |
| ported-now | 3 | `DungeonRunPlayerLed`, `DungeonRunPlayerLedPullYd`, `DungeonRunPlayerLedAwayMinutes`: read by Part 2 |
| ported-other | 1 | `DungeonRunLootFfa`: the detour is never needed, a run starts and goes on only on group loot or need-before-greed (`StartRun` refuses `loot-method`, `TickRun` pauses on it every tick) |
| hard-coded | 10 | raid keys (the run refuses a raid group, `StartRun` `group-raid`), `RezHold` = 1, `WalkBackRescue`/`DryWalk`/`BotKeysForPlayers` = 0, `FactJudgeCharmedLead` = 1 |
| not read | 61 | no TBF source reads them; the template line has no effect and says so |

The per-key table is `forever/data/run-knobs-139.tsv` (key, TB default, status, where, note). It is the source of the PR table and of the
notes in `forever/conf/aiplayerbot.conf.dist` (`gen_aiplayerbot_conf.py` writes `TBF: not read ...` / `TBF: hard-coded to <v> ...` under each
key, as AGENTS.md asks: a key that is read but unused says so in the template). `forever/tools/test_run_knobs.py` keeps all of it honest:
every key has one row, a `read` row names a real line of a built file that names the config member, and a `not-read` / `hard-coded` /
`ported-other` row fails the test when a built file starts to read it (a port that forgets the table). Run it with
`python3 forever/tools/test_run_knobs.py`.

### Why the "matters for a 5-man run" keys are not ported (the evidence)

The owner's list: tank post, pull gate, seek, stand, loot rules, boss spread, carrier.

* **Tank post, seek, boss spread, carrier, pull gate, heal reserve, avoid cast, burn mana, ranged only, use object (51 keys)** are *row-driven boss tactics*:
  the action layer is `ai/playerbot/strategy/actions/BossTacticsActions.cpp` and `.../triggers/BossTacticsTriggers.cpp`, neither is in
  `forever/SLICE-W4.txt`, and they act only on `ai_boss_tactic` rows of their kind. The rows TB ships (`data/sql/world/*.sql`, kind per map): `tank_post` Onyxia
  249; `seek_aura` / `seek_pull` / `spread` / `pull_gate` / `heal_reserve` / `avoid_cast` Zul'Gurub 309; `burn_mana` / `use_object` / `swap_on_aura` AQ20 509;
  `add_crew` Onyxia 249. **No 5-man map carries a row of any key-gated kind**: the 5-man maps (Deadmines 36, WC 43, Uldaman 70, Zul'Farrak 209, Scholomance 289,
  Stratholme 329, ...) carry `interrupt`, `kill_first` and `behind` rows only, and those kinds have no key. The TBF world schema has the table but ships 0 rows
  (`forever/sql/README.md`). The TBF run refuses a raid group (`StartRun`, `group-raid`). So these keys cannot change a 5-man run in either codebase.
* **Stand (pull stand, stand clearance, stand discipline, stand pull pre-cc), the aggro estimate (4 keys), straggler pull, respawn nest, totem focus, pack focus,
  auto cc above/risky (14 keys)**: these are the 5-man keys that do matter, and they are not ported here on purpose. `ApplyConfig` forces them off
  (`TbRunFactsPure.h`) because the readers are not ported; each reader is a 100-600 line geometry search in `commands/Commander.cpp` (hostile scans, navmesh and
  line-of-sight queries; for example `ReadStandPullHold` 11115, the clearance read 10640, the ahead estimate 11265) whose result only a live run can tune. Porting
  them blind would put 14 switches in the template that look live and were never walked. They are the follow-up (one key family per PR, each with a live check),
  listed in the table with their TB reader line.
* **Loot rules**: covered, see `ported-other` above.

## Part 2: the player-led run (opt-in)

**Start.** `.tb run start <tank> <plan> lead=player`, typed in game by the player who leads his own party (`lead=bot` is the default said out loud; any
other `lead=` value is `bad-argument`). `<tank>` is the bot that pulls (the run's `leader` as always: `.tb run status|stop|pause|resume|events <tank>` work as before).
This is the smaller of the two options the ticket allows: a word on the existing command, no new config switch to forget. The existing key
`AiPlayerbot.DungeonRunPlayerLed` (TB default 1) is the master switch: 0 refuses the word (`disabled`) and ends a player-led run already going on the next tick.
Without the word nothing changes: `StartRun` is untouched and a bot-led run behaves as before (the default, D1).

**What it does** (the Turtle human-lead behaviour, `runtime/PlayerLedPolicy.h` unchanged, 1242 lines, 54 KB of tests in `tools/test_player_led_policy.cpp`):
the player leads the party and walks wherever he wants; the bots follow *him* (they keep him as their commander, nothing is rebound); every second the run reads
where he stands (`PlayerLedPick`), picks the plan's open pack or boss step nearest him within `AiPlayerbot.DungeonRunPlayerLedPullYd` (default 40 yd, on the
same floor and team, a navmesh way no longer than 1.5 times the straight line) or the one the party is already fighting, and the tank pulls it once he stands still
near his bots (the gate, the rest, the crowd control, the revives and the fight ceilings are the bot-led step machine's own). The run never orders a walk (`move`,
`walk`, `escape`, `rescue` are dropped: "you lead the way"); the plan's rows are the *order of the packs and the places of the bosses*, not a route. A player's own order
never pauses the run (`move` classes are obeyed, a fight order is his pull, `stay` holds the next pull until his `follow`); only `.tb run pause` / `stop` do. He down (ghost) or off the
plan's map: the bots hold; back: they follow again; away longer than `AiPlayerbot.DungeonRunPlayerLedAwayMinutes` (10): the run stops. Every pack and boss down: the run is done.
The end of a player-led run (stop, done, time limit) sends the bots `follow` (not `stay`): they go on following him.

**Refusals** (nothing changes, in the order checked; `forever/src/host/run/TbRunPlayerLedPure.h`, the bind gate of the Turtle policy `PlayerLed::CheckBind`):
`no-commander` (the console), `not-bot` (the tank), `no-plan`, `disabled`, `commander-is-bot`, `not-grouped`, `raid`, `not-leader` (the starter must lead the party),
`group-too-large`, **`no-bots`** (no bot in the party) and **`not-controllable`** (a bot that is not the starter's own: its host master and its commander are somebody else's),
`not-live` (a bot out of the world), `hired`, `bot-bound` (a bot of his in another commander's binding, for example a bot-led run's tank),
`tank-not-in-party`, `tank-not-bot`, `not-commanded` (his own bot with no commander yet: bind the party first, `.tb master party`, the run never binds or rebuilds a kit),
then the executor's tank check (`no-engine`, `no-tank`: a warrior tank or a bear druid with a pull kit, `PlayerLedStartCheck`), `already-running`, `wrong-map` (he and every bot stand in his instance of the
plan's map), `bags`, `loot-method`. "Owned" means the bot's host master or its commander is the starter. Other real players in the party are allowed (they are never ordered).

**Engine facts used** (all in project files; no ForeverCore change):
* the bot-led run is keyed by its tank and requires the tank to lead the group (`TickRun`, `group->GetLeaderGUID() != rec.leader`): the player-led tick expects the player
  as group leader instead;
* `SyncBinding` would bind every member to the tank (`SyncBinding`, `BindMember`): skipped for a player-led run, as is the real-player pause and `OutsideOrder`'s pause (replaced by `PlayerLed::NotePlayerOrder`);
* the executor requires followers bound to the lead bot (`ReadParty` in `TbRunOrders.cpp`, `expectedCommander`): the new `Party::commander` is the player for a player-led run, so `follow` and `revive party` line up with the bots' real commander;
* the tank is not held from following (`HoldRunLead`, read in `TbOrders.cpp` `BeforeEngineTick`, is not called): it follows the player like the other bots, and pulls on the run's orders;
* the run orders only the roster it accepted at the start: the executor's party is the tank plus `bound` (`Issue`, `TbRunOrders.cpp`, `party.members`), never every bot the group holds now;
  every second `SyncPlayerLedRoster` drops a bot that left the party or is commanded by somebody else (one `roster` event each), and the run ends when its tank is rebound ("the tank bot is no longer commanded by you");
* `follow` (the player is back) and `revive party` go to the tank too in a player-led run (a bot-led run's lead gets `stay` there, `TbRunOrders.cpp` `DoPartyWide`/`DoReviveParty`): the player, not the tank, walks;
* the party-wide reads (`PartyGuids`: the tank and `bound`) list the owned bots, so a stop, a pause and the combat read cover them; `ReleaseMember` leaves their binding alone because their commander is not the tank (`TbRun.cpp` `ReleaseMember`);
* a group's run is found by the group leader or, for a player-led run, by its commander (`RunOfGroupLeader`, `TbRun.cpp`): the hooks the bot engine asks (`BotInRunningRun`, ...) answer for a player-led party too.

**Not done / limits.** A bot that joins the party after the start is not part of the run (Turtle: the same). `summon` is still not supported on ForeverCore (decision D3), so the straggler
ladder's `summon <bot>` is refused `unsupported` by the executor and the run's note says so; the player brings the straggler. The run rows' `from step N` start of
`run/wc-r9` is not combined with `lead=player` (a partway start of the picker exists, `PlayerLed::State::started`, but is not wired). A player-led run is not offered to the dashboard feed beyond what a run record already shows (`mode=player commander=<name>` in `.tb run status`).

## Tests

* `python3 forever/tools/test_run_knobs.py` - the table against the source and the template (mutations below).
* `g++ -std=c++17 -Wall -Wextra -I. forever/tools/test_run_playerled.cpp -o /tmp/test_run_playerled && /tmp/test_run_playerled` - 143 checks: the `lead=` word, every start refusal, and the scripted headless
  check: a GM walks a seven-step plan from its entrance, the run pulls pack one, two, three and then the first boss each only when he stands near it and still, in plan order, never orders a walk,
  and never touches the pack after the boss; the default (state off) still walks the plan itself; the GM away is `stay`, back is `follow`.
* `python3 forever/tools/test_run_playerled_wiring.py` - 16 source pins for the glue no standalone test can run: a bot-led run is untouched (`StartRun`, `SyncBinding`, the real-player pause), the player-led tick (group leader,
  roster, tank rebound, key off, pick before the facts, the player-led `Tick`), the executor (roster only, followers lined up with the player, the tank follows), the end sends `follow`, the three keys are read by the glue.
* the policy's own tests stay in `tools/test_player_led_policy.cpp` (`tools/verify_all.sh`).

Mutations the tests catch (run by hand, 2026-11-22; the knob test: a built file starting to read a not-read key, a moved `read` citation, a stale code statement; the wiring test: the tank given `stay`, a bind in a player-led run): a not-read key that a built file starts to read; a `read` row whose line moved; the `lead=` word taken from the leader or plan position; `DecideStart` skipping the owned-bot
refusal or the commander rule; `Pick` never picking a step (the boss is never reached); `Dropped` letting a walk through; a bot-led tick routed through the player-led policy.

## Review decisions (Codex adversarial, 2026-11-22)

* **"Enabled by default": not changed.** The opt-in is the explicit word `lead=player` on the start (owner decision 2026-11-22: "an explicit option ... default stays D1"): with no word no run is ever player-led, whatever
  the key says. `AiPlayerbot.DungeonRunPlayerLed` is only the server owner's kill switch and keeps the Turtle default 1: the same shared `PlayerbotAIConfig.cpp` sets it for the Turtle build, and a TBF-only default would
  break the AGENTS.md rule that the code fallback equals the shipped template. Set it to 0 to refuse the word everywhere.
* **Orders to a bot that joined or was rebound after the start** (real): fixed, the roster above. Round 2: a dropped bot's queued run orders and run pull are cancelled before the entry goes
  (`CancelRunWork` cannot reach it afterwards), and `RunOfBot` gives a late joiner or a dropped bot no run policy (kill-first, armed triggers, plan map).
* **The recovery `follow` made the tank stay** (real): fixed, the tank follows in a player-led run.
* **Two player-led runs in one party** (real): `StartPlayerLedRun` refuses `already-running` when the player has a live player-led run or any bot of the party (the tank included) is in another live run.

## Live check

`LIVE-QUEUE.md` row (READY, forever-test2): a player-led RFC start with a GM leading, the bots follow to the first pull.
