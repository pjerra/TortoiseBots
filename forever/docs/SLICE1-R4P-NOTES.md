# Slice 1, round R4P: the PlayerbotAI class (7 .cpp files, `PlayerbotAI.h`)

List: `forever/SLICE1-R4P.txt` (PlayerbotAI.cpp, PlayerbotAIActivity/Chat/Items/Packets/Quests/Spells.cpp, 11,500 lines).
Compat pair: `forever/src/compat/TbCompat_P.h` (ForeverCore) and `ai/cmangos-compat-r4p.h` (Turtle, same names and signatures,
each forwarding to the call the bot always made). T = Turtle core (`~/playerbots-references/penqle-tortoise-wow-modsys/src/game`),
F = pinned ForeverCore (`~/ForeverCore/src/server/game`); every helper in the pair carries its T and F file:line in a comment.
Measure: `FOREVER_SLOT=4 FOREVER_CENSUS_LIST=forever/SLICE1-R4P.txt forever/tools/vm-census.sh "" 14`.

## Numbers

| Run | Error places in my 7 files | Total in the census (17-19 files) |
|---|---|---|
| BEFORE (`9b32de181`) | 576 (PlayerbotAI.cpp 314, Spells 106, Packets 80, Chat 34, Items 23, Activity 10, Quests 9) | 618 in 18 files |
| AFTER | 273 (PlayerbotAI.cpp 149, Packets 72, Items 18, Spells 16, Chat 13, Quests 5; Activity 0) | 315 in 17 files |

The other 42 places of the census are in headers other packages own (`TaxiRouteGuard.h` 8, `GuidPosition.h` 5, `ServerFacade.h` 5,
`UseItemAction.h` 5, `Talentspec.h` 4, `RandomBotFacade.h` 4, `ItemUsageValue.h` 4, `ItemVisitors.h` 3, `GenericActions.h` 2,
`PlayerbotAI.h` 1, `ReachTargetActions.h` 1): the R3 list, untouched. `PlayerbotAI.h` (mine): its one place is
`ChatHandler::ExtractSpellIdFromLink` (line 73, see Left open, link parsing).

Remaining 273 by family (the table "Left open" below has the reason for each): packet tables and packed guids 78 (68 of them in
Packets.cpp), jump/fall/knockback/facing/movement packets 33, mounts/camp/skills/fishing/guardians 23, keyring/bank/display id 16,
chat channels/addon/tags 15, instance bind/area trigger 13, loot 12, taxi 11, summon-object slots 10, guid-from-data/raw value 10,
party portal/game object use 8, transport 7, guild 6, emotes 5, spell data/item use 5, debug aura apply 4, server settings 4,
quest internals 4, cinematic/camera 4, link parsing 2, constructor talent points 1, two hand-built queue calls 2.

## Families closed and how (a = name, b = both-core helper, c = helper with different numbers, listed below)

