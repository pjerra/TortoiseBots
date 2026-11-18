// ForeverCore side of the both-core helpers and names of slice 2, package V2 (loot values, corpses, dispel target, the loot object stack).
// Owned by package V2: no other package edits this file. Turtle side: ai/cmangos-compat-s2v.h (same names and signatures, each
// forwarding to the call the bot always made).
//
// Lead's decision D2: the bot loots MONEY from the corpses of creatures its group killed. Corpse lootability (who may open it, the tap,
// the group's loot rules), the distance and the money itself are done properly here. What a corpse holds besides money, game object
// loot, gathering and skinning go to three named unported features (UnportedFeatures.h: "loot: items", "game object loot, gathering and
// skinning", "loot tables (drop maps)"). Rule for those: the failure value makes the bot do LESS (no item, no object, no drop known).
//
// Core facts used below (F = pinned ForeverCore, src/server/game; T = Turtle core):
//  * A corpse's loot is a Loot object. In the open world every tapper has a personal one (Creature::m_personalLoot, Unit.cpp:11965-11983);
//    a grouped kill in a dungeon is one shared Loot (Creature::m_loot, Unit.cpp:11896-11922). Creature::GetLootForPlayer (Creature.cpp:1438)
//    returns the one a player may see, nullptr when this player has none. T keeps one public Loot per creature (Creature::loot).
//  * Player::isAllowedToLoot (Player.cpp:20246) is the core's own entitlement test, the one that masks the lootable dynamic flag per viewer
//    (ViewerDependentValues.h:76) and that CMSG_LOOT applies (LootHandler.cpp:61, :214).
//  * The loot handlers run on the world thread (PROCESS_THREADUNSAFE, Opcodes.cpp:689-693) and need no map context. Everything in this file
//    is a READ: it takes no map lock, writes nothing, and the engine ticks on the world thread outside the map update (World.cpp:2462-2475,
//    findings item 2 still holds: nothing here proves a map thread cannot touch the same corpse while a tick runs).
#pragma once

#include <algorithm>
#include <sstream>
#include <vector>

// ---------------------------------------------------------------------------------------------------------------------
// Guid as a qualifier. A value is asked by a string ("should loot object <qualifier>"). T writes the 64-bit guid as a decimal number
// and reads it back with stoull; F's guid is 128 bits, so the number would be a hash that cannot be turned back into a guid. The text
// is the lossless "high/low" form the GuidPosition string already uses (GuidWriteCompat / GuidReadCompat, TbCompat_C.h). Every caller
// that builds the qualifier of "should loot object" must use GuidQualifierCompat, and the reader GuidFromQualifierCompat.
// DIFFERENT ANSWER: the qualifier text differs by core. A malformed text reads as the empty guid on F (T's stoull throws).
// ---------------------------------------------------------------------------------------------------------------------
inline std::string GuidQualifierCompat(ObjectGuid const& guid)
{
    std::ostringstream out;
    GuidWriteCompat(out, guid);
    return out.str();
}

inline ObjectGuid GuidFromQualifierCompat(std::string const& text)
{
    std::istringstream in(text);
    return GuidReadCompat(in);
}

// ---------------------------------------------------------------------------------------------------------------------
// Whose corpse is it. T ObjectGuid / Loot calls under the same names as the lines they replace in LootObjectStack.cpp (MayLootCorpse).
// ---------------------------------------------------------------------------------------------------------------------
// The core's own test: may this player open this corpse's loot. T `Player::IsAllowedToLoot(creature)`, F `Player::isAllowedToLoot`
// (Player.h:2962, body Player.cpp:20246): dead, no pending bind, a Loot for this player that is not looted, the player in its allowed
// looters, something in it for all or for the player (gold counts: Loot::hasItemForAll, Loot.cpp:1186), then the loot method's turn rule.
inline bool CorpseAllowedToLootCompat(Player* bot, Creature* creature) { return bot->isAllowedToLoot(creature); }

// The loot of a corpse as this bot sees it (nullptr: this bot has none). T `&creature->loot` (always present), F GetLootForPlayer
// (Creature.h:318-319 / Creature.cpp:1438). This is the same answer the core's CMSG_LOOT gives (LootHandler.cpp:253).
inline Loot* CreatureLootForCompat(Creature const* creature, Player const* bot) { return creature->GetLootForPlayer(bot); }

