// Standalone check of host/run/TbRunBindingPure.h: what the run's tick does with a member's binding, second by second (pause and regroup included).
//   cd <worktree> && clang++ -std=c++20 -I forever/src forever/tools/test_run_binding.cpp -o /tmp/test_run_binding && /tmp/test_run_binding
#include "host/run/TbRunBindingPure.h"

#include <cstdio>

using TortoiseBots::run::BindingSync;
using TortoiseBots::run::SyncBindingAction;

namespace
{
int failures = 0;
int checks = 0;

void Check(bool ok, char const* what)
{
    ++checks;
    if (!ok)
    {
        ++failures;
        std::printf("FAIL: %s\n", what);
    }
}
}

int main()
{
    // The normal state of a member the run bound: host master and commander table both name the lead.
    Check(SyncBindingAction(true, true, true) == BindingSync::Keep, "the run's binding is in place: keep");
    // The engine's master adoption replaced the commander table entry (the host master is still the lead, the same person): the run puts its binding back.
    Check(SyncBindingAction(true, true, false) == BindingSync::Reassert, "the commander entry was dropped under the run's own master: reassert");
    // A master that is not the lead: somebody else rebound the bot (`.tb master`, a stop that restored the old one). The newer decision stays.
    Check(SyncBindingAction(true, false, false) == BindingSync::Keep, "a master that is not the lead: leave it");
    Check(SyncBindingAction(true, false, true) == BindingSync::Keep, "a master that is not the lead while the table still names the lead: leave it");
    // A member the run did not bind (a new joiner, another party's bot): SyncBinding never binds it here.
    Check(SyncBindingAction(false, true, false) == BindingSync::Keep, "not bound by the run: never bind here");
    Check(SyncBindingAction(false, true, true) == BindingSync::Keep, "not bound by the run, already the lead's: keep");

    std::printf("%s: %d checks, %d failed\n", failures ? "FAIL" : "RESULT: PASS", checks, failures);
    return failures ? 1 : 0;
}
