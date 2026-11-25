// Free-bot profession upkeep on the ForeverCore build.
//
// An enchanter keeps its own bags and gear in order: it breaks down
// disenchantable junk (uncommon armor and weapons the core can disenchant at
// its skill) and applies a better permanent enchant to the gear it wears.
// Both halves cast through the core's own opcode path, CMSG_CAST_SPELL with
// the item as target (Spell::SpellCastTargets reads TARGET_FLAG_ITEM,
// Spell.cpp:411), so the core's fit, reagent, totem and skill checks decide
// and the cast's skill-up lands (SpellEffects.cpp:2543,3636).
//
// The decisions are in ai/playerbot/policy/profession/EnchantUpkeepPolicy.h;
// the disenchant loot window is taken with the mail service's own helper
// because both cast the same spell. World thread, from BotMgr's per-bot pass
// after the engine tick; no database call.
#include "host/TbHostProfessionUpkeep.h"

#include "host/TbHostCapitalErrands.h"
#include "host/TbHostGuild.h"
#include "host/TbHostGuildMail.h"
#include "host/TbHostGuildOrderDisenchant.h"
#include "host/TbHostGuildOrderEnchant.h"
#include "host/TbHostGuildOrders.h"
#include "host/TbHostProfession.h"
#include "host/pool/TbPoolRegistry.h"

#include "ai/playerbot/policy/profession/EnchantUpkeepPolicy.h"
#include "playerbot/GuildOrderEnchants.h"
#include "runtime/GuildOrderContract.h"
#include "runtime/GuildOrderDisenchantPolicy.h"
#include "runtime/GuildOrderEnchantPolicy.h"

