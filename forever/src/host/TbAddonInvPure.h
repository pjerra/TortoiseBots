// The `inv` and `item` addon verbs' core-free half (TBF #134 pieces P134-A/P134-B):
// TB's `.bot inv|inventory` and `.bot item <bot> equip|unequip|give <bag> <slot>`
// grammar (TortoiseBots/commands/Inventory.cpp:158-228,353-600) over the TBM
// channel. No core header and no legacy type; tested standalone by
// forever/tools/test_addon_inv.cpp. The live half that moves the live items
// is host/TbAddonInv.cpp; its snapshot emitter is shared with i-trade.
#pragma once

#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>

#include "runtime/policy/infra/EquipSlotPolicy.h"

namespace TortoiseBots
{
namespace inv
{
inline std::string Lower(std::string_view word)
{
    std::string out;
    out.reserve(word.size());
    for (char c : word)
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return out;
}

inline std::string Trim(std::string const& text)
{
    std::size_t begin = 0;
    while (begin < text.size() && (text[begin] == ' ' || text[begin] == '\t'))
        ++begin;
    std::size_t end = text.size();
    while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t'))
        --end;
    return text.substr(begin, end - begin);
}

inline char const* InvUsage()
{
    return "Usage: inv <bot>";
}

inline char const* ItemUsage()
{
    return "Usage: item <bot> trade | equip/unequip/give <bag> <slot> [mainhand|offhand|finger1|finger2|trinket1|trinket2]";
}

// One parsed `inv <bot>` tail (TB's HandleInventory takes the bare bot name).
struct InvRequest
{
    bool ok = false;
    std::string bot;
    std::string error;
    std::string message;
};

inline InvRequest ParseInvRequest(std::string_view args)
{
    InvRequest out;
    out.error = "invalid";
    out.message = InvUsage();
    std::string const input = Trim(std::string(args));
    if (input.empty() || input.find_first_of(" \t") != std::string::npos)
        return out;
    out.ok = true;
    out.bot = Lower(input);
    out.error.clear();
    out.message.clear();
    return out;
}

// One parsed `item <bot> <op> ...` tail (TB's HandleItem: intent `item <op>`).
struct ItemRequest
{
    enum class Op
    {
        Equip,
        Unequip,
        Give,
        Trade,
        Unknown
    };

