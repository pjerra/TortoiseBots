// S1-R2: the `.tb hear` handler (see host/chat/TbChatCmd.h). It runs in the engine build only; the option-OFF build's refusal is in
// TbH3Off.cpp.
#include "host/chat/TbChatCmd.h"
#include "host/chat/TbChatIn.h"
#include "host/TbH3.h"

#include "Player.h"

#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace TortoiseBots
{
namespace chatin
{
namespace
{
// The tail after the first `count` words, whitespace before it dropped: the text as it was typed, spaces and all.
std::string RestAfter(std::string_view tail, size_t count)
{
    size_t pos = 0;
    for (size_t word = 0; word < count && pos < tail.size(); ++word)
    {
        while (pos < tail.size() && std::isspace(static_cast<unsigned char>(tail[pos])))
            ++pos;
        while (pos < tail.size() && !std::isspace(static_cast<unsigned char>(tail[pos])))
            ++pos;
    }
    while (pos < tail.size() && std::isspace(static_cast<unsigned char>(tail[pos])))
        ++pos;
    return std::string(tail.substr(pos));
}
}

bool HandleHear(ChatHandler* handler, Trinity::ChatCommands::Tail tail)
{
    h3::Words const args = h3::Split(tail);
    if (!h3::RequireBuilt(handler, args, "hear"))
        return false;
    if (args.w.size() < 3)
        return h3::Fail(handler, args, "hear", "missing-argument", ".tb hear <bot> <sender> whisper|party <text>");

    std::string const kindWord = h3::Lower(args.w[2]);
    unsigned kind = 0;
    if (kindWord == "whisper")
        kind = KindWhisper;
    else if (kindWord == "party")
        kind = KindParty;
    else
        return h3::Fail(handler, args, "hear", "bad-argument", "the kind is whisper or party");

    std::string text = RestAfter(std::string_view(tail), 3);
    // The echo token is not part of the text (`Split` took it out of the words; here it is still at the end of the tail).
    if (!args.token.empty())
    {
        size_t end = text.size();
        while (end > 0 && std::isspace(static_cast<unsigned char>(text[end - 1])))
            --end;
        if (end >= args.token.size() && text.compare(end - args.token.size(), args.token.size(), args.token) == 0)
        {
            size_t start = end - args.token.size();
            while (start > 0 && std::isspace(static_cast<unsigned char>(text[start - 1])))
                --start;
            text.erase(start);
        }
    }
    if (text.empty())
        return h3::Fail(handler, args, "hear", "missing-argument", ".tb hear <bot> <sender> whisper|party <text>");

    Player* bot = h3::FindOnline(args.w[0]);
    if (!bot)
        return h3::Fail(handler, args, "hear", "no-such-player", args.w[0]);
    if (!h3::IsHostBot(bot))
        return h3::Fail(handler, args, "hear", "not-a-bot", args.w[0]);
    Player* sender = h3::FindOnline(args.w[1]);
    if (!sender)
        return h3::Fail(handler, args, "hear", "no-such-player", args.w[1]);

    Verdict const verdict = Hear(bot, sender, kind, text);

    h3::Reply reply(handler, args, "hear");
    reply.Begin();
    h3::Rec rec("hear");
    rec.S("bot", bot->GetName()).S("sender", sender->GetName()).S("kind", kindWord)
        .S("verdict", CodeName(verdict.code)).B("queued", verdict.accepted());
    reply.Line(rec);
    h3::Rec end("hear");
    reply.End(end);
    return true;
}
}
}