| Family | Places | How |
|---|---|---|
| `GetName()` into printf/`SC_PHASE` (`const std::string` to varargs, `const char*` from string) | 44 | (b) `CStrCompat(x->GetName())` (R3 helper), call sites only |
| `ObjectGuid::GetRawValue()` span in logs and `RitualHolds().Holds` | 13 | (c) `GuidRawCompat` |
| `HasFlag(PLAYER_FLAGS / UNIT_FIELD_FLAGS, ..)`, sanctuary, XP, next-level XP, item durability, `m_movementInfo.pos.x` | 53 | (b) `HasPlayerFlagCompat`, `HasUnitFlagCompat`, `PlayerInSanctuaryCompat` (c), `PlayerNextLevelXPCompat`, `PlayerXPCompat` (R3), `ItemDurabilityCompat`, `ItemMaxDurabilityCompat`, `MovementPosCompat` |
| `IsAFK`, `IsGMVisible`, `IsTaxiCheater`, `GetCasterGuid`, `GetAuraCharges` | 8 | (a) name entries (casing/rename), each checked against bot class members by grep |
| Spell construction, `SetCastItem`, cast targets (`setDestination`, unit, game object, item) | 49 | (b)/(c) `NewSpellCompat`, `SpellSetCastItemCompat`, `SpellTargets{SetUnit,SetGO,SetItem,GetItem,SetDestination}Compat` |
| Auras: list by type, `GetAura(id, effect)`, `GetHolder()` for caster/charges/duration, `IsPositive`, `IsAreaAura` | 28 | (c) `AuraListCompat`, `AurasByTypeCompat`, `GetAuraCompat`, `AuraHolderCompat`, `AuraIsPositiveCompat`, `AuraIsAreaCompat`; `AuraHolderMapCompat` for `GetSpellAuraHolderMap` loops |
| Immunity (`IsImmuneToSpell`, `...Effect`, `...Damage`) | 7 | (c) `IsImmuneToSpellCompat`, `IsImmuneToSpellEffectCompat`, `IsImmuneToDamageCompat` |
| State of a cast, timers, power cost, range mod, DR group, world boss rank | 12 | (a) `SPELL_STATE_CASTING`, `GetCastedTime`, `DIMINISHING_*` names; (b)/(c) `SpellPowerCostCompat`, `ApplyRangeSpellModCompat`, `DiminishingGroupOfSpellCompat`, `CreatureIsWorldBossRankCompat` |
| Teleport acks, logout cancel | 7 | (b) `AckNearTeleportCompat`, `AckFarTeleportCompat`, `CancelLogoutCompat` (the way `TbBotSession::AcknowledgeTeleports` acks) |
| Chat: say, yell, whisper, party, raid, "like a player" (say, yell, party, guild), raw queue | 31 | (b)/(c) `BotSayCompat`, `BotYellCompat`, `BotWhisperCompat`, `BotGroupChatCompat`, `BotChatLikePlayerCompat`, `BotQueuePacketCompat` |
| Faction reaction (copy of the old core's reputation logic) | 15 | (c) `FactionReactionCompat`: T body moved word for word into the Turtle helper, F body is the same algorithm over F's field names |
| Players by guid/name/account, `ObjectGuid(HIGHGUID_PLAYER, n)`, `IsHeadless`, `HasNetworkTransport`, duel opponent, `ObjectGuid` as bool | 27 | (b) `PlayerAccountIdCompat`, `PlayerNameByGuidCompat`, `PlayerGuidByNameCompat`, `PlayerByGuidCompat`, `PlayerGuidCompat`, `IsHeadlessCompat`, `HasNetworkTransportCompat` (R3), `DuelOpponentGuidCompat`; `if (guid)` became `!guid.IsEmpty()` (T has it) |
| Area name/zone/capital flag, zone at a point, quest slot id, cast speed, motion stack top, bot cheats, config value, `countof`, aura name, BG queue | 22 | (b) `AreaNameOrEmptyCompat`, `AreaZoneIdCompat`, `AreaHasFlagsCompat` (R3), `ZoneIdAtCompat`, `GetQuestSlotIdCompat` (R3), `UnitCastSpeedModCompat` (c), `MotionMasterTopCompat`, `SetHealthPercentCompat`, `RemoveAllSpellCooldownCompat`, `DurabilityRepairAllCompat`, `WorldIntConfigCompat` (R3); (a) `countof`, `SPELL_AURA_OBS_MOD_MANA`, `InBattleGroundQueue`, the movement flag names `MOVEFLAG_NONE/BACKWARD/WALK_MODE/FALLINGFAR/JUMPING` |

Text-preserving rule kept: every call site differs from the old line only by the helper spelling; the Turtle helper is the old call
(arguments evaluated once). Two small structural edits, both on Turtle behaviour-neutral: `GetFactionReaction`'s body moved into the
Turtle helper; `Whisper`'s two identical branches (the same packet with `getObjectGuid()` and `GetObjectGuid()`) became one call. One
declaration changed: `WorldSession const* fromSession` is `WorldSession*` (`PlayerbotAI.cpp:3124`) so the helper takes the pointer the
session is stored in.

## Different answers (the live test must know)

* `GuidRawCompat`: F gives the low 64-bit word of the 128-bit guid (counter plus spawn-group bits, `ObjectGuid.h:325-346`), T the packed
  64-bit value. Fine for a log line and for a player key; it carries no entry for a creature.
* `PlayerInSanctuaryCompat`: T player flag `PLAYER_FLAGS_SANCTUARY` (0x10000), set by the area; F reads the unit's sanctuary pvp flag
  (`Unit.h:902`), because 0x10000 means low-level raid on F. Used by the idle-scan shortcut only.
* Spell targets: T `setUnitTarget` also copies the unit's position into the destination (`Spell.cpp:95-106`), F `SetUnitTarget` does not;
  T `setGOTarget` leaves the target mask bit off (`Spell.cpp:124-129`), F sets `TARGET_FLAG_GAMEOBJECT`; T dereferences a null game
  object, F ignores it; `setDestination` has no orientation (0 on F).
* Casting state: T `SPELL_STATE_CASTING` is only a channel in progress (`Spell.cpp:3996-3999`), the cast bar is `PREPARING`; the entry maps
  it to F `SPELL_STATE_CHANNELING`, the same meaning. `GetCastedTime` is T `m_timer`, F `GetTimer()` (the same field).
* Auras: the bot's `Aura*` is F's whole `Aura` (T: one effect inside a holder). `AurasByTypeCompat` lists each effect's base aura (two effects
  of one type list the aura twice, as T lists two Auras) and returns a copy (T a reference). `AuraIsPositiveCompat` is the spell's
  classification on F, the applied aura's flag on T. `GetAuraCompat` finds the effect, then its base.
