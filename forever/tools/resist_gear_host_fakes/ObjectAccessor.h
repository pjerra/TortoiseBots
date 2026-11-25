// The world's connected players, as ObjectAccessor::FindConnectedPlayer answers them for the host's recovery paths.
#pragma once

#include "ObjectGuid.h"

#include <map>

class Player;

inline std::map<uint64, Player*>& ConnectedPlayers()
{
    static std::map<uint64, Player*> players;
    return players;
}

class ObjectAccessor
{
public:
    static Player* FindConnectedPlayer(ObjectGuid const& guid)
    {
        auto const it = ConnectedPlayers().find(guid.GetCounter());
        return it == ConnectedPlayers().end() ? nullptr : it->second;
    }
};
