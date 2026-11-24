// One session: headless (a bot) unless the test says a player holds it. The core's WorldSession::IsBot is the flag
// its bot transport sets (WorldSession.h:1079); the reset treats any other session as a human's.
#pragma once

#include "Define.h"

class WorldSession
{
public:
    WorldSession() = default;
    explicit WorldSession(bool bot) : _bot(bot) {}
    bool IsBot() const { return _bot; }

private:
    bool _bot = false;
};
