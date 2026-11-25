// Slice 3, host package H3: the test kit (spec decision D4): `.tb learn`, `.tb equip`, `.tb itemscan`. Core APIs only (no engine header); the commands
// refuse in a build without the engine, like every slice command. No id is written in this module: the driver passes spell and item ids from the
// data collaborator's lists (forever/data/waterys/rfc/) and `.tb itemscan` says what this server and this bot make of them.
#include "host/TbH3.h"
#include "TbBotMgr.h"
#include "host/TbEngineBrain.h"
#include "compat/TbItemPrototype.h"
#include "host/cmd/TbReplacePure.h"
#include "host/cmd/TbStartOutfit.h"
#include "host/cmd/TbTestTargetPure.h"

#include "DB2Stores.h"
#include "Chat.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "QuestDef.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "WorldSession.h"

#include <algorithm>
#include <cstdlib>
#include <vector>
#include <map>
#include <set>

namespace TortoiseBots
{
namespace h3
{
namespace
{
constexpr size_t MaxIds = 40;

// One piece a replace threw away: what the log line and the command reply name.
struct DestroyedPiece
{
    uint32 entry = 0;
    std::string guid;
    std::string reason;
};

// The core's InventoryResult by name (ItemDefines.h:24-140); the ones a gear check can meet, the number for the rest.
std::string ResultName(InventoryResult result)
{
    switch (result)
    {
#define TB_R(x) case x: return #x
        TB_R(EQUIP_ERR_OK);
        TB_R(EQUIP_ERR_CANT_EQUIP_LEVEL_I);
        TB_R(EQUIP_ERR_CANT_EQUIP_SKILL);
        TB_R(EQUIP_ERR_WRONG_SLOT);
        TB_R(EQUIP_ERR_BAG_FULL);
        TB_R(EQUIP_ERR_PROFICIENCY_NEEDED);
        TB_R(EQUIP_ERR_NO_SLOT_AVAILABLE);
        TB_R(EQUIP_ERR_CANT_EQUIP_EVER);
        TB_R(EQUIP_ERR_2HANDED_EQUIPPED);
        TB_R(EQUIP_ERR_2HSKILLNOTFOUND);
        TB_R(EQUIP_ERR_ITEM_MAX_COUNT);
        TB_R(EQUIP_ERR_NOT_EQUIPPABLE);
        TB_R(EQUIP_ERR_CANT_SWAP);
        TB_R(EQUIP_ERR_ITEM_NOT_FOUND);
        TB_R(EQUIP_ERR_NOT_OWNER);
        TB_R(EQUIP_ERR_GENERIC_STUNNED);
        TB_R(EQUIP_ERR_PLAYER_DEAD);
        TB_R(EQUIP_ERR_CLIENT_LOCKED_OUT);
        TB_R(EQUIP_ERR_INV_FULL);
        TB_R(EQUIP_ERR_NOT_IN_COMBAT);
        TB_R(EQUIP_ERR_CANT_EQUIP_RANK);
        TB_R(EQUIP_ERR_CANT_EQUIP_REPUTATION);
        TB_R(EQUIP_ERR_ITEM_UNIQUE_EQUIPPABLE);
        TB_R(EQUIP_ERR_ITEM_COOLDOWN);
        TB_R(EQUIP_ERR_CANT_EQUIP_NEED_TALENT);
        TB_R(EQUIP_ERR_SHAPESHIFT_FORM_CANNOT_EQUIP);
        TB_R(EQUIP_ERR_CANT_USE_ITEM);
#undef TB_R
        default:
            return "inventory-result-" + std::to_string(unsigned(result));
    }
}

// Digits only (target::ParseIdWord): strtoul used to take "-1" (wrapping to 4294967295, "all" for removeitem), "+3" and " 5". Used by learn, equip and
// removeitem, which now refuse those words. additem does not use this: it takes the core's command parser (TbScripts.cpp:888).
bool ParseId(std::string const& word, uint32& out)
{
    return target::ParseIdWord(word, out);
}

// The bot named by the first word: an in-world host bot.
Player* BotArg(ChatHandler* handler, Words const& args, char const* cmd)
{
    if (args.w.empty())
    {
        Fail(handler, args, cmd, "missing-bot", "name the bot first");
        return nullptr;
    }
    Player* bot = FindOnline(args.w[0]);
    if (!bot)
    {
        Fail(handler, args, cmd, "no-such-player", args.w[0] + " is not in the world");
        return nullptr;
    }
    if (!IsHostBot(bot))
    {
        Fail(handler, args, cmd, "not-a-bot", args.w[0] + " is not a logged-in host bot");
        return nullptr;
    }
    return bot;
}

// TB-1: the character `.tb learn`/`.tb equip` change: a host bot, or the sender's own character, never another real player. A first word that is already
// the command's own id means no name was given, and then the command means the sender's character.
Player* TargetArg(ChatHandler* handler, Words const& args, char const* cmd, size_t& start)
{
    start = 1;
    if (args.w.empty())
    {
        Fail(handler, args, cmd, "missing-bot", "name a host bot, or give an id with no name to act on your own character");
        return nullptr;
    }
    if (target::IsIdWord(args.w[0]))
        start = 0;
    std::string code, detail;
    Player* player = ResolveTestTarget(handler, start == 0 ? std::string_view() : std::string_view(args.w[0]), code, detail);
    if (!player)
        Fail(handler, args, cmd, code, detail);
    return player;
}

bool Ids(ChatHandler* handler, Words const& args, char const* cmd, size_t from, std::vector<uint32>& ids)
{
    if (args.w.size() <= from)
        return Fail(handler, args, cmd, "missing-argument", "give at least one id");
    if (args.w.size() - from > MaxIds)
        return Fail(handler, args, cmd, "too-many", "at most " + std::to_string(MaxIds) + " ids per command");
    for (size_t i = from; i < args.w.size(); ++i)
    {
        uint32 id = 0;
        if (!ParseId(args.w[i], id))
            return Fail(handler, args, cmd, "bad-id", args.w[i]);
        ids.push_back(id);
    }
    return true;
}

// The item objectives of every quest still in the character's log (incomplete, complete, failed): the required items `.tb equip replace` may not
// destroy. The quest log is the core's own map (Player::getQuestStatusMap), a rewarded quest is not in it.
std::vector<replace::ActiveObjective> ActiveItemObjectives(Player* player)
{
    std::vector<replace::ActiveObjective> objectives;
    for (auto const& logged : player->getQuestStatusMap())
    {
        QuestStatus const status = logged.second.Status;
        if (status != QUEST_STATUS_INCOMPLETE && status != QUEST_STATUS_COMPLETE && status != QUEST_STATUS_FAILED)
            continue;
        Quest const* quest = sObjectMgr->GetQuestTemplate(logged.first);
        if (!quest)
            continue;
        for (QuestObjective const& objective : quest->GetObjectives())
            if (objective.Type == QUEST_OBJECTIVE_ITEM)
                objectives.push_back({int(objective.Type), objective.ObjectID});
    }
    return objectives;
}
}

// ---- `.tb learn <bot> <spell id>... [#token]` ----
// The core's own learn path (the one `.learn` takes, cs_learn.cpp:78-103): SpellMgr::IsSpellValid, then Player::LearnSpell (Player.h:2029). An id the
// core does not know is refused. No rank chain is added: every id is asked for by name.

bool HandleLearn(ChatHandler* handler, Trinity::ChatCommands::Tail tail)
{
    Words const args = Split(tail);
    if (!RequireBuilt(handler, args, "learn"))
        return false;
    size_t start = 1;
    Player* bot = TargetArg(handler, args, "learn", start);
    if (!bot)
        return false;
    std::vector<uint32> ids;
    if (!Ids(handler, args, "learn", start, ids))
        return false;

    uint32 learned = 0;
    Reply reply(handler, args, "learn");
    reply.Begin();
    for (uint32 id : ids)
    {
        std::string result;
        SpellInfo const* info = sSpellMgr->GetSpellInfo(id, DIFFICULTY_NONE);
        if (!info)
            result = "unknown";
        else if (!SpellMgr::IsSpellValid(info, bot, false))
            result = "invalid";
        else if (bot->HasSpell(id))
            result = "known";
        else
        {
            bot->LearnSpell(id, false);
            result = bot->HasSpell(id) ? "learned" : "not-learned";
            if (result == "learned")
                ++learned;
        }
        Rec rec("learn");
        rec.S("bot", bot->GetName()).N("spell", id).S("result", result).S("name", info && info->SpellName ? info->SpellName->Str[LOCALE_enUS] : "");
        reply.Line(rec);
    }
    Rec end("learn");
    end.S("bot", bot->GetName()).N("learned", learned);
    reply.End(end);
    return true;
}

// ---- `.tb equip <bot> <item id> [replace] [#token]` ----
// The core's own checks and calls: Player::CanEquipNewItem (Player.h:1604: class, level, skill, slot, two-hand rules, combat, stun), then
// Player::EquipNewItem (:1621) into the slot it found. A slot that is taken refuses with the core's reason (EQUIP_ERR_NO_SLOT_AVAILABLE). With
// `replace` the item goes to the bags and the core's own swap (Player::SwapItem, the call of a client's equip click) puts it on and the old piece
// into the bags, after the same check with swap = true. A displaced piece the equipment makes disposable (start-outfit gear, or poor quality) is
// destroyed instead of kept, only a single item (a stack stays in the bags, named in `note`), found again by GUID after the swap, only once the incoming piece is confirmed in the slot; a quest item is never destroyed (TbReplacePure.h). The log line
// names a destroy and the record lists it.

bool HandleEquip(ChatHandler* handler, Trinity::ChatCommands::Tail tail)
{
    Words const args = Split(tail);
    if (!RequireBuilt(handler, args, "equip"))
        return false;
    size_t start = 1;
    Player* bot = TargetArg(handler, args, "equip", start);
    if (!bot)
        return false;
    uint32 item = 0;
    if (args.w.size() <= start || !ParseId(args.w[start], item))
        return Fail(handler, args, "equip", "missing-argument", ".tb equip <character> <item id> [replace]");
    bool const replace = args.w.size() > start + 1 && Lower(args.w[start + 1]) == "replace";
    if (args.w.size() > start + 1 && !replace)
        return Fail(handler, args, "equip", "unexpected-argument", args.w[start + 1]);

    ItemTemplate const* proto = sObjectMgr->GetItemTemplate(item);
    Reply reply(handler, args, "equip");
    // One equip record: the item, the core's answer, the slot or the reason, and what a replace threw away; one line.
    auto const say = [&](char const* result, std::string const& why, int slot = -1, int code = -1, char const* note = nullptr,
                         bool replaced = false, std::vector<DestroyedPiece> const* destroyed = nullptr)
    {
        Rec rec("equip");
        rec.S("bot", bot->GetName()).N("item", item);
        if (proto)
            rec.S("name", proto->GetName(LOCALE_enUS));
        rec.S("result", result);
        if (slot >= 0)
            rec.N("slot", slot);
        rec.S("why", why);
        if (code >= 0)
            rec.N("code", code);
        if (replaced)
            rec.B("replaced", true);
        if (destroyed && !destroyed->empty())
        {
            std::vector<std::vector<std::string>> rows;
            rows.reserve(destroyed->size());
            for (DestroyedPiece const& piece : *destroyed)
                rows.push_back({std::to_string(piece.entry), piece.guid, piece.reason});
            rec.N("destroyed", int64(destroyed->size())).LP("destroyed_item", rows);
        }
        if (note)
            rec.S("note", note);
        reply.Begin();
        reply.Line(rec);
        reply.End(Rec("equip").S("bot", bot->GetName()));
    };
    if (!proto)
    {
        say("refused", "unknown-item");
        return true;
    }

    uint16 dest = 0;
    InventoryResult result = bot->CanEquipNewItem(NULL_SLOT, dest, item, false);
    if (result == EQUIP_ERR_OK)
    {
        Item* worn = bot->EquipNewItem(dest, item, ItemContext::NONE, true);
        if (worn)
            say("equipped", "", int(dest & 255));
        else
            say("refused", "core-returned-no-item");
        return true;
    }
    // With swap = false the core answers an occupied weapon / shield / ring slot with EQUIP_ERR_NO_SLOT_AVAILABLE (Player.cpp:11927) but FindEquipSlot returns
    // NULL_SLOT and so EQUIP_ERR_NOT_EQUIPPABLE (:11821-11822) when every slot of the item's type is taken (live, M package: a one-hand weapon over a starter weapon).
    // Both mean "the slot is taken": `replace` asks again with swap = true, which names the real refusal (proficiency, level, class...).
    if (!replace || (result != EQUIP_ERR_NO_SLOT_AVAILABLE && result != EQUIP_ERR_NOT_EQUIPPABLE))
    {
        say("refused", ResultName(result), -1, int(result));
        return true;
    }

    result = bot->CanEquipNewItem(NULL_SLOT, dest, item, true);
    if (result != EQUIP_ERR_OK)
    {
        say("refused", ResultName(result), -1, int(result));
        return true;
    }
    if (!bot->StoreNewItemInBestSlots(item, 1, ItemContext::NONE))
    {
        say("refused", "bags-full");
        return true;
    }
    Item* fresh = bot->GetItemByEntry(item, ItemSearchLocation::Inventory);
    uint16 swapDest = 0;
    if (!fresh || (result = bot->CanEquipItem(NULL_SLOT, swapDest, fresh, true)) != EQUIP_ERR_OK)
    {
        say("refused", fresh ? ResultName(result) : std::string("item-not-in-bags"), -1, -1, "item left in bags");
        return true;
    }
    // The pieces the swap will take off the body: the one in the named slot and, for a two-hander, the offhand (SwapItem's
    // AutoUnequipOffhandIfNeed puts it in the bags). Only their GUIDs are kept: SwapItem stores the old piece with StoreItem, which merges a
    // stackable piece into an equal stack in the bags and frees the moved Item, so a raw Item* would dangle. They are re-resolved after the swap.
    Item const* const displacedItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, uint8(swapDest & 255));
    ObjectGuid const displacedGuid = displacedItem ? displacedItem->GetGUID() : ObjectGuid::Empty;
    Item const* const displacedOffItem = ((swapDest & 255) == EQUIPMENT_SLOT_MAINHAND && proto->GetInventoryType() == INVTYPE_2HWEAPON)
        ? bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND)
        : nullptr;
    ObjectGuid const displacedOffGuid = displacedOffItem ? displacedOffItem->GetGUID() : ObjectGuid::Empty;
    Player* const sender = SenderPlayer(handler);
    bool const mayDestroy = replace::MayDestroyReplacedItem(IsHostBot(bot),
        !handler->GetSession() || handler->GetSession()->GetSecurity() >= SEC_GAMEMASTER, sender && (sender == bot));
    bot->SwapItem(fresh->GetPos(), swapDest);
    Item* worn = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, uint8(swapDest & 255));
    bool const equipped = worn && worn->GetEntry() == item;
    if (equipped)
    {
        std::vector<replace::ActiveObjective> const activeItems = ActiveItemObjectives(bot);
        std::vector<DestroyedPiece> destroyed;
        std::string kept;   // a disposable stack that was left in the bags, named in the record
        auto const dropIfDisposable = [&](ObjectGuid const& guid)
        {
            if (guid.IsEmpty() || !mayDestroy)
                return;
            // Re-resolved by GUID after the swap: a piece the swap merged into a bag stack is gone and nothing is destroyed.
            Item* const piece = bot->GetItemByGuid(guid);
            if (!piece)
                return;
            ItemTemplate const* oldProto = piece->GetTemplate();
            if (!oldProto)
                return;
            // A quest item by class, by bonding or by the quest it starts, or a required item of a quest the character still has, is never
            // destroyed: it goes to the bags (TbReplacePure.h).
            bool const questItem = replace::IsQuestItem(piece->GetEntry(), oldProto->GetClass() == ITEM_CLASS_QUEST,
                oldProto->GetBonding() == BIND_QUEST, oldProto->GetStartQuest() != 0, activeItems);
            replace::Disposable const why = replace::DisposableReason(equipped,
                replace::StartOutfitItem(bot, piece->GetEntry()), oldProto->GetQuality(), questItem, piece->IsNotEmptyBag());
            // Only a piece that really moved into the bags (never the one just equipped), and only a single item: a stack stays in the bags.
            replace::Fate const fate = replace::DisplacedFate(true, Player::IsInventoryPos(piece->GetBagSlot(), piece->GetSlot()),
                piece->GetGUID() == worn->GetGUID(), piece->GetCount(), why);
            if (fate == replace::Fate::KeepStack)
            {
                TC_LOG_INFO("tortoisebots", "TestKit: .tb equip kept {} x{} ({}) of {} ({}) in the bags: disposable ({}) but a stack of more than one",
                    piece->GetEntry(), piece->GetCount(), piece->GetGUID().ToString(), bot->GetName(), bot->GetGUID().ToString(),
                    replace::ReasonName(why));
                if (!kept.empty())
                    kept += ';';
                kept += "kept-stack:" + std::to_string(piece->GetEntry()) + "x" + std::to_string(piece->GetCount()) + " in bags";
                return;
            }
            if (fate != replace::Fate::Destroy)
                return;
            destroyed.push_back({piece->GetEntry(), piece->GetGUID().ToString(), replace::ReasonName(why)});
            // One durable line per destroy: what left the character, whose it was, and why it was not worth a bag slot.
            TC_LOG_INFO("tortoisebots", "TestKit: .tb equip destroyed {} x{} ({}) of {} ({}): the replaced piece is {}",
                piece->GetEntry(), piece->GetCount(), piece->GetGUID().ToString(), bot->GetName(), bot->GetGUID().ToString(),
                replace::ReasonName(why));
            bot->DestroyItem(piece->GetBagSlot(), piece->GetSlot(), true);
        };
        dropIfDisposable(displacedGuid);
        dropIfDisposable(displacedOffGuid);
        say("equipped", "", int(swapDest & 255), -1, kept.empty() ? nullptr : kept.c_str(), true, &destroyed);
    }
    else
        say("refused", "swap-failed", -1, -1, "item left in bags");
    return true;
}

