// ForeverCore side of the both-core helpers and names of slice 1, round R4P (the PlayerbotAI class).
// Owned by package R4P: no other package edits this file. Turtle side: ai/cmangos-compat-r4p.h (same names and
// signatures, each forwarding to the call the bot always made).
//
// Notation: T = the Turtle core (~/playerbots-references/penqle-tortoise-wow-modsys/src/game), F = the pinned ForeverCore
// (~/ForeverCore/src/server/game). Where the two cores answer the same question with different numbers the entry says so
// and the notes (forever/docs/SLICE1-R4P-NOTES.md, "Different answers") repeat it.
#pragma once

// The typed packet classes and the trade data the helpers below use (the slice census compiles with these on the include path, as
// forever/src/TbBotSession.cpp includes them).
#include "CharacterPackets.h"
#include "ChatPackets.h"
#include "MovementPackets.h"
#include "TradeData.h"

// "The player stands in a sanctuary." T: the PLAYER_FLAGS_SANCTUARY player flag (0x10000, T Player.h:399). F: that bit is now
// LOW_LEVEL_RAID_ENABLED (Player.h:543, "pre-3.0.3 PLAYER_FLAGS_SANCTUARY"), so the unit's sanctuary pvp flag answers the question
// (Unit.h:912). Different answer: the flag is set by the area in the one core and by the unit's pvp state in the other.
inline bool PlayerInSanctuaryCompat(Player const* player) { return player->IsInSanctuary(); }

// The experience still needed for the next level. T update field PLAYER_NEXT_LEVEL_XP, F Player.h:1354.
inline uint32 PlayerNextLevelXPCompat(Player const* player) { return player->GetXPForNextLevel(); }

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
// grep over ai/ runtime/ host/ commands/). T Player.h / Unit.h against F Player.h:1307 (isAFK), :1340 (isGMVisible),
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
// Spell casting. T builds `new Spell(caster, SpellEntry const*, bool triggered)` (Spell.h:336) and fills a SpellCastTargets
// with set*Target / setDestination; F builds `Spell(WorldObject*, SpellInfo const*, TriggerCastFlags)` (Spell.h:475) and its
// targets have SetUnitTarget / SetGOTarget / SetItemTarget / SetDst (Spell.cpp:199-352). `triggered` is the full triggered mask:
// the cast ignores cost, cooldown, cast time and the other checks (the old core's "triggered" flag did the same).
// ---------------------------------------------------------------------------------------------------------------
inline Spell* NewSpellCompat(Unit* caster, SpellEntry const* info, bool triggered)
{
    return new Spell(caster, GetSpellInfoCompat(info), triggered ? TRIGGERED_FULL_MASK : TRIGGERED_NONE);
}

// T Spell::SetCastItem (Spell.h:492) stores the pointer; F has the public member the cast reads in prepare (Spell.cpp:3494-3497).
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
// T Aura::IsAreaAura (SpellAuras.h:514); F: the spell carries an area aura effect (SpellInfo.h:465).
inline bool AuraIsAreaCompat(Aura const* aura) { return aura->GetSpellInfo()->HasAreaAuraEffect(); }

// ---------------------------------------------------------------------------------------------------------------
// Immunity. T Unit::IsImmuneToSpell(spell, castOnSelf), IsImmuneToSpellEffect(spell, index, castOnSelf),
// IsImmuneToDamage(school, spell); F Unit.h:1739-1747 asks for the caster. castOnSelf says the caster is the target itself.
// Different answers: F also answers from the caster (its own immunity-ignoring auras, dispel-type rules); the effect mask is
// every effect of the spell (MAX_EFFECT_MASK, as Spell.cpp:3220 asks).
// ---------------------------------------------------------------------------------------------------------------
inline bool IsImmuneToSpellCompat(Unit const* target, SpellEntry const* spell, bool castOnSelf, WorldObject const* caster)
{
    SpellInfo const* info = GetSpellInfoCompat(spell);
    return target->IsImmunedToSpell(info, MAX_EFFECT_MASK, castOnSelf ? target : caster);
}
inline bool IsImmuneToSpellEffectCompat(Unit const* target, SpellEntry const* spell, uint32 effectIndex, bool castOnSelf, WorldObject const* caster)
{
    SpellInfo const* info = GetSpellInfoCompat(spell);
    // SpellInfo::GetEffect ASSERTs index < effect count (SpellInfo.h:602) and the core's ASSERT aborts a release build; the bot asks
    // for effects 0..2 of every spell, so a spell with fewer effects answers "not immune" (it has no such effect to be immune to).
    if (!info || effectIndex >= info->GetEffects().size())
        return false;
    SpellEffectInfo const& effect = info->GetEffect(SpellEffIndex(effectIndex));
    // Slice 8 (K8 notes section 4, H8): the core refuses an interrupt on a unit whose mechanic immunity holds MECHANIC_INTERRUPT (SpellInfo::CanBeInterrupted,
    // SpellInfo.cpp:1814); the engine's own read of effect immunity does not know it, so a Kick at such a mob was cast and wasted. Immune = the bot does less.
    if (effect.Effect == SPELL_EFFECT_INTERRUPT_CAST && (target->GetMechanicImmunityMask() & (uint64(1) << MECHANIC_INTERRUPT)))
        return true;
    return target->IsImmunedToSpellEffect(info, effect, castOnSelf ? target : caster);
}
inline bool IsImmuneToDamageCompat(Unit const* target, SpellEntry const* spell, WorldObject const* caster)
{
    return target->IsImmunedToDamage(caster, GetSpellInfoCompat(spell));
}

