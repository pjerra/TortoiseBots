#include "playerbot/playerbot.h"
#include "SharedValueContext.h"
#include "LootValues.h"
#include "playerbot/strategy/actions/LootAction.h"
#include "../../policy/loot/SkinningLootPolicy.h"
#include "../../policy/loot/LootViewRulePolicy.h"

using namespace ai;

// Penqle's Singleton<> requires an explicit instantiation in a .cpp file.
INSTANTIATE_SINGLETON_1(ai::SharedObjectContext);

// LootAccess methods read from the wrapped Loot* (proper accessor-based
// access; the original layout-cheat reinterpret_cast pattern was removed).

LootType LootAccess::lootType() const
{
	return loot ? loot->loot_type : LOOT_CORPSE;
}

std::vector<LootItem*> LootAccess::GetLootContentFor(Player* player) const
{
	// The walk over the loot's own item lists is a both-core helper (T: the original walk; F: no items, feature "loot: items").
	return LootContentForCompat(loot, player, [](bool freeforall, bool turnAssigned, bool viewerHoldsTurn, bool holderHasCopies)
	{
		return ai::PersonalCopyVisible(freeforall, turnAssigned, viewerHoldsTurn, holderHasCopies);
	});
}

// Get loot status for a specified player.
// cmangos returned bitflags reflecting "has gold / not fully looted / contains FFA / etc."
// Derive the status from the per-player content list (shared items plus the
// core's per-player quest/FFA/conditional lists) and the group round-robin
// contract; do not route through the core's intentionally empty loot-view
// permission sentinel.
uint32 LootAccess::GetLootStatusFor(Player const* player) const
{
	return LootStatusForCompat(loot, player, [this](Player* p) { return GetLootContentFor(p); });
}

// Is there is any loot available for provided player
bool LootAccess::IsLootedFor(Player const* player) const
{
	return (GetLootStatusFor(player) == 0);
}

bool LootAccess::IsLootedForAll() const
{
	if (!loot)
		return true;

	// Penqle's allowed-looters set is private; use the public IsAllowedLooter instead.
	// For our purposes (despawn timer), "looted for all" = no items left + no gold left.
	if (loot->gold > 0)
		return false;

	for (auto const& item : loot->items)
	{
		if (!item.is_looted)
			return false;
	}
	return true;
}

LootTemplateAccess const* DropMapValue::GetLootTemplate(ObjectGuid guid, LootType type)
{
	// T reads the creature / game object / item loot tables and casts the core's template to LootTemplateAccess; F keeps the rows of a
	// loot template private, so the helper answers nullptr there (feature "loot tables (drop maps)").
	return LootTemplateOfGuidCompat<LootTemplateAccess>(guid, type);
}

DropMap* ItemDropMapValue::Calculate()
{
	DropMap* dropMap = new DropMap;

	for (uint32 itemId = 0; itemId < sItemStorage.GetMaxEntry(); itemId++)
	{
		ItemPrototype const* proto = sItemStorage.LookupEntry<ItemPrototype>(itemId);

		if (!proto)
			continue;

		if (!(proto->Flags & ITEM_FLAG_HAS_LOOT))
			continue;

		LootTemplateAccess const* lTemplateA = DropMapValue::GetLootTemplate(GuidFromNumbersCompat(HIGHGUID_ITEM, itemId, uint32(1)), LOOT_CORPSE);

		if (lTemplateA)
		{
			for (LootStoreItemCompat const& lItem : lTemplateA->Entries)
				dropMap->insert(std::make_pair(lItem.itemid, itemId));

			for (LootLootGroupAccess const& group : lTemplateA->Groups)
			{
				for (LootStoreItemCompat const& lItem : group.ExplicitlyChanced)
					dropMap->insert(std::make_pair(lItem.itemid, itemId));

				for (LootStoreItemCompat const& lItem : group.EqualChanced)
					dropMap->insert(std::make_pair(lItem.itemid, itemId));
			}
		}
	}

	return dropMap;
}

