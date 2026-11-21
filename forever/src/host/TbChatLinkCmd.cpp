#include "host/TbChatLinkCmd.h"

#include "Chat.h"
#include "compat/TbChatLinkPure.h"

#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>

namespace TortoiseBots
{
namespace chatlink
{
namespace
{
// The grammar's percent-encoding is the identity for everything this reply writes: the ids are digits, `;` separates list items and `:` the
// parts of a qualifier. The two refusals below carry fixed text, encoded here by hand (the grammar's [A-Za-z0-9._-] rule).
bool Fail(ChatHandler* handler, char const* line, std::string const& token)
{
    std::string text(line);
    if (!token.empty())
        text += " " + token;
    handler->SendSysMessage(text);
    handler->SetSentErrorMessage(true);
    return false;
}
}

bool HandleLink(ChatHandler* handler, Trinity::ChatCommands::Tail tail)
{
    // The text as typed, with the echo token split off the end exactly as the engine build's commands split it (a '#' word, 1-32 of
    // [A-Za-z0-9_.:-] after it; a malformed one is refused before the text is read).
    std::string text(tail);
    std::string token;
    bool badToken = false;
    {
        size_t end = text.size();
        while (end > 0 && std::isspace(static_cast<unsigned char>(text[end - 1])))
            --end;
        size_t start = end;
        while (start > 0 && !std::isspace(static_cast<unsigned char>(text[start - 1])))
            --start;
        if (end > start && text[start] == '#')
        {
            std::string const word = text.substr(start, end - start);
            bool good = word.size() >= 2 && word.size() <= 33;
            for (size_t i = 1; good && i < word.size(); ++i)
            {
                char const c = word[i];
                good = std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.' || c == ':' || c == '-';
            }
            if (good)
                token = word;
            else
                badToken = true;
            text.erase(start);
        }
    }
    if (badToken)
        return Fail(handler, "error code=bad-token cmd=link msg=a%20token%20is%20%23%20and%201%20to%2032%20of%20A-Za-z0-9_.%3A-%3B%20nothing%20was%20done", "");
    if (detail::Trim(text).empty())
        return Fail(handler, "error code=missing-argument cmd=link msg=.tb%20link%20%3Ctext%3E", token);

    // One scan feeds both lists, so an item can never be in one and not the other. Every value is encoded before it reaches the
    // record: the fields are digits, but the encoder is what keeps a malformed qualifier from adding a key or a line.
    std::string items;
    std::string qualifiers;
    for (ai::chatlink::ItemLink const& link : ItemLinks(text))
    {
        items += (items.empty() ? "" : ";") + std::to_string(link.id);
        std::string const legacy = LegacyItemQualifier(link.qualifier);
        if (!legacy.empty())
            qualifiers += (qualifiers.empty() ? "" : ";") + EncodedValue(legacy);
    }

    std::string const line = "link quest=" + std::to_string(QuestId(text)) + " spell=" + std::to_string(SpellId(text)) +
        " items=" + items + " quals=" + qualifiers + (token.empty() ? "" : " " + token);
    handler->SendSysMessage("link-begin" + (token.empty() ? std::string() : " " + token));
    handler->SendSysMessage(line);
    handler->SendSysMessage("link-end count=1 records=1" + (token.empty() ? std::string() : " " + token));
    return true;
}
}
}