// The diminishing-returns group of a spell. T SpellEntry::GetDiminishingReturnsGroup(triggered) separates the "triggered stun" groups
// by the flag; F SpellInfo::GetDiminishingReturnsGroupForSpell() (SpellInfo.h:605) keeps the vanilla groups (SharedDefines.h:6654-6679)
// and takes no flag. Different answer: the triggered flag is not passed. The group names the bot tests are F's DIMINISHING_STUN
// and DIMINISHING_KIDNEY_SHOT.
inline DiminishingGroup DiminishingGroupOfSpellCompat(SpellEntry const* spell, bool /*triggered*/)
{
    return GetSpellInfoCompat(spell)->GetDiminishingReturnsGroupForSpell();
}
#define DIMINISHING_CONTROL_STUN DIMINISHING_STUN
#define DIMINISHING_KIDNEYSHOT DIMINISHING_KIDNEY_SHOT

// ---------------------------------------------------------------------------------------------------------------
// Teleports and logout the bot acknowledges for itself, through the core's own handlers (the way forever/src/TbBotSession.cpp does
// for the spike: AcknowledgeTeleports). T hand-builds MSG_MOVE_TELEPORT_ACK (guid, the counter the core waits for, a time) and
// calls HandleMoveTeleportAckOpcode; F's handler takes the typed packet (WorldSession.h:1377) and needs the mover guid only
// (TbBotSession.cpp:69-72). The far teleport: T HandleMoveWorldportAckOpcode() with no argument, F HandleMoveWorldportAck()
// (WorldSession.h:1464, "for server-side calls"). Logout cancel: T takes any packet (the handler reads none), F the typed
// LogoutCancel (WorldSession.h:1404).
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
// Whisper (Player.h:1412-1422) and the session's own chat entry (WorldSession::HandleChatMessage, WorldSession.h:1723).
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
// The queue a raw packet goes to. T QueuePacket(WorldPacket*); F QueuePacket(WorldPacket&&) (WorldSession.h:1112). Transport only:
// the layout of the packet is its builder's.
inline void BotQueuePacketCompat(Player* bot, WorldPacket const& packet) { bot->GetSession()->QueuePacket(WorldPacket(packet)); }

inline bool PlayerNameByGuidCompat(ObjectGuid guid, std::string& name) { return sCharacterCache->GetCharacterNameByGuid(guid, name); }
inline ObjectGuid PlayerGuidByNameCompat(std::string const& name) { return sCharacterCache->GetCharacterGuidByName(name); }
inline ObjectGuid PlayerGuidCompat(uint32 low) { return ObjectGuid::Create<HighGuid::Player>(low); }

// A session that belongs to a headless bot (no client). T WorldSession::IsHeadless; F IsBot (WorldSession.h:1079), the flag
// the bot session sets when it is made. Different answer: T's flag is set by the Turtle bot manager, F's by BotSession.
inline bool IsHeadlessCompat(WorldSession* session) { return session->IsBot(); }

// ---------------------------------------------------------------------------------------------------------------
// Movement flag names. T names (T Object.h:212-238) against F MovementFlags (MovementInfo.h:29-70): WALK_MODE is WALKING (0x100 on
// both), FALLINGFAR is FALLING_FAR (T 0x4000, F 0x1000: the same fact), JUMPING is the flag the client sets while it is in a jump
// or fall, which F calls FALLING (T JUMPING 0x2000; the 3.3.5 core's FALLING is 0x2000 too). F has no ONTRANSPORT flag (a
// transport is the movement info's transport guid): that name is not defined here.
// ---------------------------------------------------------------------------------------------------------------

// The reaction of one faction template to another: the algorithm of PlayerbotAI::GetFactionReaction (a copy of the old core's
// reputation logic, ai/cmangos-compat-r4p.h keeps it word for word) over F's field names (DB2Structure.h:1712-1721): ourMask is
// FactionGroup, friendlyMask FriendGroup, hostileMask EnemyGroup, enemyFaction Enemies, friendFaction Friend, faction Faction.
// Different answer from the core's own IsHostileTo / IsFriendlyTo (DB2Structure.h:1726-1752): the same tests in another order
// (the mask test first here), kept as the bot always read them.
inline ReputationRank FactionReactionCompat(FactionTemplateEntry const* thisTemplate, FactionTemplateEntry const* otherTemplate)
{
    ASSERT(thisTemplate);
    ASSERT(otherTemplate);

    if (otherTemplate->FactionGroup & thisTemplate->EnemyGroup)
        return REP_HOSTILE;

    if (thisTemplate->Enemies[0] && otherTemplate->Faction)
    {
        for (unsigned int i : thisTemplate->Enemies)
        {
            if (i == otherTemplate->Faction)
                return REP_HOSTILE;
        }
    }

    if (otherTemplate->FactionGroup & thisTemplate->FriendGroup)
        return REP_FRIENDLY;

    if (thisTemplate->Friend[0] && otherTemplate->Faction)
    {
        for (unsigned int i : thisTemplate->Friend)
        {
            if (i == otherTemplate->Faction)
                return REP_FRIENDLY;
        }
    }

    if (thisTemplate->FactionGroup & otherTemplate->FriendGroup)
        return REP_FRIENDLY;

    if (otherTemplate->Friend[0] && thisTemplate->Faction)
    {
        for (unsigned int i : otherTemplate->Friend)
        {
            if (i == thisTemplate->Faction)
                return REP_FRIENDLY;
        }
    }
    return REP_NEUTRAL;
}

