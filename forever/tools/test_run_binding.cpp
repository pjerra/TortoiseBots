// Standalone check of host/run/TbRunBindingPure.h: what the run's tick does with a member's binding, second by second (pause and regroup included).
//   cd <worktree> && clang++ -std=c++20 -I forever/src forever/tools/test_run_binding.cpp -o /tmp/test_run_binding && /tmp/test_run_binding
#include "host/run/TbRunBindingPure.h"

#include <cstdio>
#include <string>
#include <vector>

using TortoiseBots::run::BindingEntry;
using TortoiseBots::run::BindingSync;
using TortoiseBots::run::RepairBinding;
using TortoiseBots::run::SyncBindingAction;

namespace
{
int failures = 0;
int checks = 0;

void Check(bool ok, std::string const& what)
{
    ++checks;
    if (!ok)
    {
        ++failures;
        std::printf("FAIL: %s\n", what.c_str());
    }
}

// The binding table and host master as one member's line, for the convergence checks below.
struct Member
{
    BindingEntry entry = BindingEntry::Lead;
    bool masterIsLead = true;
    bool masterAway = false;
    int binds = 0;              // host binds the tick made (BindBotMaster)
    int commanderWrites = 0;    // commander entries the tick wrote (h3::SetCommander)
};

// One SyncBinding pass for this member, with the repair's two writes as host code makes them: the host bind (SetMaster) sets the lead and clears the
// away mark, and the commander entry is written only after a bind the host accepted.
BindingSync Tick(Member& member, bool boundByRun = true)
{
    BindingSync const action = SyncBindingAction(boundByRun, member.masterAway, member.masterIsLead, member.entry);
    if (action == BindingSync::Reassert)
        RepairBinding([&] { ++member.binds; member.masterIsLead = true; member.masterAway = false; return true; },
                      [&] { ++member.commanderWrites; member.entry = BindingEntry::Lead; });
    return action;
}
}

int main()
{
    // The normal state of a member the run bound: host master and commander table both name the lead.
    Check(SyncBindingAction(true, false, true, BindingEntry::Lead) == BindingSync::Keep, "the run's binding is in place: keep");
    // The engine's master logic moved the host master off the lead while the run's entry stayed: the run puts the master (and the entry) back.
    Check(SyncBindingAction(true, false, false, BindingEntry::Lead) == BindingSync::Reassert, "the host master moved under the run's entry: reassert");
    // The engine adopted the lead as host master without a commander entry (the live pause state: commanded-group behaviour off, `commander=mixed`).
    Check(SyncBindingAction(true, false, true, BindingEntry::Missing) == BindingSync::Reassert, "the commander entry was dropped under the run's own master: reassert");
    // No entry and no lead master: nothing of the run's to put back (a release that restored an older master, a re-login).
    Check(SyncBindingAction(true, false, false, BindingEntry::Missing) == BindingSync::Keep, "no entry and a master that is not the lead: leave it");
    // A commander somebody else set (`.tb master` writes the entry too): newer than the run's binding, whatever the host master is doing.
    Check(SyncBindingAction(true, false, false, BindingEntry::Other) == BindingSync::Keep, "a commander somebody else set: leave it");
    Check(SyncBindingAction(true, false, true, BindingEntry::Other) == BindingSync::Keep, "a commander somebody else set, lead master: leave it");
    // A member whose master is away waits out its grace: a bind (BotMgr::SetMaster) would clear that wait.
    Check(SyncBindingAction(true, true, true, BindingEntry::Missing) == BindingSync::Keep, "master away: never bind");
    Check(SyncBindingAction(true, true, false, BindingEntry::Lead) == BindingSync::Keep, "master away, master moved: never bind");
    // A member the run did not bind (a new joiner, another party's bot): SyncBinding never binds it here.
    Check(SyncBindingAction(false, false, false, BindingEntry::Lead) == BindingSync::Keep, "not bound by the run: never bind here");
    Check(SyncBindingAction(false, false, true, BindingEntry::Missing) == BindingSync::Keep, "not bound by the run, no entry: never bind here");
    Check(SyncBindingAction(false, false, true, BindingEntry::Lead) == BindingSync::Keep, "not bound by the run, already the lead's: keep");

    // The repair's order: the host bind first, the commander entry only after a bind the host accepted.
    {
        Member member;
        std::vector<std::string> calls;
        bool const repaired = RepairBinding([&] { calls.push_back("bind"); return true; }, [&] { calls.push_back("commander"); });
        Check(repaired && calls == std::vector<std::string>({"bind", "commander"}), "a repair binds first, then writes the commander entry");
        calls.clear();
        bool const refused = RepairBinding([&] { calls.push_back("bind"); return false; }, [&] { calls.push_back("commander"); });
        Check(!refused && calls == std::vector<std::string>({"bind"}), "a refused bind leaves the commander table alone");
        Check(member.entry == BindingEntry::Lead && member.binds == 0, "the refused-bind check touched no table");
    }

    // One sync is enough: the first tick repairs both tables, the second is Keep and makes no host write (the away mark is not reset every tick).
    {
        Member member;
        member.masterIsLead = false;                    // the engine moved the host master off the run's lead
        Check(Tick(member) == BindingSync::Reassert && member.binds == 1 && member.commanderWrites == 1, "moved master: one tick repairs");
        Check(member.masterIsLead && member.entry == BindingEntry::Lead, "moved master: the host master and the entry name the lead again");
        Check(Tick(member) == BindingSync::Keep && member.binds == 1, "moved master: the next tick is Keep, no second bind");
        member.masterAway = true;                       // a grace that starts later must survive the ticks that follow
        Check(Tick(member) == BindingSync::Keep && member.masterAway, "away after the repair: the mark is not cleared");
    }
    {
        Member member;
        member.entry = BindingEntry::Missing;           // the engine adopted the lead without an entry
        member.masterAway = true;
        Check(Tick(member) == BindingSync::Keep && member.masterAway && member.binds == 0, "missing entry under a master that is away: the grace stands");
        member.masterAway = false;
        Check(Tick(member) == BindingSync::Reassert && member.entry == BindingEntry::Lead && member.commanderWrites == 1, "missing entry: one tick writes it");
        Check(Tick(member) == BindingSync::Keep && member.binds == 1, "missing entry: the next tick is Keep");
    }

    std::printf("%s: %d checks, %d failed\n", failures ? "FAIL" : "RESULT: PASS", checks, failures);
    return failures ? 1 : 0;
}
