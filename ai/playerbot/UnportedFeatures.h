// The closed list of "not ported yet" features of slice 1 (package R6A). Core-free: ForeverCore counts the calls of the helpers
// that answer for one of these (forever/src/compat/TbCompat_Unported.h); the Turtle side (ai/cmangos-compat-unported.h) names the
// same features so a call site reads the same on both cores, and there the helpers are the original calls. One row per feature:
// id (the enumerator), name (the text of the log line, the boot line and forever/docs/UNPORTED.md), what it is.
// Add a row here, never a string at a call site.
#pragma once

#include <cstdint>

// name (the text of the log line and of the summary), what it is. Keep the order: it is the order of the summary.
#define TB_UNPORTED_FEATURES(X) \
    X(PacketHandlerTables, "packet handler tables", "the legacy opcode-to-handler tables (1.12 opcode names); ForeverCore's bot session neither sends nor receives these packets, so nothing is registered") \
    X(ItemRandomProperties, "item random properties", "the random property / suffix of an item (1.12 ItemRandomProperties.dbc); ForeverCore uses item bonus lists") \
    X(KeyringAndBankSlots, "bank and keyring slots", "the 1.12 keyring slots and bank item slots; ForeverCore has neither (keys live in bags, the bank is bank bags)") \
    X(LootObjects, "loot objects", "the loot of a creature or game object as a public member (1.12 Creature::loot); ForeverCore keeps loot per looting player") \
    X(ItemDisplayId, "item display id", "ItemPrototype::DisplayInfoID, used to find consumables that look the same; the mirror prototype has no display id") \
    X(ItemUsePacket, "item use packet", "imbue / use an item by queueing a CMSG_USE_ITEM packet with a packed guid (1.12 layout)") \
    X(ItemSlotOfPrototype, "equip slot of an item prototype", "Player::FindEquipSlot on a prototype; ForeverCore asks about an Item") \
    X(Companions, "companions", "spells with the summon critter effect (1.12 SPELL_EFFECT_SUMMON_CRITTER); ForeverCore has no such effect") \
    X(Auction, "auction (type only)", "AuctionEntry named in the declarations of the auction code (RandomBotFacade.h, ItemUsageValue.h); the slice never reaches that code and ForeverCore has AuctionPosting instead") \
    X(IncomingLoot, "incoming: loot", "loot roll, loot start, loot response packets (1.12 layouts)") \
    X(IncomingQuest, "incoming: quest", "quest accept, complete, confirm, share, details and update packets (1.12 layouts)") \
    X(IncomingTaxi, "incoming: taxi", "activate taxi and taxi node packets") \
    X(IncomingGroup, "incoming: group and party", "group invite, uninvite, set leader, party command, ready check, summon request and duel request packets") \
    X(IncomingTradeAndItems, "incoming: trade and item errors", "trade status, buy errors, item push, inventory change failure, master use item packets") \
    X(IncomingBattleground, "incoming: battleground status", "the battleground status packet") \
    X(IncomingGuildPetition, "incoming: guild and petition", "guild invite and petition signature packets") \
    X(IncomingExperience, "incoming: experience and honor", "level up, xp gain and honor gain packets") \
    X(IncomingEmotes, "incoming: emotes", "text emote and emote packets") \
    X(IncomingObjectUse, "incoming: gossip, game object and area trigger", "gossip hello, game object use and area trigger packets") \
    X(IncomingDeath, "incoming: release spirit and resurrect", "repop, reclaim corpse and resurrect request packets") \
    X(IncomingCastFailed, "incoming: cast failed", "SMSG_CAST_RESULT, the asynchronous failure of a cast (findings item 9); the bot never hears that a cast failed after it started") \
    X(IncomingSpeedChange, "incoming: forced speed change", "the six forced-speed packets and the client acks the bot answers them with (findings item 9)") \
    X(IncomingKnockBack, "incoming: knock back", "the knock-back packet and the jump the bot answers it with") \
    X(IncomingChat, "incoming: chat message", "chat messages (1.12 SMSG_MESSAGECHAT layout): party and whisper commands typed to a bot") \
    X(IncomingSpellState, "incoming: spell failure and delay", "spell failure and spell delayed packets (1.12 layouts)") \
    X(JumpFall, "jump, fall and knockback", "jumping, fall damage bookkeeping and knock-back handling by 1.12 movement packets (MSG_MOVE_JUMP, SetFallInformation, forced movement acks); ForeverCore moves by spline and movement generators") \
    X(WorldAverageDiff, "world update time average", "sWorld.GetAverageDiff(): the average world update time, a host value (R4M request 5); ForeverCore keeps no such average") \
    X(Talents, "talents", "talent points and trees (1.12 TalentTab.dbc, Player::GetFreeTalentPoints, GetBonusTalentCount); ForeverCore has no equivalent accessors in the pre-included set") \
    X(SpawnGuid, "guid from spawn data", "an object guid built from a spawn record (creature data id + spawn id, 1.12 guid layout); ForeverCore keys spawns by spawn id and map, not by guid counter") \
    X(GuidPositionWire, "wire form of a GuidPosition", "writing a GuidPosition into a packet buffer (1.12 layout)") \
    X(BattlegroundQueue, "battleground queue", "battleground template id from a queue type id (BattleGroundMgr::BGTemplateId); ForeverCore's queue type id is a struct and the mapping is not ported") \
    X(ChatLinks, "chat link parsing", "extracting quest and spell ids from chat links (1.12 ChatHandler helpers ExtractKeyFromLink, ExtractSpellIdFromLink)") \
    X(Pets, "pets and guardians", "pet spell learning and autocast, pet command packets, the controlled-guardian walk (1.12 Pet, CallForAllControlledUnits)") \
    X(GameObjectUse, "game object use", "using a game object by the use packet (lightwell, party portal, ritual objects); ForeverCore uses typed packets and a different interact check") \
    X(ItemCasts, "item and totem casts", "casting an item spell with a hand-built Spell (SpellCastTargets setters, triggered flags, SetCastItem, item cooldowns); ForeverCore constructs a Spell from SpellInfo") \
    X(HealEstimate, "heal estimate (spell damage bonus)", "Player::CalculateSpellDamage and SpellHealingBonusDone on a SpellEntry; the mirror entry is not a SpellInfo") \
    X(CustomStrategyStore, "custom strategy store", "reading custom strategy lines from ai_playerbot_custom_strategy; that table does not exist in ForeverCore characters schema") \
    X(StrategyStoreTransactions, "strategy store transactions", "BeginTransaction(serial) / CommitTransaction() around saving strategies; ForeverCore transactions are objects and the ai_playerbot tables do not exist") \
    X(Skills, "skills", "skill lines ForeverCore does not name (thrown) and skill writes (SetSkill with the 1.12 argument list)") \
    X(QuestInternals, "quest internals", "the 1.12 quest template fields (ReqItemId/ReqItemCount, ReqSource*, GetQuestLevel, GetRewOrReqMoney, IsActive) and Player::RemoveQuest; ForeverCore has quest objectives and a classic level range instead") \
    X(Taxi, "taxi", "flight masters and taxi paths (Player::GetTaxi, TaxiPathEntry/TaxiNodesEntry fields, ActivateTaxiPathTo, CleanupFlagsOnTaxiPathFinished); ForeverCore's taxi model is different") \
    X(TransportBoarding, "transports", "boarding, leaving and riding transports (MoveOnTransport, MoveOffTransport, UseTransport, FollowOnTransport, AddPassenger, RemovePassenger, transport movement info); ForeverCore's passenger model and spline transform differ") \
    X(NavmeshProbe, "navmesh and path internals", "the navmesh-island unstick probe over MMapFactory / PathInfo::FindWalkPoly, and the 1.12 path type and nav flag bits (PATHFIND_*, NAV_*); ForeverCore's MMapManager and path flags differ") \
    X(CinematicCamera, "cinematic and camera", "cinematic state (watching_cinematic_entry, CinematicEnd); ForeverCore keeps a cinematic manager object") \
    X(InstanceBind, "instance bind", "instance saves and binds (DungeonPersistentState, InstancePlayerBind, GetBoundInstance) for the area trigger admission check; ForeverCore has InstanceLock") \
    X(Camp, "camp, rest bonus, water breathing", "the camp kit skill (SKILL_SURVIVAL2), rest bonus (SetRestBonus), the water breathing interval, the fishing zone of an area (AreaEntry::GetById)") \
    X(SpeedCheat, "speed cheat", "UpdateSpeed(type, forced, ratio) with a forced rate; ForeverCore recomputes speed from auras") \
    X(MovementInterrupt, "movement interrupt and heartbeat", "MovementGenerator::Interrupt and Player::SendHeartBeat; ForeverCore movement generators have no such calls") \
    X(AddonChat, "chat channels, addon bridge, chat tags", "addon messages (SendAddonMessage, BuildChatPacket with a chat tag), chat channels (ChannelMgr by team) and guild chat; ForeverCore chat is typed packets") \
    X(GuildRanks, "guild", "guild rank rights and member slots (Guild::GetRanksSize, GetMemberSlot, HasRankRight); ForeverCore has Guild::Member and rank ids") \
    X(AuraHolders, "aura holders (debug apply)", "SpellAuraHolder and CreateSpellAuraHolder for the debug aura command; ForeverCore creates auras from SpellInfo") \
    X(SpawnIdOfGuid, "spawn id of a creature", "the spawn record of a creature or game object found by the guid counter (T GetCreatureDataPair(guid.GetCounter()), GetGODataPair); ForeverCore's guid counter is not a spawn id (findings item 20)") \
    X(CreatureLevels, "creature template level", "CreatureInfo::level_max (T); ForeverCore templates scale levels per difficulty") \
    X(PoolsAndEvents, "pools and game events", "the pool and game event of a creature or game object found by its guid counter (PoolMgr::IsPartOfAPool, GameEventMgr::GetGameEventId); ForeverCore keys both by spawn id") \
    X(SummonSlots, "summon-object slots", "the four summon object slots of a player (m_ObjectSlotGuid, SPELL_EFFECT_SUMMON_OBJECT_SLOT1-4: totems, traps); ForeverCore has one slot effect and different bookkeeping") \
    X(SpellData, "spell data (icon ids, item charges)", "SpellEntry::SpellIconID and Item::GetSpellCharges(index) on the mirror spell entry; ForeverCore reads them from SpellInfo / ItemEffectEntry") \
    X(BossTactics, "boss and raid tactics", "boss-specific hold, taunt, heal, position and spawn rules (BossTacticsActions, Fankriss, Patchwerk, Viscidus, Solnius, meet-and-heal holds, boss stands, boss tactics and dungeon plan tables); the link stand-ins answer: no boss rule applies (R6B)") \
    X(TravelMgr, "travel and exploration", "TravelMgr, the travel node map, paths, travel targets and destinations, area levels, quest travel table, taxi node zones; nothing is loaded and every query answers: no route, no target (R6B)") \
    X(LootStack, "loot object stack", "ai::LootObject and LootObjectStack, the bot's list of corpses and objects to loot; the stack stays empty (R6B)") \
    X(RandomItems, "random item tables", "RandomItemMgr (stat weights, food picks), the item usage reagent and vendor tables; no table is built (R6B)") \
    X(BotTexts, "bot texts and help", "PlayerbotTextMgr and PlayerbotHelpMgr (ai_playerbot_texts, help topics); the text lookups answer the text name itself (R6B)") \
    X(TalentSpecs, "talent spec tables", "TalentSpec and ClassSpecs, the premade talent specs and their checks; no spec is read (R6B)") \
    X(PoolServices, "pool and hire services", "the random-bot pool tables (RandomBotFacade caches, RandomBotAccountRegistry, AhBotSellerAccount), HireLifecycle, ClaimLifecycle, the activity leases and the guild order store (TortoiseBots::GuildOrders); answers: not a pool bot, not hired, not claimed, no order (R6B)") \
    X(Tracing, "trace and observability services", "WhyTraceService, RunTraceService, ObservabilityEmitter, CombatMonitorService: switched off, every hook is a no-op (R6B)") \
    X(PlayerSummon, "player summon request", "PlayerConvenience::RequestSummon, the meeting-stone style summon of a bot to its master; the host's own placement (BotMgr::Think teleports a far bot next to its master) does that job (R6B)") \
    X(BotFactory, "bot factory (ammo, pets)", "PlayerbotFactory ammo and hunter pet set-up and pet training; nothing is created (R6B)") \
    X(ChatFilters, "chat filter chain", "CompositeChatFilter, the filters that rewrite a command line before the bot reads it; the line is used as it comes (R6B)") \
    X(FleeManager, "flee manager", "FleeManager and IdleMobVeto, the flee destination search; no destination is found (R6B)") \
    X(CampSpots, "camp spots table", "CampMgr, the table of camp spots (ai_playerbot_camp) and the active camp list; empty (R6B)") \
    X(MiscLegacyServices, "other legacy services", "small out-of-slice helpers the slice links against: mount spell test, cheat mask parsing, emote variants, grind instance filter, meeting stone spot search, fishing session end, area trigger run, db store load, cast cancel log (R6B)") \
    /* --- slice 2, package I2 --- */ \
    X(LootItems, "loot: items, quest items and rolls", "taking items out of a loot window (the autostore packet), the 1.12 per-slot loot view rights and personal copies, quest items in loot, loot rolls and the bot-only drop boost; lead decision D2 = money only, ForeverCore loot is read from its Loot object and only the money is taken (slice 2, I2)") \
    X(LootGather, "loot: gathering, skinning and game objects", "opening a game object (chest, herb, ore) or skinning a corpse by a cast and the loot window that follows; the bot does not gather, skin or open objects on ForeverCore yet (slice 2, I2)") \
    X(ItemUseOther, "item use except food and drink", "using an item on a target, an item, a game object or a lock, potions, scrolls, gems, quest-giver items, hearthstone, bandages, mounts: only food and drink on the bot itself go through the core's own item cast (slice 2, I2)")

namespace tbcompat
{
enum class Feature : uint16_t
{
#define TB_UNPORTED_ENUM(id, name, what) id,
    TB_UNPORTED_FEATURES(TB_UNPORTED_ENUM)
#undef TB_UNPORTED_ENUM
    Count
};

}
