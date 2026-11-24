#include "host/TbMountShop.h"

#include "host/TbMountShopPure.h"

#if defined(TORTOISEBOTS_SLICE1) && TORTOISEBOTS_SLICE1

#include "host/TbHostTown.h"

#include "ai/playerbot/policy/travel/MountShopPolicy.h"

#include "CellImpl.h"
#include "Creature.h"
#include "GameTime.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "Spell.h"
#include "SpellAuraDefines.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "SharedDefines.h"
#include "Trainer.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <list>

namespace tbhost
{
namespace mountshop
{
namespace
{

using MountShopPolicy::MountTier;

// The riding floors only avoid a world scan for a bot that cannot pay even with
// the best reputation discount; FindPlan still checks the exact trainer price.
constexpr uint64_t kApprenticeFloorCopper = 800000;    // 80g of the 100g row
constexpr uint64_t kJourneymanFloorCopper = 8000000;   // 800g of the 1000g row

bool IsRidingSpell(uint32_t spellId)
{
    SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId, DIFFICULTY_NONE);
    if (!info)
        return false;
    for (SpellEffectInfo const& effect : info->GetEffects())
        if (effect.IsEffect(SPELL_EFFECT_SKILL) && effect.MiscValue == SKILL_RIDING)
            return true;
    return false;
}

bool IsRidingTrainer(Creature const* creature)
{
    if (!creature)
        return false;
    uint32_t const trainerId = creature->GetTrainerId();
    Trainer::Trainer const* trainer = trainerId ? sObjectMgr->GetTrainer(trainerId) : nullptr;
    if (!trainer)
        return false;
    for (Trainer::Spell const& spell : trainer->GetSpells())
        if (IsRidingSpell(spell.SpellId))
            return true;
    return false;
}

MountTier TierOf(uint8_t level)
{
    return IsEpicTier(level) ? MountTier::Epic : MountTier::Normal;
}

// The core grants the racial riding spells from the riding skill itself
// (Player::LearnSkillRewardedSpells via SetSkill); a skill written by any other
// path (`.tb setskill`, a seed) must not leave a bot unable to use the mount
// item its skill allows. Idempotent.
void EnsureRidingRewardedSpells(Player* bot)
{
    uint16_t const value = bot->GetSkillValue(SKILL_RIDING);
    if (value)
        bot->LearnSkillRewardedSpells(SKILL_RIDING, value, Races(bot->GetRace()));
}

bool HasMountSpell(Player* bot, uint32_t spellId)
{
    return bot->HasSpell(spellId);
}

bool HasTierMountFromItems(Player* bot, MountTier tier)
{
    for (uint32_t itemId : MountShopPolicy::GetRacialMounts(bot->GetRace(), tier))
    {
        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId);
        if (!proto)
            continue;
        for (ItemEffectEntry const* effect : proto->Effects)
        {
            if (!effect->SpellID)
                continue;
            SpellInfo const* info = sSpellMgr->GetSpellInfo(uint32_t(effect->SpellID), DIFFICULTY_NONE);
            if (info && info->HasAura(SPELL_AURA_MOUNTED) && HasMountSpell(bot, uint32_t(effect->SpellID)))
                return true;
        }
    }
    return false;
}

// Fallback for a mount learned from a source the item lists do not name (the
// turtle, a class mount, a collection reward). Any mounted spell counts as the
// normal tier; the epic tier needs the 100% speed aura, so a normal mount is
// still replaced at 60.
bool HasMountedSpell(Player* bot, bool epic)
{
    for (auto const& [spellId, playerSpell] : bot->GetSpellMap())
    {
        if (playerSpell.state == PLAYERSPELL_REMOVED)
            continue;
        SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId, DIFFICULTY_NONE);
        if (!info)
            continue;
        bool mounted = false;
        bool speedSeen = false;
        float speed = 0.0f;
        for (SpellEffectInfo const& effect : info->GetEffects())
        {
            if (effect.ApplyAuraName == SPELL_AURA_MOUNTED)
                mounted = true;
            else if (effect.ApplyAuraName == SPELL_AURA_MOD_INCREASE_MOUNTED_SPEED)
            {
                speedSeen = true;
                speed = effect.BasePoints;
            }
        }
        if (!mounted)
            continue;
        if (!epic)
            return true;
        if (speedSeen && speed >= 90.0f)
            return true;
    }
    return false;
}

bool HasMount(Player* bot, bool epic)
{
    if (HasTierMountFromItems(bot, TierOf(bot->GetLevel())))
        return true;
    return HasMountedSpell(bot, epic);
}

uint64_t TrainerCost(tbhost::town::TrainerSpellView const& spell, float discount)
{
    return uint64_t(std::floor(double(spell.moneyCost) * double(discount)));
}

