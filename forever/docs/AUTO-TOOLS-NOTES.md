# Auto-grant Profession Tools (`TortoiseBots.AutoTools`)

- **Config:** `TortoiseBots.AutoTools = 1` (default `1`) in `forever/conf/tortoisebots.conf.dist`.
- **Scope:** Pool bots (`IsPoolBot`) and free bots (`IsFreeBot`). Never real players or owned alts.
- **Trigger:** `OnLogin` and `OnLevelChanged` in `forever/src/TbScripts.cpp` (both `TORTOISEBOTS_SLICE1=ON` and `OFF` builds).
- **Rules (`forever/src/host/TbAutoToolsPure.h`):**
  - Mining (`186`) -> Mining Pick (`2901`; satisfied by any pick in `756, 778, 1819, 1893, 1959, 2901, 9465, 20723`)
  - Skinning (`393`) -> Skinning Knife (`7005`; satisfied by any knife in `7005, 12709, 19901`)
  - Blacksmithing (`164`) -> Blacksmith Hammer (`5956`)
  - Fishing (`356`) -> Fishing Pole (`6256`)
  - Engineering (`202`) -> Blacksmith Hammer (`5956`) + Arclight Spanner (`6219`)
  - Enchanting (`333`) -> Runed Copper Rod (`6218`; satisfied by any rod in `6218, 6339, 11130, 11145, 16207`)
- **Storage:** `CanStoreNewItem(INVENTORY_SLOT_BAG_0, NULL_SLOT, dest, itemId, 1)` + `StoreNewItem` into a free bag slot (never equips over weapons).
- **Shared gather check:** `forever/src/host/TbHostProfession.cpp` (`HasRequiredGatherTool`) reuses `TortoiseBots::autotools::HasRequiredGatherTool`.
