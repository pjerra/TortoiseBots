// The bot manager's two answers the resist-gear host asks: is this guid a host bot, and is the module on.
#pragma once

#include "ObjectGuid.h"

#include <set>

namespace TortoiseBots
{
class BotMgr
{
public:
    static BotMgr& Instance()
    {
        static BotMgr manager;
        return manager;
    }

    bool IsBot(ObjectGuid const& guid) const { return Bots().count(guid.GetCounter()) != 0; }
    bool Enabled() const { return true; }

    static std::set<uint64>& Bots()
    {
        static std::set<uint64> bots;
        return bots;
    }
};
}
