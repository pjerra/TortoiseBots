// Commander port, package P4: the map layers of the Commander card and the live position report (FOREVER-COMMANDER-PORT-SPEC.md section 5, the P4
// row; decisions D5 and D6). The card's mobs, bosses, objects and bot extras are filled into the COMMANDERS bindings the dash-runs collector sends;
// COMMANDER_POS goes out through the same sender shim (dash/TbDashLink.h), never a second socket.
//
//   FillFeeds(out)   one snapshot cycle (2 s): per binding the hostiles near its lead (cap 24), the game objects its bots have near them (cap 16),
//                    the known bosses of its lead's dungeon with their state, and what each bot is doing (cast, crowd control, rest, target, boss
//                    debuffs, ready check). A layer whose scan did not run (no lead in the world, the feed off) stays unreported: the wire carries
//                    null, never an empty list.
//   UpdateFeeds(diff) the COMMANDER_POS clock (TortoiseBots.Commander.PosMs, 100 ms by default): one datagram per standing binding of the window,
//                    positions and the enemies of that binding's last full report, resolved by guid.
//
// Nothing here runs per bot tick: the fill runs in the dashboard's own cycle, the position pass on its clock, and both only while a binding stands.
// No database call, no grid or world scan outside the fill's own sweeps, no raw pointer kept across ticks. World thread only.
//
// Core headers only here; the engine-facing parts are in TbCmdFeeds.cpp behind TORTOISEBOTS_SLICE1. Without the option every function does nothing.
#pragma once

#include "Define.h"
#include "host/cmd/TbCmdFeedsPure.h"

#include <vector>

namespace TortoiseBots
{
namespace cmd
{
// World start and every config load: the keys (TortoiseBots.Commander.Feeds, .MobRange, .ObjectRange, .PosMs) and, once the plan tables are sealed,
// the boss spawn cache (one pass over the core's creature spawn data; cannot be built before the plans are loaded).
void LoadFeeds();
// OnBeforeMapsUpdate, after the dash-runs cycle: the position clock.
void UpdateFeeds(uint32 diff);
// The dash-runs collector, before it sends its COMMANDERS datagram: fills the map layers of every binding that has a lead in the world.
void FillFeeds(std::vector<::TortoiseBots::CommanderTelemetry::BindingInfo>& out);
// Whether the feed is on (config, engine build) - the position clock's gate.
bool FeedsActive();
}
}