// The player whose turn the corpse is (round robin owner); empty when released or when there is none. T `Loot::roundRobinPlayer` (a raw
// 64-bit guid), F Loot::roundRobinPlayer (Loot.h:299, an ObjectGuid, set to the loot owner at generation, Loot.cpp:959).
inline ObjectGuid CorpseRoundRobinGuidCompat(Creature const* creature, Player const* bot)
{
    Loot const* loot = creature->GetLootForPlayer(bot);
    return loot ? loot->roundRobinPlayer : ObjectGuid::Empty;
}

// The group's assigned master looter. T `Group::GetLooterGuid()` is the master looter under MASTER_LOOT (Group.h:223); F keeps the master
// looter apart (Group.h:316 GetMasterLooterGuid; GetLooterGuid there is the round robin pointer, empty under free-for-all, Group.cpp:1675).
inline ObjectGuid GroupMasterLooterGuidCompat(Group const* group) { return group->GetMasterLooterGuid(); }

// "Another member's turn: does the corpse show a personal item (quest, free-for-all, conditional) to this bot". T asks
// ai::HasVisiblePersonalItemFor(creature->loot, bot) (policy/loot/CorpseLootPolicy.h, passed in as `visible`). F: money only (D2), so
// no personal item is ever claimed: false, which keeps the bot away from another member's corpse (LESS). Counted when the corpse holds
// items the bot did not weigh.
template <class VisibleFn>
inline bool CorpseHasVisiblePersonalItemForCompat(Creature* creature, Player* bot, VisibleFn&& /*visible*/)
{
    Loot const* loot = creature->GetLootForPlayer(bot);
    if (loot && loot->unlootedCount)
        tbcompat::Unported(tbcompat::Feature::LootItems);
    return false;
}

// The five facts the corpse-refusal text (policy/loot/CorpseRefusalReasonPolicy.h, a debug line) is built from. T builds them from the
// Loot of the creature (the allowed-looter test, a quest item list, hasItemFor, another member's turn, a reserved personal item).
// F: allowed looter = Loot::HasAllowedLooter (Loot.h:333), items = whether the player has a personal item (Loot::hasItemFor, Loot.h:349),
// turn = a round robin owner other than the bot. Debug text only; no decision reads it.
struct CorpseRefusalFactsCompat
{
    bool allowedLooter = false;
    bool hasQuestItems = false;
    bool hasItemFor = false;
    bool otherTurn = false;
    bool reserved = false;
};

template <class VisibleFn>
inline CorpseRefusalFactsCompat CorpseRefusalFactsOfCompat(Creature const* creature, Player* bot, VisibleFn&& /*visible*/)
{
    CorpseRefusalFactsCompat facts;
    Loot const* loot = creature->GetLootForPlayer(bot);
    if (!loot)
        return facts;
    facts.allowedLooter = loot->HasAllowedLooter(bot->GetGUID());
    facts.otherTurn = !loot->roundRobinPlayer.IsEmpty() && loot->roundRobinPlayer != bot->GetGUID();
    facts.hasItemFor = loot->hasItemFor(bot);
    return facts;
}

// ---------------------------------------------------------------------------------------------------------------------
// Corpse distance. T `creature->IsWithinDistInMap(bot, bot->GetMaxLootDistance(creature), true, SizeFactor::None)` (Player.cpp:17288:
// max(INTERACTION_DISTANCE, max(1.5, own reach) + 4/3 + max(1.5, the corpse's reach)), centre to centre, 3D, same map). F has no such
// function; its server test is `IsWithinDist(creature, 30 yd)` with both combat reaches added (AELootCreatureCheck, LootHandler.cpp:41,
// :50, :214). The bot keeps T's approach distance (the formula, over F's INTERACTION_DISTANCE 5.0, ObjectDefines.h:24, and F's combat
// reaches): it is always inside what F's server accepts (30 yd plus reaches), so a corpse the bot calls "in range" never gets a "too
// far" refusal, and the bot walks up to the corpse as on T instead of opening it from 30 yd.
// DIFFERENT ANSWER: only through the two combat reaches, which differ by core.
// ---------------------------------------------------------------------------------------------------------------------
inline bool CorpseInLootRangeCompat(Player* bot, Creature* creature)
{
    float const reach = std::max(1.5f, bot->GetCombatReach()) + 1.333333373069763f + std::max(1.5f, creature->GetCombatReach());
    float const maxDistance = std::max(float(INTERACTION_DISTANCE), reach);
    return creature->IsWithinDistInMap(bot, maxDistance, true, false, false);
}

