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
//  * A corpse's loot is a Loot object. In the open world every tapper has a personal one (Creature::m_personalLoot, Unit.cpp:11975-11993);
//    a grouped kill in a dungeon is one shared Loot (Creature::m_loot, Unit.cpp:11906-11932). Creature::GetLootForPlayer (Creature.cpp:1438)
//    returns the one a player may see, nullptr when this player has none. T keeps one public Loot per creature (Creature::loot).
//  * Player::isAllowedToLoot (Player.cpp:20256) is the core's own entitlement test, the one that masks the lootable dynamic flag per viewer
//    (ViewerDependentValues.h:76) and that CMSG_LOOT applies (LootHandler.cpp:61, :214).
//  * The loot handlers run on the world thread (PROCESS_THREADUNSAFE, Opcodes.cpp:689-693) and need no map context. Everything in this file
//    is a READ: it takes no map lock, writes nothing, and the engine ticks on the world thread outside the map update (World.cpp:2462-2475,
//    findings item 2 still holds: nothing here proves a map thread cannot touch the same corpse while a tick runs).
#pragma once

#include "TbCompat_S5.h"           // WorldQueryCompat (slice 5): the guarded world database read the loot table copy uses

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
// (Player.h:2962, body Player.cpp:20256): dead, no pending bind, a Loot for this player that is not looted, the player in its allowed
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
// ai::HasVisiblePersonalItemFor(creature->loot, bot) (policy/loot/CorpseLootPolicy.h, passed in as `visible`). F (slice 9): Loot::hasItemFor
// (Loot.cpp:1197: the player's quest items and free-for-all items), the very test Player::isAllowedToLoot ends in for a member whose turn it is not
// (Player.cpp:20284). The policy's own T algorithm is not run.
template <class VisibleFn>
inline bool CorpseHasVisiblePersonalItemForCompat(Creature* creature, Player* bot, VisibleFn&& /*visible*/)
{
    Loot const* loot = creature->GetLootForPlayer(bot);
    return loot && loot->hasItemFor(bot);
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
// Skinning a looted corpse and game object loot. Skinning and gathering (herb, ore) stay the named feature "loot: gathering and skinning" (failure
// value: false = "not skinnable by this bot", counted when something was refused). A CHEST is ported since slice 9: it may be looted when it is a chest
// in its ready state; the opening cast and the window are read like a corpse's (TbCompat_I2.h LootGatherUnportedCompat, TbCompat_S9.h poll).
// ---------------------------------------------------------------------------------------------------------------------
// T: the corpse is skinnable by the bot: flagged, its body loot gone, and the core's own IsSkinnableBy (Creature.h). F: false.
inline bool CorpseSkinnableByCompat(Player* /*bot*/, Creature* creature)
{
    if (creature->HasUnitFlag(UNIT_FLAG_SKINNABLE))
        tbcompat::Unported(tbcompat::Feature::GatheringLoot);
    return false;
}

// T: true (a spawned game object in the ready state may be looted: the legacy code checks locks and skills next). F: only a chest; a node (herb, ore,
// fishing hole) or any other object is not a loot target (counted).
inline bool GameObjectLootAllowedCompat(GameObject const* object)
{
    if (object && object->GetGoType() == GAMEOBJECT_TYPE_CHEST)
        return true;
    tbcompat::Unported(tbcompat::Feature::GatheringLoot);
    return false;
}

// ---------------------------------------------------------------------------------------------------------------------
// The loot an object carries, for "should loot object". T `&creature->loot` / `&gameobject->loot`, nullptr for anything else. F: the loot FOR THIS BOT
// (WorldObject::GetLootForPlayer, Object.h:167, virtual: a creature's, a game object's); nullptr when it has none (a chest not opened yet).
// ---------------------------------------------------------------------------------------------------------------------
inline Loot* LootOfObjectCompat(WorldObject* object, Player* bot)
{
    if (object->IsCreature() || object->IsGameObject())
        return object->GetLootForPlayer(bot);
    return nullptr;
}

// What "no loot object" means for the caller. T: nothing (a creature always has its Loot; a game object without one falls to the goober test). F: true,
// "nothing to take": for a creature that is "this bot has no loot here"; for a game object it is a chest or other object that was not opened yet (no loot
// is generated before the first use, GameObject.cpp:2749), and the caller's goober test is not ported: LESS. A chest is looted through the loot object
// stack (the opening cast), not through this value.
inline bool AbsentLootMeansNothingToTakeCompat(WorldObject* object)
{
    if (object->IsGameObject())
        tbcompat::Unported(tbcompat::Feature::GatheringLoot);
    return true;
}

// The loot a guid names, for a player. T sLootMgr.GetLoot(player, guid): the loot of the object the guid names (an empty guid means the loot window the
// player has open). F: a creature's or a game object's loot for this player (WorldObject::GetLootForPlayer), or the open window's object when the guid is
// empty; an item or the corpse of a player is not read (nullptr). Needs the player in the world and its map.
inline Loot* LootOfGuidCompat(Player* player, ObjectGuid guid)
{
    if (!player || !player->IsInWorld())
        return nullptr;
    if (guid.IsEmpty())
        guid = player->GetLootGUID();
    if (guid.IsEmpty())
        return nullptr;
    if (guid.IsCreatureOrVehicle())
    {
        Creature* creature = player->GetMap()->GetCreature(guid);
        return creature ? creature->GetLootForPlayer(player) : nullptr;
    }
    if (guid.IsGameObject())
    {
        GameObject* object = player->GetMap()->GetGameObject(guid);
        return object ? object->GetLootForPlayer(player) : nullptr;
    }
    return nullptr;
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

// The items of a loot this player could take now. T: the unlooted shared items plus the player's quest / free-for-all / conditional copies (`visible` is
// ai::PersonalCopyVisible). F (slice 9): the items the core's loot window shows this player as takeable (LootItem::GetUiTypeForPlayer, Loot.cpp:212:
// ALLOW_LOOT or OWNER). It already folds in looted flags, the allowed-looter list, the personal quest / free-for-all copies (Loot::PlayerFFAItems), the
// round-robin turn of an item under the threshold and a roll winner's right. An item under a roll, locked or for the master looter is not takeable.
template <class VisibleFn>
inline std::vector<LootItem*> LootContentForCompat(Loot const* loot, Player* player, VisibleFn&& /*visible*/)
{
    std::vector<LootItem*> content;
    if (!loot || !player)
        return content;
    for (LootItem const& item : loot->items)
    {
        if (item.type != LootItemType::Item)
            continue;
        Optional<LootSlotType> const uiType = item.GetUiTypeForPlayer(player, *loot);
        if (uiType && (*uiType == LOOT_SLOT_TYPE_ALLOW_LOOT || *uiType == LOOT_SLOT_TYPE_OWNER))
            content.push_back(const_cast<LootItem*>(&item));
    }
    return content;
}

// The status bits of a loot for a player. T: gold, items left, free-for-all items. F: the gold bit, NOT_FULLY_LOOTED for an item the bot could take now
// or one that is still under a roll ("an active roll is unfinished loot for everyone", as on T), FFA for a free-for-all copy. `content` is
// LootAccess::GetLootContentFor. RELEASED_ITEMS (T's "the round robin owner released the turn") is not set: the core folds the turn into the UI type.
template <class ContentFn>
inline uint32 LootStatusForCompat(Loot const* loot, Player const* player, ContentFn&& content)
{
    if (!loot || !player)
        return 0;
    uint32 status = 0;
    if (loot->gold != 0)
        status |= LOOT_STATUS_CONTAIN_GOLD;
    for (LootItem* item : content(const_cast<Player*>(player)))
    {
        status |= LOOT_STATUS_NOT_FULLY_LOOTED;
        if (item->freeforall)
            status |= LOOT_STATUS_CONTAIN_FFA;
    }
    for (LootItem const& item : loot->items)
    {
        if (item.type != LootItemType::Item)
            continue;
        Optional<LootSlotType> const uiType = item.GetUiTypeForPlayer(player, *loot);
        if (uiType && *uiType == LOOT_SLOT_TYPE_ROLL_ONGOING)
            status |= LOOT_STATUS_NOT_FULLY_LOOTED;
    }
    return status;
}

// ---------------------------------------------------------------------------------------------------------------------
// Loot templates (the drop maps). F keeps the rows of a LootTemplate private (LootMgr.h:150-151), so since slice 9 (package L9) the module COPIES the
// rows it needs from the world database, once per table, the first time a template of that table is asked for (a boot-time call of
// LootTablePreloadCompat() does the same earlier; the readers that ask are the chest check of the loot object stack and, where the shared context is
// built, the drop maps). The reader is `WorldQueryCompat` (slice 5, TbCompat_S5.h): ForeverCore's connection ABORTS THE SERVER on a query naming a missing table or
// column (MySQLConnection.cpp:596-601), so it asks information_schema first; a table that is not as expected reads as EMPTY (one warning line, the templates then answer nullptr).
// The row type has the names of T's LootStoreItem fields the readers use (itemid, chance, mincountOrRef; needs_quest for the boost block); the layout
// classes the readers fill are ai::LootTemplateAccess / LootLootGroupAccess (LootValues.h): rows with a group id go into Groups[group - 1], ExplicitlyChanced
// when they carry a chance and EqualChanced when the chance is 0, exactly as the core's own LootTemplate::AddEntry sorts them (LootMgr.cpp).
// What the core folds in at loot generation and the copy does not know: loot modes, conditions, the per-player quest-required test. Currency and
// tracking-quest rows are skipped (a Classic table holds none).
// ---------------------------------------------------------------------------------------------------------------------
struct LootStoreItemCompat
{
    uint32 itemid = 0;
    float chance = 0.0f;
    int32 mincountOrRef = 0;
    bool needs_quest = false;
};
using LootStoreItemListCompat = std::vector<LootStoreItemCompat>;

namespace ai
{
namespace slice
{
void LogWarn(std::string const& message);
}
}

namespace tbs9
{
struct LootTableRow
{
    uint32 entry = 0;
    LootStoreItemCompat item;
    uint8 group = 0;
};
struct LootTableCopy
{
    bool loaded = false;
    std::unordered_map<uint32, std::vector<LootTableRow>> byEntry;
};

// The copy of one table, read once (a table that is not readable stays empty and is not asked again).
inline LootTableCopy const& LootTableOf(std::string const& table)
{
    static std::map<std::string, LootTableCopy> copies;
    LootTableCopy& copy = copies[table];
    if (copy.loaded)
        return copy;
    copy.loaded = true;

    // The guarded world read of slice 5 (TbCompat_S5.h): information_schema first, a missing table or column reads as no result, never an abort.
    std::string const sql = "SELECT `Entry`, `ItemType`, `Item`, `Chance`, `QuestRequired`, `GroupId`, `MinCount` FROM `" + table + "`";
    bool schemaMissing = false;
    QueryResult result = WorldQueryCompat(sql.c_str(), &schemaMissing);
    if (schemaMissing)
    {
        ai::slice::LogWarn("TortoiseBots: loot table `" + table + "` is missing or lacks a column: not copied (its templates answer nothing)");
        return copy;
    }
    if (!result)
        return copy;
    uint64 rows = 0;
    do
    {
        Field* fields = result->Fetch();
        int32 const itemType = fields[1].GetInt8();
        if (itemType != 0 && itemType != 1)
            continue;                                       // currency and tracking quests are not read
        LootTableRow row;
        row.entry = fields[0].GetUInt32();
        row.item.itemid = fields[2].GetUInt32();
        row.item.chance = fields[3].GetFloat();
        row.item.needs_quest = fields[4].GetBool();
        row.group = fields[5].GetUInt8();
        row.item.mincountOrRef = itemType == 1 ? -int32(fields[2].GetUInt32()) : int32(fields[6].GetUInt8());
        copy.byEntry[row.entry].push_back(row);
        ++rows;
    } while (result->NextRow());
    ai::slice::LogWarn("TortoiseBots: loot table `" + table + "` copied: " + std::to_string(rows) + " rows, " + std::to_string(copy.byEntry.size()) + " entries");
    return copy;
}

// The template of one entry of a table in the module's layout class; nullptr when the entry has no rows (T: no template).
template <class Access>
inline Access const* LootTemplateOf(std::string const& table, uint32 entry)
{
    static std::map<std::pair<std::string, uint32>, Access> built;
    auto found = built.find({ table, entry });
    if (found != built.end())
        return &found->second;

    LootTableCopy const& copy = LootTableOf(table);
    auto rows = copy.byEntry.find(entry);
    if (rows == copy.byEntry.end())
        return nullptr;

    Access access;
    for (LootTableRow const& row : rows->second)
    {
        if (!row.group)
        {
            access.Entries.push_back(row.item);
            continue;
        }
        if (access.Groups.size() < row.group)
            access.Groups.resize(row.group);
        typename decltype(access.Groups)::value_type& group = access.Groups[row.group - 1];
        if (row.item.chance != 0.0f)
            group.ExplicitlyChanced.push_back(row.item);
        else
            group.EqualChanced.push_back(row.item);
    }
    return &built.emplace(std::make_pair(table, entry), std::move(access)).first->second;
}
}

// Copies the four tables the readers use now (the call a boot-time hook may make; the readers copy on first use otherwise).
inline void LootTablePreloadCompat()
{
    tbs9::LootTableOf("gameobject_loot_template");
    tbs9::LootTableOf("reference_loot_template");
    tbs9::LootTableOf("creature_loot_template");
    tbs9::LootTableOf("item_loot_template");
}

// The template of a loot table for the object a guid names, by loot type. T walks the creature / game object / item templates by the object's template
// and casts the core's template to the module's layout class (`Access`). F: a corpse's table (the template's LootID of the normal difficulty,
// CreatureData.h), a game object's (GameObjectTemplate::GetLootId), an item's container table, copied as above. Skinning, pickpocketing, fishing and
// disenchanting are the named feature "loot: gathering and skinning": nullptr.
template <class Access>
inline Access const* LootTemplateOfGuidCompat(ObjectGuid const& guid, LootType type)
{
    if (guid.IsCreature())
    {
        if (type != LOOT_CORPSE)
        {
            tbcompat::Unported(tbcompat::Feature::GatheringLoot);
            return nullptr;
        }
        CreatureTemplate const* info = sObjectMgr.GetCreatureTemplate(guid.GetEntry());
        CreatureDifficulty const* difficulty = info ? info->GetDifficulty(DIFFICULTY_NONE) : nullptr;
        return difficulty && difficulty->LootID ? tbs9::LootTemplateOf<Access>("creature_loot_template", difficulty->LootID) : nullptr;
    }
    if (guid.IsGameObject())
    {
        if (type != LOOT_CORPSE && type != LOOT_CHEST)
        {
            tbcompat::Unported(tbcompat::Feature::GatheringLoot);
            return nullptr;
        }
        GameObjectTemplate const* info = sObjectMgr.GetGameObjectTemplate(guid.GetEntry());
        return info && info->GetLootId() ? tbs9::LootTemplateOf<Access>("gameobject_loot_template", info->GetLootId()) : nullptr;
    }
    if (guid.IsItem())
    {
        if (type != LOOT_CORPSE)
        {
            tbcompat::Unported(tbcompat::Feature::GatheringLoot);
            return nullptr;
        }
        return tbs9::LootTemplateOf<Access>("item_loot_template", guid.GetEntry());
    }
    return nullptr;
}

// The game object chest's template (T LootTemplates_Gameobject.GetLootFor(lootId)) and a reference template (LootTemplates_Reference.GetLootFor(id)).
template <class Access, class Info>
inline Access const* GameObjectLootTemplateCompat(Info const* info)
{
    return info && info->GetLootId() ? tbs9::LootTemplateOf<Access>("gameobject_loot_template", info->GetLootId()) : nullptr;
}

template <class Access>
inline Access const* ReferenceLootTemplateCompat(uint32 referenceId)
{
    return tbs9::LootTemplateOf<Access>("reference_loot_template", referenceId);
}

// ---------------------------------------------------------------------------------------------------------------------
// Quest relation tables: ObjectMgr::Get*Quest*Relations are public on ForeverCore.
// QuestObjectMgr in QuestValues.h is only used by QuestValues.cpp (not in slice); keep empty map macros for its header.
// ---------------------------------------------------------------------------------------------------------------------
using QuestRelationsMap = QuestRelations;
inline QuestRelationsMap& QuestRelationsMapCompat()
{
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

// ---------------------------------------------------------------------------------------------------------------------
// The global (shared) value context, "drop map", "item drop map", "item drop list", "entry loot list" (R3). T reads them through
// GAI_VALUE / GAI_VALUE2 (strategy/ValueMacros.h: sSharedObjectContext, whose constructor builds SharedValueContext, whose creators name
// the vendor, travel, trainer, quest, mount and PvP values). Those classes are not in the slice, so using the singleton at all would not link
// (R3: ten vtables). On F the shared context is not built and every read answers the empty value of its type: the drop maps are read from
// loot templates F keeps private anyway (feature "loot tables (drop maps)").
// Absent answer: T{} = nullptr for a `DropMap*`, an empty list for `entry loot list` / `item drop list`.
// Effect at the callers: every one tests for null / iterates a list: ItemDropMapValue (LootValues.cpp:157) returns the drop map without the item-sourced
// drops, ItemDropListValue and EntryLootListValue (:184, :205) return an empty list, EntryLootUsageValue and HasUpgradeValue (:259, :269) find no item
// (no upgrade is chased, none is looted for it), the game object paths of the stack (LootObjectStack.cpp:276, :519) and of LootAction (:312) are not
// reached on F (no game object is ever a loot target). Nothing is looted or chased because of them: LESS.
// ---------------------------------------------------------------------------------------------------------------------
template <class T, class Param>
inline T SharedValueUnportedCompat(Param const& /*param*/)
{
    tbcompat::Unported(tbcompat::Feature::LootTables);
    return T{};
}
#define GAI_VALUE_COMPAT(type, name) SharedValueUnportedCompat<type>(0)
#define GAI_VALUE2_COMPAT(type, name, param) SharedValueUnportedCompat<type>(param)
