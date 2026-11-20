// The Turtle side of the "not ported yet" list of slice 1 (package R6A). Same helper names and signatures as
// forever/src/compat/TbCompat_Unported.h; here every helper IS the call the bot always made (identity), so the Turtle build
// behaves exactly as before. Each helper is the legacy expression or block moved here word for word.
// Included from ai/cmangos-compat-shim.h after the R4 files.
#pragma once

#include "playerbot/UnportedFeatures.h"

// Headers of the core features the moved bodies below use that ai/botpch.h includes later (or only the legacy files do).
#include "GameEventMgr.h"
#include "Chat/Channel.h"
#include "Chat/ChannelMgr.h"
#include "Maps/Pool/PoolManager.h"
#include "Battlegrounds/BattleGroundMgr.h"


// ===============================================================================================================
// ITEMS (PlayerbotAIItems.cpp, ChatHelper.cpp, ItemVisitors.h, UseItemAction.h, ItemUsageValue.h)
// ===============================================================================================================

// A run of slot numbers for `for (int i : XxxSlotsCompat())`. Same type on both cores; only the numbers differ.
struct SlotRangeCompat
{
    int first;
    int last;   // one past the end
    struct Iter
    {
        int value;
        int operator*() const { return value; }
        Iter& operator++() { ++value; return *this; }
        bool operator!=(Iter const& other) const { return value != other.value; }
    };
    Iter begin() const { return { first }; }
    Iter end() const { return { last }; }
};


// Keyring and bank item slot ranges: the slot constants of the Turtle core.
inline SlotRangeCompat KeyringSlotsCompat() { return { KEYRING_SLOT_START, KEYRING_SLOT_END }; }
inline SlotRangeCompat BankItemSlotsCompat() { return { BANK_SLOT_ITEM_START, BANK_SLOT_ITEM_END }; }

// The loot of a creature or game object: the public member.
inline Loot* LootOfCreatureCompat(Creature* creature) { return &creature->loot; }
inline Loot* LootOfGameObjectCompat(GameObject* object) { return &object->loot; }

inline bool ItemProtoHasDisplayIdCompat(ItemPrototype const* proto, uint32 displayId) { return proto->DisplayInfoID == displayId; }

// PlayerbotAI::ImbueItem's packet, moved word for word (the CMANGOS branch: the MANGOS branch was never compiled by this build).
inline void QueueUseItemPacketCompat(Player* bot, uint8 bagIndex, uint8 slot, uint8 spell_index, uint16 targetFlag, ObjectGuid const& targetGUID)
{
    std::unique_ptr<WorldPacket> packet(new WorldPacket(CMSG_USE_ITEM, 20));
    *packet << bagIndex;
    *packet << slot;
    *packet << spell_index;
    *packet << targetFlag;
    if (targetFlag & (TARGET_FLAG_UNIT | TARGET_FLAG_ITEM | TARGET_FLAG_GAMEOBJECT))
        *packet << targetGUID.WriteAsPacked();
    bot->GetSession()->QueuePacket(packet.release());
}

inline bool ItemProtoFitsSlotCompat(Player* bot, ItemPrototype const* proto, uint32 slot)
{
    uint8 eslot = bot->FindEquipSlot(proto, NULL_SLOT, true);
    return slot == eslot;
}

inline bool ItemRandomPropertySuffixCompat(int32 randomPropertyId, int loc_idx, std::string& suffix)
{
    ItemRandomPropertiesEntry const* item_rand = sItemRandomPropertiesStore.LookupEntry(abs(randomPropertyId));
    if (!item_rand)
        return false;
    if (loc_idx < 0)
        loc_idx = 0;
    suffix = item_rand->nameSuffix[loc_idx];
    return true;
}

inline bool SpellSummonsCritterCompat(SpellEntry const* spellInfo)
{
    for (int i = 0; i < 3; i++)
    {
        if (spellInfo->Effect[i] == SPELL_EFFECT_SUMMON_CRITTER)
            return true;
    }
    return false;
}


inline int32 ItemRandomPropertyIdCompat(Item* item) { return item->GetItemRandomPropertyId(); }
inline int32 ItemRandomPropertyIdCompat(LootItem const& item) { return item.randomPropertyId; }
inline int32 NewItemRandomPropertyIdCompat(uint32 itemId) { return Item::GenerateItemRandomPropertyId(itemId); }


// ===============================================================================================================
// PACKET HANDLER TABLES
// ===============================================================================================================
constexpr bool OpcodesAgreeCompat(uint32 policyValue, uint32 coreOpcode) { return policyValue == coreOpcode; }

