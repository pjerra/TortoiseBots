// Slice 5, host package H5: the run leader's decision headers compiled into the module, so the census and BOTH builds (option ON and OFF) see them
// (spec FOREVER-RUN-S5S6-SPEC.md section 4.2). The headers are the Turtle ones, unchanged: runtime/DungeonRunPolicy.h (P), RemoteCommanderPolicy.h
// (RCP) and SystemRunPolicy.h; they hold plain data and the standard library only. This translation unit adds no behaviour: it proves they compile
// with this core's compiler flags, and the one function below is what keeps a linker from dropping the object.
#include "runtime/DungeonRunPolicy.h"
#include "runtime/RemoteCommanderPolicy.h"
#include "runtime/SystemRunPolicy.h"

namespace TortoiseBots
{
namespace run
{
// A fixed answer built from one fact of each header, so the headers are used, not only parsed. Not called by any command.
char const* RunHeadersProbe()
{
    DungeonRun::Run run;
    return DungeonRun::StateName(run.state);
}
}
}
