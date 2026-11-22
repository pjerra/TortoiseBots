// The Turtle side of the both-core helpers the ForeverCore port added in slice 2, package I2 (loot, add loot and use item actions).
// Same names and signatures as forever/src/compat/TbCompat_I2.h; each forwards to the call the bot always made.
// Owned by package I2: no other package edits this file.
#pragma once

// ===============================================================================================================
// LOOT: opening a corpse, reading the window, taking the money, releasing (LootAction.cpp)
// Each helper is the packet the legacy code built and the handler it handed it to, moved word for word.
// ===============================================================================================================

// CMSG_LOOT: open the loot window of a corpse. The server answers with SMSG_LOOT_RESPONSE, which reaches the bot as the
// "loot response" event, so nothing else is done here. Returns true: the legacy code treated the request as sent.
template <class AiType>
inline bool LootOpenCompat(AiType* /*ai*/, Player* bot, ObjectGuid const& guid)
{
    WorldPacket packet(CMSG_LOOT, 8);
    packet << guid;
    bot->GetSession()->HandleLootOpcode(packet);
    return true;
}

// CMSG_LOOT_RELEASE.
inline void LootReleaseCompat(Player* bot, ObjectGuid const& guid)
{
    WorldPacket packet(CMSG_LOOT_RELEASE, 8);
    packet << guid;
    bot->GetSession()->HandleLootReleaseOpcode(packet);
}

// Release every open window of the bot: Turtle opens one window per corpse and has no area loot, so this is the named window only.
inline void LootReleaseAllCompat(Player* bot, ObjectGuid const& guid) { LootReleaseCompat(bot, guid); }

// CMSG_LOOT_MONEY: take the money of the open window. Returns true (the legacy code never looked at an answer).
inline bool LootMoneyCompat(Player* bot)
{
    WorldPacket packet(CMSG_LOOT_MONEY, 0);
    bot->GetSession()->HandleLootMoneyOpcode(packet);
    return true;
}

// The head of the loot response packet (StoreLootAction::Execute): the corpse guid, the loot type, the money and the item count; a
// packet of 10 bytes or less is Player::SendLootError's [guid][0][error code], returned as false with the code in `lootError`.
inline bool LootWindowReadCompat(Player* /*bot*/, WorldPacket& p, ObjectGuid& guid, uint8& loot_type, uint32& gold, uint8& items, uint8& lootError)
{
    p.rpos(0);
    p >> guid;      // 8 corpse guid
    p >> loot_type; // 1 loot type

    if (p.size() > 10)
    {
        p >> gold;      // 4 money on corpse
        p >> items;     // 1 number of items on corpse
        return true;
    }

    lootError = 0;
    if (p.rpos() < p.size())
        p >> lootError;
    return false;
}

// The core's loot of the bot's open window (sLootMgr.GetLoot(bot)) and the loot target of the player (SetLootGuid).
inline Loot* LootOfWindowCompat(Player* bot, ObjectGuid const& /*guid*/) { return sLootMgr.GetLoot(bot); }
inline void SetLootGuidCompat(Player* bot, ObjectGuid const& guid) { bot->SetLootGuid(guid); }

// The rights the 1.12 loot view gives the bot on one slot of the open window (StoreLootAction's item loop), as the facts the policy
// ai::BotMayTakeLootSlot is asked with. Returns false when the slot holds no item for the bot.
inline bool LootSlotFactsCompat(Player* bot, Loot* loot, uint8 itemindex, bool& allowedForPlayer, bool& isQuest, bool& isFfa,
    bool& isBlocked, bool& sharedAllowed)
{
    QuestItem* qitem = nullptr;
    QuestItem* ffaitem = nullptr;
    LootItem* lootItem = loot->LootItemInSlot(itemindex, bot->GetGUIDLow(), &qitem, &ffaitem);
    if (!lootItem)
        return false;

    WorldObject const* lootTarget = loot ? loot->GetLootTarget() : nullptr;
    bool const personal = qitem || ffaitem;
    sharedAllowed = !personal &&
        lootItem->GetSlotTypeForSharedLoot(ALL_PERMISSION, bot, lootTarget) != MAX_LOOT_SLOT_TYPE;
    allowedForPlayer = lootItem->AllowedForPlayer(bot, lootTarget);
    isQuest = qitem != nullptr;
    isFfa = ffaitem != nullptr;
    isBlocked = lootItem->is_blocked;
    return true;
}