DropMap* DropMapValue::Calculate()
{
	DropMap* dropMap = new DropMap;

	int32 sEntry;

	for (uint32 entry = 0; entry < sCreatureStorage.GetMaxEntry(); entry++)
	{
		sEntry = entry;

		LootTemplateAccess const* lTemplateA = GetLootTemplate(GuidFromNumbersCompat(HIGHGUID_UNIT, entry, uint32(1)), LOOT_CORPSE);

		if (lTemplateA)
		{
			for (LootStoreItemCompat const& lItem : lTemplateA->Entries)
				dropMap->insert(std::make_pair(lItem.itemid, sEntry));

			for (LootLootGroupAccess const& group : lTemplateA->Groups)
			{
				for (LootStoreItemCompat const& lItem : group.ExplicitlyChanced)
					dropMap->insert(std::make_pair(lItem.itemid, sEntry));

				for (LootStoreItemCompat const& lItem : group.EqualChanced)
					dropMap->insert(std::make_pair(lItem.itemid, sEntry));
			}
		}
	}

	for (uint32 entry = 0; entry < sGOStorage.GetMaxEntry(); entry++)
	{
		sEntry = entry;

		LootTemplateAccess const* lTemplateA = GetLootTemplate(GuidFromNumbersCompat(HIGHGUID_GAMEOBJECT, entry, uint32(1)), LOOT_CORPSE);

		if (lTemplateA)
		{
			for (LootStoreItemCompat const& lItem : lTemplateA->Entries)
				dropMap->insert(std::make_pair(lItem.itemid, -sEntry));

			for (LootLootGroupAccess const& group : lTemplateA->Groups)
			{
				for (LootStoreItemCompat const& lItem : group.ExplicitlyChanced)
					dropMap->insert(std::make_pair(lItem.itemid, -sEntry));

				for (LootStoreItemCompat const& lItem : group.EqualChanced)
					dropMap->insert(std::make_pair(lItem.itemid, -sEntry));
			}
		}
	}

	DropMap* itemDropMap = GAI_VALUE_COMPAT(DropMap*, "item drop map");
	if (!itemDropMap)
		return dropMap;

	//Add items that drop from items.
	// Stage the new pairs first: inserting into dropMap while iterating a range from the same
	// unordered_multimap can trigger a rehash, which invalidates the range iterators (itr /
	// range.second) and crashes on the next ++itr. Build the additions, then apply them once.
	std::vector<std::pair<uint32, int32>> itemSourcedDrops;
	for (auto& [lootItemId, sourceItemId] : *itemDropMap)
	{
		auto range = dropMap->equal_range(sourceItemId);
		for (auto itr = range.first; itr != range.second; ++itr)
			itemSourcedDrops.emplace_back(lootItemId, itr->second);
	}

	for (auto& drop : itemSourcedDrops)
		dropMap->insert(drop);

	return dropMap;
}

//What items does this entry have in its loot list?
std::list<int32> ItemDropListValue::Calculate()
{
	uint32 itemId = stoi(getQualifier());

	DropMap* dropMap = GAI_VALUE_COMPAT(DropMap*, "drop map");
	if (!dropMap)
		return {};

	std::list<int32> entries;

	auto range = dropMap->equal_range(itemId);

	for (auto itr = range.first; itr != range.second; ++itr)
		entries.push_back(itr->second);

	return entries;
}

//What items does this entry have in its loot list?
std::list<uint32> EntryLootListValue::Calculate()
{
	int32 entry = stoi(getQualifier());

	std::list<uint32> items;

	DropMap* dropMap = GAI_VALUE_COMPAT(DropMap*, "drop map");
	if (!dropMap)
		return {};
	for (auto it = dropMap->begin(); it != dropMap->end(); ++it)
	{
		if (it->second == entry)
		{
			items.push_back(it->first);
		}
	}

	return items;
}

