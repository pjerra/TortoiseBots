// S1-R1: the chat link parser both core sides read chat text with, core-free so a unit test can prove it (forever/tools/test_chat_link.cpp).
//
// ForeverCore's chat text carries the modern link layouts (|Hitem:19019::3::::::60:::::::::|h, |Hquest:783:1:1:60|h, |Hspell:585:0|h); the bot
// code asks the questions the 1.12 ChatHandler helpers answered (ExtractKeyFromLink / ExtractSpellIdFromLink, ai/cmangos-compat-unported.h).
// The modern item layout puts a GEM in field 3, where 1.12 has the random property (HyperlinkTags.cpp:283 at the core pin), so the qualifier
// is normalized to `id:enchant:0`: the port's item-random-properties row is unported and an item carries no random property on this core.
// A link or number that cannot be read answers 0 (or an empty list): the caller reads that as "no id", never as an invented one.
#pragma once

#include "../../../ai/playerbot/ChatLinkScan.h"

#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace TortoiseBots
{
namespace chatlink
{
namespace detail
{
inline bool IsDigit(char c) { return c >= '0' && c <= '9'; }

inline bool IsSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

// The value of the digit run starting at `begin` (the run's end goes into `end`), or -1 when there is no run or it is longer than a
// 32-bit id (9 digits): a longer run is not an id and the caller keeps looking.
inline std::int64_t DigitValue(std::string_view text, size_t begin, size_t& end)
{
    end = begin;
    while (end < text.size() && IsDigit(text[end]))
        ++end;
    if (end == begin || end - begin > 9)
        return -1;
    std::int64_t value = 0;
    for (size_t i = begin; i < end; ++i)
        value = value * 10 + (text[i] - '0');
    return value;
}

inline std::string_view Trim(std::string_view text)
{
    size_t first = 0;
    size_t last = text.size();
    while (first < last && IsSpace(text[first]))
        ++first;
    while (last > first && IsSpace(text[last - 1]))
        --last;
    return text.substr(first, last - first);
}
}

// The id of the first `|H<tag>:<digits>` link in `text`, else 0. The tag must end at the ':' so `item` does not match `|Hitemset:`.
inline std::uint32_t LinkId(std::string_view text, std::string_view tag)
{
    std::string needle("|H");
    needle.append(tag);
    needle += ':';
    size_t pos = 0;
    while ((pos = text.find(needle, pos)) != std::string_view::npos)
    {
        size_t end = 0;
        std::int64_t const value = detail::DigitValue(text, pos + needle.size(), end);
        if (value >= 0)
            return static_cast<std::uint32_t>(value);
        pos += needle.size();
    }
    return 0;
}

// Every id of every `|H<tag>:<digits>` link, once each; a link whose field is not a 1-9 digit id is not in the list.
inline std::set<std::uint32_t> AllIds(std::string_view text, std::string_view tag)
{
    std::string needle("|H");
    needle.append(tag);
    needle += ':';
    std::set<std::uint32_t> ids;
    size_t pos = 0;
    while ((pos = text.find(needle, pos)) != std::string_view::npos)
    {
        size_t end = 0;
        std::int64_t const value = detail::DigitValue(text, pos + needle.size(), end);
        if (value > 0)
            ids.insert(static_cast<std::uint32_t>(value));
        pos = (end > pos ? end : pos + needle.size());
    }
    return ids;
}

// A bare number: the trimmed text of 1-9 digits (the quest branch's rule), else 0.
inline std::uint32_t BareNumber(std::string_view text)
{
    std::string_view const trimmed = detail::Trim(text);
    if (trimmed.empty() || trimmed.size() > 9)
        return 0;
    std::uint32_t value = 0;
    for (char c : trimmed)
    {
        if (!detail::IsDigit(c))
            return 0;
        value = value * 10 + (c - '0');
    }
    return value;
}

// A quest id: the `quest` link, else a bare number, else 0.
inline std::uint32_t QuestId(std::string_view text)
{
    std::uint32_t const id = LinkId(text, "quest");
    return id ? id : BareNumber(text);
}

// A spell id: the `spell` link, then the `enchant` recipe link, else a bare number, else 0. `talent` and `trade` answer 0: a talent's
// RankID lookup is not ported, and the trade link's layout differs.
inline std::uint32_t SpellId(std::string_view text)
{
    std::uint32_t const id = LinkId(text, "spell");
    if (id)
        return id;
    std::uint32_t const enchant = LinkId(text, "enchant");
    if (enchant)
        return enchant;
    return BareNumber(text);
}

// The id and qualifier of every `Hitem:` link, once per link, in the order it is written. The scan is `ai::chatlink::ItemLinks`: the bot
// brain's ChatHelper::parseItemQualifiers reads the same function, so the console and the brain cannot disagree about an item link.
inline std::vector<ai::chatlink::ItemLink> ItemLinks(std::string_view text) { return ai::chatlink::ItemLinks(text); }

// The qualifier text of every `Hitem:` link - what stands between `Hitem:` and the next '|' - in the order it is written.
inline std::vector<std::string> ItemQualifiers(std::string_view text)
{
    std::vector<std::string> qualifiers;
    for (ai::chatlink::ItemLink const& link : ai::chatlink::ItemLinks(text))
        qualifiers.push_back(link.qualifier);
    return qualifiers;
}

// An item qualifier in the 1.12 layout the bot's ItemQualifier reads (`id:enchant:randomProperty[:suffix]`). A qualifier of 3 or 4 fields
// is the 1.12 one (a short one too: short cannot be modern, the modern link has 13 fields) and keeps those fields, every one written as a
// number (`ai::chatlink::FieldValue`: 1-9 digits, else 0; the random property is `SignedFieldValue`, a negative suffix id stays negative). A longer one is the modern layout and keeps the id and the enchant, with the
// random property 0 (field 3 is a gem there, not a random property). No field reaches the bot's ItemQualifier as chat text.
inline std::string LegacyItemQualifier(std::string_view qualifier)
{
    if (qualifier.empty())
        return std::string();
    std::vector<std::string_view> fields;
    size_t start = 0;
    while (true)
    {
        size_t const colon = qualifier.find(':', start);
        fields.push_back(qualifier.substr(start, colon == std::string_view::npos ? std::string_view::npos : colon - start));
        if (colon == std::string_view::npos)
            break;
        start = colon + 1;
    }
    std::string out;
    size_t const kept = fields.size() <= 4 ? fields.size() : 2;
    for (size_t i = 0; i < kept; ++i)
    {
        if (i)
            out += ':';
        // Field 3 of the 1.12 layout is the random property, an int32 that is negative for suffix items: it keeps its sign.
        if (i == 2 && fields.size() <= 4)
            out += std::to_string(ai::chatlink::SignedFieldValue(fields[i]));
        else
            out += std::to_string(ai::chatlink::FieldValue(fields[i]));
    }
    if (fields.size() > 4)
        out += ":0";
    return out;
}

// A reply value with the grammar-2 encoding applied (the command needs no encoder from the engine build): [A-Za-z0-9._-] and the
// qualifier's ':' stand as they are, every other byte is %XX. A space, '=' or newline from the chat text can then neither add a key
// nor split the record.
inline std::string EncodedValue(std::string_view value)
{
    static char const hex[] = "0123456789ABCDEF";
    std::string out;
    for (unsigned char const c : value)
    {
        if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            c == '.' || c == '_' || c == '-' || c == ':')
            out += static_cast<char>(c);
        else
        {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 0x0F];
        }
    }
    return out;
}
}
}
