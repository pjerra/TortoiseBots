#pragma once

#include "Define.h"

namespace TortoiseBots
{
namespace gear
{
// Source gate for generated pool-bot gear on the Forever host: an item passes when it has a known, ordinary
// source (world/dungeon drop below the configured source tier, vendor, quest, craft) and is not raid, PvP,
// reputation or blacklisted gear. Items with no known source fail (custom/unobtainable rows).
// The index is built once at startup from the world tables.
void InitGearSourceIndex();
bool SourceGateAllows(uint32 itemId);
} // namespace gear
} // namespace TortoiseBots
