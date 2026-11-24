// A player by guid: connected (loading or in the world) when the test put its guid into `fakeConnectedPlayers`.
#pragma once

#include "Define.h"
#include "ObjectGuid.h"
#include "Player.h"

class ObjectAccessor
{
public:
    static Player* FindConnectedPlayer(ObjectGuid guid)
    {
        static Player player;
        return fakeConnectedPlayers.count(guid.GetCounter()) ? &player : nullptr;
    }
};