// ---------------------------------------------------------------------------------------------------------------------
// Skinning a looted corpse and game object loot: not in this slice (D2). Failure value: false ("not skinnable by this bot", "may not
// be looted"). Counted (feature "game object loot, gathering and skinning") only when something was actually refused: a corpse that
// carries the skinnable flag, a game object the bot was asked to consider.
// ---------------------------------------------------------------------------------------------------------------------
// T: the corpse is skinnable by the bot: flagged, its body loot gone, and the core's own IsSkinnableBy (Creature.h). F: false.
inline bool CorpseSkinnableByCompat(Player* /*bot*/, Creature* creature)
{
    if (creature->HasUnitFlag(UNIT_FLAG_SKINNABLE))
        tbcompat::Unported(tbcompat::Feature::GatheringLoot);
    return false;
}

// T: true (a spawned game object in the ready state may be looted: the legacy code checks locks and skills next). F: false, so no game
// object ever becomes a loot target; the object stays where it is.
inline bool GameObjectLootAllowedCompat(GameObject const* /*object*/)
{
    tbcompat::Unported(tbcompat::Feature::GatheringLoot);
    return false;
}

// ---------------------------------------------------------------------------------------------------------------------
// The loot an object carries, for "should loot object". T `&creature->loot` / `&gameobject->loot`, nullptr for anything else. F: a
// creature's loot FOR THIS BOT (nullptr when it has none); a game object's loot is not read (GatheringLoot), nullptr.
// ---------------------------------------------------------------------------------------------------------------------
inline Loot* LootOfObjectCompat(WorldObject* object, Player* bot)
{
    if (Creature* creature = object->ToCreature())
        return creature->GetLootForPlayer(bot);
    return nullptr;
}

// What "no loot object" means for the caller. T: nothing (a creature always has its Loot; a game object without one falls to the goober
// test). F: true, "nothing to take": for a creature that is "this bot has no loot here" (T's `return true` for a non-game-object would
// send the bot to a corpse the core refuses); for a game object, game object loot is not ported (LESS), counted.
inline bool AbsentLootMeansNothingToTakeCompat(WorldObject* object)
{
    if (object->IsGameObject())
        tbcompat::Unported(tbcompat::Feature::GatheringLoot);
    return true;
}

// The loot a guid names, for a player. T sLootMgr.GetLoot(player, guid): the loot of the object the guid names (an empty guid means the
// loot window the player has open). F: a creature's loot for this player, or the open window's creature when the guid is empty; every
// other kind of object (game object, item, corpse of a player) is not read (nullptr). Needs the player in the world and its map.
inline Loot* LootOfGuidCompat(Player* player, ObjectGuid guid)
{
    if (!player || !player->IsInWorld())
        return nullptr;
    if (guid.IsEmpty())
        guid = player->GetLootGUID();
    if (guid.IsEmpty() || !guid.IsCreatureOrVehicle())
        return nullptr;
    Creature* creature = player->GetMap()->GetCreature(guid);
    return creature ? creature->GetLootForPlayer(player) : nullptr;
}

// ---------------------------------------------------------------------------------------------------------------------
// What a Loot holds, as LootAccess (LootValues.h) reads it.
// ---------------------------------------------------------------------------------------------------------------------
// The status bits T's shim defines (ai/cmangos-compat-shim.h:987): same names, same numbers.
enum LootStatusFlags : uint32
{
    LOOT_STATUS_FAKE_LOOT              = 0x01,
    LOOT_STATUS_CONTAIN_GOLD           = 0x02,
    LOOT_STATUS_NOT_FULLY_LOOTED       = 0x04,
    LOOT_STATUS_CONTAIN_FFA            = 0x08,
    LOOT_STATUS_CONTAIN_RELEASED_ITEMS = 0x10
};

// The items of a loot this player could take (T: the unlooted shared items plus the player's quest / free-for-all / conditional copies;
// `visible` is ai::PersonalCopyVisible). F: none (feature "loot: items", D2: money only), counted when the loot still holds items.
template <class VisibleFn>
inline std::vector<LootItem*> LootContentForCompat(Loot const* loot, Player* /*player*/, VisibleFn&& /*visible*/)
{
    if (loot && loot->unlootedCount)
        tbcompat::Unported(tbcompat::Feature::LootItems);
    return {};
}