// ---- `.tb removeitem [<character>] <item id> [count] [#token]` ----
// The inverse of `.tb additem`: takes up to `count` (no count = all) of the item out of the target's BAGS (backpack, the worn bags and the reagent bag),
// through the core's Item*-based Player::DestroyItemCount(Item*, uint32&, bool) (Player.h:1679: quest bookkeeping included). The stacks are collected
// first, by the pure rule target::IsBagStack (never equipped, bank, keyring, child-equipment, the worn bags themselves, nor a stack in an open trade), and
// only those Items are destroyed, so nothing outside the bags can be touched (the core's entry-based DestroyItemCount sweeps equipment and bank too).
// Only the item named is touched, so a quest item goes only when it is the one asked for. One record `removeitem bot=.. item=<id> removed=<n> left=<n>`
// (left = what the bags hold AFTER the removal, read again, not computed; an open-trade stack is still in its bag slot, so it counts in left but not in
// removed); errors are typed: missing-bot / no-such-player / not-a-bot / no-character /
// engine-not-built / missing-argument / bad-id / unknown-item / unexpected-argument.
// Persistence: like the core's own `.additem` (cs_misc.cpp HandleAddItemCommandHelper, which calls no save), the change is written by the next
// autosave or logout (DestroyItem marks the row removed); this test kit does not force a save, a crash before then brings the item back.