#include "CharacterCache.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "SharedDefines.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "SpellPackets.h"
#include "WorldPacket.h"
#include "WorldSession.h"

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace tbhost
{
namespace upkeep
{
namespace
{
using TortoiseBots::GuildOrders::EnchantEntry;

// The client's Disenchant, the same spell the mail service casts (the core
// validates the effect and the item at Spell.cpp:8220).
constexpr uint32_t kSpellDisenchant = 13262;
// The strongest rod: the enchanting totem requirement accepts its category,
// so one rod covers every enchant the catalogue holds.
constexpr uint32_t kArcaniteRod = 16207;

constexpr uint32_t kDisenchantCooldownMs = 5000;
constexpr uint32_t kEnchantCooldownMs = 15000;
constexpr uint64_t kCastTimeoutMs = 15000;
// A cast queued behind a global cooldown may start late; nothing is decided
// about the item before this grace has run out.
constexpr uint64_t kCastGraceMs = 2500;
// A cast that did not land is not retried on the same item for a while.
constexpr uint64_t kFailSkipMs = 10 * 60 * 1000;

struct PendingCast
{
    bool active = false;
    bool enchant = false;
    ObjectGuid itemGuid;
    uint32_t itemEntry = 0;
    uint32_t spellId = 0;
    uint32_t enchantId = 0;
    uint64_t startedMs = 0;
    std::unordered_map<uint32_t, uint32_t> bagsBefore; // disenchant only
};

struct BotState
{
    uint64_t nowMs = 0;
    PendingCast cast;
    uint64_t nextDisenchantMs = 0;
    uint64_t nextEnchantMs = 0;
    bool grantedDisenchant = false;
    uint16_t kitSkill = 0; // enchanting skill the recipes were granted for
    std::unordered_map<uint64_t, uint64_t> failedItems;                        // disenchant stack -> retry after
    std::unordered_map<uint64_t, std::unordered_map<uint32_t, uint64_t>> failedEnchants; // item -> spell -> retry after
};

std::unordered_map<uint32_t, BotState>& States()
{
    static std::unordered_map<uint32_t, BotState> states;
    return states;
}

std::vector<EnchantEntry> const& Catalogue()
{
    static std::vector<EnchantEntry> const entries = ai::GuildOrderEnchants();
    return entries;
}

std::unordered_map<uint32_t, uint32_t> BagCounts(Player* bot)
{
    std::unordered_map<uint32_t, uint32_t> counts;
    bot->ForEachItem(ItemSearchLocation::Inventory, [&counts](Item* item)
    {
        counts[item->GetEntry()] += item->GetCount();
        return ItemSearchCallbackResult::Continue;
    });
    return counts;
}

std::string ItemName(Item* item)
{
    ItemTemplate const* proto = item ? item->GetTemplate() : nullptr;
    return proto ? std::string(proto->GetName(LOCALE_enUS)) : std::string();
}

// A real player shares the world when the guild's leader sits on an account
// the module did not create: the same narrow test the other all-bot rules use.
bool SharesWithRealPlayer(Player* bot)
{
    if (!bot->GetGuildId())
        return false;
    ObjectGuid const leader = tbhost::guild::GetLeaderGuid(bot->GetGuildId());
    if (leader.IsEmpty())
        return false;
    uint32_t const accountId = sCharacterCache->GetCharacterAccountIdByGuid(leader);
    return accountId != 0 && !TortoiseBots::pool::AccountRegistered(accountId);
}

bool ProtectedItem(Player* bot, uint32_t itemId)
{
    if (!itemId)
        return true;
    if (tbhost::guild_orders::IsItemNeededByGuildOrder(bot->GetGuildId(), itemId))
        return true;
    return TortoiseBots::GuildOrders::DisenchantRowsHoldItem(
        tbhost::guild_orders::DisenchantQueued(bot->GetGUID().GetCounter()), itemId);
}

bool OtherErrand(Player* bot)
{
    return tbhost::prof::HasActiveErrand(bot) ||
        tbhost::errands::HasActiveErrand(bot) ||
        tbhost::guild_mail::HasActiveErrand(bot) ||
        tbhost::guild_orders::HasActiveDisenchantErrand(bot) ||
        tbhost::guild_orders::HasActiveEnchantErrand(bot) ||
        tbhost::guild_orders::HasOpenOrder(bot->GetGUID().GetCounter());
}

// The enchant id the spell applies (SPELL_EFFECT_ENCHANT_ITEM's misc value),
// 0 when the spell is not an enchant.
uint32_t EnchantIdOfSpell(uint32_t spellId)
{
    SpellInfo const* spell = sSpellMgr->GetSpellInfo(spellId, DIFFICULTY_NONE);
    if (!spell)
        return 0;
    for (SpellEffectInfo const& effect : spell->GetEffects())
        if (effect.Effect == SPELL_EFFECT_ENCHANT_ITEM && effect.MiscValue > 0)
            return uint32_t(effect.MiscValue);
    return 0;
}

bool GrantItem(Player* bot, uint32_t itemId)
{
    if (!bot || !itemId || !sObjectMgr->GetItemTemplate(itemId))
        return false;
    ItemPosCountVec dest;
    if (bot->CanStoreNewItem(INVENTORY_SLOT_BAG_0, NULL_SLOT, dest, itemId, 1) != EQUIP_ERR_OK)
        return false;
    return bot->StoreNewItem(dest, itemId, true, 0, GuidSet(), ItemContext::NONE) != nullptr;
}

// The profession's own kit: the recipes its skill covers (the catalogue is
// the trainer/recipe list, and nothing else grants them on this build) and
// the rod its casts need. The rod is re-checked until it lands: a full bag
// must not leave the bot unable to cast.
void EnsureEnchantKit(Player* bot, BotState& st)
{
    uint16_t const skill = bot->GetSkillValue(SKILL_ENCHANTING);
    if (st.kitSkill == skill)
    {
        if (bot->HasItemCount(kArcaniteRod, 1))
            return;
        if (GrantItem(bot, kArcaniteRod))
            TC_LOG_INFO("tortoisebots", "[W7] enchant-rod bot={} item={}", bot->GetName(), kArcaniteRod);
        return;
    }
    st.kitSkill = skill;

    uint32_t learned = 0;
    for (EnchantEntry const& entry : Catalogue())
    {
        if (!TortoiseBots::prof::upkeep::EnchantRecipeGrantable(entry.reqSkill, skill) ||
            bot->HasSpell(entry.spellId))
            continue;
        SpellInfo const* info = sSpellMgr->GetSpellInfo(entry.spellId, DIFFICULTY_NONE);
        if (!info || !SpellMgr::IsSpellValid(info, bot, false))
            continue;
        bot->LearnSpell(entry.spellId, false);
        ++learned;
    }
    if (learned)
        TC_LOG_INFO("tortoisebots", "[W7] enchant-learn bot={} skill={} spells={}",
            bot->GetName(), uint32_t(skill), learned);

    if (!bot->HasItemCount(kArcaniteRod, 1) && GrantItem(bot, kArcaniteRod))
        TC_LOG_INFO("tortoisebots", "[W7] enchant-rod bot={} item={}", bot->GetName(), kArcaniteRod);
}

bool StartCast(Player* bot, BotState& st, uint32_t spellId, Item* item, bool enchant, uint32_t enchantId)
{
    if (!bot->GetSession() || !item)
        return false;
    if (bot->isMoving())
        bot->StopMoving();
    if (bot->IsMounted())
        bot->Dismount();

    PendingCast cast;
    cast.active = true;
    cast.enchant = enchant;
    cast.itemGuid = item->GetGUID();
    cast.itemEntry = item->GetEntry();
    cast.spellId = spellId;
    cast.enchantId = enchantId;
    cast.startedMs = st.nowMs;
    if (!enchant)
        cast.bagsBefore = BagCounts(bot);
    st.cast = std::move(cast);

    WorldPackets::Spells::CastSpell packet{ WorldPacket(CMSG_CAST_SPELL) };
    packet.Cast.SpellID = int32_t(spellId);
    packet.Cast.Target.Flags = TARGET_FLAG_ITEM;
    packet.Cast.Target.Item = item->GetGUID();
    bot->GetSession()->HandleCastSpellOpcode(packet);
    if (!bot->IsNonMeleeSpellCast(false))
        TC_LOG_DEBUG("tortoisebots", "[W7] upkeep-cast-queued bot={} spell={} guid={}",
            bot->GetName(), spellId, item->GetGUID().GetCounter());
    return true;
}

std::string FormatGains(std::unordered_map<uint32_t, uint32_t> const& before,
    std::unordered_map<uint32_t, uint32_t> const& after)
{
    std::string out;
    for (auto const& [itemId, count] : after)
    {
        uint32_t had = 0;
        auto const it = before.find(itemId);
        if (it != before.end())
            had = it->second;
        if (count <= had)
            continue;
        if (!out.empty())
            out += ';';
        out += std::to_string(itemId) + ':' + std::to_string(count - had);
    }
    return out.empty() ? "-" : out;
}

void NoteFailure(BotState& st, PendingCast const& cast)
{
    if (cast.enchant)
        st.failedEnchants[cast.itemGuid.GetCounter()][cast.spellId] = st.nowMs + kFailSkipMs;
    else
        st.failedItems[cast.itemGuid.GetCounter()] = st.nowMs + kFailSkipMs;
}

void FinishCast(Player* bot, BotState& st)
{
    PendingCast const cast = st.cast;
    if (bot->IsNonMeleeSpellCast(false))
    {
        if (st.nowMs - cast.startedMs < kCastTimeoutMs)
            return;
        TC_LOG_INFO("tortoisebots", "[W7] upkeep-timeout bot={} spell={} guid={}",
            bot->GetName(), cast.spellId, cast.itemGuid.GetCounter());
        NoteFailure(st, cast);
        st.cast = PendingCast{};
        return;
    }

    bool const pastGrace = st.nowMs - cast.startedMs >= kCastGraceMs;
    if (cast.enchant)
    {
        Item* item = bot->GetItemByGuid(cast.itemGuid);
        uint32_t const got = item ? item->GetEnchantmentId(PERM_ENCHANTMENT_SLOT) : 0;
        if (item && got == cast.enchantId)
        {
            TC_LOG_INFO("tortoisebots", "[W7] enchant bot={} item={} name={} guid={} spell={} enchant={}",
                bot->GetName(), item->GetEntry(), ItemName(item), cast.itemGuid.GetCounter(),
                cast.spellId, cast.enchantId);
        }
        else if (pastGrace)
        {
            TC_LOG_INFO("tortoisebots", "[W7] enchant-fail bot={} spell={} guid={} have={}",
                bot->GetName(), cast.spellId, cast.itemGuid.GetCounter(), got);
            NoteFailure(st, cast);
        }
        else
            return;
    }
    else
    {
        if (bot->GetItemByGuid(cast.itemGuid))
        {
            tbhost::guild_orders::TakeItemLoot(bot, cast.itemGuid);
            if (bot->GetItemByGuid(cast.itemGuid) && !pastGrace)
                return;
        }
        Item* item = bot->GetItemByGuid(cast.itemGuid);
        std::unordered_map<uint32_t, uint32_t> const after = BagCounts(bot);
        std::string const gains = FormatGains(cast.bagsBefore, after);
        if (!item)
        {
            TC_LOG_INFO("tortoisebots", "[W7] disenchant bot={} item={} guid={} spell={} gained={}",
                bot->GetName(), cast.itemEntry, cast.itemGuid.GetCounter(), cast.spellId, gains);
        }
        else
        {
            TC_LOG_INFO("tortoisebots", "[W7] disenchant-fail bot={} item={} guid={} spell={}",
                bot->GetName(), cast.itemEntry, cast.itemGuid.GetCounter(), cast.spellId);
            NoteFailure(st, cast);
        }
    }
    st.cast = PendingCast{};
}

Item* FindDisenchantItem(Player* bot, BotState& st, bool sharesWithRealPlayer)
{
    Item* found = nullptr;
    uint16_t const skill = bot->GetSkillValue(SKILL_ENCHANTING);
    bot->ForEachItem(ItemSearchLocation::Inventory, [&](Item* item)
    {
        ItemTemplate const* proto = item ? item->GetTemplate() : nullptr;
        if (!proto || item->IsInTrade())
            return ItemSearchCallbackResult::Continue;
        uint64_t const guidLow = item->GetGUID().GetCounter();
        auto const failed = st.failedItems.find(guidLow);
        if (failed != st.failedItems.end() && st.nowMs < failed->second)
            return ItemSearchCallbackResult::Continue;

        Optional<uint32_t> const lootId = item->GetDisenchantLootId();
        Optional<uint16> const required = item->GetDisenchantSkillRequired();
        TortoiseBots::prof::upkeep::DisenchantUpkeepFacts facts;
        facts.itemClass = proto->GetClass();
        facts.bonding = uint32_t(proto->GetBonding());
        facts.quality = proto->GetQuality();
        facts.hasLoot = lootId.has_value();
        facts.requiredSkill = required ? *required : 0;
        facts.botSkill = skill;
        facts.sharesWithRealPlayer = sharesWithRealPlayer;
        if (!TortoiseBots::prof::upkeep::DisenchantUpkeepWants(facts))
            return ItemSearchCallbackResult::Continue;
        // The store lookups behind the protection run only for an item the
        // cheap core gates already admitted.
        facts.protectedItem = ProtectedItem(bot, item->GetEntry());
        if (!TortoiseBots::prof::upkeep::DisenchantUpkeepWants(facts))
            return ItemSearchCallbackResult::Continue;
        found = item;
        return ItemSearchCallbackResult::Stop;
    });
    return found;
}

struct EnchantChoice
{
    Item* item = nullptr;
    EnchantEntry const* entry = nullptr;
    uint32_t enchantId = 0;
    uint32_t currentWeight = 0;
    uint32_t newWeight = 0;
};

// Best weight gain among the known enchants whose reagents the bot holds and
// whose fit admits an equipped item. `0` gain means nothing to do.
EnchantChoice FindEnchant(Player* bot, BotState& st)
{
    EnchantChoice best;

    // Slots the level-60 spec table owns are off limits for this scan: the
    // bot wears the common player choice there, or deliberately none.
    bool specGoverned[EQUIPMENT_SLOT_END] = {};
    for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
        if (Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
            specGoverned[slot] = SpecEnchantGoverns(bot, item);

    for (EnchantEntry const& entry : Catalogue())
    {
        if (entry.reagents.empty() || !bot->HasSpell(entry.spellId))
            continue;
        SpellInfo const* spell = sSpellMgr->GetSpellInfo(entry.spellId, DIFFICULTY_NONE);
        if (!spell)
            continue;
        uint32_t const enchantId = EnchantIdOfSpell(entry.spellId);
        uint32_t const newWeight = EnchantWeightFor(bot, enchantId);
        if (!enchantId || !newWeight)
            continue;

        Item* bestItem = nullptr;
        uint32_t bestCurrent = 0;
        for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
        {
            Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
            ItemTemplate const* proto = item ? item->GetTemplate() : nullptr;
            if (!proto || item->IsInTrade() || specGoverned[slot])
                continue;
            auto const itemFailed = st.failedEnchants.find(item->GetGUID().GetCounter());
            if (itemFailed != st.failedEnchants.end())
            {
                auto const spellFailed = itemFailed->second.find(entry.spellId);
                if (spellFailed != itemFailed->second.end() && st.nowMs < spellFailed->second)
                    continue;
            }
            if (!item->IsFitToSpellRequirements(spell))
                continue;
            uint32_t const current = EnchantWeightFor(bot, item->GetEnchantmentId(PERM_ENCHANTMENT_SLOT));
            if (newWeight <= current)
                continue;
            if (!bestItem || current < bestCurrent)
            {
                bestItem = item;
                bestCurrent = current;
            }
        }
        if (!bestItem)
            continue;

        TortoiseBots::prof::upkeep::EnchantUpkeepFacts facts;
        facts.spellTakesReagents = true;
        facts.hasReagents = true;
        for (auto const& [itemId, count] : entry.reagents)
        {
            if (ProtectedItem(bot, itemId))
                facts.protectedReagents = true;
            if (!bot->HasItemCount(itemId, count))
                facts.hasReagents = false;
        }
        for (uint32_t tool : entry.tools)
            if (tool && !bot->HasItemCount(tool, 1))
                facts.hasReagents = false;
        facts.currentWeight = bestCurrent;
        facts.newWeight = newWeight;
        if (!TortoiseBots::prof::upkeep::EnchantUpkeepWants(facts))
            continue;

        uint32_t const gain = newWeight - bestCurrent;
        if (!best.item || gain > best.newWeight - best.currentWeight)
            best = EnchantChoice{ bestItem, &entry, enchantId, bestCurrent, newWeight };
    }
    return best;
}

void TryDisenchant(Player* bot, BotState& st)
{
    st.nextDisenchantMs = st.nowMs + kDisenchantCooldownMs;
    if (!bot->HasSkill(SKILL_ENCHANTING))
        return;
    if (!bot->HasSpell(kSpellDisenchant) && !st.grantedDisenchant)
    {
        st.grantedDisenchant = true;
        bot->LearnSpell(kSpellDisenchant, false);
    }
    if (!bot->HasSpell(kSpellDisenchant))
        return;
    // The cast's whole point is the materials; with no room the loot window
    // would stay open and the item would never be consumed.
    if (bot->GetFreeInventorySlotCount() == 0)
        return;

    Item* item = FindDisenchantItem(bot, st, SharesWithRealPlayer(bot));
    if (!item)
        return;
    StartCast(bot, st, kSpellDisenchant, item, false, 0);
}

void TryEnchant(Player* bot, BotState& st)
{
    st.nextEnchantMs = st.nowMs + kEnchantCooldownMs;
    if (!bot->HasSkill(SKILL_ENCHANTING))
        return;
    EnsureEnchantKit(bot, st);

    EnchantChoice const choice = FindEnchant(bot, st);
    if (!choice.item || !choice.entry)
        return;
    TC_LOG_INFO("tortoisebots", "[W7] enchant-start bot={} item={} name={} guid={} spell={} enchant={} slot={} weight={}->{}",
        bot->GetName(), choice.item->GetEntry(), ItemName(choice.item),
        choice.item->GetGUID().GetCounter(), choice.entry->spellId, choice.enchantId,
        TortoiseBots::GuildOrders::EnchantSlotToken(choice.entry->slot),
        choice.currentWeight, choice.newWeight);
    StartCast(bot, st, choice.entry->spellId, choice.item, true, choice.enchantId);
}

} // namespace

void TickProfessionUpkeep(Player* bot, uint32_t diffMs, bool disenchant, bool enchantGear)
{
    if (!bot || !bot->IsInWorld() || !bot->FindMap() || bot->IsBeingTeleported())
        return;
    BotState& st = States()[bot->GetGUID().GetCounter()];
    st.nowMs += diffMs;

    if (st.cast.active)
    {
        FinishCast(bot, st);
        return;
    }
    if (!bot->IsAlive() || bot->IsInCombat() || bot->GetTrader() || bot->IsNonMeleeSpellCast(false))
        return;
    // A mounted bot cannot finish either cast; it dismounts on its own route.
    if (bot->IsMounted() || OtherErrand(bot))
        return;

    if (disenchant && st.nowMs >= st.nextDisenchantMs)
    {
        TryDisenchant(bot, st);
        if (st.cast.active)
            return;
    }
    if (enchantGear && st.nowMs >= st.nextEnchantMs)
        TryEnchant(bot, st);
}

bool HasActiveUpkeepCast(Player* bot)
{
    if (!bot)
        return false;
    auto const found = States().find(bot->GetGUID().GetCounter());
    return found != States().end() && found->second.cast.active;
}

void ForgetProfessionUpkeep(uint32_t botGuidLow)
{
    States().erase(botGuidLow);
}

} // namespace upkeep
} // namespace tbhost