inline bool PackedGuidReadCompat(tbcompat::Feature, WorldPacket& packet, ObjectGuid& guid)
{
    packet >> guid.ReadAsPacked();
    return true;
}

template <class Helper, class Spec>
inline void RegisterPacketHandlersCompat(Helper& helper, Spec const* specs, size_t count)
{
    for (size_t i = 0; i < count; ++i)
        helper.AddHandler(specs[i].opcode, specs[i].handler, specs[i].delay);
}

// Runs a block of the legacy code that ForeverCore cannot compile or run yet. T: the block. F (TbCompat_Unported.h): counts the
// feature and runs nothing; the block is a generic lambda, so what it does with its `auto` arguments is not looked at there.
template <class Block, class... Args>
inline void UnportedBlockCompat(tbcompat::Feature, Block&& block, Args&&... args) { block(std::forward<Args>(args)...); }
template <class Result, class Block, class... Args>
inline Result UnportedBlockResultCompat(tbcompat::Feature, Result, Block&& block, Args&&... args) { return block(std::forward<Args>(args)...); }
inline void RegisterDirectPacketCasesCompat() {}

inline uint32 WorldAverageDiffCompat() { return sWorld.GetAverageDiff(); }
inline void SetFallInformationCompat(Player* player, float z) { player->SetFallInformation(z); }


// ===============================================================================================================
// TALENTS, GUID FROM SPAWN DATA, BATTLEGROUND QUEUE, CHAT LINKS
// ===============================================================================================================
inline uint32 TotalTalentPointsCompat(Player* bot)
{
    uint32 pts = bot->GetLevel() < 10 ? 0 : bot->GetLevel() - 9;
    pts += bot->GetBonusTalentCount();
    return uint32(pts * sWorld.getConfig(CONFIG_FLOAT_RATE_TALENT));
}
inline uint32 FreeTalentPointsCompat(Player* bot) { return bot->GetFreeTalentPoints(); }
// Whether the premade talent specs of the config are read and checked (R9). Always on this core; on ForeverCore they are not ported (one summary line).
inline bool PremadeSpecsPortedCompat() { return true; }
template <class Tab>
inline int TalentTabPageCompat(Tab const* tab) { return tab->tabpage; }

template <class DataPair>
inline ObjectGuid GuidOfCreatureDataCompat(DataPair const* dataPair) { return ObjectGuid(HIGHGUID_UNIT, dataPair->second.creature_id[0], dataPair->first); }
template <class DataPair>
inline ObjectGuid GuidOfGameObjectDataCompat(DataPair const* dataPair) { return ObjectGuid(HIGHGUID_GAMEOBJECT, dataPair->second.id, dataPair->first); }

template <class Pos, class GuidPos>
inline void GuidPositionWriteCompat(ByteBuffer& b, GuidPos& guidP)
{
    b << (ObjectGuid)guidP;
    b << (Pos)guidP;
}



// ===============================================================================================================
// PROPER both-core helpers added by R6A
// ===============================================================================================================
inline uint32 AmmoItemIdCompat(Player const* player) { return const_cast<Player*>(player)->GetUInt32Value(PLAYER_AMMO_ID); }

// The wand blockers: the unit flags and Unit::IsSpellProhibited, as GenericTriggers.cpp read them.
inline bool UnitSilencedCompat(Unit* unit) { return (unit->GetUInt32Value(UNIT_FIELD_FLAGS) & UNIT_FLAG_SILENCED) != 0; }
inline bool UnitPacifiedCompat(Unit* unit) { return (unit->GetUInt32Value(UNIT_FIELD_FLAGS) & UNIT_FLAG_PACIFIED) != 0; }
inline bool SpellProhibitedCompat(Player* player, SpellEntry const* spellInfo) { return player->IsSpellProhibited(spellInfo); }

// ===============================================================================================================
// PETS, GAME OBJECTS, ITEM CASTS, MISC FEATURES
// ===============================================================================================================
// (Pets: PetLearnSpellCompat, PetToggleAutocastCompat, PetDismissCommandCompat, ForAllGuardiansCompat moved to cmangos-compat-p11.h in slice 11.)

inline bool UseGameObjectCompat(Player* bot, GameObject* go)
{
    WorldPacket data(CMSG_GAMEOBJ_USE);
    data << go->getObjectGuid();
    bot->GetSession()->HandleGameObjectUseOpcode(data);
    return true;
}

template <class AI>
inline bool HasThrownSkillCompat(AI* ai) { return ai->HasSkill(SKILL_THROWN); }

inline uint32 CreatureSchoolImmuneMaskCompat(CreatureInfo const* info) { return info->school_immune_mask; }
inline uint32 CreatureMechanicImmuneMaskCompat(CreatureInfo const* info) { return info->mechanic_immune_mask; }

