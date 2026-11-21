// Ownership guard pure decision: refuses a bot owned by another commander/master or in a party led by another real player.
// No core header and no legacy type; tested standalone by forever/tools/test_guard.cpp.
#pragma once

#include "host/cmd/TbTestTargetPure.h"

#include <string_view>

namespace TortoiseBots
{
namespace h3
{
namespace guard
{
using Guid = target::Guid;

enum class Verdict
{
    Allowed,
    ForeignBot,
    ForeignLead,
};

struct Subject
{
    Guid caller;
    Guid binding;
    Guid master;
    bool masterIsBot = false;
    Guid partyLeader;
    bool partyLeaderIsBot = false;
};

inline Verdict CheckControl(Subject const& s)
{
    if (s.caller.empty())
        return Verdict::Allowed;
    if (!s.binding.empty() && !(s.binding == s.caller))
        return Verdict::ForeignBot;
    if (!s.master.empty() && !s.masterIsBot && !(s.master == s.caller))
        return Verdict::ForeignBot;
    if (!s.partyLeader.empty() && !s.partyLeaderIsBot && !(s.partyLeader == s.caller))
        return Verdict::ForeignLead;
    return Verdict::Allowed;
}

inline char const* CodeOf(Verdict v)
{
    switch (v)
    {
        case Verdict::Allowed:     return "";
        case Verdict::ForeignBot:  return "foreign-bot";
        case Verdict::ForeignLead: return "foreign-lead";
    }
    return "";
}
}
}
}
