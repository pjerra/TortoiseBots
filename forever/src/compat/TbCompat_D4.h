// ForeverCore side of the both-core helpers and names of slice 4, package D4 (instance and death: the dead strategy, release spirit, accept
// resurrect, the priest's party-member-to-resurrect value). Owned by package P4: no other package edits this file.
// Turtle side: ai/cmangos-compat-d4.h (same names and signatures, each forwards to the call the bot always made).
// Notes: forever/docs/SLICE4-P-NOTES.md (every helper below, its Turtle side and the core line it calls).
#pragma once

#include "MiscPackets.h"           // WorldPackets::Misc::RepopRequest / ResurrectResponse (the typed packets of the two handlers)

// ===============================================================================================================
// RELEASE SPIRIT (ReleaseSpiritAction.h: ReleaseSpiritAction, AutoReleaseSpiritAction)
// ===============================================================================================================

// Release the spirit: the core's own handler, the one a client's "Release Spirit" ends in (WorldSession::HandleRepopRequest,
// MiscHandler.cpp:63-93; THREADUNSAFE, Opcodes.cpp:957: it runs here on the world thread, where those handlers normally run: engine and host
// commands run with the maps idle, World.cpp:2490-2500). The typed packet carries one bit (CheckInstance) the handler never reads.
// T: the 1.12 packet, `CMSG_REPOP_REQUEST` + uint8(0), HandleRepopRequestOpcode (MiscHandler.cpp:61), and `true`: the original never looked
// at the outcome. F: the handler returns without doing anything when the player is alive or already a ghost (:65), is being teleported (:71:
// a second release while the first one's teleport runs) or carries SPELL_AURA_PREVENT_RESURRECTION (:74); so the answer is what the player
// is afterwards: a ghost (BuildPlayerRepop casts the Ghost aura 8326, Player.cpp:4705, which sets PLAYER_FLAGS_GHOST) or alive again (inside a
// dungeon RepopAtGraveyard teleports with TELE_REVIVE_AT_TELEPORT, Player.cpp:5275, and a same-map teleport revives the player at once at 50 %:
// Player.cpp:1399; a far teleport revives it when the worldport lands, MovementHandler.cpp:333). A caller that gets false knows nothing changed.
inline bool RepopRequestCompat(Player* bot)
{
    WorldSession* const session = bot->GetSession();
    if (!session)
        return false;
    WorldPackets::Misc::RepopRequest packet{ WorldPacket(CMSG_REPOP_REQUEST) };
    session->HandleRepopRequest(packet);
    return bot->IsAlive() || bot->HasPlayerFlag(PLAYER_FLAGS_GHOST);
}

// Does a release inside this bot's dungeon come back at the dungeon's entrance? F: RepopAtGraveyard (Player.cpp:5232-5283) revives every
// released player of a dungeon or raid map at once (`shouldResurrect`, :5218-5224) and teleports it to the world safe location the
// INSTANCE SCRIPT names (`GetWorldSafeLoc(instance->GetEntranceLocation())`, :5230); with none set it falls back to the nearest
// graveyard of the player's zone (:5233), which for Ragefire Chasm is Razor Hill (Durotar: outside, spec section 1). RFC's script never
// sets one: the host does, once, from the trigger's own target (spec section 6). Until then the answer is false and `auto release` waits at
// the body (relhold=no-entrance): never a graveyard revive in another zone. The entrance must be a real row (`ObjectMgr::GetWorldSafeLoc`,
// ObjectMgr.h:1199), as the core's own lookup is. Anything that is not a dungeon or raid map has no entrance: true. A read, no state changed.
// T: always true (the original released unconditionally).
inline bool InstanceEntranceKnownCompat(Player* bot)
{
    Map const* const map = bot->FindMap();
    if (!map || !map->IsDungeon())
        return true;
    InstanceScript const* const script = bot->GetInstanceScript();
    return script && sObjectMgr.GetWorldSafeLoc(script->GetEntranceLocation()) != nullptr;
}

// N3 (package N): does this living party member KNOW an out-of-combat resurrection? T: always true, the legacy rule counted every priest, paladin, shaman and
// druid (the class switch stays at the call site, so the Turtle behaviour is unchanged). F: the host answers from the member's own spell book through the
// legacy names (priest "resurrection", paladin "redemption", shaman "ancestral spirit", druid "revive": the action names PriestActions.h:214,
// PaladinActions.h:712, ShamanActions.h:504, DruidActions.h:110); a real player is counted by class (nothing to ask), a host bot with no engine cannot raise.
namespace TortoiseBots { namespace h3 { bool MemberKnowsOocResurrection(Player* member); } }
inline bool MemberKnowsOocResurrectionCompat(Player* member) { return TortoiseBots::h3::MemberKnowsOocResurrection(member); }

