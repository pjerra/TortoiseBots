// ForeverCore side of the both-core helpers and names of slice 1, round R4P (the PlayerbotAI class).
// Owned by package R4P: no other package edits this file. Turtle side: ai/cmangos-compat-r4p.h (same names and
// signatures, each forwarding to the call the bot always made).
//
// Notation: T = the Turtle core (~/playerbots-references/penqle-tortoise-wow-modsys/src/game), F = the pinned ForeverCore
// (~/ForeverCore/src/server/game). Where the two cores answer the same question with different numbers the entry says so
// and the notes (forever/docs/SLICE1-R4P-NOTES.md, "Different answers") repeat it.
#pragma once

// ---------------------------------------------------------------------------------------------------------------
// Guids. T ObjectGuid::GetRawValue() is the one 64-bit word (high type | entry | counter); F's guid is 128 bit and
// GetRawValue() is a byte span (ObjectGuid.h:325-326). GetRawValue(0) is the low word: the counter (40 bits) with the
// spawn-group bits above it (ObjectGuid.h:336-346), unique for a player. The bot only logs it ("targetGuid=0x%llx") or
// uses it as a key for a player's guid (RemoteCommander::RitualHolds().Holds).
// Different answer: the number is not the Turtle number, and for a creature it does not carry the entry.
// ---------------------------------------------------------------------------------------------------------------
inline uint64 GuidRawCompat(ObjectGuid const& guid) { return guid.GetRawValue(0); }

// ---------------------------------------------------------------------------------------------------------------
// Flag words. T keeps the player and unit flags in update fields read with HasFlag(PLAYER_FLAGS / UNIT_FIELD_FLAGS, mask)
// (T Object.h:521); F has typed accessors HasPlayerFlag (Player.h:3044) and HasUnitFlag (Unit.h:862), whose masks are the same
// bits for the flags the bot names (PLAYER_FLAGS_GHOST/RESTING 0x10/0x20 on both, T Player.h:387-388, F Player.h:531-532;
// UNIT_FLAG_IN_COMBAT 0x80000, UNIT_FLAG_FLEEING 0x800000, F UnitDefines.h:186-190).
// ---------------------------------------------------------------------------------------------------------------
inline bool HasPlayerFlagCompat(Player const* player, uint32 flags) { return player->HasPlayerFlag(PlayerFlags(flags)); }
inline bool HasUnitFlagCompat(Unit const* unit, uint32 flags) { return unit->HasUnitFlag(UnitFlags(flags)); }

// "The player stands in a sanctuary." T: the PLAYER_FLAGS_SANCTUARY player flag (0x10000, T Player.h:399). F: that bit is now
// LOW_LEVEL_RAID_ENABLED (Player.h:543, "pre-3.0.3 PLAYER_FLAGS_SANCTUARY"), so the unit's sanctuary pvp flag answers the question
// (Unit.h:902). Different answer: the flag is set by the area in the one core and by the unit's pvp state in the other.
inline bool PlayerInSanctuaryCompat(Player const* player) { return player->IsInSanctuary(); }

// The experience still needed for the next level. T update field PLAYER_NEXT_LEVEL_XP, F Player.h:1353.
inline uint32 PlayerNextLevelXPCompat(Player const* player) { return player->GetXPForNextLevel(); }

// An item's durability and its maximum. T update fields ITEM_FIELD_DURABILITY / ITEM_FIELD_MAXDURABILITY, F the item data
// (Item.h:274-276: Durability, MaxDurability).
inline uint32 ItemDurabilityCompat(Item const* item) { return *item->m_itemData->Durability; }
inline uint32 ItemMaxDurabilityCompat(Item const* item) { return *item->m_itemData->MaxDurability; }

// The position held in a movement info. T MovementInfo::pos is a Position with x, y, z, o (T Object.h:283); F Position keeps
// them private behind getters (Position.h). The struct carries the four numbers by value under the Turtle names.
struct TbMovementPos
{
    float x, y, z, o;
};
inline TbMovementPos MovementPosCompat(MovementInfo const& info)
{
    return { info.pos.GetPositionX(), info.pos.GetPositionY(), info.pos.GetPositionZ(), info.pos.GetOrientation() };
}

