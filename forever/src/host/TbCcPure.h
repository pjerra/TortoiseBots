// Slice 8, package G8 (review fix): the crowd-control clear's write/verify/undo core and the run's record of the marks it set, core-free so
// tools/test_cc_order.cpp can force a read-back that does not take.
//
// TbCc.cpp ClearCcOrder works in this order: it writes, it reads the writes back, and only then is the clear true. A write that does not hold is undone
// completely (both values and every raid icon), so a refused clear changes nothing: the bot keeps the mark it had and the icon stays where it was. The run's
// end (TbRun.cpp ClearRunCc) clears the bots a run `cc` order marked, which DungeonRun::Run::ccHolds can no longer name after a step advance or a resume
// (ResetStep clears it, not the bots' values).
#pragma once

#include <map>
#include <string>

namespace TortoiseBots
{
namespace h8
{
// Which raid icon a clear takes down, decided before any write: `icon` is the slot (-1: the values only), `own` says the bare clear took the icon of the
// bot's own mark (whatever unit it holds), and `needsGroup` is set only for a NAMED unit, whose icon must be looked up on the party's slots: a values-only
// reset touches no icon, so a bot that left the party is still taken off its run mark.
struct CcClearTarget
{
    int icon = -1;
    bool own = false;
    bool needsGroup = false;
};

inline CcClearTarget CcClearTargetOf(bool markNamed, bool unitNamed, int namedIndex, int ownIndex)
{
    CcClearTarget target;
    target.needsGroup = unitNamed;
    if (markNamed && unitNamed)
        target.icon = namedIndex;
    else if (!markNamed && !unitNamed)
    {
        target.icon = ownIndex;
        target.own = true;
    }
    return target;
}

// One icon slot to take down and the unit it holds now (read BEFORE the values are reset, so an icon a player moved since is left alone).
template <class Guid>
struct CcClearPlan
{
    int icon = -1;
    Guid held{};
};

enum class CcClearFailure
{
    None,
    Values,
    Icon,
};

// The clear itself: snapshot every write, write, read back. On failure put every value and every icon back exactly as they were. `Values` says GetCc, GetFamily,
// SetCc and SetFamily; `Icons` says Get(index), Set(index, guid), Snapshot() and Restore() (Restore puts every slot back to the snapshot).
template <class Guid, class Values, class Icons>
inline CcClearFailure ClearCcState(Values& values, Icons& icons, CcClearPlan<Guid> const& plan)
{
    std::string const oldMark = values.GetCc();
    std::string const oldFamily = values.GetFamily();
    icons.Snapshot();
    values.SetCc("none");
    values.SetFamily("none");
    if (plan.icon >= 0)
        icons.Set(plan.icon, Guid());
    CcClearFailure failure = CcClearFailure::None;
    if (plan.icon >= 0 && icons.Get(plan.icon) == plan.held)
        failure = CcClearFailure::Icon;
    else if (values.GetCc() != "none" || values.GetFamily() != "none")
        failure = CcClearFailure::Values;
    if (failure != CcClearFailure::None)
    {
        values.SetFamily(oldFamily);
        values.SetCc(oldMark);
        icons.Restore();
    }
    return failure;
}

// ---- the run's record of the bots its own `cc` orders marked (TbRun.cpp ClearRunCc) ----

inline void CcOwnedRecord(std::map<std::string, std::string>& owned, std::string const& bot, std::string const& mark)
{
    owned[bot] = mark;
}

inline void CcOwnedForget(std::map<std::string, std::string>& owned, std::string const& bot)
{
    owned.erase(bot);
}

// The run still owns the bot's mark while the bot's `rti cc` is that mark, or none (already cleared, or another order took it off the mark): then the run's
// write, or a prefix of it, is what is left to take back. Another mark is a later order's, and the run leaves it.
inline bool CcMarkOwned(std::string const& recordedMark, std::string const& currentMark)
{
    return currentMark.empty() || currentMark == recordedMark;
}
}
}
