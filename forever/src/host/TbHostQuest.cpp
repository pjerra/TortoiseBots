// W3: `.tb quest <bot> status|accept|turnin|share|drop|reward` command handler.
// Compiled only when TORTOISEBOTS_SLICE1 is ON (`TbHost*.cpp` glob in CMakeLists.txt);
// TbH3Off.cpp answers `error code=engine-not-built cmd=quest` when OFF.
#include "host/TbH3.h"
#include "TbBotMgr.h"
#include "host/TbEngineBrain.h"

#include "compat/botpch_forever.h"

#include "ObjectMgr.h"
#include "Player.h"
#include "QuestDef.h"

#include "playerbot/PlayerbotAI.h"
#include "playerbot/strategy/AiObjectContext.h"
#include "playerbot/strategy/Event.h"
#include "playerbot/strategy/Value.h"
#include "playerbot/strategy/values/QuestValues.h"
#include "PlayerbotAIStorage.h"

#include <cstdlib>
#include <string>
#include <vector>

namespace TortoiseBots
{
namespace h3
{
namespace
{
bool SendReply(ChatHandler* handler, Words const& args, std::vector<Rec> const& lines, Rec const& end)
{
    Reply reply(handler, args, "quest");
    reply.Begin();
    for (Rec const& line : lines)
        reply.Line(line);
    reply.End(end);
    return true;
}

bool ParseQuestId(std::string const& word, uint32& out)
{
    if (word.empty() || word.size() > 9)
        return false;
    char* end = nullptr;
    unsigned long const value = std::strtoul(word.c_str(), &end, 10);
    if (!end || *end != '\0' || !value)
        return false;
    out = uint32(value);
    return true;
}

std::string QuestStatusName(QuestStatus status)
{
    switch (status)
    {
        case QUEST_STATUS_NONE:       return "none";
        case QUEST_STATUS_COMPLETE:   return "complete";
        case QUEST_STATUS_INCOMPLETE: return "incomplete";
        case QUEST_STATUS_FAILED:     return "failed";
        case QUEST_STATUS_REWARDED:   return "rewarded";
        default:                      return "other";
    }
}

std::string ObjectivesSummary(Player const* bot, Quest const* quest)
{
    if (!bot || !quest)
        return {};
    std::string out;
    for (QuestObjective const& obj : quest->GetObjectives())
    {
        if (obj.Amount <= 0)
            continue;
        int32 const done = bot->GetQuestObjectiveData(obj);
        if (!out.empty())
            out += ';';
        out += std::to_string(done) + "/" + std::to_string(obj.Amount);
    }
    return out;
}

Player* ResolveQuestBot(ChatHandler* handler, Words const& args)
{
    if (args.w.empty())
    {
        Fail(handler, args, "quest", "missing-target", "name a bot first");
        return nullptr;
    }
    Player* bot = FindOnline(args.w[0]);
    if (!bot)
    {
        Fail(handler, args, "quest", "no-such-player", "no online character named " + args.w[0]);
        return nullptr;
    }
    if (!IsHostBot(bot))
    {
        Fail(handler, args, "quest", "not-a-bot", args.w[0] + " is not a host bot");
        return nullptr;
    }
    if (!MayLeadBots(handler, bot->GetGUID()))
    {
        Fail(handler, args, "quest", "not-allowed", "not allowed to command " + args.w[0]);
        return nullptr;
    }
    return bot;
}
} // namespace

bool HandleQuest(ChatHandler* handler, Trinity::ChatCommands::Tail tail)
{
    Words const args = Split(std::string_view(tail));
    if (args.badToken)
        return Fail(handler, args, "quest", "bad-argument", "malformed echo token");
    if (!engine::Built())
        return Fail(handler, args, "quest", "engine-not-built", "the engine is not built in");

    Player* bot = ResolveQuestBot(handler, args);
    if (!bot)
        return false;
    if (args.w.size() < 2)
        return Fail(handler, args, "quest", "missing-verb", "expected status|accept|turnin|share|drop|reward");

    std::string const& verb = args.w[1];
    if (verb == "status")
    {
        if (args.w.size() > 2)
            return Fail(handler, args, "quest", "unexpected-argument", "unexpected argument after status");
        std::vector<Rec> lines;
        uint32 quests = 0;
        for (uint16 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
        {
            uint32 const qId = GetQuestSlotIdCompat(bot, slot);
            if (!qId)
                continue;
            Quest const* q = sObjectMgr.GetQuestTemplate(qId);
            if (!q)
                continue;
            ++quests;
            Rec r("quest");
            r.S("bot", bot->GetName())
             .N("id", qId)
             .S("title", QuestTitleCompat(q))
             .S("status", QuestStatusName(bot->GetQuestStatus(qId)))
             .S("objectives", ObjectivesSummary(bot, q));
            lines.push_back(std::move(r));
        }
        Rec end("quest-end");
        end.S("bot", bot->GetName()).S("verb", "status").N("quests", quests);
        return SendReply(handler, args, lines, end);
    }

    PlayerbotAI* ai = PlayerbotAIStorage::Instance().GetAI(bot);
    if (!ai)
        return Fail(handler, args, "quest", "no-engine", "the bot has no running engine (`.tb engine <bot> on`)");

    {
        std::string code, detail;
        if (!CheckBotAccess(handler, bot, code, detail))
            return Fail(handler, args, "quest", code, detail);
    }

    if (verb == "accept")
    {
        if (args.w.size() < 3)
            return Fail(handler, args, "quest", "missing-argument", "expected <questId|*>");
        if (args.w.size() > 3)
            return Fail(handler, args, "quest", "unexpected-argument", "unexpected argument after quest id");
        std::string const& target = args.w[2];
        uint32 questId = 0;
        if (target != "*")
        {
            if (!ParseQuestId(target, questId))
                return Fail(handler, args, "quest", "bad-id", "invalid quest id: " + target);
            Quest const* q = sObjectMgr.GetQuestTemplate(questId);
            if (!q || !QuestIsVanillaEligibleCompat(q))
            {
                std::vector<Rec> lines;
                Rec r("quest");
                r.S("bot", bot->GetName()).S("verb", "accept").N("id", questId).S("status", "refused").S("code", "filtered");
                lines.push_back(std::move(r));
                Rec end("quest-end");
                end.S("bot", bot->GetName()).S("verb", "accept");
                return SendReply(handler, args, lines, end);
            }
        }

        ai::Event ev("tb_quest", target, bot);
        bool const ok = ai->DoSpecificAction("accept quest", ev);
        std::vector<Rec> lines;
        Rec r("quest");
        r.S("bot", bot->GetName()).S("verb", "accept");
        if (questId)
            r.N("id", questId);
        bool const accepted = questId ? QuestIsAcceptedStatusCompat(bot->GetQuestStatus(questId)) : ok;
        r.S("status", accepted ? "accepted" : "refused");
        lines.push_back(std::move(r));
        Rec end("quest-end");
        end.S("bot", bot->GetName()).S("verb", "accept");
        return SendReply(handler, args, lines, end);
    }

    if (verb == "turnin")
    {
        if (args.w.size() > 3)
            return Fail(handler, args, "quest", "unexpected-argument", "unexpected argument");
        uint32 questId = 0;
        if (args.w.size() == 3 && !ParseQuestId(args.w[2], questId))
            return Fail(handler, args, "quest", "bad-id", "invalid quest id: " + args.w[2]);

        ai::Event ev("tb_quest", "", bot);
        bool const ok = ai->DoSpecificAction("talk to quest giver", ev);
        bool const rewarded = questId ? bot->GetQuestRewardStatus(questId) : ok;
        std::vector<Rec> lines;
        Rec r("quest");
        r.S("bot", bot->GetName()).S("verb", "turnin");
        if (questId)
            r.N("id", questId);
        r.S("status", rewarded ? "ok" : "no-turnin");
        lines.push_back(std::move(r));
        Rec end("quest-end");
        end.S("bot", bot->GetName()).S("verb", "turnin");
        return SendReply(handler, args, lines, end);
    }

    if (verb == "share")
    {
        if (args.w.size() < 3)
            return Fail(handler, args, "quest", "missing-argument", "expected <questId>");
        if (args.w.size() > 3)
            return Fail(handler, args, "quest", "unexpected-argument", "unexpected argument");
        uint32 questId = 0;
        if (!ParseQuestId(args.w[2], questId))
            return Fail(handler, args, "quest", "bad-id", "invalid quest id: " + args.w[2]);
        ai::Event ev("tb_quest", args.w[2], bot);
        bool const shared = ai->DoSpecificAction("share", ev);
        std::vector<Rec> lines;
        Rec r("quest");
        r.S("bot", bot->GetName()).S("verb", "share").N("id", questId).S("status", shared ? "shared" : "refused");
        lines.push_back(std::move(r));
        Rec end("quest-end");
        end.S("bot", bot->GetName()).S("verb", "share");
        return SendReply(handler, args, lines, end);
    }

    if (verb == "drop")
    {
        if (args.w.size() < 3)
            return Fail(handler, args, "quest", "missing-argument", "expected <questId|all>");
        if (args.w.size() > 3)
            return Fail(handler, args, "quest", "unexpected-argument", "unexpected argument");
        std::string const& target = args.w[2];
        uint32 questId = 0;
        bool dropped = false;
        if (target == "all")
        {
            for (uint32 id : ai->GetAllCurrentQuestIds())
            {
                ai->DropQuest(id);
                dropped = true;
            }
        }
        else
        {
            if (!ParseQuestId(target, questId))
                return Fail(handler, args, "quest", "bad-id", "invalid quest id: " + target);
            if (bot->GetQuestStatus(questId) != QUEST_STATUS_NONE)
            {
                ai->DropQuest(questId);
                dropped = (bot->GetQuestStatus(questId) == QUEST_STATUS_NONE);
            }
        }
        std::vector<Rec> lines;
        Rec r("quest");
        r.S("bot", bot->GetName()).S("verb", "drop");
        if (questId)
            r.N("id", questId);
        r.S("status", dropped ? "dropped" : "refused");
        lines.push_back(std::move(r));
        Rec end("quest-end");
        end.S("bot", bot->GetName()).S("verb", "drop");
        return SendReply(handler, args, lines, end);
    }

    if (verb == "reward")
    {
        if (args.w.size() < 3)
            return Fail(handler, args, "quest", "missing-argument", "expected auto|list|ask|reset|?");
        if (args.w.size() > 3)
            return Fail(handler, args, "quest", "unexpected-argument", "unexpected argument");
        std::string const& mode = args.w[2];
        if (mode != "auto" && mode != "list" && mode != "ask" && mode != "reset" && mode != "?")
            return Fail(handler, args, "quest", "bad-argument", "expected auto|list|ask|reset|?");
        ai::Event ev("tb_quest", mode, bot);
        ai->DoSpecificAction("quest reward", ev);
        uint8 const cur = ai->GetAiObjectContext()->GetValue<uint8>("quest reward")->Get();
        char const* modeName = "config";
        switch (static_cast<ai::QuestRewardOptionType>(cur))
        {
            case ai::QuestRewardOptionType::QUEST_REWARD_OPTION_AUTO: modeName = "auto"; break;
            case ai::QuestRewardOptionType::QUEST_REWARD_OPTION_LIST: modeName = "list"; break;
            case ai::QuestRewardOptionType::QUEST_REWARD_OPTION_ASK:  modeName = "ask"; break;
            default: break;
        }
        std::vector<Rec> lines;
        Rec r("quest");
        r.S("bot", bot->GetName()).S("verb", "reward").S("mode", modeName);
        lines.push_back(std::move(r));
        Rec end("quest-end");
        end.S("bot", bot->GetName()).S("verb", "reward");
        return SendReply(handler, args, lines, end);
    }

    return Fail(handler, args, "quest", "unknown-verb", "unknown quest verb: " + verb);
}

void RebuildQuestEligibility()
{
    RebuildQuestEligibilityCompat();
}

} // namespace h3
} // namespace TortoiseBots