// ---------------------------------------------------------------------------------------------------------------
// Member renames only the casing differs on (each used as `x->Name(...)`, no bot class defines a member of the name:
// grep over ai/ runtime/ host/ commands/). T Player.h / Unit.h against F Player.h:1306 (isAFK), :1340 (isGMVisible),
// :1338 (isTaxiCheater; SetTaxiCheater has the same spelling on both).
// ---------------------------------------------------------------------------------------------------------------
#define IsAFK isAFK
#define IsGMVisible isGMVisible
#define IsTaxiCheater isTaxiCheater
// The caster guid and the proc charges of an aura. T asks the aura's holder (SpellAuraHolder::GetCasterGuid /
// GetAuraCharges), F the aura itself (Aura::GetCasterGUID, SpellAuras.h:187; Aura::GetCharges, :229). AuraHolderCompat (below)
// hands the bot the object these two are asked of.
#define GetCasterGuid GetCasterGUID
#define GetAuraCharges GetCharges

// ---------------------------------------------------------------------------------------------------------------
// Duel. T Player::m_duel->opponent is the opponent's guid (null pointer = no duel); F Player::duel is a DuelInfo holding the
// opponent Player* (Player.h:462-472, :2205).
// ---------------------------------------------------------------------------------------------------------------
inline ObjectGuid DuelOpponentGuidCompat(Player const* player)
{
    return player->duel && player->duel->Opponent ? player->duel->Opponent->GetGUID() : ObjectGuid::Empty;
}

// ---------------------------------------------------------------------------------------------------------------
// Spell casting. T builds `new Spell(caster, SpellEntry const*, bool triggered)` (Spell.h:336) and fills a SpellCastTargets
// with set*Target / setDestination; F builds `Spell(WorldObject*, SpellInfo const*, TriggerCastFlags)` (Spell.h:475) and its
// targets have SetUnitTarget / SetGOTarget / SetItemTarget / SetDst (Spell.cpp:199-352). `triggered` is the full triggered mask:
// the cast ignores cost, cooldown, cast time and the other checks (the old core's "triggered" flag did the same).
// ---------------------------------------------------------------------------------------------------------------
inline Spell* NewSpellCompat(Unit* caster, SpellEntry const* info, bool triggered)
{
    return new Spell(caster, GetSpellInfoCompat(info), triggered ? TRIGGERED_FULL_MASK : TRIGGERED_NONE);
}

// T Spell::SetCastItem (Spell.h:492) stores the pointer; F has the public member the cast reads in prepare (Spell.cpp:3489-3492).
inline void SpellSetCastItemCompat(Spell* spell, Item* item) { spell->m_CastItem = item; }

// Different answers: T setUnitTarget also copies the unit's position into the destination (Spell.cpp:95-106), F does not; T
// setGOTarget does not set the target mask bit (Spell.cpp:124-129, line commented out) and F sets TARGET_FLAG_GAMEOBJECT; T
// setGOTarget(null) dereferences null, F ignores it. T setDestination has no orientation: 0 here.
inline void SpellTargetsSetUnitCompat(SpellCastTargets& targets, Unit* unit) { targets.SetUnitTarget(unit); }
inline void SpellTargetsSetGOCompat(SpellCastTargets& targets, GameObject* object) { targets.SetGOTarget(object); }
inline void SpellTargetsSetItemCompat(SpellCastTargets& targets, Item* item) { targets.SetItemTarget(item); }
inline Item* SpellTargetsGetItemCompat(SpellCastTargets const& targets) { return targets.GetItemTarget(); }
inline void SpellTargetsSetDestinationCompat(SpellCastTargets& targets, float x, float y, float z)
{
    targets.SetDst(x, y, z, 0.0f);
}

