# Commander P6: Task Orders (Objects, Loot Windows, Gossip) — Notes

Branch: `feat/cmd-p6-interact` (merged `origin/main` `83a5807f5` into `origin/cmd/p2` `e5bb5104d`; conflicts in `forever/src/TbScripts.cpp` and `forever/src/host/run/TbRun.cpp` resolved keeping both sides' intent).

## What Was Done

1. **Shared Task Core (`forever/src/host/run/TbRunTasks.{h,cpp}`, `TbRunTasksPure.h`)**:
   - Extended `TbRunTasksPure.h` with `Verb::Gossip`, `Kind::Gossip`, `OwnerKind::{Run,Commander}`, `MakeGossipOption(...)` (mapping `GossipOptionNpc::None` to plain gossip and non-`None` to service options for `GossipOrder::IsPlainOption`), and `VerbName(...)`.
   - Refactored `TbRunTasks.cpp` so `Begin(RunRec&...)` and `BeginCommander(ObjectGuid commander, ...)` share one `BeginTask(...)` implementation, and `SetLootMethod(RunRec&...)` and `SetLootMethodForParty(...)` share one loot-method implementation.
   - Added `BeginGossipAct(...)` using `GetNPCIfCanInteractWith(..., UNIT_NPC_FLAG_GOSSIP, UNIT_NPC_FLAG_2_NONE)`, `WorldPackets::NPC::Hello` -> `HandleGossipHelloOpcode`, `GossipOrder::MatchOption`, and `WorldPackets::NPC::GossipSelectOption` -> `HandleGossipSelectOptionOpcode` (when an option is given; lists menu options via `GossipOrder::DescribeMenu` when omitted).
   - Added `BotStatesOf(ObjectGuid owner, bots)` to track active and last-completed task status per bot.
   - Updated `TbRun.cpp` `run::Update` to call `tasks::Update(diffMs)` before `if (Table().empty()) return;` so commander tasks tick even when no dungeon run is active.

2. **`.tb cmd` Task Verbs (`forever/src/host/cmd/TbCmdTasks.{h,cpp}`, `forever/src/TbScripts.cpp`)**:
   - Implemented `.tb cmd loot|open|use|gossip|state|lootmethod` (plus `ritual` / `scout` -> `unsupported`, `use <itemId>` -> `unsupported`).
   - Added `#else` (`TORTOISEBOTS_SLICE1=OFF`) stub in `TbCmdTasks.cpp` returning `error code=engine-not-built cmd=cmd`.
   - Exposed `TryHandleTaskVerb(...)` in `TbCmdTasks.h` as a one-line seam for A (`TbCmdOrders.cpp`) to dispatch task verbs from its unified `.tb cmd` handler.
   - Registered `{ "cmd", TortoiseBots::cmd::HandleCmd, rbac::RBAC_PERM_COMMAND_GM, Console::Yes }` in `forever/src/TbScripts.cpp`.

3. **Docs & Tests**:
   - Updated `forever/docs/TB-COMMANDS.md` (section 14) and `forever/docs/UNPORTED.md` (`incoming: gossip, game object and area trigger` and `game object use` notes).
   - Extended `forever/tools/test_run_tasks.cpp` with `TestGossipAndCommanderP6` (option matching, service/coded/invalid option refusal, gossip walk -> act -> fail, empty/already-looted corpse refusal).
   - Added `forever/tools/test_cmd_p6_wiring.py` (with `--mutate` mode verifying 3 mutations fail).
   - Updated `forever/tools/test_slice9_doors_wiring.py` to inspect `BeginTask(` and `SetLootMethodForParty(`.

## Gates

- `tbf-build.sh ... tortoisebots` (`TORTOISEBOTS_SLICE1=ON`): `[4/4] Linking CXX static library src/server/modules/TortoiseBots/libtortoisebots.a`
- `TB_SLICE=OFF tbf-build.sh ... tortoisebots`: `[4/4] Linking CXX static library src/server/modules/TortoiseBots/libtortoisebots.a`
- `tbf-build.sh ... census:forever/SLICE9.txt`: `files 265  compile clean 265  with errors 0  error places 0 (in 0 files)  includes of a header ForeverCore does not have 0`
- `test_run_tasks`: `test_run_tasks: 160 checks, 0 failed`
- `test_run_orders`: `RESULT: PASS (209 checks)`
- `test_cmd_p6_wiring.py --mutate`: `PASS: test_cmd_p6_wiring (all 3 mutations caught)`
- `test_slice9_doors_wiring.py`: `Ran 24 tests in 0.006s — OK`
- `test_cmd_p2_wiring.py`: `Ran 17 tests in 0.018s — OK (skipped=4)`
- `slice_registry_check.py`: `summary: (a)=0  (b)=224 keys to register or stub later`
- `slice_gen_context.py --check`: `SliceAiObjectContext.cpp is up to date`

## Live Test Steps (For Lead Integration)

1. Log in GM character `Mcmd` and bind a party in Ragefire Chasm (e.g. `Mtank` as lead):
   - `.tb login Mtank Mcmd` (and party members)
   - `.tb engine Mtank on`
   - `.tb master party Mtank Mcmd`
2. **Loot & already-looted corpse**:
   - Kill a mob, target corpse (`my` or `<32-hex guid>`):
   - `.tb cmd loot my Mtank #p6_1` -> expect `cmd verb=loot bot=Mtank status=accepted target=... #p6_1`
   - `.tb cmd state Mtank #p6_2` (after 2-4 s) -> expect `task bot=Mtank busy=no status=done verb=loot ... #p6_2`
   - Repeat `.tb cmd loot my Mtank #p6_3` on the same corpse -> `.tb cmd state Mtank` reports `status=failed ... Nothing is left to loot` (never `done`).
3. **Open chest / Use door**:
   - Target chest/door (`my` or `entry:<id>`):
   - `.tb cmd open entry:<chest_entry> Mtank #p6_4` -> expect `status=accepted`, `.tb cmd state` -> `status=done`
   - `.tb cmd use entry:<door_entry> Mtank #p6_5` -> expect `status=accepted`, `.tb cmd state` -> `status=done` (repeating on open door reports `already open ...: not used again`).
4. **Gossip**:
   - Stand near a `UNIT_NPC_FLAG_GOSSIP` NPC (if `AiPlayerbot.BotGossipNpcs` is non-empty, include its entry):
   - `.tb cmd gossip my Mtank #p6_6` -> `.tb cmd state` lists menu options (`status=done`)
   - `.tb cmd gossip my Mtank 99 #p6_7` -> `.tb cmd state` reports `status=failed ... No option 99`
   - `.tb cmd gossip my Mtank 1 #p6_8` -> `.tb cmd state` reports `status=done ... selected 1 "..."`
5. **Loot method & unsupported**:
   - `.tb cmd lootmethod ffa #p6_9` -> expect `cmd verb=lootmethod status=ok method=ffa`
   - `.tb cmd ritual #p6_10` -> expect `error code=unsupported cmd=cmd ... #p6_10`

## What Waits for A (Commander P3)

- `TbCmdTasks.cpp` resolves the commander's bound party via `ResolveCommanderParty(...)` on `cmd/p2`'s `h3::CommanderBindings()` + `cmd::IsCommandedBy(...)`. When A's lifted `Party` / `TbCmdOrders.cpp` lands, `TbCmdOrders.cpp` can call `TryHandleTaskVerb(...)` in one line and `ResolveCommanderParty` can be rewired to A's `Party` helper.
- P6 does not duplicate A's `outside-order` run-pause hook; A's `Issue` / `TbCmdOrders` owns run-pause coordination when both land.
