// The Turtle side of the both-core helpers the ForeverCore port added in slice 2, package V2 (loot values, corpses, dispel target, the loot object stack).
// Same names and signatures as forever/src/compat/TbCompat_V2.h; each forwards to the call the bot always made (or holds the legacy block
// word for word, with the policy calls it made handed in as callables: the policy headers are included after this file).
// Owned by package V2: no other package edits this file.
#pragma once

#include <sstream>
#include <string>
#include <vector>

// Guid as a qualifier (see TbCompat_V2.h): T writes the raw 64-bit guid as a decimal number and reads it back with stoull.
inline std::string GuidQualifierCompat(ObjectGuid const& guid) { return std::to_string(guid.GetRawValue()); }
inline ObjectGuid GuidFromQualifierCompat(std::string const& text) { return ObjectGuid(std::stoull(text)); }

// Whose corpse is it (LootObjectStack.cpp MayLootCorpse).
inline bool CorpseAllowedToLootCompat(Player* bot, Creature* creature) { return bot->IsAllowedToLoot(creature); }
inline Loot* CreatureLootForCompat(Creature const* creature, Player const* /*bot*/) { return const_cast<Loot*>(&creature->loot); }
inline ObjectGuid CorpseRoundRobinGuidCompat(Creature const* creature, Player const* /*bot*/) { return ObjectGuid(creature->loot.roundRobinPlayer); }
inline ObjectGuid GroupMasterLooterGuidCompat(Group const* group) { return group->GetLooterGuid(); }
template <class VisibleFn>
inline bool CorpseHasVisiblePersonalItemForCompat(Creature* creature, Player* bot, VisibleFn&& visible) { return visible(creature->loot, bot); }

// The five facts of the corpse-refusal debug text, built as LootObject::Refresh built them.
struct CorpseRefusalFactsCompat
{
    bool allowedLooter = false;
    bool hasQuestItems = false;
    bool hasItemFor = false;
    bool otherTurn = false;
    bool reserved = false;
};

template <class VisibleFn>
inline CorpseRefusalFactsCompat CorpseRefusalFactsOfCompat(Creature const* creature, Player* bot, VisibleFn&& visible)
{
    CorpseRefusalFactsCompat facts;
    Loot const& loot = creature->loot;
    facts.otherTurn = loot.roundRobinPlayer != 0 && loot.roundRobinPlayer != bot->GetObjectGuid();
    facts.reserved = loot.hasItemFor(bot) && !visible(loot, bot);
    facts.allowedLooter = loot.IsAllowedLooter(bot->GetObjectGuid(), false);
    facts.hasQuestItems = !loot.m_questItems.empty();
    facts.hasItemFor = loot.hasItemFor(bot);
    return facts;
}

// Corpse distance: the server's own loot range (Player::GetMaxLootDistance), centre to centre, 3D, same map.
inline bool CorpseInLootRangeCompat(Player* bot, Creature* creature)
{
    return creature->IsWithinDistInMap(bot, bot->GetMaxLootDistance(creature), true, SizeFactor::None);
}

// Skinning a looted corpse; looting a game object at all.
inline bool CorpseSkinnableByCompat(Player* bot, Creature* creature)
{
    return creature->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_SKINNABLE) && creature->loot.isLooted() && creature->IsSkinnableBy(bot);
}
inline bool GameObjectLootAllowedCompat(GameObject const* /*object*/) { return true; }

// The loot an object carries (ShouldLootObject), and what its absence means there.
inline Loot* LootOfObjectCompat(WorldObject* object, Player* /*bot*/)
{
    Loot* objLoot = nullptr;
    if (object->IsCreature()) objLoot = &((Creature*)object)->loot;
    else if (object->IsGameObject()) objLoot = &((GameObject*)object)->loot;
    return objLoot;
}
inline bool AbsentLootMeansNothingToTakeCompat(WorldObject* /*object*/) { return false; }
inline Loot* LootOfGuidCompat(Player* player, ObjectGuid guid) { return sLootMgr.GetLoot(player, guid); }

