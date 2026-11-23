// Standalone test of the `trade` addon verb's core-free half (forever/src/host/TbAddonTradePure.h):
// argument parsing, free trade slot selection, and the trade-window tick state machine.

#include "forever/src/host/TbAddonTradePure.h"

#include <array>
#include <cassert>
#include <iostream>

using namespace TortoiseBots::trade;

int main()
{
    int checks = 0;
    auto ok = [&](bool cond)
    {
        assert(cond);
        ++checks;
    };

    // 1. ParseTradeArgs
    {
        ParsedTradeArgs const empty = ParseTradeArgs("   ");
        ok(empty.valid && !empty.hasBotName && empty.botName.empty());

        ParsedTradeArgs const named = ParseTradeArgs("  Aelindra  ");
        ok(named.valid && named.hasBotName && named.botName == "Aelindra");

        ParsedTradeArgs const extra = ParseTradeArgs("Aelindra extra");
        ok(!extra.valid);
    }

    // 2. FindFreeTradeSlot
    {
        std::array<bool, kTradedSlotCount> occ{ false, false, false, false, false, false };
        ok(FindFreeTradeSlot(occ) == 0);
        occ[0] = true;
        occ[1] = true;
        ok(FindFreeTradeSlot(occ) == 2);
        occ = { true, true, true, true, true, true };
        ok(FindFreeTradeSlot(occ) == -1);
    }

    // 3. Stranger trade cancels immediately
    {
        TradeTickInput in;
        in.hasTrade = true;
        in.hasPartnerTrade = true;
        in.serial = 10;
        in.traderAllowed = false;
        TradeTickDecision const d = DecideTradeTick(in, TradeBotState{});
        ok(d.action == TradeTickAction::Cancel);
    }

    // 4. Player-initiated trade triggers BeginTrade once per serial
    {
        TradeTickInput in;
        in.hasTrade = true;
        in.hasPartnerTrade = true;
        in.serial = 42;
        in.awaitingProposerBusy = false;
        in.traderAllowed = true;

        TradeTickDecision const d1 = DecideTradeTick(in, TradeBotState{});
        ok(d1.action == TradeTickAction::BeginTrade);
        ok(d1.nextState.lastBegunSerial == 42);

        TradeTickDecision const d2 = DecideTradeTick(in, d1.nextState);
        ok(d2.action == TradeTickAction::None);
    }

    // 5. Bot-initiated trade does not call BeginTrade
    {
        TradeTickInput in;
        in.hasTrade = true;
        in.hasPartnerTrade = true;
        in.serial = 55;
        in.awaitingProposerBusy = true;
        in.traderAllowed = true;

        TradeTickDecision const d = DecideTradeTick(in, TradeBotState{});
        ok(d.action == TradeTickAction::None);
    }

    // 6. Owner accept triggers AcceptTrade once per (serial, hisServerStateIndex), retries on state change
    {
        TradeTickInput in;
        in.hasTrade = true;
        in.hasPartnerTrade = true;
        in.serial = 77;
        in.awaitingProposerBusy = true;
        in.traderAllowed = true;
        in.hisAccepted = true;
        in.myAccepted = false;
        in.hisServerStateIndex = 1001;

        TradeTickDecision const d1 = DecideTradeTick(in, TradeBotState{});
        ok(d1.action == TradeTickAction::AcceptTrade);

        // Same state index on next tick -> do not spin
        TradeTickDecision const d2 = DecideTradeTick(in, d1.nextState);
        ok(d2.action == TradeTickAction::None);

        // Partner modifies trade -> new state index -> AcceptTrade again
        in.hisServerStateIndex = 1002;
        TradeTickDecision const d3 = DecideTradeTick(in, d2.nextState);
        ok(d3.action == TradeTickAction::AcceptTrade);
    }

    std::cout << "PASS test_addon_trade (" << checks << " checks)\n";
    return 0;
}