//What is the item's loot chance?
float LootChanceValue::Calculate()
{
	int32 entry = getMultiQualifierInt(getQualifier(), 0, " ");
	uint32 itemId = getMultiQualifierInt(getQualifier(), 1, " ");

	LootTemplateAccess const* lTemplateA;

	if (entry > 0)
		lTemplateA = DropMapValue::GetLootTemplate(GuidFromNumbersCompat(HIGHGUID_UNIT, entry, uint32(1)), LOOT_CORPSE);
	else
		lTemplateA = DropMapValue::GetLootTemplate(GuidFromNumbersCompat(HIGHGUID_GAMEOBJECT, entry, uint32(1)), LOOT_CORPSE);

	if (lTemplateA)
	{
		for (auto& item : lTemplateA->Entries)
			if (item.itemid == itemId)
				return item.chance;

		for (LootLootGroupAccess const& group : lTemplateA->Groups)
		{
			for (LootStoreItemCompat const& item : group.ExplicitlyChanced)
				if (item.itemid == itemId)
					return item.chance;

			float equalChance = 100.0f / (float)group.EqualChanced.size();

			for (LootStoreItemCompat const& item : group.EqualChanced)
				if (item.itemid == itemId)
					return item.chance ? item.chance : equalChance;
		}
	}

	return 0.0f;
}

itemUsageMap EntryLootUsageValue::Calculate()
{
	itemUsageMap items;

	for (auto itemId : GAI_VALUE2_COMPAT(std::list<uint32>, "entry loot list", getQualifier()))
	{
		items[AI_VALUE2(ItemUsage, "item usage", itemId)].push_back(itemId);
	}

	return items;
}

bool HasUpgradeValue::Calculate()
{
	for (auto itemId : GAI_VALUE2_COMPAT(std::list<uint32>, "entry loot list", getQualifier()))
	{
		ForceItemUsage forceUsage = AI_VALUE2_EXISTS(ForceItemUsage, "force item usage", itemId, ForceItemUsage::FORCE_USAGE_NONE);

		if (forceUsage == ForceItemUsage::FORCE_USAGE_NEED)
			return true;

		ItemQualifier qualifier(itemId);

		ItemUsage equip = ItemUsageValue::QueryItemUsageForEquip(qualifier, bot);
		if (equip == ItemUsage::ITEM_USAGE_EQUIP)
			return true;
	}
	return false;
}

//How many (stack) items can be looted while still having free space.
uint32 StackSpaceForItem::Calculate()
{
	uint32 maxValue = 999;

	uint32 itemId = stoi(getQualifier());

	ItemPrototype const* proto = sItemStorage.LookupEntry<ItemPrototype>(itemId);

	if (!proto)
		return maxValue;

	if (proto->MaxCount > 0)
		return proto->MaxCount - AI_VALUE2(uint32, "item count", proto->Name1);

	if (ai->HasActivePlayerMaster())
		return maxValue;

	if (AI_VALUE(uint8, "bag space") <= 80)
		return maxValue;

	uint32 maxStack = proto->GetMaxStackSize();
	if (maxStack == 1)
		return 0;

	std::list<Item*> found = AI_VALUE2(std::list < Item*>, "inventory items", chat->formatItem(proto));

	maxValue = 0;

	for (auto stack : found)
		if (maxStack - stack->GetCount() > maxValue)
			maxValue = maxStack - stack->GetCount();

	return maxValue;
}

