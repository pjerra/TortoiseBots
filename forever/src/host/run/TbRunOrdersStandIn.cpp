// Slice 6, package C6: WEAK stand-ins for the order executor of package O6 (TbRunOrders.h), so the tick and `.tb run` link and answer truthfully before O6 merges.
// The executor is not here: every order is refused `unsupported` (the run then pauses on its first order with that reason: the bot does LESS), the late-failure
// list is empty, and `StartCheck` refuses (a run cannot start without an executor). O6's TbRunOrders.cpp defines the same functions strongly; the strong
// definition replaces these at link time, and the merge deletes this file (and TbRunOrders.h here, which is a byte copy of O6's commit a752649).
#include "host/run/TbRunOrders.h"

#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1

namespace TortoiseBots
{
namespace run
{
__attribute__((weak)) OrderAnswer Issue(RunRec& /*rec*/, std::string const& order, uint32 /*nowMs*/, bool /*gather*/)
{
    OrderAnswer answer;
    answer.code = "unsupported";
    answer.text = "unsupported: no order executor is built into this module yet (" + order.substr(0, order.find(' ')) + ")";
    return answer;
}

__attribute__((weak)) void TakeLateFailures(RunRec& /*rec*/, uint32 /*nowMs*/, std::vector<LateFailure>& /*out*/)
{
}

__attribute__((weak)) void ForgetRunOrders(ObjectGuid /*leader*/)
{
}

__attribute__((weak)) bool StartCheck(ObjectGuid /*leader*/, std::string& code, std::string& message)
{
    code = "no-executor";
    message = "this module has no order executor yet (package O6 is not merged): a run cannot start";
    return false;
}
}
}

#endif
