// Slice 3, host package H3: the test kit (spec decision D4): `.tb learn`, `.tb equip`, `.tb itemscan`. Core APIs only (no engine header); the commands
// refuse in a build without the engine, like every slice command. No id is written in this module: the driver passes spell and item ids from the
// data collaborator's lists (forever/data/waterys/rfc/) and `.tb itemscan` says what this server and this bot make of them.
#include "host/TbH3.h"
#include "TbBotMgr.h"
#include "host/TbEngineBrain.h"

#include "DB2Stores.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "SpellInfo.h"
#include "SpellMgr.h"

#include <cstdlib>

namespace TortoiseBots
{
namespace h3
{
namespace
{
constexpr size_t MaxIds = 40;

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

bool ParseId(std::string const& word, uint32& out)
{
    if (word.empty() || word.size() > 9)
        return false;
    char* end = nullptr;
    unsigned long const value = std::strtoul(word.c_str(), &end, 10);
    if (!end || *end != '\0' || !value)
        return false;
    out = uint32(value);
    return true;
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
}

// ---- `.tb learn <bot> <spell id>... [#token]` ----
// The core's own learn path (the one `.learn` takes, cs_learn.cpp:78-103): SpellMgr::IsSpellValid, then Player::LearnSpell (Player.h:2028). An id the
// core does not know is refused. No rank chain is added: every id is asked for by name.

bool HandleLearn(ChatHandler* handler, Trinity::ChatCommands::Tail tail)
{
    Words const args = Split(tail);
    if (!RequireBuilt(handler, args, "learn"))
        return false;
    Player* bot = BotArg(handler, args, "learn");
    if (!bot)
        return false;
    std::vector<uint32> ids;
    if (!Ids(handler, args, "learn", 1, ids))
        return false;

    uint32 learned = 0;
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
        Say(handler, "learn bot=" + Esc(bot->GetName()) + " spell=" + std::to_string(id) + " result=" + result +
            " name=" + Esc(info && info->SpellName ? info->SpellName->Str[LOCALE_enUS] : ""), args);
    }
    Say(handler, "learn-end bot=" + Esc(bot->GetName()) + " count=" + std::to_string(ids.size()) + " learned=" + std::to_string(learned), args);
    return true;
}

// ---- `.tb equip <bot> <item id> [replace] [#token]` ----
// The core's own checks and calls: Player::CanEquipNewItem (Player.h:1603: class, level, skill, slot, two-hand rules, combat, stun), then
// Player::EquipNewItem (:1621) into the slot it found. A slot that is taken refuses with the core's reason (EQUIP_ERR_NO_SLOT_AVAILABLE). With
// `replace` the item goes to the bags and the core's own swap (Player::SwapItem, the call of a client's equip click) puts it on and the old piece
// into the bags, after the same check with swap = true.

bool HandleEquip(ChatHandler* handler, Trinity::ChatCommands::Tail tail)
{
    Words const args = Split(tail);
    if (!RequireBuilt(handler, args, "equip"))
        return false;
    Player* bot = BotArg(handler, args, "equip");
    if (!bot)
        return false;
    uint32 item = 0;
    if (args.w.size() < 2 || !ParseId(args.w[1], item))
        return Fail(handler, args, "equip", "missing-argument", ".tb equip <bot> <item id> [replace]");
    bool const replace = args.w.size() > 2 && Lower(args.w[2]) == "replace";
    if (args.w.size() > 2 && !replace)
        return Fail(handler, args, "equip", "unexpected-argument", args.w[2]);

    std::string const head = "equip bot=" + Esc(bot->GetName()) + " item=" + std::to_string(item);
    ItemTemplate const* proto = sObjectMgr->GetItemTemplate(item);
    if (!proto)
    {
        Say(handler, head + " result=refused why=unknown-item", args);
        return true;
    }
    std::string const name = Esc(proto->GetName(LOCALE_enUS));

    uint16 dest = 0;
    InventoryResult result = bot->CanEquipNewItem(NULL_SLOT, dest, item, false);
    if (result == EQUIP_ERR_OK)
    {
        Item* worn = bot->EquipNewItem(dest, item, ItemContext::NONE, true);
        Say(handler, head + " name=" + name + (worn ? " result=equipped slot=" + std::to_string(unsigned(dest & 255)) + " why=none" : " result=refused why=core-returned-no-item"), args);
        return true;
    }
    // With swap = false the core answers an occupied weapon / shield / ring slot with EQUIP_ERR_NO_SLOT_AVAILABLE (Player.cpp:11831) but FindEquipSlot returns
    // NULL_SLOT and so EQUIP_ERR_NOT_EQUIPPABLE (:11821-11822) when every slot of the item's type is taken (live, M package: a one-hand weapon over a starter weapon).
    // Both mean "the slot is taken": `replace` asks again with swap = true, which names the real refusal (proficiency, level, class...).
    if (!replace || (result != EQUIP_ERR_NO_SLOT_AVAILABLE && result != EQUIP_ERR_NOT_EQUIPPABLE))
    {
        Say(handler, head + " name=" + name + " result=refused why=" + ResultName(result) + " code=" + std::to_string(unsigned(result)), args);
        return true;
    }

    result = bot->CanEquipNewItem(NULL_SLOT, dest, item, true);
    if (result != EQUIP_ERR_OK)
    {
        Say(handler, head + " name=" + name + " result=refused why=" + ResultName(result) + " code=" + std::to_string(unsigned(result)), args);
        return true;
    }
    if (!bot->StoreNewItemInBestSlots(item, 1, ItemContext::NONE))
    {
        Say(handler, head + " name=" + name + " result=refused why=bags-full", args);
        return true;
    }
    Item* fresh = bot->GetItemByEntry(item, ItemSearchLocation::Inventory);
    uint16 swapDest = 0;
    if (!fresh || (result = bot->CanEquipItem(NULL_SLOT, swapDest, fresh, true)) != EQUIP_ERR_OK)
    {
        Say(handler, head + " name=" + name + " result=refused why=" + (fresh ? ResultName(result) : std::string("item-not-in-bags")) + " note=item_left_in_bags", args);
        return true;
    }
    bot->SwapItem(fresh->GetPos(), swapDest);
    Item* worn = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, uint8(swapDest & 255));
    if (worn && worn->GetEntry() == item)
        Say(handler, head + " name=" + name + " result=equipped slot=" + std::to_string(unsigned(swapDest & 255)) + " why=none replaced=yes", args);
    else
        Say(handler, head + " name=" + name + " result=refused why=swap-failed note=item_left_in_bags", args);
    return true;
}

// ---- `.tb itemscan <bot> <item id>... [#token]` ----
// For each id: whether the item exists on this server (ObjectMgr::GetItemTemplate) and whether THIS bot could equip it now, by the core's own check
// with swap = true (an occupied slot does not count against it): Player::CanEquipNewItem (class, level, skill, slot, combat). Nothing is created or
// changed (the check makes a temporary item and deletes it).

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
    for (uint32 id : ids)
    {
        std::string line = "itemscan bot=" + Esc(bot->GetName()) + " item=" + std::to_string(id);
        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(id);
        if (!proto)
        {
            Say(handler, line + " exists=no", args);
            continue;
        }
        ++exist;
        uint16 dest = 0;
        InventoryResult const result = bot->CanEquipNewItem(NULL_SLOT, dest, id, true);
        bool const ok = result == EQUIP_ERR_OK;
        if (ok)
            ++can;
        line += " exists=yes name=" + Esc(proto->GetName(LOCALE_enUS)) + " class=" + std::to_string(proto->GetClass()) + "/" + std::to_string(proto->GetSubClass()) +
            " inv=" + std::to_string(unsigned(proto->GetInventoryType())) + " req=" + std::to_string(proto->GetBaseRequiredLevel()) +
            " quality=" + std::to_string(proto->GetQuality()) + " ilvl=" + std::to_string(proto->GetBaseItemLevel()) +
            std::string(" canequip=") + (ok ? "yes" : "no") + " slot=" + (ok ? std::to_string(unsigned(dest & 255)) : std::string("none")) +
            " why=" + (ok ? std::string("none") : ResultName(result));
        Say(handler, line, args);
    }
    Say(handler, "itemscan-end bot=" + Esc(bot->GetName()) + " count=" + std::to_string(ids.size()) + " exist=" + std::to_string(exist) + " canequip=" + std::to_string(can), args);
    return true;
}
}
}
