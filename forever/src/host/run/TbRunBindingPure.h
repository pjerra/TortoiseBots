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
    Reassert    // the run bound this member and the binding table no longer agrees with the run's lead: put the host master and the entry back
};

// What the table holds for the member (h3::StoredCommander), read as the run's rule needs it. The table is read WITHOUT h3::CommanderGuid's "the host
// master still names it" check: an entry that lapsed because the host master moved is exactly the state the rule has to see.
enum class BindingEntry
{
    Missing,    // no entry: the engine adopted the run's lead as the host master and wrote no commander entry (the live pause symptom)
    Lead,       // the run's own entry (BindMember writes it at start and for a joiner)
    Other       // `.tb master` named another commander: somebody's newer decision, not the run's to undo
};

// The run bound a member to its lead (the run's commander, decision D1). One sync is enough: a Reassert is repaired by the two writes RepairBinding
// makes (the host master back to the lead and the entry with it), and the next sync sees BindingEntry::Lead with the lead as master, which is Keep.
//  * entry Lead, master not the lead: the engine's own master logic moved the host master under the run's entry (it adopts the group's leader, drops
//    a master outside the party): the run puts both back. A master somebody set is not this case, `.tb master` writes the entry too.
//  * entry Missing, master the lead: the engine took the lead as master without a commander entry (commanded-group behaviour stays off and the party
//    reads `commander=mixed` until the entry is back): the entry is written.
//  * entry Other: somebody else's commander stays, whatever the host master is doing (`.tb master`, a release that restored an older one).
//  * a member whose master is away waits out its grace (BotMgr's detach): a bind would clear that wait, so it is left alone until it is back.
//  * a member the run did not bind is never bound here (start binds, and a new joiner is bound only when it is commanded already).
inline BindingSync SyncBindingAction(bool boundByRun, bool masterAway, bool masterIsLead, BindingEntry entry)
{
    if (!boundByRun || masterAway)
        return BindingSync::Keep;
    switch (entry)
    {
        case BindingEntry::Other:
            return BindingSync::Keep;
        case BindingEntry::Lead:
            return masterIsLead ? BindingSync::Keep : BindingSync::Reassert;
        case BindingEntry::Missing:
            return masterIsLead ? BindingSync::Reassert : BindingSync::Keep;
    }
    return BindingSync::Keep;
}

// The two writes a Reassert makes, in the order that keeps a refused host bind from touching the commander table: `bind` is the host bind
// (BotManager::BindBotMaster: the host master and the engine's pointer) and `setCommander` the entry (h3::SetCommander). False = the bind was refused:
// nothing was written and the next sync asks again. True = both writes happened, so the next sync is Keep.
template <class Bind, class SetCommander>
inline bool RepairBinding(Bind bind, SetCommander setCommander)
{
    if (!bind())
        return false;
    setCommander();
    return true;
}
}
}