static_assert(target::kBagZero == INVENTORY_SLOT_BAG_0, "bag-stack rule: bag 0");
static_assert(target::kBagStart == INVENTORY_SLOT_BAG_START, "bag-stack rule: first worn bag");
static_assert(target::kReagentBagEnd == REAGENT_BAG_SLOT_END, "bag-stack rule: end of the reagent bag");
static_assert(target::kBackpackStart == INVENTORY_SLOT_ITEM_START, "bag-stack rule: backpack start");

bool HandleRemoveItem(ChatHandler* handler, Trinity::ChatCommands::Tail tail)
{
    Words const args = Split(tail);
    if (!RequireBuilt(handler, args, "removeitem"))
        return false;
    size_t start = 1;
    Player* bot = TargetArg(handler, args, "removeitem", start);
    if (!bot)
        return false;
    if (args.w.size() <= start)
        return Fail(handler, args, "removeitem", "missing-argument", ".tb removeitem <character> <item id> [count]");
    uint32 item = 0;
    if (!ParseId(args.w[start], item))
        return Fail(handler, args, "removeitem", "bad-id", args.w[start]);
    uint32 requested = 0;
    if (args.w.size() > start + 1 && !ParseId(args.w[start + 1], requested))
        return Fail(handler, args, "removeitem", "bad-id", args.w[start + 1]);
    if (args.w.size() > start + 2)
        return Fail(handler, args, "removeitem", "unexpected-argument", args.w[start + 2]);
    if (!sObjectMgr->GetItemTemplate(item))
        return Fail(handler, args, "removeitem", "unknown-item", args.w[start]);

    // The exact stacks in the bags (pure rule target::IsBagStack). ForEachItem(Inventory) walks the backpack and the worn bags' contents, the
    // ReagentBank location walks the reagent bag's; the rule drops everything else it also returns (equipped bags, keyring slots).
    uint8 const backpackSlots = uint8(bot->GetInventorySlotCount());
    // removable: the stacks that may be destroyed (an open-trade stack is skipped); otherwise every matching stack still in a bag slot, trade included.
    auto collect = [&](bool removable)
    {
        std::vector<Item*> stacks;
        auto visit = [&](Item* it)
        {
            if (it->GetEntry() == item && target::IsBagStack(it->GetBagSlot(), it->GetSlot(), backpackSlots, removable && it->IsInTrade()))
                stacks.push_back(it);
            return ItemSearchCallbackResult::Continue;
        };
        bot->ForEachItem(ItemSearchLocation::Inventory, visit);
        bot->ForEachItem(ItemSearchLocation::ReagentBank, visit);
        return stacks;
    };
    auto total = [](std::vector<Item*> const& stacks)
    {
        uint32 sum = 0;
        for (Item* it : stacks)
            sum += it->GetCount();
        return sum;
    };

    std::vector<Item*> const stacks = collect(true);
    uint32 const want = target::RemoveAmount(total(stacks), requested);
    uint32 remaining = want;
    for (Item* it : stacks)
    {
        if (!remaining)
            break;
        bot->DestroyItemCount(it, remaining, true);   // an Item* destroyed here is never read again; the other collected Items are separate objects
    }
    uint32 const removed = want - remaining;
    uint32 const left = total(collect(false));

    Reply reply(handler, args, "removeitem");
    reply.Begin();
    reply.Line(Rec("removeitem").S("bot", bot->GetName()).N("item", item).N("removed", removed).N("left", left));
    reply.End(Rec("removeitem").S("bot", bot->GetName()));
    return true;
}

