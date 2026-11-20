# W6a Package E6 Notes (Auto-Equip Upgrades)

Branch: `feat/w6a-equip` (base `main`).

## What was done

- **Slice list & context registration:**
  - Added `ai/playerbot/strategy/actions/EquipAction.cpp` and `ai/playerbot/strategy/generic/MaintenanceStrategy.cpp` to `forever/SLICE9.txt`.
  - Registered `maintenance` (strategy), `equip upgrades` (action), `equipment audit` and `item push result` (triggers) in `forever/tools/slice_gen_context.py` (`W6_KEYS`) and regenerated `forever/src/slice/SliceAiObjectContext.cpp`.
  - Added `nonCombatEngine->addStrategy("maintenance")` in `forever/src/slice/SliceAiFactory.cpp` so free bots run the 300 s `equipment audit` trigger (other `MaintenanceStrategy` keys stay unregistered and are skipped by the engine).
- **Compat helpers (`forever/src/compat/TbCompat_W6.h`, `ai/cmangos-compat-w6.h`, included via the shims):**
  - `AutoEquipCompat(Player*, uint8, uint8)`: sends typed `WorldPackets::Item::AutoEquipItem` (with `Inv.Items.push_back({ bagIndex, slot })`) into `WorldSession::HandleAutoEquipItemOpcode` on ForeverCore; Turtle body keeps the original `WorldPacket(CMSG_AUTOEQUIP_ITEM, 2)` call.
  - `CanUseItemForEquipCompat(Player const*, ItemPrototype const*)`: on ForeverCore checks `bot->CanUseItem(tpl)` plus `tpl->GetSkill()` (`bot->GetSkillValue(itemSkill) == 0 -> EQUIP_ERR_PROFICIENCY_NEEDED`), closing the `ItemTemplate*` proficiency gap in `ItemUsageValue::QueryItemUsageForEquip` for both bag items and template-only queries; Turtle body keeps `bot->CanUseItem(proto)`.
  - `MayAutoEquipUpgradesCompat(Player*)`: enforces decision D4 at `EquipUpgradesAction::Execute` (`!TortoiseBots::BotCommands::IsInCommandedGroup(bot)` on ForeverCore; `true` on Turtle).
  - `EQUIP_ERR_INVENTORY_FULL` mapped to `EQUIP_ERR_INV_FULL` in `TbCompat_W6.h`; `EquipAction.cpp` updated to use existing `PlayerAmmoIdCompat` (`TbCompat_S9.h`) and `ItemDurabilityCompat`/`ItemMaxDurabilityCompat` (`TbCompat_Unit.h`).
- **Push poll (`forever/src/compat/TbCompat_S9.h`):**
  - Extended `PollPacketEventCompat` with `"item push result"` via `tbs9::PollItemPush`, tracking per-bot bag item GUIDs and emitting a push event when a new `WEAPON`, `ARMOR`, `CONTAINER`, or `QUIVER` item enters the bags.
- **Unported stand-ins (`forever/src/host/TbHostOwnership.cpp`, `forever/src/slice/SliceLinkStubs.cpp`, `ai/playerbot/UnportedFeatures.h`, `forever/docs/UNPORTED.md`):**
  - Removed the `SliceLinkStubs.cpp` stand-in for `ai::EquipAction::EquipItem`.
  - Added stand-ins that do LESS: `ClaimLifecycle::IsClaimed` (`false`, `Feature::PoolServices`), `BotGearService::PinnedCap` (`0`, `Feature::PoolServices`), and `PlayerbotDbStore::Save` (no-op, `Feature::MiscLegacyServices`).
- **Wiring test (`forever/tools/test_w6a_equip_wiring.py`):**
  - Pins both-core header parity, Turtle original expressions, typed `AutoEquipItem`, proficiency check, push poll + slice registration, and D4 free-bot-only gate (with `--mutate` failing non-zero).

## Gates

- ON module build (`tbf-build.sh <wt> tortoisebots`): `[4/4] Linking CXX static library src/server/modules/TortoiseBots/libtortoisebots.a` (exit 0).
- OFF module build (`TB_SLICE=OFF tbf-build.sh <wt> tortoisebots`): `[26/26] Linking CXX static library src/server/modules/TortoiseBots/libtortoisebots.a` (exit 0).
- Census (`tbf-build.sh <wt> census:forever/SLICE9.txt`): `files 267  compile clean 0  with errors 267  error places 0 (in 0 files)  includes of a header ForeverCore does not have 0` — note: `/build` `compdb` in `forever-local` names `/usr/bin/c++` (GCC), which rejects `port_census.py`'s clang-only flags (`-ferror-limit=0` / `-include-pch`) before parsing.
- Wiring tests (`python3 -m unittest forever/tools/test_w6a_equip_wiring.py forever/tools/test_slice9_l9_wiring.py`): `Ran 19 tests in 0.004s` / `OK` (exit 0); mutation (`python3 forever/tools/test_w6a_equip_wiring.py --mutate`): `AssertionError: EquipUpgradesAction::Execute missing MayAutoEquipUpgradesCompat(bot) gate` (exit 1).
- Context & registry (`python3 forever/tools/slice_gen_context.py --check && python3 forever/tools/slice_registry_check.py --quiet`): `SliceAiObjectContext.cpp is up to date` / `summary: (a)=0  (b)=231 keys to register or stub later` (exit 0).

## Untested / Open Questions

- **Not run here:** `worldserver` link (deferred to lead integration with `g-weights` and `g-diag`, since `RandomItemMgr` weight/spec symbols are in `g-weights`), Turtle build/unit tests (Turtle-side behaviour in `ai/cmangos-compat-w6.h` is identical to the original expressions), and live `forever-agent` tests.
- **Note on `ItemDurabilityCompat`:** `ItemDurabilityCompat` and `ItemMaxDurabilityCompat` already existed in `forever/src/compat/TbCompat_Unit.h` and `ai/cmangos-compat-r3.h`, so `EquipAction.cpp` reuses them directly rather than redefining `ItemDurabilityCompat` in `TbCompat_W6.h`.
- **Merge note:** `forever/src/compat/TbCompat_W6.h` and `ai/cmangos-compat-w6.h` contain our `// equip` block and an empty `// weights` placeholder for `g-weights`.
