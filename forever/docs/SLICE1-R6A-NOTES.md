# ForeverCore port, slice 1, package R6A: the last 460 error places ("not ported yet")

Branch `slice1/integ` (worktree `~/wt-slice-integ`), build slot: the default one. T = Turtle core, F = pinned ForeverCore.
Census (`FOREVER_CENSUS_LIST=forever/SLICE1.txt forever/tools/vm-census.sh "" 14`): **460 places in 27 files -> 0** (`files 127  compile clean 127  with errors 0  error places 0`).

## 1. The mechanism (as built)

* `ai/playerbot/UnportedFeatures.h` (core-free): the closed table, one X-row per feature (enumerator, name, what it is): 58 rows. Deviation from the
  brief, on purpose: the call is `tbcompat::Unported(tbcompat::Feature::X)` (an enumerator, so a name outside the table does not compile) instead of a
  string, and the table lives in this core-free header so the Turtle side can name the same features at its call sites (`UnportedBlockCompat`).
* `forever/src/compat/TbCompat_Unported.h/.cpp` (F): `Unported(feature)` counts per feature with atomics and logs the FIRST call of a name once
  (`TortoiseBots: not ported yet: <name>`, TC_LOG_WARN, "server.loading"); `UnportedSummary()` returns the table with counts;
  `UnportedBootLine()` ("slice: N features not ported: a, b, c") and `UnportedReport()` (one line per feature with its count) format it for the host;
  `UnportedPacketHandler(name)` counts one packet table row under its group. The helpers of each feature follow, each with its failure value in its comment.
  The header is included by `forever-compat-shim.h` at the end of the R4 block; `TbCompat_Unported.cpp` is compiled WITHOUT the shim (it defines
  `TB_UNPORTED_TABLE_ONLY` to skip the helpers that need the core's types).
* `ai/cmangos-compat-unported.h` (T): the same helper names and signatures; every one is the original call or the original block moved word for word.
  Included by `ai/cmangos-compat-shim.h` after `cmangos-compat-r4c.h`.
* `forever/docs/UNPORTED.md`: the to-do list (feature, what, call sites, failure value, what porting needs).

Three tools were used, in this order of preference; no `#if` on the core in a legacy file, no stub outside the mechanism:
1. a both-core helper for one expression or statement (`XxxCompat`);
2. `UnportedBlockCompat(feature, [&](auto* player, ...) { legacy block }, player, ...)` / `UnportedBlockResultCompat(feature, failure, lambda, args)`:
   T calls the lambda (the block is the original code, indentation kept), F counts the feature and does not call it. The lambda is generic and
   its `auto` parameters shadow the names the block uses (`bot`, `master`, `ai`), so what the block does with them is not looked at on F: only names
   that do not depend on them (types, constants) must exist there. Used for 20 blocks (transport boarding, taxi use, jump tail, pet spell sweep, aura
   holders, chat channels, spawn-record lookups, chat links, battleground template id, party flight). Its weakness: the block is compiled on F only
   as far as the dependent names allow, so the F side of a block is proven by the census only for the non-dependent names;
3. a block moved word for word into a template helper of the T header (loot release check, area admission, summon slots, item cast loop, strategy lines).

## 2. NEEDS FROM host (R6B wires these; nothing was added to a host file)

```cpp
// 1. in the host file that logs the boot (forever/src/...): the table without the core-type helpers
#define TB_UNPORTED_TABLE_ONLY 1
#include "compat/TbCompat_Unported.h"                      // once per host translation unit that needs it
// 2. the boot line, after the slice is up
TC_LOG_INFO("server.loading", "TortoiseBots: {}", tbcompat::UnportedBootLine());
// 3. the `.tb unported` console command (commands table of the host): prints the counts
handler->PSendSysMessage("{}", tbcompat::UnportedReport());
```

Also for R6B: (a) `PlayerbotDbStore.cpp`, `runtime/`, `commands/` still hold printf-style `PQuery/PExecute` (finding 18): grep them before they join the link;
(b) `sPlayerbotDbStore.Save(ai)` is skipped on F by the store transaction gate (`DbStoreBeginCompat` returns false), so PlayerbotDbStore.cpp is not needed for the slice;
(c) the three `CharacterDatabase` users below send real queries on F.

## 3. Done properly instead of Unported (F has the thing; no counter)

| Helper | T call | F call | Where |
|---|---|---|---|
| `AmmoItemIdCompat` | `GetUInt32Value(PLAYER_AMMO_ID)` | `Player::GetAmmoId()` (Classic 1.60 ammo) | PlayerbotAI.cpp (2), GenericSpellActions.cpp, PullStrategy.cpp |
| `UnitSilencedCompat`, `UnitPacifiedCompat`, `SpellProhibitedCompat` | unit flags, `IsSpellProhibited` | silence aura, `UNIT_FLAG_PACIFIED`, `SpellHistory::IsSchoolLocked` | GenericTriggers.cpp (wand blockers) |
| `SessionHasInstantLogoutCompat` | security >= InstantLogout setting | `HasPermission(RBAC_PERM_INSTANT_LOGOUT)` (MiscHandler.cpp:251) | PlayerbotAI.cpp (2) |
| `DismountCompat` | remove mount auras, `Unmount`, `UpdateSpeed(RUN, true/false)` | `RemoveAurasByType`, `Dismount`, `UpdateSpeed(RUN)` | PlayerbotAI::Unmount |
| `StopMovingCompat` | flags, `StopMoving`, synthesized MSG_MOVE_STOP, clear + idle | `Unit::StopMoving`, `MotionClearCompat`, `MoveIdle` | PlayerbotAI::StopMoving (every follow tick) |
| `ClearStaleTransportStateCompat` | clear MOVEFLAG_ONTRANSPORT + transport data | `transport.Reset()` when no transport is set | PlayerbotAI::Reset |
| `SplineSetTransportCompat`, `SplinePointToTransportCompat`, `TransportOfCompat` (R4C) | `SetTransport(guid)`, `CalculatePassengerOffset` | no-ops: F's spline transforms world points when the unit is on a transport (MoveSplineInit.cpp:310) | MovementActions dispatch / chase |
| `PlayTextEmoteCompat` + `TEXTEMOTE_NOD/NO/OPENFIRE` | hand-built text emote packet | typed `CTextEmote`, F's emote ids, sound index -1 | PlayerbotAI::PlayEmote (every successful chat action) |
| `NavMeshQueryCompat(mapId, instanceId)`, `NavExcludeFlagsCompat` | MMapManager query by map, NAV_MAGMA/SLIME/WATER/STEEP | `GetNavMeshQuery(map, map, instance)`, `NAV_MAGMA_SLIME|NAV_WATER|NAV_GROUND_STEEP` | WorldPosition::ClosestCorrectPoint |
| `PlayerViewObjectCompat` | `GetCamera().GetBody()` | `GetViewpoint()` | PlayerbotAI::HasPlayerNearby |
| `FlightMasterIfCanInteractCompat` | `GetNPCIfCanInteractWith(guid, flag)` | same with the second flag word | PlayerbotAI party flight |
| `UnitInMapCompat`, `AnyTypeCreatureInMapCompat`, `WorldObjectInMapCompat`, `GameEventActiveCompat`, `BroadcastFacingCompat` (F: nothing, `SetFacingTo` tells the clients) | Map / manager calls | F's Map getters by guid type, `sGameEventMgr->IsActiveEvent` | PlayerbotAI.cpp, GuidPosition.cpp |
| `VisitUnitsCompat`, `VisitGameObjectsCompat` (R4V) | grid searchers | same | portal follow, hostile units |
| `SKILL_FIRST_AID = 129` (TbCompat_Names.h) | same | the line id (F's own Player.cpp:6478 names "First Aid 129") | ChatHelper.cpp, ItemVisitors.h |
| `GuidFromRawCompat` (R4C) for `ObjectGuid(uint64)` | | empty guid + one error line | the flee failure anchor (PlayerbotAI.cpp) |
| `OpcodesAgreeCompat`, `PackedGuidReadCompat`, reserved opcode enum | | see 4 | PlayerbotAIPackets.cpp |

### The printf PQuery trap (finding 18): `CharacterPQueryCompat`
F's `PQuery` takes `{}` formats and silently drops printf arguments. `CharacterPQueryCompat(format, args...)` formats with `vsnprintf` (the Turtle rules, same
text) and sends it with `CharacterDatabase.Query()`; a `static_assert` refuses a `std::string` argument (printf needs `char const*`). T: the core's own `PQuery`.
Escaping stays where it was (strings are escaped, quoted by the SQL text, with `EscapeStringCompat` = `escape_string` / `EscapeString`). Schema checked read only on the test DB:
| Call | Table in F `characters` | Sent on F? |
|---|---|---|
| `PlayerbotAIConfig.cpp:1776` `SELECT name, guid FROM characters WHERE account = '%u'` | `characters(guid, account, name)` exists | yes |
| `GenericActions.cpp:237` `SELECT id, entry, owner FROM character_pet WHERE owner = '%u' AND (slot = '%u' OR slot > '%u')` | `character_pet(id, entry, owner, slot)` exists | yes |
| `CustomStrategy.cpp:120` `SELECT action_line FROM ai_playerbot_custom_strategy ...` | no `ai_playerbot_*` table | no: feature "custom strategy store", `LoadCustomStrategyLinesCompat` loads no lines |
The strategy store transactions (`ChangeStrategyAction.cpp` x3, `BeginTransaction(serial)`/`CommitTransaction()`) are one feature ("strategy store transactions"): begin returns false on F and the saves in between are skipped.
`PlayerbotAIConfig.cpp:1776` keeps the Turtle behaviour of never freeing the raw result on T (the helper returns the raw pointer there; F's result is a shared pointer).

## 4. Packet tables (78 places)
The tables and the switch of `HandleBotOutgoingPacket` are written with 1.12 opcode names. The 41 names ForeverCore does not have are numbers of a reserved block
(`0xF000 + n`, above F's `MAX_OPCODE` 0x3FFF; `LegacyOpcodeCompat` in `TbCompat_Unported.h`), so the tables and every `case` compile and can never match a real packet
(the names F also has keep F's values: those rows are never registered either). `RegisterPacketHandlers` goes through `RegisterPacketHandlersCompat`: T registers each row; F registers
nothing and counts each row under the group of what its handler does (13 `incoming: ...` features: loot, quest, taxi, group and party, trade and item errors, battleground status,
guild and petition, experience and honor, emotes, gossip/game object/area trigger, release spirit and resurrect, **cast failed**, forced speed change). `RegisterDirectPacketCasesCompat` (called once beside the three registrations) counts the four cases handled
directly (chat message, spell failure and delay, forced speed change, knock back). The 1.12 layout reads (`ReadAsPacked`) go through `PackedGuidReadCompat` (F: false, the guid stays empty: every caller
compares it with the bot's guid, and speed change and knock back return early). `static_assert`s comparing policy opcodes with core opcodes go through `OpcodesAgreeCompat` (F: true: no wire value to compare).
"incoming: cast failed" is the one feature to watch live (findings item 9).

## 5. Sites where the failure value needed care (file:line at the final commit)
1. `PlayerbotAI.cpp:259` `FreeTalentPointsCompat(bot) > 0` is in the constructor: 0 keeps the "auto talents" branch unreached (finding 1's trap stays closed).
2. `PlayerbotAI.cpp:1034`, `PlayerbotAIPackets.cpp:538`, `MovementActions.cpp:6265` `WorldAverageDiffCompat` 0: only shortens a delay / jump time (the delay is added to a cast delay: the AI wakes a little earlier than on T).
3. `PlayerbotAI.cpp:1390` `CanEnterAreaCompat` returns **true** (the function answers "may enter"); false would forbid every dungeon entry. F's own teleport checks still apply.
4. `GenericActions.cpp:443` `PetDismissCommandCompat` false: the caller now returns false before telling "Dismissing pet" (T unchanged: always true).
5. `GenericActions.h:102` / `MovementActions.cpp:1153` / `PlayerbotAI.cpp` portal use: the helpers return bool and the caller returns that bool (was `return true` after the packet).
6. `WorldPosition.cpp:1640` `ClosestCorrectPoint`: the query is asserted non-null (T too). F gives the query by `GetNavMeshQuery(map, map, instanceId)` and `nullptr` where no mesh is loaded: same assertion as T without mmaps (`MANGOS_ASSERT`).
7. `MovementActions.cpp:2328` the island probe (`ProbePlaceUnstickIsolated`): a `nullptr` query returns false ("not isolated"), so no unstick teleport; with a non-null query and poly 0 it would answer "isolated", which is why the probe uses `NavMeshProbeQueryCompat` (Unported, always null on F) and not the proper `NavMeshQueryCompat`.
8. `ChangeStrategyAction.cpp:21,61,133`: the saves between begin and commit run only when begin is true.
9. `GuidPosition.h:17-18` data-pair constructors: the guid is empty, the position part still reads the record; no caller of these constructors is in the slice.
10. `WorldPosition.cpp:109,131` guid -> spawn record: nullptr -> `set(WorldPosition())` (the "unknown guid" answer). `PlayerbotAI.cpp:4440` `GetUnit(CreatureDataPair)` null unit.
11. `PlayerbotAI.cpp:4681` `GetMaxPreferedGuildSize`: `GuildRanksOfBotCompat` false returns the unscaled size before the division by `maxRank`.
12. `PlayerbotAIItems.cpp:82...` keyring and bank item loops: an empty range; a counter call per scan (cheap atomic).
13. `GenericSpellActions.cpp:973` heal estimate 0: a spell that heals nothing is skipped (healer path only).
14. `GenericTriggers.h:1750` `QuestLogNearlyFullTrigger::HasDroppableQuest`: **the brief names it as a combat-loop trigger; it is not**: it is the "quest log nearly full" trigger, which `forever/src/slice/SliceAiObjectContext.cpp` does not register. Done through a view (`QuestTriageViewOfCompat`: T fills it with the same pure getters, same unsigned types; F returns the empty view: level 0 = scaling = never grey).
15. `PlayerbotAI.cpp:880` the transport snap and jump-off branches: conditions only (`TransportOfCompat`), the bodies are blocks, so a count appears only when a bot is on a transport.
16. `PlayerbotAI.cpp:4195`/`PlayAttackEmote`: the raw ids 305 and 307 in the attack emote list are 1.12 ids; on F they name other emotes (not changed: needs a table).
17. `WorldPosition.cpp` `case HIGHGUID_TRANSPORT:`: no F tag; `#define HIGHGUID_TRANSPORT HighGuid::Count` (never a real high guid) keeps the label (it does not match anything on F; F's transports match `HIGHGUID_MO_TRANSPORT`, which is in the same switch).

## 6. Different answers (watch live, R8)
Text emotes use F's ids and no sound variant; `Unmount` recomputes the run speed once; `StopMoving` does not synthesize a MSG_MOVE_STOP; the facing broadcast is F's own `SetFacingTo`;
instant logout follows the RBAC permission, not a security number; wand silence is the silence aura, not a flag; a bot on a transport walks world-space points and F's spline localises them
(DispatchMovement for a bot standing on a transport assumes its input points are the same space as on T: not run); excluded nav flags merge magma and slime.

## 7. Numbers and gates
| Gate | Result |
|---|---|
| Census, slice | 460 places in 27 files -> `files 127  compile clean 127  with errors 0  error places 0 (in 0 files)` |
| ForeverCore module, slice option off: `~/forever-build/build.sh 12 tortoisebots` | `CMAKE_RC=0 MAKE_RC=0` (compiles `TbCompat_Unported.cpp`) |
| Turtle syntax check of all 23 touched or including `.cpp` files (docker g++ `-fsyntax-only` with the build dir's flags, no new build directory) | 0 errors |
| Turtle build `vm-build ~/wt-slice-integ --wait` | see the last section |
| Turtle unit tests `vm-test tb ~/wt-slice-integ` | see the last section |

## 8. What I am unsure about
* The 20 lambda blocks are proven on F only for names that do not depend on their `auto` arguments (types, constants); the rest is the original code, never compiled against F. If a later slice enables one, port it, do not trust it.
* `DismountCompat`, `StopMovingCompat`, `ClearStaleTransportStateCompat`, `PlayTextEmoteCompat` and `NavExcludeFlagsCompat` are F code written from reading the core, never run.
* `FreeTalentPointsCompat` 0 and `CanEnterAreaCompat` true are decisions, not measurements.
* Counter calls on hot paths (`WorldAverageDiffCompat` per cast, keyring/bank scans): atomic increments, but the counts will be large; the log line is once per name.
