// Dashboard round 1 (FOREVER-DASH-RUNS-SPEC.md): the Commander and Dungeon runs tabs on ForeverCore.
//
// Once per dashboard cycle (TortoiseBots.Dashboard.IntervalMs, default 2 s) it reads the run table (host/run/TbRun.h) and the leaders' parties,
// sends one COMMANDERS datagram (one binding per run: plan, step, leg, phase, note, the steps coming up, the last events, the party) built by
// the Turtle builder, and writes one `<run>.summary.json` per finished run into TortoiseBots.Dashboard.RunSummaryDir (the dashboard's
// -trace-dir). It makes no database call and runs no command: everything it reads is in memory already.
// The dungeon copies, their mobs and the raid flag (DUNGEON_MOBS) and the raid groups (RAIDS) are the roster sender's (TbDashboard.cpp).
// Built with TORTOISEBOTS_SLICE1; without it every function here does nothing. World thread only.
#pragma once

#include "Define.h"

namespace TortoiseBots
{
namespace DashRuns
{
void LoadConfig();          // TortoiseBots.Dashboard.Runs (default 1) and .RunSummaryDir (default "": no summary files)
void Update(uint32 diff);   // from OnBeforeMapsUpdate, after run::Update
bool Active();              // the feed is on: Dashboard.Enable = 1, Dashboard.Runs = 1, the engine is built and the dashboard socket is open
}
}