inline bool PlayerHiddenGmCompat(Player* player) { return player->GetVisibility() == VISIBILITY_OFF; }

inline int32 EstimateHealOfEffectCompat(Player* bot, Unit* target, SpellEntry const* info, uint32 effect)
{
    int32 base = bot->CalculateSpellDamage(target, info, SpellEffectIndex(effect), nullptr);
    if (base <= 0)
        return 0;
    return bot->SpellHealingBonusDone(target, info, SpellEffectIndex(effect), base, HEAL);
}

// CastItemTargetAction::Execute's cast loop, moved word for word. SpellType is ai::BotUseItemSpell (declared after this header).
template <class SpellType>
inline bool CastItemOnTargetCompat(Player* bot, Unit* target, Item* item, ItemPrototype const* proto)
{
    SpellCastTargets targets;
    if (target)
    {
        targets.setUnitTarget(target);
        targets.setDestination(target->getPositionX(), target->getPositionY(), target->getPositionZ());
    }
    else
        targets.m_targetMask = TARGET_FLAG_SELF;

    // use triggered flag only for items with many spell casts and for not first cast
    int count = 0;

    for (int i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
    {
        _Spell const& spellData = proto->Spells[i];

        // no spell
        if (!spellData.SpellId)
            continue;

        // wrong triggering type
        if (spellData.SpellTrigger != ITEM_SPELLTRIGGER_ON_USE && spellData.SpellTrigger != ITEM_SPELLTRIGGER_ON_NO_DELAY_USE)
            continue;

        SpellEntry const* spellInfo = GetSpellEntryCompat(spellData.SpellId);
        if (!spellInfo)
        {
            continue;
        }

        if (spellInfo->Targets & TARGET_FLAG_DEST_LOCATION)
            targets.m_targetMask = TARGET_FLAG_DEST_LOCATION;

        SpellType* spell = new SpellType(bot, spellInfo, (count > 0) ? TRIGGERED_OLD_TRIGGERED : TRIGGERED_NONE);

        Item* tItem = nullptr;

        if (item)
        {
            spell->SetCastItem(item);
        }

        bool result = (spell->ForceSpellStart(&targets) == SPELL_CAST_OK);

        if (!result)
            return false;

        // NOTE: no manual cooldown on the item-cheat path (see UseAction::
        // UseItemInternal). The core applies spell + category cooldowns in
        // Spell::SendSpellCooldown after validation; pre-applying them here
        // made the deferred SpellEvent re-validation see our own cooldown
        // and cancel the just-started cast.

        ++count;
    }

    return count;
}
inline void AddSpellAndCategoryCooldownsCompat(Player* bot, SpellEntry const* spellInfo, uint32 itemId) { bot->AddSpellAndCategoryCooldowns(spellInfo, itemId); }
using ItemCastSpellBaseCompat = Spell;

inline bool DbStoreBeginCompat(uint32 serialId) { CharacterDatabase.BeginTransaction(serialId); return true; }
inline void DbStoreCommitCompat() { CharacterDatabase.CommitTransaction(); }
inline void LoadCustomStrategyLinesCompat(std::string const& qualifier, uint32 owner, std::list<std::string>& lines)
{
    std::string safeQualifier = qualifier;
    CharacterDatabase.escape_string(safeQualifier);
    auto results = CharacterDatabase.PQuery("SELECT action_line FROM ai_playerbot_custom_strategy WHERE name = '%s' and owner = '%u' order by idx",
            safeQualifier.c_str(), owner);
    std::unique_ptr<QueryResult> results_guard(results);
    if (results)
    {
        do
        {
            Field* fields = results->Fetch();
            std::string action = fields[0].GetString();
            lines.push_back(action);
        } while (results->NextRow());
    }
}

// Database reads with printf formats: the core's own PQuery (printf formats on this core). The raw result is returned as the
// call returned it; QueryGuardCompat is the unique_ptr the call sites wrapped it in.
template <class... Args>
inline auto CharacterPQueryCompat(char const* format, Args... args) { return CharacterDatabase.PQuery(format, args...); }
inline std::unique_ptr<QueryResult> QueryGuardCompat(QueryResult* result) { return std::unique_ptr<QueryResult>(result); }
inline void EscapeStringCompat(std::string& text) { CharacterDatabase.escape_string(text); }


// ===============================================================================================================
// QUEST INTERNALS
// ===============================================================================================================
struct QuestTriageViewCompat
{
    struct Item { uint32 id = 0; uint32 count = 0; };
    bool inactive = false;
    bool hasItemObjective = false;
    bool deliver = false;
    uint32 level = 0;
    uint32 levelForPlayer = 0;
    uint32 type = 0;
    uint32 suggestedPlayers = 0;
    int32 rewardOrRequiredMoney = 0;
    std::vector<Item> items;
};
// The reads QuestLogNearlyFullTrigger::HasDroppableQuest made, in the same terms (all pure getters).
inline QuestTriageViewCompat QuestTriageViewOfCompat(Player* bot, Quest const* quest)
{
    QuestTriageViewCompat view;
    view.inactive = !quest->IsActive();
    bool hasItemObjective = false;
    for (uint8 i = 0; i < QUEST_ITEM_OBJECTIVES_COUNT; ++i)
        if (quest->ReqItemId[i] && quest->ReqItemCount[i])
            hasItemObjective = true;
    for (uint8 i = 0; i < QUEST_SOURCE_ITEM_IDS_COUNT; ++i)
        if (quest->ReqSourceId[i] && quest->ReqSourceCount[i])
            hasItemObjective = true;
    view.hasItemObjective = hasItemObjective;
    view.deliver = quest->HasSpecialFlag(QUEST_SPECIAL_FLAG_DELIVER);
    view.level = quest->GetQuestLevel();
    view.levelForPlayer = bot->GetQuestLevelForPlayer(quest);
    view.type = quest->GetType();
    view.suggestedPlayers = quest->GetSuggestedPlayers();
    view.rewardOrRequiredMoney = quest->GetRewOrReqMoney();
    for (uint8 i = 0; i < QUEST_ITEM_OBJECTIVES_COUNT; ++i)
        if (quest->ReqItemCount[i])
            view.items.push_back({ quest->ReqItemId[i], quest->ReqItemCount[i] });
    return view;
}

inline std::string QuestTitleCompat(Quest const* quest) { return quest->GetTitle(); }
inline void RemoveQuestFromLogCompat(Player* bot, uint32 questId) { bot->RemoveQuest(questId); }
inline std::vector<std::pair<uint32, uint32>> QuestRequiredItemsCompat(Quest const* quest)
{
    std::vector<std::pair<uint32, uint32>> items;
    for (uint8 i = 0; i < std::size(quest->ReqItemId); ++i)
        items.push_back({ quest->ReqItemId[i], quest->ReqItemCount[i] });
    return items;
}


// ===============================================================================================================
// TAXI
// ===============================================================================================================
// TaxiRouteGuard.h's BotInstantFlightFinish after the destination node is known, moved word for word.
inline bool TaxiInstantFinishCompat(Player* bot, uint32 destNode)
{
    TaxiNodesEntry const* dest = sObjectMgr.GetTaxiNodeEntry(destNode);
    if (!dest)
        return false;
    bot->GetTaxi().ClearTaxiDestinations();
    bot->CleanupFlagsOnTaxiPathFinished();
    bot->GetMotionMaster()->MovementExpired();
    return bot->TeleportTo(dest->map_id, dest->x, dest->y, dest->z, bot->GetOrientation());
}


// ===============================================================================================================
// TRANSPORTS, GAME OBJECT PORTALS, NAVMESH PROBE
// ===============================================================================================================
template <class Init>
inline void SplineSetTransportCompat(Init& init, GenericTransport* transport) { init.SetTransport(transport->GetGUIDLow()); }
inline void SplinePointToTransportCompat(GenericTransport* transport, float& x, float& y, float& z) { transport->CalculatePassengerOffset(x, y, z); }

inline bool PortalSpellOfGameObjectCompat(uint32 entry, uint32& spellId)
{
    GameObjectInfo const* goInfo = sGOStorage.LookupEntry<GameObjectInfo>(entry);
    if (!goInfo || goInfo->type != GAMEOBJECT_TYPE_SPELLCASTER)
        return false;
    spellId = goInfo->spellcaster.spellId;
    return true;
}
inline bool UsePortalObjectCompat(Player* bot, GameObject* go)
{
    if (!bot->GetGameObjectIfCanInteractWith(go->getObjectGuid(), MAX_GAMEOBJECT_TYPE))
        return false;
    std::unique_ptr<WorldPacket> packet(new WorldPacket(CMSG_GAMEOBJ_USE));
    *packet << go->getObjectGuid();
    bot->GetSession()->QueuePacket(packet.release());
    return true;
}

inline bool TaxiDestinationCompat(Player* player, uint32& mapId, float& x, float& y, float& z)
{
    const Taxi::Map tMap(player->GetTaxi().GetTaxiPath());
    if (tMap.empty())
        return false;
    auto tEnd = tMap.back();
    if (!tEnd)
        return false;
    mapId = tEnd->mapId;
    x = tEnd->x;
    y = tEnd->y;
    z = tEnd->z;
    return true;
}

// The position a player reads on the zone map (T `Map2ZoneCoordinates`; ForeverCore: the UI map position, forever/src/compat/TbCompat_Unported.cpp).
inline void Map2ZoneCoordinatesCompat(float& x, float& y, uint32 zone) { Map2ZoneCoordinates(x, y, zone); }

inline dtNavMeshQuery const* NavMeshQueryCompat(uint32 mapId, uint32 /*instanceId*/ = 0)
{
    MMAP::MMapManager* const mmap = MMAP::MMapFactory::createOrGetMMapManager();
    if (!mmap)
        return nullptr;
    return mmap->GetNavMeshQuery(mapId);
}
inline dtNavMeshQuery const* NavMeshProbeQueryCompat(uint32 mapId) { return NavMeshQueryCompat(mapId); }
template <class... Args>
inline dtPolyRef NavMeshFindWalkPolyCompat(Args&&... args) { return PathInfo::FindWalkPoly(std::forward<Args>(args)...); }


// ===============================================================================================================
// PLAYERBOTAI.CPP
// ===============================================================================================================
inline void EndCinematicIfWatchingCompat(Player* bot)
{
    if (bot->watching_cinematic_entry)
        bot->CinematicEnd();
}

inline ObjectGuid LootGuidCompat(Player* bot) { return bot->GetLootGuid(); }
// The loot-window release check of PlayerbotAI::UpdateAIInternal (inside `if (lootObject)`), moved word for word.
template <class GuidPos>
inline void ReleaseLootIfFarCompat(Player* bot, GuidPos& lootObject, bool shouldRelease)
{
    if (Loot* loot = sLootMgr.GetLoot(bot, bot->GetLootGuid()))
    {
        if (lootObject.IsUnit())
        {
            Unit* unitTarget = (Unit*)loot->GetLootTarget();
            Loot const* utLoot = (unitTarget && unitTarget->GetTypeId() == TYPEID_UNIT) ? &static_cast<Creature const*>(unitTarget)->loot : nullptr;
            if (utLoot && utLoot->GetLootingPlayers().count(bot->getObjectGuid()) == 0)
            {
                // You shouldn't release if you aren't looting already
                shouldRelease = false;
            }
        }
        else if (lootObject.IsGameObject())
        {
            GameObject* gameobjectTarget = (GameObject*)loot->GetLootTarget();
            Loot const* goLoot = gameobjectTarget ? &gameobjectTarget->loot : nullptr;
            if (goLoot && !goLoot->GetLootingPlayers().count(bot->getObjectGuid()))
            {
                // You shouldn't release if you aren't looting already
                shouldRelease = false;
            }
        }
        else
        {
            // If it's not a unit or GO, what looting would this be?
            // safe to assume you probably shouldn't release
            shouldRelease = false;
        }
        // release loot if moving and far from object.
        if (shouldRelease)
        {
            bot->GetSession()->DoLootRelease(bot->GetLootGuid());
        }
    }
}

inline void BroadcastFacingCompat(Player* bot)
{
    WorldPacket data(MSG_MOVE_SET_FACING);
    data << bot->GetPackGUID();
    data << bot->m_movementInfo;
    bot->GetMover()->SendMessageToSetExcept(&data, bot);
}

// PlayerbotAI::CanEnterArea's random-bot branch, moved word for word (Area is the AreaTrigger).
template <class Area>
inline bool CanEnterAreaCompat(Player* bot, Area const* area)
{
    DungeonPersistentState* state = bot->GetBoundInstanceSaveForSelfOrGroup(area->mapid);
    Map* map = sMapMgr.FindMap(area->mapid, state ? state->GetInstanceId() : 0);
    const MapEntry* mapEntry = sMapStore.LookupEntry(area->mapid);

    // check if this account try to abuse reseting instance
    if (mapEntry->IsNonRaidDungeon())
    {
        // Tortoise performs the instance admission check in the normal
        // teleport/login path; no separate bot-only check is needed here.
    }

    // Map's state check
    if (map && map->IsDungeon())
    {
        // cannot enter if the instance is full (player cap), GMs don't count, must not check when teleporting around the same map
        if (bot->GetMapId() != area->mapid)
        {
            if (((DungeonMap*)map)->GetPlayersCountExceptGMs() >= ((DungeonMap*)map)->GetMaxPlayers())
            {
                return false;
            }

            // In Combat check
            if (map && map->GetInstanceData() && map->GetInstanceData()->IsEncounterInProgress())
            {
                return false;
            }

            // Bind Checks
            InstancePlayerBind* pBind = bot->GetBoundInstance(area->mapid);
            if (pBind && pBind->perm && pBind->state != state)
            {
                return false;
            }

            if (pBind && pBind->perm && pBind->state != map->GetPersistentState())
            {
                return false;
            }
        }
    }

    return true;
}

inline void DismountCompat(Player* bot)
{
    bot->RemoveSpellsCausingAura(SPELL_AURA_MOUNTED);
    bot->Unmount();

    bot->UpdateSpeed(MOVE_RUN, true);
    bot->UpdateSpeed(MOVE_RUN, false);
}

inline void EnsureLockpickSkillCompat(Player* bot, uint32 lockpickMax)
{
    if (bot->GetSkillValuePure(SKILL_LOCKPICKING) < lockpickMax)
        bot->SetSkill(SKILL_LOCKPICKING, lockpickMax, lockpickMax);
}
inline void EnsureCampSurvivalSkillCompat(Player* bot, uint32 cap)
{
    if (bot->GetSkillValuePure(SKILL_SURVIVAL2) < cap)
        bot->SetSkill(SKILL_SURVIVAL2, cap, cap);
}
inline void SetRestBonusCompat(Player* bot, float bonus) { bot->SetRestBonus(bonus); }
inline void ClearWaterBreathingCompat(Player* bot) { bot->SetWaterBreathingIntervalMultiplier(0); }
inline uint32 FishingZoneOfAreaCompat(uint32 areaId)
{
    AreaTableEntry const* fishArea = AreaEntry::GetById(areaId);
    return (fishArea && fishArea->ZoneId) ? fishArea->ZoneId : 0;
}
inline void SpeedCheatCompat(Player* bot)
{
    bot->UpdateSpeed(MOVE_WALK, true, 10);
    bot->UpdateSpeed(MOVE_RUN, true, 10);
    bot->UpdateSpeed(MOVE_SWIM, true, 10);
}

inline bool SessionHasInstantLogoutCompat(WorldSession* session) { return session->GetSecurity() >= (AccountTypes)sWorld.getConfig(CONFIG_UINT32_INSTANT_LOGOUT); }

template <class P>
inline void RemoveOnTransportFlagCompat(P* bot) { bot->m_movementInfo.RemoveMovementFlag(MOVEFLAG_ONTRANSPORT); }
inline void InterruptCurrentMovementCompat(Player* bot)
{
    if (!bot->GetMotionMaster()->empty())
        if (MovementGenerator* movgen = bot->GetMotionMaster()->top())
            movgen->Interrupt(*bot);
}
inline void SendHeartBeatCompat(Player* bot) { bot->SendHeartBeat(); }

inline void ClearStaleTransportStateCompat(Player* bot)
{
    if (!bot->GetTransport() && bot->m_movementInfo.HasMovementFlag(MOVEFLAG_ONTRANSPORT))
    {
        bot->m_movementInfo.RemoveMovementFlag(MOVEFLAG_ONTRANSPORT);
        bot->m_movementInfo.ClearTransportData();
    }
}

// PlayerbotAI::StopMoving's body after the early returns, moved word for word.
inline void StopMovingCompat(Player* bot)
{
    if (bot->GetTransport())
        bot->m_movementInfo.SetMovementFlags(MOVEFLAG_ONTRANSPORT);
    else
        bot->m_movementInfo.SetMovementFlags(MOVEFLAG_NONE);

    bot->StopMoving();
    MovementInfo mInfo = bot->m_movementInfo;
    float x, y, z;
    bot->GetPosition(x, y, z);
    float o = bot->GetOrientation();
    mInfo.ChangePosition(x, y, z, o);
    WorldPacket data(MSG_MOVE_STOP);
    data << mInfo;
    bot->GetSession()->HandleMovementOpcodes(data);

    if (bot->GetMotionMaster()->GetCurrentMovementGeneratorType())
    {
        bot->GetMotionMaster()->Clear(false, true);
        bot->GetMotionMaster()->MoveIdle();
    }
}

inline void SendAddonMessageCompat(Player& to, char const* prefix, std::string const& message) { to.SendAddonMessage(prefix, message); }
template <class Facade>
inline void SendAddonChatCompat(Facade& facade, Player* from, Player* to, std::string const& response)
{
    WorldPacket data;
    ChatHandler::BuildChatPacket(data, CHAT_MSG_ADDON, response.c_str(), LANG_ADDON,
            CHAT_TAG_NONE, from->getObjectGuid(), from->GetName());
    facade.SendPacket(to, data);
}

inline bool PartyPortalSpellOfObjectCompat(GameObject* go, uint32& spellId)
{
    GameObjectInfo const* goInfo = go->GetGOInfo();
    if (!goInfo || goInfo->type != GAMEOBJECT_TYPE_SPELLCASTER || !goInfo->spellcaster.partyOnly)
        return false;
    spellId = goInfo->spellcaster.spellId;
    return true;
}
inline bool GameObjectNoInteractCompat(GameObject* go) { return go->HasFlag(GAMEOBJECT_FLAGS, GO_FLAG_NO_INTERACT); }
inline bool GameObjectCanInteractCompat(Player* bot, GameObject* go) { return bot->GetGameObjectIfCanInteractWith(go->getObjectGuid(), MAX_GAMEOBJECT_TYPE) != nullptr; }
inline void SendGameObjectUseCompat(Player* bot, GameObject* go)
{
    std::unique_ptr<WorldPacket> packet(new WorldPacket(CMSG_GAMEOBJ_USE));
    *packet << go->getObjectGuid();
    bot->GetSession()->QueuePacket(packet.release());
}

inline bool TaxiSourceDestinationCompat(Player* player, uint32& src, uint32& dst)
{
    src = player->GetTaxi().GetTaxiSource();
    dst = player->GetTaxi().GetTaxiDestination();
    return true;
}
inline bool TaxiNodePositionCompat(uint32 node, uint32& mapId, float& x, float& y, float& z)
{
    TaxiNodesEntry const* entry = sObjectMgr.GetTaxiNodeEntry(node);
    if (!entry)
        return false;
    mapId = entry->map_id;
    x = entry->x;
    y = entry->y;
    z = entry->z;
    return true;
}
inline Creature* FlightMasterIfCanInteractCompat(Player* bot, ObjectGuid const& guid) { return bot->GetNPCIfCanInteractWith(guid, UNIT_NPC_FLAG_FLIGHTMASTER); }

inline void PlayTextEmoteCompat(Player* bot, uint32 emote, uint32 variant, ObjectGuid const& target)
{
    WorldPacket data(SMSG_TEXT_EMOTE);
    data << (TextEmotes)emote;
    data << variant;
    data << target;
    bot->GetSession()->HandleTextEmoteOpcode(data);
}

inline Unit* UnitInMapCompat(Map* map, ObjectGuid const& guid) { return map->GetUnit(guid); }
inline Creature* AnyTypeCreatureInMapCompat(Map* map, ObjectGuid const& guid) { return map->GetAnyTypeCreature(guid); }
inline WorldObject* WorldObjectInMapCompat(Map* map, ObjectGuid const& guid) { return map->GetWorldObject(guid); }

template <class GuildType>
inline bool GuildRanksOfBotCompat(GuildType* guild, ObjectGuid const& botGuid, uint32& maxRank, uint32& botRank)
{
    maxRank = guild->GetRanksSize();
    MemberSlot* botMember = guild->GetMemberSlot(botGuid);
    botRank = botMember->RankId;
    return true;
}

inline WorldObject* PlayerViewObjectCompat(Player* player) { return player->GetCamera().GetBody(); }


// ===============================================================================================================
// WORLDPOSITION.CPP, GUIDPOSITION.CPP
// ===============================================================================================================
inline GameObjectDataPair const* GameObjectDataPairOfCounterCompat(uint64 counter) { return sObjectMgr.GetGODataPair(counter); }
inline CreatureDataPair const* CreatureDataPairOfCounterCompat(uint64 counter) { return sObjectMgr.GetCreatureDataPair(counter); }
inline uint32 CreatureMaxLevelCompat(CreatureInfo const* info) { return info->level_max; }
template <class PathFinderType>
inline void ExcludeSteepSlopesCompat(PathFinderType& pathfinder) { pathfinder.ExcludeSteepSlopes(); }

constexpr bool PathTypeBitsMatchCoreCompat(uint32 normal, uint32 shortcut, uint32 incomplete, uint32 noPath, uint32 notUsingPath, uint32 destForced, uint32 flyPath, uint32 underwater)
{
    return normal == PATHFIND_NORMAL && shortcut == PATHFIND_SHORTCUT && incomplete == PATHFIND_INCOMPLETE && noPath == PATHFIND_NOPATH &&
        notUsingPath == PATHFIND_NOT_USING_PATH && destForced == PATHFIND_DEST_FORCED && flyPath == PATHFIND_FLYPATH && underwater == PATHFIND_UNDERWATER;
}
constexpr bool NavFlagsMatchCoreCompat(uint32 ground, uint32 magma, uint32 slime, uint32 water, uint32 steep)
{
    return ground == NAV_GROUND && magma == NAV_MAGMA && slime == NAV_SLIME && water == NAV_WATER && steep == NAV_STEEP_SLOPES;
}
inline uint16 NavExcludeFlagsCompat() { return NAV_MAGMA | NAV_SLIME | NAV_WATER | NAV_STEEP_SLOPES; }


inline uint16 PoolOfSpawnCompat(bool creature, uint64 counter)
{
    return creature ? sPoolMgr.IsPartOfAPool<Creature>(counter) : sPoolMgr.IsPartOfAPool<GameObject>(counter);
}
// GuidPosition::GetGameEventId: the pool's top pool first, then the creature or game object itself.
inline int16 GameEventOfSpawnCompat(uint16 poolId, bool creature, bool gameObject, uint64 counter)
{
    if (poolId)
    {
        uint16 top_pool_id = sPoolMgr.IsPartOfTopPool<Pool>(poolId);

        if (int16 event_id = sGameEventMgr.GetGameEventId<Pool>(top_pool_id))
            return event_id;
    }

    if (creature)
        return sGameEventMgr.GetGameEventId<Creature>(counter);
    if (gameObject)
        return sGameEventMgr.GetGameEventId<GameObject>(counter);

    return 0;
}
inline bool GameEventActiveCompat(int16 eventId) { return sGameEventMgr.IsActiveEvent(eventId); }


inline bool GuildSayCompat(Player* bot, Guild* guild, std::string const& msg)
{
    if (!guild->HasRankRight(bot->GetRank(), GR_RIGHT_GCHATSPEAK))
    {
        return false;
    }

    guild->BroadcastToGuild(bot->GetSession(), msg.c_str(), LANG_UNIVERSAL);
    return true;
}
inline bool SayToChannelCompat(Player* bot, std::string const& name, std::string const& msg)
{
    ChannelMgr* mgr = channelMgr(bot->GetTeam());
    Channel* channel = mgr ? mgr->GetOrCreateChannel(name) : nullptr;
    if (!channel) return false;
    ObjectGuid guid = bot->GetObjectGuid();
    channel->Join(guid, "", false);
    channel->AsyncSay(guid, msg.c_str(), LANG_UNIVERSAL);
    return true;
}
template <class Facade>
inline void SendChatPacketCompat(Facade& facade, Player* from, Player* to, uint32 chatType, std::string const& text, uint32 language)
{
    WorldPacket data;
    ChatHandler::BuildChatPacket(data, static_cast<ChatMsg>(chatType), text.c_str(), static_cast<Language>(language), CHAT_TAG_NONE, from->getObjectGuid(), from->GetName());
    facade.SendPacket(to, data);
}


// ===============================================================================================================
// SPELLS
// ===============================================================================================================
inline void ReleaseForeignSummonObjectsCompat(Player* bot, SpellEntry const* pSpellInfo)
{
    for (uint32 j = 0; j < MAX_EFFECT_INDEX; ++j)
    {
        uint8 slot = 0;
        switch (pSpellInfo->Effect[j])
        {
        case SPELL_EFFECT_SUMMON_OBJECT_SLOT1: slot = 0; break;
        case SPELL_EFFECT_SUMMON_OBJECT_SLOT2: slot = 1; break;
        case SPELL_EFFECT_SUMMON_OBJECT_SLOT3: slot = 2; break;
        case SPELL_EFFECT_SUMMON_OBJECT_SLOT4: slot = 3; break;
        default: continue;
        }

        if (ObjectGuid guid = bot->m_ObjectSlotGuid[slot])
        {
            if (GameObject* obj = bot ? bot->GetMap()->GetGameObject(guid) : nullptr)
            {
                //Object is not mine because I created an object with same guid on different map.
                //Make object mine, remove it from my list and give it back to the original owner.
                if (obj->GetOwnerGuid() != bot->getObjectGuid())
                {
                    ObjectGuid ownerGuid = obj->GetOwnerGuid();
                    obj->SetOwnerGuid(bot->getObjectGuid());
                    obj->SetLootState(GO_JUST_DEACTIVATED);

                    bot->RemoveGameObject(obj, false);
                    bot->m_ObjectSlotGuid[slot].Clear();

                    obj->SetOwnerGuid(ownerGuid);
                }
            }
        }
    }
}
inline bool SpellIconIdIsCompat(SpellEntry const* spell, uint32 icon) { return spell->SpellIconID == icon; }
inline int32 ItemSpellChargesCompat(Item const* item, uint8 index) { return item->GetSpellCharges(index); }

// @@END@@