* Immunity: F also reads the caster (its immunity-ignoring effects) and takes every effect of the spell (`MAX_EFFECT_MASK`); `castOnSelf`
  makes the target its own caster. Helpers take the caster (`bot`) as an extra last argument that the Turtle side ignores.
* `DiminishingGroupOfSpellCompat`: F's `GetDiminishingReturnsGroupForSpell()` has no "triggered" flag, so a triggered stun lands in the
  same group as the cast one. `DIMINISHING_CONTROL_STUN` is `DIMINISHING_STUN`, `DIMINISHING_KIDNEYSHOT` is `DIMINISHING_KIDNEY_SHOT`.
* `CreatureIsWorldBossRankCompat`: T rank `CREATURE_ELITE_WORLDBOSS`, F the boss type flag (`Creature::isWorldBoss`), never for a pet.
* `FactionReactionCompat`: the bot's order of tests (mask first), not F's `IsHostileTo`/`IsFriendlyTo` order.
* `IsHeadlessCompat`: F `WorldSession::IsBot()` (set by `BotSession`), T `IsHeadless()`. `HasNetworkTransportCompat` stays R3's.
* `UnitCastSpeedModCompat`: T update field `UNIT_MOD_CAST_SPEED`, F `ModCastingSpeed` (the cast-time multiplier). Both 1.0 normal, haste below.
  The GCD note (`NoteSpellGlobalCooldown`) scales 1.0-1.5 s with it; F also has `ModSpellHaste` and `ModHaste`, which this does not read.
* Chat: F `Say`/`Yell`/`Whisper` run the core's language and ignore rules (T built the packet by hand); `BotWhisperCompat` also tells the sender
  on F (the bot session drops it). The "like a player" kinds call `HandleChatMessage` at once on F, T queued the packet for the next
  session update. `BotGroupChatCompat` builds one `SMSG_CHAT` per receiver (locale of the receiver).