// ---------------------------------------------------------------------------------------------------------------
// Auras. T keeps one Aura per effect inside a SpellAuraHolder (SpellAuras.h:511-515, Unit.h:387 AuraList = list of Aura*, Unit.h:899
// GetAurasByType) and a unit's aura of a type is such an effect. F's Aura is the whole aura of one spell (SpellAuras.h:182-238) and
// the per-effect object is AuraEffect, whose GetBase() is that Aura (SpellAuraEffects.h:40). The bot's `Aura*` therefore is the
// F Aura: the list of a type is the base aura of each effect applied with that type (an aura with two effects of one type is
// listed twice, as T lists its two Auras), and the holder questions are asked of the F Aura itself.
// ---------------------------------------------------------------------------------------------------------------
using AuraListCompat = std::vector<Aura*>;
inline AuraListCompat AurasByTypeCompat(Unit const* unit, AuraType type)
{
    AuraListCompat result;
    for (AuraEffect const* effect : unit->GetAuraEffectsByType(type))
        result.push_back(effect->GetBase());
    return result;
}
// T Unit::GetAura(spellId, effindex) (Unit.h:891): the aura effect of that spell and index, or null.
inline Aura* GetAuraCompat(Unit* unit, uint32 spellId, uint32 effectIndex)
{
    AuraEffect* effect = unit->GetAuraEffect(spellId, uint8(effectIndex));
    return effect ? effect->GetBase() : nullptr;
}
// What T's `aura->GetHolder()` gives (null for an aura without a holder; the bot only asks it for the caster, charges, duration,
// stacks). F: the Aura itself, which answers GetCasterGUID, GetCharges, GetDuration, GetStackAmount (SpellAuras.h:187-238).
inline Aura const* AuraHolderCompat(Aura const* aura) { return aura; }
inline Aura* AuraHolderCompat(Aura* aura) { return aura; }
// T Aura::IsPositive (SpellAuras.h:512, set when the aura is made); F: the spell's own classification (SpellInfo.h:520).
// Different answer: T's flag is per applied aura, F's is the spell's.
inline bool AuraIsPositiveCompat(Aura const* aura) { return aura->GetSpellInfo()->IsPositive(); }
// T Aura::IsAreaAura (SpellAuras.h:514); F: the spell carries an area aura effect (SpellInfo.h:462).
inline bool AuraIsAreaCompat(Aura const* aura) { return aura->GetSpellInfo()->HasAreaAuraEffect(); }

// ---------------------------------------------------------------------------------------------------------------
// Immunity. T Unit::IsImmuneToSpell(spell, castOnSelf), IsImmuneToSpellEffect(spell, index, castOnSelf),
// IsImmuneToDamage(school, spell); F Unit.h:1723-1731 asks for the caster. castOnSelf says the caster is the target itself.
// Different answers: F also answers from the caster (its own immunity-ignoring auras, dispel-type rules); the effect mask is
// every effect of the spell (MAX_EFFECT_MASK, as Spell.cpp:3215 asks).
// ---------------------------------------------------------------------------------------------------------------
inline bool IsImmuneToSpellCompat(Unit const* target, SpellEntry const* spell, bool castOnSelf, WorldObject const* caster)
{
    SpellInfo const* info = GetSpellInfoCompat(spell);
    return target->IsImmunedToSpell(info, MAX_EFFECT_MASK, castOnSelf ? target : caster);
}
inline bool IsImmuneToSpellEffectCompat(Unit const* target, SpellEntry const* spell, uint32 effectIndex, bool castOnSelf, WorldObject const* caster)
{
    SpellInfo const* info = GetSpellInfoCompat(spell);
    return target->IsImmunedToSpellEffect(info, info->GetEffect(SpellEffIndex(effectIndex)), castOnSelf ? target : caster);
}
inline bool IsImmuneToDamageCompat(Unit const* target, SpellEntry const* spell, WorldObject const* caster)
{
    return target->IsImmunedToDamage(caster, GetSpellInfoCompat(spell));
}

// ---------------------------------------------------------------------------------------------------------------
// A cast in progress. T's SPELL_STATE_CASTING is set only when a CHANNELED spell starts channeling (T Spell.cpp:3996-3999, the
// cast bar runs in SPELL_STATE_PREPARING); F's state for that is SPELL_STATE_CHANNELING (Spell.h:259). T Spell::GetCastedTime
// (Spell.h:429) is the m_timer of the spell (remaining cast time, or remaining channel time), F's GetTimer (Spell.h:728) the same field.
// ---------------------------------------------------------------------------------------------------------------
#define SPELL_STATE_CASTING SPELL_STATE_CHANNELING
#define GetCastedTime GetTimer

// The cost of a spell in its own power type. T Spell::CalculatePowerCost (Spell.h:373); F SpellInfo::CalcPowerCost (SpellInfo.h:578),
// asked for the spell's power type, 0 when the spell has no cost in it.
inline uint32 SpellPowerCostCompat(SpellEntry const* spell, Unit const* caster)
{
    SpellInfo const* info = GetSpellInfoCompat(spell);
    Optional<SpellPowerCost> cost = info->CalcPowerCost(Powers(spell->powerType), false, caster, info->GetSchoolMask());
    return cost ? uint32(cost->Amount) : 0u;
}