// "Waiting to Resurrect" (spell 2584) on a bot that released inside a battleground. T: the original self cast with the old triggered flag.
// F: the core puts the aura on a player itself when its spirit guide is chosen (Player::SetAreaSpiritHealer, Player.cpp:33626) and the bot
// cannot choose one: battlegrounds are out of the port (a later slice), counted and not cast: the bot does LESS.
inline void WaitingForResurrectAuraCompat(Player* /*bot*/) { tbcompat::Unported(tbcompat::Feature::BattlegroundRelease); }

// ===============================================================================================================
// RESURRECTION REQUEST (AcceptResurrectAction.h, PartyMemberToResurrect.cpp)
// ===============================================================================================================

// T `Player::IsRessurectRequested()` (the 1.12 spelling), F `Player::IsResurrectRequested()` (Player.h:2167: the pending-request record exists).
inline bool IsResurrectRequestedCompat(Player const* player) { return player->IsResurrectRequested(); }

// The player that offered the resurrection: the first guid of the event's packet, as the original read it (`WorldPacket p(event.GetPacket());
// p.rpos(0); p >> guid`). T's packet is the server's SMSG_RESURRECT_REQUEST (the offerer's guid first); on F the host captures that packet in
// the session's send path and raises the event with the offerer's guid as the packet (`Event("resurrect request", offerer)` writes it:
// Event.h), so the same first guid is the offerer. `Event::getObject` reads exactly that (Event.cpp:9) and answers an empty guid for an
// event with no packet, which the accept handler refuses (IsResurrectRequestedBy is false for an empty guid, Player.h:2159).
template <class EventType>
inline ObjectGuid ResurrectOffererCompat(EventType& event) { return event.getObject(); }

// May this bot take a resurrection from this player? T: yes, from anyone (the legacy bot accepts every request: `true`). F (lead decision,
// spec section 3 fallbacks): only from a member of the bot's own group or from its commander (the master it is bound to); a request from
// anybody else is declined (DeclineResurrectCompat). Less than T, never more.
template <class AI>
inline bool ResurrectOffererAcceptableCompat(AI* ai, ObjectGuid const& offerer)
{
    Player* const bot = ai->GetBot();
    if (!bot || offerer.IsEmpty())
        return false;
    if (Group* const group = bot->GetGroup())
        if (group->IsMember(offerer))
            return true;
    Player* const master = ai->GetMaster();
    return master && master->GetGUID() == offerer;
}

// Answer a resurrection request through the core's own handler (WorldSession::HandleResurrectResponse, MiscHandler.cpp:507-534; THREADUNSAFE,
// Opcodes.cpp:1002: world thread, as above). POLARITY DIFFERS: T's packet is guid + uint8 with 1 = accept and 0 = decline (T
// HandleResurrectResponseOpcode: `if (status == 0) ClearResurrectRequestData()`), F's typed packet has Response 0 = ACCEPT, 1 = decline, 2 =
// timeout (the handler: `if (packet.Response != 0) ClearResurrectRequestData()`, MiscHandler.cpp:511-515). Both helpers below set the
// field the handler reads, so `AcceptResurrectCompat` accepts on both cores.
// What accepting does on F (the answer to "how does a session with no client accept"): the handler checks that a request FROM THIS offerer is
// pending (:517), and if the offerer is in the world looks at its instance's combat-resurrection charges (:519-529: only when the config
// asks for them AND an encounter is in progress; the RFC script has none), then ResurrectUsingRequestData (Player.cpp:28232): TeleportTo the
// caster's position at cast time, and when that is a near teleport (the same map) the resurrect is DELAYED to the teleport's acknowledgement
// (DELAYED_RESURRECT_PLAYER, :28084-28087, run by ProcessDelayedOperations, Player.cpp:1569, which the teleport ack calls, MovementHandler.cpp:479): the host session acknowledges near teleports
// by itself (TbBotSession.cpp AcknowledgeTeleports), so the bot is alive one tick later at the offerer's position. The return value says
// whether the answer was GIVEN: T `true` as it always was; F `false` (nothing done) when the bot is not dead or has no request from that
// offerer, so the caller never reports an acceptance the core did not take. "Alive" is not known yet at return (delayed), the action's
// result means "accepted and handed to the core".
inline bool AcceptResurrectCompat(Player* bot, ObjectGuid const& offerer)
{
    WorldSession* const session = bot->GetSession();
    if (!session || bot->IsAlive() || !bot->IsResurrectRequestedBy(offerer))
        return false;
    WorldPackets::Misc::ResurrectResponse packet{ WorldPacket(CMSG_RESURRECT_RESPONSE) };
    packet.Resurrecter = offerer;
    packet.Response = 0;                                // 0 = accept on F (T: 1)
    session->HandleResurrectResponse(packet);
    return true;
}