* Movement flags: `MOVEFLAG_JUMPING` is F `MOVEMENTFLAG_FALLING` (the client's jump/fall bit), `MOVEFLAG_FALLINGFAR` is `FALLING_FAR`
  (T 0x4000, F 0x1000), `WALK_MODE` is `WALKING`. F has no `ONTRANSPORT` flag: not defined (Left open, transport).

## Packets: the full table

The legacy code receives packets through hooks that copy them (`HandleBotOutgoingPacket`, `HandleMasterIncomingPacket`,
`HandleMasterOutgoingPacket`) and sends by building 1.12 layouts by hand. ForeverCore's bot session drops every packet it would send
(`TbBotSession.cpp` `SendPacket`), so none of the inbound handlers fires there; no byte layout was translated. Classes: (i) the slice's
follow/assist/melee path needs the ACTION, done through a helper that calls the core; (ii) not needed, Left open with the opcode name.

**Senders (the bot makes the packet): all done where the slice or the brief needs them, the rest left open**

| Sender | Where | Class | What |
|---|---|---|---|
| near-teleport ack `MSG_MOVE_TELEPORT_ACK` | PlayerbotAI.cpp:2660s | (i) | `AckNearTeleportCompat` |
| far-teleport ack | PlayerbotAI.cpp:2720s | (i) | `AckFarTeleportCompat` (`HandleMoveWorldportAck()`) |
| logout cancel (3 sites) | PlayerbotAI.cpp:468, 2815, 3205 | (i) | `CancelLogoutCompat` |
| chat: whisper, party, raid | PlayerbotAIChat.cpp | (i) | `BotWhisperCompat`, `BotGroupChatCompat` |
| chat as a player: say, yell, party, guild | PlayerbotAIChat.cpp | (i) | `BotChatLikePlayerCompat` |
| raw queue of a packet | PlayerbotAI.cpp `QueuePacket` | (i) transport only | `BotQueuePacketCompat` |
| `MSG_MOVE_SET_FACING` broadcast (+ `GetPackGUID`, `GetMover`) | PlayerbotAI.cpp:1224 | (ii), 3 places | F's `SetFacingTo` goes through `MoveFace`, a spline that the visible clients get (`Unit.cpp:14037-14044`); the extra packet may be redundant. Linking round decides |
| `MSG_MOVE_STOP`, `MSG_MOVE_FALL_LAND` + `HandleMovementOpcodes` (jump/fall simulation, `StopMoving` of the stuck unit) | PlayerbotAI.cpp:620-720, 6040s | (ii) | jump and fall landing simulation |
| `SMSG_TEXT_EMOTE` + `HandleTextEmoteOpcode` | PlayerbotAI.cpp:4280s | (ii) | emotes (TEXTEMOTE_* ids also differ) |
| `CMSG_GAMEOBJ_USE` | PlayerbotAI.cpp:3805 | (ii) | party portal use |
| item-use packet with a packed guid | Items.cpp:1063 | (ii) | `WriteAsPacked` |
| `MSG_MINIMAP_PING` | Chat.cpp:488 | (ii) | ping |
| addon message `CHAT_MSG_ADDON` + `SendAddonMessage` (3 sites) | PlayerbotAI.cpp:3100s | (ii) | the TBM addon bridge |
| pet action | Spells.cpp:1796 | (ii) | `HandlePetAction` |

**Receivers: the opcode tables and the switch (all (ii), 68 places in Packets.cpp plus 6 elsewhere)**

* `kMasterIncomingPacketHandlers` (18): `CMSG_GAMEOBJ_USE`, `CMSG_AREATRIGGER`, `CMSG_LOOT_ROLL`, `CMSG_GOSSIP_HELLO`,
  `CMSG_QUESTGIVER_HELLO`, `CMSG_QUESTGIVER_COMPLETE_QUEST`, `CMSG_QUESTGIVER_ACCEPT_QUEST`, `CMSG_QUEST_CONFIRM_ACCEPT`,
  `CMSG_ACTIVATETAXI`, `CMSG_ACTIVATETAXIEXPRESS`, `CMSG_TAXICLEARALLNODES`, `CMSG_TAXICLEARNODE`, `CMSG_GROUP_UNINVITE`,
  `CMSG_GROUP_UNINVITE_GUID`, `CMSG_PUSHQUESTTOPARTY`, `CMSG_REPOP_REQUEST`, `CMSG_RECLAIM_CORPSE`, `CMSG_USE_ITEM`. Master-side
  quest, taxi, loot, gossip and group events.
* `kBotOutgoingPacketHandlers` (35): petition, battlefield status, group invite/set leader, guild invite, buy errors, force-run speed
  ("check mount state"), resurrect request, inventory change failure (twice), trade status (+ extended), loot response, six quest
  updates, item push, party command, level up, xp gain, pvp credit, text emote, emote, loot rolls (start/vote/won), summon request, ready
  check, quest confirm/details, `SMSG_CAST_RESULT` ("cast failed"), duel requested. These opcode names do not exist on F under the
  same name (`SMSG_BATTLEFIELD_STATUS`, `SMSG_GROUP_INVITE`, `SMSG_GROUP_SET_LEADER`, `SMSG_FORCE_RUN_SPEED_CHANGE`,
  `SMSG_TRADE_STATUS_EXTENDED`, `SMSG_QUESTUPDATE_*` x5, `SMSG_LEVELUP_INFO`, `SMSG_LOG_XPGAIN`, `SMSG_LOOT_START_ROLL`,
  `MSG_RAID_READY_CHECK`, `SMSG_QUESTGIVER_QUEST_DETAILS`, `SMSG_CAST_RESULT`); F has typed classes of other names and layouts.
* `kMasterOutgoingPacketHandlers` (1): `SMSG_PARTY_COMMAND_RESULT`.
* The switch in `HandleBotOutgoingPacket`: `SMSG_SPELL_FAILURE` and `SMSG_SPELL_DELAYED` (packed guid), `SMSG_EMOTE`, `SMSG_MESSAGECHAT`
  (the command intake by chat: whispers/party from the master, enchant-order replies; `SMSG_MESSAGECHAT` is `SMSG_CHAT` on F),
  six forced-speed changes (the headless auto-ack: `HandleForceSpeedChangeAckOpcodes`), `SMSG_MOVE_KNOCK_BACK`, `SMSG_CAST_RESULT`,
  `SMSG_GROUP_INVITE`.
* The `static_assert`s on the speed-ack opcodes (12) fail because the six server and six client opcode names are not F's names.

Why none of them is (i): they carry information INTO the bot (an event happened), and F's bot session drops everything it sends, so no
event arrives on F whatever the handler does; the bot's engine reads the same facts from the core's state (aura, health, group, target).
The two receivers that could matter for the slice are named so the live test can look for them: (1) `SMSG_CAST_RESULT`: only the
CC-failure log reads it on T, and `PlayerbotAI::CastSpell` already gets the synchronous result from `prepare()`; an asynchronous cast
failure (interrupted at the end of the bar) is not seen on F; (2) the six speed changes: the headless session acked them to stop the
core's 2+2 s resend timer; F has no such timer for server-moved units, as far as read (`HandleForceSpeedChangeAck`, `WorldSession.h:1368`,
is the call if R8 shows a bot that never takes a speed change).