// ---------------------------------------------------------------------------------------------------------------
// Other names. T Common.h:292 `countof(array)`; ForeverCore has none. T Unit.h SPELL_AURA_OBS_MOD_MANA = 21 (SpellAuraDefines.h:134),
// F SPELL_AURA_OBS_MOD_POWER = 21 (SpellAuraDefines.h:108): the periodic percent mana regeneration of drinks. T InBattleGroundQueue,
// F InBattlegroundQueue (casing only; used as `x->InBattleGroundQueue()`).
// ---------------------------------------------------------------------------------------------------------------
#ifndef countof
#define countof(array) (sizeof(array) / sizeof((array)[0]))
#endif
#define SPELL_AURA_OBS_MOD_MANA SPELL_AURA_OBS_MOD_POWER
#define InBattleGroundQueue InBattlegroundQueue

// A player by guid, also when it is connected but not in the world yet. T ObjectAccessor::FindPlayerNotInWorld; F FindConnectedPlayer
// (ObjectAccessor.h:87, the call the shim's own FindPlayerNotInWorld member makes).
inline Player* FindPlayerNotInWorldCompat(ObjectGuid guid) { return ObjectAccessor::FindConnectedPlayer(guid); }

// The speed multiplier of the unit's casting: T update field UNIT_MOD_CAST_SPEED (1.0 normal, haste below it: Unit.cpp:10395-10397), F
// UnitData::ModCastingSpeed (UpdateFields.h:387). The bot scales the 1-1.5 s global cooldown with it.
inline float UnitCastSpeedModCompat(Unit const* unit) { return *unit->m_unitData->ModCastingSpeed; }

// The generator on top of the unit's motion stack, or null when there is none. T MotionMaster::empty()/top(), F Empty() and
// GetCurrentMovementGenerator() (MotionMaster.h:125-129).
inline MovementGenerator* MotionMasterTopCompat(MotionMaster* master)
{
    return master->Empty() ? nullptr : master->GetCurrentMovementGenerator();
}

// The auras applied to a unit, as (spell id, aura) pairs. T Player::GetSpellAuraHolderMap is a map of SpellAuraHolder (the holder of one
// spell); F keeps AuraApplication per applied aura (Unit.h:1335), whose GetBase() is the Aura (SpellAuras.h:134: GetAura() of the
// application). The aura answers what the bot asks the holder (GetSpellProto, caster, stacks, duration).
inline std::vector<std::pair<uint32, Aura*>> AuraHolderMapCompat(Unit const* unit)
{
    std::vector<std::pair<uint32, Aura*>> result;
    for (auto const& entry : unit->GetAppliedAuras())
        result.emplace_back(entry.first, entry.second->GetBase());
    return result;
}

// The zone at a point. T sTerrainMgr.getZoneId(map, x, y, z), F TerrainMgr::GetZoneId with the empty phase shift (TerrainMgr.h:151).
inline uint32 ZoneIdAtCompat(uint32 mapId, float x, float y, float z)
{
    return sTerrainMgr.GetZoneId(PhasingHandler::GetEmptyPhaseShift(), mapId, x, y, z);
}

// ---------------------------------------------------------------------------------------------------------------
// The bot cheats (health, cooldown, repair; BotCheatMask). T Unit::SetHealthPercent (Unit.h:446), Unit::RemoveAllSpellCooldown
// (Unit.h:1033), Player::DurabilityRepairAll(cost, discount) (Player.h:1443, returns the cost); F: health as a share of the maximum,
// SpellHistory::ResetAllCooldowns (SpellHistory.h:170), Player::DurabilityRepairAll(takeCost, discountMod, guildBank)
// (Player.h:2456, no result). The repair cheat does not use the cost.
// ---------------------------------------------------------------------------------------------------------------
inline void SetHealthPercentCompat(Unit* unit, float percent) { unit->SetHealth(uint32(unit->GetMaxHealth() * percent / 100.0f)); }
inline void RemoveAllSpellCooldownCompat(Unit* unit) { unit->GetSpellHistory()->ResetAllCooldowns(); }
inline void DurabilityRepairAllCompat(Player* player, bool takeCost, float discountMod) { player->DurabilityRepairAll(takeCost, discountMod, false); }
