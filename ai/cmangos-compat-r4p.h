// The Turtle side of the both-core helpers the ForeverCore port added in slice 1, round R4P (the PlayerbotAI class).
// Same names and signatures as forever/src/compat/TbCompat_P.h; each forwards to the call the bot always made.
// Owned by package R4P: no other package edits this file.
#pragma once

inline uint64 GuidRawCompat(ObjectGuid const& guid) { return guid.GetRawValue(); }

inline bool HasPlayerFlagCompat(Player const* player, uint32 flags) { return player->HasFlag(PLAYER_FLAGS, flags); }
inline bool HasUnitFlagCompat(Unit const* unit, uint32 flags) { return unit->HasFlag(UNIT_FIELD_FLAGS, flags); }
inline bool PlayerInSanctuaryCompat(Player const* player) { return player->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_SANCTUARY); }
inline uint32 PlayerNextLevelXPCompat(Player const* player) { return player->GetUInt32Value(PLAYER_NEXT_LEVEL_XP); }
inline uint32 ItemDurabilityCompat(Item const* item) { return item->GetUInt32Value(ITEM_FIELD_DURABILITY); }
inline uint32 ItemMaxDurabilityCompat(Item const* item) { return item->GetUInt32Value(ITEM_FIELD_MAXDURABILITY); }

// The Turtle position already has the names x, y, z, o: handed back as it is.
inline Position const& MovementPosCompat(MovementInfo const& info) { return info.pos; }


inline ObjectGuid DuelOpponentGuidCompat(Player const* player)
{
    return player->m_duel ? player->m_duel->opponent : ObjectGuid();
}

inline Spell* NewSpellCompat(Unit* caster, SpellEntry const* info, bool triggered) { return new Spell(caster, info, triggered); }
inline void SpellSetCastItemCompat(Spell* spell, Item* item) { spell->SetCastItem(item); }
inline void SpellTargetsSetUnitCompat(SpellCastTargets& targets, Unit* unit) { targets.setUnitTarget(unit); }
inline void SpellTargetsSetGOCompat(SpellCastTargets& targets, GameObject* object) { targets.setGOTarget(object); }
inline void SpellTargetsSetItemCompat(SpellCastTargets& targets, Item* item) { targets.setItemTarget(item); }
inline Item* SpellTargetsGetItemCompat(SpellCastTargets const& targets) { return targets.getItemTarget(); }
inline void SpellTargetsSetDestinationCompat(SpellCastTargets& targets, float x, float y, float z) { targets.setDestination(x, y, z); }

using AuraListCompat = Unit::AuraList;
inline Unit::AuraList const& AurasByTypeCompat(Unit const* unit, AuraType type) { return unit->GetAurasByType(type); }
inline Aura* GetAuraCompat(Unit* unit, uint32 spellId, uint32 effectIndex) { return unit->GetAura(spellId, SpellEffectIndex(effectIndex)); }
inline SpellAuraHolder* AuraHolderCompat(Aura* aura) { return aura->GetHolder(); }
inline SpellAuraHolder const* AuraHolderCompat(Aura const* aura) { return aura->GetHolder(); }
inline bool AuraIsPositiveCompat(Aura const* aura) { return aura->IsPositive(); }
inline bool AuraIsAreaCompat(Aura const* aura) { return aura->IsAreaAura(); }

inline bool IsImmuneToSpellCompat(Unit const* target, SpellEntry const* spell, bool castOnSelf, WorldObject const* /*caster*/)
{
    return target->IsImmuneToSpell(spell, castOnSelf);
}
inline bool IsImmuneToSpellEffectCompat(Unit const* target, SpellEntry const* spell, uint32 effectIndex, bool castOnSelf, WorldObject const* /*caster*/)
{
    return target->IsImmuneToSpellEffect(spell, SpellEffectIndex(effectIndex), castOnSelf);
}
inline bool IsImmuneToDamageCompat(Unit const* target, SpellEntry const* spell, WorldObject const* /*caster*/)
{
    return target->IsImmuneToDamage(spell->GetSpellSchoolMask(), spell);
}

inline uint32 SpellPowerCostCompat(SpellEntry const* spell, Unit const* caster) { return Spell::CalculatePowerCost(spell, const_cast<Unit*>(caster), nullptr, nullptr); }
inline void ApplyRangeSpellModCompat(Player* modOwner, SpellEntry const* spell, float& range) { modOwner->ApplySpellMod(spell->Id, SPELLMOD_RANGE, range); }
inline DiminishingGroup DiminishingGroupOfSpellCompat(SpellEntry const* spell, bool triggered) { return spell->GetDiminishingReturnsGroup(triggered); }
inline bool CreatureIsWorldBossRankCompat(Creature const* creature)
{
    CreatureInfo const* info = creature->GetCreatureInfo();
    return info && info->rank == CREATURE_ELITE_WORLDBOSS;
}

inline void AckNearTeleportCompat(Player* bot)
{
    WorldPacket p = WorldPacket(MSG_MOVE_TELEPORT_ACK, 8 + 4 + 4);
    p << bot->getObjectGuid();
    p << bot->GetLastCounterForMovementChangeType(TELEPORT); // movement counter the core is waiting to ACK
    p << (uint32) time(0); // time - not currently used
    bot->GetSession()->HandleMoveTeleportAckOpcode(p);
}
inline void AckFarTeleportCompat(Player* bot) { bot->GetSession()->HandleMoveWorldportAckOpcode(); }
inline void CancelLogoutCompat(Player* bot)
{
    WorldPacket p;
    bot->GetSession()->HandleLogoutCancelOpcode(p);
}

