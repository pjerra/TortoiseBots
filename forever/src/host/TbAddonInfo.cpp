// The addon info readers live half (TBF #130 piece f-info): the TBM read
// lines the addon polls. The rule without the core is host/TbAddonInfoPure.h.
#include "host/TbAddonInfo.h"
#include "host/TbAddonChannel.h"
#include "host/TbAddonInfoPure.h"

#include "TbBotMgr.h"
#include "ai/playerbot/DungeonPlanMgr.h"
#include "host/TbCc.h"
#include "host/run/TbRun.h"
#include "runtime/DungeonRunPolicy.h"

#include "Config.h"
#include "Group.h"
#include "Player.h"
#include <algorithm>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace TortoiseBots
{
namespace addoninfo
{
namespace
{
// One Info-window switch: the addon id, the conf key it reads live, whether
// the value renders on/off (else a number), and the label. Fallbacks equal
// the shipped conf/aiplayerbot.conf.dist and tortoisebots.conf.dist values,
// so deleting a line changes nothing.
struct Feature
{
    char const* id;
    char const* key;
    bool boolean;
    int32 defaultNumber;
    char const* label;
};

Feature const kFeatures[] = {
    { "engine-default", "TortoiseBots.Slice.EngineByDefault", true, 0, "Bot engine by default" },
    { "pool", "TortoiseBots.Pool.Enable", true, 0, "Bot pool" },
    { "bg", "TortoiseBots.Bg.Enable", true, 0, "Battlegrounds" },
    { "tactical-orders", "TortoiseBots.Orders.Tactical", true, 1, "Tactical orders" },
    { "player-commands", "TortoiseBots.Player.Commands", true, 1, "Player .bot commands" },
    { "addon-hint", "TortoiseBots.Player.AddonHint", true, 0, "Add-on hint for new players" },
    { "max-bots", "TortoiseBots.Player.MaxBots", false, 4, "Bots per player" },
    { "ritual-fill", "TortoiseBots.RitualFill.Enable", true, 1, "Bots fill multi-user objects" },
    { "ritual-fill-range", "TortoiseBots.RitualFill.Range", false, 30, "Fill range (yd)" },
    { "ritual-fill-hold", "TortoiseBots.RitualFill.HoldSeconds", false, 120, "Fill hold (s)" },
    { "object-use", "TortoiseBots.ObjectUse.Commands", true, 1, "use/ritual/soulstone" },
    { "follow-distance", "TortoiseBots.Spike.FollowDistance", false, 3, "Follow distance (yd)" },
    { "master-grace", "TortoiseBots.Slice.MasterGraceSeconds", false, 300, "Master grace (s)" },
    { "auto-learn", "TortoiseBots.AutoLearnOnLevel", true, 1, "Auto-learn spells" },
    { "auto-talents", "TortoiseBots.AutoTalentsOnLevel", true, 1, "Auto-talents" },
    { "auto-profs", "TortoiseBots.AutoProfessions", true, 1, "Auto professions" },
    { "auto-tools", "TortoiseBots.AutoTools", true, 1, "Auto tools" },
    { "auto-pet", "TortoiseBots.AutoHunterPet", true, 1, "Hunter starter pet" },
    { "auto-food", "TortoiseBots.AutoFood", true, 1, "Auto food" },
    { "class-quests", "TortoiseBots.ClassQuestSpells", true, 0, "Class quest spells" },
    { "world-gather", "TortoiseBots.World.Professions", true, 0, "Free-bot gathering" },
    { "world-fishing", "TortoiseBots.World.Fishing", true, 1, "Free-bot fishing" },
    { "world-disenchant", "TortoiseBots.World.Disenchant", true, 1, "Free-bot disenchanting" },
    { "world-enchant-gear", "TortoiseBots.World.EnchantGear", true, 1, "Free-bot gear enchanting" },
    { "world-auction", "TortoiseBots.World.Auction", true, 0, "Bot auction house" },
    { "group-quests", "TortoiseBots.World.GroupQuests", true, 1, "Group quests" },
    { "capital-errands", "TortoiseBots.World.CapitalErrands", true, 1, "Capital errands" },
    { "guild-mail", "TortoiseBots.World.GuildMail", true, 1, "Guild mail" },
    { "guild-accept-players", "TortoiseBots.World.GuildAcceptPlayers", true, 0, "Join bot guilds by whisper" },
    { "world-pickpocket", "TortoiseBots.World.Pickpocket", true, 1, "Rogue pickpocket" },
    { "world-lockboxes", "TortoiseBots.World.Lockboxes", true, 1, "Rogue lockbox opening" },
    { "world-lockbox-mail", "TortoiseBots.World.LockboxMail", true, 1, "Lockbox by guild mail" },
    { "rest-health", "TortoiseBots.Slice.RestHealthPct", false, 90, "Rest at health %" },
    { "rest-mana", "TortoiseBots.Slice.RestManaPct", false, 80, "Rest at mana %" },
};

std::string FeatureValue(Feature const& feature)
{
    if (feature.boolean)
        return sConfigMgr->GetBoolDefault(feature.key, feature.defaultNumber != 0, true) ? "on" : "off";
    return std::to_string(sConfigMgr->GetIntDefault(feature.key, feature.defaultNumber, true));
}

// The settings the search reads: the module's and the engine's keys only,
// never the world's (those hold database passwords). TB searches AiPlayerbot.*;
// TBF adds its own TortoiseBots.* namespace.
bool SearchableKey(std::string const& key)
{
    return key.compare(0, 13, "TortoiseBots.") == 0 || key.compare(0, 12, "AiPlayerbot.") == 0;
}

void HandleFeatures(Player* requester, std::string_view args)
{
    FeaturesRequest const request = ParseFeaturesArgs(args);
    if (request.usageError)
    {
        addon::Err(requester, "features", "usage", "features [all <at least two letters of a setting name>]");
        return;
    }
    if (!request.search)
    {
        for (Feature const& feature : kFeatures)
            addon::Send(requester, FeatureLine(feature.id, FeatureValue(feature), feature.label));
        return;
    }
    std::vector<std::string> keys = sConfigMgr->GetKeysByString("");
    std::sort(keys.begin(), keys.end());
    std::vector<std::pair<std::string, std::string>> matches;
    for (std::string const& key : keys)
    {
        if (!SearchableKey(key) || !ConfigMatches(request.filter, key))
            continue;
        matches.emplace_back(key, MaskedConfigValue(key, sConfigMgr->GetStringDefault(key, "", true)));
    }
    std::size_t const total = matches.size();
    std::size_t const shown = std::min(total, kConfigMaxRows);
    addon::Send(requester, ConfigSummaryLine(total, request.filter));
    addon::Send(requester, ConfigBeginLine(total));
    for (std::size_t i = 0; i < shown; ++i)
        addon::Send(requester, ConfigLine(matches[i].first, matches[i].second));
    addon::Send(requester, ConfigEndLine(shown, total));
}

// The run the requester's party is walking: the first group member (bots
// lead runs; a player never does) whose guid holds one. Runs exist only in the slice build.
run::RunRec* PartyRun(Player* requester)
{
#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1
    Group* group = requester ? requester->GetGroup() : nullptr;
    if (!group)
        return nullptr;
    for (GroupReference const& reference : group->GetMembers())
    {
        Player* member = reference.GetSource();
        if (!member)
            continue;
        if (run::RunRec* rec = run::Find(member->GetGUID()))
            return rec;
    }
#endif
    return nullptr;
}

void HandleRunStatus(Player* requester)
{
#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1
    run::RunRec* rec = PartyRun(requester);
    if (!rec || !rec->run.plan)
    {
        addon::Send(requester, NoRunLine());
        addon::Ack(requester, "run status", "party", 0, "");
        return;
    }
    addon::Send(requester, TortoiseBots::DungeonRun::StatusLine(rec->run));
    // TBF keeps no raid bind and no roles: the count comes from the run's
    // party (the bound members plus the lead), every role field reads "-".
    addon::Send(requester, RaidLayoutLine(std::to_string(rec->bound.size() + 1), "-", "-", "-", "-"));
    TortoiseBots::DungeonRun::StatusFields const fields = TortoiseBots::DungeonRun::StatusParts(rec->run);
    addon::Ack(requester, "run status", "party", std::uint32_t(fields.step), fields.plan);
#else
    addon::Send(requester, NoRunLine());
    addon::Ack(requester, "run status", "party", 0, "");
#endif
}

void HandleRunHere(Player* requester)
{
#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1
    ai::DungeonPlanTable& table = ai::GetDungeonPlans();
    if (requester && table.Sealed())
    {
        std::uint32_t const map = requester->GetMapId();
        std::vector<PlanPoint> points;
        for (ai::DungeonPlan const* plan : table.All())
        {
            if (!plan || plan->map != map)
                continue;
            for (ai::PlanStep const& step : plan->steps)
                points.push_back(PlanPoint{ plan->name, plan->map, step.x, step.y, step.z });
        }
        NearestPlan const near =
            PickNearestPlan(map, requester->GetPositionX(), requester->GetPositionY(), requester->GetPositionZ(), points);
        if (near.found)
        {
            addon::Send(requester, RunHereLine(near.plan, near.yards));
            return;
        }
    }
#endif
    addon::Send(requester, NoRunHereLine());
}

void HandleRunList(Player* requester)
{
    std::vector<PlanEntry> plans;
#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1
    ai::DungeonPlanTable& table = ai::GetDungeonPlans();
    if (table.Sealed())
    {
        std::vector<ai::DungeonPlan const*> all = table.All();
        std::sort(all.begin(), all.end(),
            [](ai::DungeonPlan const* a, ai::DungeonPlan const* b) { return a->name < b->name; });
        for (ai::DungeonPlan const* plan : all)
        {
            PlanEntry entry;
            entry.name = plan->name;
            entry.map = plan->map;
            entry.steps = plan->steps.size();
            for (ai::PlanStep const& step : plan->steps)
                entry.unsupported += step.kind == ai::PlanStepKind::None ? 1 : 0;
            plans.push_back(entry);
        }
    }
#endif
    addon::Send(requester, RunListLine(plans));
    addon::Ack(requester, "run list", "plans", std::uint32_t(plans.size()), "");
}

void HandleRun(Player* requester, std::string_view args)
{
    RunRequest const request = ParseRunArgs(args);
    switch (request.reader)
    {
        case RunReader::Status: HandleRunStatus(requester); return;
        case RunReader::Here: HandleRunHere(requester); return;
        case RunReader::List: HandleRunList(requester); return;
        case RunReader::Other: break;
    }
    // Mutations (start/pause/...) belong to no v1 piece yet: fail closed so
    // the addon keeps them on bridge instead of hanging the click.
    addon::Err(requester, "run " + request.sub, "unsupported", "not available on this server yet");
}
std::vector<std::string> BuildFeaturesChatLines()
{
    std::vector<std::string> lines;
    lines.reserve(std::size(kFeatures));
    for (Feature const& feature : kFeatures)
        lines.push_back("TortoiseBots: feature " + std::string(feature.id) + "=" + FeatureValue(feature) + " (" + feature.label + ")");
    return lines;
}
} // namespace

void AppendCcAssign(Player* requester)
{
    // TB's roster trailer shape: BEGIN, one row per party bot holding a
    // crowd-control mark, END. TBF reads the mark from the bot's own order
    // values (host/TbCc.h); the M4 party-wide pick (open PR #208) writes the
    // same values, so it shows up here with no change.
    std::vector<std::pair<std::string, std::string>> rows;
    Group* group = requester ? requester->GetGroup() : nullptr;
    if (group)
        for (GroupReference const& reference : group->GetMembers())
        {
            Player* member = reference.GetSource();
            if (!member || !BotMgr::Instance().IsBot(member->GetGUID()))
                continue;
            std::string mark, family;
            h8::CcOrderValues(member, mark, family);
            // TB's gate (commands/Info.cpp:495): the mark alone never lists
            // a bot, since "rti cc" reads moon on every fresh bot. The strategy
            // question exists only with the engine, so the option-OFF build lists none.
#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1
            if (CcRowVisible(h8::HasCombatCcStrategy(member), mark))
                rows.emplace_back(member->GetName(), mark);
#endif
        }
    addon::Send(requester, CcBeginLine(rows.size()));
    for (auto const& row : rows)
        addon::Send(requester, CcLine(row.first, row.second));
    addon::Send(requester, std::string(kCcEnd));
}

std::vector<std::string> FeaturesChatLines()
{
    return BuildFeaturesChatLines();
}

void RegisterAddonVerbsInfo()
{
    addon::RegisterVerb("features", HandleFeatures);
    addon::RegisterVerb("run", HandleRun);
    addon::Advertise("run:status");
    addon::Advertise("run:here");
    addon::Advertise("run:list");
}
} // namespace addoninfo
} // namespace TortoiseBots
