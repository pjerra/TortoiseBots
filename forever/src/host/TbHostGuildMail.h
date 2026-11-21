#pragma once

#include "ObjectGuid.h"

#include <cstdint>

class Player;

namespace tbhost
{
namespace guild_mail
{

void TickGuildMail(Player* bot, uint32_t diffMs);
bool HasActiveErrand(Player const* bot);
void ClearBotGuildMail(ObjectGuid const& botGuid);

} // namespace guild_mail
} // namespace tbhost
