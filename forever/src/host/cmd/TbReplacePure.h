// The PURE half of `.tb equip <character> <item id> replace`: which replaced piece is thrown away instead of put in the bags. No core header and no
// legacy type; tested standalone by forever/tools/test_equip_replace.cpp (g++ -std=c++17 -I forever/src forever/tools/test_equip_replace.cpp).
//
// The rule: a piece that comes off the body is destroyed only when the equip it made way for is CONFIRMED (the new piece is in the slot) AND
//   (a) the piece is one of the items the core creates that character with (its race and class; the start-outfit data the module reads through
//       StartOutfitItem, forever/src/host/cmd/TbStartOutfit.h), or
//   (b) the piece is poor (grey) quality.
// Everything else goes to the bags as before. Two pieces are never destroyed whatever their provenance or quality: a quest item (it starts a quest)
// and a bag that still holds items. Quality alone is not enough: a common piece the character did not start with is loot, not starter gear.
//
// Who may lose a piece: a host bot, or the sender's own character when the sender is a GM. `.tb equip` refuses another real player's character before
// this is asked (TbTestTargetPure.h), so the sender check only keeps a non-GM sender's own character's gear untouched.
#pragma once

#include <cstdint>

namespace TortoiseBots
{
namespace h3
{
namespace replace
{
// Why a displaced piece is destroyed; the log line and the command reply name it. No: it goes to the bags.
enum class Disposable : unsigned char
{
    No,
    StartOutfit,
    Poor
};

// ItemQuality poor (0). Common (1) is NOT disposable on its own.
inline constexpr unsigned kPoorQuality = 0;

// The decision, from the facts the handler can check. `equipConfirmed`: the incoming piece is in the slot the swap aimed at.
inline Disposable DisposableReason(bool equipConfirmed, bool startOutfit, unsigned quality, bool questItem, bool nonEmptyBag)
{
    if (!equipConfirmed || questItem || nonEmptyBag)
        return Disposable::No;
    if (startOutfit)
        return Disposable::StartOutfit;
    if (quality <= kPoorQuality)
        return Disposable::Poor;
    return Disposable::No;
}

// The words the log line and the reply carry for a destroy ("start-outfit", "poor"); "" for No.
inline char const* ReasonName(Disposable reason)
{
    switch (reason)
    {
        case Disposable::StartOutfit: return "start-outfit";
        case Disposable::Poor: return "poor";
        default: return "";
    }
}

// The target may lose the replaced piece: it is a host bot, or the sender's own character and the sender is a GM.
inline bool MayDestroyReplacedItem(bool targetIsHostBot, bool senderIsGm, bool senderIsTarget)
{
    return targetIsHostBot || (senderIsGm && senderIsTarget);
}
}
}
}
