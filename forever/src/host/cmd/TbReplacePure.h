// The PURE half of `.tb equip <character> <item id> replace`: which replaced piece is thrown away instead of put in the bags. No core header and no
// legacy type; tested standalone by forever/tools/test_equip_replace.cpp (g++ -std=c++17 -I forever/src forever/tools/test_equip_replace.cpp).
//
// The rule: a piece that comes off the body is destroyed only when the piece going on is strictly better in item level AND the old one is starter
// gear (poor or common quality: the grey and white pieces a character starts with). Anything better keeps the old behaviour and goes to the bags.
//
// Who may lose a piece: a host bot, or the sender's own character when the sender is a GM. `.tb equip` refuses another real player's character before
// this is asked (TbTestTargetPure.h), so the sender check only keeps a non-GM sender's own character's gear untouched.
#pragma once

namespace TortoiseBots
{
namespace h3
{
namespace replace
{
// ItemQuality: poor 0, common 1 (the starter gear); uncommon and above is worth keeping.
inline constexpr unsigned kCommonQuality = 1;

inline bool StarterPiece(unsigned quality)
{
    return quality <= kCommonQuality;
}

// `oldItemLevel` < `newItemLevel`: the incoming piece is strictly better.
inline bool IsStarterUpgrade(unsigned oldQuality, unsigned oldItemLevel, unsigned newItemLevel)
{
    return StarterPiece(oldQuality) && oldItemLevel < newItemLevel;
}

// The target may lose the replaced piece: it is a host bot, or the sender's own character and the sender is a GM.
inline bool MayDestroyReplacedItem(bool targetIsHostBot, bool senderIsGm, bool senderIsTarget)
{
    return targetIsHostBot || (senderIsGm && senderIsTarget);
}
}
}
}
