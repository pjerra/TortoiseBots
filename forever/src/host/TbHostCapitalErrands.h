#pragma once

#include "ObjectGuid.h"

#include <cstdint>

class Player;

namespace tbhost
{
namespace errands
{

void TickCapitalErrands(Player* bot, uint32_t diffMs);
void ClearBotCapitalErrands(ObjectGuid const& botGuid);

} // namespace errands
} // namespace tbhost
