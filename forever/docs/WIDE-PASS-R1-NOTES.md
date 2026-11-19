# Wide mechanical pass R1: existing helpers onto the files outside the slices

Branch `port/wide-pass` (off `port/forever`), worktree `~/wt-wide-pass`. Measure: whole tree
`forever/tools/vm-census.sh "" 10` (default slot 0 on the build VM; its tables are in `~/forever-build/census/`).

Scope: every `.cpp` outside `forever/` and outside `forever/SLICE1.txt` (133 files), `~/wt-slice2-integ/forever/SLICE2.txt`
(160) and `~/wt-slice3-integ/forever/SLICE3.txt` (170), plus the header of the same base name. The resulting 335 paths are
`forever/tools/wide-pass-exclude.txt`, and the rewrite script excludes them, the compat headers and everything under
`forever/`.

No helper was added, no stand-in, no `#if` on the core outside `forever/`, no behaviour change on Turtle (every helper used
below has an identity Turtle body, checked in `ai/cmangos-compat-r3.h` / `-r4a.h` / `-r4c.h` / `-r4p.h`), and
`git diff --stat port/forever HEAD -- forever/src ai/cmangos-compat*` is empty.

## Baseline (whole tree, `port/forever` + this pass before any edit)

```
files 649  compile clean 323  with errors 326  error places 7648 (in 349 files)  includes of a header ForeverCore does not have 40
```

The brief expected about 7,650; the 2-place difference to the recorded 7,650 is the tree's own changes since (the data
commits on `port/forever`), not this pass.

## The script

`forever/tools/rewrite_wide_pass.py` reads the census `errors.tsv` (one `file:line<TAB>message` per error place) and
applies one family at a time to the lines the family's predicate names. A rewrite never lands inside a comment or a string,
and the guid-key family rewrites *every* raw site of an eligible file (not only the flagged ones) so a file cannot end up
half keyed. `--check` prints what would change; the default writes.

    python3 forever/tools/rewrite_wide_pass.py --errors ~/forever-build/census/errors.tsv --family cstr

## Families closed

Places = replacements made; a line that carried two errors counts twice. "Turtle body" is the identity proof (the helper's
Turtle side in `ai/cmangos-compat-r3.h` or the `-r4*.h` file named).

| family | pattern | helper (Turtle body) | places | files |
|---|---|---|---|---|
| cstr | `sLog.outDetail("... %s", bot->GetName())` into a printf-style call | `CStrCompat(x)` (`char const*`, and `text.c_str()` on ForeverCore) | 177 | 25 |
| player | `sObjectMgr.GetPlayer(guid)` | `GetPlayerCompat` (`sObjectMgr.GetPlayer(guid)`) | 55 | 31 |
| unitflags | `u->HasFlag(UNIT_FIELD_FLAGS, f)` | `HasUnitFlagCompat` (`u->HasFlag(UNIT_FIELD_FLAGS, f)`) | 23 | 9 |
| playerflags | `p->HasFlag(PLAYER_FLAGS, f)` | `HasPlayerFlagCompat` (`p->HasFlag(PLAYER_FLAGS, f)`) | 20 | 8 |
| height | `map->GetHeight(x, y, z[, vmap, dist])` | `GetHeightCompat` (`map->GetHeight(...)`) | 31 | 12 |
| angle | `w->GetAngle(t)` | `GetAngleCompat` (`w->GetAngle(t)`) | 15 | 11 |
| los | `w->IsWithinLOSInMap(t, dyn)`, `w->IsWithinLOS(x, y, z, dyn)` | `IsWithinLOSInMapCompat` / `IsWithinLOSCompat` (the same members) | 26 | 17 |
| path | `p->calculate(...)`, `getPathType`, `getPath`, `getActualEndPosition`, `Length` | `PathCalculateCompat`, `PathTypeCompat`, `PathPointsCompat`, `PathActualEndCompat`, `PathLengthCompat` (`p.calculate(...)`, ...); the 5 sites that hand the uint32 to a `PathType` parameter wrap it as `PathType(...)`, as slice 1's own sites do | 133 | 9 |
| headless | `session->IsHeadless()` | `IsHeadlessCompat` (`session->IsHeadless()`) | 64 | 19 |
| transport | `session->HasNetworkTransport()` | `HasNetworkTransportCompat` (`session->HasNetworkTransport()`) | 39 | 13 |
| zoneid | `area->ZoneId` | `AreaZoneIdCompat` (`area->ZoneId`) | 89 | 13 |
| bgmap | `map->IsBattleGround()` | `MapIsBattlegroundCompat` (`map->IsBattleGround()`) | 18 | 6 |
| summon | `w->SummonCreature(entry, x, y, z, o, type, ms)` | `SummonCreatureCompat` (`w->SummonCreature(...)`) | 30 | 7 |
| questlevel | `quest->GetQuestLevel()` | `QuestLevelCompat` (`quest->GetQuestLevel()`) | 17 | 8 |
| guidkey | `x.GetRawValue()` where the number is a key, a comparison, a sort order or a log field | `GuidKeyCompat` (`x.GetRawValue()`) | 46 | 15 |
| accountid | `sObjectMgr.GetPlayerAccountIdByGUID(g)` | `AccountIdOfCharacterCompat` (the same call) | 12 | 8 |
| playername | `sObjectMgr.GetPlayerNameByGUID(g, name)` | `PlayerNameByGuidCompat` (the same call) | 8 | 7 |
| playerguid | `sObjectMgr.GetPlayerGuidByName(name)` | `PlayerGuidByNameCompat` (the same call) | 3 | 3 |
| underwater | `u->IsUnderwater()` | `IsUnderwaterCompat` (`u->IsUnderwater()`) | 6 | 6 |
| respawn | `c->GetRespawnCoord(x, y, z)` | `GetRespawnCoordCompat` (`c->GetRespawnCoord(...)`) | 2 | 1 |
| holder | `aura->GetHolder()` | `AuraHolderCompat` (`aura->GetHolder()`) | 2 | 1 |