    bool ok = false;
    std::string intent = "item ?";
    std::string bot;
    Op op = Op::Unknown;
    bool hasPos = false;
    std::uint32_t bag = 0;
    std::uint32_t slot = 0;
    bool hasTargetSlot = false;
    std::uint8_t targetSlot = 0;
    std::string error;
    std::string message;
};

inline bool ParseBagSlot(std::string const& word, std::uint32_t& number)
{
    if (word.empty() || word.size() > 3)
        return false;
    std::uint32_t value = 0;
    for (char c : word)
    {
        if (c < '0' || c > '9')
            return false;
        value = value * 10 + static_cast<std::uint32_t>(c - '0');
    }
    if (value > 255)
        return false;
    number = value;
    return true;
}

inline ItemRequest ParseItemRequest(std::string_view args)
{
    ItemRequest out;
    out.error = "invalid";
    out.message = ItemUsage();
    std::string const input = Trim(std::string(args));
    std::size_t pos = input.find_first_of(" \t");
    std::string const bot = Lower(input.substr(0, pos));
    std::string rest = pos == std::string::npos ? std::string() : Trim(input.substr(pos + 1));
    if (bot.empty())
        return out;
    out.bot = bot;
    pos = rest.find_first_of(" \t");
    std::string const opWord = Lower(rest.substr(0, pos));
    rest = pos == std::string::npos ? std::string() : Trim(rest.substr(pos + 1));
    if (opWord == "equip")
        out.op = ItemRequest::Op::Equip;
    else if (opWord == "unequip")
        out.op = ItemRequest::Op::Unequip;
    else if (opWord == "give")
        out.op = ItemRequest::Op::Give;
    else if (opWord == "trade")
        out.op = ItemRequest::Op::Trade;
    else
        return out;
    out.intent = "item " + opWord;
    if (out.op == ItemRequest::Op::Trade)
    {
        out.ok = true;
        out.error.clear();
        out.message.clear();
        return out;
    }
    pos = rest.find_first_of(" \t");
    std::string const bagWord = rest.substr(0, pos);
    std::string afterBag = pos == std::string::npos ? std::string() : Trim(rest.substr(pos + 1));
    pos = afterBag.find_first_of(" \t");
    std::string const slotWord = afterBag.substr(0, pos);
    std::string const slotName = pos == std::string::npos ? std::string() : Trim(afterBag.substr(pos + 1));
    std::uint32_t bag = 0;
    std::uint32_t slot = 0;
    if (!ParseBagSlot(bagWord, bag) || !ParseBagSlot(slotWord, slot))
        return out;
    out.hasPos = true;
    out.bag = bag;
    out.slot = slot;
    if (!slotName.empty())
    {
        std::uint8_t target = 0;
        if (!ParseEquipTargetSlot(slotName, target))
            return out;
        out.hasTargetSlot = true;
        out.targetSlot = target;
    }
    out.ok = true;
    out.error.clear();
    out.message.clear();
    return out;
}

inline char const* ItemOpWord(ItemRequest::Op op)
{
    switch (op)
    {
        case ItemRequest::Op::Equip: return "equip";
        case ItemRequest::Op::Unequip: return "unequip";
        case ItemRequest::Op::Give: return "give";
        case ItemRequest::Op::Trade: return "trade";
        case ItemRequest::Op::Unknown: break;
    }
    return "?";
}

// TB's ACK scope for item orders (Inventory.cpp): the bot, never the party.
inline std::string ItemScope(std::string const& bot)
{
    return "bot:" + bot;
}

// One `eub` flag field (Inventory.cpp InventoryItemFlags): e = the bot can
// equip it, u = the mature item-usage value calls it an upgrade,
// b = it cannot be traded to the requester right now.
inline std::string InvFlags(bool equippable, bool upgrade, bool noTrade)
{
    std::string flags;
    if (equippable)
        flags += 'e';
    if (upgrade)
        flags += 'u';
    if (noTrade)
        flags += 'b';
    return flags.empty() ? "-" : flags;
}

// The TBM:INV_* snapshot (Inventory.cpp:158-211), the lines BotPanel parses.
inline std::string FormatInvBegin(std::string const& bot, unsigned long long copper)
{
    return "TBM:INV_BEGIN|" + bot + "|" + std::to_string(copper);
}

inline std::string FormatInvEq(std::uint32_t slot, std::uint32_t itemId, std::uint32_t dura, std::uint32_t maxDura)
{
    return "TBM:INV_EQ|" + std::to_string(slot) + "|" + std::to_string(itemId) + "|" + std::to_string(dura) +
        "|" + std::to_string(maxDura);
}

inline std::string FormatInvEqBag(std::uint32_t slot, std::uint32_t itemId, std::uint32_t bagSize)
{
    return "TBM:INV_EQ|" + std::to_string(slot) + "|" + std::to_string(itemId) + "|0|0|" + std::to_string(bagSize);
}

inline std::string FormatInvItem(std::uint32_t bag, std::uint32_t slot, std::uint32_t itemId, std::uint32_t count,
    std::string const& flags)
{
    return "TBM:INV_ITEM|" + std::to_string(bag) + "|" + std::to_string(slot) + "|" + std::to_string(itemId) +
        "|" + std::to_string(count) + "|" + flags;
}

inline std::string FormatInvEnd(std::string const& bot, std::uint32_t free, std::uint32_t total)
{
    return "TBM:INV_END|" + bot + "|" + std::to_string(free) + "|" + std::to_string(total);
}
} // namespace inv
} // namespace TortoiseBots