// ---- `.tb itemscan <bot> <item id>... [#token]` ----
// For each id: whether the item exists on this server (ObjectMgr::GetItemTemplate), how many the bot has on it (Player::GetItemCount: its worn
// pieces and everything in its bags, never bank storage) and whether THIS bot could equip it now, by the core's own check with swap = true (an
// occupied slot does not count against it): Player::CanEquipNewItem (class, level, skill, slot, combat). Nothing is created or changed (the check
// makes a temporary item and deletes it).

bool HandleItemScan(ChatHandler* handler, Trinity::ChatCommands::Tail tail)
{
    Words const args = Split(tail);
    if (!RequireBuilt(handler, args, "itemscan"))
        return false;
    Player* bot = BotArg(handler, args, "itemscan");
    if (!bot)
        return false;
    std::vector<uint32> ids;
    if (!Ids(handler, args, "itemscan", 1, ids))
        return false;

    uint32 exist = 0, can = 0;
    Reply reply(handler, args, "itemscan");
    reply.Begin();
    for (uint32 id : ids)
    {
        Rec rec("itemscan");
        rec.S("bot", bot->GetName()).N("item", id);
        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(id);
        if (!proto)
        {
            rec.B("exists", false);
            reply.Line(rec);
            continue;
        }
        ++exist;
        uint16 dest = 0;
        InventoryResult const result = bot->CanEquipNewItem(NULL_SLOT, dest, id, true);
        bool const ok = result == EQUIP_ERR_OK;
        if (ok)
            ++can;
        rec.B("exists", true).S("name", proto->GetName(LOCALE_enUS)).P("class", {std::to_string(proto->GetClass()), std::to_string(proto->GetSubClass())});
        rec.N("inv", bot->GetItemCount(id, false)).N("type", unsigned(proto->GetInventoryType())).N("req", proto->GetBaseRequiredLevel()).N("quality", proto->GetQuality()).N("ilvl", proto->GetBaseItemLevel());
        rec.B("canequip", ok);
        if (ok)
            rec.N("slot", unsigned(dest & 255));
        rec.S("why", ok ? std::string() : ResultName(result));
        AppendItemScanEngine(bot, id, rec);
        reply.Line(rec);
    }
    Rec end("itemscan");
    end.S("bot", bot->GetName()).N("exist", exist).N("canequip", can);
    reply.End(end);
    return true;
}

