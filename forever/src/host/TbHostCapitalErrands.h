#pragma once

#include "ObjectGuid.h"

#include <cstdint>

class Player;

namespace tbhost
{
namespace errands
{

void TickCapitalErrands(Player* bot, uint32_t diffMs);
// Answers a duel challenge from anyone (the core's Player::duel state) and drives the fight while a duel
// runs. Called for every in-world bot, with or without capital errands.
void TickDuelResponse(Player* bot);
bool HasActiveErrand(Player const* bot);
void ClearBotCapitalErrands(ObjectGuid const& botGuid);

} // namespace errands
} // namespace tbhost