## The constructor and `IsOwnedBot()` (FOREVER-SLICE1-FINDINGS item 1)

Read, not changed. `PlayerbotAI::PlayerbotAI(Player* bot)` (`PlayerbotAI.cpp:208-262`), in this order:

1. `:211-212` `if (!bot->IsTaxiCheater() && HasCheat(BotCheatMask::taxi)) bot->SetTaxiCheater(true);` writes the Player (F: `m_ExtraFlags`
   `PLAYER_EXTRA_TAXICHEAT`, `Player.h:1338-1339`; `IsTaxiCheater` is now the name entry for `isTaxiCheater`). `HasCheat` reads
   `sPlayerbotAIConfig.botCheatMask`, not the master.
2. `:230` `accountId = PlayerAccountIdCompat(guid)`, `:232` the context, `:236-239` the four engines, packet handlers registered.
3. `:257-260` `if (!HasRealPlayerMaster() && !IsOwnedBot() && bot->GetFreeTalentPoints() > 0 && !AltBotService::Instance().IsAutonomous(guid))`
   then `DoSpecificAction("auto talents")`. `master` is still null here, so `HasRealPlayerMaster()` is false.
4. `:264-268` `IsOwnedBot() = bot && BotManager::Instance().IsBot(guid) && !BotManager::Instance().IsRandomBot(guid)`
   (`runtime/BotManager.cpp:2006-2019`: `BotRegistryKey(guid.GetRawValue(), key)` then `m_bots.find(key)`; `IsRandomBot` reads
   `record.random`). `AiFactory.cpp:1203` and `PlayerbotFactory.cpp:1637` read the same method later.

What a ForeverCore host must provide, and where:

