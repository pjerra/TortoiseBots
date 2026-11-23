// The `professions` addon verb (TBF #134 piece P134-D): mirrors TB's `.bot professions <bot>`
// (TortoiseBots/commands/Professions.cpp) over the TBM addon channel.
#include "host/TbAddonProfessions.h"
#include "host/TbAddonProfessionsPure.h"

#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1

#include "host/TbAddonChannel.h"
#include "host/TbAddonPure.h"
#include "host/cmd/TbCmdBinding.h"
#include "host/player/TbPlayerAuthPure.h"
#include "host/run/TbRunHooks.h"
#include "TbBotMgr.h"
#include "PlayerbotAIStorage.h"

#include "runtime/GuildOrderCraftPolicy.h"
#include "runtime/GuildOrderEnchantPolicy.h"

#include "BattlenetAccountMgr.h"
#include "CharacterCache.h"
#include "DB2Stores.h"
#include "Group.h"
#include "ItemTemplate.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "RBAC.h"
#include "SharedDefines.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Trainer.h"
#include "WorldSession.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace TortoiseBots
{
namespace professions
{
namespace
{

using ProfessionSnapshotPolicy::AbilityRow;
using ProfessionSnapshotPolicy::IsKnownRecipe;
using ProfessionSnapshotPolicy::IsMissingRecipe;
using ProfessionSnapshotPolicy::IsProfessionSkill;
using ProfessionSnapshotPolicy::MissingRow;
using ProfessionSnapshotPolicy::ProfessionRow;

std::uint32_t CallerBnet(Player* requester)
{
    WorldSession* session = requester ? requester->GetSession() : nullptr;
    if (!session)
        return 0;
    std::uint32_t const bnet = session->GetBattlenetAccountId();
    if (!bnet || Battlenet::AccountMgr::GetIdByGameAccount(session->GetAccountId()) != bnet)
        return 0;
    return bnet;
}

player::BotFacts FactsFor(Player* requester, Player* bot, std::uint32_t bnet)
{
    player::BotFacts facts;
    if (!bot)
        return facts;
    ObjectGuid const guid = bot->GetGUID();
    facts.isBot = BotMgr::Instance().IsBot(guid);
    std::uint32_t const gameAccount = sCharacterCache->GetCharacterAccountIdByGuid(guid);
    if (bnet && gameAccount && Battlenet::AccountMgr::GetIdByGameAccount(gameAccount) == bnet)
        facts.account = bnet;
    facts.master = BotMgr::Instance().MasterOf(guid).GetCounter();
    facts.commandedByCaller = requester && cmd::IsCommandedBy(requester, bot);
    if (Group* group = bot->GetGroup())
    {
        ObjectGuid const leader = group->GetLeaderGUID();
        facts.groupLeader = leader.GetCounter();
        facts.leaderIsBot = BotMgr::Instance().IsBot(leader);
    }
    facts.inRun = run::BotInRunningRun(bot);
    return facts;
}

// One scan of SkillLineAbility indexed by the top-level classic profession skill ID.
std::unordered_map<uint32_t, std::vector<SkillLineAbilityEntry const*>> const& AbilityBySkill()
{
    static std::unordered_map<uint32_t, std::vector<SkillLineAbilityEntry const*>> const index = []
    {
        std::unordered_map<uint32_t, std::vector<SkillLineAbilityEntry const*>> out;
        for (SkillLineAbilityEntry const* ability : sSkillLineAbilityStore)
        {
            if (!ability || !ability->Spell || !ability->SkillLine)
                continue;
            uint32_t const skillId = Player::GetClassicProfessionSkill(ability->SkillLine);
            out[skillId].push_back(ability);
        }
        return out;
    }();
    return index;
}

// Lowest-entry recipe item whose use spell teaches each recipe spell.
std::unordered_map<uint32_t, uint32_t> const& RecipeItemBySpell()
{
    static std::unordered_map<uint32_t, uint32_t> const index = []
    {
        std::unordered_map<uint32_t, uint32_t> out;
        for (auto const& [entry, tpl] : sObjectMgr->GetItemTemplateStore())
        {
            if (tpl.GetClass() != ITEM_CLASS_RECIPE)
                continue;
            for (ItemEffectEntry const* eff : tpl.Effects)
            {
                if (!eff || eff->SpellID <= 0)
                    continue;
                SpellInfo const* use = sSpellMgr->GetSpellInfo(uint32(eff->SpellID), DIFFICULTY_NONE);
                if (!use)
                    continue;
                for (SpellEffectInfo const& se : use->GetEffects())
                {
                    if (se.Effect != SPELL_EFFECT_LEARN_SPELL || se.TriggerSpell == 0)
                        continue;
                    auto [it, inserted] = out.emplace(se.TriggerSpell, entry);
                    if (!inserted && entry < it->second)
                        it->second = entry;
                }
            }
        }
        return out;
    }();
    return index;
}

// Lowest required skill among tradeskill trainers teaching each recipe spell.
std::unordered_map<uint32_t, uint32_t> const& TrainerMinSkillBySpell()
{
    static std::unordered_map<uint32_t, uint32_t> const index = []
    {
        std::unordered_map<uint32_t, uint32_t> out;
        auto record = [&](uint32_t spellId, uint32_t reqSkill)
        {
            if (!spellId)
                return;
            auto [it, inserted] = out.emplace(spellId, reqSkill);
            if (!inserted && reqSkill < it->second)
                it->second = reqSkill;
        };
        for (auto const& [id, trainer] : sObjectMgr->GetTrainers())
        {
            if (trainer.GetType() != Trainer::Type::Tradeskill)
                continue;
            for (Trainer::Spell const& tSpell : trainer.GetSpells())
            {
                if (!tSpell.SpellId)
                    continue;
                record(tSpell.SpellId, tSpell.ReqSkillRank);
                if (SpellInfo const* teaching = sSpellMgr->GetSpellInfo(tSpell.SpellId, DIFFICULTY_NONE))
                {
                    for (SpellEffectInfo const& se : teaching->GetEffects())
                    {
                        if (se.Effect == SPELL_EFFECT_LEARN_SPELL && se.TriggerSpell > 0)
                            record(se.TriggerSpell, tSpell.ReqSkillRank);
                    }
                }
            }
        }
        return out;
    }();
    return index;
}

void FillAbilityRow(AbilityRow& row, uint32_t skillId, SkillLineAbilityEntry const* ability, Player const* bot)
{
    row.skillId = skillId;
    row.spellId = uint32_t(ability->Spell);
    row.reqSkillValue = ability->MinSkillLineRank > 0 ? uint32_t(ability->MinSkillLineRank) : 0;
    row.minValue = ability->TrivialSkillLineRankLow > 0 ? uint32_t(ability->TrivialSkillLineRankLow) : 0;
    row.maxValue = ability->TrivialSkillLineRankHigh > 0 ? uint32_t(ability->TrivialSkillLineRankHigh) : 0;
    row.known = bot->HasSpell(row.spellId);
    row.learnOnGetSkill =
        ability->GetAcquireMethod() == SkillLineAbilityAcquireMethod::AutomaticSkillRank ||
        ability->GetAcquireMethod() == SkillLineAbilityAcquireMethod::AutomaticCharLevel;

    SpellInfo const* spell = sSpellMgr->GetSpellInfo(row.spellId, DIFFICULTY_NONE);
    if (!spell)
        return;
    row.passive = spell->IsPassive();
    auto const& effects = spell->GetEffects();
    row.effect0 = effects.empty() ? 0 : uint32_t(effects[0].Effect);
    row.isSkillStep = (effects.size() > 1 && effects[1].Effect == SPELL_EFFECT_SKILL_STEP);
    for (SpellEffectInfo const& eff : effects)
    {
        if (eff.Effect == SPELL_EFFECT_CREATE_ITEM && eff.ItemType > 0)
        {
            row.createsItem = true;
            if (!row.createdItemId)
                row.createdItemId = uint32_t(eff.ItemType);
        }
        if (eff.Effect == SPELL_EFFECT_ENCHANT_ITEM ||
            eff.Effect == SPELL_EFFECT_ENCHANT_ITEM_TEMPORARY)
            row.isEnchant = true;
    }
}

std::string CraftOrderField(AbilityRow const& row)
{
    GuildOrders::CraftOrderableInputs inputs;
    inputs.known = row.known;
    inputs.createsItem = row.createsItem;
    if (ItemTemplate const* proto = sObjectMgr->GetItemTemplate(row.createdItemId))
    {
        char const* itemName = proto->GetName(LOCALE_enUS);
        inputs.itemOrderable = itemName && *itemName &&
            proto->GetBonding() != BIND_ON_ACQUIRE &&
            proto->GetBonding() != BIND_QUEST &&
            proto->GetClass() != ITEM_CLASS_QUEST &&
            proto->GetDuration() == 0;
        inputs.itemNeedsProfession = proto->GetRequiredSkill() != 0;
    }
    if (SpellInfo const* spell = sSpellMgr->GetSpellInfo(row.spellId, DIFFICULTY_NONE))
        inputs.spellRecoveryMs = spell->GetRecoveryTime();
    return GuildOrders::CraftOrderToken(GuildOrders::CraftOrderableVerdict(inputs));
}

void SendProfessionSnapshot(Player* requester, Player* bot)
{
    std::vector<ProfessionRow> profs;
    std::unordered_set<uint32_t> professionSkills;
    std::unordered_map<uint32_t, uint32_t> skillMax;
    for (SkillLineEntry const* skill : sSkillLineStore)
    {
        if (!skill || skill->ParentSkillLineID != 0 ||
            !IsProfessionSkill(uint32_t(skill->CategoryID), skill->ID))
            continue;
        if (!bot->HasSkill(skill->ID))
            continue;
        professionSkills.insert(skill->ID);
        uint32_t const val = bot->GetSkillValue(skill->ID);
        uint32_t const maxVal = bot->GetMaxSkillValue(skill->ID);
        profs.push_back({ skill->ID, val, maxVal });
        skillMax[skill->ID] = maxVal;
    }

    std::vector<RecipeSnapshot> recipes;
    std::vector<MissingRow> missing;
    auto const& bySkill = AbilityBySkill();
    auto const& recipeItems = RecipeItemBySpell();
    auto const& trainerSpells = TrainerMinSkillBySpell();

    for (uint32_t skillId : professionSkills)
    {
        auto found = bySkill.find(skillId);
        if (found == bySkill.end())
            continue;
        for (SkillLineAbilityEntry const* ability : found->second)
        {
            AbilityRow row;
            FillAbilityRow(row, skillId, ability, bot);
            if (IsKnownRecipe(row))
            {
                recipes.push_back({ row, CraftOrderField(row) });
                continue;
            }
            auto itemIt = recipeItems.find(row.spellId);
            uint32_t const recipeItem = itemIt != recipeItems.end() ? itemIt->second : 0;
            auto trainerIt = trainerSpells.find(row.spellId);
            bool const fromTrainer = trainerIt != trainerSpells.end();
            uint32_t const trainerSkill = fromTrainer ? trainerIt->second : 0;
            ItemTemplate const* recipeProto = recipeItem ? sObjectMgr->GetItemTemplate(recipeItem) : nullptr;
            uint32_t const itemSkill = recipeProto ? recipeProto->GetRequiredSkillRank() : 0;
            row.reqSkillValue = GuildOrders::EnchantReqSkillCombine(
                fromTrainer, trainerSkill, recipeProto != nullptr, itemSkill);
            if (!IsMissingRecipe(row, skillMax[skillId], fromTrainer, recipeItem))
                continue;
            missing.push_back({ row.skillId, row.spellId, row.createdItemId,
                row.reqSkillValue, recipeItem });
        }
    }

    for (std::string const& line : BuildSnapshotLines(bot->GetName(), profs, recipes, missing))
        addon::Send(requester, line);
}

void HandleProfessionsVerb(Player* requester, std::string_view args)
{
    if (!requester || !requester->IsInWorld())
    {
        addon::Err(requester, "professions", "requester", "Requester must be in world.");
        return;
    }

    ParsedProfessions const parsed = ParseProfessionsArgs(args);
    if (!parsed.ok)
    {
        addon::Err(requester, "professions", "no-bot", kProfessionsUsage);
        return;
    }

    Player* found = nullptr;
    for (ObjectGuid const& guid : BotMgr::Instance().BotGuids())
    {
        Player* candidate = ObjectAccessor::FindPlayer(guid);
        if (!candidate || !candidate->IsInWorld() || candidate == requester)
            continue;
        if (addon::pure::Lower(candidate->GetName()) == parsed.botNameLower)
        {
            found = candidate;
            break;
        }
    }
    if (!found || !PlayerbotAIStorage::Instance().GetAI(found))
    {
        addon::Err(requester, "professions", "no-bot", kProfessionsUsage);
        return;
    }

    std::uint32_t const bnet = CallerBnet(requester);
    player::Caller caller;
    caller.gm = requester->GetSession() && requester->GetSession()->HasPermission(rbac::RBAC_PERM_COMMAND_GM);
    caller.account = bnet;
    caller.guid = requester->GetGUID().GetCounter();
    player::Verdict const verdict = player::Decide(caller, FactsFor(requester, found, bnet), player::Tier::Tactical);
    if (verdict != player::Verdict::Allowed)
    {
        addon::Err(requester, "professions", player::CodeOf(verdict), "You may not inspect that bot.");
        return;
    }

    SendProfessionSnapshot(requester, found);
}

} // namespace
} // namespace professions
} // namespace TortoiseBots

void TortoiseBots::professions::RegisterAddonVerbsProfessions()
{
    addon::RegisterVerb("professions", &HandleProfessionsVerb);
    addon::AdvertiseCap("professions");
}

#else

void TortoiseBots::professions::RegisterAddonVerbsProfessions()
{
}

#endif
