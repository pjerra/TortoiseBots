#include "ai/playerbot/policy/world/CapitalErrandPolicy.h"

#include <cassert>
#include <cstdio>

using namespace TortoiseBots::world;

namespace
{
CapitalErrandInput BaseErrandInput()
{
    CapitalErrandInput in;
    in.isFreeBot = true;
    in.hasActiveOrder = false;
    in.isAlive = true;
    in.inCombat = false;
    in.isMoving = false;
    in.isCasting = false;
    in.inDuel = false;
    in.inCapital = true;
    in.auctionEnabled = true;
    in.nowMs = 500000;
    return in;
}

DuelCandidateInput BaseDuelInput()
{
    DuelCandidateInput in;
    in.challengerIsFreeBot = true;
    in.challengerAlive = true;
    in.challengerLevel = 20;
    in.challengerHealthPct = 100;
    in.targetIsBot = true;
    in.targetIsFreeBot = true;
    in.targetAlive = true;
    in.targetLevel = 20;
    in.targetHealthPct = 100;
    in.allowDuelingArea = true;
    in.minDuelLevel = 10;
    in.distanceYd = 10.0f;
    in.maxRangeYd = 30.0f;
    in.nowMs = 500000;
    return in;
}
} // namespace

int main()
{
    int checks = 0;

    // 1. Ordered or non-free bots never run autonomous capital errands.
    {
        auto in = BaseErrandInput();
        in.unreadMailCount = 2;
        in.isFreeBot = false;
        assert(PickCapitalErrand(in) == CapitalErrandKind::None);
        in.isFreeBot = true;
        in.hasActiveOrder = true;
        assert(PickCapitalErrand(in) == CapitalErrandKind::None);
        ++checks;
    }

    // 2. Outside capital or during global cooldown -> None.
    {
        auto in = BaseErrandInput();
        in.unreadMailCount = 1;
        in.inCapital = false;
        assert(PickCapitalErrand(in) == CapitalErrandKind::None);
        in.inCapital = true;
        in.nextGlobalTryMs = in.nowMs + 1;
        assert(PickCapitalErrand(in) == CapitalErrandKind::None);
        ++checks;
    }

    // 3. Priority order: Mailbox > Trainer > Bank > AuctionHouse, respecting per-kind cooldowns.
    {
        auto in = BaseErrandInput();
        in.unreadMailCount = 1;
        in.hasTrainableSpell = true;
        in.freeBagSlots = 2;
        in.bankableItemCount = 3;
        in.ahSellableItemCount = 2;

        assert(PickCapitalErrand(in) == CapitalErrandKind::Mailbox);

        in.mailboxCooldownUntilMs = in.nowMs + 1000;
        assert(PickCapitalErrand(in) == CapitalErrandKind::Trainer);

        in.trainerCooldownUntilMs = in.nowMs + 1000;
        assert(PickCapitalErrand(in) == CapitalErrandKind::Bank);

        in.bankCooldownUntilMs = in.nowMs + 1000;
        assert(PickCapitalErrand(in) == CapitalErrandKind::AuctionHouse);

        in.auctionEnabled = false;
        assert(PickCapitalErrand(in) == CapitalErrandKind::None);
        ++checks;
    }

    // 4. Bank item filter protects equipped, quest, grey, food/water/ammo, and tools.
    {
        assert(ShouldBankItemCapital(7, 1, false, false, false, false));   // Trade goods
        assert(ShouldBankItemCapital(9, 2, false, true, false, false));    // Recipe
        assert(ShouldBankItemCapital(2, 2, false, false, false, false));   // BoE green gear
        assert(!ShouldBankItemCapital(2, 2, true, false, false, false));   // Equipped
        assert(!ShouldBankItemCapital(12, 1, false, false, false, false)); // Quest
        assert(!ShouldBankItemCapital(7, 0, false, false, false, false));  // Poor/grey
        assert(!ShouldBankItemCapital(7, 1, false, false, false, true));   // Prof tool
        assert(!ShouldBankItemCapital(0, 1, false, false, true, false));   // Food/water/ammo
        ++checks;
    }

    // 5. Bot-vs-bot duels at the gate: requires AllowDueling, both free bots, level & HP & cooldown gates.
    {
        auto d = BaseDuelInput();
        assert(CanInitiateBotDuel(d));

        d.allowDuelingArea = false;
        assert(!CanInitiateBotDuel(d));
        d.allowDuelingArea = true;

        d.targetIsBot = false;
        assert(!CanInitiateBotDuel(d));
        d.targetIsBot = true;

        d.challengerHealthPct = 85;
        assert(!CanInitiateBotDuel(d));
        d.challengerHealthPct = 100;

        d.targetLevel = 24; // > 20 + 3
        assert(!CanInitiateBotDuel(d));
        d.targetLevel = 20;

        d.challengerCooldownUntilMs = d.nowMs + 1000;
        assert(!CanInitiateBotDuel(d));
        d.challengerCooldownUntilMs = 0;

        // Shared-clock target cooldown blocks initiation until nowMs >= targetCooldownUntilMs.
        d.targetCooldownUntilMs = d.nowMs + 1000;
        assert(!CanInitiateBotDuel(d));
        d.nowMs = d.targetCooldownUntilMs;
        assert(CanInitiateBotDuel(d));

        static_assert(kDuelScanIntervalMs > 0 && kDuelScanIntervalMs <= kDuelCooldownMs,
            "Duel scan throttle must be positive and within duel cooldown");
        ++checks;
    }

    // 6. Accepting bot duels.
    {
        assert(ShouldAcceptBotDuel(true, true, false, true, 20, 10, 95));
        assert(!ShouldAcceptBotDuel(true, true, false, false, 20, 10, 95)); // non-bot challenger
        assert(!ShouldAcceptBotDuel(false, true, false, true, 20, 10, 95)); // non-free bot
        assert(!ShouldAcceptBotDuel(true, true, false, true, 5, 10, 95));   // below min level
        ++checks;
    }

    // 7. Classic capital zone fallback recognizes Stormwind/Orgrimmar/etc. even without LinkedChat flag.
    {
        assert(IsCapitalZoneId(1519));  // Stormwind
        assert(IsCapitalZoneId(1637));  // Orgrimmar
        assert(IsCapitalZoneId(1537));  // Ironforge
        assert(IsCapitalZoneId(1657));  // Darnassus
        assert(IsCapitalZoneId(1497));  // Undercity
        assert(IsCapitalZoneId(1638));  // Thunder Bluff
        assert(!IsCapitalZoneId(12));   // Elwynn Forest
        assert(IsCapitalAreaOrZone(false, false, 1519));
        assert(IsCapitalAreaOrZone(true, false, 12));
        assert(IsCapitalAreaOrZone(false, true, 12));
        assert(!IsCapitalAreaOrZone(false, false, 12));
        ++checks;
    }

    std::printf("PASS: test_capital_errands_policy (%d checks, 0 failed)\n", checks);
    return 0;
}
