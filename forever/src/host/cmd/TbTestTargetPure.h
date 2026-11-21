// TB-1 (FOREVER-AGENT-QUALITY-SPEC): the PURE half of "who may a test-kit command change". No core header and no legacy type; tested standalone by
// forever/tools/test_tb_self_target.cpp (g++ -std=c++17 -I. forever/tools/test_tb_self_target.cpp).
//
// The rule: `.tb role`, `.tb equip`, `.tb learn`, `.tb hurt` and `.tb power` act on a host bot, or on the character of the sender (the GM who typed the
// command), and on nothing else; another real player's character is refused. This is the same rule as `MayLeadBots` (TbH3.cpp), which the party, master
// and instance commands already use.
//
// The no-name forms: a first word that is already the command's own argument (a role, a power kind, a number) means no name was given, and the target
// is the sender. Names are never numeric on this server, so a number cannot be mistaken for one.
#pragma once

#include <cstdint>
#include <cstddef>
#include <string_view>

namespace TortoiseBots
{
namespace h3
{
namespace target
{
// The two 64-bit words of a guid (ObjectGuid::GetRawValue(0) and (1)): the pure half holds no core type.
struct Guid
{
    std::uint64_t low = 0;
    std::uint64_t high = 0;
    bool empty() const { return low == 0 && high == 0; }
    friend bool operator==(Guid a, Guid b) { return a.low == b.low && a.high == b.high; }
};

// A guid is an identity, not a bot: `hostBot` says what the character behind it is. The console has no character (sender empty), so it can only name bots.
inline bool MayTarget(Guid character, Guid sender, bool hostBot)
{
    if (character.empty())
        return false;
    return hostBot || (!sender.empty() && sender == character);
}

// `.tb role`'s roles (lower case).
inline bool IsRoleWord(std::string_view word)
{
    return word == "tank" || word == "healer" || word == "heal" || word == "dps" || word == "none" || word == "clear";
}

// `.tb power`'s kinds (lower case).
inline bool IsPowerWord(std::string_view word)
{
    return word == "mana" || word == "rage" || word == "energy";
}

// A positive id of at most 9 digits (the range ParseId takes in TbTestKit.cpp). "0" is no id.
inline bool IsIdWord(std::string_view word)
{
    if (word.empty() || word.size() > 9)
        return false;
    bool nonZero = false;
    for (char c : word)
    {
        if (c < '0' || c > '9')
            return false;
        nonZero = nonZero || c != '0';
    }
    return nonZero;   // "0" and "00" are no id
}

// A number a command carries itself (`.tb hurt 0` is a percent): at most 9 digits, zero allowed.
inline bool IsNumberWord(std::string_view word)
{
    if (word.empty() || word.size() > 9)
        return false;
    for (char c : word)
        if (c < '0' || c > '9')
            return false;
    return true;
}

// `.tb removeitem`: how many of an item to destroy out of the `inBags` the target carries in its bags (equipped pieces are never counted). `requested` 0
// means "all" (no count given); a count above what the bags hold removes what there is. The reply's left= is NOT computed from this: the handler re-reads the bags.
inline std::uint32_t RemoveAmount(std::uint32_t inBags, std::uint32_t requested)
{
    return (requested == 0 || requested > inBags) ? inBags : requested;
}

// The id / count words of the test-kit commands (`.tb additem`, `.tb equip`, `.tb removeitem`...): DIGITS ONLY, 1 to 999999999. No sign ("-1" and "+3" are
// refused, they used to go through strtoul and wrap to 4294967295), no space, no suffix ("3x"), no zero, no overflow (at most 9 digits fit a uint32).
inline bool ParseIdWord(std::string_view word, std::uint32_t& out)
{
    if (!IsIdWord(word))
        return false;
    std::uint32_t value = 0;
    for (char c : word)
        value = value * 10 + std::uint32_t(c - '0');
    out = value;
    return true;
}

// Where the "bags" of `.tb removeitem` are, as the core numbers them (Player.h; TbTestKit.cpp static_asserts every value against the core's enums).
constexpr std::uint8_t kBagZero = 255;            // INVENTORY_SLOT_BAG_0: the player's own slots (equipment, worn bags, backpack, bank bag list, keyring...)
constexpr std::uint8_t kBagStart = 30;            // INVENTORY_SLOT_BAG_START: the four worn bags
constexpr std::uint8_t kReagentBagEnd = 35;       // REAGENT_BAG_SLOT_END: the reagent bag is slot 34
constexpr std::uint8_t kBackpackStart = 35;       // INVENTORY_SLOT_ITEM_START

// Is an item with this position one of the "bags" stacks `.tb removeitem` may destroy? YES: a stack in the backpack (the player's own slots from
// kBackpackStart for `backpackSlots` slots), or in the contents of a worn bag or the reagent bag (the item's bag slot 30..34). NO: anything equipped, the
// worn bags themselves, the bank (and its bags), the keyring and child-equipment slots, anything else, and any stack that is in an open trade (the core's
// entry-based destroy skips those and would carry on into the equipment and the bank).
inline bool IsBagStack(std::uint8_t bagSlot, std::uint8_t slot, std::uint8_t backpackSlots, bool inTrade)
{
    if (inTrade)
        return false;
    if (bagSlot == kBagZero)
        return slot >= kBackpackStart && unsigned(slot) < unsigned(kBackpackStart) + backpackSlots;
    return bagSlot >= kBagStart && bagSlot < kReagentBagEnd;
}
}
}
}
