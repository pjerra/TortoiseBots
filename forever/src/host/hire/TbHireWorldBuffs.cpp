// World buffs at the six capital recruiters (TbHire.h package P5): the page, the purchase and the unlock hooks.
//
// Model: earn the buff once the real way, turn in its unlock quest at a capital recruiter for a fee, then buy
// it again for gold. The purchase applies the original aura with the recruiter as caster, so the aura-credit script can
// tell a bought blessing from an earned one by the caster guid. Everything core-facing sits behind TORTOISEBOTS_SLICE1;
// the default build answers the contract with "not visible" and registers nothing.
#include "host/hire/TbHireWorldBuffs.h"
#include "host/hire/TbHire.h"

#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1

#include "host/hire/TbHireWorldBuffsPure.h"

#include "Chat.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "GossipDef.h"
#include "Group.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "ObjectGuid.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "QuestDef.h"
#include "ScriptMgr.h"
#include "SpellAuras.h"
#include "SpellScript.h"
#include "WorldSession.h"

#include "compat/botpch_forever.h"
#include "playerbot/WorldPosition.h"
#include "playerbot/ChatHelper.h"
#include "playerbot/PlayerbotAI.h"
#include "playerbot/PlayerbotAIConfig.h"

#include <algorithm>
#include <cstdint>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace TortoiseBots::hire
{
namespace
{
// One option row of a script-built menu. The icon value is the classic client's icon index (6 is TB's money bag);
// order index -1 lets GossipMenu assign the next free one, which the AI turns back into sender/action.
void AddItem(Player* player, GossipOptionNpc icon, std::string const& text, uint32 sender, uint32 action)
{
    player->PlayerTalkClass->GetGossipMenu().AddMenuItem(0, -1, icon, text, LANG_UNIVERSAL,
        GossipOptionFlags::None, Optional<int32>(), 0, 0, false, 0, "", Optional<int32>(), Optional<int32>(),
        sender, action);
}

// Each recruiter ships its own greeting as npc_text with id = entry (entry + 100 without the PvE-only line).
uint32 GreetingText(Creature* creature)
{
    uint32 entry = creature ? creature->GetEntry() : 0;
    if (!worldbuffs::HireRecruiterEntry(entry))
        return DEFAULT_GOSSIP_MESSAGE;
    return sPlayerbotAIConfig.hireAllowPvP ? entry + 100 : entry;
}

void CloseWithHint(Player* player, std::string const& hint)
{
    player->PlayerTalkClass->SendCloseGossip();
    if (!hint.empty())
        ChatHandler(player->GetSession()).PSendSysMessage("%s", hint.c_str());
}

bool IsBotPlayer(Player* player)
{
    WorldSession* session = player ? player->GetSession() : nullptr;
    return !session || session->IsBot();
}

bool FeatureEnabled()
{
    return sPlayerbotAIConfig.worldBuffsEnabled;
}

bool CurrentPrices(uint8 purchase, WorldBuffPricePair& prices)
{
    return WorldBuffPrices(purchase, prices,
        sPlayerbotAIConfig.worldBuffsPriceBaseCopper,
        sPlayerbotAIConfig.worldBuffsPricePerPersonCopper,
        sPlayerbotAIConfig.worldBuffsSaygePriceBaseCopper,
        sPlayerbotAIConfig.worldBuffsSaygePricePerPersonCopper,
        sPlayerbotAIConfig.worldBuffsSongflowerPriceBaseCopper,
        sPlayerbotAIConfig.worldBuffsSongflowerPricePerPersonCopper,
        sPlayerbotAIConfig.worldBuffsSilithystPriceBaseCopper,
        sPlayerbotAIConfig.worldBuffsSilithystPricePerPersonCopper);
}

// Buyer + live, alive group/raid members within 40 yd of the NPC. Pool bots standing nearby are not in the group and
// get nothing; hired companions are, so they are buffed with the buyer.
void CollectBuffTargets(Player* buyer, Creature* npc, std::vector<Player*>& out)
{
    out.clear();
    if (!buyer || !npc)
        return;
    out.push_back(buyer);
    Group* group = buyer->GetGroup();
    if (!group)
        return;
    for (auto const& slot : group->GetMemberSlots())
    {
        if (slot.guid == buyer->GetGUID())
            continue;
        Player* member = ObjectAccessor::FindPlayer(slot.guid);
        if (!member || !member->IsInWorld() || !member->IsAlive())
            continue;
        if (!npc->IsWithinDistInMap(member, 40.0f))
            continue;
        out.push_back(member);
    }
}

void FilterQuestMenu(Player* player, Creature* creature, uint32 first, uint32 last)
{
    // PrepareQuestMenu merges every quest the NPC starts or takes; keep only this page's range (with their icons), so
    // the world-buff page never shows a contract quest and vice versa.
    player->PrepareQuestMenu(creature->GetGUID());
    QuestMenu& menu = player->PlayerTalkClass->GetQuestMenu();
    std::vector<std::pair<uint32, uint8>> kept;
    for (uint8 i = 0; i < menu.GetMenuItemCount(); ++i)
    {
        QuestMenuItem const& item = menu.GetItem(i);
        if (item.QuestId >= first && item.QuestId <= last)
            kept.push_back(std::make_pair(item.QuestId, item.QuestIcon));
    }
    menu.ClearMenu();
    for (auto const& keep : kept)
        menu.AddMenuItem(keep.first, keep.second);
}

void ShowBuyConfirm(Player* player, Creature* creature, uint32 buyAction);

void ShowSaygeMenu(Player* player, Creature* creature)
{
    uint32 count = 0;
    WorldBuffSaygePick const* picks = WorldBuffSaygePicks(count);
    for (uint32 i = 0; i < count; ++i)
        AddItem(player, GossipOptionNpc::Banker, picks[i].label, worldbuffs::kSenderBuy,
            EncodeSaygeAction(kWorldBuffBuySayge, i + 1));
    AddItem(player, GossipOptionNpc::None, "< Back", worldbuffs::kSenderBack, worldbuffs::kSenderSayge);
    player->PlayerTalkClass->SendGossipMenu(GreetingText(creature), creature->GetGUID());
}

// The price shown is the one the purchase would charge right now; the quoted head count rides in the confirm action, so
// a group that changed in between is re-quoted instead of charged a price the player never saw.
void ShowBuyConfirm(Player* player, Creature* creature, uint32 buyAction)
{
    uint8 purchase = DecodeBuyIndex(buyAction);
    uint32 saygePick = DecodeSaygePick(buyAction);
    WorldBuffPricePair prices;
    char const* name = worldbuffs::PurchaseName(purchase, player->GetTeam() == HORDE);
    if (!name || !CurrentPrices(purchase, prices))
    {
        ShowWorldBuffMenu(player, creature);
        return;
    }
    std::vector<Player*> targets;
    CollectBuffTargets(player, creature, targets);
    uint32 people = static_cast<uint32>(targets.size());
    uint32 total = WorldBuffTotalPrice(prices.baseCopper, prices.perPersonCopper, people);
    std::ostringstream label;
    label << "Pay " << ai::ChatHelper::formatMoney(total) << " for " << name;
    if (purchase == kWorldBuffBuySayge)
    {
        uint32 count = 0;
        WorldBuffSaygePick const* picks = WorldBuffSaygePicks(count);
        if (saygePick >= 1 && saygePick <= count)
            label << " of " << picks[saygePick - 1].label;
    }
    if (people > 1)
        label << " on you and " << (people - 1) << (people == 2 ? " group member" : " group members") << " nearby";
    else
        label << " on you";
    label << ". Confirm?";
    AddItem(player, GossipOptionNpc::Banker, label.str(), worldbuffs::kSenderConfirm,
        EncodeConfirmAction(buyAction, people));
    AddItem(player, GossipOptionNpc::None, "< Back", worldbuffs::kSenderBack, worldbuffs::kSenderSayge);
    player->PlayerTalkClass->SendGossipMenu(GreetingText(creature), creature->GetGUID());
}

// Charge first, then apply; refund only when nobody received anything. Auras (not casts) with the recruiter as caster:
// duration and death rules stay the spell's own, and the aura-credit script ignores recruiter casters.
void BuyWorldBuff(Player* player, Creature* creature, uint32 confirmAction)
{
    uint32 buyAction = DecodeConfirmBuyAction(confirmAction);
    uint8 purchase = DecodeBuyIndex(buyAction);
    uint32 saygePick = DecodeSaygePick(buyAction);
    if (!IsValidWorldBuffPurchase(purchase))
    {
        ShowWorldBuffMenu(player, creature);
        return;
    }
    bool horde = player->GetTeam() == HORDE;
    uint32 questId = worldbuffs::UnlockQuestFor(purchase, horde);
    if (!questId || !player->GetQuestRewardStatus(questId))
    {
        CloseWithHint(player, "That blessing is not unlocked yet. Complete its unlock quest first.");
        return;
    }
    uint32 saygeSpell = 0;
    if (purchase == kWorldBuffBuySayge)
    {
        uint32 count = 0;
        WorldBuffSaygePick const* picks = WorldBuffSaygePicks(count);
        if (saygePick < 1 || saygePick > count)
        {
            ShowSaygeMenu(player, creature);
            return;
        }
        saygeSpell = picks[saygePick - 1].spellId;
    }
    std::vector<uint32> apply;
    std::vector<uint32> strip;
    if (!worldbuffs::PurchaseSpells(purchase, saygeSpell, horde, apply, strip))
    {
        ShowWorldBuffMenu(player, creature);
        return;
    }
    WorldBuffPricePair prices;
    if (!CurrentPrices(purchase, prices))
    {
        ShowWorldBuffMenu(player, creature);
        return;
    }
    std::vector<Player*> targets;
    CollectBuffTargets(player, creature, targets);
    if (targets.size() != DecodeConfirmCount(confirmAction))
    {
        ShowBuyConfirm(player, creature, buyAction);
        return;
    }
    uint32 total = WorldBuffTotalPrice(prices.baseCopper, prices.perPersonCopper,
        static_cast<uint32>(targets.size()));
    if (player->GetMoney() < total)
    {
        CloseWithHint(player, "You cannot afford that blessing (" + ai::ChatHelper::formatMoney(total) + ").");
        return;
    }
    player->ModifyMoney(-static_cast<int64>(total));
    player->SaveToDB();

    bool anyBuffed = false;
    for (Player* target : targets)
    {
        if (!target || !target->IsInWorld() || !target->IsAlive())
            continue;
        for (uint32 stripId : strip)
            target->RemoveAurasDueToSpell(stripId);
        for (uint32 spellId : apply)
            if (creature->AddAura(spellId, target))
                anyBuffed = true;
    }
    if (!anyBuffed)
    {
        player->ModifyMoney(static_cast<int64>(total));
        player->SaveToDB();
        CloseWithHint(player, "The blessing could not be applied. Your gold has been refunded.");
        return;
    }
    player->PlayerTalkClass->SendCloseGossip();
    ChatHandler(player->GetSession()).PSendSysMessage("The recruiter's criers raise the blessing over your company.");
}
} // namespace

bool WorldBuffsVisible(Player* player, Creature* recruiter)
{
    if (!player || !recruiter || !worldbuffs::CapitalRecruiter(recruiter->GetEntry()))
        return false;
    return ShouldShowWorldBuffs(FeatureEnabled(), IsBotPlayer(player), player->GetLevel(),
        sPlayerbotAIConfig.worldBuffsMinLevel);
}

void ShowWorldBuffMenu(Player* player, Creature* recruiter)
{
    if (!WorldBuffsVisible(player, recruiter))
    {
        if (player)
            player->PlayerTalkClass->SendCloseGossip();
        return;
    }
    player->PlayerTalkClass->ClearMenus();
    bool horde = player->GetTeam() == HORDE;
    std::vector<Player*> targets;
    CollectBuffTargets(player, recruiter, targets);
    uint32 people = static_cast<uint32>(targets.size());
    for (uint8 purchase = kWorldBuffBuyRally; purchase <= kWorldBuffBuyCount; ++purchase)
    {
        uint32 questId = worldbuffs::UnlockQuestFor(purchase, horde);
        if (!questId || !player->GetQuestRewardStatus(questId))
            continue;
        WorldBuffPricePair prices;
        char const* name = worldbuffs::PurchaseName(purchase, horde);
        if (!name || !CurrentPrices(purchase, prices))
            continue;
        uint32 total = WorldBuffTotalPrice(prices.baseCopper, prices.perPersonCopper, people);
        std::string label = std::string(name) + (purchase == kWorldBuffBuySayge ? "..." : "") +
            " (" + ai::ChatHelper::formatMoney(total) + ")";
        if (purchase == kWorldBuffBuySayge)
            AddItem(player, GossipOptionNpc::Banker, label, worldbuffs::kSenderSayge, 0);
        else
            AddItem(player, GossipOptionNpc::Banker, label, worldbuffs::kSenderBuy, purchase);
    }
    AddItem(player, GossipOptionNpc::None, "< Back", worldbuffs::kSenderBack, worldbuffs::kSenderBack);
    FilterQuestMenu(player, recruiter, worldbuffs::kQuestFirst, worldbuffs::kQuestLast);
    player->PlayerTalkClass->SendGossipMenu(GreetingText(recruiter), recruiter->GetGUID());
}

bool OnWorldBuffSelect(Player* player, Creature* recruiter, uint32 sender, uint32 action)
{
    if (!player || !recruiter)
        return false;
    if (sender < worldbuffs::kSenderRoot || sender > worldbuffs::kSenderConfirm)
        return false;
    // A stale menu must not bypass a flag flip, the level gate or the bot check.
    if (!WorldBuffsVisible(player, recruiter))
    {
        CloseWithHint(player, "The recruiter is not offering blessings right now.");
        return true;
    }
    player->PlayerTalkClass->ClearMenus();
    switch (sender)
    {
        case worldbuffs::kSenderRoot:
            if (action == worldbuffs::kRootActionMainMenu)
                recruiter->AI()->OnGossipHello(player);
            else
                ShowWorldBuffMenu(player, recruiter);
            return true;
        case worldbuffs::kSenderSayge:
            if (action == 0)
                ShowSaygeMenu(player, recruiter);
            else
                ShowWorldBuffMenu(player, recruiter);
            return true;
        case worldbuffs::kSenderBuy:
        {
            uint8 purchase = DecodeBuyIndex(action);
            uint32 saygePick = DecodeSaygePick(action);
            if (purchase == kWorldBuffBuySayge && saygePick == 0)
                ShowSaygeMenu(player, recruiter);
            else
                ShowBuyConfirm(player, recruiter, action);
            return true;
        }
        case worldbuffs::kSenderConfirm:
            BuyWorldBuff(player, recruiter, action);
            return true;
        case worldbuffs::kSenderBack:
            if (action == worldbuffs::kSenderSayge)
                ShowWorldBuffMenu(player, recruiter);
            else
                recruiter->AI()->OnGossipHello(player);
            return true;
        default:
            return false;
    }
}

namespace worldbuffs
{
namespace
{
// Earned-through-play aura gains (Dire Maul tribute, Sayge, Songflower) complete the unlock quest of the receiver only.
class WorldBuffAuraScript : public AuraScript
{
    PrepareAuraScript(WorldBuffAuraScript);

    void OnApply(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        if (!sPlayerbotAIConfig.worldBuffsEnabled)
            return;
        Aura* aura = GetAura();
        Unit* target = GetTarget();
        if (!aura || !target || !target->IsPlayer())
            return;
        uint32 spellId = aura->GetId();
        Player* player = target->ToPlayer();
        uint32 questId = AuraQuestFor(spellId, player->GetTeam() == HORDE);
        if (!questId)
            return;
        ObjectGuid const casterGuid = aura->GetCasterGUID();
        bool const inRaid = player->GetMap() && player->GetMap()->IsRaid();
        if (!ShouldCreditAura(sPlayerbotAIConfig.worldBuffsEnabled,
                CasterIsRecruiter(casterGuid.IsCreature(), casterGuid.GetEntry()), inRaid,
                player->GetQuestStatus(questId) == QUEST_STATUS_INCOMPLETE,
                player->GetQuestRewardStatus(questId)))
            return;
        player->AreaExploredOrEventHappens(questId);
    }

public:
    void Register() override
    {
        AfterEffectApply += AuraEffectApplyFn(WorldBuffAuraScript::OnApply, EFFECT_0, SPELL_AURA_ANY,
            AURA_EFFECT_HANDLE_REAL);
    }
};

class WorldBuffAuraLoader : public SpellScriptLoader
{
public:
    WorldBuffAuraLoader() : SpellScriptLoader("tortoisebots_hire_worldbuff_aura") { }
    AuraScript* GetAuraScript() const override { return new WorldBuffAuraScript(); }
};

// The unlock fee is a negative classic RewardMoney row: applying it from the live config keeps one knob for both
// 200-gold raid unlocks and 100-gold rest, the way the Turtle build rewrites its quest rows at start.
void ApplyUnlockFees()
{
    uint32 const raidFee = sPlayerbotAIConfig.worldBuffsUnlockFeeRaidCopper;
    uint32 const restFee = sPlayerbotAIConfig.worldBuffsUnlockFeeCopper;
    for (uint8 purchase = kWorldBuffBuyRally; purchase <= kWorldBuffBuyCount; ++purchase)
    {
        uint32 const fee = purchase <= kWorldBuffBuyZandalar ? raidFee : restFee;
        for (bool horde : { false, true })
        {
            uint32 const questId = UnlockQuestFor(purchase, horde);
            if (Quest* quest = const_cast<Quest*>(sObjectMgr.GetQuestTemplate(questId)))
                quest->SetClassicMoney(-static_cast<int32>(fee), 0);
        }
    }
}

class WorldBuffFees : public WorldScript
{
public:
    WorldBuffFees() : WorldScript("tortoisebots_hire_worldbuff_fees") { }

    void OnWorldInitialized() override { ApplyUnlockFees(); }
    void OnConfigLoad(bool reload) override
    {
        if (reload)
            ApplyUnlockFees();
    }
};

class WorldBuffHooks : public PlayerScript
{
public:
    WorldBuffHooks() : PlayerScript("tortoisebots_hire_worldbuffs") { }

    void OnCreatureKill(Player* killer, Creature* killed) override
    {
        if (!killer || !killed || !sPlayerbotAIConfig.worldBuffsEnabled)
            return;
        uint32 credit = BossKillCredit(killed->GetEntry());
        if (!credit)
            return;
        killer->RewardPlayerAndGroupAtEvent(credit, killed);
    }

    void OnPVPKill(Player* killer, Player* killed) override
    {
        if (!killer || !killed || !sPlayerbotAIConfig.worldBuffsEnabled)
            return;
        if (killer->GetZoneId() != kZoneSilithus || killer->GetTeam() == killed->GetTeam())
            return;
        killer->RewardPlayerAndGroupAtEvent(kCreditSilithyst, killed);
    }

    void OnMapChanged(Player* player) override
    {
        if (!player || !player->GetMap())
            return;
        if (!ShouldStripOnRaidEntry(sPlayerbotAIConfig.worldBuffsEnabled,
                sPlayerbotAIConfig.worldBuffsKeepInRaids, player->GetMap()->IsRaid()))
            return;
        uint32 count = 0;
        for (uint32 const* spell = RaidStripSpells(count); count > 0; --count, ++spell)
            player->RemoveAurasDueToSpell(*spell);
    }
};
} // namespace

void AddScripts()
{
    new WorldBuffFees();
    new WorldBuffHooks();
    new WorldBuffAuraLoader();
}
} // namespace worldbuffs
} // namespace TortoiseBots::hire

#else

namespace TortoiseBots::hire
{
bool WorldBuffsVisible(Player* /*player*/, Creature* /*recruiter*/)
{
    return false;
}

void ShowWorldBuffMenu(Player* /*player*/, Creature* /*recruiter*/)
{
}

bool OnWorldBuffSelect(Player* /*player*/, Creature* /*recruiter*/, uint32 /*sender*/, uint32 /*action*/)
{
    return false;
}

namespace worldbuffs
{
void AddScripts()
{
}
} // namespace worldbuffs
} // namespace TortoiseBots::hire

#endif