// CMSG_AUTOSTORE_LOOT_ITEM.
inline void LootAutostoreCompat(Player* bot, Loot* /*loot*/, uint8 itemindex)
{
    WorldPacket packet(CMSG_AUTOSTORE_LOOT_ITEM, 1);
    packet << itemindex;
    bot->GetSession()->HandleAutostoreLootItemOpcode(packet);
}

// Gathering, skinning and opening game objects are a later slice on ForeverCore (lead decision D2); on Turtle they always run.
inline bool LootGatherUnportedCompat(WorldObject* /*object*/) { return false; }

// Item looting is a later slice on ForeverCore (lead decision D2); on Turtle the item loop of the loot window always runs.
inline bool ItemLootingUnportedCompat() { return false; }

// The 1.12 quest template's required item i and its count (Quest::ReqItemId / ReqItemCount).
inline bool QuestRequiredItemCompat(Quest const* quest, int index, uint32& itemId, uint32& count)
{
    itemId = quest->ReqItemId[index];
    count = quest->ReqItemCount[index];
    return true;
}

// Player::GetMaxLootDistance(creature): how far from the corpse the server lets the bot open it (for the debug lines).
inline float LootDistanceCompat(Player* bot, Creature* creature) { return bot->GetMaxLootDistance(creature); }

// The group's id as a key (Group::GetId).
inline uint64 GroupIdKeyCompat(Group* group) { return uint64(group->GetId()); }

// Loot-boost helpers (ApplyBotLootBonus): the server's own quality rate of an item drop, and the percent roll.
template <class LootStoreType>
inline float DropRateOfQualityCompat(LootStoreType& creatureLootStore, uint32 quality)
{
    return creatureLootStore.IsRatesAllowed()
        ? sWorld.getConfig(quality == ITEM_QUALITY_UNCOMMON ? CONFIG_FLOAT_RATE_DROP_ITEM_UNCOMMON : CONFIG_FLOAT_RATE_DROP_ITEM_RARE)
        : 1.0f;
}
inline bool RollChanceCompat(float chance) { return roll_chance_f(chance); }

// Whether a corpse still has loot this bot may take (ai::CorpseHasBodyLootFor): the raw lootable flag or the viewer's own right.
inline bool CorpseHasBodyLootForCompat(Player* bot, Creature* creature)
{
    return creature->HasFlag(UNIT_DYNAMIC_FLAGS, UNIT_DYNFLAG_LOOTABLE) || bot->IsAllowedToLoot(creature);
}

// ai::HasVisiblePersonalItemFor: the policy hands in its own algorithm (`body`), which reads the 1.12 personal item maps.
template <class LootType, class ViewerType, class Body>
inline bool PersonalItemVisibleCompat(LootType const& loot, ViewerType* viewer, Body&& body) { return body(loot, viewer); }

// ===============================================================================================================
// ITEM USE (UseItemAction.cpp / UseItemAction.h)
// ===============================================================================================================

// What one on-use spell of an item did (the loop of UseAction::UseItemInternal): `skipped` = the loop continues with the next spell,
// `cast` = the cast started, the targets the item was aimed at, and the sleep the action takes after a cast that was not food or drink.
struct ItemSpellUseCompat
{
    bool skipped = false;
    bool cast = false;
    uint32 castDuration = 0;
    Unit* unit = nullptr;
    Item* item = nullptr;
    GameObject* gameObject = nullptr;
};

