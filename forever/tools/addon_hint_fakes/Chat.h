// The chat surface the add-on hint uses: a system message (recorded, with an optional hook the lock test parks
// inside), a formatted system message (ignored) and the error flag.
#pragma once

#include "FakeWorld.h"
#include "WorldSession.h"

#include <string>

class ChatHandler
{
public:
    explicit ChatHandler(WorldSession* session) : session_(session) {}

    void SendSysMessage(std::string const& text)
    {
        fakeworld::sent.push_back(text);
        if (fakeworld::sendHook)
            fakeworld::sendHook();
    }

    template <class... Args>
    void PSendSysMessage(char const*, Args const&...)
    {
    }

    void SetSentErrorMessage(bool) {}

    WorldSession* GetSession() const { return session_; }

private:
    WorldSession* session_;
};
