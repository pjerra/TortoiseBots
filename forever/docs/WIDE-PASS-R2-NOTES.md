# Wide mechanical pass R2: four more families, with four new both-core helper groups

Branch `port/wide-pass-2` (off `port/forever`: slices 1-4 and the first wide pass are in), worktree `~/wt-wide2`, build slot 2.
Second pass over the legacy files that are in no slice, this time ADDING helpers for four of the families the first pass
(`WIDE-PASS-R1-NOTES.md`, table "Left for the slices") had to leave. Method, rules and scripts as the first pass; the helpers
live ONLY in a new pair, found through a wrapper (the way slice 8 did it), so no shared shim and no file under `forever/src/slice` is edited.

| piece | path |
|---|---|
| ForeverCore side | `forever/src/compat/TbCompat_W2.h` |
| Turtle side (same names and signatures) | `ai/cmangos-compat-w2.h` |
| the wrapper every rewritten file includes (`__has_include("TbCompat_W2.h")`, else the Turtle file) | `ai/playerbot/TbCompatW2.h` |
| family 1 generator + include inserter | `forever/tools/w2_names.py` |
| families 2-4 rewrite script | `forever/tools/rewrite_wide_pass2.py` |
| files this pass must not touch | `forever/tools/wide2-exclude.txt` (394 paths) |

Excluded: every file in `forever/SLICE*.txt` of this tree (slices 1-4 and their sub-lists), `forever/SLICE8.txt` of `tbf/s8/k`,
`forever/SLICE5.txt` of `tbf/run/f6` (fetched first), the first pass's own exclude list, and the `.h`/`.cpp` of the same base name of each.
`tbf/run/s5-integ` does not exist on the remote (no such branch to read; `tbf/run/f6` carries the slice-5 list). A rewritten file gets
`#include "playerbot/TbCompatW2.h"` after its last leading include (`"../ai/playerbot/TbCompatW2.h"` in `runtime/`, as its neighbours reach `ai/`).

Census command: `FOREVER_SLOT=2 forever/tools/vm-census.sh "" 10`. Tables of the baseline are in `~/wide2-census/before/` on the laptop
(errors.tsv, names.tsv, where.tsv).

## Numbers (whole tree, slot 2)

| step | summary line (`forever/tools/vm-census.sh "" 10`, slot 2) | change |
|---|---|---|
| baseline (`port/forever` + first pass) | `files 649  compile clean 354  with errors 295  error places 6505 (in 315 files)  includes of a header ForeverCore does not have 40` | |
| after family 1 (names) | `files 649  compile clean 356  with errors 293  error places 6145 (in 313 files)  includes ... 40` | -360 (2 files clean: `GreetAction.cpp`, `DruidAiObjectContext.cpp`) |
| after family 2 (getConfig) | `files 649  compile clean 354  with errors 295  error places 6040 (in 313 files)  includes ... 40` | -105; **but 2 files lost their clean state**: see "Bug found by the census" |
| after family 3 (group id) | `files 649  compile clean 354  with errors 295  error places 6030 (in 313 files)  includes ... 40` | -10 (exactly the 10 sites) |
| after family 4 (spawn position) | `files 649  compile clean 354  with errors 295  error places 5997 (in 313 files)  includes ... 40` | -33 (exactly the 33 census places) |
| final before the review (log line fixed) | `files 649  compile clean 356  with errors 293  error places 5995 (in 312 files)  includes of a header ForeverCore does not have 40` | -2; 510 places closed at that point |
| final, after the review reverted 9 group-id sites | `files 649  compile clean 356  with errors 293  error places 6004 (in 312 files)  includes of a header ForeverCore does not have 40` | +9 (the reverted sites): **501 places closed in all**, 2 files clean, none gained a place; slice censuses 4/3/2/1 again 174/170/160/133 clean, 0 errors; option-OFF build `CMAKE_RC=0 MAKE_RC=0` |

Family 1 closed 360 places, more than the 304 name uses it rewrote: an undeclared name drags the types of the expression it is in (`invalid argument type`, `no matching function`) with it.

Bug found by the census (fixed in the last code commit): the first version of `NoteDefault` in `TbCompat_W2.h` used `TC_LOG_WARN`, which expands `sLog->...`;
the shim renames `sLog` to its printf proxy, so every file that includes the wrapper AFTER the shim failed with "no viable conversion from 'BotLog' to 'Log *'" at that line.
Only 2 files were affected on the surface (the two that family 1 had just made clean); the other including files already had errors of their own. It now calls
`Log::instance()->OutMessage(...)`, as `CmangosLogProxy` does (`forever-compat-shim.h:495`). A one-file census of `GreetAction.cpp` is clean again and the final census above has no error in `TbCompat_W2.h`.