inline void BotSayCompat(Player* bot, std::string const& text, uint32 lang) { bot->Say(text, lang); }
inline void BotYellCompat(Player* bot, std::string const& text, uint32 lang) { bot->Yell(text, lang); }
inline void BotWhisperCompat(Player* bot, Player* receiver, std::string const& text)
{
    WorldPacket data;
    ChatHandler::BuildChatPacket(data, CHAT_MSG_WHISPER, text.c_str(), LANG_UNIVERSAL, bot->GetChatTag(), bot->getObjectGuid(), bot->GetName());
    receiver->GetSession()->SendPacket(&data);
}
inline void BotGroupChatCompat(Player* bot, ChatMsg chatType, std::string const& text, std::vector<Player*> const& receivers)
{
    WorldPacket data;
    ChatHandler::BuildChatPacket(data, chatType, text.c_str(), LANG_UNIVERSAL, CHAT_TAG_NONE, bot->getObjectGuid(), bot->GetName());
    for (Player* receiver : receivers)
        receiver->GetSession()->SendPacket(&data);
}
inline void BotChatLikePlayerCompat(Player* bot, ChatMsg chatType, uint32 lang, std::string const& text)
{
    WorldPacket packet_template(CMSG_MESSAGECHAT);
    packet_template << chatType; // the enum, four bytes as the legacy code wrote it
    packet_template << lang;
    packet_template << text;
    std::unique_ptr<WorldPacket> packetPtr(new WorldPacket(packet_template));
    bot->GetSession()->QueuePacket(packetPtr.release());
}
inline void BotQueuePacketCompat(Player* bot, WorldPacket const& packet)
{
    std::unique_ptr<WorldPacket> queued(new WorldPacket(packet));
    bot->GetSession()->QueuePacket(queued.release());
}

inline uint32 PlayerAccountIdCompat(ObjectGuid guid) { return sObjectMgr.GetPlayerAccountIdByGUID(guid); }
inline bool PlayerNameByGuidCompat(ObjectGuid guid, std::string& name) { return sObjectMgr.GetPlayerNameByGUID(guid, name); }
inline ObjectGuid PlayerGuidByNameCompat(std::string const& name) { return sObjectMgr.GetPlayerGuidByName(name); }
inline Player* PlayerByGuidCompat(ObjectGuid guid) { return sObjectMgr.GetPlayer(guid); }
inline ObjectGuid PlayerGuidCompat(uint64 low) { return ObjectGuid(HIGHGUID_PLAYER, low); }
inline bool IsHeadlessCompat(WorldSession* session) { return session->IsHeadless(); }

// The reaction of one faction template to another, the body PlayerbotAI::GetFactionReaction held before the port (a copy of the
// core's reputation logic), moved here word for word.
inline ReputationRank FactionReactionCompat(FactionTemplateEntry const* thisTemplate, FactionTemplateEntry const* otherTemplate)
{
    MANGOS_ASSERT(thisTemplate)
        MANGOS_ASSERT(otherTemplate)

        // Original logic begins

        if (otherTemplate->ourMask & thisTemplate->hostileMask)
            return REP_HOSTILE;

    if (thisTemplate->enemyFaction[0] && otherTemplate->faction)
    {
        for (unsigned int i : thisTemplate->enemyFaction)
        {
            if (i == otherTemplate->faction)
                return REP_HOSTILE;
        }
    }

    if (otherTemplate->ourMask & thisTemplate->friendlyMask)
        return REP_FRIENDLY;

    if (thisTemplate->friendFaction[0] && otherTemplate->faction)
    {
        for (unsigned int i : thisTemplate->friendFaction)
        {
            if (i == otherTemplate->faction)
                return REP_FRIENDLY;
        }
    }

    if (thisTemplate->ourMask & otherTemplate->friendlyMask)
        return REP_FRIENDLY;

    if (otherTemplate->friendFaction[0] && thisTemplate->faction)
    {
        for (unsigned int i : otherTemplate->friendFaction)
        {
            if (i == thisTemplate->faction)
                return REP_FRIENDLY;
        }
    }
    return REP_NEUTRAL;
}

inline Player* FindPlayerNotInWorldCompat(ObjectGuid guid) { return ObjectAccessor::FindPlayerNotInWorld(guid); }
inline float UnitCastSpeedModCompat(Unit const* unit) { return unit->GetFloatValue(UNIT_MOD_CAST_SPEED); }
inline MovementGenerator* MotionMasterTopCompat(MotionMaster* master)
{
    if (!master->empty())
        if (MovementGenerator* movgen = master->top())
            return movgen;
    return nullptr;
}
inline Unit::SpellAuraHolderMap const& AuraHolderMapCompat(Unit const* unit) { return unit->GetSpellAuraHolderMap(); }
inline uint32 AreaZoneIdCompat(AreaTableEntry const* area) { return area->ZoneId; }
inline std::string AreaNameOrEmptyCompat(AreaTableEntry const* area) { return area && area->Name ? std::string(area->Name) : std::string(); }
inline uint32 ZoneIdAtCompat(uint32 mapId, float x, float y, float z) { return sTerrainMgr.getZoneId(mapId, x, y, z); }