uint64_t VendorCost(ItemTemplate const* proto, float discount)
{
    uint32_t const buyCount = std::max<uint32_t>(proto->GetBuyCount(), 1u);
    uint64_t const base = (uint64_t(proto->GetBuyPrice()) + buyCount - 1) / buyCount;
    return std::max<uint64_t>(1, uint64_t(std::floor(double(base) * double(discount))));
}

// The item data's own price, not a copied budget: a bot below 80% of the
// cheapest mount of its tier cannot afford it even at Exalted.
uint64_t CheapestTierMountPrice(uint8_t race, MountTier tier)
{
    uint64_t cheapest = std::numeric_limits<uint64_t>::max();
    for (uint32_t itemId : MountShopPolicy::GetRacialMounts(race, tier))
    {
        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId);
        if (proto && proto->GetBuyPrice())
            cheapest = std::min<uint64_t>(cheapest, proto->GetBuyPrice());
    }
    return cheapest;
}

template <typename Fn>
void ForEachNearbyCreature(Player* bot, float rangeYd, Fn&& fn)
{
    std::list<Creature*> creatures;
    Trinity::AnyUnitInObjectRangeCheck check(bot, rangeYd);
    Trinity::CreatureListSearcher<Trinity::AnyUnitInObjectRangeCheck> searcher(bot, creatures, check);
    Cell::VisitGridObjects(bot, searcher, rangeYd);

    uint32_t const botZone = bot->GetZoneId();
    for (Creature* creature : creatures)
    {
        if (!creature || !creature->IsAlive())
            continue;
        if (creature->GetZoneId() != botZone || !creature->InSamePhase(bot) || bot->IsHostileTo(creature))
            continue;
        fn(creature);
    }
}

} // namespace

char const* StepToken(Step step)
{
    switch (step)
    {
        case Step::TrainRiding: return "train-riding";
        case Step::BuyMount: return "buy-mount";
        case Step::UseItem: return "use-item";
        default: return "none";
    }
}

Need NeedOf(Player* bot)
{
    if (!bot)
        return Need::None;
    uint8_t const level = bot->GetLevel();
    uint16_t const target = RidingTargetForLevel(level);
    if (!target)
        return Need::None;
    if (bot->GetSkillValue(SKILL_RIDING) < target)
        return Need::TrainRiding;
    return HasMount(bot, IsEpicTier(level)) ? Need::None : Need::BuyMount;
}

