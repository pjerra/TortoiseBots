// The session surface the add-on hint uses: headless-transport test and the security level (SEC_PLAYER is the
// normal player; anything above it is staff).
#pragma once

#include "Define.h"

enum : uint8
{
    SEC_PLAYER = 0,
    SEC_MODERATOR = 1,
    SEC_GAMEMASTER = 2,
    SEC_ADMINISTRATOR = 3,
    SEC_CONSOLE = 4,
};

class WorldSession
{
public:
    bool IsBot() const { return bot; }
    int GetSecurity() const { return security; }

    bool bot = false;
    int security = SEC_PLAYER;
};