## Family 1: `TEXTEMOTE_*` and the other name-level identifiers

Helper: none (names). Both cores' enumerators are read by `w2_names.py` and every row is checked, then written as a `#define` in the
generated block of `TbCompat_W2.h` (the Turtle file needs nothing: the Turtle core has the names).

* `TEXTEMOTE_*`: Turtle `enum TextEmotes` (`SharedDefines.h:394`, ~/playerbots-references/penqle-tortoise-wow-modsys/src/game) against
  ForeverCore `enum TextEmotes` (`src/server/game/Miscellaneous/SharedDefines.h:3299`). A name is mapped only when ForeverCore has the same
  name with `TEXT_EMOTE_` AND the same number: 171 of 172 rows qualify (every number identical; the generated block prints
  `T:<line> F:<line>` per row). 167 are defined here; `TEXTEMOTE_CHEER` (`TbCompat_I2.h:15`), `TEXTEMOTE_NOD`, `_NO`, `_OPENFIRE`
  (`TbCompat_Names.h:177-179`) already exist and are not redefined. NOT mapped (stays undeclared, nothing in the code uses it):
  `TEXTEMOTE_MOUNTSPECIAL` = 366, which ForeverCore's enum does not have. 42 further `TEXTEMOTE_` names appear in the tree only inside
  comments (`//case TEXTEMOTE_FACEPALM:` and so on) and are not defined.
* 47 other names, each verified by hand for the same meaning and the same number in both cores (T value and file:line, F value and file:line):

