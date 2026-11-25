// The player surface the add-on hint uses: the guid and the session.
#pragma once

#include "ObjectGuid.h"
#include "WorldSession.h"

class Player
{
public:
    ObjectGuid GetGUID() const { return guid; }
    WorldSession* GetSession() { return &session; }

    ObjectGuid guid;
    WorldSession session;
};
