// Slice 10, host package H10: see TbHybrid.h. Core API only; every read is side-effect free (no aura is touched, no creature is updated).
#include "host/TbHybrid.h"
#include "host/TbH3.h"

#include "ObjectAccessor.h"
#include "Item.h"
#include "Player.h"
#include "SharedDefines.h"
#include "SpellAuras.h"
#include "SpellInfo.h"
#include "Totem.h"

#include <cstring>
#include <string>
#include <vector>

namespace TortoiseBots
{
namespace h10
{
using namespace h3;

namespace
{
// The English name of a spell (the module reads names the same way: TbScripts.cpp). "" when the spell has none.
std::string SpellNameOf(SpellInfo const* info)
{
    if (!info || !info->SpellName)
        return std::string();
    char const* name = (*info->SpellName)[LOCALE_enUS];
    return name ? std::string(name) : std::string();
}

bool StartsWith(std::string const& text, char const* prefix)
{
    return text.compare(0, std::strlen(prefix), prefix) == 0;
}

bool EndsWith(std::string const& text, char const* suffix)
{
    size_t const n = std::strlen(suffix);
    return text.size() >= n && text.compare(text.size() - n, n, suffix) == 0;
}

// The totems the bot owns now: slots 1..4 (SUMMON_SLOT_TOTEM .. SUMMON_SLOT_TOTEM_4, SharedDefines.h:6745-6748). A slot whose guid no longer resolves to a Totem
// (it expired or the owner died: Totem.cpp:34-51 unsummons it) is skipped. Seconds are rounded UP so a totem with 400 ms left still reads 1.
std::vector<std::vector<std::string>> TotemItems(Player* bot)
{
    std::vector<std::vector<std::string>> items;
    for (uint32 slot = SUMMON_SLOT_TOTEM; slot < MAX_TOTEM_SLOT; ++slot)
    {
        ObjectGuid const guid = bot->m_SummonSlot[slot];
        if (guid.IsEmpty())
            continue;
        Creature* creature = ObjectAccessor::GetCreature(*bot, guid);
        Totem* totem = creature ? creature->ToTotem() : nullptr;
        if (!totem || !totem->IsAlive())
            continue;
        int64 const ms = totem->GetTotemDuration().count();
        items.push_back({ std::to_string(totem->GetEntry()), std::to_string(ms > 0 ? (ms + 999) / 1000 : 0) });
    }
    return items;
}

// The main-hand weapon's temporary enchantment (TEMP_ENCHANTMENT_SLOT = 1, ItemDefines.h:195): id and seconds left (the duration is in milliseconds).
std::vector<std::string> ImbueParts(Player* bot)
{
    Item* weapon = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND);
    if (!weapon)
        return {};
    uint32 const id = weapon->GetEnchantmentId(TEMP_ENCHANTMENT_SLOT);
    if (!id)
        return {};
    uint32 const ms = weapon->GetEnchantmentDuration(TEMP_ENCHANTMENT_SLOT);
    return { std::to_string(id), std::to_string((ms + 999) / 1000) };
}

// A paladin's own seal and own aura (name based: the ids come from the data, none is written here). The aura must be the bot's own (a party member's Devotion
// Aura on the bot is that member's, and a seal is never cast on another player).
void PaladinAuras(Player* bot, uint32& seal, uint32& paura)
{
    seal = 0;
    paura = 0;
    for (auto const& pair : bot->GetAppliedAuras())
    {
        Aura const* aura = pair.second->GetBase();
        if (!aura || aura->GetCasterGUID() != bot->GetGUID())
            continue;
        SpellInfo const* info = aura->GetSpellInfo();
        if (!info || info->SpellFamilyName != SPELLFAMILY_PALADIN)
            continue;
        std::string const name = SpellNameOf(info);
        if (!seal && StartsWith(name, "Seal of "))
            seal = info->Id;
        else if (!paura && EndsWith(name, " Aura"))
            paura = info->Id;
    }
}
}

void AppendHybridState(Rec& rec, Player* bot)
{
    uint32 const form = uint32(bot->GetShapeshiftForm());
    rec.S("form", form == uint32(FORM_NONE) ? std::string("none") : std::to_string(form));
    rec.LP("totems", TotemItems(bot));
    std::vector<std::string> const imbue = ImbueParts(bot);
    if (imbue.empty())
        rec.S("imbue", "");
    else
        rec.P("imbue", imbue);
    uint32 seal = 0, paura = 0;
    if (bot->GetClass() == CLASS_PALADIN)
        PaladinAuras(bot, seal, paura);
    rec.S("seal", seal ? std::to_string(seal) : std::string());
    rec.S("paura", paura ? std::to_string(paura) : std::string());
}

void SelfTestRecords(Reply& reply)
{
    reply.Line(Rec("selftest").S("case", "state-h10-bear").S("bot", "Mbear").S("form", "5").LP("totems", {}).S("imbue", "").S("seal", "").S("paura", ""));
    reply.Line(Rec("selftest").S("case", "state-h10-shaman").S("bot", "Msham").S("form", "none")
        .LP("totems", { { "5000", "52" }, { "5001", "118" } }).P("imbue", { "1000", "1795" }).S("seal", "").S("paura", ""));
    reply.Line(Rec("selftest").S("case", "state-h10-paladin").S("bot", "Mpal").S("form", "none").LP("totems", {}).S("imbue", "").S("seal", "2000").S("paura", "3000"));
}
}
}