| Turtle name | T value, file:line | ForeverCore name | F value, file:line |
|---|---|---|---|
| `ITEM_CLASS_JUNK` | 15, Objects/ItemPrototype.h:161 | `ITEM_CLASS_MISCELLANEOUS` | 15, server/game/Entities/Item/ItemTemplate.h:489 |
| `TRADE_STATUS_BEGIN_TRADE` | 1, SharedDefines.h:1839 | `TRADE_STATUS_PROPOSED` | 1, server/game/Miscellaneous/SharedDefines.h:7117 |
| `TRADE_STATUS_OPEN_WINDOW` | 2, SharedDefines.h:1840 | `TRADE_STATUS_INITIATED` | 2, server/game/Miscellaneous/SharedDefines.h:7118 |
| `TRADE_STATUS_TRADE_CANCELED` | 3, SharedDefines.h:1841 | `TRADE_STATUS_CANCELLED` | 3, server/game/Miscellaneous/SharedDefines.h:7119 |
| `TRADE_STATUS_TRADE_ACCEPT` | 4, SharedDefines.h:1842 | `TRADE_STATUS_ACCEPTED` | 4, server/game/Miscellaneous/SharedDefines.h:7120 |
| `BIND_QUEST_ITEM` | 4, Objects/ItemPrototype.h:63 | `BIND_QUEST` | 4, server/game/Entities/Item/ItemTemplate.h:145 |
| `BIND_WHEN_USE` | 3, Objects/ItemPrototype.h:62 | `BIND_ON_USE` | 3, server/game/Entities/Item/ItemTemplate.h:144 |
| `BIND_WHEN_PICKED_UP` | 1, Objects/ItemPrototype.h:60 | `BIND_ON_ACQUIRE` | 1, server/game/Entities/Item/ItemTemplate.h:142 |
| `BIND_WHEN_EQUIPPED` | 2, Objects/ItemPrototype.h:61 | `BIND_ON_EQUIP` | 2, server/game/Entities/Item/ItemTemplate.h:143 |
| `SKILL_2H_MACES` | 160, SharedDefines.h:1160 | `SKILL_TWO_HANDED_MACES` | 160, server/game/Miscellaneous/SharedDefines.h:5898 |
| `SKILL_2H_AXES` | 172, SharedDefines.h:1166 | `SKILL_TWO_HANDED_AXES` | 172, server/game/Miscellaneous/SharedDefines.h:5903 |
| `SKILL_2H_SWORDS` | 55, SharedDefines.h:1132 | `SKILL_TWO_HANDED_SWORDS` | 55, server/game/Miscellaneous/SharedDefines.h:5874 |
| `FORM_BEAR` | 5, SharedDefines.h:1588 | `FORM_BEAR_FORM` | 5, server/game/Spells/Auras/SpellAuraDefines.h:783 |
| `FORM_CAT` | 1, SharedDefines.h:1584 | `FORM_CAT_FORM` | 1, server/game/Spells/Auras/SpellAuraDefines.h:779 |
| `FORM_MOONKIN` | 31, SharedDefines.h:1601 | `FORM_MOONKIN_FORM` | 31, server/game/Spells/Auras/SpellAuraDefines.h:809 |
| `FORM_DIREBEAR` | 8, SharedDefines.h:1591 | `FORM_DIRE_BEAR_FORM` | 8, server/game/Spells/Auras/SpellAuraDefines.h:786 |
| `FORM_DEFENSIVESTANCE` | 18, SharedDefines.h:1597 | `FORM_DEFENSIVE_STANCE` | 18, server/game/Spells/Auras/SpellAuraDefines.h:796 |
| `GAMEOBJECT_TYPE_SUMMONING_RITUAL` | 18, SharedDefines.h:273 | `GAMEOBJECT_TYPE_RITUAL` | 18, server/game/Miscellaneous/SharedDefines.h:3184 |
| `GAMEOBJECT_TYPE_MO_TRANSPORT` | 15, SharedDefines.h:270 | `GAMEOBJECT_TYPE_MAP_OBJ_TRANSPORT` | 15, server/game/Miscellaneous/SharedDefines.h:3181 |
| `QUEST_TYPE_RAID` | 62, QuestDef.h:139 | `QUEST_INFO_RAID` | 62, server/game/Miscellaneous/SharedDefines.h:5559 |
| `QUEST_TYPE_DUNGEON` | 81, QuestDef.h:140 | `QUEST_INFO_DUNGEON` | 81, server/game/Miscellaneous/SharedDefines.h:5560 |
| `QUEST_TYPE_PVP` | 41, QuestDef.h:138 | `QUEST_INFO_PVP` | 41, server/game/Miscellaneous/SharedDefines.h:5558 |
| `UNIT_FLAG_IMMUNE_TO_PLAYER` | 256, Objects/UnitDefines.h:412 | `UNIT_FLAG_IMMUNE_TO_PC` | 256, server/game/Entities/Unit/UnitDefines.h:175 |
| `CHAT_TAG_NONE` | 0, Chat/Chat.h:85 | `CHAT_FLAG_NONE` | 0, server/game/Miscellaneous/SharedDefines.h:6559 |
| `TARGET_ICON_COUNT` | 8, Group/Group.h:49 | `TARGET_ICONS_COUNT` | 8, server/game/Groups/Group.h:54 |
| `CAST_FLAG_AMMO` | 32, Spells/Spell.h:67 | `CAST_FLAG_PROJECTILE` | 32, server/game/Spells/Spell.h:99 |
| `EQUIP_ERR_ITEM_DOESNT_GO_TO_SLOT` | 3, Objects/Item.h:50 | `EQUIP_ERR_WRONG_SLOT` | 3, server/game/Entities/Item/ItemDefines.h:29 |
| `EQUIP_ERR_NONEMPTY_BAG_OVER_OTHER_BAG` | 5, Objects/Item.h:52 | `EQUIP_ERR_BAG_IN_BAG` | 5, server/game/Entities/Item/ItemDefines.h:31 |
| `EQUIP_ERR_CANT_TRADE_EQUIP_BAGS` | 6, Objects/Item.h:53 | `EQUIP_ERR_TRADE_EQUIPPED_BAG` | 6, server/game/Entities/Item/ItemDefines.h:32 |
| `EQUIP_ERR_ONLY_AMMO_CAN_GO_HERE` | 7, Objects/Item.h:54 | `EQUIP_ERR_AMMO_ONLY` | 7, server/game/Entities/Item/ItemDefines.h:33 |
| `EQUIP_ERR_NO_REQUIRED_PROFICIENCY` | 8, Objects/Item.h:55 | `EQUIP_ERR_PROFICIENCY_NEEDED` | 8, server/game/Entities/Item/ItemDefines.h:34 |
| `EQUIP_ERR_NO_EQUIPMENT_SLOT_AVAILABLE` | 9, Objects/Item.h:56 | `EQUIP_ERR_NO_SLOT_AVAILABLE` | 9, server/game/Entities/Item/ItemDefines.h:35 |
| `EQUIP_ERR_YOU_CAN_NEVER_USE_THAT_ITEM` | 10, Objects/Item.h:57 | `EQUIP_ERR_CANT_EQUIP_EVER` | 10, server/game/Entities/Item/ItemDefines.h:36 |
| `EQUIP_ERR_YOU_CAN_NEVER_USE_THAT_ITEM2` | 11, Objects/Item.h:58 | `EQUIP_ERR_CANT_EQUIP_EVER_2` | 11, server/game/Entities/Item/ItemDefines.h:37 |
| `EQUIP_ERR_NO_EQUIPMENT_SLOT_AVAILABLE2` | 12, Objects/Item.h:59 | `EQUIP_ERR_NO_SLOT_AVAILABLE_2` | 12, server/game/Entities/Item/ItemDefines.h:38 |
| `EQUIP_ERR_CANT_EQUIP_WITH_TWOHANDED` | 13, Objects/Item.h:60 | `EQUIP_ERR_2HANDED_EQUIPPED` | 13, server/game/Entities/Item/ItemDefines.h:39 |
| `EQUIP_ERR_ITEM_DOESNT_GO_INTO_BAG` | 15, Objects/Item.h:62 | `EQUIP_ERR_WRONG_BAG_TYPE` | 15, server/game/Entities/Item/ItemDefines.h:41 |
| `EQUIP_ERR_ITEM_DOESNT_GO_INTO_BAG2` | 16, Objects/Item.h:63 | `EQUIP_ERR_WRONG_BAG_TYPE_2` | 16, server/game/Entities/Item/ItemDefines.h:42 |
| `EQUIP_ERR_CANT_CARRY_MORE_OF_THIS` | 17, Objects/Item.h:64 | `EQUIP_ERR_ITEM_MAX_COUNT` | 17, server/game/Entities/Item/ItemDefines.h:43 |
| `EQUIP_ERR_NO_EQUIPMENT_SLOT_AVAILABLE3` | 18, Objects/Item.h:65 | `EQUIP_ERR_NO_SLOT_AVAILABLE_3` | 18, server/game/Entities/Item/ItemDefines.h:44 |
| `EQUIP_ERR_ITEM_CANT_STACK` | 19, Objects/Item.h:66 | `EQUIP_ERR_CANT_STACK` | 19, server/game/Entities/Item/ItemDefines.h:45 |
| `EQUIP_ERR_ITEM_CANT_BE_EQUIPPED` | 20, Objects/Item.h:67 | `EQUIP_ERR_NOT_EQUIPPABLE` | 20, server/game/Entities/Item/ItemDefines.h:46 |
| `EQUIP_ERR_ITEMS_CANT_BE_SWAPPED` | 21, Objects/Item.h:68 | `EQUIP_ERR_CANT_SWAP` | 21, server/game/Entities/Item/ItemDefines.h:47 |
| `EQUIP_ERR_SLOT_IS_EMPTY` | 22, Objects/Item.h:69 | `EQUIP_ERR_SLOT_EMPTY` | 22, server/game/Entities/Item/ItemDefines.h:48 |
| `EQUIP_ERR_CANT_DROP_SOULBOUND` | 24, Objects/Item.h:71 | `EQUIP_ERR_DROP_BOUND_ITEM` | 24, server/game/Entities/Item/ItemDefines.h:50 |
| `EQUIP_ERR_TRIED_TO_SPLIT_MORE_THAN_COUNT` | 26, Objects/Item.h:73 | `EQUIP_ERR_TOO_FEW_TO_SPLIT` | 26, server/game/Entities/Item/ItemDefines.h:52 |
| `EQUIP_ERR_COULDNT_SPLIT_ITEMS` | 27, Objects/Item.h:74 | `EQUIP_ERR_SPLIT_FAILED` | 27, server/game/Entities/Item/ItemDefines.h:53 |


  A name whose number differs between the cores stays unported even when the meaning is the same: `EQUIP_ERR_INVENTORY_FULL` (T 50, F `EQUIP_ERR_INV_FULL` 51;
  ForeverCore's `InventoryResult` is renumbered from 33 on, ItemDefines.h) and every `EQUIP_ERR_*` from T 28 up, `SPELL_AURA_MOD_CRIT_PERCENT` (T 52, F
  `SPELL_AURA_MOD_CRIT_PCT` 290), `SPELL_AURA_MOD_SHIELD_BLOCKVALUE` (T 158, F `_PCT` 150). A name whose meaning differs: `EQUIP_ERR_CANT_DUAL_WIELD` (T 14, F 14 is
  `EQUIP_ERR_2HSKILLNOTFOUND`), `SEC_DEVELOPER` (T 3 "developer", F `SEC_ADMINISTRATOR` 3: a judgement about which account level the bot means), `QUEST_STATUS_AVAILABLE` (F has no such state),
  `TEAM_INDEX_*` (not in the Turtle reference core under that name). A name ForeverCore does not have: `TRAINER_TYPE_MOUNTS/TRADESKILLS/PETS` (F's enum holds only `_CLASS`, SharedDefines.h:5015),
  the `GOSSIP_ICON_*`/`GOSSIP_OPTION_*` names (F has them as a typed `GossipOptionIcon`, no spelling to map to), `VISIBILITY_OFF`, `UNIT_FLAG_SPAWNING`, `UNIT_NPC_FLAG_SPIRIT*`, all `BG_*` (battlegrounds not
  ported), the update-field indexes (`UNIT_FIELD_FLAGS`, `PLAYER_XP`, ... : the `GetUInt32Value` family, out of scope), the opcodes, the `*HOOK_*` ids, the Turtle-only `CHALLENGE_*` modes, `CONFIG_*` keys (family 2).
  Left name places by group: BG_* 208, update fields 133, hooks 51, GOSSIP 39, EQUIP_ERR 39, quest 36, trainer 35, CONFIG 35 (family 2 took 75 uses of the same names), spell 20, opcodes 20, SKILL 19, CHALLENGE 19, the rest about 200 in one-or-two-use names.
* Closed places: 167 emote names (197 error places in `EmoteAction.cpp`) + 47 other names (107 error places).
* Files that include the wrapper for family 1: 26. A source file that uses a mapped name but includes the wrapper gets the macro; nothing else changes.

Risk the pass checked: no mapped Turtle name is also an identifier in a ForeverCore enum (script: zero collisions over the 27,000 constants of `src/server` and `src/common`).

## Family 2: `sWorld.getConfig(...)` (75 sites, 21 files)

Helpers (one per Turtle value type): `WorldU32ConfigW2`, `WorldI32ConfigW2`, `WorldBoolConfigW2`, `WorldFloatConfigW2`. Turtle side: the original call
(`sWorld.getConfig(key)`, overloaded on the key's enum: `World.h:1100-1115`). The call site is rewritten by the key's own type token
(`CONFIG_UINT32_*` -> U32, `CONFIG_INT32_*` -> I32, `CONFIG_BOOL_*` -> Bool, `CONFIG_FLOAT_*` -> Float), argument unchanged.

ForeverCore side, three kinds of key:

1. A key with a ForeverCore equivalent of the SAME meaning: the helper reads the core (`getIntConfig` World.h:750, `getBoolConfig` :724, `getFloatConfig` :737, `getRate` :714). The Turtle name resolves to the core's
   enumerator through `forever-compat-shim.h:265-282` (existing macros) or, for the rates, through macros in `TbCompat_W2.h` (`#ifndef`-guarded). The value is the operator's setting in worldserver.conf, so a default that differs between the cores (below) is the operator's choice, not ours.
2. A key with no equivalent: an enumerator of `TbW2U32Key`/`TbW2BoolKey`/`TbW2FloatKey` (named as the Turtle key); the helper returns the TURTLE default and logs once per key (`server.loading` WARN: "server setting X has no ForeverCore equivalent; the bot uses the Turtle default D"). No value of our own.
3. `Rate.Talent`: no `World` member on ForeverCore, but the config key exists with the same meaning (talent points per level times it: `TraitMgr.cpp:95-105`, default 1, `worldserver.conf.dist:2758`): read with `sConfigMgr->GetFloatDefault("Rate.Talent", 1.0f)`, 1.0 being the Turtle default too.

| Turtle key (World.cpp line, default) | ForeverCore (World.h enumerator line; World.cpp default line) | same meaning? default there |
|---|---|---|
| `CONFIG_UINT32_MAX_PLAYER_LEVEL` (1095, 60) | `CONFIG_MAX_PLAYER_LEVEL` (:286; :782) | yes (MaxPlayerLevel); default = the expansion's level cap |
| `CONFIG_UINT32_MIN_PETITION_SIGNS` (1115, 9) | `CONFIG_MIN_PETITION_SIGNS` (:303; :799) | yes; 4 |
| `CONFIG_UINT32_SKILL_CHANCE_ORANGE/YELLOW/GREEN/GREY` (1146-1149, 100/75/25/0) | `CONFIG_SKILL_CHANCE_*` (:320-323; :815-818) | yes; 100/75/25/0 |
| `CONFIG_UINT32_SKILL_GAIN_CRAFTING` (1154, 1) | `CONFIG_SKILL_GAIN_CRAFTING` (:326; :821) | yes; 1 |
| `CONFIG_UINT32_CHARACTERS_PER_REALM` / `_PER_ACCOUNT` (1090-1091, 10/50) | `CONFIG_CHARACTERS_PER_REALM/_ACCOUNT` (:281/:280; :776/:777) | yes; 60/60 |
| `CONFIG_INT32_QUEST_LOW_LEVEL_HIDE_DIFF` (1187, 4) | `CONFIG_QUEST_LOW_LEVEL_HIDE_DIFF` (:338; :835), read back as int32 | yes; 4 |
| `CONFIG_BOOL_ALLOW_TWO_SIDE_INTERACTION_GUILD/GROUP/AUCTION` (1062/1061/1064, false) | `CONFIG_ALLOW_TWO_SIDE_INTERACTION_*` (:113/:112/:114; :627/:626/:628) | yes; false/false/**true** |
| `CONFIG_BOOL_ADDON_CHANNEL` (1037, true) | `CONFIG_ADDON_CHANNEL` (:106; :615) | yes; true |
| `CONFIG_BOOL_INSTANCE_IGNORE_LEVEL/RAID` (1109/1110, false) | `CONFIG_INSTANCE_IGNORE_LEVEL/RAID` (:118/:121; :634/:642) | yes; false |
| `CONFIG_FLOAT_CREATURE_FAMILY_ASSISTANCE_RADIUS` (1022, 10.0) | `CONFIG_CREATURE_FAMILY_ASSISTANCE_RADIUS` (:233; :955) | yes; 10.0 |
| `CONFIG_FLOAT_RATE_CREATURE_AGGRO` (Rate.Creature.Aggro, 1.0) | `RATE_CREATURE_AGGRO` (:545; World.cpp:1047) | yes; 1.0 |
| `CONFIG_FLOAT_RATE_AUCTION_DEPOSIT` (991, 1.0) | `RATE_AUCTION_DEPOSIT` (:551; :1053) | yes; 1.0 |
| `CONFIG_FLOAT_RATE_XP_KILL/QUEST/EXPLORE` (963/965/966, 1.0) | `RATE_XP_KILL/QUEST/EXPLORE` (:514/:516/:517; :1015/:1017/:1018) | yes; 1.0 |
| `CONFIG_FLOAT_RATE_DROP_MONEY` (962, 1.0), `_DROP_ITEM_POOR..LEGENDARY` (954-959, 1.0) | `RATE_DROP_MONEY` (:513; :1014), `RATE_DROP_ITEM_*` (:504-509; :1005-1010) | yes; 1.0 |
| `CONFIG_FLOAT_RATE_HONOR` (997, 1.0) | `RATE_HONOR` (:554; :1056) | yes; 1.0 |
| `CONFIG_FLOAT_RATE_REPUTATION_GAIN/LOWLEVEL_KILL/LOWLEVEL_QUEST` (967-969, 1.0) | `RATE_REPUTATION_GAIN/_LOWLEVEL_KILL/_LOWLEVEL_QUEST` (:520-522; :1022-1024) | yes; 1.0 |
| `CONFIG_FLOAT_RATE_TALENT` (1001, 1.0) | config key `Rate.Talent`, `TraitMgr.cpp:105` | yes; 1.0 |
| `CONFIG_FLOAT_RATE_XP_KILL_ELITE` (964, 1.0) | none (one kill rate, `Rate.XP.Kill`) | **no**: Turtle default 1.0 + log |
| `CONFIG_UINT32_AUCTION_DEPOSIT_MIN` (993, 0) | none | **no**: Turtle default 0 + log |
| `CONFIG_UINT32_ACCOUNT_CONCURRENT_AUCTION_LIMIT` (994, 0 = none) | none | **no**: Turtle default 0 + log |
| `CONFIG_UINT32_AV_MIN_PLAYERS_IN_QUEUE` (1222, 0) | none | **no**: Turtle default 0 + log |
| `CONFIG_BOOL_ALLOW_TWO_SIDE_ACCOUNTS` (1058, false) | none | **no**: Turtle default false + log |

Places: 75 (U32 33, I32 2, Bool 9, Float 31; files `RandomItemMgr.cpp`, `Talentspec.cpp`, `BudgetValues.cpp`, `CraftValues.cpp`, `ItemUsageValue.cpp`, `XpGainAction.cpp`, `Commander.cpp`, `ObservabilityEmitter.cpp`, the runtime services and others). None left.
Watch live: the three defaults that differ between the cores in the table are the operator's setting on ForeverCore (CharactersPer* 60, MinPetitionSigns 4, TwoSide.Auction true). The `Rate.XP.Kill.Elite` row means the observability emitter reports 1.0 for the elite rate on ForeverCore with one log line.

## Family 3: `Group::GetId()` (44 census places: 1 rewritten, 43 left; 9 sites reverted after review)

Helper: `GroupIdW2(Group const*)`. Turtle side: `group->GetId()` (`Group.h:215`, the groups-table id). ForeverCore side: `uint32(group->GetGUID().GetCounter())`
(`Group.h:312`; the guid is `ObjectGuid::Create<HighGuid::Party>(sGroupMgr->GenerateGroupId())`, `Group.cpp:150` and `:224`; `GenerateGroupId` is `NextGroupId++` starting at 1,
`GroupMgr.cpp:28,91-98`; never reused in a process, never stored, so it is one non-zero identity per live group for the session, which is what every rewritten
site needs). The same number goes back into a lookup through `GroupMgr::GetGroupByGUID(ObjectGuid::Create<HighGuid::Party>(id))` (`GroupMgr.cpp:124`: `GroupStore` is keyed by the counter): the shape a later `GetGroupById` helper needs.

What each site does with the id (read one by one):

| site | use of the id | persisted or sent? | done |
|---|---|---|---|
| `PatchwerkSafety.cpp:63` | half of the key of the in-memory `openings` map (instance id, group id) | no | rewritten |
| `BotLfgService.cpp:301,508,721,869,952,1003,1017,1054` | `BotLfgGroup::groupId`, stored when the group is formed (508), compared with live groups, AND looked up in the Commander binding table (`:728` `GroupBindingKey(group.groupId)`) | not sent, but the id meets the binding table's id | **reverted** (first rewritten, then reverted by the review) |
| `HireProvisionService.cpp:1485` | compare of the bot's group with `boundRaid.groupId`, which `BoundRaidOf` copies from the binding (written by `group->GetId()` at `Commander.cpp` 2054, 2690, 14792) | not sent, but the other operand is the binding's id | **reverted** (two id domains on ForeverCore: a bot in the bound raid would read as outside and its seat hire would be cancelled) |
| `Commander.cpp` (31 sites: 921, 965, 1167, 1174, 1640, 1882, 2054, 2244, 2281, 2576, 2611, 2631, 2661, 2671, 2690, 5269, 5275, 12968, 14754, 14792, 16447, 16538, 17002, 17133, 17184, 17197, 17266, 17282, 17297, 17344, 17365, 17386) | `Binding::groupId` (`RemoteCommanderPolicy.h:63`) and the maps keyed by it. The binding's id is handed on as `ref.groupId` (`Commander.cpp:16435`) to `CombatMonitorService`, which prints it as `"group":<id>` and as the fight id `<group>-<n>` in the combat-monitor JSON (`CombatMonitorPolicy.h:378,383,1262-1264`) | **sent** (dashboard JSON) | left |
| `ObservedRaids.cpp:174` | candidate-group set, then `info.groupId = groupId` in the raid telemetry `GroupInfo` | **sent** (telemetry) | left |
| `ObservabilityEmitter.cpp:463` | `snap.groupId`, written into the snapshot | **sent** | left |

Audit of every `GroupIdW2` call left (one): `PatchwerkSafety.cpp:64` `KeyFor`. All four uses of the `openings` map (`:68`, `:104`, `:229` and the type at `:63`) build their key by `KeyFor(...)`, so
both halves of every comparison and every key come from `GroupIdW2`; `tools/test_group_id_identity_wiring.py` pins that.

For the Commander port: BotLfgService (8 sites above) and `HireProvisionService.cpp:1485` must be switched TOGETHER with the binding's writers (`binding.groupId = group->GetId()` at `Commander.cpp` 2054, 2690, 14792)
and every reader of `Binding::groupId`, all to `GroupIdW2`, or the lookups miss on ForeverCore (a miss reads as "not bound"). The decision for the lead there: whether the JSON `group` field and the fight id may carry the counter
(an opaque id of one session) or the emit sites need a separate number. `tools/test_group_id_identity_wiring.py` pins the current state: the seat check and the LFG service keep `GetId()`, the binding writers keep `group->GetId()`,
and a file that uses `GroupIdW2` never touches the binding table.

## Family 4: spawn record `position` (33 census places, 68 field reads, 4 files)

Helpers (one per component; each has an overload for `CreatureData` and `GameObjectData` on Turtle, one for `SpawnData` on ForeverCore): `SpawnXW2`, `SpawnYW2`, `SpawnZW2`, `SpawnOW2`, and `SpawnMapIdW2` (the fifth component:
the sites read `position.mapId` in the same expressions, so it would have left the rewritten lines half done).
Turtle side: `data.position.x` (`Creature.h:286`, `GameObject.h:614`, `WorldLocation` `SharedDefines.h:1901`). ForeverCore side: `data.spawnPoint.GetPositionX/Y/Z`, `GetOrientation` (`Maps/SpawnData.h:136`) and `data.mapId` (`SpawnData.h:121`).
`data.position.x` -> `SpawnXW2(data)`, `p->position.x` -> `SpawnXW2(*p)`: one call, the receiver evaluated once.

Places: `TravelMgr.cpp:2137,2609` (2 lines, 10 reads), `RpgSubActions.cpp:96` (1 read), `WorldBuffTravelActions.cpp` (15 lines, 32 reads), `Commander.cpp` (16 lines, 25 reads): 68 reads in all. Left: none of the 33. The `x`/`y`/`z` errors on `Position` in `BattleGroundTactics.cpp` (the bot's own point constants, 82 places) are another thing (`Position` fields of the module's own static data, a battleground file) and stay.

## Wiring tests re-pinned (same expression, new spelling; none weakened)

| test | old pin | new pin |
|---|---|---|
| `tools/test_boss_pair_wiring.py` | `if (!creature && !map->IsLoaded(data->position.x, data->position.y))` | `if (!creature && !map->IsLoaded(SpawnXW2(*data), SpawnYW2(*data)))` (same following lines) |
| `tools/test_pack_clear_gate_wiring.py` | `data->position.x` | `SpawnXW2(*data)` |

New test: `tools/test_group_id_identity_wiring.py` (5 tests; the bound-raid comparison, the binding writers, the LFG service and `GroupIdW2` users never share an id source with the binding table).

127 python wiring tests that name one of the changed files were run: these two were the only failures, both fixed as above; every other one passes unchanged. No test pinned `sWorld.getConfig(` or a `GetId()` of the rewritten sites.

## Gates

| gate | command | result |
|---|---|---|
| whole-tree census, before | see Numbers | `files 649  compile clean 354  with errors 295  error places 6505 (in 315 files)  includes of a header ForeverCore does not have 40` |
| whole-tree census, after | `FOREVER_SLOT=2 forever/tools/vm-census.sh "" 10` | `files 649  compile clean 356  with errors 293  error places 6004 (in 312 files)  includes of a header ForeverCore does not have 40` (after the review; 5995 before it) |
| slice 4 | `FOREVER_SLOT=2 FOREVER_CENSUS_LIST=forever/SLICE4.txt forever/tools/vm-census.sh "" 10` | `files 174  compile clean 174  with errors 0  error places 0` |
| slice 3 | same with SLICE3.txt | `files 170  compile clean 170  with errors 0  error places 0` |
| slice 2 | same with SLICE2.txt | `files 160  compile clean 160  with errors 0  error places 0` |
| slice 1 | same with SLICE1.txt | `files 133  compile clean 133  with errors 0  error places 0` |
| ForeverCore module, slice option OFF | `ssh yulon-ubuntu2 'FOREVER_SLOT=2 ~/forever-build/build.sh 12 tortoisebots'` | `CMAKE_RC=0 MAKE_RC=0` (3 s: the default module build does not compile the files of this pass; the census is the compile check for them) |
| Turtle build | `vm-build ~/wt-wide2 --wait` | `COMPILE_SECONDS=395 CMAKE_RC=0 MAKE_RC=0`, `QUEUE_RC=0`, rc=0 (module-tree-hash cdfebf8f7d8bee1a, tip 0da0254a8) |
| Turtle tests | `vm-test tb ~/wt-wide2` | `RESULT: PASS  kind=tb wt=wt-wide2 sha=0da0254a8`, rc=0, `vm_duration=1201s` |
| helper files | `git diff --stat port/forever HEAD -- forever/src/compat 'ai/cmangos-compat*'` | only `ai/cmangos-compat-w2.h` (+27) and `forever/src/compat/TbCompat_W2.h` (+401); `forever/src/slice` empty |

After the review revert the Turtle gates are PENDING: a `forever-world` container was running on the VM, so they were not started (the reverted sites are back to the original expressions, so the Turtle code is as at `0da0254a8` minus nothing new; the new test is python only). The Turtle build and test ran on tip `0da0254a8` (code of all four families). The two later commits change only `forever/src/compat/TbCompat_W2.h` (the ForeverCore-only
log call; the Turtle build never sees that file) and `forever/docs`, so the Turtle result stands for the final commit. The Turtle gates were run before the lead's note about the live test on the VM (one `forever-world`
container was up while they ran); no further Turtle job was started after it.

## Left for later (all four families)

* Family 1: `TEXTEMOTE_MOUNTSPECIAL`; the other names above (BG_*, update-field indexes, hooks, GOSSIP_*, renumbered EQUIP_ERR_*, quest/trainer names).
* Family 2: nothing.
* Family 3: 43 places (Commander 31, BotLfgService 8, HireProvisionService 1, ObservedRaids 1, ObservabilityEmitter 1, PatchwerkSafety closed); all wait for the Commander port.
* Family 4: nothing of the 33. `sObjectMgr.GetGroupById` and the rest of the spawn-record API (`creature_id[0]`, ...) are separate families.

