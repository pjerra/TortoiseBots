# Fix: Gather & Craft Multi-Tick Errands (`fix/wl2-gather`)

Branch: `fix/wl2-gather` (base `feat/world-life-2`).

## Cause

1. Herb Gathering (`2366`) and Mining (`2575`) have a cast time: `OpenNode` cast the spell and immediately called `LootOpenNode` in the same tick, before the cast finished or loot existed, yet still returned `true` with `code="ok"`.
2. Out-of-range nodes started `MovePoint` and returned `true` (`code="walking"`), which `TbOrders::Run` turned into `ord=<seq>:ok`.
3. `LootOpenNode` returned `afterFree <= beforeFree`, which was true even when nothing was looted.
4. `Craft()` also fired `HandleCastSpellOpcode` in a loop and returned `ok` immediately without waiting for the cast or verifying the crafted item count rose.

## Fix

1. **Pure policies** (`GatherNodePolicy.h`, `CraftForSelfPolicy.h`):
   - `StepGatherErrand`: `Walking -> Casting -> Looting -> Done | Failed`. Fails with `node-gone` if the node despawns or is taken while walking, `timeout` if walk/cast/loot exceeds its bound, `no-loot` if looting yields 0 items, and `ok` only when `itemsGained > 0`.
   - `StepCraftErrand`: `Casting -> Done | Failed` (or `CastNext` for `count > 1`). Reports `pending:casting` while casting and `ok` only when the crafted item count rises (`craft-no-item` / `timeout` otherwise).
2. **Host multi-tick errand driver** (`TbHostProfession.h/.cpp`, `TbBotMgr.cpp`, `TbOrders.cpp`):
   - Keeps node `ObjectGuid` (never a raw pointer) and re-finds the `GameObject` each tick via `FindMap()->GetGameObject(guid)`.
   - `LootOpenNodeWithStats` snapshots bag counts before `HandleAutostoreLootItemOpcode`, verifies item count rose, releases the loot (`DoLootRelease`), and logs `[W7] gather bot=<bot> node=<entry> skill=<id> before=<v> after=<v> item=<name> count=<n>`.
   - `TbBotMgr::Update` ticks `tbhost::prof::TickErrands(me, diff)`; `TbOrders::BeforeEngineTick` polls `PollOrderErrand` so `tb state` shows `ord=<seq>:pending%3Awalking` / `pending%3Acasting` / `pending%3Alooting` until settled to `ok` or `fail:<code>`.
   - `TickFreeGather` and W9 `HostGatherCraftProvider::StepGatherOrCraft` (`TbHostGuildOrders.cpp`) share the same errand driver and count only confirmed item gains.

## Gates

- Pure tests (`forever/tools/test_w7_professions.cpp`): `PASS: test_w7_professions (49 checks, 0 failed)`
- Wiring & mutation (`forever/tools/test_w7_prof_wiring.py`): `Ran 4 tests in 0.001s -> OK`; `--mutate` -> `FAILED (failures=2)`
- Census (`forever/SLICE-W7.txt`): `files 286  compile clean 286  with errors 0  error places 0 (in 0 files)  includes of a header ForeverCore does not have 0`
- Census (`forever/SLICE-W9.txt`): `files 287  compile clean 287  with errors 0  error places 0 (in 0 files)  includes of a header ForeverCore does not have 0`
- Link ON (`worldserver`): `BUILD OK worldserver ON 4b3c775f7`

## Live check for lead

Position `Cmdr` (Herbalism) near Silverleaf (`1617`), >5 yd away:
1. `tb order Cmdr gather #g1` -> `tb state Cmdr #g2` immediately shows `ord=<seq>:pending%3Awalking` (then `pending%3Acasting` / `pending%3Alooting`), never `ok` early.
2. Once looted, `tb state Cmdr #g3` shows `ord=<seq>:ok`, Silverleaf in bags, and `Server.log` shows `[W7] gather bot=Cmdr node=1617 skill=182 before=... after=... item=Silverleaf count=...`.
3. Despawn or have another player take the node mid-walk -> `ord=<seq>:fail%3Anode-gone`.
4. `tb order Cmdr craft 3755 1 #c1` -> `tb state Cmdr #c2` shows `ord=<seq>:pending%3Acasting` until cast completes and Linen Bag count rises -> `ord=<seq>:ok`.
