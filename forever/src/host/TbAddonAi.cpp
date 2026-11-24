// Piece P135-C (TBF #135): `.tb ai <bot> save|load|list|reset [preset]`,
// `.tb cs <bot> [<name> ? | <name> <idx> [line]]`, and addon verb `ai` (Owner tier).
#include "host/TbAddonAi.h"
#include "slice/SliceCustomStrategyPure.h"

#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1

#include "compat/botpch_forever.h"
#include "compat/TbCompat_CharStore.h"

#include "host/TbAddonChannel.h"
#include "host/TbAddonPure.h"
#include "host/TbH3.h"
#include "host/cmd/TbCmdBinding.h"
#include "host/player/TbPlayerAuthPure.h"
#include "host/run/TbRunHooks.h"
#include "slice/SliceCustomStrategy.h"
#include "TbBotMgr.h"

#include "playerbot/PlayerbotAI.h"
#include "playerbot/PlayerbotDbStore.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/strategy/Value.h"
#include "PlayerbotAIStorage.h"

#include "BattlenetAccountMgr.h"
#include "CharacterCache.h"
#include "DatabaseEnv.h"
#include "Group.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "RBAC.h"
#include "WorldSession.h"

#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace TortoiseBots
{
namespace aipreset
{
namespace
{

using ai::slice::aipreset::AiOp;
using ai::slice::aipreset::CsOp;
using ai::slice::aipreset::ParsedAiArgs;
using ai::slice::aipreset::ParsedCsArgs;

std::set<std::string> const kStoreColumns = { "guid", "preset", "key", "value" };

std::string JoinTail(h3::Words const& args, std::size_t from)
{
    std::string out;
    for (std::size_t i = from; i < args.w.size(); ++i)
    {
        if (!out.empty())
            out.push_back(' ');
        out += args.w[i];
    }
    return out;
}

std::uint32_t AiBnet(Player* requester)
{
    WorldSession* session = requester ? requester->GetSession() : nullptr;
    if (!session)
        return 0;
    std::uint32_t const bnet = session->GetBattlenetAccountId();
    if (!bnet || Battlenet::AccountMgr::GetIdByGameAccount(session->GetAccountId()) != bnet)
        return 0;
    return bnet;
}

player::BotFacts FactsFor(Player* requester, Player* bot, std::uint32_t bnet)
{
    player::BotFacts facts;
    if (!bot)
        return facts;
    ObjectGuid const guid = bot->GetGUID();
    facts.isBot = BotMgr::Instance().IsBot(guid);
    std::uint32_t const gameAccount = sCharacterCache->GetCharacterAccountIdByGuid(guid);
    if (bnet && gameAccount && Battlenet::AccountMgr::GetIdByGameAccount(gameAccount) == bnet)
        facts.account = bnet;
    facts.master = BotMgr::Instance().MasterOf(guid).GetCounter();
    facts.commandedByCaller = requester && cmd::IsCommandedBy(requester, bot);
    if (Group* group = bot->GetGroup())
    {
        ObjectGuid const leader = group->GetLeaderGUID();
        facts.groupLeader = leader.GetCounter();
        facts.leaderIsBot = BotMgr::Instance().IsBot(leader);
    }
    facts.inRun = run::BotInRunningRun(bot);
    return facts;
}

player::Caller MakeCaller(Player* requester, std::uint32_t bnet)
{
    player::Caller caller;
    caller.gm = requester && requester->GetSession() &&
        requester->GetSession()->HasPermission(rbac::RBAC_PERM_COMMAND_GM);
    caller.account = bnet;
    caller.guid = requester ? requester->GetGUID().GetCounter() : 0;
    return caller;
}

Player* FindBotByName(std::string_view name)
{
    std::string const needle = addon::pure::Lower(name);
    for (ObjectGuid const& guid : BotMgr::Instance().BotGuids())
    {
        Player* candidate = ObjectAccessor::FindPlayer(guid);
        if (candidate && candidate->IsInWorld() && addon::pure::Lower(candidate->GetName()) == needle)
            return candidate;
    }
    return nullptr;
}

std::vector<std::string> ListPresetsForBot(uint32_t guid)
{
    std::vector<std::string> presets;
    if (!CharacterSchemaHasCompat("ai_playerbot_db_store", kStoreColumns))
        return presets;
    QueryResult results = CharacterQueryGuardedCompat(ai::slice::aipreset::ListPresetsSql(guid));
    if (!results)
        return presets;
    do
    {
        Field* fields = results->Fetch();
        std::string preset = fields[0].IsNull() ? std::string() : fields[0].GetString();
        presets.push_back(preset.empty() ? "(default)" : preset);
    } while (results->NextRow());
    return presets;
}

void ResetBotAi(Player* bot, PlayerbotAI* ai)
{
    uint32_t const guid = bot->GetGUID().GetCounter();
    if (CharacterSchemaHasCompat("ai_playerbot_db_store", kStoreColumns))
    {
        QueryResult results = CharacterQueryGuardedCompat(ai::slice::aipreset::SelectPresetValueRowsSql(guid));
        if (results)
        {
            do
            {
                Field* fields = results->Fetch();
                std::string const val = fields[0].IsNull() ? std::string() : fields[0].GetString();
                std::size_t const gt = val.find('>');
                if (gt == std::string::npos || val.find('>', gt + 1) != std::string::npos)
                    continue;
                std::string const name = val.substr(0, gt);
                if (ai::UntypedValue* value = ai->GetAiObjectContext()->GetUntypedValue(name))
                    value->Reset();
            } while (results->NextRow());
            CharacterExecuteGuardedCompat(ai::slice::aipreset::DeleteDefaultValuesSql(guid));
        }
    }
    ai->ResetStrategies(false);
    sPlayerbotDbStore.Save(ai, "");
}

bool ExecuteAiOp(Player* bot, PlayerbotAI* ai, ParsedAiArgs const& parsed, std::vector<std::string>& lines, std::string& detail)
{
    lines.clear();
    switch (parsed.op)
    {
    case AiOp::Save:
        sPlayerbotDbStore.Save(ai, parsed.preset);
        detail = parsed.preset.empty() ? "save:(default)" : ("save:" + parsed.preset);
        lines.push_back("Strategies saved");
        return true;
    case AiOp::Load:
        sPlayerbotDbStore.Load(ai, parsed.preset);
        detail = parsed.preset.empty() ? "load:(default)" : ("load:" + parsed.preset);
        lines.push_back("Strategies loaded");
        return true;
    case AiOp::List:
    {
        std::vector<std::string> presets = ListPresetsForBot(bot->GetGUID().GetCounter());
        lines.push_back("### PRESETS AVAILABLE ###");
        for (std::string const& p : presets)
            lines.push_back(p);
        detail = "list:" + std::to_string(presets.size());
        return true;
    }
    case AiOp::Reset:
        ResetBotAi(bot, ai);
        detail = "reset";
        lines.push_back("AI was reset to defaults");
        return true;
    }
    return false;
}

void HandleAiVerb(Player* requester, std::string_view rawArgs)
{
    if (!requester || !requester->IsInWorld())
    {
        addon::Err(requester, "ai", "requester", "Requester must be in world.");
        return;
    }
    ParsedAiArgs const parsed = ai::slice::aipreset::ParseAiArgs(rawArgs);
    if (!parsed.ok)
    {
        addon::Err(requester, "ai", parsed.errCode, parsed.errMsg);
        return;
    }
    Player* bot = FindBotByName(parsed.botName);
    if (!bot || bot == requester)
    {
        addon::Err(requester, "ai", "no-bot", "Unknown or uncontrollable bot name.");
        return;
    }
    std::uint32_t const bnet = AiBnet(requester);
    player::Verdict const verdict = player::Decide(MakeCaller(requester, bnet), FactsFor(requester, bot, bnet), player::Tier::Owner);
    if (verdict != player::Verdict::Allowed)
    {
        addon::Err(requester, "ai", player::CodeOf(verdict), "You may not configure that bot.");
        return;
    }
    PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
    if (!ai || !ai->GetAiObjectContext())
    {
        addon::Err(requester, "ai", "no-engine", "The bot has no running engine.");
        return;
    }

    std::vector<std::string> lines;
    std::string detail;
    ExecuteAiOp(bot, ai, parsed, lines, detail);
    for (std::string const& line : lines)
        addon::Send(requester, line);
    addon::Ack(requester, "ai", "bot:" + bot->GetName(), 1, detail);
}

Player* ResolveSingleBot(ChatHandler* handler, h3::Words const& args, char const* cmd, bool& failed)
{
    failed = true;
    if (args.badToken)
    {
        h3::Fail(handler, args, cmd, "bad-token", "token must be 1..32 characters of [A-Za-z0-9_.:-]");
        return nullptr;
    }
    if (args.w.empty())
    {
        h3::Fail(handler, args, cmd, "missing-argument", "expected <bot>");
        return nullptr;
    }
    Player* bot = h3::FindOnline(args.w[0]);
    if (!bot || !h3::IsHostBot(bot))
    {
        h3::Fail(handler, args, cmd, "no-bot", "unknown or offline bot: " + args.w[0]);
        return nullptr;
    }
    std::string code;
    std::string detail;
    if (!h3::CheckBotAccess(handler, bot, code, detail))
    {
        h3::Fail(handler, args, cmd, code, detail);
        return nullptr;
    }
    failed = false;
    return bot;
}

} // namespace

bool HandleAiCommand(ChatHandler* handler, Trinity::ChatCommands::Tail tail)
{
    h3::Words const args = h3::Split(tail);
    if (!h3::RequireBuilt(handler, args, "ai"))
        return false;
    bool failed = false;
    Player* bot = ResolveSingleBot(handler, args, "ai", failed);
    if (failed)
        return false;

    std::string const raw = bot->GetName() + (args.w.size() > 1 ? (" " + JoinTail(args, 1)) : std::string());
    ParsedAiArgs const parsed = ai::slice::aipreset::ParseAiArgs(raw);
    if (!parsed.ok)
        return h3::Fail(handler, args, "ai", parsed.errCode, parsed.errMsg);

    PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
    if (!ai || !ai->GetAiObjectContext())
        return h3::Fail(handler, args, "ai", "no-engine", "the bot has no running engine (`.tb engine <bot> on`)");

    std::vector<std::string> lines;
    std::string detail;
    ExecuteAiOp(bot, ai, parsed, lines, detail);
    for (std::string const& line : lines)
        handler->SendSysMessage(line.c_str());

    h3::Reply reply(handler, args, "ai");
    reply.Begin();
    reply.Line(h3::Rec("ai").S("bot", bot->GetName()).S("op", detail));
    reply.End(h3::Rec("ai").S("bot", bot->GetName()).S("op", detail));
    return true;
}

bool HandleCsCommand(ChatHandler* handler, Trinity::ChatCommands::Tail tail)
{
    h3::Words const args = h3::Split(tail);
    if (!h3::RequireBuilt(handler, args, "cs"))
        return false;
    bool failed = false;
    Player* bot = ResolveSingleBot(handler, args, "cs", failed);
    if (failed)
        return false;

    PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
    if (!ai || !ai->GetAiObjectContext())
        return h3::Fail(handler, args, "cs", "no-engine", "the bot has no running engine (`.tb engine <bot> on`)");

    std::string const rest = JoinTail(args, 1);
    ParsedCsArgs const parsed = ai::slice::aipreset::ParseCsArgs(rest);
    if (!parsed.ok)
        return h3::Fail(handler, args, "cs", parsed.errCode, parsed.errMsg);

    uint32_t const owner = bot->GetGUID().GetCounter();
    if (parsed.op == CsOp::Help)
    {
        std::vector<std::string> names = ai::slice::aipreset::ListCustomStrategies(owner);
        handler->SendSysMessage("=== Custom strategies ===");
        for (std::string const& name : names)
            handler->SendSysMessage(name.c_str());
        handler->SendSysMessage("Usage: .tb cs <bot> <name> <idx> <action_line>");
        h3::Reply reply(handler, args, "cs");
        reply.Begin();
        reply.Line(h3::Rec("cs").S("bot", bot->GetName()).S("op", "list").N("count", names.size()));
        reply.End(h3::Rec("cs").S("bot", bot->GetName()).S("op", "list"));
        return true;
    }
    if (parsed.op == CsOp::Print)
    {
        auto rows = ai::slice::aipreset::ReadCustomStrategyLines(owner, parsed.name);
        handler->PSendSysMessage("=== %s ===", parsed.name.c_str());
        for (auto const& row : rows)
            handler->PSendSysMessage("#%u %s", row.first, row.second.c_str());
        h3::Reply reply(handler, args, "cs");
        reply.Begin();
        reply.Line(h3::Rec("cs").S("bot", bot->GetName()).S("name", parsed.name).S("op", "print").N("count", rows.size()));
        reply.End(h3::Rec("cs").S("bot", bot->GetName()).S("name", parsed.name).S("op", "print"));
        return true;
    }

    if (!ai::slice::aipreset::EditCustomStrategyLine(ai, owner, parsed.name, parsed.idx, parsed.line))
        return h3::Fail(handler, args, "cs", "store-failed", "custom strategy table unavailable or invalid input");

    handler->PSendSysMessage("#%u %s", parsed.idx, parsed.line.c_str());
    std::string const op = parsed.line.empty() ? "delete" : "edit";
    h3::Reply reply(handler, args, "cs");
    reply.Begin();
    reply.Line(h3::Rec("cs").S("bot", bot->GetName()).S("name", parsed.name).N("idx", parsed.idx).S("op", op));
    reply.End(h3::Rec("cs").S("bot", bot->GetName()).S("name", parsed.name).S("op", op));
    return true;
}

void RegisterAddonVerbsAi()
{
    addon::RegisterVerb("ai", &HandleAiVerb);
    addon::AdvertiseCap("ai");
}

} // namespace aipreset
} // namespace TortoiseBots

#else

#include "Chat.h"

namespace TortoiseBots
{
namespace aipreset
{

namespace
{
bool NotBuilt(ChatHandler* handler)
{
    if (handler)
        handler->SendSysMessage("error code=engine-not-built");
    return false;
}
} // namespace

bool HandleAiCommand(ChatHandler* handler, Trinity::ChatCommands::Tail) { return NotBuilt(handler); }
bool HandleCsCommand(ChatHandler* handler, Trinity::ChatCommands::Tail) { return NotBuilt(handler); }
void RegisterAddonVerbsAi() { }

} // namespace aipreset
} // namespace TortoiseBots

#endif
