#pragma once

// #24 host side: the table of real players the pool-share rule reads, and the rule itself. The decision is the core-free
// ai/playerbot/policy/world/PoolShareMobsPolicy.h; this layer keeps the table (once a second, from BotMgr::Update, world
// thread only) and fills the rule's facts from the core and from BotMgr.
//
// The engine asks Judge() from GrindTargetValue (the possible-targets loop and the idle fallback) through
// `PoolShareMobsSkipCompat` in forever/src/compat/TbCompat_W1.h, i.e. from the same world-thread pass: no lock is taken.
// Slice OFF: this file is not compiled (CMakeLists.txt globs host/TbHost*.cpp into the slice build only) and the compat
// answers false, so nothing changes.

#include "ai/playerbot/policy/world/PoolShareMobsPolicy.h"

#include <cstdint>

class Player;
class Unit;

namespace tbhost
{
namespace sharemobs
{

// One pass of BotMgr::Update: rebuilds the real-player table when 1000 ms have gone by since the last rebuild (and clears it
// at once while the switch is off, so the default costs one branch a tick). The first rebuild happens one second after the
// switch is on, which is why the live test turns it on two minutes before the player arrives.
void Tick(std::uint32_t diff);

// #24's mob rule for one candidate unit: None when the rule does not apply to this bot or this mob may be started,
// NearPlayer / QuestNearPlayer when it must not be (any reason but self-defence, which the attackers loop answers earlier).
TortoiseBots::world::MobVerdict Judge(Player* bot, Unit* unit);

} // namespace sharemobs
} // namespace tbhost
