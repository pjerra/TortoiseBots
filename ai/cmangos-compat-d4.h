// The Turtle side of the both-core helpers the ForeverCore port added in slice 4, package D4 (instance and death: the dead strategy, release
// spirit, accept resurrect, the priest's party-member-to-resurrect value).
// Same names and signatures as forever/src/compat/TbCompat_D4.h; each forwards to the call the bot always made, word for word (the notes,
// forever/docs/SLICE4-P-NOTES.md, section 3, name the original line of every helper). Owned by package P4: no other package edits this file.
#pragma once

// ===============================================================================================================
// RELEASE SPIRIT (ReleaseSpiritAction.h)
// ===============================================================================================================

// ReleaseSpiritAction::Execute and AutoReleaseSpiritAction::Execute: the 1.12 packet and the handler, then `true` (the original never looked at the outcome).
inline bool RepopRequestCompat(Player* bot)
{
    WorldPacket packet(CMSG_REPOP_REQUEST);
    packet << uint8(0);
    bot->GetSession()->HandleRepopRequestOpcode(packet);
    return true;
}

// AutoReleaseSpiritAction::isUseful: T has no entrance concept to wait for, the release was never held for one (always true).
inline bool InstanceEntranceKnownCompat(Player* /*bot*/) { return true; }

// N3 (package N): a living party member counts as a rezzer when it KNOWS an out-of-combat resurrection. T: always true (the legacy class test stands alone).
inline bool MemberKnowsOocResurrectionCompat(Player* /*member*/) { return true; }

// `bot->CastSpell(bot, 2584, TRIGGERED_OLD_TRIGGERED);` (Waiting to Resurrect, in a battleground).
inline void WaitingForResurrectAuraCompat(Player* bot) { bot->CastSpell(bot, 2584, TRIGGERED_OLD_TRIGGERED); }

// ===============================================================================================================
// RESURRECTION REQUEST (AcceptResurrectAction.h, PartyMemberToResurrect.cpp)
// ===============================================================================================================

// `player->IsRessurectRequested()`.
inline bool IsResurrectRequestedCompat(Player const* player) { return player->IsRessurectRequested(); }

// AcceptResurrectAction::Execute: `WorldPacket p(event.GetPacket()); p.rpos(0); ObjectGuid guid; p >> guid;` (a template: the event class is
// not complete where this header is read).
template <class EventType>
inline ObjectGuid ResurrectOffererCompat(EventType& event)
{
    WorldPacket p(event.GetPacket());
    p.rpos(0);
    ObjectGuid guid;
    p >> guid;
    return guid;
}

// T accepts a resurrection from anyone (nothing in the original asks who offers it).
template <class AI>
inline bool ResurrectOffererAcceptableCompat(AI* /*ai*/, ObjectGuid const& /*offerer*/) { return true; }

// AcceptResurrectAction::Execute: guid + uint8(1) (accept), HandleResurrectResponseOpcode, `true`.
inline bool AcceptResurrectCompat(Player* bot, ObjectGuid const& offerer)
{
    WorldPacket packet(CMSG_RESURRECT_RESPONSE, 8 + 1);
    packet << offerer;
    packet << uint8(1);                                       // accept
    bot->GetSession()->HandleResurrectResponseOpcode(packet); // queue the packet to get around race condition
    return true;
}

// The same packet with uint8(0) (decline: T's handler clears the request). Never called on T (T accepts everyone).
inline bool DeclineResurrectCompat(Player* bot, ObjectGuid const& offerer)
{
    WorldPacket packet(CMSG_RESURRECT_RESPONSE, 8 + 1);
    packet << offerer;
    packet << uint8(0);
    bot->GetSession()->HandleResurrectResponseOpcode(packet);
    return true;
}

// ===============================================================================================================
// SELF RESURRECTION (SelfResurrectAction)
// ===============================================================================================================

// `bot->GetUInt32Value(PLAYER_SELF_RES_SPELL)` and `WorldPacket packet(CMSG_SELF_RES); HandleSelfResOpcode(packet);`.
inline bool SelfResurrectPossibleCompat(Player* bot) { return bot->GetUInt32Value(PLAYER_SELF_RES_SPELL) != 0; }
inline bool SelfResurrectCompat(Player* bot)
{
    WorldPacket packet(CMSG_SELF_RES);
    bot->GetSession()->HandleSelfResOpcode(packet);
    return true;
}

// ===============================================================================================================
// REPOP (RepopAction)
// ===============================================================================================================

// `group->RemoveMember(guid, 0)`.
inline void GroupRemoveMemberCompat(Group* group, ObjectGuid const& guid) { group->RemoveMember(guid, 0); }

// `sObjectMgr.GetClosestGraveYard(bot->GetPositionX(), GetPositionY(), GetPositionZ(), bot->GetMapId(), bot->GetTeam())`.
inline WorldSafeLocsEntry const* ClosestGraveyardCompat(Player* bot)
{
    return sObjectMgr.GetClosestGraveYard(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), bot->GetMapId(), bot->GetTeam());
}

// `bot->GetRace() == RACE_GOBLIN || bot->GetRace() == RACE_HIGH_ELF`.
inline bool RepopUsesHomebindCompat(Player* bot) { return bot->GetRace() == RACE_GOBLIN || bot->GetRace() == RACE_HIGH_ELF; }

// `bot->TeleportToHomebind(0, hearthCooldown);` (the caller sends the heartbeat, as before).
inline bool TeleportToHomebindCompat(Player* bot, bool hearthCooldown = true) { bot->TeleportToHomebind(0, hearthCooldown); return true; }
