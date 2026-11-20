// Dashboard round 1: the way our feeds reach the dashboard's UDP socket (FOREVER-DASH-RUNS-SPEC.md decision D2).
//
// The roster sender (forever/src/TbDashboard.cpp) keeps its socket, session and sequence in an anonymous namespace. Until it exposes them
// (session, seq, send: the four lines listed in forever/docs/DASH-RUNS-R1-NOTES.md), this file is the local shim: the same config keys
// (TortoiseBots.Dashboard.Enable / Host / Port), its own socket, a session taken at load and its own sequence. The dashboard keeps every
// datagram type's session and sequence apart (COMMANDERS has its own), so a second sender is safe for the types it alone sends.
// When the exposure lands, only TbDashLink.cpp changes: its four functions forward to TortoiseBots::Dashboard.
// Built with TORTOISEBOTS_SLICE1 only. World thread only.
#pragma once

#include "Define.h"

#include <string>

namespace TortoiseBots
{
namespace DashRuns
{
namespace link
{
void Load();                              // (re)reads the config and opens the socket; Active() says whether it worked
bool Active();                            // the dashboard feed is on and the socket is open
uint64 Session();                         // the server session the datagrams carry
uint64 NextSeq();                         // 1, 2, 3 ... within the session
void Send(std::string const& datagram);   // one datagram, never blocks, a failure is dropped
}
}
}