// ---- `.tb statcensus [#token]` ----
// One-pass census of ItemStatType ids across all loaded item templates.

bool HandleStatCensus(ChatHandler* handler, Trinity::ChatCommands::Tail tail)
{
    Words const args = Split(tail);
    if (!RequireBuilt(handler, args, "statcensus"))
        return false;
    if (!args.w.empty())
        return Fail(handler, args, "statcensus", "unexpected-argument", args.w[0]);

    struct StatStat
    {
        uint32 items = 0;
        int32 minVal = 0;
        int32 maxVal = 0;
        uint32 sampleItem = 0;
        std::string sampleName;
        int32 sampleVal = 0;
    };

    std::map<int32, StatStat> byId;
    uint32 templates = 0;
    uint32 withStats = 0;
    for (auto const& [entry, tpl] : sObjectMgr->GetItemTemplateStore())
    {
        ++templates;
        ItemPrototype const* proto = tbcompat::ItemProto(entry);
        if (!proto)
            continue;
        bool any = false;
        std::set<int32> seenOnItem;
        for (uint32 i = 0; i < MAX_ITEM_PROTO_STATS; ++i)
        {
            int32 const statVal = proto->ItemStat[i].ItemStatValue;
            if (statVal == 0)
                continue;
            any = true;
            int32 const statType = int32(proto->ItemStat[i].ItemStatType);
            StatStat& st = byId[statType];
            if (seenOnItem.insert(statType).second)
                ++st.items;
            if (st.sampleItem == 0)
            {
                st.minVal = statVal;
                st.maxVal = statVal;
                st.sampleItem = entry;
                st.sampleName = proto->Name1;
                st.sampleVal = statVal;
            }
            else
            {
                st.minVal = std::min(st.minVal, statVal);
                st.maxVal = std::max(st.maxVal, statVal);
            }
        }
        if (any)
            ++withStats;
    }

    Reply reply(handler, args, "statcensus");
    reply.Begin();
    for (auto const& [id, st] : byId)
    {
        Rec rec("statcensus");
        rec.N("id", id).N("items", st.items).N("min", st.minVal).N("max", st.maxVal)
           .N("sample", st.sampleItem).S("samplename", st.sampleName).N("sampleval", st.sampleVal);
        reply.Line(rec);
    }
    Rec end("statcensus");
    end.N("templates", templates).N("with_stats", withStats).N("stat_ids", byId.size());
    reply.End(end);
    return true;
}
}
}