// The items whose use is limited to some targets (T: the item_required_target table).
inline bool IsTargetValidForItemUse(uint32 itemID, Unit* target)
{
    ItemRequiredTargetMapBounds bounds = sObjectMgr.GetItemRequiredTargetMapBounds(itemID);
    if (bounds.first == bounds.second)
    {
        return true;
    }

    if (target)
    {
        for (ItemRequiredTargetMap::const_iterator itr = bounds.first; itr != bounds.second; ++itr)
        {
            if (itr->second.IsFitToRequirements(target))
            {
                return true;
            }
        }
    }

    return false;
}

// The count of the stack the bot has just used from: the item object's own count (the item is still there on this core).
inline uint32 ItemCountAfterUseCompat(Player*, Item* itemUsed, uint32) { return itemUsed->GetCount(); }

// One on-use spell of an item: UseAction::UseItemInternal's loop body from "Check if valid targets" to the end of the cast, moved
// word for word. SpellType is ai::BotUseItemSpell (declared after this header); `itemForSpell(spellId)` is the "item for spell" value.
template <class SpellType, class AiType, class ItemForSpellFn>
inline ItemSpellUseCompat CastItemSpellCompat(AiType* ai, Player* bot, ItemPrototype const* proto, _Spell const& spellData,
    SpellEntry const* spellInfo, uint32 itemId, uint8 successCasts, Item* itemUsed, Unit* unit, GameObject* gameObject, Item* item,
    ItemForSpellFn&& itemForSpell, bool foodOrDrink)
{
    ItemSpellUseCompat out;
    Unit* unitTarget = nullptr;
    Item* itemTarget = nullptr;
    GameObject* gameObjectTarget = nullptr;

        // Check if valid targets
        bool validTarget = false;
        SpellCastTargets targets;

        // Try to figure out which targets are allowed if the spell doesn't provide it
        uint32 spellTargets = spellInfo->Targets;
        if (spellTargets == 0)
        {
            // Unit target
            if ((spellInfo->EffectImplicitTargetA[0] == TARGET_UNIT || spellInfo->EffectImplicitTargetA[0] == TARGET_UNIT_ENEMY) || // Unit Target
                (proto->Class == ITEM_CLASS_CONSUMABLE && proto->SubClass == ITEM_SUBCLASS_SCROLL) || // Scrolls
                (proto->Class == ITEM_CLASS_TRADE_GOODS && proto->SubClass == ITEM_SUBCLASS_EXPLOSIVES) || // Explosives
                (spellData.SpellCategory == 150) || // First aid
                (spellData.SpellCategory == 831)) // Soulstone
            {
                spellTargets |= TARGET_FLAG_UNIT;
            }

            // Location target
            if ((spellInfo->EffectImplicitTargetA[0] == TARGET_ENUM_UNITS_ENEMY_AOE_AT_DEST_LOC) || // Hostile Aoe Spell
                (proto->Class == ITEM_CLASS_TRADE_GOODS && proto->SubClass == ITEM_SUBCLASS_EXPLOSIVES)) // Explosives
            {
                spellTargets |= TARGET_FLAG_DEST_LOCATION;
            }

            // No target
            if (spellTargets == 0 &&
                (spellInfo->EffectImplicitTargetA[0] == TARGET_NONE || // No target
                 spellInfo->EffectImplicitTargetA[0] == TARGET_UNIT_CASTER)) // Self Target
            {
                validTarget = true;
            }
        }

        if (spellTargets & TARGET_FLAG_DEST_LOCATION)
        {
            if (unit)
            {
                unitTarget = unit;
                targets.setDestination(unit->getPositionX(), unit->getPositionY(), unit->getPositionZ());
                validTarget = true;
            }
            else if (gameObject && gameObject->isSpawned())
            {
                gameObjectTarget = gameObject;
                targets.setDestination(gameObject->getPositionX(), gameObject->getPositionY(), gameObject->getPositionZ());
                validTarget = true;
            }
        }

        if (spellTargets & TARGET_FLAG_UNIT && !validTarget)
        {
            if (unit && IsTargetValidForItemUse(itemId, unit))
            {
                unitTarget = unit;
                targets.setUnitTarget(unit);
                validTarget = true;
            }
        }

        // A key item on a locked object: Targets carries TARGET_FLAG_LOCKED (0x4000), and the effect
        // targets a game object; either one, given a game object, takes this branch.
        bool goEffect = false;
        for (uint8 i = 0; i < MAX_EFFECT_INDEX; ++i)
        {
            if (spellInfo->EffectImplicitTargetA[i] == TARGET_GAMEOBJECT)
            {
                goEffect = true;
            }
        }

        if ((spellTargets & TARGET_FLAG_GAMEOBJECT || spellTargets & TARGET_FLAG_LOCKED || (goEffect && gameObject)) && !validTarget)
        {
            if (gameObject && gameObject->isSpawned())
            {
                gameObjectTarget = gameObject;
                targets.setGOTarget(gameObject);
                targets.m_targetMask = TARGET_FLAG_GAMEOBJECT;
                validTarget = true;
            }
        }

        if (spellTargets & TARGET_FLAG_ITEM && !validTarget)
        {
            if (item)
            {
                itemTarget = item;
                targets.setItemTarget(item);
                validTarget = true;
            }
            else
            {
                // Try to figure out the item target
                Item* itemForSpell_ = itemForSpell(spellInfo->Id);
                if (itemForSpell_)
                {
                    if (!itemForSpell_->GetEnchantmentId(TEMP_ENCHANTMENT_SLOT))
                    {
                        validTarget = true;
                        itemTarget = itemForSpell_;

                        if (bot->GetTrader())
                        {
                            targets.setTradeItemTarget(bot);
                        }
                        else
                        {
                            targets.setItemTarget(itemForSpell_);
                        }
                    }
                }
            }
        }

        if (!validTarget)
        {
            targets.setUnitTarget(bot);
            targets.m_targetMask = TARGET_FLAG_SELF;
            validTarget = true;
        }

        if (validTarget)
        {
            // Validate spellInfo is a proper sSpellTemplate entry before constructing (mirrors MANGOS_ASSERT in Spell::Spell)
            if (!spellInfo || spellInfo != GetSpellEntryCompat(spellInfo->Id))
            {
                out.skipped = true;
                out.unit = unitTarget;
                out.item = itemTarget;
                out.gameObject = gameObjectTarget;
                return out;
            }

            // Use triggered flag only for items with many spell casts and for not first cast
            SpellType* spell = new SpellType(bot, spellInfo, (successCasts > 0) ? TRIGGERED_OLD_TRIGGERED : TRIGGERED_NONE);
            // Spend the item if used in the spell
            if (itemUsed)
            {
                spell->SetCastItem(itemUsed);
                    }

            // Stop the movement for casted items
            const bool isCastedSpell = spell->GetCastedTime() > 0 || IsChanneledSpell(spellInfo);
            if (isCastedSpell)
            {
                ai->StopMoving();
            }

            const SpellCastResult result = spell->ForceSpellStart(&targets);

            // Check for using item on trade item
            bool successCast = result == SPELL_CAST_OK;
            if (result == SPELL_FAILED_DONT_REPORT && (targets.m_targetMask & TARGET_FLAG_TRADE_ITEM))
            {
                successCast = true;
            }
            if (!successCast)
                ai->NoteCastCheckFail(result);

            if (successCast)
            {
                if (foodOrDrink)
                {
                    out.castDuration = 24000;
                }
                else
                {
                    out.castDuration = ai->GetSpellCastDuration(spell);
                }

                out.cast = true;
            }
        }

    out.unit = unitTarget;
    out.item = itemTarget;
    out.gameObject = gameObjectTarget;
    return out;
}

