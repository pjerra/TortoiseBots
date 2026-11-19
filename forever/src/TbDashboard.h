#pragma once

#include "Define.h"

// Feeds the TortoiseBots dashboard (tools/observability) on ForeverCore: a HEARTBEAT and one BOT_BATCH of every logged-in bot,
// over UDP, in the dashboard's own datagram format. Runs on the world thread (WorldScript::OnBeforeMapsUpdate).
namespace TortoiseBots::Dashboard
{
void LoadConfig();
void Update(uint32 diff);
}