816 replacements in 135 files; the census counts 807 error places closed (a place is one file, line and message, and
some lines carried two). The guid-key family is the one with a ForeverCore-side behaviour note: the key is a hash of
the 128 bits, which is the model slice 1 already uses (`SLICE1-R4M-NOTES.md` section 3); on Turtle the value is the raw
guid, unchanged. It was applied only to files that never turn such a number back into a guid or a player.

## What was checked by eye

For each family the dry run prints every changed line; 20 sites were read per family (and all 46 guid-key sites, because that
family changes a value's meaning on ForeverCore). The calls keep their argument order and are evaluated once; pointer
receivers keep the `*` the helper's `PathFinder&` parameter needs (`PathCalculateCompat(*pathfinder, ...)`), value receivers
do not. Two sites kept a trailing argument the helpers do not take and were left (`IsWithinLOS` with the Turtle core's
optional extra distance, four places).

## Left for the slices

Everything below still needs a helper that does not exist, an answer that differs between the cores, or a feature that is
not ported. Places are counted in the after-census on the files this pass may touch (6510 places remain there).

| family | places | why it is left | examples (file:line) |
|---|---|---|---|
| `TEXTEMOTE_*` and the other name-level identifiers | 258 | ForeverCore spells them `TEXT_EMOTE_*`; a name goes to `TbCompat_Names.h`, and this pass adds nothing to the helper files | `ai/playerbot/strategy/actions/EmoteAction.cpp:1074`, `:1118`, `:1185` |
| the DB result API (`Fetch`, `NextRow`, `GetCppString`, `unique_ptr<QueryResult>`) | 271 | ForeverCore has no `QueryResult`; the result adaptor does not exist | `runtime/GuildOrderStore.cpp`, `ai/playerbot/TravelNode.cpp` |
| `sWorld.getConfig(...)` | 75 | the names mix uint32/int32 (one helper), bool (one helper) and float (none), and several float names have no ForeverCore spelling | `ai/playerbot/RandomItemMgr.cpp:291`, `:4122` |
| `ObjectGuid::GetString()` | 61 | the Turtle text is `<type> <name> (0x...)`, `GuidTextCompat` the stream form: a text change on Turtle | `ai/playerbot/strategy/actions/AcceptInvitationAction.h:31`, `ai/playerbot/strategy/actions/GossipHelloAction.cpp:33` |
| `GetUInt32Value(PLAYER_FIELD_*)` | 54 | a field-by-field mapping; no single helper | `ai/playerbot/PlayerbotFactory.cpp:3932`, `ai/playerbot/TravelMgr.cpp:761` |
| `Group::GetId()` | 44 | ForeverCore's group id is another member and `GroupScopeGuidCompat` is not the same question | `commands/Commander.cpp:1167`, `ai/playerbot/strategy/actions/PatchwerkSafety.cpp:63` |
| threat and hostile-reference walks (`GetSource`) | 35 | needs an adaptor over `ThreatReference`; none exists | `runtime/ObservabilityEmitter.cpp` |
| spawn record `position` fields | 33 | `LocationOfCompat(record)` takes the whole record; each of these sites reads one component | `ai/playerbot/TravelMgr.cpp:2136`, `ai/playerbot/strategy/actions/RpgSubActions.cpp:95` |
| `Map::GetUnit(guid)` | 24 | no helper; the answer differs with what the map has loaded | `commands/Action.cpp:699`, `ai/playerbot/strategy/actions/TellReputationAction.cpp:18` |
| `strstr` on a `std::string` | 17 | not a call shape a helper covers | `ai/playerbot/RandomItemMgr.cpp:1008` |
| flag reads with no helper (`UNIT_NPC_FLAGS`, `ITEM_FIELD_FLAGS`, `CHRRACES_FLAGS_NOT_PLAYABLE`) | 16 | `HasUnitFlagCompat`/`HasPlayerFlagCompat` cover two field groups only | `ai/playerbot/strategy/actions/BankAction.cpp:17`, `ai/playerbot/strategy/actions/UnlockItemAction.cpp:26` |
| `escape_string` | 15 | DB API | `ai/playerbot/PlayerbotDbStore.cpp` |
| player lookup by name | 12 | `sObjectMgr.GetPlayer(name)` has no both-core helper (`GetPlayerCompat` takes a guid) | `commands/BotCommands.cpp:267`, `commands/Order.cpp:101` |
| `IsImmuneToSpell*` | 12 | the helper takes a caster argument the sites do not pass | `ai/playerbot/PlayerbotAISpells.cpp` |
| `GetVisibility` | 11 | no helper | `ai/playerbot/strategy/actions/ViscidusActions.cpp:116` |
| a printf argument that is a `std::string` on Turtle too (item `Name1`, spell `SpellName[0]`) | 10 | the Turtle-side `CStrCompat` has only the `char*` overload and the helper files may not change, so wrapping one would not compile on Turtle | `ai/playerbot/PlayerbotFactory.cpp:457`, `:1959`, `ai/playerbot/TravelNode.cpp:2627` |
| `IsWithinLOS` with the fifth distance | 4 | the helper has no such parameter | `commands/Commander.cpp:10412`, `:10446`, `:7381`, `:11373` |
| `GetQuestLevelForPlayer` | 3 | another question than `QuestLevelCompat` | — |
| `PathGenerator::getEndPosition` | 1 | no helper | `ai/playerbot/strategy/actions/GoAction.cpp:730` |
| guid keys in the files that rebuild a guid from the number | the files are untouched (the census counted 428 places in them before this pass) | `ObjectGuid(raw)`, `FindCharacter(raw)`, `GuidPosition(raw)`, `QueuePublicNote(raw)` read the number back; a hash cannot be turned back, so the file needs the real port (store the `ObjectGuid`) | `commands/Commander.cpp:10206`, `ai/playerbot/strategy/actions/BossTacticsActions.cpp:170`, `ai/playerbot/PlayerbotDbStore.cpp:24` |
| feature areas (auction, battleground, guild rank, taxi, quest internals, pets) | the rest | the feature is not ported | see `UNPORTED.md` |

## Where the places went

Largest drops (`where.tsv`, before -> after): `commands/Commander.cpp` 163, `ai/playerbot/strategy/actions/BossTacticsActions.cpp` 48,
`ai/playerbot/strategy/actions/ReviveFromCorpseAction.cpp` 41, `runtime/BotManager.cpp` 37, `ai/playerbot/TravelMgr.cpp` 26,
`ChooseTravelTargetAction.cpp` 19, `ViscidusActions.cpp` 18, `ReleaseSpiritAction.h` 15, `PlayerbotFactory.cpp` 15,
`runtime/ObservabilityEmitter.cpp` 14. Eleven files compile clean now that had errors before (DruidTriggers.h,
MageActions.cpp, MageTriggers.h, PaladinTriggers.h, LineTargetValue.cpp, DebugEngineCommands.cpp, HireAction.cpp,
ListQuestsActions.cpp, TellLosAction.cpp, ThaddiusDungeonActions.cpp, ThaddiusDungeonTriggers.cpp); no file gained a place.

## Numbers and gates

| Gate | Command | Result |
|---|---|---|
| Census, baseline | `forever/tools/vm-census.sh "" 10` | `files 649 compile clean 323 with errors 326 error places 7648 (in 349 files)` |
| Census, after | same | `files 649 compile clean 332 with errors 317 error places 6841 (in 338 files)` — 807 places closed, no file gained one, 11 files clean |
| Census, slice 1 | `FOREVER_CENSUS_LIST=forever/SLICE1.txt forever/tools/vm-census.sh "" 10` | see the gate at the end of the file |
| Helper files unchanged | `git diff --stat 37434278c HEAD -- forever/src ai/cmangos-compat*` (37434278c = the merge base with `port/forever` when this branch started) | empty |
| ForeverCore module, slice option off | `ssh yulon-ubuntu2 '~/forever-build/build.sh 12 tortoisebots'` | see the gate at the end of the file |
| Turtle build / tests | `vm-build ~/wt-wide-pass --wait`, `vm-test tb ~/wt-wide-pass` | see the gate at the end of the file |

## Wiring tests

37 test files under `tools/` (and one fixture include) were re-pinned, each to the new spelling of the SAME expression (no assertion deleted,
weakened or turned into a looser match; two count assertions kept their exact expected number with the new call shape):

| test | what the pin now reads |
|---|---|
| `test_airborne_nukes_wiring` | `IsWithinLOSInMapCompat(bot, target, true)` |
| `test_add_crew_wiring`, `test_avoid_cast_wiring`, `test_victim_escape_wiring`, `test_jindo_r4_shade_wiring`, `test_hire_bound_raid_wiring` | `GuidKeyCompat(...)` where the row key was built from `GetRawValue()` |
| `test_approach_walkback_wiring` | `PathLengthCompat(path)`, `PathPointsCompat(path)` |
| `test_bot_area_trigger_wiring`, `test_bot_lfg_advert_wiring`, `test_player_in_party_wiring`, `test_system_run_wiring`, `test_lft_party_accept_wiring` | `HasNetworkTransportCompat(<session>)` |
| `test_cast_cancel_wiring` | `target ? CStrCompat(target->GetName())` |
| `test_guard_assigned_tank_wiring` | `!HasUnitFlagCompat(attacker, UNIT_FLAG_NOT_SELECTABLE)` (still counted twice) |
| `test_hire_bound_raid_wiring` | `!IsHeadlessCompat(master->GetSession())` |
| `test_island_escape_wiring` | `GetHeightCompat(map, ...)`, `PathTypeCompat(path)`, `PathLengthCompat(path)`, `PathCalculateCompat(probe, ...)` |
| `test_lead_floor_surface_wiring` | `GetHeightCompat(surfaceMap, ...)` |
| `test_magma_guard_wiring` | `PathCalculateCompat(path, px, py, ground, false)`, `PathTypeCompat(path)`, `PathActualEndCompat(path)`, the count of `PathCalculateCompat(path,` is still 1 |
| `test_move_fail_note_wiring` | `static_cast<uint32>(PathTypeCompat(*active))` |
| `test_player_in_party_wiring` | `p.ghost = HasPlayerFlagCompat(member, PLAYER_FLAGS_GHOST)` |
| `test_player_led_wiring` | `PathShortEnough(noPath, PathLengthCompat(path), ...)` |
| `test_raid_boss_logging` | `if (IsHeadlessCompat(session))` |
| `test_run_leader_wiring` | `CStrCompat(commander->GetName())`, `PathCalculateCompat(...)` (4 sites), `PathTypeCompat(path)`, `PathLengthCompat(path)` |
| `test_run_release_hold_wiring` | `if (HasPlayerFlagCompat(bot, PLAYER_FLAGS_GHOST))` |
| `test_scout_command_wiring` | the three `PathCalculateCompat(<path>, origin, ...)` calls are still counted as three |
| `test_seek_meet_preplace_wiring`, `test_seek_meet_wiring`, `test_seek_take_src_wiring` | `CStrCompat(<name>)` in the row |
| `test_seek_walkout_wiring`, `test_summon_point_wiring` | `GetHeightCompat(<map>, ...)` |
| `test_straggler_walk_wiring` | `PathCalculateCompat(path, ...)`, `PathPointsCompat(...)`, `PathTypeCompat(*active)`, `PathLengthCompat(*active)` |
| `test_tank_post_wiring` | `GetPlayerCompat(`, `GetHeightCompat(map, ...)` |
| `test_time_sinks_wiring` | `PathTypeCompat(path) & PATHFIND_NOPATH` |
| `test_viscidus_burst`, `test_viscidus_cycle`, `test_viscidus_reform` | their `g++` harness now defines the two doubles the extracted production method calls (`GuidKeyCompat`, `IsWithinLOSInMapCompat`/`HasUnitFlagCompat`), like the core doubles it already had |
| `tools/hire_deferred_dismissal_fixture.inc` | the `Session` double gained `IsHeadlessCompat`, the helper the extracted `HireLifecycle` methods call |

The suite runs as `bash tools/verify_all.sh ../tortoise-wow-f3` (the `../tortoise-wow` next to this worktree is another
checkout without the host seams, so the host-contract section needs the explicit path); the engine tests are the same
`vm-test tb` gate.

`port/forever` moved while this branch was open (slice 2 merged, `dce9f4f31`, 57 commits). Of the 170 files this pass
changed, exactly one is also touched by those 57 (`MaintenanceValues.h`, and with the same `GetAngleCompat` change), so a
merge is clean; the helper-file gate is quoted against the merge base because against the moved `port/forever` the diff
shows slice 2's own helper changes reversed, not this pass.
