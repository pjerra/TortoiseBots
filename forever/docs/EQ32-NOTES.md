# EQ32 Notes: `.tb gear <bot|party [leader]> upgrade [#token]`

Branch: `feat/equip-order-32`.

## What was done

- **Explicit-order allowance (`forever/src/compat/TbCompat_W6.h`):**
  - Added `tbs_w6::ExplicitEquipOrderFlag()` and RAII guard `tbs_w6::ScopedExplicitEquipOrder`.
  - Updated `MayAutoEquipUpgradesCompat(Player* bot)` so it returns true when `tbs_w6::ExplicitEquipOrderFlag()` is set or `!TortoiseBots::BotCommands::IsInCommandedGroup(bot)`.
  - Turtle twin (`ai/cmangos-compat-w6.h`) is unchanged.
- **Command handler (`forever/src/host/TbPartyState.cpp`, `forever/src/host/TbH3.h`, `forever/src/TbScripts.cpp`, `forever/src/host/TbH3Off.cpp`):**
  - Added `TortoiseBots::h3::HandleGear` and registered `"gear"` in `forever/src/TbScripts.cpp` (`Console::Yes`, `RBAC_PERM_COMMAND_GM`).
  - Added OFF-build stub `HandleGear -> NotBuilt(handler, args, "gear")` in `forever/src/host/TbH3Off.cpp`.
  - Validates all addressed bots up front (`no-engine`, `in-combat`), snapshots `EQUIPMENT_SLOT_START..EQUIPMENT_SLOT_END`, runs `ai->DoSpecificAction("equip upgrades", Event("tb gear", std::string(), bot), true)` inside `tbs_w6::ScopedExplicitEquipOrder`, and diffs slots before/after.
- **Docs & wiring test:**
  - Documented `.tb gear <bot|party [leader]> upgrade [#token]` in `forever/docs/TB-COMMANDS.md` section 6.
  - Added `forever/tools/test_eq32_wiring.py` (including `--mutate` mode).

## Gates

- ON module build (`tbf-build.sh <wt> tortoisebots`): `[309/309] Linking CXX static library src/server/modules/TortoiseBots/libtortoisebots.a` (exit 0).
- OFF module build (`TB_SLICE=OFF tbf-build.sh <wt> tortoisebots`): `[27/27] Linking CXX static library src/server/modules/TortoiseBots/libtortoisebots.a` (exit 0).
- Census (`tbf-build.sh <wt> census:forever/SLICE9.txt`): `files 268  compile clean 268  with errors 0  error places 0 (in 0 files)  includes of a header ForeverCore does not have 0` (exit 0).
- Registry & context: `python3 forever/tools/slice_registry_check.py --quiet` -> `summary: (a)=0  (b)=231 keys to register or stub later`; `python3 forever/tools/slice_gen_context.py --check` -> `SliceAiObjectContext.cpp is up to date` (exit 0).
- Wiring tests (`python3 -m unittest forever/tools/test_eq32_wiring.py forever/tools/test_w6a_equip_wiring.py`): `Ran 10 tests in 0.001s` / `OK` (exit 0); mutation (`python3 forever/tools/test_eq32_wiring.py --mutate`): `AssertionError: HandleGear must scope the explicit-order allowance with ScopedExplicitEquipOrder` (exit 1).

## Expected live output

```
gear-begin #g1
gear bot=Mtank slot=15 old=2504 new=15210 #g1
gear bot=Mpriest changed=0 #g1
gear-end count=2 records=2 target=party:Mtank bots=2 changed=1 #g1
```

In combat or without engine:
```
error code=in-combat cmd=gear msg=Mtank%20is%20in%20combat #g2
error code=no-engine cmd=gear msg=Mtank%20has%20no%20running%20engine%20%28%60.tb%20engine%20%3Cbot%3E%20on%60%29 #g2
```

In OFF build:
```
error code=engine-not-built cmd=gear msg=the%20engine%20is%20not%20built%20in%20%28this%20build%20has%20no%20group-fight%20commands%29 #g3
```

## Untested

- `worldserver` link and live `forever-agent` / console test (left for lead integration).
