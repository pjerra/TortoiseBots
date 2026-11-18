// The Turtle side of the "not ported yet" list of slice 1 (package R6A). Same helper names and signatures as
// forever/src/compat/TbCompat_Unported.h; here every helper IS the call the bot always made (identity), so the Turtle build
// behaves exactly as before. Each helper is the legacy expression or block moved here word for word.
// Included from ai/cmangos-compat-shim.h after the R4 files.
#pragma once

#include "playerbot/UnportedFeatures.h"


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
inline void PetLearnSpellCompat(Pet* pet, uint32 spellId) { pet->LearnSpell(spellId); }
inline void PetToggleAutocastCompat(Pet* pet, uint32 spellId, bool on) { pet->ToggleAutocast(spellId, on); }
// GenericActions.cpp: the dismiss command of the pet action, sent as a pet action packet.
inline bool PetDismissCommandCompat(Player* bot, Pet* pet)
{
    const ObjectGuid& petGuid = pet->getObjectGuid();
    const ObjectGuid& targetGuid = ObjectGuid();
    const uint8 flag = ACT_COMMAND;
    const uint32 spellId = COMMAND_DISMISS;
    const uint32 command = (flag << 24) | spellId;

    HandlePetActionCompat(bot->GetSession(), petGuid, command, targetGuid);
    return true;
}
template <class Visitor>
inline void ForAllGuardiansCompat(Player* bot, Visitor&& visitor) { bot->CallForAllControlledUnits(visitor, CONTROLLED_GUARDIANS); }

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

inline dtNavMeshQuery const* NavMeshQueryCompat(uint32 mapId)
{
    MMAP::MMapManager* const mmap = MMAP::MMapFactory::createOrGetMMapManager();
    if (!mmap)
        return nullptr;
    return mmap->GetNavMeshQuery(mapId);
}
template <class... Args>
inline dtPolyRef NavMeshFindWalkPolyCompat(Args&&... args) { return PathInfo::FindWalkPoly(std::forward<Args>(args)...); }

// @@END@@