Plan FindPlan(Player* bot, float rangeYd)
{
    Plan plan;
    if (!bot || !bot->IsInWorld() || !bot->FindMap())
    {
        plan.reason = "not-in-world";
        return plan;
    }

    EnsureRidingRewardedSpells(bot);

    uint8_t const level = bot->GetLevel();
    uint16_t const ridingSkill = bot->GetSkillValue(SKILL_RIDING);
    uint64_t const money = bot->GetMoney();

    switch (NeedOf(bot))
    {
        case Need::None:
            plan.reason = "none";
            return plan;
        case Need::BuyMount:
        {
            MountShopPolicy::MountItemSpan const tierMounts = MountShopPolicy::GetRacialMounts(bot->GetRace(), TierOf(level));
            for (uint32_t itemId : tierMounts)
            {
                if (bot->GetItemByEntry(itemId, ItemSearchLocation::Inventory))
                {
                    plan.step = Step::UseItem;
                    plan.itemId = itemId;
                    plan.reason = "owned-item";
                    return plan;
                }
            }

            uint64_t const cheapest = CheapestTierMountPrice(bot->GetRace(), TierOf(level));
            if (cheapest == std::numeric_limits<uint64_t>::max())
            {
                plan.reason = "no-racial-mounts";
                return plan;
            }
            if (money < cheapest - cheapest / 5)
            {
                plan.reason = "no-money";
                return plan;
            }

            Creature* best = nullptr;
            float bestDist = std::numeric_limits<float>::max();
            bool sawVendor = false;
            bool sawUnaffordable = false;
            bool sawUnusable = false;

            ForEachNearbyCreature(bot, rangeYd, [&](Creature* vendor)
            {
                if (!vendor->HasNpcFlag(UNIT_NPC_FLAG_VENDOR))
                    return;
                VendorItemData const* items = vendor->GetVendorItems();
                if (!items)
                    return;
                float const discount = bot->GetReputationPriceDiscount(vendor);
                for (uint32_t slot = 0; slot < items->GetItemCount(); ++slot)
                {
                    VendorItem const* vItem = items->GetItem(slot);
                    if (!vItem || !tierMounts.contains(vItem->item))
                        continue;
                    sawVendor = true;
                    ItemTemplate const* proto = sObjectMgr->GetItemTemplate(vItem->item);
                    if (!proto)
                        continue;
                    if (bot->CanUseItem(proto) != EQUIP_ERR_OK)
                    {
                        sawUnusable = true;
                        continue;
                    }
                    if (!tbhost::town::CheckVendorItemAllowed(bot, vendor, *vItem))
                    {
                        sawUnusable = true;
                        continue;
                    }
                    uint64_t const price = VendorCost(proto, discount);
                    if (!bot->HasEnoughMoney(price))
                    {
                        sawUnaffordable = true;
                        continue;
                    }
                    float const distance = bot->GetDistance(vendor);
                    if (distance < bestDist)
                    {
                        bestDist = distance;
                        best = vendor;
                        plan.vendorSlot = slot;
                        plan.itemId = vItem->item;
                        plan.cost = uint32_t(std::min<uint64_t>(price, std::numeric_limits<uint32_t>::max()));
                    }
                }
            });

            if (best)
            {
                plan.step = Step::BuyMount;
                plan.npc = best->GetGUID();
                plan.reason = "needs-mount";
            }
            else if (sawUnaffordable)
                plan.reason = "mount-unaffordable";
            else if (sawUnusable)
                plan.reason = "mount-unusable";
            else if (sawVendor)
                plan.reason = "mount-unaffordable";
            else
                plan.reason = "no-mount-vendor";
            return plan;
        }
        case Need::TrainRiding:
        default:
            break;
    }

    bool const journeyman = (ridingSkill >= kApprenticeRiding && level >= kEpicMountLevel);
    if (money < (journeyman ? kJourneymanFloorCopper : kApprenticeFloorCopper))
    {
        plan.reason = "no-money";
        return plan;
    }

    Creature* best = nullptr;
    float bestDist = std::numeric_limits<float>::max();
    bool sawTrainer = false;
    bool sawUnaffordable = false;

    ForEachNearbyCreature(bot, rangeYd, [&](Creature* trainer)
    {
        if (!trainer->HasNpcFlag(UNIT_NPC_FLAG_TRAINER) || !IsRidingTrainer(trainer))
            return;
        sawTrainer = true;
        float const discount = bot->GetReputationPriceDiscount(trainer);
        bool affordable = false;
        uint64_t cheapest = std::numeric_limits<uint64_t>::max();
        for (tbhost::town::TrainerSpellView const& spell : tbhost::town::GetTrainerSpells(trainer))
        {
            if (!IsRidingSpell(spell.spellId))
                continue;
            if (!tbhost::town::CanTeachTrainerSpell(bot, trainer, spell, discount))
                continue;
            affordable = true;
            cheapest = std::min(cheapest, TrainerCost(spell, discount));
        }
        if (!affordable)
        {
            sawUnaffordable = true;
            return;
        }
        float const distance = bot->GetDistance(trainer);
        if (distance < bestDist)
        {
            bestDist = distance;
            best = trainer;
            plan.cost = uint32_t(std::min<uint64_t>(cheapest, std::numeric_limits<uint32_t>::max()));
        }
    });

    if (best)
    {
        plan.step = Step::TrainRiding;
        plan.npc = best->GetGUID();
        plan.reason = "needs-riding";
    }
    else if (sawUnaffordable)
        plan.reason = "riding-unaffordable";
    else if (sawTrainer)
        plan.reason = "riding-unaffordable";
    else
        plan.reason = "no-riding-trainer";
    return plan;
}

