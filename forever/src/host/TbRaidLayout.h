#pragma once
// The raid layout on the Forever host: `.tb layout <leader>` puts the leader's raid into the shape RaidLayout::Plan
// (runtime/RaidLayoutPolicy.h) gives it, and a bot that joins a raid with a layout takes the seat of its role.

#include "ChatCommand.h"

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
// Returns the subgroup it took (0-7), or -1 when it was not placed.
int PlaceJoiner(Group* group, Player* member);

// The group is gone (GroupScript::OnDisband): its layout record goes with it.
void ForgetLayout(Group* group);
}
}
