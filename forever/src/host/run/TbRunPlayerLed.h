// Issue #139: the opt-in player-led run, the world-facing half (forever/docs/RUN-KNOBS-139-NOTES.md). The pure half is TbRunPlayerLedPure.h (the start's
// gate), the policy is the Turtle runtime/PlayerLedPolicy.h unchanged. Compiled with TORTOISEBOTS_SLICE1 only.
#pragma once

#include "host/run/TbRun.h"

namespace TortoiseBots
{
namespace run
{
// AiPlayerbot.DungeonRunPlayerLed: the switch of the mode. A run already going ends when it is switched off (the next tick).
bool PlayerLedKeyOn();

// Copies AiPlayerbot.DungeonRunPlayerLedPullYd and AiPlayerbot.DungeonRunPlayerLedAwayMinutes into the run's player-led state.
void PlayerLedConfigure(PlayerLed::State& pl);

// One run tick, before the facts are read: reads the leading player as the run's anchor (his place, his state, how far the farthest bot is from him),
// lists what the party fights, and lets PlayerLed::Pick choose the step from where he stands (Run::step). `tank` is the run's leader bot (the path
// check asks its navmesh way). Returns the anchor the tick's PlayerLed::Tick needs.
PlayerLed::Anchor PlayerLedPick(RunRec& rec, Player* tank);
}
}
