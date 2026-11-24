// The session table: one session per account, headless (a bot) unless the test says a player holds it.
#pragma once

#include "Define.h"
#include "Player.h"

#include <map>

inline std::map<uint32, WorldSession> fakeSessions;

class World
{
public:
    WorldSession* FindSession(uint32 accountId)
    {
        auto const found = fakeSessions.find(accountId);
        return found == fakeSessions.end() ? nullptr : &found->second;
    }
};

inline World fakeWorld;
inline World* sWorld = &fakeWorld;