// Decline: T's uint8(0), F's Response 1 (the handler clears the request). Only reached on F (T accepts everyone).
inline bool DeclineResurrectCompat(Player* bot, ObjectGuid const& offerer)
{
    WorldSession* const session = bot->GetSession();
    if (!session || bot->IsAlive() || !bot->IsResurrectRequestedBy(offerer))
        return false;
    WorldPackets::Misc::ResurrectResponse packet{ WorldPacket(CMSG_RESURRECT_RESPONSE) };
    packet.Resurrecter = offerer;
    packet.Response = 1;                                // 1 = decline on F (T: 0)
    session->HandleResurrectResponse(packet);
    return true;
}

// ===============================================================================================================
// SELF RESURRECTION (SelfResurrectAction in ReleaseSpiritAction.h; the action is NOT registered in the slice)
// ===============================================================================================================

// T: PLAYER_SELF_RES_SPELL != 0 (soulstone, ankh); the packet CMSG_SELF_RES. F keeps its self-resurrection spells in the player's own list
// (InitializeSelfResurrectionSpells, Player.cpp:1237) and takes the spell in the typed packet: a later slice. Not possible, not done, counted.
inline bool SelfResurrectPossibleCompat(Player* /*bot*/) { tbcompat::Unported(tbcompat::Feature::SelfResurrection); return false; }
inline bool SelfResurrectCompat(Player* /*bot*/) { tbcompat::Unported(tbcompat::Feature::SelfResurrection); return false; }

// ===============================================================================================================
// REPOP (RepopAction in ReleaseSpiritAction.h: a random bot without a master goes home; the action is NOT registered in the slice)
// ===============================================================================================================

// Remove a member from the bot's group. T `Group::RemoveMember(guid, 0)` (0 = the default removal), F `RemoveMember(guid, GROUP_REMOVEMETHOD_DEFAULT)`
// (Group.h; the same default removal).
inline void GroupRemoveMemberCompat(Group* group, ObjectGuid const& guid) { group->RemoveMember(guid, GROUP_REMOVEMETHOD_DEFAULT); }

// The graveyard nearest the bot for its team. T `sObjectMgr.GetClosestGraveYard(x, y, z, map, team)` (zone-linked, not proximity-bounded: the
// caller's own distance test stays). F `ObjectMgr::GetClosestGraveyard(location, team, conditionObject)` (ObjectMgr.cpp:7330: the same zone
// link, and the graveyard rows' conditions, e.g. the Horde-only Northern Durotar yard). A read; null when there is none.
inline WorldSafeLocsEntry const* ClosestGraveyardCompat(Player* bot)
{
    return sObjectMgr.GetClosestGraveyard(*bot, bot->GetTeam(), bot);
}

// T: a Goblin or High Elf bot starts in a custom zone the bot pool excludes, so it comes back through its homebind instead of the racial start
// point (RepopAction). F: those two custom starts do not exist; every bot has its racial start (PlayerInfo createPosition).
inline bool RepopUsesHomebindCompat(Player* /*bot*/) { return false; }

// Teleport to the homebind. T `TeleportToHomebind(options, hearthCooldown)` then `SendHeartBeat()` (the caller sends the beat through
// SendHeartBeatCompat). F: the core's own homebind field, the target RepopAtGraveyard uses itself for a ghost under the map
// (Player.cpp:5285); a teleport there has no hearthstone cooldown, so the flag only exists to keep the signature shared.
inline bool TeleportToHomebindCompat(Player* bot, bool /*hearthCooldown*/ = true) { return bot->TeleportTo(bot->m_homebind); }