namespace
{

bool Fail(std::string* refusal, char const* code)
{
    if (refusal)
        *refusal = code;
    return false;
}

bool TrainRiding(Player* bot, Creature* trainer, uint32_t* spentCopper, std::string* refusal)
{
    if (!bot || !trainer)
        return Fail(refusal, "invalid-arg");
    if (!IsRidingTrainer(trainer))
        return Fail(refusal, "not-riding-trainer");

    float const discount = bot->GetReputationPriceDiscount(trainer);
    uint64_t const before = bot->GetMoney();
    uint32_t learned = 0;
    std::string lastRefusal;
    for (tbhost::town::TrainerSpellView const& spell : tbhost::town::GetTrainerSpells(trainer))
    {
        if (!IsRidingSpell(spell.spellId))
            continue;
        if (!tbhost::town::CanTeachTrainerSpell(bot, trainer, spell, discount, nullptr))
            continue;
        std::string why;
        if (tbhost::town::TeachTrainerSpell(bot, trainer, spell.spellId, &why))
            ++learned;
        else
            lastRefusal = why;
    }
    EnsureRidingRewardedSpells(bot);

    if (spentCopper)
        *spentCopper = uint32_t(std::min<uint64_t>(before - bot->GetMoney(), std::numeric_limits<uint32_t>::max()));
    if (!learned)
        return Fail(refusal, lastRefusal.empty() ? "nothing-affordable" : lastRefusal.c_str());
    return true;
}

bool UseOwnedMountItem(Player* bot, uint32_t itemId, uint32_t* spentCopper, std::string* refusal)
{
    if (spentCopper)
        *spentCopper = 0;
    Item* item = bot->GetItemByEntry(itemId, ItemSearchLocation::Inventory);
    if (!item)
        return Fail(refusal, "item-not-in-bags");

    for (ItemEffectEntry const* effect : item->GetEffects())
    {
        if (effect->TriggerType != ITEM_SPELLTRIGGER_ON_USE || !effect->SpellID)
            continue;
        SpellCastTargets targets;
        targets.SetUnitTarget(bot);
        bot->CastItemUseSpell(item, uint32_t(effect->SpellID), targets, ObjectGuid::Empty, { 0, 0, 0 });
        return true;
    }

    // No on-use cast: apply the item's learn effects directly, which is the
    // state a use would leave (the mount spell is the item's own effect).
    bool learned = false;
    for (ItemEffectEntry const* effect : item->GetEffects())
    {
        if (effect->TriggerType != ITEM_SPELLTRIGGER_ON_LEARN || !effect->SpellID)
            continue;
        if (!bot->HasSpell(uint32_t(effect->SpellID)))
        {
            bot->LearnSpell(uint32_t(effect->SpellID), false);
            learned = true;
        }
    }
    return learned ? true : Fail(refusal, "no-use-effect");
}

bool BuyAndUseMount(Player* bot, Creature* vendor, uint32_t vendorSlot, uint32_t itemId, uint32_t* spentCopper, std::string* refusal)
{
    if (spentCopper)
        *spentCopper = 0;
    ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId);
    if (!proto)
        return Fail(refusal, "unknown-item");
    if (bot->CanUseItem(proto) != EQUIP_ERR_OK)
        return Fail(refusal, "unusable-item");

    // A mount item already in the bags is not bought again.
    if (bot->GetItemByEntry(itemId, ItemSearchLocation::Inventory))
        return UseOwnedMountItem(bot, itemId, spentCopper, refusal);

    uint64_t const before = bot->GetMoney();
    std::string why;
    if (!tbhost::town::BuyItemFromVendorSlot(bot, vendor, vendorSlot, itemId, 1, &why))
        return Fail(refusal, why.empty() ? "buy-failed" : why.c_str());

    if (spentCopper)
        *spentCopper = uint32_t(std::min<uint64_t>(before - bot->GetMoney(), std::numeric_limits<uint32_t>::max()));
    return UseOwnedMountItem(bot, itemId, nullptr, refusal);
}

} // namespace

bool ExecutePlan(Player* bot, Plan const& plan, uint32_t* spentCopper, std::string* refusal)
{
    if (spentCopper)
        *spentCopper = 0;
    if (!bot)
        return Fail(refusal, "invalid-arg");

    switch (plan.step)
    {
        case Step::TrainRiding:
        {
            Creature* trainer = ObjectAccessor::GetCreature(*bot, plan.npc);
            if (!trainer || !tbhost::town::NpcIfCanInteract(bot, plan.npc, UNIT_NPC_FLAG_TRAINER))
                return Fail(refusal, "no-riding-trainer");
            return TrainRiding(bot, trainer, spentCopper, refusal);
        }
        case Step::BuyMount:
        {
            Creature* vendor = ObjectAccessor::GetCreature(*bot, plan.npc);
            if (!vendor || !tbhost::town::NpcIfCanInteract(bot, plan.npc, UNIT_NPC_FLAG_VENDOR))
                return Fail(refusal, "no-mount-vendor");
            return BuyAndUseMount(bot, vendor, plan.vendorSlot, plan.itemId, spentCopper, refusal);
        }
        case Step::UseItem:
            return UseOwnedMountItem(bot, plan.itemId, spentCopper, refusal);
        default:
            return Fail(refusal, "nothing-to-do");
    }
}

} // namespace mountshop
} // namespace tbhost

#else

namespace tbhost
{
namespace mountshop
{

char const* StepToken(Step step)
{
    switch (step)
    {
        case Step::TrainRiding: return "train-riding";
        case Step::BuyMount: return "buy-mount";
        case Step::UseItem: return "use-item";
        default: return "none";
    }
}

Need NeedOf(Player*)
{
    return Need::None;
}

Plan FindPlan(Player*, float)
{
    Plan plan;
    plan.reason = "engine-not-built";
    return plan;
}

bool ExecutePlan(Player*, Plan const&, uint32_t*, std::string* refusal)
{
    if (refusal)
        *refusal = "engine-not-built";
    return false;
}

} // namespace mountshop
} // namespace tbhost

#endif
