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
    if (word.empty() || word.size() > 9 || word == "0")
        return false;
    for (char c : word)
        if (c < '0' || c > '9')
            return false;
    return true;
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
// means "all" (no count given); a count above what the bags hold removes what there is. The reply's left= is inBags - this.
inline std::uint32_t RemoveAmount(std::uint32_t inBags, std::uint32_t requested)
{
    return (requested == 0 || requested > inBags) ? inBags : requested;
}
}
}
}
