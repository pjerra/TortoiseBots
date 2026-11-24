#include "host/TbHostCapitalErrands.h"

#include "TbBotMgr.h"
#include "host/TbEngineBrain.h"
#include "host/TbHostAuction.h"
#include "host/TbHostGuildMail.h"
#include "host/TbHostGuildOrderDisenchant.h"
#include "host/TbHostGuildOrders.h"
#include "host/TbHostProfession.h"
#include "host/TbHostTown.h"
#include "host/TbMountShop.h"

#include "Bag.h"
#include "CellImpl.h"
#include "Creature.h"
#include "DB2Stores.h"
#include "DuelPackets.h"
#include "GameObject.h"
#include "GameTime.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "Log.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Opcodes.h"
#include "Player.h"
#include "WorldSession.h"

#include "playerbot/policy/ah/AhSellPolicy.h"
#include "playerbot/policy/world/CapitalErrandPolicy.h"

#include <algorithm>
#include <list>
#include <unordered_map>

namespace tbhost
{
namespace errands
{
namespace
{

using namespace TortoiseBots::world;

constexpr float kMaxTownVisitDistYd = 500.0f;
constexpr float kMountSearchDistYd = 150.0f;
constexpr float kDuelSearchDistYd = 30.0f;
constexpr uint64_t kErrandWalkTimeoutMs = 45000;
constexpr uint64_t kDebugRefusalIntervalMs = 60000;
constexpr uint64_t kMountNoPlanCooldownMs = 60000;
constexpr uint64_t kMountStepPauseMs = 3000;
constexpr uint32_t kSpellDuel = 7266;

struct ActiveErrand
{
    bool active = false;
    CapitalErrandKind kind = CapitalErrandKind::None;
    ObjectGuid targetGuid;
    bool targetIsGameObject = false;
    uint64_t startedAtMs = 0;
};

struct BotErrandState
{
    uint64_t nextGlobalTryMs = 0;
    uint64_t nextDuelScanMs = 0;
    uint64_t mailboxCooldownUntilMs = 0;
    uint64_t trainerCooldownUntilMs = 0;
    uint64_t bankCooldownUntilMs = 0;
    uint64_t ahCooldownUntilMs = 0;
    uint64_t mountCooldownUntilMs = 0;
    uint64_t duelCooldownUntilMs = 0;
    bool duelFightLogged = false;
    uint64_t nextDebugLogMs = 0;
    ActiveErrand current;
};

std::unordered_map<ObjectGuid, BotErrandState>& States()
{
    static std::unordered_map<ObjectGuid, BotErrandState> map;
    return map;
}

char const* KindName(CapitalErrandKind kind)
{
    switch (kind)
    {
        case CapitalErrandKind::Mailbox: return "mailbox";
        case CapitalErrandKind::Trainer: return "trainer";
        case CapitalErrandKind::Bank: return "bank";
        case CapitalErrandKind::AuctionHouse: return "ah";
        case CapitalErrandKind::Mount: return "mount";
        default: return "none";
    }
}

bool IsInCapitalArea(Player const* bot)
{
    if (!bot)
        return false;
    bool areaLinkedChat = false;
    if (AreaTableEntry const* area = sAreaTableStore.LookupEntry(bot->GetAreaId()))
        areaLinkedChat = area->GetFlags().HasFlag(AreaFlags::LinkedChat);
    bool zoneLinkedChat = false;
    if (AreaTableEntry const* zone = sAreaTableStore.LookupEntry(bot->GetZoneId()))
        zoneLinkedChat = zone->GetFlags().HasFlag(AreaFlags::LinkedChat);
    return IsCapitalAreaOrZone(areaLinkedChat, zoneLinkedChat, bot->GetZoneId());
}

bool IsInDuelingArea(Player const* bot)
{
    if (!bot)
        return false;
    if (AreaTableEntry const* area = sAreaTableStore.LookupEntry(bot->GetAreaId()))
        return area->GetFlags().HasFlag(AreaFlags::AllowDueling);
    return false;
}

template <typename Fn>
void ForEachBagItem(Player* bot, Fn&& fn)
{
    for (uint8_t slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
    {
        if (Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
            fn(item);
    }
    for (uint8_t bagSlot = INVENTORY_SLOT_BAG_START; bagSlot < INVENTORY_SLOT_BAG_END; ++bagSlot)
    {
        Bag* bag = bot->GetBagByPos(bagSlot);
        if (!bag)
            continue;
        for (uint32_t slot = 0; slot < bag->GetBagSize(); ++slot)
        {
            if (Item* item = bag->GetItemByPos(slot))
                fn(item);
        }
    }
}

uint32_t CountFreeBagSlots(Player* bot)
{
    uint32_t freeSlots = 0;
    for (uint8_t slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
    {
        if (!bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
            ++freeSlots;
    }
    for (uint8_t bagSlot = INVENTORY_SLOT_BAG_START; bagSlot < INVENTORY_SLOT_BAG_END; ++bagSlot)
    {
        Bag* bag = bot->GetBagByPos(bagSlot);
        if (!bag)
            continue;
        for (uint32_t slot = 0; slot < bag->GetBagSize(); ++slot)
        {
            if (!bag->GetItemByPos(slot))
                ++freeSlots;
        }
    }
    return freeSlots;
}

bool IsFoodOrWaterOrAmmo(ItemTemplate const* proto)
{
    if (!proto)
        return false;
    if (proto->GetClass() == ITEM_CLASS_PROJECTILE)
        return true;
    if (proto->GetClass() == ITEM_CLASS_CONSUMABLE &&
        (proto->GetSubClass() == ITEM_SUBCLASS_FOOD_DRINK || proto->GetSubClass() == ITEM_SUBCLASS_CONSUMABLE_OTHER))
        return true;
    return false;
}

uint32_t CountBankableItems(Player* bot)
{
    uint32_t count = 0;
    ForEachBagItem(bot, [&](Item* item)
    {
        ItemTemplate const* proto = item->GetTemplate();
        if (!proto)
            return;
        bool const isTool = ai::IsAhProfessionTool(proto->GetId(), false);
        if (ShouldBankItemCapital(
                proto->GetClass(),
                proto->GetQuality(),
                false,
                item->IsSoulBound(),
                IsFoodOrWaterOrAmmo(proto),
                isTool))
        {
            ++count;
        }
    });
    return count;
}

uint32_t CountAhSellableItems(Player* bot)
{
    uint32_t count = 0;
    ForEachBagItem(bot, [&](Item* item)
    {
        ItemTemplate const* proto = item->GetTemplate();
        if (!proto)
            return;
        if (item->IsSoulBound() || proto->GetClass() == ITEM_CLASS_QUEST || proto->GetQuality() < ITEM_QUALITY_UNCOMMON)
            return;
        if (ai::IsAhProfessionTool(proto->GetId(), false) || IsFoodOrWaterOrAmmo(proto))
            return;
        ++count;
    });
    return count;
}

Creature* FindNearestBanker(Player* bot, float maxDistYd)
{
    if (!bot || !bot->IsInWorld() || !bot->FindMap())
        return nullptr;
    std::list<Creature*> creatures;
    Trinity::AnyUnitInObjectRangeCheck check(bot, maxDistYd);
    Trinity::CreatureListSearcher<Trinity::AnyUnitInObjectRangeCheck> searcher(bot, creatures, check);
    Cell::VisitGridObjects(bot, searcher, maxDistYd);

    Creature* best = nullptr;
    float bestDist = maxDistYd + 1.0f;
    uint32_t const botZone = bot->GetZoneId();
    for (Creature* c : creatures)
    {
        if (!c || !c->IsAlive() || !c->HasNpcFlag(UNIT_NPC_FLAG_BANKER))
            continue;
        if (c->GetZoneId() != botZone || !c->InSamePhase(bot) || bot->IsHostileTo(c))
            continue;
        float const d = bot->GetDistance(c);
        if (d <= maxDistYd && d < bestDist)
        {
            bestDist = d;
            best = c;
        }
    }
    return best;
}

Creature* FindNearestClassTrainer(Player* bot, float maxDistYd)
{
    if (!bot || !bot->IsInWorld() || !bot->FindMap())
        return nullptr;
    std::list<Creature*> creatures;
    Trinity::AnyUnitInObjectRangeCheck check(bot, maxDistYd);
    Trinity::CreatureListSearcher<Trinity::AnyUnitInObjectRangeCheck> searcher(bot, creatures, check);
    Cell::VisitGridObjects(bot, searcher, maxDistYd);

    Creature* best = nullptr;
    float bestDist = maxDistYd + 1.0f;
    uint32_t const botZone = bot->GetZoneId();
    for (Creature* c : creatures)
    {
        if (!c || !c->IsAlive() || !tbhost::town::IsTrainer(c))
            continue;
        if (tbhost::town::GetTrainerKind(c) != tbhost::town::TrainerKind::Class)
            continue;
        if (!tbhost::town::IsTrainerOf(c, bot))
            continue;
        if (c->GetZoneId() != botZone || !c->InSamePhase(bot) || bot->IsHostileTo(c))
            continue;
        float const d = bot->GetDistance(c);
        if (d <= maxDistYd && d < bestDist)
        {
            bestDist = d;
            best = c;
        }
    }
    return best;
}

bool HasTrainableClassSpell(Player* bot, Creature* trainer)
{
    if (!bot || !trainer)
        return false;
    float const discountMod = bot->GetReputationPriceDiscount(trainer);
    for (tbhost::town::TrainerSpellView const& sp : tbhost::town::GetTrainerSpells(trainer))
    {
        if (tbhost::town::CanTeachTrainerSpell(bot, trainer, sp, discountMod))
            return true;
    }
    return false;
}

bool DepositBankItems(Player* bot, Creature* banker)
{
    if (!bot || !banker)
        return false;
    if (!tbhost::town::NpcIfCanInteract(bot, banker->GetGUID(), UNIT_NPC_FLAG_BANKER))
        return false;

    std::vector<Item*> toDeposit;
    ForEachBagItem(bot, [&](Item* item)
    {
        ItemTemplate const* proto = item->GetTemplate();
        if (!proto)
            return;
        if (ShouldBankItemCapital(
                proto->GetClass(),
                proto->GetQuality(),
                false,
                item->IsSoulBound(),
                IsFoodOrWaterOrAmmo(proto),
                ai::IsAhProfessionTool(proto->GetId(), false)))
        {
            toDeposit.push_back(item);
        }
    });

    uint32_t deposited = 0;
    for (Item* item : toDeposit)
    {
        if (!item || !item->IsInWorld())
            continue;
        ItemPosCountVec dest;
        if (bot->CanBankItem(NULL_BAG, NULL_SLOT, dest, item, false) != EQUIP_ERR_OK)
            break;
        bot->RemoveItem(item->GetBagSlot(), item->GetSlot(), true);
        bot->BankItem(dest, item, true);
        ++deposited;
    }
    return deposited > 0;
}

bool ExecuteErrandAtTarget(Player* bot, CapitalErrandKind kind, ObjectGuid const& targetGuid)
{
    switch (kind)
    {
        case CapitalErrandKind::Mailbox:
            return TortoiseBots::engine::RunAction(bot, "mail", "take");
        case CapitalErrandKind::Trainer:
        {
            Creature* trainer = tbhost::town::NpcIfCanInteract(bot, targetGuid, UNIT_NPC_FLAG_TRAINER);
            if (!trainer || !tbhost::town::IsTrainer(trainer) || !tbhost::town::IsTrainerOf(trainer, bot))
                return false;
            float const discountMod = bot->GetReputationPriceDiscount(trainer);
            uint32_t learned = 0;
            for (tbhost::town::TrainerSpellView const& sp : tbhost::town::GetTrainerSpells(trainer))
            {
                if (!tbhost::town::CanTeachTrainerSpell(bot, trainer, sp, discountMod))
                    continue;
                if (tbhost::town::TeachTrainerSpell(bot, trainer, sp.spellId))
                    ++learned;
            }
            return learned > 0;
        }
        case CapitalErrandKind::Bank:
        {
            Creature* banker = tbhost::town::NpcIfCanInteract(bot, targetGuid, UNIT_NPC_FLAG_BANKER);
            return DepositBankItems(bot, banker);
        }
        case CapitalErrandKind::AuctionHouse:
            return TortoiseBots::engine::RunAction(bot, "ah", "vendor");
        default:
            return false;
    }
}

void SetKindCooldown(BotErrandState& st, CapitalErrandKind kind, uint64_t nowMs)
{
    uint64_t const until = nowMs + CooldownMsForErrand(kind);
    switch (kind)
    {
        case CapitalErrandKind::Mailbox: st.mailboxCooldownUntilMs = until; break;
        case CapitalErrandKind::Trainer: st.trainerCooldownUntilMs = until; break;
        case CapitalErrandKind::Bank: st.bankCooldownUntilMs = until; break;
        case CapitalErrandKind::AuctionHouse: st.ahCooldownUntilMs = until; break;
        case CapitalErrandKind::Mount: st.mountCooldownUntilMs = until; break;
        default: break;
    }
    st.nextGlobalTryMs = nowMs + kErrandGlobalIntervalMs;
}

// Training is one step: with riding learned the bot still has to walk to the
// vendor, so a successful training resumes in seconds instead of after the
// full cooldown. Buying and using wait the full cooldown: the mount spell is
// learned from the item's cast, and a second purchase must not follow a cast
// that has not landed yet.
void FinishMountErrand(BotErrandState& st, uint64_t nowMs, bool ok, mountshop::Step step)
{
    uint64_t const pause = (ok && step == mountshop::Step::TrainRiding)
        ? kMountStepPauseMs
        : kMountCooldownMs;
    st.mountCooldownUntilMs = nowMs + pause;
    st.nextGlobalTryMs = nowMs + pause;
}

uint8_t DuelMinLevel()
{
    return static_cast<uint8_t>(std::clamp<uint32_t>(TortoiseBots::engine::DuelMinimumLevel(), 1, 255));
}

// Answers whatever duel the core has put this bot in. A challenger the bot knows as its real player master is
// accepted at any health, anyone else only at 90% or more; both rules are TB's (AcceptDuelAction.h:16-38).
void RespondToDuel(Player* bot, BotErrandState& st, uint64_t nowMs)
{
    if (!bot->duel)
    {
        st.duelFightLogged = false;
        return;
    }

    auto const& mgr = TortoiseBots::BotMgr::Instance();

    if (bot->duel->State == DUEL_STATE_CHALLENGED && bot != bot->duel->Initiator)
    {
        Player* initiator = bot->duel->Initiator;
        ObjectGuid const masterGuid = mgr.MasterOf(bot->GetGUID());
        Player* master = masterGuid.IsEmpty() ? nullptr : ObjectAccessor::FindPlayer(masterGuid);
        // TB's HasRealPlayerMaster: a master with a network session; a bot master does not earn the exemption.
        bool const masterIsRealPlayer = master && master->GetSession() && !master->GetSession()->IsBot();
        bool const challengerIsMaster = masterIsRealPlayer && master == initiator;
        uint8_t const myHpPct = static_cast<uint8_t>(std::clamp<float>(bot->GetHealthPct(), 0.0f, 100.0f));
        bool const accept = ShouldAcceptBotDuel(
            bot->IsAlive(),
            bot->IsInCombat(),
            challengerIsMaster,
            bot->GetLevel(),
            DuelMinLevel(),
            myHpPct);

        WorldPackets::Duel::DuelResponse resp{ WorldPacket(CMSG_DUEL_RESPONSE) };
        resp.ArbiterGUID = *bot->m_playerData->DuelArbiter;
        resp.Accepted = accept;
        resp.Forfeited = !accept;
        bot->GetSession()->HandleDuelResponseOpcode(resp);
        st.duelCooldownUntilMs = nowMs + kDuelCooldownMs;
        TC_LOG_INFO("tortoisebots", "[W2] duel-response bot={} initiator={} accept={}",
            bot->GetName(), initiator ? initiator->GetName() : "?", accept ? 1 : 0);
        return;
    }

    if (bot->duel->State == DUEL_STATE_IN_PROGRESS && bot->duel->Opponent)
    {
        if (!bot->GetVictim())
            bot->Attack(bot->duel->Opponent, true);
        if (!st.duelFightLogged)
        {
            st.duelFightLogged = true;
            TC_LOG_INFO("tortoisebots", "[W2] duel-fight bot={} opponent={}",
                bot->GetName(), bot->duel->Opponent->GetName());
        }
    }
}

void ScanForBotDuel(Player* bot, BotErrandState& st, uint64_t nowMs)
{
    if (!bot->IsAlive() || bot->IsInCombat() || bot->isMoving() || bot->GetCurrentSpell(CURRENT_GENERIC_SPELL))
        return;
    if (nowMs < st.nextDuelScanMs || nowMs < st.duelCooldownUntilMs || !IsInDuelingArea(bot))
        return;
    st.nextDuelScanMs = nowMs + kDuelScanIntervalMs;

    auto const& mgr = TortoiseBots::BotMgr::Instance();
    uint8_t const minDuelLevel = DuelMinLevel();
    uint8_t const myHpPct = static_cast<uint8_t>(std::clamp<float>(bot->GetHealthPct(), 0.0f, 100.0f));

    std::list<Player*> nearbyPlayers;
    Trinity::AnyUnitInObjectRangeCheck check(bot, kDuelSearchDistYd);
    Trinity::PlayerListSearcher<Trinity::AnyUnitInObjectRangeCheck> searcher(bot, nearbyPlayers, check);
    Cell::VisitWorldObjects(bot, searcher, kDuelSearchDistYd);

    for (Player* target : nearbyPlayers)
    {
        if (!target || target == bot || !target->IsInWorld() || target->FindMap() != bot->FindMap())
            continue;
        if (!mgr.IsFreeBot(target->GetGUID()))
            continue;

        auto const targetItr = States().find(target->GetGUID());
        uint64_t const targetCooldownUntilMs = (targetItr != States().end()) ? targetItr->second.duelCooldownUntilMs : 0;

        DuelCandidateInput din;
        din.challengerIsFreeBot = true;
        din.challengerAlive = bot->IsAlive();
        din.challengerInCombat = bot->IsInCombat();
        din.challengerInDuel = (bot->duel != nullptr);
        din.challengerIsMoving = bot->isMoving();
        din.challengerLevel = bot->GetLevel();
        din.challengerHealthPct = myHpPct;
        din.targetIsBot = true;
        din.targetIsFreeBot = true;
        din.targetAlive = target->IsAlive();
        din.targetInCombat = target->IsInCombat();
        din.targetInDuel = (target->duel != nullptr);
        din.targetLevel = target->GetLevel();
        din.targetHealthPct = static_cast<uint8_t>(std::clamp<float>(target->GetHealthPct(), 0.0f, 100.0f));
        din.allowDuelingArea = IsInDuelingArea(target);
        din.minDuelLevel = minDuelLevel;
        din.distanceYd = bot->GetDistance(target);
        din.maxRangeYd = kDuelSearchDistYd;
        din.nowMs = nowMs;
        din.challengerCooldownUntilMs = st.duelCooldownUntilMs;
        din.targetCooldownUntilMs = targetCooldownUntilMs;

        if (!CanInitiateBotDuel(din))
            continue;

        bot->CastSpell(target, kSpellDuel, false);
        if (bot->duel != nullptr)
        {
            st.duelCooldownUntilMs = nowMs + kDuelCooldownMs;
            States()[target->GetGUID()].duelCooldownUntilMs = nowMs + kDuelCooldownMs;
            TC_LOG_INFO("tortoisebots", "[W2] duel-challenge bot={} target={}", bot->GetName(), target->GetName());
        }
        break;
    }
}

} // namespace

void TickCapitalErrands(Player* bot, uint32_t /*diffMs*/)
{
    if (!bot || !bot->IsInWorld() || !bot->FindMap() || bot->IsBeingTeleported())
        return;

    uint64_t const nowMs = GameTime::GetGameTimeMS();
    BotErrandState& st = States()[bot->GetGUID()];

    if (!bot->duel)
        ScanForBotDuel(bot, st, nowMs);

    if (st.current.active)
    {
        if (!bot->IsAlive() || bot->IsInCombat() || bot->duel != nullptr ||
            (nowMs - st.current.startedAtMs) >= kErrandWalkTimeoutMs)
        {
            st.current = ActiveErrand{};
            st.nextGlobalTryMs = nowMs + kErrandFailBackoffMs;
            return;
        }

        WorldObject* target = nullptr;
        if (st.current.targetIsGameObject)
            target = ObjectAccessor::GetGameObject(*bot, st.current.targetGuid);
        else
            target = ObjectAccessor::GetCreature(*bot, st.current.targetGuid);

        if (!target || !target->IsInWorld() || target->GetMap() != bot->FindMap())
        {
            st.current = ActiveErrand{};
            st.nextGlobalTryMs = nowMs + kErrandFailBackoffMs;
            return;
        }

        if (bot->GetDistance(target) <= INTERACTION_DISTANCE)
        {
            bot->StopMoving();
            if (st.current.kind == CapitalErrandKind::Mount)
            {
                mountshop::Plan const plan = mountshop::FindPlan(bot, INTERACTION_DISTANCE + 1.0f);
                std::string refusal;
                uint32_t spent = 0;
                bool const ok = plan.step != mountshop::Step::None &&
                    mountshop::ExecutePlan(bot, plan, &spent, &refusal);
                TC_LOG_INFO("tortoisebots", "[W2] capital-errand bot={} kind=mount step={} ok={} spent={} {}",
                    bot->GetName(), mountshop::StepToken(plan.step), ok ? 1 : 0, spent, refusal);
                FinishMountErrand(st, nowMs, ok, plan.step);
            }
            else
            {
                bool const ok = ExecuteErrandAtTarget(bot, st.current.kind, st.current.targetGuid);
                TC_LOG_INFO("tortoisebots", "[W2] capital-errand bot={} kind={} ok={}",
                    bot->GetName(), KindName(st.current.kind), ok ? 1 : 0);
                SetKindCooldown(st, st.current.kind, nowMs);
            }
            st.current = ActiveErrand{};
        }
        else if (!bot->isMoving())
        {
            bot->GetMotionMaster()->MovePoint(0, target->GetPositionX(), target->GetPositionY(), target->GetPositionZ());
        }
        return;
    }

    auto logRefusal = [&](char const* reason)
    {
        if (nowMs >= st.nextDebugLogMs)
        {
            st.nextDebugLogMs = nowMs + kDebugRefusalIntervalMs;
            TC_LOG_DEBUG("tortoisebots", "[W2] capital-errand-skip bot={} reason={}", bot->GetName(), reason);
        }
    };

    if (!bot->IsAlive() || bot->IsInCombat() ||
        bot->GetCurrentSpell(CURRENT_GENERIC_SPELL) || bot->duel != nullptr)
    {
        logRefusal(!bot->IsAlive() ? "dead" : bot->IsInCombat() ? "combat" : bot->duel ? "duel" : "casting");
        return;
    }
    if (tbhost::prof::HasActiveErrand(bot) || tbhost::guild_mail::HasActiveErrand(bot) ||
        tbhost::guild_orders::HasActiveDisenchantErrand(bot) ||
        tbhost::guild_orders::HasOpenOrder(bot->GetGUID().GetCounter()))
    {
        logRefusal("busy-errand-or-order");
        return;
    }
    if (nowMs < st.nextGlobalTryMs)
        return;

    mountshop::Plan mountPlan;
    bool needsMount = false;
    if (nowMs >= st.mountCooldownUntilMs)
    {
        mountPlan = mountshop::FindPlan(bot, kMountSearchDistYd);
        needsMount = mountPlan.step != mountshop::Step::None;
        if (!needsMount)
            st.mountCooldownUntilMs = nowMs + kMountNoPlanCooldownMs;
    }

    bool const inCapital = IsInCapitalArea(bot);
    if (!inCapital && !needsMount)
    {
        if (nowMs >= st.nextDebugLogMs)
        {
            st.nextDebugLogMs = nowMs + kDebugRefusalIntervalMs;
            TC_LOG_DEBUG("tortoisebots", "[W2] capital-errand-skip bot={} reason=not-capital zone={} area={}",
                bot->GetName(), bot->GetZoneId(), bot->GetAreaId());
        }
        return;
    }

    Creature* classTrainer = (inCapital && nowMs >= st.trainerCooldownUntilMs) ? FindNearestClassTrainer(bot, kMaxTownVisitDistYd) : nullptr;
    bool const hasTrainable = classTrainer && HasTrainableClassSpell(bot, classTrainer);

    CapitalErrandInput in;
    in.isFreeBot = true;
    in.hasActiveOrder = false;
    in.isAlive = true;
    in.inCombat = false;
    in.isMoving = false;
    in.isCasting = false;
    in.inDuel = false;
    in.inCapital = inCapital;
    in.auctionEnabled = inCapital && tbhost::ah::IsWorldAuctionEnabled();
    in.unreadMailCount = inCapital ? tbhost::town::GetUnreadMailCount(bot) : 0;
    in.hasTrainableSpell = hasTrainable;
    in.needsMountPurchase = needsMount;
    in.freeBagSlots = inCapital ? CountFreeBagSlots(bot) : 0;
    in.bankableItemCount = (inCapital && in.freeBagSlots <= kBankFreeSlotsThreshold) ? CountBankableItems(bot) : 0;
    in.ahSellableItemCount = in.auctionEnabled ? CountAhSellableItems(bot) : 0;
    in.nowMs = nowMs;
    in.nextGlobalTryMs = st.nextGlobalTryMs;
    in.mailboxCooldownUntilMs = st.mailboxCooldownUntilMs;
    in.trainerCooldownUntilMs = st.trainerCooldownUntilMs;
    in.bankCooldownUntilMs = st.bankCooldownUntilMs;
    in.ahCooldownUntilMs = st.ahCooldownUntilMs;
    in.mountCooldownUntilMs = st.mountCooldownUntilMs;

    CapitalErrandKind const pick = PickCapitalErrand(in);
    if (pick == CapitalErrandKind::None)
    {
        if (nowMs >= st.nextDebugLogMs)
        {
            st.nextDebugLogMs = nowMs + kDebugRefusalIntervalMs;
            TC_LOG_DEBUG("tortoisebots",
                "[W2] capital-errand-skip bot={} reason=none-picked unread={} train={} mount={} freeSlots={} bankable={} ahEnabled={} ahSellable={}",
                bot->GetName(), in.unreadMailCount, hasTrainable ? 1 : 0, mountPlan.reason.c_str(), in.freeBagSlots,
                in.bankableItemCount, in.auctionEnabled ? 1 : 0, in.ahSellableItemCount);
        }
        st.nextGlobalTryMs = nowMs + kErrandGlobalIntervalMs;
        return;
    }

    WorldObject* dest = nullptr;
    bool isGo = false;
    switch (pick)
    {
        case CapitalErrandKind::Mailbox:
            dest = tbhost::town::FindNearestMailbox(bot, kMaxTownVisitDistYd);
            isGo = true;
            break;
        case CapitalErrandKind::Trainer:
            dest = classTrainer;
            break;
        case CapitalErrandKind::Bank:
            dest = FindNearestBanker(bot, kMaxTownVisitDistYd);
            break;
        case CapitalErrandKind::AuctionHouse:
            dest = tbhost::ah::FindNearestFactionAuctioneer(bot, kMaxTownVisitDistYd);
            break;
        case CapitalErrandKind::Mount:
            if (mountPlan.step == mountshop::Step::UseItem)
            {
                std::string refusal;
                uint32_t spent = 0;
                bool const ok = mountshop::ExecutePlan(bot, mountPlan, &spent, &refusal);
                TC_LOG_INFO("tortoisebots", "[W2] capital-errand bot={} kind=mount step=use-item ok={} spent={} {}",
                    bot->GetName(), ok ? 1 : 0, spent, refusal);
                FinishMountErrand(st, nowMs, ok, mountPlan.step);
                return;
            }
            dest = ObjectAccessor::GetCreature(*bot, mountPlan.npc);
            break;
        default:
            break;
    }

    if (!dest)
    {
        if (nowMs >= st.nextDebugLogMs)
        {
            st.nextDebugLogMs = nowMs + kDebugRefusalIntervalMs;
            TC_LOG_DEBUG("tortoisebots", "[W2] capital-errand-skip bot={} reason=no-dest kind={}",
                bot->GetName(), KindName(pick));
        }
        SetKindCooldown(st, pick, nowMs);
        return;
    }

    if (bot->GetDistance(dest) <= INTERACTION_DISTANCE)
    {
        bot->StopMoving();
        if (pick == CapitalErrandKind::Mount)
        {
            std::string refusal;
            uint32_t spent = 0;
            bool const ok = mountshop::ExecutePlan(bot, mountPlan, &spent, &refusal);
            TC_LOG_INFO("tortoisebots", "[W2] capital-errand bot={} kind=mount step={} ok={} spent={} {}",
                bot->GetName(), mountshop::StepToken(mountPlan.step), ok ? 1 : 0, spent, refusal);
            FinishMountErrand(st, nowMs, ok, mountPlan.step);
            return;
        }
        bool const ok = ExecuteErrandAtTarget(bot, pick, dest->GetGUID());
        TC_LOG_INFO("tortoisebots", "[W2] capital-errand bot={} kind={} ok={}",
            bot->GetName(), KindName(pick), ok ? 1 : 0);
        SetKindCooldown(st, pick, nowMs);
        return;
    }

    if (bot->isMoving())
        bot->StopMoving();
    st.current.active = true;
    st.current.kind = pick;
    st.current.targetGuid = dest->GetGUID();
    st.current.targetIsGameObject = isGo;
    st.current.startedAtMs = nowMs;
    bot->GetMotionMaster()->MovePoint(0, dest->GetPositionX(), dest->GetPositionY(), dest->GetPositionZ());
}

bool HasActiveErrand(Player const* bot)
{
    if (!bot)
        return false;
    auto const itr = States().find(bot->GetGUID());
    return itr != States().end() && itr->second.current.active;
}

void TickDuelResponse(Player* bot)
{
    if (!bot || !bot->IsInWorld() || !bot->FindMap() || bot->IsBeingTeleported())
        return;

    RespondToDuel(bot, States()[bot->GetGUID()], GameTime::GetGameTimeMS());
}

void ClearBotCapitalErrands(ObjectGuid const& botGuid)
{
    States().erase(botGuid);
}

} // namespace errands
} // namespace tbhost