// The range of a spell changed by the caster's spell mods. T Player::ApplySpellMod(spellId, SPELLMOD_RANGE, value); F Player.h:2142
// with the SpellInfo and SpellModOp::Range (SpellDefines.h:159).
inline void ApplyRangeSpellModCompat(Player* modOwner, SpellEntry const* spell, float& range)
{
    modOwner->ApplySpellMod(GetSpellInfoCompat(spell), SpellModOp::Range, range);
}

// The diminishing-returns group of a spell. T SpellEntry::GetDiminishingReturnsGroup(triggered) separates the "triggered stun" groups
// by the flag; F SpellInfo::GetDiminishingReturnsGroupForSpell() (SpellInfo.h:602) keeps the vanilla groups (SharedDefines.h:6654-6679)
// and takes no flag. Different answer: the triggered flag is not passed. The group names the bot tests are F's DIMINISHING_STUN
// and DIMINISHING_KIDNEY_SHOT.
inline DiminishingGroup DiminishingGroupOfSpellCompat(SpellEntry const* spell, bool /*triggered*/)
{
    return GetSpellInfoCompat(spell)->GetDiminishingReturnsGroupForSpell();
}
#define DIMINISHING_CONTROL_STUN DIMINISHING_STUN
#define DIMINISHING_KIDNEYSHOT DIMINISHING_KIDNEY_SHOT

// "This creature is a world boss by rank". T `creature->GetCreatureInfo()->rank == CREATURE_ELITE_WORLDBOSS` (T CreatureInfo.h); F has
// no such rank (the classification list ends at MinusMob, SharedDefines.h:5181-5189) and keeps the fact as the boss type flag, which
// Creature::isWorldBoss reads (Creature.cpp:2637, the same member the shim's IsWorldBoss entry uses).
// Different answer: rank against type flag, and F never answers true for a pet.
inline bool CreatureIsWorldBossRankCompat(Creature const* creature) { return creature->isWorldBoss(); }

// ---------------------------------------------------------------------------------------------------------------
// Teleports and logout the bot acknowledges for itself, through the core's own handlers (the way forever/src/TbBotSession.cpp does
// for the spike: AcknowledgeTeleports). T hand-builds MSG_MOVE_TELEPORT_ACK (guid, the counter the core waits for, a time) and
// calls HandleMoveTeleportAckOpcode; F's handler takes the typed packet (WorldSession.h:1367) and needs the mover guid only
// (TbBotSession.cpp:69-72). The far teleport: T HandleMoveWorldportAckOpcode() with no argument, F HandleMoveWorldportAck()
// (WorldSession.h:1446, "for server-side calls"). Logout cancel: T takes any packet (the handler reads none), F the typed
// LogoutCancel (WorldSession.h:1394).
// ---------------------------------------------------------------------------------------------------------------
inline void AckNearTeleportCompat(Player* bot)
{
    WorldPackets::Movement::MoveTeleportAck ack{ WorldPacket(CMSG_MOVE_TELEPORT_ACK) };
    ack.MoverGUID = bot->GetGUID();
    bot->GetSession()->HandleMoveTeleportAck(ack);
}
inline void AckFarTeleportCompat(Player* bot) { bot->GetSession()->HandleMoveWorldportAck(); }
inline void CancelLogoutCompat(Player* bot)
{
    WorldPackets::Character::LogoutCancel cancel{ WorldPacket(CMSG_LOGOUT_CANCEL) };
    bot->GetSession()->HandleLogoutCancelOpcode(cancel);
}