* **Before `new PlayerbotAI(bot)`** (R5's `engine::Create`, `TbEngineBrain.cpp`, and in any case before the constructor runs) the host's
  answers to `BotManager::Instance().IsBot(guid)` and `IsRandomBot(guid)` must be "bot" and "not random" for that character. `BotManager`
  itself is not in the slice (its link stub is R6, `SliceLinkStubs.cpp`), so the stub is the place: `IsBot(guid)` answers true when
  `BotMgr` holds a session entry for the guid (the entry exists from `Login`, before the Player does: `BotSession::BeginLogin`), and
  `IsRandomBot` answers false. `BotRegistryKey` cannot be reused (its input, `GetRawValue()`, is a span on F): key the stub by
  `guid.GetCounter()` like `OnPlayerLogin` does (`BotManager.cpp:1221`).
* The same stub set must answer `AltBotService::Instance().IsAutonomous(guid)` false (the fourth condition at `:258`).
* `HasRealPlayerMaster()` is false before `SetMaster`; that is Turtle's order too, so ownership is the only thing the host adds.
* `bot->GetFreeTalentPoints()` (`:257`) has NO ForeverCore counterpart (talents are the trait system, `TraitMgr`; no free-point accessor
  in `Player.h`). This place is Left open. The finding's risk ("a host bot with free talent points would spend them on `.tb engine on`")
  is therefore real only after the linking round decides this accessor: until then owned-ness is the guard that keeps the action
  unreached, so the stub in the previous point must be in place first.
* A failed `Create` after step 1 leaves `PLAYER_EXTRA_TAXICHEAT` set on the Player if the cheat mask asks for it. On F the host can read
  `isTaxiCheater()` before `new PlayerbotAI` and restore it with `SetTaxiCheater(false)` when `Create` fails; the constructor does not
  roll it back.

## Left open

Every item is a place that compiles on neither core as written, not needed by follow/assist/melee, and where the F feature exists in
another shape (or not yet): rule (d), nothing stubbed. Counts are error places.

| Family | Places | File:lines | What it needs |
|---|---|---|---|
| Packet tables, opcodes and packed guids (see the table above) | 78 | Packets.cpp 68; PlayerbotAI.cpp:172-178, 3805; Chat.cpp:488; Items.cpp:1063, 1066; Quests | per-opcode typed handlers, decision per event |
| Jump/fall/knockback/facing, motion-stack interrupt, `UpdateSpeed(type, forced[, ratio])`, `SetFallInformation(z)`, `GetAverageDiff` | 33 | PlayerbotAI.cpp:620-770 (jump), 1057, 1224-1226, 1467-1472, 2660-2664, 2725, 6030-6070; Packets.cpp:483-524; Spells.cpp (1) | `MovementGenerator::Interrupt` has no F counterpart; F has no world-update average (`GetAverageDiff`, 2 places: the reaction cast delay and the knockback landing time; needs a host value) |
| Mounts, camp (skills, rest bonus, fishing zone), guardians, ammo, water breathing | 23 | PlayerbotAI.cpp:1467-1472, 1717-1905, 798-812, 772 | mount/skill/rest APIs of F (`GetPureSkillValue`, 4-argument `SetSkill`); `GenerateItemRandomPropertyId` has no F counterpart (3) |
| Keyring and bank item slots, item display id | 16 | Items.cpp:82-342, 825, 844; PlayerbotAI.cpp:4944 | F has no keyring slots and a different bank layout (`BANK_SLOT_BAG_START` 63 = the bank tab + bags); `DisplayInfoID` is left out of the item mirror (SHIM-R2 D3) |
| Chat channels, addon messages, chat tags | 15 | Chat.cpp:32-42, 165-170, 401, 488; PlayerbotAI.cpp:3100-3132 | channel lookup by name (`ChannelMgr::ForTeam`, `GetCustomChannel`, system channels need the zone), `Channel` header, the TBM addon bridge |
| Instance bind and area trigger entry | 13 | PlayerbotAI.cpp:1416-1451 | `AreaTrigger::mapid`, `DungeonMap`, instance lock API |
| Loot | 12 | PlayerbotAI.cpp:485-528; Items.cpp:273-274 | `Player::GetLootGuid`, `Loot::GetLootTarget/GetLootingPlayers`, `Creature::loot` |
| Taxi | 11 | PlayerbotAI.cpp:3888-4032 | `Player::GetTaxi`, `TaxiNodesEntry` fields (`map_id`, `x`, `y`, `z`), `GetNPCIfCanInteractWith` takes a third argument |
| Summon-object slots | 10 | Spells.cpp:1362-1383 | `SPELL_EFFECT_SUMMON_OBJECT_SLOT2-4` do not exist on F, `m_ObjectSlotGuid` is `m_ObjectSlot`, `SetOwnerGuid` is `SetOwnerGUID` |
| Guid from a spawn data pair or a raw value, map lookups by guid | 10 | PlayerbotAI.cpp:3380, 4519-4596 | a guid factory (R3 left it to R4C: creature/object guids carry the map and the spawn id on F), `Map::GetUnit` and friends |
| Party portal and game object use | 8 | PlayerbotAI.cpp:3692-3837 | `GameObjectTemplate::spellcaster`, go flags, `GetGameObjectIfCanInteractWith`, the searcher constructor |
| Transport | 7 | PlayerbotAI.cpp:874-917, 2742-2745 | `WorldPosition::isOnTransport(GenericTransport*)` (R4C), `AddPassenger`, no `ONTRANSPORT` flag |
| Guild | 6 | PlayerbotAI.cpp:4764-4766; Chat.cpp:149-154 | `Guild::GetRanksSize`, `MemberSlot`, rank rights, `BroadcastToGuild(session, officerOnly, ..)` |
| Emotes | 5 | PlayerbotAI.cpp:4190-4286 | `TEXTEMOTE_*` ids, `CTextEmote` packet |
| Spell data and item use | 5 | Spells.cpp:1796, 1903, 2093-2094, 2213 | `SpellIconID` is not in the mirror (F has file data ids, not the 1.12 numbers), `mechanic_immune_mask` (F immunity data), `Item::GetSpellCharges(effect)`, `HandlePetAction` |
| Debug aura apply and hostile search | 4 | PlayerbotAI.cpp:5887-5919 | `CreateSpellAuraHolder`, `Unit::AddSpellAuraHolder`, the searcher constructor (R3's `UnitListSearcherCompat` is the way in, not used by this site) |
| Server settings | 4 | PlayerbotAI.cpp:2605, 2611 | `CONFIG_UINT32_INSTANT_LOGOUT` has no F setting |
| Quest internals | 4 | Quests.cpp:26, 28, 168-172 | the quest-requirements view over `Quest::GetObjectives()`, `RemoveQuest` |
| Cinematic and camera | 4 | PlayerbotAI.cpp:408-409, 4823 | `CinematicMgr`, `Player::GetCamera` |
| Chat link parsing | 2 | PlayerbotAI.cpp:140; PlayerbotAI.h:73 | F's `extractKeyFromLink` takes `char*` not `char**`; `ExtractSpellIdFromLink` has no F counterpart |
| Constructor talent points | 1 | PlayerbotAI.cpp:257 | no free-talent-point accessor on F (see the constructor section) |

## NEEDS FROM

* R4C: a creature/object guid factory (`GuidPosition.h:13,17,18,20`, R3 list) is also needed at `PlayerbotAI.cpp:3380, 4519, 4574`;
  `WorldPosition::isOnTransport(GenericTransport*)` for the 4 transport places at `PlayerbotAI.cpp:874-906`.
* Host/linking (R5/R6): the `BotManager::IsBot/IsRandomBot` and `AltBotService::IsAutonomous` answers before construction (previous
  section); a world update-time average for `GetAverageDiff`.

## OFFERED (defined in this pair, usable by the other packages; the lead dedupes)

`GuidRawCompat`, `HasPlayerFlagCompat`, `HasUnitFlagCompat`, `PlayerInSanctuaryCompat`, `PlayerNextLevelXPCompat`, `ItemDurabilityCompat`,
`ItemMaxDurabilityCompat`, `MovementPosCompat`, `DuelOpponentGuidCompat`, the aura set (`AuraListCompat`, `AurasByTypeCompat`,
`GetAuraCompat`, `AuraHolderCompat`, `AuraIsPositiveCompat`, `AuraIsAreaCompat`, `AuraHolderMapCompat`: R3's `GenericTriggers.h`
`GetSpellAuraHolderMap` places, `Value.h` and the aura users of R4A/R4V), the spell set (`NewSpellCompat`, `SpellSetCastItemCompat`,
`SpellTargets*Compat`, `SpellPowerCostCompat`, `ApplyRangeSpellModCompat`, `IsImmuneTo*Compat`, `DiminishingGroupOfSpellCompat`; R3's
`UseItemAction.h` `Spell` constructor places), the name entries `IsAFK`, `IsGMVisible`, `IsTaxiCheater`, `GetCasterGuid`, `GetAuraCharges`,
`SPELL_STATE_CASTING`, `GetCastedTime`, `DIMINISHING_CONTROL_STUN`, `DIMINISHING_KIDNEYSHOT`, `SPELL_AURA_OBS_MOD_MANA`, `countof`,
`InBattleGroundQueue`, `MOVEFLAG_NONE/BACKWARD/WALK_MODE/FALLINGFAR/JUMPING`; `PlayerAccountIdCompat`, `PlayerNameByGuidCompat`,
`PlayerGuidByNameCompat`, `PlayerByGuidCompat`, `PlayerGuidCompat`, `IsHeadlessCompat`, `FindPlayerNotInWorldCompat`,
`CreatureIsWorldBossRankCompat`, `UnitCastSpeedModCompat`, `MotionMasterTopCompat`, `AreaNameOrEmptyCompat`, `AreaZoneIdCompat`,
`ZoneIdAtCompat`, the chat set (`Bot*Compat`), `AckNearTeleportCompat`, `AckFarTeleportCompat`, `CancelLogoutCompat`.

## Gates

Code commit `7be9c0c99` (the final commit adds only the six wiring-test pins named below and these gate lines).

| Gate | Command | Result |
|---|---|---|
| Slice census (list R4P) | `FOREVER_SLOT=4 FOREVER_CENSUS_LIST=forever/SLICE1-R4P.txt forever/tools/vm-census.sh "" 14` | before `error places 618 (in 18 files)`, mine 576; after `error places 315 (in 17 files)`, mine 273; `TbCompat_P.h` 0 |
| ForeverCore module, slice option off | `ssh yulon-ubuntu2 'FOREVER_SLOT=4 ~/forever-build/build.sh 12 tortoisebots'` | `CMAKE_RC=0 MAKE_RC=0` |
| Turtle build | `vm-build ~/wt-slice-r4p --wait` at `7be9c0c99` | `COMPILE_SECONDS=2955 CMAKE_RC=0 MAKE_RC=0`, `QUEUE_RC=0`, job `1-1794992839461421143-2073500` (59 module files; the wrapper kept waiting after the job was done, the result was read with `--result`) |
| Turtle unit tests | `vm-test tb ~/wt-slice-r4p` | first run at `7be9c0c99`: `RESULT: FAIL`, 9 wiring tests pinned the old spelling of lines this round rewrote; after the pins below, at `dd120bafa`: `RESULT: PASS`, rc=0, `vm_duration=1378s` |

Wiring pins changed to the new spelling of the SAME expression (each still proves what it proved): `test_aura_tactics_wiring`
(`new Spell(` -> `NewSpellCompat(`: the dispel gate still comes before the spell is made), `test_speed_change_ack_wiring`
(`!bot->GetSession()->IsHeadless()` -> `!IsHeadlessCompat(bot->GetSession())`: the headless guard still comes before the ack),
`test_target_facing_wiring` (`new Spell(bot, spellInfo, false)` -> `NewSpellCompat(...)`, `target->HasInArc(bot)` -> `HasInArcCompat(target, bot,
M_PI_F)`: the facing pre-check still sits before the probe and reads the default arc), `test_teleport_landing_wiring`
(`HandleMoveTeleportAckOpcode(p)` -> `AckNearTeleportCompat(bot)`, twice: the ack still comes before the landing logic),
`test_hire_never_leaves_wiring` (`ObjectAccessor::FindPlayerNotInWorld(hirerGuid)` -> `FindPlayerNotInWorldCompat(hirerGuid)`),
`test_stun_interrupt_wiring` (`IsImmuneToSpell`, `IsImmuneToSpellEffect`, `GetAurasByType(...).empty()`, `info->rank == CREATURE_ELITE_WORLDBOSS`
-> their `...Compat` helpers, with the caster argument `bot`).
The `action_loop` "Mage stay FAILED x9+" line in the decision-trail report is in the R3 PASS run too (not a test failure).

## Unsure

* The Turtle side of the helpers compiled only in the VM `vm-build` (below); the F side is proven by the census (no error in `TbCompat_P.h`).
* `IsHeadless` is not in the Turtle core source under `~/playerbots-references` (only the repository's own callers use it), so its constness
  on T is unknown: the helper takes a non-const `WorldSession*`.
* The `FactionReactionCompat` F body was written from the F field names, not run against data.
* Aura `IsPositive` and `GetAuraCompat` (effect lookup) change what some triggers see on F; the live test in R8 should watch the warrior's
  `rend`/`battle shout` "has aura" checks first.