// CMSG_OPEN_ITEM: open an item that holds loot (a quest item bag) from the bags.
inline bool OpenItemCompat(Player* bot, Item* item)
{
    // Open quest item in inventory, containing related items (e.g Gnarlpine necklace, containing Tallonkai's Jewel)
    std::unique_ptr<WorldPacket> packet(new WorldPacket(CMSG_OPEN_ITEM, 2));
    *packet << item->GetBagSlot();
    *packet << item->GetSlot();
    bot->GetSession()->QueuePacket(packet.release());
    return true;
}

// ai::BotUseItemSpell::OpenLockCheck, moved word for word (a friend of the class, so it reads the Spell members the same way).
// The class declares it a friend with `template <class S> friend bool BotUseItemOpenLockCheckCompat(S*);`.
template <class SpellType>
inline bool BotUseItemOpenLockCheckCompat(SpellType* self)
{
    for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
    {
        // for effects of spells that have only one target
        switch (GetSpellEntryCompat(self->m_spellInfo)->Effect[i])
        {
            case SPELL_EFFECT_OPEN_LOCK_ITEM:
            case SPELL_EFFECT_OPEN_LOCK:
            {
                if (self->m_caster->GetTypeId() != TYPEID_PLAYER) // only players can open locks, gather etc.
                    return false;

                // we need a go target in case of TARGET_GAMEOBJECT (for other targets acceptable GO and items)
                // cached-target-allow: this is this spell's own cast target, read while its own prepare/cast validates
                // it and the core reads the same pointer in the same pass (Spell::EffectOpenLock); it is not a
                // current-spell pointer left over from an earlier tick.
                GameObject* const go = self->m_targets.getGOTarget();
                if (GetSpellEntryCompat(self->m_spellInfo)->EffectImplicitTargetA[i] == TARGET_GAMEOBJECT && !go)
                    return false;

                // get the lock entry
                uint32 lockId;
                if (go)
                {
                    // In BattleGround players can use only flags and banners
                    if (((Player*)self->m_caster)->InBattleGround() &&
                        !((Player*)self->m_caster)->CanUseBattleGroundObject())
                        return false;

                    lockId = go->GetGOInfo()->GetLockId();
                    if (!lockId)
                        return false;

                    // check if its in use only when cast is finished (called from spell::cast() with strict = false)
                    if (go->getLootState() == GO_ACTIVATED)
                        return false;

                    if (go->HasFlag(GAMEOBJECT_FLAGS, GO_FLAG_IN_USE))
                        return false;

                    // done in client but we need to recheck anyway
                    if (go->GetGOInfo()->CannotBeUsedUnderImmunity() && self->m_caster->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_IMMUNE))
                        return false;
                }
                else if (Item* item = self->m_targets.getItemTarget())
                {
                    // not own (trade?)
                    if (item->GetOwner() != self->m_caster)
                        return false;

                    lockId = GetItemProtoCompat(item)->LockID;

                    // if already unlocked
                    if (!lockId || item->HasFlag(ITEM_FIELD_FLAGS, ITEM_DYNFLAG_UNLOCKED))
                        return false;
                }
                else
                    return false;

                if (!lockId)                                            // possible case for GO and maybe for items.
                    return false;

                // Get LockInfo
                LockEntry const* lockInfo = sLockStore.LookupEntry(lockId);

                if (!lockInfo)
                    return false;

                // check lock compatibility
                SpellEffectIndex effIdx = SpellEffectIndex(i);
                SkillType skillId = SKILL_NONE;
                int32 reqSkillValue = 0;
                int32 skillValue = 0;
                SpellCastResult res = self->CanOpenLock(effIdx, lockId, skillId, reqSkillValue, skillValue);
                if (res == SPELL_FAILED_BAD_TARGETS)
                    return true;
            }
        }
    }

    return false;
}

// The plain use of a game object: the use packet queued on the bot's session, as UseAction::UseGameObject always sent it.
inline bool QueueGameObjectUseCompat(Player* bot, GameObject* go)
{
    std::unique_ptr<WorldPacket> packet(new WorldPacket(CMSG_GAMEOBJ_USE));
    *packet << go->getObjectGuid();
    bot->GetSession()->QueuePacket(packet.release());
    return true;
}

// Whether this core refuses the use of an item that is not food or drink (T: never), and the use of a game object by the bot.
inline bool ItemUseUnportedCompat(bool /*foodOrDrink*/) { return false; }
inline void RemoveStealthAurasCompat(Player* bot) { if (bot) bot->RemoveSpellsCausingAura(SPELL_AURA_MOD_STEALTH); }
inline bool GameObjectUseUnportedCompat() { return false; }