// What a Loot holds (LootAccess). `visible` is ai::PersonalCopyVisible; `content` is LootAccess::GetLootContentFor.
template <class VisibleFn>
inline std::vector<LootItem*> LootContentForCompat(Loot const* loot, Player* player, VisibleFn&& visible)
{
	std::vector<LootItem*> retvec;
	if (!loot)
		return retvec;

	// Penqle's items vector holds LootItem values; bot consumers want pointers.
	// Cast away const to publish as ptr — bot consumers treat as read-only via const accessors.
	for (auto const& item : loot->items)
	{
		if (item.is_looted)
			continue;
		// FFA and conditional items have a per-player looted flag. Read those
		// through the lists below instead of bypassing that flag here.
		if (player && (item.freeforall || item.conditionId))
			continue;
		retvec.push_back(const_cast<LootItem*>(&item));
	}

	if (!player)
		return retvec;

	// Quest drops live outside loot->items, in the core's per-player quest/FFA/
	// conditional lists. Without them a corpse whose only loot for this bot is
	// a quest item looks empty. Read personal copies through these lists and
	// skip entries already looted (either the global or per-player flag).
	uint32 const guidLow = player->GetGUIDLow();

	QuestItemMap const* maps[3] = { &loot->GetPlayerQuestItems(), &loot->GetPlayerFFAItems(), &loot->GetPlayerNonQuestNonFFAConditionalItems() };
	bool const turnAssigned = loot->roundRobinPlayer != 0;
	bool const viewerHoldsTurn = loot->roundRobinPlayer == player->GetGUID();
	uint32 const turnHolder = ObjectGuid(loot->roundRobinPlayer).GetCounter();
	for (uint8 m = 0; m < 3; ++m)
	{
		QuestItemMap::const_iterator it = maps[m]->find(guidLow);
		if (it == maps[m]->end() || !it->second)
			continue;

		bool const isQuestMap = (m == 0);
		for (QuestItem const& qi : *it->second)
		{
			LootItemList const& store = isQuestMap ? loot->m_questItems : loot->items;
			if (qi.index >= store.size())
				continue;

			LootItem* item = const_cast<LootItem*>(&store[qi.index]);
			if (qi.is_looted || item->is_looted)
				continue;

			// "Should loot object" must agree with the window: the core hides a personal
			// non-FFA copy while another member holds the turn (m == 1 is the FFA list,
			// which the core always shows).
			if (m != 1 && !visible(item->freeforall, turnAssigned, viewerHoldsTurn,
			                       maps[m]->find(turnHolder) != maps[m]->end()))
				continue;

			if (!isQuestMap)
			{
				bool alreadyListed = false;
				for (LootItem* listed : retvec)
				{
					if (listed == item)
					{
						alreadyListed = true;
						break;
					}
				}
				if (alreadyListed)
					continue;
			}

			retvec.push_back(item);
		}
	}

	return retvec;
}

template <class ContentFn>
inline uint32 LootStatusForCompat(Loot const* loot, Player const* player, ContentFn&& content)
{
	if (!loot || !player)
		return 0;

	uint32 status = 0;

	if (loot->gold != 0)
		status |= LOOT_STATUS_CONTAIN_GOLD;

	WorldObject const* lootTarget = loot->GetLootTarget();
	Group* group = const_cast<Player*>(player)->GetGroup();
	for (LootItem* lootItemPtr : content(const_cast<Player*>(player)))
	{
		LootItem const& lootItem = *lootItemPtr;
		if (lootItem.is_looted || !lootItem.AllowedForPlayer(player, lootTarget))
			continue;

		// Match the core's GROUP_PERMISSION view: an under-threshold item is
		// hidden from non-round-robin members until the designated looter
		// releases it. An active roll is still unfinished loot for everyone.
		if (group && !lootItem.freeforall && loot->roundRobinPlayer &&
			loot->roundRobinPlayer != player->GetObjectGuid() && lootItem.is_underthreshold)
			continue;

		status |= LOOT_STATUS_NOT_FULLY_LOOTED;

		if (lootItem.freeforall)
			status |= LOOT_STATUS_CONTAIN_FFA;

		// The status above already marks blocked and rolling items unfinished.
	}
	return status;
}

// Loot templates (the drop maps) and the row type of the module's layout classes.
using LootStoreItemCompat = LootStoreItem;
using LootStoreItemListCompat = LootStoreItemList;

template <class Access>
inline Access const* LootTemplateOfGuidCompat(ObjectGuid const& guid, LootType type)
{
	LootTemplate const* lTemplate = nullptr;

	if (guid.IsCreature())
	{
		CreatureInfo const* info = sObjectMgr.GetCreatureTemplate(guid.GetEntry());

		if (info)
		{
			if (type == LOOT_CORPSE)
				lTemplate = LootTemplates_Creature.GetLootFor(info->loot_id);
			else if (type == LOOT_PICKPOCKETING && info->pickpocket_loot_id)
				lTemplate = LootTemplates_Pickpocketing.GetLootFor(info->pickpocket_loot_id);
			else if (type == LOOT_SKINNING && info->skinning_loot_id)
				lTemplate = LootTemplates_Skinning.GetLootFor(info->skinning_loot_id);
		}
	}
	else if (guid.IsGameObject())
	{
		GameObjectInfo const* info = sObjectMgr.GetGameObjectInfo(guid.GetEntry());

		if (info && info->GetLootId() != 0)
		{
			if (type == LOOT_CORPSE)
				lTemplate = LootTemplates_Gameobject.GetLootFor(info->GetLootId());
			else if (type == LOOT_FISHINGHOLE)
				lTemplate = LootTemplates_Fishing.GetLootFor(info->GetLootId());
		}
	}
	else if (guid.IsItem())
	{
		ItemPrototype const* proto = GetItemProtoByIdCompat(guid.GetEntry());

		if (proto)
		{
			if (type == LOOT_CORPSE)
				lTemplate = LootTemplates_Item.GetLootFor(proto->ItemId);
			else if (type == LOOT_DISENCHANTING && proto->DisenchantID)
				lTemplate = LootTemplates_Disenchant.GetLootFor(proto->DisenchantID);
		}
	}

	return reinterpret_cast<Access const*>(lTemplate);
}

template <class Access, class Info>
inline Access const* GameObjectLootTemplateCompat(Info const* info)
{
	return reinterpret_cast<Access const*>(LootTemplates_Gameobject.GetLootFor(info->GetLootId()));
}

template <class Access>
inline Access const* ReferenceLootTemplateCompat(uint32 referenceId)
{
	return reinterpret_cast<Access const*>(LootTemplates_Reference.GetLootFor(referenceId));
}

// Quest relation tables: T's own ObjectMgr members (QuestValues.h reads them through a derived class); nothing to add.
