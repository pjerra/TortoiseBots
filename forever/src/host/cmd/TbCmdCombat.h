// Commander port, package P5 (the fight debugger): see forever/docs/CMD-P5-NOTES.md. FOREVER-COMMANDER-PORT-SPEC.md section 5 (P5), section 7
// (its three checks) and section 8 (its live step).
//
// The ForeverCore side of runtime/CombatMonitorService.cpp: the core seams (one UnitScript and one PlayerScript), the 1 Hz sample of every
// standing commander card, the cast poll while a fight runs, the live fight window of the COMMANDERS datagram and the finished fights and
// events COMBAT_FIGHT / COMBAT_EVENTS through the dashboard link (forever/src/dash/TbDashLink.h, the stand-in of the roster sender's own
// socket). The books, the JSON and the outcomes are the shared service and runtime/CombatMonitorPolicy.h; nothing here builds a payload.
//
// World thread only. The hooks run on map threads and keep no pointer past the call: the service's member table routes them to the book.
#pragma once

#include "Define.h"
#include "runtime/CommanderTelemetry.h"

namespace TortoiseBots
{
namespace cmd
{
namespace combat
{
// The world tick (TbScripts.cpp, after the run tick): the 1 Hz sample, the 250 ms cast poll, the 100 ms event drain.
void Update(uint32 diff);
// `.tb commander status` and the dash-runs COMMANDERS datagram: the live fight window (`combat` object) of the binding's group, empty
// outside a fight (cmd::AppendBindings calls this per card).
void FillWindow(CommanderTelemetry::BindingInfo& binding);
// The two script objects (AddSC_tortoisebots). A no-op build without the engine registers nothing.
void RegisterScripts();
} // namespace combat
} // namespace cmd
} // namespace TortoiseBots