// The status bits of a loot for a player. T: gold, items left, free-for-all items. F: the gold bit only (the money is real and known);
// the item bits are not set (feature "loot: items"), so a corpse with items only reads as "looted" for this bot, which is what D2 asks
// ("not looted": the bot does not take items). `content` is T's item walk and is not called here.
template <class ContentFn>
inline uint32 LootStatusForCompat(Loot const* loot, Player const* player, ContentFn&& /*content*/)
{
    if (!loot || !player)
        return 0;
    uint32 status = 0;
    if (loot->gold != 0)
        status |= LOOT_STATUS_CONTAIN_GOLD;
    if (loot->unlootedCount)
        tbcompat::Unported(tbcompat::Feature::LootItems);
    return status;
}

// ---------------------------------------------------------------------------------------------------------------------
// Loot templates (the drop maps). F keeps the rows of a LootTemplate private (LootMgr.h:150-151), so no drop can be read: feature "loot
// tables (drop maps)", failure value nullptr / no row. The reader code (DropMapValue, ItemDropMapValue, LootChanceValue, the chest
// check in LootObjectStack.cpp) is compiled with the row type below and never runs on F: every template lookup answers nullptr.
// The row type has the names of T's LootStoreItem fields the readers use (itemid, chance, mincountOrRef); it is never filled.
// ---------------------------------------------------------------------------------------------------------------------
struct LootStoreItemCompat
{
    uint32 itemid = 0;
    float chance = 0.0f;
    int32 mincountOrRef = 0;
};
using LootStoreItemListCompat = std::vector<LootStoreItemCompat>;

// The template of a loot table for an object a guid names, by loot type. Failure value: nullptr. T walks the creature / game object / item
// templates and casts the core's template to the module's layout class (`Access`).
template <class Access>
inline Access const* LootTemplateOfGuidCompat(ObjectGuid const& /*guid*/, LootType /*type*/)
{
    tbcompat::Unported(tbcompat::Feature::LootTables);
    return nullptr;
}

// The game object chest's template (T LootTemplates_Gameobject.GetLootFor(lootId)) and a reference template
// (LootTemplates_Reference.GetLootFor(id)). Failure value: nullptr.
template <class Access, class Info>
inline Access const* GameObjectLootTemplateCompat(Info const* /*info*/)
{
    tbcompat::Unported(tbcompat::Feature::LootTables);
    return nullptr;
}

template <class Access>
inline Access const* ReferenceLootTemplateCompat(uint32 /*referenceId*/)
{
    tbcompat::Unported(tbcompat::Feature::LootTables);
    return nullptr;
}

// ---------------------------------------------------------------------------------------------------------------------
// Quest relation tables (QuestValues.h, QuestObjectMgr). T's ObjectMgr hands out its four relation maps by reference
// (ObjectMgr.h:1612-1615, QuestRelationsMap = multimap<uint32, uint32>, :328). F's two "involved" maps have no accessor at all (ObjectMgr.h:
// 1817, :1820 are private; only the first two have a HACK getter) and its per-entry getters return a result object, so the whole table is
// not read: feature "quest internals", every map reads as empty (no giver, no turn-in known: the bot starts and hands in nothing).
// ---------------------------------------------------------------------------------------------------------------------
using QuestRelationsMap = QuestRelations;
inline QuestRelationsMap& QuestRelationsMapCompat()
{
    tbcompat::Unported(tbcompat::Feature::QuestInternals);
    static QuestRelationsMap none;
    return none;
}
#define m_CreatureQuestRelations QuestRelationsMapCompat()
#define m_CreatureQuestInvolvedRelations QuestRelationsMapCompat()
#define m_GOQuestRelations QuestRelationsMapCompat()
#define m_GOQuestInvolvedRelations QuestRelationsMapCompat()

// The dialog status numbers the bot's own quest value computes and compares (T QuestDef.h:119-131; F reports a QuestGiverStatus bit mask
// instead, QuestDef.h:154, which the bot's value never reads: QuestValues.cpp builds the numbers itself from the quest log).
enum __QuestGiverStatus
{
    DIALOG_STATUS_NONE        = 0,
    DIALOG_STATUS_UNAVAILABLE = 1,
    DIALOG_STATUS_CHAT        = 2,
    DIALOG_STATUS_INCOMPLETE  = 3,
    DIALOG_STATUS_REWARD_REP  = 4,
    DIALOG_STATUS_AVAILABLE   = 5,
    DIALOG_STATUS_REWARD_OLD  = 6,
    DIALOG_STATUS_REWARD2     = 7,
    DIALOG_STATUS_UNDEFINED   = 100
};
