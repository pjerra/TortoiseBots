#pragma once
#include "BossTacticsPolicy.h"
namespace ai
{
inline bool FiveManMap(uint32_t map)
{
    switch(map)
    {
        case 33: case 34: case 36: case 43: case 47: case 48: case 70: case 90:
        case 109: case 129: case 189: case 209: case 229: case 230: case 289:
        case 329: case 349: case 389: case 429: return true;
        default: return false;
    }
}
inline bool FiveManKind(BossTacticKind kind)
{
    return kind==BossTacticKind::Interrupt || kind==BossTacticKind::KillFirst ||
        kind==BossTacticKind::Behind || kind==BossTacticKind::AvoidCast;
}
inline bool FiveManSafetyAction(std::string const& name)
{
    return name=="boss tactics stand" || name=="boss tactics step behind" || name=="boss tactics avoid cast";
}
// What a temporary pull wait (WaitForAttackMultiplier) still lets through on a five-man map: the safety movements, and the
// interrupt of a listed cast. An interrupt only ever targets a unit that is already engaged with the group (ScanBossTactics),
// so it pulls nothing new; holding it back during the wait is what let a sleep or a heal land (job lesson: a generic pull or
// wait multiplier must not block encounter actions). Kill-first and other damage stay held.
inline bool FiveManPullWaitAllowed(std::string const& name)
{
    return FiveManSafetyAction(name) || name=="boss tactics interrupt";
}
}