// ---------------------------------------------------------------------------------------------------------------
// Chat, one helper per kind of message. T hand-builds the packets (ChatHandler::BuildChatPacket, or a CMSG_MESSAGECHAT the
// session reads on its next update); F has the typed packet (ChatPackets::Chat, ChatPackets.h:151-171), the unit's Say / Yell /
// Whisper (Player.h:1411-1421) and the session's own chat entry (WorldSession::HandleChatMessage, WorldSession.h:1704).
// Different answers, all kinds: F's Say / Yell / Whisper run the core's language and ignore-list rules; the "like a player" kinds
// run now (F) and not on the next session update (T).
// ---------------------------------------------------------------------------------------------------------------
inline void BotSayCompat(Player* bot, std::string const& text, uint32 lang) { bot->Say(text, Language(lang)); }
inline void BotYellCompat(Player* bot, std::string const& text, uint32 lang) { bot->Yell(text, Language(lang)); }
// A whisper from `bot` to `receiver` (T built the packet itself and sent it to the receiver only; F's Whisper also tells the sender).
inline void BotWhisperCompat(Player* bot, Player* receiver, std::string const& text)
{
    bot->Whisper(text, LANG_UNIVERSAL, receiver);
}
// A line in the party or raid chat, shown to each listed player. T BuildChatPacket + SendPacket per receiver; F one SMSG_CHAT per
// receiver (Initialize sets the receiver for the locale).
inline void BotGroupChatCompat(Player* bot, ChatMsg chatType, std::string const& text, std::vector<Player*> const& receivers)
{
    for (Player* receiver : receivers)
    {
        WorldPackets::Chat::Chat packet;
        packet.Initialize(chatType, LANG_UNIVERSAL, bot, receiver, text);
        receiver->SendDirectMessage(packet.Write());
    }
}
// The bot says the text as a player would, in the given chat (say, yell, party, guild). T queues the CMSG_MESSAGECHAT a client
// would send; F calls the session's chat entry the opcode handler calls.
inline void BotChatLikePlayerCompat(Player* bot, ChatMsg chatType, uint32 lang, std::string const& text)
{
    bot->GetSession()->HandleChatMessage(chatType, Language(lang), text);
}
// The queue a raw packet goes to. T QueuePacket(WorldPacket*); F QueuePacket(WorldPacket&&) (WorldSession.h:1109). Transport only:
// the layout of the packet is its builder's.
inline void BotQueuePacketCompat(Player* bot, WorldPacket const& packet) { bot->GetSession()->QueuePacket(WorldPacket(packet)); }

// ---------------------------------------------------------------------------------------------------------------
// Players by guid or name. T asks the object manager (ObjectMgr.cpp:658-722: the name table, the cached player data, the online
// player's session); F's character cache has the same three answers (CharacterCache.h:72-75). T ObjectMgr::GetPlayer(guid) is
// ObjectAccessor::FindPlayer (ObjectMgr.h:701), the same call F has (ObjectAccessor.h:82). A player guid from a database
// counter: T ObjectGuid(HIGHGUID_PLAYER, low), F ObjectGuid::Create<HighGuid::Player>(low).
// ---------------------------------------------------------------------------------------------------------------
inline uint32 PlayerAccountIdCompat(ObjectGuid guid) { return sCharacterCache->GetCharacterAccountIdByGuid(guid); }
inline bool PlayerNameByGuidCompat(ObjectGuid guid, std::string& name) { return sCharacterCache->GetCharacterNameByGuid(guid, name); }
inline ObjectGuid PlayerGuidByNameCompat(std::string const& name) { return sCharacterCache->GetCharacterGuidByName(name); }
inline Player* PlayerByGuidCompat(ObjectGuid guid) { return ObjectAccessor::FindPlayer(guid); }
inline ObjectGuid PlayerGuidCompat(uint64 low) { return ObjectGuid::Create<HighGuid::Player>(low); }

// A session that belongs to a headless bot (no client). T WorldSession::IsHeadless; F IsBot (WorldSession.h:1078), the flag
// the bot session sets when it is made. Different answer: T's flag is set by the Turtle bot manager, F's by BotSession.
inline bool IsHeadlessCompat(WorldSession* session) { return session->IsBot(); }

// ---------------------------------------------------------------------------------------------------------------
// Movement flag names. T names (T Object.h:212-238) against F MovementFlags (MovementInfo.h:29-70): WALK_MODE is WALKING (0x100 on
// both), FALLINGFAR is FALLING_FAR (T 0x4000, F 0x1000: the same fact), JUMPING is the flag the client sets while it is in a jump
// or fall, which F calls FALLING (T JUMPING 0x2000; the 3.3.5 core's FALLING is 0x2000 too). F has no ONTRANSPORT flag (a
// transport is the movement info's transport guid): that name is not defined here.
// ---------------------------------------------------------------------------------------------------------------
#define MOVEFLAG_NONE MOVEMENTFLAG_NONE
#define MOVEFLAG_BACKWARD MOVEMENTFLAG_BACKWARD
#define MOVEFLAG_WALK_MODE MOVEMENTFLAG_WALKING
#define MOVEFLAG_FALLINGFAR MOVEMENTFLAG_FALLING_FAR
#define MOVEFLAG_JUMPING MOVEMENTFLAG_FALLING
