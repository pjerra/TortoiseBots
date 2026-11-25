// The passthrough `.bot` verbs over the addon channel (TBF #130 piece f-verbs): the eight verbs the player layer answers
// (add/logout/remove/summon/stats/who/maintenance/autogear) run through player::Dispatch with the reply sink set, so no
// command text changes; the captured lines are mirrored verbatim and folded into one Ack/Err. `remove` keeps its sent word
// as the intent while the layer prints `logout` lines; `who` acks the roster scope with its row count; `add` joins the
// party, so its Ack is party-scoped. A declined Dispatch sends nothing (the engine is off: the core answers over chat);
// a Dispatch with no terminal line is an `unknown` Err so the addon status line always resolves.
#include "host/TbAddonVerbs.h"
#include "host/TbAddonVerbsPure.h"

#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1

#include "host/TbAddonChannel.h"
#include "host/player/TbPlayerCmd.h"

#include <string>
#include <string_view>
#include <vector>

namespace TortoiseBots
{
namespace addonverbs
{
namespace
{

void RunVerb(std::string_view verb, Player* requester, std::string_view args)
{
    std::vector<std::string> lines;
    player::SetReplySink(&lines);
    std::string const rest = args.empty() ? std::string(verb) : std::string(verb) + " " + std::string(args);
    bool const claimed = player::Dispatch(requester, rest);
    player::SetReplySink(nullptr);
    if (!claimed)
        return;
    for (std::string const& line : lines)
        if (!line.empty())
            addon::Send(requester, line);
    Outcome const outcome = Summarize(verb, lines);
    if (!outcome.answered)
    {
        addon::Err(requester, verb, "unknown", "no reply");
        return;
    }
    if (outcome.ok)
        addon::Ack(requester, verb, outcome.scope, outcome.count, outcome.detail);
    else
        addon::Err(requester, verb, outcome.code, outcome.message);
}

void HandleAdd(Player* requester, std::string_view args) { RunVerb("add", requester, args); }
void HandleLogout(Player* requester, std::string_view args) { RunVerb("logout", requester, args); }
void HandleRemove(Player* requester, std::string_view args) { RunVerb("remove", requester, args); }
void HandleSummon(Player* requester, std::string_view args) { RunVerb("summon", requester, args); }
void HandleStats(Player* requester, std::string_view args) { RunVerb("stats", requester, args); }
void HandleWho(Player* requester, std::string_view args) { RunVerb("who", requester, args); }
void HandleMaintenance(Player* requester, std::string_view args) { RunVerb("maintenance", requester, args); }
void HandleAutogear(Player* requester, std::string_view args) { RunVerb("autogear", requester, args); }
void HandleHire(Player* requester, std::string_view args) { RunVerb("hire", requester, args); }

} // namespace

void RegisterAddonVerbsPassthrough()
{
    addon::RegisterVerb("add", &HandleAdd);
    addon::RegisterVerb("logout", &HandleLogout);
    addon::RegisterVerb("remove", &HandleRemove);
    addon::RegisterVerb("summon", &HandleSummon);
    addon::RegisterVerb("stats", &HandleStats);
    addon::RegisterVerb("who", &HandleWho);
    addon::RegisterVerb("maintenance", &HandleMaintenance);
    addon::RegisterVerb("autogear", &HandleAutogear);
    addon::RegisterVerb("hire", &HandleHire);
}

} // namespace addonverbs
} // namespace TortoiseBots

#else   // no engine: the player entry declines everything, so there is nothing to pass through

namespace TortoiseBots
{
namespace addonverbs
{
void RegisterAddonVerbsPassthrough() { }
} // namespace addonverbs
} // namespace TortoiseBots

#endif
