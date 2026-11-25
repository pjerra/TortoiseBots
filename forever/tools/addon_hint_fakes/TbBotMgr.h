// BotMgr as the add-on hint reads it: the engine switch and the bot test.
#pragma once

#include "FakeWorld.h"
#include "ObjectGuid.h"

class BotMgr
{
public:
    static BotMgr& Instance()
    {
        static BotMgr instance;
        return instance;
    }

    bool Enabled() const { return fakeworld::botMgrEnabled; }
    bool IsBot(ObjectGuid const& guid) const { return fakeworld::bots.count(guid.GetCounter()) != 0; }
};
