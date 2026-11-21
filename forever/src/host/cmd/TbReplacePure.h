// The PURE half of `.tb equip <character> <item id> replace`: which replaced piece is thrown away instead of put in the bags. No core header and no
// legacy type; tested standalone by forever/tools/test_equip_replace.cpp (g++ -std=c++17 -I forever/src forever/tools/test_equip_replace.cpp).
//
// The rule: a piece that comes off the body is destroyed only when the equip it made way for is CONFIRMED (the new piece is in the slot) AND
//   (a) the piece is one of the items the core creates that character with (its race and class; the start-outfit data the module reads through
//       StartOutfitItem, forever/src/host/cmd/TbStartOutfit.h), or
//   (b) the piece is poor (grey) quality.
// Everything else goes to the bags as before. Two pieces are never destroyed whatever their provenance or quality: a quest item and a bag that
// still holds items. A piece is a quest item when its template is in the quest class, bonds as a quest item, starts a quest, or when an item
// objective of a quest still in the character's log names its entry (IsQuestItem; the handler reads the template and the quest log through the
// core). Quality alone is not enough: a common piece the character did not start with is loot, not starter gear.
//
// Who may lose a piece: a host bot, or the sender's own character when the sender is a GM. `.tb equip` refuses another real player's character before
// this is asked (TbTestTargetPure.h), so the sender check only keeps a non-GM sender's own character's gear untouched.
#pragma once

#include <cstdint>
#include <vector>

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

// QUEST_OBJECTIVE_ITEM (QuestDef.h, the core's QuestObjectiveType): the type whose ObjectID is a required item entry.
inline constexpr int kItemObjective = 1;

// One objective of a quest the character still has in its log: the core's objective type and the id it names (QuestObjective::Type, :ObjectID).
struct ActiveObjective
{
    int type = 0;
    std::int32_t objectId = 0;
};

// An item objective of one of the character's active quests names this entry.
inline bool RequiredByActiveQuest(std::uint32_t entry, std::vector<ActiveObjective> const& objectives)
{
    for (ActiveObjective const& objective : objectives)
        if (objective.type == kItemObjective && objective.objectId > 0 && std::uint32_t(objective.objectId) == entry)
            return true;
    return false;
}

// The piece is a quest item: never destroyed. The facts are the template's class, bonding and start quest, and the item objectives of every quest
// still in the character's log (required items of quests it currently has).
inline bool IsQuestItem(std::uint32_t entry, bool questClass, bool questBonding, bool startsQuest,
                        std::vector<ActiveObjective> const& activeObjectives)
{
    return questClass || questBonding || startsQuest || RequiredByActiveQuest(entry, activeObjectives);
}

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

// What becomes of a displaced piece AFTER the swap. The handler remembers only the piece's GUID before the swap (a raw Item* can be freed by it:
// SwapItem stores the old piece with StoreItem, which merges a stackable piece into an equal stack in the bags and frees the moved Item) and
// re-resolves it by that GUID afterwards. `found`: the GUID still names an item of the character; `inBags`: it is in the inventory (not the slot just
// equipped); `isWorn`: it is the piece just equipped; `count`: its stack size.
enum class Fate : unsigned char
{
    Gone,        // merged away or freed by the swap: nothing to destroy, nothing to touch
    Keep,        // stays where the swap put it (not disposable, or not in the bags)
    KeepStack,   // disposable, but a stack of more than one: destroying it would throw away more than the one piece replaced; it stays in the bags
    Destroy      // exactly one disposable item in the bags
};

inline Fate DisplacedFate(bool found, bool inBags, bool isWorn, std::uint32_t count, Disposable why)
{
    if (!found)
        return Fate::Gone;
    if (isWorn || !inBags || why == Disposable::No)
        return Fate::Keep;
    return count == 1 ? Fate::Destroy : Fate::KeepStack;
}

// The target may lose the replaced piece: it is a host bot, or the sender's own character and the sender is a GM.
inline bool MayDestroyReplacedItem(bool targetIsHostBot, bool senderIsGm, bool senderIsTarget)
{
    return targetIsHostBot || (senderIsGm && senderIsTarget);
}
}
}
}
