// The run's binding rule for a member it bound, with no game type (a pure function: forever/tools/test_run_binding.cpp).
// TbRun.cpp's SyncBinding asks it once a second, so the run's commander holds through a pause and a regroup.
#pragma once

namespace TortoiseBots
{
namespace run
{
enum class BindingSync
{
    Keep,       // nothing to do: the binding is the run's, or the member is not the run's to bind
    Reassert    // the run bound this member and the host master is still the run's lead, but the commander table no longer says so: put it back
};

// The run bound a member to its lead (the run's commander, decision D1). The engine's own master logic can drop the commander entry while the host master
// stays the lead (it adopts the group leader as a master: the same person, no commander table entry), and a pause is where that shows. The run puts its
// binding back then. A host master that is NOT the lead is somebody else's newer decision (`.tb master`, `.tb run stop`): the run leaves it alone, and a
// member it did not bind is never bound here (start binds, and a new member is bound only when it is commanded already).
inline BindingSync SyncBindingAction(bool boundByRun, bool masterIsLead, bool commanderIsLead)
{
    if (!boundByRun || !masterIsLead || commanderIsLead)
        return BindingSync::Keep;
    return BindingSync::Reassert;
}
}
}
