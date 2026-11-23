// Core-free logic for the `trade` addon verb and headless trade-window state
// machine (forever/src/host/TbAddonTrade.cpp). Tested standalone by
// forever/tools/test_addon_trade.cpp.
#pragma once

#include <array>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>

namespace TortoiseBots
{
namespace trade
{
inline constexpr std::size_t kTradedSlotCount = 6;

struct ParsedTradeArgs
{
    bool valid = false;
    bool hasBotName = false;
    std::string botName;
};

inline ParsedTradeArgs ParseTradeArgs(std::string_view args)
{
    ParsedTradeArgs out;
    std::size_t i = 0;
    auto skipWs = [&]()
    {
        while (i < args.size() && std::isspace(static_cast<unsigned char>(args[i])))
            ++i;
    };
    auto nextWord = [&]() -> std::string_view
    {
        skipWs();
        std::size_t const start = i;
        while (i < args.size() && !std::isspace(static_cast<unsigned char>(args[i])))
            ++i;
        return args.substr(start, i - start);
    };

    std::string_view const first = nextWord();
    if (first.empty())
    {
        out.valid = true;
        out.hasBotName = false;
        return out;
    }

    std::string_view const extra = nextWord();
    if (!extra.empty())
        return out;

    out.valid = true;
    out.hasBotName = true;
    out.botName.assign(first);
    return out;
}

inline int FindFreeTradeSlot(std::array<bool, kTradedSlotCount> const& occupied)
{
    for (std::size_t i = 0; i < kTradedSlotCount; ++i)
    {
        if (!occupied[i])
            return static_cast<int>(i);
    }
    return -1;
}

enum class TradeTickAction
{
    None,
    Cancel,
    BeginTrade,
    AcceptTrade,
};

struct TradeTickInput
{
    bool hasTrade = false;
    bool hasPartnerTrade = false;
    std::uint64_t serial = 0;
    bool awaitingProposerBusy = false;
    bool traderAllowed = false;
    bool hisAccepted = false;
    bool myAccepted = false;
    std::uint32_t hisServerStateIndex = 0;
};

struct TradeBotState
{
    std::uint64_t lastBegunSerial = 0;
    std::uint64_t lastAcceptSerial = 0;
    std::uint32_t lastAcceptStateIndex = 0;
    bool hasAcceptAttempt = false;
};

struct TradeTickDecision
{
    TradeTickAction action = TradeTickAction::None;
    TradeBotState nextState;
};

inline TradeTickDecision DecideTradeTick(TradeTickInput const& in, TradeBotState state)
{
    TradeTickDecision out;
    if (!in.hasTrade)
    {
        out.nextState = TradeBotState{};
        return out;
    }

    if (!in.hasPartnerTrade)
    {
        out.nextState = state;
        return out;
    }

    if (!in.traderAllowed)
    {
        out.action = TradeTickAction::Cancel;
        out.nextState = state;
        return out;
    }

    if (!in.awaitingProposerBusy && state.lastBegunSerial != in.serial)
    {
        state.lastBegunSerial = in.serial;
        out.action = TradeTickAction::BeginTrade;
        out.nextState = state;
        return out;
    }

    if (in.hisAccepted && !in.myAccepted)
    {
        bool const alreadyTriedThisState = state.hasAcceptAttempt &&
            state.lastAcceptSerial == in.serial &&
            state.lastAcceptStateIndex == in.hisServerStateIndex;
        if (!alreadyTriedThisState)
        {
            state.hasAcceptAttempt = true;
            state.lastAcceptSerial = in.serial;
            state.lastAcceptStateIndex = in.hisServerStateIndex;
            out.action = TradeTickAction::AcceptTrade;
            out.nextState = state;
            return out;
        }
    }

    out.nextState = state;
    return out;
}
} // namespace trade
} // namespace TortoiseBots
