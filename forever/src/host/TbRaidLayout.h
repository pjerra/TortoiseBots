#pragma once
// The raid layout on the Forever host: `.tb layout <leader>` puts the leader's raid into the shape RaidLayout::Plan
// (runtime/RaidLayoutPolicy.h) gives it, and a bot that joins a raid with a layout takes the seat of its role.

#include "ChatCommand.h"
#include "ObjectGuid.h"

#include <string>

class Group;
class Player;

namespace TortoiseBots
{
namespace h3
{
// .tb layout <leader> [#token]
bool HandleLayout(ChatHandler* handler, Trinity::ChatCommands::Tail args);

// A member that joined `group` after its layout was applied: only it is placed (the subgroup of its role, an off-tank
// mark for a tank, a tank or the raid for a healer); nobody else moves. Nothing happens without an applied layout.
// Returns the subgroup it took (0-7), or -1 when it was not placed. `heals`, when given, gets a healer's target: the tank's name or
// "raid" (empty for any other role).
int PlaceJoiner(Group* group, Player* member, std::string* heals = nullptr);

// A member is leaving `group` (GroupScript::OnRemoveMember): a tank's healers lose their focus on it (they heal the raid), and the
// leaver's own focus list is cleared. Nothing happens for a group without a layout.
void NoteLayoutMemberLeft(Group* group, ObjectGuid member);

// The group is gone (GroupScript::OnDisband): its layout record goes with it.
void ForgetLayout(Group* group);

// A join that would turn `group` (a full party) into a raid must not: a member has a commander binding, or the leader has a run that is
// not over. Both end when the party becomes a raid (the binding with `group-raid`, the run needs a party). `why` is the refusal.
// The build without the engine has neither and answers false.
bool RaidConvertBlocked(Group* group, std::string& why);
}
}
