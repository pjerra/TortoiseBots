// The chat link scan both the bot brain and the ForeverCore pure parser read item links with: core-free (standard headers only), so
// forever/tools/test_chat_link.cpp proves the same code the world thread runs, and one rule means `items=` and `quals=` cannot disagree.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ai
{
namespace chatlink
{
namespace detail
{
inline bool IsDigit(char c) { return c >= '0' && c <= '9'; }
}

// One numeric qualifier field: 1-9 digits as a value, else 0. Never throws; a longer run is not a field this port reads (and cannot
// overflow the 32-bit id space), and a field with anything but digits is no number.
inline std::uint32_t FieldValue(std::string_view field)
{
    if (field.empty() || field.size() > 9)
        return 0;
    std::uint32_t value = 0;
    for (char c : field)
    {
        if (!detail::IsDigit(c))
            return 0;
        value = value * 10 + (c - '0');
    }
    return value;
}

// Plain unsigned decimal chat text as a 32-bit value: 1-10 digits that fit, nothing else (no sign, no space). False for anything
// else, an overflow included; `out` is then untouched. Never throws, so a long digit run in chat cannot raise std::out_of_range.
inline bool ParseUnsigned(std::string_view text, std::uint32_t& out)
{
    if (text.empty() || text.size() > 10)
        return false;
    std::uint64_t value = 0;
    for (char c : text)
    {
        if (!detail::IsDigit(c))
            return false;
        value = value * 10 + static_cast<std::uint64_t>(c - '0');
    }
    if (value > 0xFFFFFFFFull)
        return false;
    out = static_cast<std::uint32_t>(value);
    return true;
}

// Signed decimal chat text as a 32-bit value: an optional '+' or '-', then 1-10 digits, and the whole number inside int32. False for
// anything else (overflow, a lone sign, any other character); `out` is then untouched. Never throws.
inline bool ParseSigned(std::string_view text, std::int32_t& out)
{
    bool negative = false;
    if (!text.empty() && (text[0] == '-' || text[0] == '+'))
    {
        negative = text[0] == '-';
        text.remove_prefix(1);
    }
    if (text.empty() || text.size() > 10)
        return false;
    std::int64_t value = 0;
    for (char c : text)
    {
        if (!detail::IsDigit(c))
            return false;
        value = value * 10 + (c - '0');
    }
    if (negative)
        value = -value;
    if (value < INT32_MIN || value > INT32_MAX)
        return false;
    out = static_cast<std::int32_t>(value);
    return true;
}

// A signed qualifier field (the random property of an item: negative for "of the X" suffix items). The value when ParseSigned reads it,
// else 0 - an out-of-range or garbage field is refused, a valid negative one is kept. Never throws.
inline std::int32_t SignedFieldValue(std::string_view field)
{
    std::int32_t value = 0;
    return ParseSigned(field, value) ? value : 0;
}

// Money text in the `##g##s##c` format (the first space ends it, any other character makes it 0; digits with no unit letter are
// dropped). Every index is size_t (chat text can be 511 characters, an 8-bit counter wraps at 255 and never ends) and every number
// saturates, so a 600-digit run is a large amount, not an overflow.
inline std::uint32_t ParseMoney(std::string_view text)
{
    constexpr std::uint64_t cap = 0xFFFFFFFFull;
    std::uint64_t copper = 0;
    std::uint64_t acum = 0;
    for (std::size_t i = 0; i < text.size(); ++i)
    {
        char const c = text[i];
        if (c == 'g' || c == 's' || c == 'c')
        {
            std::uint64_t const unit = c == 'g' ? 10000 : (c == 's' ? 100 : 1);
            copper += acum * unit;
            if (copper > cap)
                copper = cap;
            acum = 0;
        }
        else if (c == ' ')
            break;
        else if (detail::IsDigit(c))
        {
            acum = acum * 10 + static_cast<std::uint64_t>(c - '0');
            if (acum > cap)
                acum = cap;
        }
        else
        {
            copper = 0;
            break;
        }
    }
    return static_cast<std::uint32_t>(copper);
}

// The id and qualifier of every `Hitem:` link in the order written: the qualifier is the text between `Hitem:` and the next '|'.
// An unterminated link has no qualifier and ends the scan; a link whose id field is not a 1-9 digit number is not an item.
struct ItemLink
{
    std::uint32_t id;
    std::string qualifier;
};

inline std::vector<ItemLink> ItemLinks(std::string_view text)
{
    std::vector<ItemLink> links;
    std::size_t pos = 0;
    while ((pos = text.find("Hitem:", pos)) != std::string_view::npos)
    {
        std::size_t const begin = pos + 6;
        std::size_t const end = text.find('|', begin);
        if (end == std::string_view::npos)
            break;
        std::size_t const colon = text.find(':', begin);
        std::size_t const idEnd = (colon == std::string_view::npos || colon > end) ? end : colon;
        std::uint32_t const id = FieldValue(text.substr(begin, idEnd - begin));
        if (id)
            links.push_back(ItemLink{id, std::string(text.substr(begin, end - begin))});
        pos = end;
    }
    return links;
}
}
}