bool ShouldLootObject::Calculate()
{
	GuidPosition guid(GuidFromQualifierCompat(getQualifier()), WorldPosition(bot));

	if (!guid)
		return false;

	WorldObject* object = guid.GetWorldObject(bot->GetInstanceId());

	if (!object)
		return false;

	// Loot is stored directly on Creature/GameObject on T; F keeps it per looting player (a creature's loot for this bot, nullptr when it
	// has none; a game object's loot is not read).
	Loot* objLoot = LootOfObjectCompat(object, bot);
	if (!objLoot)
    {
		// F: a creature with no loot for this bot, or a game object (not ported): nothing to take. T: never.
		if (AbsentLootMeansNothingToTakeCompat(object))
			return false;

		if (!object->IsGameObject())
			return true;

		GameObject* go = static_cast<GameObject*>(object);

		if (go->GetGoType() != GAMEOBJECT_TYPE_GOOBER)
			return true;

		uint32 spellId = go->GetSpellId();

		if (!spellId)
            return true;

		SpellEntry const* lootSpell = GetSpellStore()->LookupEntry<SpellEntry>(spellId);

		if (!lootSpell || lootSpell->Effect[0] != SPELL_EFFECT_CREATE_ITEM)
            return true;

		uint32 itemId = lootSpell->EffectItemType[0];

		if (!AI_VALUE2(uint32, "stack space for item", itemId))
            return false;

        ItemQualifier ltemQualifier(itemId);

        if (!StoreLootAction::IsLootAllowed(ltemQualifier, ai))
            return false;

		return true;
    }

	Loot* objLoot2 = objLoot;
	if (objLoot2 && objLoot2->gold > 0)
		return true;

	// LootAccess wraps a Loot* now.
	if (!objLoot2)
		return false;

	LootAccess lootAccess(objLoot2);
	uint32 const gatherSkill = DecideGatherSkillForLoot((uint32)lootAccess.lootType(), object->IsGameObject(),
		object->IsGameObject() ? GetNodeGatherSkill(static_cast<GameObject*>(object)) : (uint32)SKILL_NONE);

	for (auto& lItem : lootAccess.GetLootContentFor(bot))
	{
		if (!lItem->itemid)
			continue;

		uint32 canLootAmount = AI_VALUE2(uint32, "stack space for item", lItem->itemid);

		if (canLootAmount < lItem->count)
			continue;

		ItemQualifier ltemQualifier(lItem);
        if (!ShouldSkipLootFilter((uint32)lootAccess.lootType(), object->IsGameObject(), gatherSkill) &&
            !StoreLootAction::IsLootAllowed(ltemQualifier, ai))
            continue;

        return true;
	}

	return false;
}

void ActiveRolls::CleanUp(Player* bot, LootRollMap& rollMap, ObjectGuid guid, uint32 slot)
{
	for (auto roll = rollMap.begin(); roll != rollMap.end();)
	{
		if (!guid.IsEmpty() && roll->first != guid)
		{
			++roll;
			continue;
		}

		if (slot && roll->second != slot)
		{
			++roll;
			continue;
		}

		Loot* loot = LootOfGuidCompat(bot, roll->first);
		if (!loot || roll->second >= loot->items.size() || loot->items[roll->second].is_looted)
		{
			roll = rollMap.erase(roll);
			continue;
		}

		if(!guid.IsEmpty())
		{
			roll = rollMap.erase(roll);
			continue;
		}

		++roll;
	}
}

std::string ActiveRolls::Format()
{
	std::ostringstream out;

	for (auto& roll : value)
	{
		WorldObject* wo = ai->GetWorldObject(roll.first);

		if (wo)
			out << wo->GetName();
		else
			out << GuidTextCompat(roll.first);

		std::string itemLink;

		Loot* loot = LootOfGuidCompat(bot, roll.first);
		if (loot)
		{
			LootItem* item = roll.second < loot->items.size() ? &loot->items[roll.second] : nullptr;

			if (item)
			{
				const ItemPrototype* proto = sItemStorage.LookupEntry<ItemPrototype>(item->itemid);

				if (proto)
				{
					itemLink = ChatHelper::formatItem(proto);
				}
			}
		}

		if (itemLink.empty())
			out << roll.second;
		else
			out << itemLink;

		out << ",";
	}

	return out.str();
}

std::string SeenNeedRolls::Format()
{
    std::ostringstream out;

    for (auto& roll : value)
        out << GuidKeyCompat(std::get<0>(roll.first)) << ":" << std::get<1>(roll.first) << ":" <<
            GuidKeyCompat(std::get<2>(roll.first)) << "=" << (uint32)roll.second << ",";

    return out.str();
}
