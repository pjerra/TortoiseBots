# W6a-diag (H6) notes

## What changed
- `.tb itemscan` (`forever/src/host/TbTestKit.cpp`, `forever/src/host/TbPartyState.cpp` via `AppendItemScanEngine`):
  - Appends `usage=` (`ItemUsageValue::QueryItemUsageForEquip` short name, omitted when the bot has no engine), `w=` (`sRandomItemMgr.ItemStatWeight(bot, qual)`), and `worn=` / `wornw=` (`ItemUsageValue::CurrentItemInSlot` entry and weight, omitted when empty or unusable).
- `.tb state` (`forever/src/host/TbPartyState.cpp`):
  - Appends `spec=` (`GetPlayerSpecId` / `GetFallbackSpecId` -> `GetSpecName`) and `gear=<slot>:<item>;...` across `EQUIPMENT_SLOT_START .. EQUIPMENT_SLOT_END - 1` via `Rec::LP`.
- `.tb statcensus` (`forever/src/host/TbH3.h`, `forever/src/host/TbH3Off.cpp`, `forever/src/host/TbTestKit.cpp`, `forever/src/TbScripts.cpp`):
  - New command scanning `sObjectMgr->GetItemTemplateStore()` in one pass (using `tbcompat::ItemProto` mirror stats) and reporting per-stat-id `items`, `min`, `max`, `sample`, `samplename`, `sampleval` plus end totals `templates`, `with_stats`, `stat_ids` in grammar v2.
  - OFF build (`TbH3Off.cpp`) answers `error code=engine-not-built cmd=statcensus ...`.
- Docs: updated `forever/docs/TB-COMMANDS.md`.

## Gates
- `python3 forever/tools/slice_registry_check.py`: `summary: (a)=0  (b)=224 keys to register or stub later`
- `python3 forever/tools/slice_gen_context.py --check`: `SliceAiObjectContext.cpp is up to date`
- ON build (`$BIN/tbf-build.sh wt-tbf-diag tortoisebots`): `[268/268] Linking CXX static library src/server/modules/TortoiseBots/libtortoisebots.a` (exit 0)
- OFF build (`TB_SLICE=OFF $BIN/tbf-build.sh wt-tbf-diag tortoisebots`): `[27/27] Linking CXX static library src/server/modules/TortoiseBots/libtortoisebots.a` (exit 0)
- Census (`$BIN/tbf-build.sh wt-tbf-diag census:forever/SLICE9.txt`): `files 265  compile clean 0  with errors 265  error places 0 (in 0 files)  includes of a header ForeverCore does not have 0`

## Untested / Open
- Runtime live test (`.tb statcensus`, `.tb itemscan`, `.tb state` against `worldserver` with `g-weights` and `g-equip` merged) not run here per package instructions.
- On `main` before `g-weights` merges, `RandomItemMgr` stubs return `0`/`""` for `w=`, `wornw=`, and `spec=`.
